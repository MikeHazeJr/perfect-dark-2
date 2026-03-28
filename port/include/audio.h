#ifndef _IN_AUDIO_H
#define _IN_AUDIO_H

#include <PR/ultratypes.h>

s32 audioInit(void);
s32 audioGetBytesBuffered(void);
s32 audioGetSamplesBuffered(void);
void audioSetNextBuffer(const s16 *buf, u32 len);
void audioEndFrame(void);

/* Volume layer system — four independent layers with master control.
 * All values are 0.0 to 1.0 (float). The effective volume for each
 * category is: master * layer. Persisted to pd.ini. */
f32 audioGetMasterVolume(void);
void audioSetMasterVolume(f32 vol);
f32 audioGetMusicVolume(void);
void audioSetMusicVolume(f32 vol);
f32 audioGetGameplayVolume(void);
void audioSetGameplayVolume(f32 vol);
f32 audioGetUiVolume(void);
void audioSetUiVolume(f32 vol);

/* Get the effective UI volume as a 0x0000–0x5000 scale for sound bridge */
u16 audioGetUiVolumeScaled(void);

/* Recompute and push composite volumes to the engine.
 * Called automatically by the setters, but can be called
 * manually after loading config or bulk changes.
 * No-op if the engine sound system hasn't initialized yet. */
void audioApplyVolumes(void);

/* Signal that the game engine's sound system (sndInit) has completed
 * and it's safe to call musicSetVolume / sndSetSfxVolume.
 * Applies persisted volume layers from pd.ini on first call. */
void audioNotifyEngineReady(void);

#endif
