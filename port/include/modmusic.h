/**
 * modmusic.h -- Mod music stream playback (Batch A-2)
 *
 * Parallel PCM playback path for mod music tracks. Runs alongside the
 * existing N64 ADPCM/sequencer pipeline. Loaded WAV files are decoded
 * to S16 stereo at device sample rate and mixed into audioEndFrame's
 * output buffer via modMusicMixInto().
 *
 * Volume is master * music_layer * mod_music_volume.
 * When a mod track is active, the base N64 sequencer is silenced via
 * musicSetVolume(0); restored when mod playback stops.
 */

#ifndef _IN_MODMUSIC_H
#define _IN_MODMUSIC_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Load a WAV file from disk and begin playback. Stops any current mod track. */
void modMusicPlay(const char *file_path);

/** Stop mod music playback and free the PCM buffer. Restores base music volume. */
void modMusicStop(void);

/** Set mod music volume (0.0 - 1.0). Stacks with master * music layer. */
void modMusicSetVolume(f32 vol);

/** Get current mod music volume (0.0 - 1.0). */
f32 modMusicGetVolume(void);

/** Returns 1 if a mod track is currently playing, 0 otherwise. */
s32 modMusicIsPlaying(void);

/**
 * Mix mod music PCM into the output buffer. Called from audioEndFrame()
 * BEFORE SDL_QueueAudio. Adds mod music samples on top of existing
 * content in outBuf (saturating S16 mix).
 *
 * outBuf:     writable S16 stereo interleaved buffer
 * numFrames:  number of stereo frames (1 frame = 2 samples = 4 bytes)
 */
void modMusicMixInto(s16 *outBuf, u32 numFrames);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODMUSIC_H */
