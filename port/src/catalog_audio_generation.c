#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <SDL.h>
#include "constants.h"
#include "assetcatalog.h"
#include "assetcatalog_sound_slots.h"
#include "assetprovider.h"
#include "catalog_audio_generation.h"
#include "modmusic.h"
#include "fs.h"
#include "sha256.h"
#include "system.h"

struct catalog_audio_generation {
    struct catalog_audio_generation *next;
    char id[CATALOG_ID_LEN], hash[65];
    u32 references, frames;
    s32 slot, source_rate;
    s16 *pcm;
    audio_sample_parameters_t parameters;
};
static catalog_audio_generation_t *s_Generations, *s_BySlot[SND_CUSTOM_END];
static SDL_threadID s_AudioThread;
static s32 onAudioThread(void)
{
    const SDL_threadID current = SDL_ThreadID();
    if (!s_AudioThread) s_AudioThread = current;
    if (current == s_AudioThread) return 1;
    sysLoudFailf("AUDIO.GENERATION.THREAD", "audio generation access outside its client thread");
    return 0;
}
static void errorText(char *error, size_t cap, const char *text)
{
    if (error && cap) snprintf(error, cap, "%s", text);
}
typedef struct audio_selection {
    const char *id;
    asset_entry_t entry;
    char path[FS_MAXPATH + 1];
    s32 found;
} audio_selection_t;
static void captureAudio(const asset_entry_t *entry, void *userdata)
{
    audio_selection_t *selected = userdata;
    if (strcmp(entry->id, selected->id) || (entry->ext.audio.category != AUDIO_CAT_SFX
            && entry->ext.audio.category != AUDIO_CAT_VOICE)) return;
    const asset_data_handle_t source = catalogEffectiveHandle(entry);
    if (source.provider != fileProvider()) return;
    const char *path = fileProviderPath(source);
    if (!path || !path[0] || strlen(path) >= sizeof(selected->path)) return;
    selected->entry = *entry;
    strcpy(selected->path, path);
    selected->found = 1;
}
static void hashU32(sha256_ctx *hash, u32 value)
{
    const u8 bytes[] = {(u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    sha256Update(hash, bytes, sizeof(bytes));
}
static void hashSource(catalog_audio_generation_t *g, const void *source, u32 size, s32 category, const char *path)
{
    sha256_ctx hash;
    u8 digest[32];
    u32 pitch;
    const char version[] = "pd.audio.generation.v1";
    sha256Init(&hash);
    sha256Update(&hash, version, sizeof(version));
    const char *extension = strrchr(path, '.');
    hashU32(&hash, extension && !SDL_strcasecmp(extension, ".mp3") ? 2
        : extension && !SDL_strcasecmp(extension, ".ogg") ? 3 : 1);
    hashU32(&hash, size);
    sha256Update(&hash, source, size);
    hashU32(&hash, (u32)category);
    memcpy(&pitch, &g->parameters.base_pitch, sizeof(pitch));
    hashU32(&hash, pitch);
#define FIELD(name) hashU32(&hash, (u32)g->parameters.name)
    FIELD(sample_pan); FIELD(sample_volume); FIELD(key_volume_index); FIELD(fxmix_key_offset);
    FIELD(has_loop); FIELD(loop_start_samples); FIELD(loop_end_samples); FIELD(loop_count);
    FIELD(has_envelope); FIELD(attack_time_us); FIELD(decay_time_us); FIELD(release_time_us);
    FIELD(attack_volume); FIELD(decay_volume);
#undef FIELD
    sha256Final(&hash, digest);
    sha256ToHex(digest, g->hash);
}
catalog_audio_generation_t *catalogAudioGenerationAcquire(const char *id, char *error, size_t cap)
{
    if (error && cap) error[0] = 0;
    if (!onAudioThread() || !id || !strchr(id, ':') || strlen(id) >= CATALOG_ID_LEN) {
        errorText(error, cap, "invalid audio generation identity"); return NULL;
    }
    audio_selection_t *selected = calloc(1, sizeof(*selected));
    catalog_audio_generation_t *g = calloc(1, sizeof(*g));
    void *bytes = NULL;
    u32 size = 0, samples = 0;
    if (!selected || !g) { errorText(error, cap, "audio generation allocation failed"); goto fail; }
    strcpy(g->id, id); selected->id = g->id;
    assetCatalogIterateByType(ASSET_AUDIO, captureAudio, selected);
    if (!selected->found || !(bytes = fsFileLoad(selected->path, &size)) || !size) {
        errorText(error, cap, "selected public audio source unavailable"); goto fail;
    }
    audioSampleParametersFromCatalog(&selected->entry, &g->parameters);
    if (!(g->parameters.base_pitch >= -FLT_MAX && g->parameters.base_pitch <= FLT_MAX)) {
        errorText(error, cap, "audio source pitch is not finite"); goto fail;
    }
    g->pcm = modMusicDecodeAudioPcm22050(selected->path, bytes, size, &samples, &g->source_rate);
    if (!g->pcm || samples < 2 || (samples & 1u) || g->source_rate <= 0) {
        errorText(error, cap, "selected public audio bytes could not decode"); goto fail;
    }
    g->frames = samples / 2;
    hashSource(g, bytes, size, selected->entry.ext.audio.category, selected->path);
    for (catalog_audio_generation_t *old = s_Generations; old; old = old->next) {
        if (strcmp(old->id, g->id) || strcmp(old->hash, g->hash)) continue;
        catalogAudioGenerationRetain(old);
        SDL_free(g->pcm); free(g); free(bytes); free(selected);
        return old;
    }
    g->slot = assetCatalogReserveSoundGenerationSlot();
    if (g->slot < 0) { errorText(error, cap, "no private native sound leaf slot available"); goto fail; }
    g->references = 1;
    g->next = s_Generations; s_Generations = g; s_BySlot[g->slot] = g;
    free(bytes); free(selected);
    return g;
fail:
    if (g) SDL_free(g->pcm);
    free(g); free(bytes); free(selected);
    return NULL;
}
void catalogAudioGenerationRetain(catalog_audio_generation_t *g)
{
    if (!g || !onAudioThread()) return;
    if (g->references == UINT_MAX) {
        sysLoudFailf("AUDIO.GENERATION.REFCOUNT", "audio generation reference count overflow"); return;
    }
    ++g->references;
}
void catalogAudioGenerationRelease(catalog_audio_generation_t *g)
{
    if (!g || !onAudioThread() || --g->references) return;
    catalog_audio_generation_t **link = &s_Generations;
    while (*link && *link != g) link = &(*link)->next;
    if (*link) *link = g->next;
    s_BySlot[g->slot] = NULL;
    assetCatalogReleaseSoundGenerationSlot(g->slot);
    SDL_free(g->pcm); free(g);
}
s32 catalogAudioGenerationSlot(const catalog_audio_generation_t *g) { return g ? g->slot : -1; }
const char *catalogAudioGenerationHash(const catalog_audio_generation_t *g) { return g ? g->hash : NULL; }
const s16 *catalogAudioGenerationPcm(const catalog_audio_generation_t *g, u32 *frames, s32 *rate)
{
    if (frames) *frames = g ? g->frames : 0;
    if (rate) *rate = g ? g->source_rate : 0;
    return g ? g->pcm : NULL;
}
catalog_audio_generation_t *catalogAudioGenerationForSlot(s32 slot)
{
    if (slot < SND_CUSTOM_START || slot >= SND_CUSTOM_END || !onAudioThread()) return NULL;
    return s_BySlot[slot];
}
struct sndstate *catalogAudioGenerationStart(const catalog_audio_generation_t *g,
    u16 volume, u8 pan, f32 pitch, u8 fxmix, u8 fxbus, struct sndstate **handle)
{
    if (!g || !onAudioThread()) return NULL;
    return audioStartPcmSound(g->pcm, g->frames, g->source_rate, &g->parameters,
        volume, pan, pitch, fxmix, fxbus, handle);
}
