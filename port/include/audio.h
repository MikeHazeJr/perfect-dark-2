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

/* Load and play a WAV file through the SDL audio device.
 * volume: 0–0x7fff (AL_VOL_FULL = 0x7fff).
 * pan:    0–127   (AL_PAN_CENTER = 64; 0 = full left, 127 = full right).
 * Returns 1 on success, 0 if the file could not be loaded or converted.
 * On failure the caller should fall back to the ROM sound path. */
s32 audioPlayFileSound(const char *path, u16 volume, u8 pan);

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
