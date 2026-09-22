#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "asset_audio_public_source_harness.h"
#include "asset_archive_writer.h"
#include "asset_path_contract.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetprovider.h"
#include "loader_walker.h"
#include "modmusic.h"
#include "fs.h"
#include "smoke_harness.h"
#include "system.h"

enum { FRAMES = 64, WAV_SIZE = 44 + FRAMES * 4 };
typedef struct target { char id[CATALOG_ID_LEN]; s32 sound, file, category, sound_id; } target_t;
static void u16le(u8 *p, u16 n) { p[0] = (u8)n; p[1] = (u8)(n >> 8); }
static void u32le(u8 *p, u32 n) { for (s32 i = 0; i < 4; i++) p[i] = (u8)(n >> (8 * i)); }
static s16 sample(s32 frame, s32 channel, s32 phase) { return (s16)(phase * 100 + frame * 3 - channel * 900); }
static void wave(u8 *p, s32 phase)
{
    memset(p, 0, WAV_SIZE);
    memcpy(p, "RIFF", 4); u32le(p + 4, WAV_SIZE - 8); memcpy(p + 8, "WAVEfmt ", 8);
    u32le(p + 16, 16); u16le(p + 20, 1); u16le(p + 22, 2); u32le(p + 24, 22050);
    u32le(p + 28, 88200); u16le(p + 32, 4); u16le(p + 34, 16);
    memcpy(p + 36, "data", 4); u32le(p + 40, FRAMES * 4);
    for (s32 i = 0; i < FRAMES; i++) for (s32 ch = 0; ch < 2; ch++)
        u16le(p + 44 + i * 4 + ch * 2, (u16)sample(i, ch, phase));
}
static int directory(const char *root, const char *leaf, char *out)
{
    return assetPathJoinChecked(out, FS_MAXPATH, root, "/", leaf) && fsCreateDir(out);
}
static int selectTargets(target_t out[2])
{
    memset(out, 0, sizeof(target_t) * 2);
    for (s32 i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *row = assetCatalogGetByIndex(i);
        s32 slot = -1;
        if (!row || row->type != ASSET_AUDIO || !row->bundled || !row->enabled) continue;
        if (row->ext.audio.category == AUDIO_CAT_SFX && row->source_soundnum > 1 && row->source_filenum < 0) slot = 0;
        if (row->ext.audio.category == AUDIO_CAT_VOICE && row->source_filenum > 0) slot = 1;
        if (slot < 0 || out[slot].id[0]) continue;
        CatalogResolveResult route = slot ? catalogResolveFile(row->source_filenum) : catalogResolveSound(row->source_soundnum);
        if (assetCatalogGetByIndex(route.catalog_id) != row || !route.path || route.source_only_blocked) continue;
        strcpy(out[slot].id, row->id);
        out[slot].sound = row->source_soundnum; out[slot].file = row->source_filenum;
        out[slot].category = row->ext.audio.category; out[slot].sound_id = row->ext.audio.sound_id;
    }
    return out[0].id[0] && out[1].id[0];
}
static int writeArchive(const char *tier, const target_t *target, s32 phase, s32 reject)
{
    char audio[FS_MAXPATH], kinddir[FS_MAXPATH], path[FS_MAXPATH];
    char ini[2048], manifest[1024];
    u8 wav[WAV_SIZE];
    const char *kind = target->category == AUDIO_CAT_VOICE ? "voice" : "sfx";
    const char *leaf = target->category == AUDIO_CAT_VOICE ? "voice.ini" : "sound.ini";
    mod_archive_writer_t *archive = NULL;
    asset_archive_writer_t *writer = NULL;
    int ok = 0;
    if (!directory(tier, "audio", audio) || !directory(audio, kind, kinddir)
        || !assetPathJoinChecked(path, sizeof(path), kinddir, "/", target->category == AUDIO_CAT_VOICE ? "source.pdvoice" : "source.pdsfx")) return 0;
    int n = snprintf(ini, sizeof(ini),
        "[%s]\ncatalog_id = %s\nfile_path = %s\n"
        "sample_pan = %s\nsample_volume = 91\nkey_base = 57\nkey_detune = -12\n"
        "loop_start_samples = 3\nloop_end_samples = 20\nloop_count = -1\n"
        "attack_time_us = 100\ndecay_time_us = 200\nrelease_time_us = 300\nattack_volume = 120\ndecay_volume = 80\n"
        "actor = Public actor\ntranscript = Public edited words\nlanguage = en\ncontext = public source test\n"
        "source_soundnum = 1\nsource_filenum = 1\n",
        kind, target->id, reject == 2 ? "sample.mp3" : "sample.wav",
        reject == 1 ? "31junk" : phase == 1 ? "31" : "99");
    if (n <= 0 || n >= (int)sizeof(ini)) return 0;
    n = snprintf(manifest, sizeof(manifest),
        "{\"pd_kind\":\"%s\",\"pd_schema_version\":1,\"id\":\"%s\","
        "\"source_index\":%d,\"source_filenum\":%d,\"data\":\"private-stale.wav\","
        "\"sample_rate_hz\":1,\"decoded_sample_count\":1,\"sample_pan\":1,\"key_base\":1,"
        "\"loop_start_samples\":0,\"loop_end_samples\":1,\"loop_count\":0,\"actor\":\"Private stale actor\"}",
        kind, target->id, target->sound, target->file);
    if (n <= 0 || n >= (int)sizeof(manifest)) return 0;
    wave(wav, phase);
    archive = modArchiveBegin(path); writer = calloc(1, sizeof(*writer));
    if (!archive || !writer) goto done;
    if (assetArchiveWriterInit(writer, archive, kind, target->id) != MODARCHIVE_OK
        || assetArchiveWriterAddDescriptor(writer, leaf, ini, (u32)strlen(ini)) != MODARCHIVE_OK
        || assetArchiveWriterAddManifestJson(writer, manifest, (u32)strlen(manifest)) != MODARCHIVE_OK
        || assetArchiveWriterAddPublicMem(writer, "sample.wav", wav, WAV_SIZE, "audio") != MODARCHIVE_OK
        || assetArchiveWriterFinishMetadata(writer) != MODARCHIVE_OK) goto done;
    ok = modArchiveFinish(archive) == MODARCHIVE_OK; archive = NULL;
done:
    if (archive) modArchiveAbort(archive);
    free(writer);
    return ok;
}
static int verify(const target_t *target, s32 phase, s32 active)
{
    const asset_entry_t *row = assetCatalogResolve(target->id);
    if (!row || row->type != ASSET_AUDIO || row->ext.audio.category != target->category
        || row->source_soundnum != target->sound || row->source_filenum != target->file
        || row->ext.audio.sound_id != target->sound_id || row->ext.audio.sample_pan != (phase == 1 ? 31 : 99)
        || row->ext.audio.sample_volume != 91 || row->ext.audio.key_base != 57 || row->ext.audio.key_detune != -12
        || !row->ext.audio.has_loop || row->ext.audio.loop_count != 0xffffffffu
        || row->ext.audio.loop_start_samples != 3 || row->ext.audio.loop_end_samples != 20
        || !row->ext.audio.has_envelope || row->ext.audio.attack_volume != 120
        || row->ext.audio.decay_volume != 80 || row->ext.audio.attack_time_us != 100
        || row->ext.audio.decay_time_us != 200 || row->ext.audio.release_time_us != 300
        || strcmp(row->ext.audio.voice_actor, "Public actor")
        || strcmp(row->ext.audio.voice_transcript, "Public edited words")) return 0;
    if (active && (!row->stage_ref_count || row->loaded_data != row)) return 0;
    CatalogResolveResult route = target->file > 0 ? catalogResolveFile(target->file) : catalogResolveSound(target->sound);
    if (!route.path || assetCatalogGetByIndex(route.catalog_id) != row || route.source_only_blocked) return 0;
    u32 count = 0; s32 rate = 0;
    s16 *pcm = modMusicLoadAudioPcm22050(route.path, &count, &rate);
    int ok = pcm && count == FRAMES * 2 && rate == 22050;
    for (u32 i = 0; ok && i < count; i++) if (pcm[i] != sample(i / 2, i % 2, phase)) ok = 0;
    SDL_free(pcm);
    return ok;
}
static int walk(const char *tier, s32 category, s32 success)
{
    loader_walker_kind_result_t result = {0};
    if (category == AUDIO_CAT_VOICE) loaderWalkerScanVoices(tier, &result);
    else loaderWalkerScanSfx(tier, &result);
    return result.entries_scanned == 1 && result.envelope_failures == 0
        && result.entries_registered == success && result.register_failures == !success;
}
static int report(const char *name, int ok)
{
    sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.PUBLIC: case=%s result=%s", name, ok ? "PASS" : "FAIL");
    return ok;
}
static int basePackedKeymapSource(void)
{
    /* This real extracted explosion-profile dependency was one of the 208
     * rows rejected by MIDI range checks. Check the production reverse route
     * and admitted controls before any synthetic same-ID replacement. */
    const asset_entry_t *row = assetCatalogResolve("base:sfx_unlabeled_avrm");
    if (!row || row->type != ASSET_AUDIO || !row->enabled || !row->bundled ||
            row->ext.audio.category != AUDIO_CAT_SFX || row->source_soundnum < 0 ||
            row->source.primary.provider != fileProvider() || !row->ext.audio.has_keymap ||
            row->ext.audio.key_min != 5 || row->ext.audio.key_max != 0 ||
            row->ext.audio.velocity_min != 176 || row->ext.audio.velocity_max != 16 ||
            row->ext.audio.key_base != 54 || row->ext.audio.sample_pan != 64 ||
            row->ext.audio.sample_volume != 71) return 0;
    /* The configured identity must resolve without truncating to its leaf.
     * Invalid wider/signed inputs must not wrap onto an admitted source. */
    if (row->source_soundnum != 0x80a0
            || catalogResolveSound(-1).catalog_id >= 0
            || catalogResolveSound(65536).catalog_id >= 0
            || catalogResolveSound(row->source_soundnum + 65536).catalog_id >= 0) return 0;
    CatalogResolveResult route = catalogResolveSound(row->source_soundnum);
    const char *selected = fileProviderPath(row->source.primary);
    if (!route.is_mod_override || route.source_only_blocked || !route.path || !selected ||
            assetCatalogGetByIndex(route.catalog_id) != row || strcmp(route.path, selected) ||
            !strstr(route.path, "base_sfx_unlabeled_avrm.pdsfx::sample.wav")) return 0;
    u32 count = 0;
    s32 rate = 0;
    s16 *pcm = modMusicLoadAudioPcm22050(route.path, &count, &rate);
    /* PCM is resampled to 22050; the API reports the original source rate. */
    s32 ok = pcm && count > 0 && !(count & 1) && rate == 44100;
    SDL_free(pcm);
    if (ok) sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.PUBLIC: witness=base_packed_keymap "
        "id=%s source_soundnum=%d key_min=5 key_max=0 velocity_min=176 velocity_max=16 "
        "provider=FileProvider decoded_samples=%u result=PASS", row->id, row->source_soundnum, count);
    return ok;
}
int assetAudioPublicSourceHarnessRun(void)
{
    target_t targets[2];
    char root[FS_MAXPATH], tier[FS_MAXPATH];
    int passed = 0, active = 0;
    if (!smokeHarnessIsActive()) return -1;
    if (!report("base_packed_keymap_source_controls_and_provider", basePackedKeymapSource())) goto done;
    passed++;
    fsFullPath("$S/audio-public-source-smoke", root, sizeof(root));
    if (!fsCreateDir(root) || !selectTargets(targets) || !directory(root, "first", tier)) goto done;
    if (!report("sfx_walker_public_source_controls",
        writeArchive(tier, &targets[0], 1, 0) && walk(tier, AUDIO_CAT_SFX, 1) && verify(&targets[0], 1, 0))) goto done;
    passed++;
    if (!report("voice_walker_public_source_and_metadata",
        writeArchive(tier, &targets[1], 1, 0) && walk(tier, AUDIO_CAT_VOICE, 1) && verify(&targets[1], 1, 0))) goto done;
    passed++;
    if (!report("walker_replacement_ignores_stale_private_echoes",
        directory(root, "second", tier) && writeArchive(tier, &targets[0], 2, 0)
        && walk(tier, AUDIO_CAT_SFX, 1) && verify(&targets[0], 2, 0))) goto done;
    passed++;
    active = catalogLoadStageAsset(ASSET_AUDIO, targets[0].id);
    if (!report("malformed_public_control_preserves_live_row",
        active && directory(root, "bad-control", tier) && writeArchive(tier, &targets[0], 3, 1)
        && walk(tier, AUDIO_CAT_SFX, 0) && verify(&targets[0], 2, 1))) goto done;
    passed++;
    if (!report("missing_public_member_preserves_live_row",
        directory(root, "missing-member", tier) && writeArchive(tier, &targets[0], 3, 2)
        && walk(tier, AUDIO_CAT_SFX, 0) && verify(&targets[0], 2, 1))) goto done;
    passed++;
done:
    if (active) catalogReleaseStageAsset(ASSET_AUDIO, targets[0].id);
    sysLogPrintf(LOG_NOTE, "ASSET.AUDIO.PUBLIC: passed=%d cases=6 result=%s", passed, passed == 6 ? "PASS" : "FAIL");
    return passed == 6 ? 0 : -1;
}
