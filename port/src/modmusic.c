/**
 * modmusic.c -- Mod music stream playback (Batch A-2)
 *
 * Parallel PCM playback path for mod music tracks. Runs alongside the
 * existing N64 ADPCM/sequencer pipeline. Audio is loaded from WAV files,
 * decoded to S16 stereo at device sample rate (22050 Hz), and mixed into
 * the output buffer in audioEndFrame() via modMusicMixInto().
 *
 * Volume chain: g_AudioMasterVolume * g_AudioMusicVolume * s_ModMusicVolume
 * This ensures the player's Master and Music sliders affect mod tracks.
 *
 * When mod music is active, the base N64 sequencer is silenced via
 * musicSetVolume(0) at the audio.c level (our own volume control,
 * NOT the legacy sequencer mute). Restored on stop.
 *
 * Design doc: context/designs/audio-mod-menu-design.md section 3.3
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "modmusic.h"
#include "audio.h"
#include "system.h"
#include "fs.h"
#include "external/minimp3.h"
#include "external/stb_vorbis.h"

/* ========================================================================
 * State
 * ======================================================================== */

static s16 *s_ModMusicPCM  = NULL;  /* decoded PCM buffer (S16 stereo) */
static u32  s_ModMusicLen  = 0;     /* buffer length in samples (L+R pairs = frames * 2) */
static u32  s_ModMusicPos  = 0;     /* current playback position in samples (integer floor of fractional cursor) */
static f64  s_ModMusicPosFrac = 0.0; /* sub-sample fraction in [0,1) for rate-adjusted playback */
static s32  s_ModMusicPlaying = 0;  /* 1 = playing, 0 = stopped */
static f32  s_ModMusicVolume  = 1.0f; /* mod-specific volume 0.0 - 1.0 */
static f32  s_ModMusicRateMul = 1.0f; /* Issue 4b: playback rate multiplier, clamped [0.97, 1.03] */
static u32  s_ModMusicSampleRate = 22050; /* device sample rate for ms<->sample conversion */

/* Forward declaration for base music volume control */
extern void musicSetVolume(u16 volume);

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

static inline f32 modmusic_clampf(f32 v, f32 lo, f32 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * Load a WAV file and convert it to the device format (22050 Hz, S16, stereo).
 * Returns a malloc'd buffer of S16 samples on success, NULL on failure.
 * *outLen receives the total number of s16 samples (frames * 2).
 */
static s16 *modmusic_loadWav(const char *path, u32 *outLen)
{
    SDL_AudioSpec wavSpec;
    Uint8 *wavBuf = NULL;
    Uint32 wavRawLen = 0;
    SDL_AudioCVT cvt;
    s32 cvtResult;
    Uint8 *pcm;
    Uint32 pcmLen;

    *outLen = 0;

    if (SDL_LoadWAV(path, &wavSpec, &wavBuf, &wavRawLen) == NULL) {
        sysLogPrintf(LOG_WARNING, "modmusic: failed to load WAV '%s': %s",
                     path, SDL_GetError());
        return NULL;
    }

    /* Convert to device format: 22050 Hz, AUDIO_S16SYS, 2 channels */
    cvtResult = SDL_BuildAudioCVT(&cvt,
        wavSpec.format, wavSpec.channels, wavSpec.freq,
        AUDIO_S16SYS, 2, 22050);

    if (cvtResult < 0) {
        sysLogPrintf(LOG_WARNING, "modmusic: SDL_BuildAudioCVT failed for '%s': %s",
                     path, SDL_GetError());
        SDL_FreeWAV(wavBuf);
        return NULL;
    }

    if (cvtResult > 0) {
        /* Conversion required */
        Uint32 cvtBufLen = wavRawLen * (Uint32)cvt.len_mult;
        pcm = (Uint8 *)SDL_malloc(cvtBufLen);
        if (!pcm) {
            SDL_FreeWAV(wavBuf);
            return NULL;
        }
        memcpy(pcm, wavBuf, wavRawLen);
        SDL_FreeWAV(wavBuf);
        cvt.buf = pcm;
        cvt.len = (int)wavRawLen;
        if (SDL_ConvertAudio(&cvt) < 0) {
            sysLogPrintf(LOG_WARNING, "modmusic: SDL_ConvertAudio failed for '%s': %s",
                         path, SDL_GetError());
            SDL_free(pcm);
            return NULL;
        }
        pcmLen = (Uint32)cvt.len_cvt;
    } else {
        /* Already in target format */
        pcm = wavBuf;
        pcmLen = wavRawLen;
    }

    *outLen = pcmLen / sizeof(s16); /* total S16 samples */
    return (s16 *)pcm;
}

/* ========================================================================
 * A-6: MP3 decoding via minimp3
 * ======================================================================== */

/**
 * Load an MP3 file and decode to S16 PCM at 22050 Hz stereo.
 * Uses minimp3 frame-by-frame decoding, then SDL_AudioCVT for resampling.
 * Returns a malloc'd buffer, NULL on failure. *outLen = total S16 samples.
 */
static s16 *modmusic_loadMp3(const char *path, u32 *outLen)
{
    FILE *f;
    long fsize;
    u8 *mp3data;
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    mp3d_sample_t frame_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    s16 *accum = NULL;
    u32 accumLen = 0;   /* total S16 samples written */
    u32 accumCap = 0;   /* allocated capacity in S16 samples */
    u32 mp3pos = 0;
    s32 mp3remaining;
    s32 srcRate = 0, srcCh = 0;

    *outLen = 0;

    f = fopen(path, "rb");
    if (!f) {
        sysLogPrintf(LOG_WARNING, "modmusic: cannot open MP3 '%s'", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 256 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    mp3data = (u8 *)malloc((size_t)fsize);
    if (!mp3data) { fclose(f); return NULL; }

    if ((long)fread(mp3data, 1, (size_t)fsize, f) != fsize) {
        free(mp3data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    mp3dec_init(&dec);
    mp3remaining = (s32)fsize;

    /* Initial allocation — ~5 minutes of stereo 44100 Hz audio */
    accumCap = 44100 * 2 * 300;
    accum = (s16 *)malloc(accumCap * sizeof(s16));
    if (!accum) { free(mp3data); return NULL; }

    while (mp3remaining > 0) {
        s32 samples = mp3dec_decode_frame(&dec,
            mp3data + mp3pos, mp3remaining, frame_pcm, &info);

        if (info.frame_bytes == 0) break; /* no more frames */

        mp3pos += (u32)info.frame_bytes;
        mp3remaining -= info.frame_bytes;

        if (samples > 0 && info.channels > 0) {
            if (srcRate == 0) {
                srcRate = info.hz;
                srcCh = info.channels;
            }

            u32 frameSamples = (u32)(samples * info.channels);

            /* Grow accumulator if needed */
            while (accumLen + frameSamples > accumCap) {
                accumCap *= 2;
                s16 *newBuf = (s16 *)realloc(accum, accumCap * sizeof(s16));
                if (!newBuf) { free(accum); free(mp3data); return NULL; }
                accum = newBuf;
            }

            memcpy(accum + accumLen, frame_pcm, frameSamples * sizeof(s16));
            accumLen += frameSamples;
        }
    }

    free(mp3data);

    if (accumLen == 0 || srcRate == 0) {
        free(accum);
        sysLogPrintf(LOG_WARNING, "modmusic: MP3 decode yielded no audio '%s'", path);
        return NULL;
    }

    /* Convert to device format: 22050 Hz, S16, stereo */
    SDL_AudioCVT cvt;
    s32 cvtResult = SDL_BuildAudioCVT(&cvt,
        AUDIO_S16SYS, (Uint8)srcCh, srcRate,
        AUDIO_S16SYS, 2, 22050);

    if (cvtResult < 0) {
        free(accum);
        sysLogPrintf(LOG_WARNING, "modmusic: MP3 AudioCVT build failed '%s'", path);
        return NULL;
    }

    if (cvtResult > 0) {
        u32 rawBytes = accumLen * sizeof(s16);
        u32 cvtBufLen = rawBytes * (u32)cvt.len_mult;
        u8 *cvtBuf = (u8 *)malloc(cvtBufLen);
        if (!cvtBuf) { free(accum); return NULL; }
        memcpy(cvtBuf, accum, rawBytes);
        free(accum);

        cvt.buf = cvtBuf;
        cvt.len = (s32)rawBytes;
        if (SDL_ConvertAudio(&cvt) < 0) {
            free(cvtBuf);
            sysLogPrintf(LOG_WARNING, "modmusic: MP3 AudioCVT failed '%s'", path);
            return NULL;
        }

        *outLen = (u32)cvt.len_cvt / sizeof(s16);
        return (s16 *)cvtBuf;
    }

    /* Already in target format (unlikely but possible) */
    *outLen = accumLen;
    return accum;
}

/* ========================================================================
 * A-6: OGG Vorbis decoding via stb_vorbis
 * ======================================================================== */

/**
 * Load an OGG Vorbis file and decode to S16 PCM at 22050 Hz stereo.
 * Uses stb_vorbis_decode_filename(), then SDL_AudioCVT for resampling.
 * Returns a malloc'd buffer, NULL on failure. *outLen = total S16 samples.
 */
static s16 *modmusic_loadOgg(const char *path, u32 *outLen)
{
    int channels = 0, sample_rate = 0;
    short *decoded = NULL;
    s32 totalSamples;

    *outLen = 0;

    totalSamples = stb_vorbis_decode_filename(path, &channels, &sample_rate,
                                               &decoded);
    if (totalSamples <= 0 || !decoded) {
        sysLogPrintf(LOG_WARNING, "modmusic: OGG decode failed '%s'", path);
        return NULL;
    }

    /* Convert to device format: 22050 Hz, S16, stereo */
    SDL_AudioCVT cvt;
    s32 cvtResult = SDL_BuildAudioCVT(&cvt,
        AUDIO_S16SYS, (Uint8)channels, sample_rate,
        AUDIO_S16SYS, 2, 22050);

    if (cvtResult < 0) {
        free(decoded);
        sysLogPrintf(LOG_WARNING, "modmusic: OGG AudioCVT build failed '%s'", path);
        return NULL;
    }

    if (cvtResult > 0) {
        u32 rawBytes = (u32)totalSamples * sizeof(s16);
        u32 cvtBufLen = rawBytes * (u32)cvt.len_mult;
        u8 *cvtBuf = (u8 *)malloc(cvtBufLen);
        if (!cvtBuf) { free(decoded); return NULL; }
        memcpy(cvtBuf, decoded, rawBytes);
        free(decoded);

        cvt.buf = cvtBuf;
        cvt.len = (s32)rawBytes;
        if (SDL_ConvertAudio(&cvt) < 0) {
            free(cvtBuf);
            sysLogPrintf(LOG_WARNING, "modmusic: OGG AudioCVT failed '%s'", path);
            return NULL;
        }

        *outLen = (u32)cvt.len_cvt / sizeof(s16);
        return (s16 *)cvtBuf;
    }

    /* Already in target format */
    *outLen = (u32)totalSamples;
    return (s16 *)decoded;
}

/* ========================================================================
 * A-6: Format-detecting loader
 * ======================================================================== */

/**
 * Detect audio format by file extension and load accordingly.
 * Supports .wav, .mp3, and .ogg.
 * Returns a malloc'd S16 PCM buffer, NULL on failure.
 */
static s16 *modmusic_loadAudio(const char *path, u32 *outLen)
{
    const char *ext;

    *outLen = 0;
    if (!path || !path[0]) return NULL;

    /* Find the last '.' in the filename */
    ext = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '.') ext = p;
    }

    if (ext) {
        if ((ext[1] == 'm' || ext[1] == 'M') &&
            (ext[2] == 'p' || ext[2] == 'P') &&
            ext[3] == '3' && ext[4] == '\0') {
            return modmusic_loadMp3(path, outLen);
        }
        if ((ext[1] == 'o' || ext[1] == 'O') &&
            (ext[2] == 'g' || ext[2] == 'G') &&
            (ext[3] == 'g' || ext[3] == 'G') && ext[4] == '\0') {
            return modmusic_loadOgg(path, outLen);
        }
    }

    /* Default: try WAV */
    return modmusic_loadWav(path, outLen);
}

/**
 * Compute the effective mod music volume as a 0.0–1.0 float.
 * Chain: master * music_layer * mod_volume.
 */
static inline f32 modmusic_effectiveVolume(void)
{
    return audioGetMasterVolume() * audioGetMusicVolume() * s_ModMusicVolume;
}

/**
 * Silence the base N64 sequencer by setting music volume to 0.
 * This uses our own volume control system, NOT the legacy sequencer mute.
 */
static void modmusic_muteBaseMusic(void)
{
    musicSetVolume(0);
}

/**
 * Restore base music to the player's configured volume.
 * Triggers a full volume recompute via audioApplyVolumes().
 */
static void modmusic_restoreBaseMusic(void)
{
    audioApplyVolumes();
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void modMusicPlay(const char *file_path)
{
    s16 *pcm;
    u32 len;
    char resolvedBuf[512];
    const char *resolved;

    if (!file_path || !file_path[0]) {
        sysLogPrintf(LOG_WARNING, "modmusic: play called with empty path");
        return;
    }

    /* Stop any current mod track first */
    if (s_ModMusicPlaying) {
        modMusicStop();
    }

    /* Resolve relative paths (e.g. "mods/foo/track.mp3") through fsFullPath()
     * so the file is found regardless of CWD. Absolute paths pass through.
     * Copy into a local buffer since fsFullPath() returns a static pointer. */
    resolved = file_path;
    if (file_path[0] != '/' && file_path[0] != '\\' &&
        !(file_path[0] && file_path[1] == ':')) {
        const char *full = fsFullPath(file_path);
        if (full) {
            strncpy(resolvedBuf, full, sizeof(resolvedBuf) - 1);
            resolvedBuf[sizeof(resolvedBuf) - 1] = '\0';
            resolved = resolvedBuf;
        }
    }

    sysLogPrintf(LOG_NOTE, "modmusic: loading '%s' (resolved from '%s')",
                 resolved, file_path);

    pcm = modmusic_loadAudio(resolved, &len);
    if (!pcm || len == 0) {
        sysLogPrintf(LOG_WARNING, "modmusic: could not load '%s'", resolved);
        return;
    }

    s_ModMusicPCM     = pcm;
    s_ModMusicLen     = len;
    s_ModMusicPos     = 0;
    s_ModMusicPosFrac = 0.0;
    s_ModMusicRateMul = 1.0f;  /* fresh track resets any in-flight rate adjustment */
    s_ModMusicPlaying = 1;

    /* Silence base N64 music while mod track plays */
    modmusic_muteBaseMusic();

    sysLogPrintf(LOG_NOTE, "modmusic: playing '%s' (%u samples, %.1f sec)",
                 resolved, len, (f32)len / (2.0f * 22050.0f));
}

void modMusicStop(void)
{
    if (s_ModMusicPCM) {
        SDL_free(s_ModMusicPCM);
        s_ModMusicPCM = NULL;
    }
    s_ModMusicLen     = 0;
    s_ModMusicPos     = 0;
    s_ModMusicPosFrac = 0.0;
    s_ModMusicRateMul = 1.0f;
    s_ModMusicPlaying = 0;

    /* Restore base music volume to player's setting */
    modmusic_restoreBaseMusic();

    sysLogPrintf(LOG_NOTE, "modmusic: stopped");
}

void modMusicSetVolume(f32 vol)
{
    s_ModMusicVolume = modmusic_clampf(vol, 0.0f, 1.0f);
}

f32 modMusicGetVolume(void)
{
    return s_ModMusicVolume;
}

s32 modMusicIsPlaying(void)
{
    return s_ModMusicPlaying;
}

void modMusicMixInto(s16 *outBuf, u32 numFrames)
{
    f32 vol;
    f64 rate;
    u32 i;
    u32 framesAvail;
    u32 framesProduced;
    u32 framesToWrite;
    s32 mixed;
    f64 cursor;
    f64 step;

    if (!s_ModMusicPlaying || !s_ModMusicPCM || !outBuf) {
        return;
    }

    vol = modmusic_effectiveVolume();
    rate = (f64)s_ModMusicRateMul;
    if (rate < 0.001) rate = 0.001;  /* belt-and-braces, setter clamps already */

    /* numFrames = stereo frames; each frame = 2 S16 samples (L + R).
     * Cursor advances at `rate` source-frames per output-frame. Linear
     * interpolation between adjacent source frames smooths the
     * sub-sample case (rate != 1.0). At rate == 1.0 the math reduces
     * to the integer-cursor fast path's behaviour exactly because
     * cursor.frac stays 0 each step. */
    cursor = (f64)s_ModMusicPos * 0.5 + s_ModMusicPosFrac;  /* fractional source-frame index */
    step   = rate;

    /* Total source frames available from current position to end. */
    framesAvail = (s_ModMusicLen / 2u);
    if ((u32)cursor >= framesAvail) {
        s_ModMusicPlaying = 0;
        modmusic_restoreBaseMusic();
        sysLogPrintf(LOG_NOTE, "modmusic: track finished");
        return;
    }

    /* How many output frames can we produce before running out of
     * source? Solve `cursor + step * N <= framesAvail - 1` for N. */
    framesProduced = (u32)(((f64)(framesAvail - 1) - cursor) / step);
    framesToWrite = (numFrames < framesProduced) ? numFrames : framesProduced;

    for (i = 0; i < framesToWrite; i++) {
        u32 idx0 = (u32)cursor;
        u32 idx1 = idx0 + 1u;
        if (idx1 >= framesAvail) idx1 = idx0;
        f64 frac = cursor - (f64)idx0;

        /* Source samples for this output frame (stereo interleaved). */
        s32 l0 = s_ModMusicPCM[idx0 * 2u];
        s32 r0 = s_ModMusicPCM[idx0 * 2u + 1u];
        s32 l1 = s_ModMusicPCM[idx1 * 2u];
        s32 r1 = s_ModMusicPCM[idx1 * 2u + 1u];

        f32 lInterp = (f32)((f64)l0 + ((f64)l1 - (f64)l0) * frac);
        f32 rInterp = (f32)((f64)r0 + ((f64)r1 - (f64)r0) * frac);

        /* Mix L. */
        mixed = (s32)outBuf[i * 2u] + (s32)(lInterp * vol);
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        outBuf[i * 2u] = (s16)mixed;

        /* Mix R. */
        mixed = (s32)outBuf[i * 2u + 1u] + (s32)(rInterp * vol);
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        outBuf[i * 2u + 1u] = (s16)mixed;

        cursor += step;
    }

    /* Write back integer + fractional position. */
    {
        u32 newFrameIdx = (u32)cursor;
        s_ModMusicPos     = newFrameIdx * 2u;
        s_ModMusicPosFrac = cursor - (f64)newFrameIdx;
    }

    /* Track finished — stop playback. Buffer stays alive until next
     * modMusicPlay or modMusicStop so we don't free mid-frame. */
    if (framesToWrite < numFrames || s_ModMusicPos >= s_ModMusicLen) {
        s_ModMusicPlaying = 0;
        modmusic_restoreBaseMusic();
        sysLogPrintf(LOG_NOTE, "modmusic: track finished");
    }
}

/* ============================================================
 * Issue 4b (2026-04-24): rate adjustment + position API for
 * networked music sync.
 *
 * The host broadcasts SVC_MUSIC_ADVANCE periodically with the
 * current track offset; clients call modMusicGetPositionMs to
 * compare against the authoritative offset and call
 * modMusicSetRate to lerp playback rate within [0.97, 1.03] for
 * gradual catch-up, or modMusicSetPositionMs as a hard-seek
 * fallback when drift exceeds the lerp window.
 * ============================================================ */

void modMusicSetRate(f32 rate)
{
    /* Clamp to [0.97, 1.03] -- ~50 cents of pitch shift max, well
     * under the threshold where listeners hear a noticeable bend. */
    if (rate < 0.97f) rate = 0.97f;
    if (rate > 1.03f) rate = 1.03f;
    s_ModMusicRateMul = rate;
}

f32 modMusicGetRate(void)
{
    return s_ModMusicRateMul;
}

u32 modMusicGetPositionMs(void)
{
    if (!s_ModMusicPlaying || s_ModMusicSampleRate == 0) return 0;
    /* s_ModMusicPos is in samples (L+R interleaved). Frames = samples/2. */
    f64 frames = (f64)s_ModMusicPos * 0.5 + s_ModMusicPosFrac;
    f64 ms = (frames * 1000.0) / (f64)s_ModMusicSampleRate;
    if (ms < 0.0) ms = 0.0;
    return (u32)ms;
}

void modMusicSetPositionMs(u32 ms)
{
    if (!s_ModMusicPlaying || s_ModMusicSampleRate == 0 || s_ModMusicLen == 0) return;
    f64 frames = ((f64)ms * (f64)s_ModMusicSampleRate) / 1000.0;
    u32 frameIdx = (u32)frames;
    u32 maxFrame = s_ModMusicLen / 2u;
    if (maxFrame == 0) return;
    if (frameIdx >= maxFrame) frameIdx = maxFrame - 1;
    s_ModMusicPos     = frameIdx * 2u;
    s_ModMusicPosFrac = frames - (f64)frameIdx;
    sysLogPrintf(LOG_NOTE, "modmusic: hard-seek to %u ms (frame %u of %u)",
                 ms, frameIdx, maxFrame);
}

u32 modMusicGetDurationMs(void)
{
    if (!s_ModMusicPCM || s_ModMusicLen == 0 || s_ModMusicSampleRate == 0) return 0;
    f64 frames = (f64)(s_ModMusicLen / 2u);
    f64 ms = (frames * 1000.0) / (f64)s_ModMusicSampleRate;
    return (u32)ms;
}
