#ifndef _IN_AUDIO_PCM_ONESHOT_H
#define _IN_AUDIO_PCM_ONESHOT_H

#include <PR/ultratypes.h>

/* Shared file decoder -> bounded pitch conversion -> existing one-shot gains.
 * Returns SDL-owned stereo S16 PCM at 22050 Hz. out_samples counts S16 samples.
 * Nonpositive finite pitch retains the historical normal-speed default;
 * nonfinite controls reject. Caller frees the result with SDL_free. */
s16 *audioPrepareFileOneShotPcm(const char *path, u16 volume, u8 pan, f32 pitch,
        f32 master_volume, f32 gameplay_volume, u32 *out_samples);

/* SDL_QueueAudio copies the complete prepared buffer; this releases the source
 * on both success and failure. No fallback source or persistent voice is used. */
s32 audioQueueFileOneShot(u32 device, const char *path, u16 volume, u8 pan,
        f32 pitch, f32 master_volume, f32 gameplay_volume);

#endif
