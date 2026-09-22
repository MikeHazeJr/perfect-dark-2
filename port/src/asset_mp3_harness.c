#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "types.h"
#include "bss.h"
#include "data.h"
#include "asset_mp3_harness.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "lib/mp3.h"
#include "lib/snd.h"
#include "mixer.h"
#include "modmusic.h"
#include "smoke_harness.h"
#include "system.h"

static s32 mp3PullForSmoke(void)
{
    Acmd commands[32], *cursor = commands;
    aClearBufferImpl(0, 3072);
    return func00037fc0(184, &cursor);
}

static s32 mp3DelayForSmoke(s32 state)
{
    for (s32 i = 0; i < 5; ++i)
        if (func00037ea4() != state || mp3PullForSmoke() != 0) return 0;
    return func00037ea4() == state;
}

static s32 mp3DrainForSmoke(u32 frames, u32 consumed)
{
    u32 pulls = consumed;
    s16 left[184], right[184];
    s32 nonzero = 0;
    while (func00037ea4() && pulls <= (frames + 183u) / 184u) {
        if (mp3PullForSmoke() != 1) return 0;
        aSaveBufferImpl(0x4e0, left, sizeof(left));
        aSaveBufferImpl(0x650, right, sizeof(right));
        for (u32 i = 0; i < 184; ++i) nonzero |= left[i] != 0 || right[i] != 0;
        ++pulls;
    }
    if (func00037ea4() != 0 || pulls != (frames + 183u) / 184u || !nonzero) return 0;
    if (frames % 184u)
        for (u32 i = frames % 184u; i < 184; ++i)
            if (left[i] || right[i]) return 0;
    return mp3PullForSmoke() == 0;
}

static s16 mp3SoundForSmoke(s32 file, s32 priority)
{
    union soundnumhack sound = {0};
    sound.id = file;
    sound.mp3priority = priority;
    return sound.packed;
}

static s32 mp3CheckForSmoke(const char *name, s32 ok)
{
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.MP3: case=%s result=%s", name, ok ? "PASS" : "FAIL");
    return ok;
}

/* A positive tick delta makes an accidentally armed response observable while
 * data or a paused owner is still live. This is the real scheduler tick. */
static s32 mp3TickOwnerForSmoke(const asset_mp3_witness_t *owner, s32 state)
{
    asset_mp3_witness_t current = {0};
    sndTick();
    return func00037ea4() == state && sndMp3HarnessWitness(&current)
        && current.playing && current.pcm == owner->pcm
        && current.frames == owner->frames && current.soundnum == owner->soundnum
        && current.response_timer240 == -1;
}

/* The normal game boot has initialized the real sound player and MP3 state.
 * SDL's dummy device prevents physical playback. Synthesis is synchronous in
 * the PC port; these direct pulls exercise the same main-bus entry point. */
int assetSourceMp3HarnessRun(void)
{
    s32 files[2] = {-1, -1}, found = 0, passed = 0, ok = 0;
    asset_mp3_witness_t first = {0}, witness = {0};
    s16 *expected = NULL;
    u32 samples = 0;
    s32 saved_update = g_Vars.lvupdate240;
    u16 saved_sfx = 0, saved_effective_sfx = 0;
    const char *driver = SDL_GetCurrentAudioDriver();
    if (!smokeHarnessIsActive() || !sndMp3HarnessWitness(&first)
            || !driver || strcmp(driver, "dummy")) {
        sysLogPrintf(LOG_ERROR, "ASSET.SOURCE.MP3: requires initialized normal sound with SDL_AUDIODRIVER=dummy result=FAIL");
        return -1;
    }
    saved_sfx = g_SfxVolume;
    saved_effective_sfx = snd0000e9dc();
    /* Exercise a deterministic nonzero main-bus gain, even when the installed
     * profile is muted. Restore both native levels on every exit below. */
    sndSetSfxVolume(0x5000);
    g_Vars.lvupdate240 = 4;
    for (s32 i = 0; i < assetCatalogGetPoolSize() && found < 2; ++i) {
        const asset_entry_t *entry = assetCatalogGetByIndex(i);
        if (!entry || !entry->occupied || !entry->enabled || entry->type != ASSET_AUDIO
                || entry->ext.audio.category != AUDIO_CAT_VOICE || entry->source_filenum <= 0
                || entry->source_filenum > 2047 || (found && files[0] == entry->source_filenum)) continue;
        CatalogResolveResult source = catalogResolveFile(entry->source_filenum);
        const char *extension = source.path ? strrchr(source.path, '.') : NULL;
        if (source.catalog_id == i && extension && !SDL_strcasecmp(extension, ".mp3"))
            files[found++] = entry->source_filenum;
    }
    if (!mp3CheckForSmoke("extracted_native_sources", found == 2)) goto done;
    ++passed;
    CatalogResolveResult source = catalogResolveFile(files[0]);
    expected = modMusicLoadAudioPcm22050(source.path, &samples, NULL);
    if (!expected || samples < 736 || samples % 2u) goto done;
    sndResetCurMp3();
    sndStartMp3(mp3SoundForSmoke(files[0], 2), 0x7fff, 64, 0);
    if (!sndMp3HarnessWitness(&first) || !mp3CheckForSmoke("actual_source_pcm",
            first.playing && first.frames == samples / 2u && first.pcm
            && !memcmp(first.pcm, expected, samples * sizeof(s16)))) goto done;
    ++passed;
    if (!mp3CheckForSmoke("five_pull_start_delay", mp3DelayForSmoke(4) && mp3PullForSmoke() == 1)) goto done;
    ++passed;
    if (!mp3TickOwnerForSmoke(&first, 1)) goto done;
    snd0000fe20();
    for (s32 i = 0; i < 3; ++i)
        if (func00037ea4() != 2 || mp3PullForSmoke() != 0
                || !mp3TickOwnerForSmoke(&first, 2)) goto done;
    snd0000fe50();
    if (!mp3CheckForSmoke("pause_resume_pull_count_zero_padding", mp3DelayForSmoke(5)
            && mp3DrainForSmoke(first.frames, 1))) goto done;
    ++passed;
    sndTick();
    if (!sndMp3HarnessWitness(&witness) || !mp3CheckForSmoke("completion_releases_owner",
            !witness.playing && !witness.pcm && witness.frames == 0)) goto done;
    ++passed;

    sndStartMp3(mp3SoundForSmoke(files[0], 1), 0x7fff, 64, 0);
    if (!sndMp3HarnessWitness(&first)) goto done;
    sndStartMp3(mp3SoundForSmoke(files[1], 1), 0x7fff, 64, 0);
    if (!sndMp3HarnessWitness(&witness) || !mp3CheckForSmoke("equal_high_priority_rejected",
            witness.playing && witness.pcm == first.pcm && witness.soundnum == first.soundnum)) goto done;
    ++passed;
    if (!sndStopMp3(0)) goto done;
    sndStartMp3(mp3SoundForSmoke(files[0], 3), 0x7fff, 64, 0);
    sndStartMp3(mp3SoundForSmoke(files[1], 2), 0x7fff, 64, 0);
    if (!sndMp3HarnessWitness(&witness) || !mp3CheckForSmoke("higher_priority_replaces",
            witness.playing && witness.soundnum == mp3SoundForSmoke(files[1], 2)
            && witness.frames > 0 && witness.pcm)) goto done;
    ++passed;
    if (!sndStopMp3(0) || !sndMp3HarnessWitness(&witness)
            || !mp3CheckForSmoke("explicit_stop_releases", !witness.playing && !witness.pcm
                && !witness.frames && func00037ea4() == 0)) goto done;
    ++passed;

    sndStartMp3(mp3SoundForSmoke(files[0], 2), 0x7fff, 64, 0);
    if (!sndMp3HarnessWitness(&first) || !sndMp3HarnessRepeat(1)
            || !mp3DelayForSmoke(4) || !mp3DrainForSmoke(first.frames, 0)) goto done;
    sndTick();
    if (!sndMp3HarnessWitness(&witness) || !witness.playing
            || witness.pcm != first.pcm || witness.frames != first.frames
            || func00037ea4() != 4 || sndStopMp3(0)) goto done;
    /* Consume the retained source a second time, including its start delay.
     * EOF stops the borrow; scheduler ownership must remain until the tick. */
    if (!mp3DelayForSmoke(4) || !mp3DrainForSmoke(first.frames, 0)
            || !sndMp3HarnessWitness(&witness)
            || !mp3CheckForSmoke("repeat_replays_retained_pcm",
                witness.playing && witness.pcm == first.pcm
                && witness.frames == first.frames && func00037ea4() == 0)) goto done;
    ++passed;
    sndMp3HarnessRepeat(0);
    sndStopMp3(0);

    sndStartMp3(mp3SoundForSmoke(files[0], 2), 0x7fff, 64, 1);
    if (!sndMp3HarnessWitness(&first) || first.response_timer240 != -1
            || !mp3DelayForSmoke(4) || mp3PullForSmoke() != 1
            || !mp3TickOwnerForSmoke(&first, 1)) goto done;
    snd0000fe20();
    for (s32 i = 0; i < 3; ++i)
        if (func00037ea4() != 2 || mp3PullForSmoke() != 0
                || !mp3TickOwnerForSmoke(&first, 2)) goto done;
    snd0000fe50();
    if (!mp3DelayForSmoke(5) || !mp3DrainForSmoke(first.frames, 1)) goto done;
    sndTick();
    if (!sndMp3HarnessWitness(&witness) || witness.playing || witness.response_timer240 != 1) goto done;
    g_Vars.lvupdate240 = 4;
    sndTick();
    if (!sndMp3HarnessWitness(&witness) || !mp3CheckForSmoke("response_only_after_eof",
            witness.playing && witness.response_timer240 == -1 && witness.pcm && witness.frames)) goto done;
    ++passed;
    ok = 1;
done:
    sndMp3HarnessRepeat(0);
    sndStopMp3(0);
    sndResetCurMp3();
    sndSetSfxVolume(saved_sfx);
    snd0000ea80(saved_effective_sfx);
    g_Vars.lvupdate240 = saved_update;
    SDL_free(expected);
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.MP3: cases=%d result=%s", passed, ok ? "PASS" : "FAIL");
    return ok ? 0 : -1;
}
