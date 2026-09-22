/**
 * modmusic.c -- Mod music stream playback (Batch A-2)
 *
 * Parallel PCM playback path for mod music tracks. Runs alongside the
 * existing ADPCM/sequencer pipeline. WAV, MP3 and Ogg Vorbis are decoded,
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
#include <limits.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "modmusic.h"
#include "audio.h"
#include "system.h"
#include "fs.h"
#include "modvfs.h"
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

/* Build a conversion plan before accumulating source PCM. Every owned byte
 * count and intermediate SDL buffer must fit its signed int API. */
static s32 modmusic_planPcmConversion(SDL_AudioFormat format, u8 channels,
        s32 rate, SDL_AudioCVT *cvt, size_t *maxSourceBytes)
{
    int result;
    size_t frameBytes;
    if (!channels || rate <= 0 || SDL_AUDIO_BITSIZE(format) == 0
            || SDL_AUDIO_BITSIZE(format) % 8 != 0) return 0;
    frameBytes = (size_t)channels * (SDL_AUDIO_BITSIZE(format) / 8);
    result = SDL_BuildAudioCVT(cvt, format, channels, rate,
        AUDIO_S16SYS, 2, 22050);
    if (result < 0 || (result > 0 && cvt->len_mult <= 0)) return 0;
    *maxSourceBytes = (size_t)INT_MAX / (result > 0 ? (size_t)cvt->len_mult : 1);
    *maxSourceBytes -= *maxSourceBytes % frameBytes;
    return *maxSourceBytes != 0;
}

/* Takes SDL ownership on every path. The caller supplies complete source
 * frames within the conversion plan's already-checked source byte limit. */
static s16 *modmusic_finishPcm(Uint8 *pcm, size_t bytes, SDL_AudioCVT *cvt,
        s32 sourceRate, u32 *outLen, s32 *outSourceRate)
{
    if (!pcm || bytes == 0 || bytes > (size_t)INT_MAX) goto fail;
    if (cvt->needed) {
        size_t capacity;
        Uint8 *replacement;
        if (cvt->len_mult <= 0 || bytes > (size_t)INT_MAX / (size_t)cvt->len_mult) {
            goto fail;
        }
        capacity = bytes * (size_t)cvt->len_mult;
        replacement = (Uint8 *)SDL_realloc(pcm, capacity);
        if (!replacement) goto fail;
        pcm = replacement;
        cvt->buf = pcm;
        cvt->len = (int)bytes;
        if (SDL_ConvertAudio(cvt) < 0 || cvt->len_cvt <= 0
                || (size_t)cvt->len_cvt > capacity) goto fail;
        bytes = (size_t)cvt->len_cvt;
    }
    if (bytes % (2 * sizeof(s16)) != 0) goto fail;
    *outLen = (u32)(bytes / sizeof(s16));
    if (outSourceRate) *outSourceRate = sourceRate;
    return (s16 *)pcm;
fail:
    SDL_free(pcm);
    return NULL;
}

typedef struct modmusic_audio_snapshot {
    const void *bytes;
    u32 size;
} modmusic_audio_snapshot_t;

static s16 *modmusic_loadWav(const char *path, u32 *outLen,
        s32 *outSourceRate, const modmusic_audio_snapshot_t *snapshot)
{
    SDL_AudioSpec wavSpec;
    Uint8 *wavBuf = NULL;
    Uint32 wavRawLen = 0;
    SDL_AudioCVT cvt;
    size_t maxSourceBytes;
    size_t frameBytes;
    u32 fileSize = 0;
    void *fileBytes = NULL;
    *outLen = 0;

    if (snapshot) {
        SDL_RWops *rw = SDL_RWFromConstMem(snapshot->bytes, (int)snapshot->size);
        if (rw) SDL_LoadWAV_RW(rw, 1, &wavSpec, &wavBuf, &wavRawLen);
    } else {
        fileBytes = fsFileLoad(path, &fileSize);
        if (fileBytes && fileSize > 0 && fileSize <= INT_MAX) {
            SDL_RWops *rw = SDL_RWFromConstMem(fileBytes, (int)fileSize);
            if (rw) SDL_LoadWAV_RW(rw, 1, &wavSpec, &wavBuf, &wavRawLen);
        }
        free(fileBytes);
        if (!wavBuf) SDL_LoadWAV(path, &wavSpec, &wavBuf, &wavRawLen);
    }
    if (!wavBuf) {
        sysLogPrintf(LOG_WARNING, "modmusic: failed to decode WAV '%s': %s", path, SDL_GetError());
        return NULL;
    }
    if (!modmusic_planPcmConversion(wavSpec.format, wavSpec.channels,
            wavSpec.freq, &cvt, &maxSourceBytes)) {
        SDL_FreeWAV(wavBuf);
        return NULL;
    }
    frameBytes = (size_t)wavSpec.channels * (SDL_AUDIO_BITSIZE(wavSpec.format) / 8);
    if (wavRawLen == 0 || wavRawLen > maxSourceBytes || wavRawLen % frameBytes != 0) {
        SDL_FreeWAV(wavBuf);
        return NULL;
    }
    return modmusic_finishPcm(wavBuf, wavRawLen, &cvt, wavSpec.freq,
        outLen, outSourceRate);
}

typedef struct modmusic_mp3_frame {
    u32 bytes;
    s32 rate, channels, version, samples;
} modmusic_mp3_frame_t;

/* Match the public archive's Layer III framing contract before the decoder
 * can silently resynchronize past a broken frame. This does not validate
 * Huffman data, reservoir contents, CRC values or gapless metadata. */
static s32 modmusic_mp3Frame(const u8 *data, u32 size, modmusic_mp3_frame_t *out)
{
    static const u16 rates[3] = {44100, 48000, 32000};
    static const u16 bitrate[2][15] = {
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}
    };
    u32 version, rate_index, bitrate_index, side_bytes, frame_bytes;
    if (size < 4 || data[0] != 0xff || (data[1] & 0xe0) != 0xe0) return 0;
    version = (data[1] >> 3) & 3;
    rate_index = (data[2] >> 2) & 3;
    bitrate_index = data[2] >> 4;
    if (version == 1 || ((data[1] >> 1) & 3) != 1 || rate_index == 3
            || bitrate_index == 0 || bitrate_index == 15 || (data[3] & 3) == 2) return 0;
    out->version = (s32)version;
    out->rate = rates[rate_index] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
    out->channels = (data[3] >> 6) == 3 ? 1 : 2;
    out->samples = version == 3 ? 1152 : 576;
    frame_bytes = (version == 3 ? 144000u : 72000u)
        * bitrate[version == 3][bitrate_index] / (u32)out->rate + ((data[2] >> 1) & 1);
    side_bytes = version == 3 ? (out->channels == 1 ? 17 : 32)
        : (out->channels == 1 ? 9 : 17);
    if (frame_bytes < 4 + ((data[1] & 1) ? 0 : 2) + side_bytes || frame_bytes > size) return 0;
    out->bytes = frame_bytes;
    return 1;
}

/* Validate tag framing only; body bytes remain opaque metadata. */
static s32 modmusic_mp3Id3TagEnd(const u8 *data, u32 size, u32 position, u32 *end)
{
    u32 version, flags, allowed, body_size = 0, next;
    if (position > size || size - position < 10 || memcmp(data + position, "ID3", 3)) return 0;
    version = data[position + 3];
    flags = data[position + 5];
    allowed = version == 2 ? 0xc0 : version == 3 ? 0xe0 : version == 4 ? 0xf0 : 0;
    if (!allowed || data[position + 4] == 255 || (flags & ~allowed)) return 0;
    for (u32 i = 6; i < 10; ++i) {
        if (data[position + i] & 0x80) return 0;
        body_size = (body_size << 7) | data[position + i];
    }
    if (body_size > size - position - 10) return 0;
    next = position + 10 + body_size;
    if (version == 4 && (flags & 0x10)) {
        if (size - next < 10 || memcmp(data + next, "3DI", 3)
                || memcmp(data + next + 3, data + position + 3, 7)) return 0;
        next += 10;
    }
    *end = next;
    return 1;
}

static s32 modmusic_mp3Id3SuffixStart(const u8 *data, u32 lower, u32 limit, u32 *start)
{
    const u8 *footer;
    u32 body_size = 0, tag_start, tag_end;
    if (lower > limit || limit - lower < 20) return 0;
    footer = data + limit - 10;
    if (memcmp(footer, "3DI", 3) || footer[3] != 4 || !(footer[5] & 0x10)) return 0;
    for (u32 i = 6; i < 10; ++i) {
        if (footer[i] & 0x80) return 0;
        body_size = (body_size << 7) | footer[i];
    }
    if (body_size > limit - lower - 20) return 0;
    tag_start = limit - 20 - body_size;
    if (!modmusic_mp3Id3TagEnd(data, limit, tag_start, &tag_end)
            || tag_end != limit || data[tag_start + 3] != 4
            || !(data[tag_start + 5] & 0x10)) return 0;
    *start = tag_start;
    return 1;
}

/* Accepted metadata: leading ID3v2.2/2.3/2.4, appended ID3v2.4 with matching
 * footer, then optional terminal ID3v1. Zero alignment may follow the last
 * MPEG frame before appended tags. Xing/Info/LAME remain decoder input. */
static s32 modmusic_mp3SourceSpan(const u8 *data, u32 size, u32 *start, u32 *end)
{
    u32 position = 0, limit = size, audio_start, tag_start;
    modmusic_mp3_frame_t first = {0};
    while (size - position >= 3 && !memcmp(data + position, "ID3", 3)) {
        if (!modmusic_mp3Id3TagEnd(data, size, position, &position)) return 0;
    }
    /* A checked v2.4 footer takes precedence over a coincidental TAG string
     * inside its opaque body at the ID3v1 candidate position. */
    if (limit - position >= 128 && !memcmp(data + limit - 128, "TAG", 3)
            && !modmusic_mp3Id3SuffixStart(data, position, limit, &tag_start)) limit -= 128;
    while (limit - position >= 10 && !memcmp(data + limit - 10, "3DI", 3)) {
        if (!modmusic_mp3Id3SuffixStart(data, position, limit, &tag_start)) return 0;
        limit = tag_start;
    }
    audio_start = position;
    while (position < limit) {
        modmusic_mp3_frame_t frame;
        if (!data[position]) {
            for (u32 i = position; i < limit; ++i) if (data[i]) return 0;
            limit = position;
            break;
        }
        if (!modmusic_mp3Frame(data + position, limit - position, &frame)) return 0;
        if (first.bytes && (first.version != frame.version || first.rate != frame.rate
                || first.channels != frame.channels)) return 0;
        first = frame;
        position += frame.bytes;
    }
    if (audio_start == limit) return 0;
    *start = audio_start;
    *end = limit;
    return 1;
}

/* Decode every frame/channel into SDL-owned storage. Format changes inside
 * one stream reject the candidate rather than mixing incompatible PCM. */
static s16 *modmusic_loadMp3(const char *path, u32 *outLen,
        s32 *outSourceRate, const modmusic_audio_snapshot_t *snapshot)
{
    u32 fileSize = 0;
    u8 *mp3data = NULL;
    u32 position = 0;
    u32 audioEnd = 0;
    mp3dec_t decoder;
    mp3d_sample_t framePcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    SDL_AudioCVT cvt;
    u8 *pcm = NULL;
    size_t usedBytes = 0;
    size_t capacity = 0;
    size_t maxSourceBytes = 0;
    s32 sourceRate = 0;
    s32 sourceChannels = 0;
    *outLen = 0;

    if (snapshot) {
        mp3data = (u8 *)snapshot->bytes;
        fileSize = snapshot->size;
    } else {
        mp3data = (u8 *)fsFileLoad(path, &fileSize);
        if (!mp3data) {
            FILE *file = fopen(path, "rb");
            long size;
            if (!file) return NULL;
            if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0
                    || size > INT_MAX || fseek(file, 0, SEEK_SET) != 0) {
                fclose(file);
                return NULL;
            }
            mp3data = (u8 *)malloc((size_t)size);
            if (!mp3data || fread(mp3data, 1, (size_t)size, file) != (size_t)size) {
                fclose(file);
                free(mp3data);
                return NULL;
            }
            fclose(file);
            fileSize = (u32)size;
        }
    }
    if (fileSize == 0 || fileSize > INT_MAX) goto fail;
    if (!modmusic_mp3SourceSpan(mp3data, fileSize, &position, &audioEnd)) goto fail;
    mp3dec_init(&decoder);
    while (position < audioEnd) {
        mp3dec_frame_info_t info = {0};
        modmusic_mp3_frame_t frame;
        int frames;
        size_t frameBytes;
        size_t needed;
        if (!modmusic_mp3Frame(mp3data + position, audioEnd - position, &frame)) goto fail;
        /* Keep reservoir state across exact complete frames. The decoder may
         * not search forward into a later frame or consume metadata as audio. */
        frames = mp3dec_decode_frame(&decoder, mp3data + position,
            (int)frame.bytes, framePcm, &info);
        if (info.frame_offset != 0 || info.frame_bytes != (int)frame.bytes
                || info.layer != 3 || info.hz != frame.rate || info.channels != frame.channels
                || (frames != 0 && frames != frame.samples)) goto fail;
        position += frame.bytes;
        /* Pinned minimp3 clears its public header on invalid side information;
         * reservoir priming returns zero with the admitted header retained. */
        if (frames == 0) {
            if (decoder.header[0] != 0xff) goto fail;
            continue;
        }
        if (info.channels < 1 || info.channels > 2 || info.hz <= 0
                || frames > MINIMP3_MAX_SAMPLES_PER_FRAME / info.channels) goto fail;
        if (!sourceRate) {
            sourceRate = info.hz;
            sourceChannels = info.channels;
            if (!modmusic_planPcmConversion(AUDIO_S16SYS, (u8)sourceChannels,
                    sourceRate, &cvt, &maxSourceBytes)) goto fail;
        } else if (sourceRate != info.hz || sourceChannels != info.channels) {
            goto fail;
        }
        frameBytes = (size_t)frames * (size_t)info.channels * sizeof(s16);
        if (frameBytes > maxSourceBytes - usedBytes) goto fail;
        needed = usedBytes + frameBytes;
        if (needed > capacity) {
            size_t grown = capacity ? capacity : frameBytes;
            u8 *replacement;
            while (grown < needed) {
                grown = grown > maxSourceBytes / 2 ? maxSourceBytes : grown * 2;
            }
            replacement = (u8 *)SDL_realloc(pcm, grown);
            if (!replacement) goto fail;
            pcm = replacement;
            capacity = grown;
        }
        memcpy(pcm + usedBytes, framePcm, frameBytes);
        usedBytes = needed;
    }
    if (!snapshot) free(mp3data);
    if (!sourceRate || usedBytes == 0) {
        SDL_free(pcm);
        return NULL;
    }
    return modmusic_finishPcm(pcm, usedBytes, &cvt, sourceRate,
        outLen, outSourceRate);
fail:
    if (!snapshot) free(mp3data);
    SDL_free(pcm);
    sysLogPrintf(LOG_WARNING, "modmusic: MP3 decode/format/size failure '%s'", path);
    return NULL;
}

/* ========================================================================
 * A-6: OGG Vorbis decoding via stb_vorbis
 * ======================================================================== */

/**
 * Incrementally decode OGG Vorbis into SDL-owned S16 storage, then convert
 * to 22050 Hz stereo. The ceiling bounds every PCM allocation, including
 * SDL's intermediate conversion storage. It never exceeds INT_MAX bytes.
 */
static s16 *modmusic_loadOggBounded(const char *path, u32 *outLen,
        s32 *outSourceRate, size_t maxPcmBytes,
        const modmusic_audio_snapshot_t *snapshot)
{
    stb_vorbis *decoder = NULL;
    stb_vorbis_info info;
    SDL_AudioCVT cvt;
    s32 cvtResult;
    short chunk[4096];
    int chunkFrames;
    int decoderError = 0;
    size_t frameBytes;
    size_t conversionMultiplier;
    size_t maxSourceBytes;
    size_t usedBytes = 0;
    size_t capacity = 0;
    u8 *pcm = NULL;
    u32 fileSize = 0;
    void *fileBytes = NULL;
    const char *failure = "invalid input size";

    *outLen = 0;
    if (maxPcmBytes > (size_t)INT_MAX) maxPcmBytes = (size_t)INT_MAX;
    if (maxPcmBytes == 0) goto fail;

    if (snapshot) {
        fileBytes = (void *)snapshot->bytes;
        fileSize = snapshot->size;
    } else fileBytes = fsFileLoad(path, &fileSize);
    if (fileBytes) {
        if (fileSize == 0 || fileSize > INT_MAX) goto fail;
        decoder = stb_vorbis_open_memory((const unsigned char *)fileBytes,
            (int)fileSize, &decoderError, NULL);
    } else {
        decoder = stb_vorbis_open_filename(path, &decoderError, NULL);
    }
    failure = "decoder open";
    if (!decoder) goto fail;

    info = stb_vorbis_get_info(decoder);
    failure = "invalid decoded format";
    if (info.channels <= 0 || info.channels > UCHAR_MAX
            || info.sample_rate == 0 || info.sample_rate > (unsigned int)INT_MAX) {
        goto fail;
    }

    cvtResult = SDL_BuildAudioCVT(&cvt,
        AUDIO_S16SYS, (Uint8)info.channels, (int)info.sample_rate,
        AUDIO_S16SYS, 2, 22050);
    failure = "SDL conversion format";
    if (cvtResult < 0 || (cvtResult > 0 && cvt.len_mult <= 0)) goto fail;

    /* SDL's len and len_cvt are signed byte counts. Bound decoded input
     * before allocating it, accounting for the eventual conversion growth. */
    conversionMultiplier = cvtResult > 0 ? (size_t)cvt.len_mult : 1;
    frameBytes = (size_t)info.channels * sizeof(s16);
    maxSourceBytes = maxPcmBytes / conversionMultiplier;
    maxSourceBytes -= maxSourceBytes % frameBytes;
    failure = "PCM exceeds SDL byte limit";
    if (maxSourceBytes == 0) goto fail;
    chunkFrames = (int)(sizeof(chunk) / sizeof(chunk[0])) / info.channels;

    for (;;) {
        int frames = stb_vorbis_get_samples_short_interleaved(decoder,
            info.channels, chunk, chunkFrames * info.channels);
        size_t chunkBytes;
        size_t needed;
        decoderError = stb_vorbis_get_error(decoder);
        failure = "Vorbis decode";
        if (decoderError != 0 || frames < 0 || frames > chunkFrames) goto fail;
        if (frames == 0) break;

        /* This product is bounded by the fixed chunk buffer. The subtraction
         * check precedes addition and every accumulator allocation. */
        chunkBytes = (size_t)frames * frameBytes;
        failure = "PCM exceeds SDL byte limit";
        if (chunkBytes > maxSourceBytes - usedBytes) goto fail;
        needed = usedBytes + chunkBytes;
        if (needed > capacity) {
            size_t grown = capacity ? capacity : chunkBytes;
            u8 *replacement;
            while (grown < needed) {
                grown = grown > maxSourceBytes / 2 ? maxSourceBytes : grown * 2;
            }
            failure = "PCM allocation";
            replacement = (u8 *)SDL_realloc(pcm, grown);
            if (!replacement) goto fail;
            pcm = replacement;
            capacity = grown;
        }
        memcpy(pcm + usedBytes, chunk, chunkBytes);
        usedBytes = needed;
    }

    /* The memory decoder borrows fileBytes through its last decode and close. */
    stb_vorbis_close(decoder);
    decoder = NULL;
    if (!snapshot) free(fileBytes);
    fileBytes = NULL;
    failure = "empty Vorbis stream";
    if (usedBytes == 0) goto fail;

    if (cvtResult > 0) {
        size_t convertedCapacity;
        /* Keep the local narrowing/allocation proof explicit at conversion. */
        failure = "PCM conversion exceeds SDL byte limit";
        if (usedBytes > maxPcmBytes / conversionMultiplier) goto fail;
        convertedCapacity = usedBytes * conversionMultiplier;
        if (convertedCapacity > capacity) {
            u8 *replacement = (u8 *)SDL_realloc(pcm, convertedCapacity);
            failure = "PCM conversion allocation";
            if (!replacement) goto fail;
            pcm = replacement;
            capacity = convertedCapacity;
        }
        cvt.buf = pcm;
        cvt.len = (int)usedBytes;
        failure = "SDL audio conversion";
        if (SDL_ConvertAudio(&cvt) < 0 || cvt.len_cvt <= 0
                || (size_t)cvt.len_cvt > convertedCapacity) {
            goto fail;
        }
        usedBytes = (size_t)cvt.len_cvt;
    }

    failure = "incomplete stereo PCM frame";
    if (usedBytes % (2 * sizeof(s16)) != 0) goto fail;
    if (outSourceRate) *outSourceRate = (s32)info.sample_rate;
    *outLen = (u32)(usedBytes / sizeof(s16));
    return (s16 *)pcm;

fail:
    if (decoder) stb_vorbis_close(decoder);
    if (!snapshot) free(fileBytes);
    SDL_free(pcm);
    sysLogPrintf(LOG_WARNING, "modmusic: OGG '%s' failed: %s (decoder=%d)",
        path, failure, decoderError);
    return NULL;
}

static s16 *modmusic_loadOgg(const char *path, u32 *outLen,
        s32 *outSourceRate, const modmusic_audio_snapshot_t *snapshot)
{
    return modmusic_loadOggBounded(path, outLen, outSourceRate, (size_t)INT_MAX, snapshot);
}

#ifdef PD_TESTS
/* Exercise the production allocation boundary using ordinary small fixtures.
 * Tests can only tighten the real SDL limit, never replace the decoder. */
s16 *modMusicTestLoadOggWithByteLimit(const char *path, u32 *outLen,
        s32 *outSourceRate, u32 maxPcmBytes)
{
    if (outLen) *outLen = 0;
    if (outSourceRate) *outSourceRate = 22050;
    if (!outLen || !path || !path[0]) return NULL;
    return modmusic_loadOggBounded(path, outLen, outSourceRate,
        (size_t)maxPcmBytes, NULL);
}
#endif

/* ========================================================================
 * A-6: Format-detecting loader
 * ======================================================================== */

/**
 * Detect audio format by file extension and load accordingly.
 * Supports .wav, .mp3, and .ogg.
 * Returns an SDL-owned S16 PCM buffer, NULL on failure.
 */
static s16 *modmusic_loadAudio(const char *path, u32 *outLen,
        s32 *outSourceRate, const modmusic_audio_snapshot_t *snapshot)
{
    const char *ext;

    *outLen = 0;
    if (outSourceRate) {
        *outSourceRate = 22050;
    }
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
            return modmusic_loadMp3(path, outLen, outSourceRate, snapshot);
        }
        if ((ext[1] == 'o' || ext[1] == 'O') &&
            (ext[2] == 'g' || ext[2] == 'G') &&
            (ext[3] == 'g' || ext[3] == 'G') && ext[4] == '\0') {
            return modmusic_loadOgg(path, outLen, outSourceRate, snapshot);
        }
    }

    /* Default: try WAV */
    return modmusic_loadWav(path, outLen, outSourceRate, snapshot);
}

s16 *modMusicLoadAudioPcm22050(const char *file_path, u32 *out_len,
        s32 *out_source_rate)
{
    if (out_len) {
        *out_len = 0;
    }
    if (out_source_rate) {
        *out_source_rate = 22050;
    }
    if (!out_len) {
        return NULL;
    }
    return modmusic_loadAudio(file_path, out_len, out_source_rate, NULL);
}

s16 *modMusicDecodeAudioPcm22050(const char *source_name, const void *bytes,
        u32 size, u32 *out_len, s32 *out_source_rate)
{
    if (out_len) *out_len = 0;
    if (out_source_rate) *out_source_rate = 22050;
    if (!out_len || !source_name || !source_name[0] || !bytes || !size || size > INT_MAX)
        return NULL;
    const modmusic_audio_snapshot_t snapshot = {bytes, size};
    return modmusic_loadAudio(source_name, out_len, out_source_rate, &snapshot);
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

s32 modMusicAudioSourceDuration60(const char *file_path)
{
    u32 samples = 0;
    s16 *pcm = modMusicLoadAudioPcm22050(file_path, &samples, NULL);
    u64 ticks;
    if (!pcm || samples < 2 || samples % 2u) {
        SDL_free(pcm);
        return -1;
    }
    ticks = ((u64)(samples / 2u) * 60u + 22049u) / 22050u;
    SDL_free(pcm);
    return ticks <= INT_MAX ? (s32)ticks : -1;
}

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

    /* EOF retains PCM until replacement or stop. Release that owner too. */
    if (s_ModMusicPCM) {
        modMusicStop();
    }

    /* Resolve loose relative paths through fsFullPath() so they are found
     * regardless of CWD. Mounted .pdmod entries must stay archive-relative;
     * fsFileLoad in the decoders will resolve those through modVFS. */
    resolved = file_path;
    if (!modVfsCanResolve(file_path) &&
        file_path[0] != '/' && file_path[0] != '\\' &&
        !(file_path[0] && file_path[1] == ':')) {
        const char *full = fsFullPath(file_path, resolvedBuf, sizeof(resolvedBuf));
        if (full && full[0]) {
            resolved = resolvedBuf;
        }
    }

    sysLogPrintf(LOG_NOTE, "modmusic: loading '%s' (resolved from '%s')",
                 resolved, file_path);

    pcm = modmusic_loadAudio(resolved, &len, NULL, NULL);
    if (!pcm || len == 0) {
        SDL_free(pcm);
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

    /* Every cursor within the source duration contributes a frame, including
     * the final frame. Its interpolation neighbour is clamped below. Bound
     * using the same accumulated cursor that samples the source, avoiding
     * differing roundoff from a separate division-based frame prediction. */
    for (i = 0; i < numFrames && cursor < (f64)framesAvail; i++) {
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

    framesToWrite = i;

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
    /* An unordered network/user value must not reach the PCM cursor cast. */
    if (!(rate >= 0.0f || rate < 0.0f)) rate = 1.0f;
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
    u32 maxFrame = s_ModMusicLen / 2u;
    u32 frameIdx;
    if (maxFrame == 0) return;
    if (frames >= (f64)maxFrame) {
        frameIdx = maxFrame - 1;
        frames = (f64)frameIdx;
    } else frameIdx = (u32)frames;
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
