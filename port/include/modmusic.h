/**
 * modmusic.h -- Mod music stream playback (Batch A-2, extended A-6)
 *
 * Parallel PCM playback path for mod music tracks. Runs alongside the
 * existing N64 ADPCM/sequencer pipeline. Audio files (WAV, MP3, OGG)
 * are decoded to S16 stereo at device sample rate and mixed into
 * audioEndFrame's output buffer via modMusicMixInto().
 *
 * Supported formats (A-6): WAV (SDL), MP3 (minimp3), OGG (stb_vorbis).
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

/** Decode a standard audio file to S16 stereo at 22050 Hz.
 *  Accepts WAV, MP3, and OGG formats. The returned buffer is owned by the
 *  caller and must be released with SDL_free. out_len receives total S16
 *  samples, not stereo frames. out_source_rate may be NULL. */
s16 *modMusicLoadAudioPcm22050(const char *file_path, u32 *out_len,
		s32 *out_source_rate);

/** Load an audio file from disk and begin playback. Stops any current mod track.
 *  Accepts WAV, MP3, and OGG formats (detected by extension). */
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

/* ============================================================
 * Issue 4b (2026-04-24): networked music sync API.
 *
 * Set the playback rate multiplier (clamped to [0.97, 1.03]) for
 * gradual drift correction. 1.0 = normal speed; >1.0 = slightly
 * faster (catches up to authoritative offset); <1.0 = slightly
 * slower (lets authoritative offset catch up). Pitch shift at the
 * boundaries is ~50 cents -- audible if you're listening for it,
 * tolerable for transient sync correction. Rate is always reset to
 * 1.0 on modMusicPlay / modMusicStop. */
void modMusicSetRate(f32 rate);
f32  modMusicGetRate(void);

/** Current playback position in milliseconds, 0 if not playing.
 *  Used by the client sync tick to compute drift vs the host's
 *  authoritative offset. */
u32  modMusicGetPositionMs(void);

/** Hard-seek to the given position in milliseconds. Clamped to track
 *  duration. Used as a last-resort fallback when drift exceeds the
 *  rate-lerp window. Logs the seek. */
void modMusicSetPositionMs(u32 ms);

/** Total track duration in milliseconds, 0 if not playing. */
u32  modMusicGetDurationMs(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODMUSIC_H */
