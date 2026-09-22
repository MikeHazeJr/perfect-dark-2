#include <limits.h>
#include <float.h>
#include <SDL.h>
#include "audio_pcm_oneshot.h"
#include "modmusic.h"

s16 *audioPrepareFileOneShotPcm(const char *path, u16 volume, u8 pan, f32 pitch,
        f32 master_volume, f32 gameplay_volume, u32 *out_samples)
{
    s16 *pcm = NULL;
    u32 samples = 0;
    s32 source_rate = 0;
    double requested_rate;
    double pcm_rate;
    s32 conversion_rate;
    size_t bytes;
    SDL_AudioCVT cvt;
    int conversion;
    if (out_samples) *out_samples = 0;
    if (!out_samples || !(pitch >= -FLT_MAX && pitch <= FLT_MAX)
            || !(master_volume >= -FLT_MAX && master_volume <= FLT_MAX)
            || !(gameplay_volume >= -FLT_MAX && gameplay_volume <= FLT_MAX)) return NULL;
    if (pitch <= 0.0f) pitch = 1.0f;
    pcm = modMusicLoadAudioPcm22050(path, &samples, &source_rate);
    if (!pcm || source_rate <= 0 || samples < 2 || samples % 2
            || samples > (u32)INT_MAX / sizeof(s16)) goto fail;

    /* Preserve the old source-frequency clamp, but do the product and clamp
     * before narrowing. The shared decoder already produced stereo22050, so
     * express that same speed change in its PCM sample-rate domain. */
    requested_rate = (double)source_rate * (double)pitch;
    if (requested_rate < 1000.0) requested_rate = 1000.0;
    else if (requested_rate > 192000.0) requested_rate = 192000.0;
    requested_rate = (s32)(requested_rate + 0.5);
    pcm_rate = 22050.0 * requested_rate / (double)source_rate;
    if (!(pcm_rate >= 1.0 && pcm_rate <= INT_MAX - 0.5)) goto fail;
    conversion_rate = (s32)(pcm_rate + 0.5);
    bytes = (size_t)samples * sizeof(s16);
    conversion = SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, 2, conversion_rate,
        AUDIO_S16SYS, 2, 22050);
    if (conversion < 0) goto fail;
    if (conversion > 0) {
        size_t capacity;
        void *replacement;
        if (cvt.len_mult <= 0 || bytes > (size_t)INT_MAX / (size_t)cvt.len_mult) goto fail;
        capacity = bytes * (size_t)cvt.len_mult;
        replacement = SDL_realloc(pcm, capacity);
        if (!replacement) goto fail;
        pcm = (s16 *)replacement;
        cvt.buf = (Uint8 *)pcm;
        cvt.len = (int)bytes;
        if (SDL_ConvertAudio(&cvt) < 0 || cvt.len_cvt <= 0
                || (size_t)cvt.len_cvt > capacity
                || cvt.len_cvt % (2 * sizeof(s16))) goto fail;
        samples = (u32)cvt.len_cvt / sizeof(s16);
    }

    /* Keep the existing per-call, master and gameplay volume layers and
     * linear pan law. Clamp invalid caller ranges before sample narrowing. */
    {
        f32 gain, pan_position, left, right;
        if (master_volume < 0.0f) master_volume = 0.0f;
        else if (master_volume > 1.0f) master_volume = 1.0f;
        if (gameplay_volume < 0.0f) gameplay_volume = 0.0f;
        else if (gameplay_volume > 1.0f) gameplay_volume = 1.0f;
        if (volume > 0x7fff) volume = 0x7fff;
        if (pan > 127) pan = 127;
        gain = ((f32)volume / (f32)0x7fff) * master_volume * gameplay_volume;
        pan_position = (f32)pan / 127.0f;
        left = gain * (1.0f - pan_position) * 2.0f;
        right = gain * pan_position * 2.0f;
        if (left > 1.0f) left = 1.0f;
        if (right > 1.0f) right = 1.0f;
        for (u32 i = 0; i < samples; i += 2) {
            pcm[i] = (s16)((s32)pcm[i] * left);
            pcm[i + 1] = (s16)((s32)pcm[i + 1] * right);
        }
    }
    *out_samples = samples;
    return pcm;
fail:
    SDL_free(pcm);
    return NULL;
}

s32 audioQueueFileOneShot(u32 device, const char *path, u16 volume, u8 pan,
        f32 pitch, f32 master_volume, f32 gameplay_volume)
{
    u32 samples = 0;
    s16 *pcm = audioPrepareFileOneShotPcm(path, volume, pan, pitch,
        master_volume, gameplay_volume, &samples);
    int result;
    if (!pcm) return 0;
    result = SDL_QueueAudio((SDL_AudioDeviceID)device, pcm,
        (Uint32)((size_t)samples * sizeof(s16)));
    SDL_free(pcm);
    return result == 0;
}
