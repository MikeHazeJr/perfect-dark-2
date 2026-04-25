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

/* Reset all four volume layers to the pd.ini baseline captured at
 * audioNotifyEngineReady time.  Call when clearing a per-agent overlay
 * so that agents without an [Audio] block inherit the global defaults. */
void audioResetToDefaults(void);

/* Signal that the game engine's sound system (sndInit) has completed
 * and it's safe to call musicSetVolume / sndSetSfxVolume.
 * Applies persisted volume layers from pd.ini on first call. */
void audioNotifyEngineReady(void);

/* Mod track selection — catalog ID of the selected mod music track.
 * Empty string means "no mod track selected" (use base game music).
 * Persisted to pd.ini as Audio.ModTrackId.
 * DEPRECATED for direct use — prefer playlist API below. Still works
 * and returns the currently-playing/first track for backward compat. */
const char *audioGetModTrackId(void);
void audioSetModTrackId(const char *id);

/* ---- Mod track playlist (replaces single-track selection) ----
 * Up to 16 tracks can be queued. Playlist + shuffle state persisted
 * to pd.ini. The host resolves one track before SVC_STAGE_START
 * and sends that single ID on the wire (no protocol change). */

#define AUDIO_MAX_PLAYLIST 16

s32  audioGetModPlaylistCount(void);
const char *audioGetModPlaylistEntry(s32 idx);
s32  audioSetModPlaylistEntry(s32 idx, const char *catalog_id);
s32  audioAddModPlaylistEntry(const char *catalog_id);
s32  audioRemoveModPlaylistEntry(const char *catalog_id);
void audioClearModPlaylist(void);
s32  audioIsInModPlaylist(const char *catalog_id);

s32  audioGetModShuffle(void);
void audioSetModShuffle(s32 on);

/* Pick next track from playlist. Returns catalog ID or empty string.
 * If shuffle: random. If sequential: advances internal index. */
const char *audioPickNextPlaylistTrack(void);

/* Reset the sequential playback index (call on match end or playlist change). */
void audioResetPlaylistIndex(void);

/**
 * Per-frame tick for host-side music advancement (v34).
 * If the host is in-game with a playlist and the current track ended,
 * picks the next track and broadcasts SVC_MUSIC_ADVANCE to all clients.
 * Call once per frame from the network tick path.
 * No-op if not host or no playlist.
 */
void audioNetworkMusicTick(void);

/**
 * Issue 4b (2026-04-24): client-side music sync.
 *
 * audioMusicSyncReceive is called by the SVC_MUSIC_ADVANCE network
 * read handler when a track-change or drift-update packet arrives.
 * On a track-change it starts the new track and hard-seeks to the
 * host's current offset (so late-joiners pick up mid-song). On a
 * drift-update it stores the new authoritative offset for the
 * correction tick.
 *
 * audioMusicSyncCorrectionTick runs every audio frame on the client.
 * It compares the local playback position to the expected host
 * position, applies a rate adjustment in [0.97, 1.03] for small
 * drift, or a hard-seek if drift exceeds 5000 ms.
 */
void audioMusicSyncReceive(const char *track_id, u32 match_clock_offset_ms);
void audioMusicSyncCorrectionTick(void);

/**
 * B-141 telemetry — read diagnostic counters for the audio push path.
 *   drops     — SDL queue was full at push time (buffered >= queueLimit).
 *   underruns — SDL queue was near-empty at push time (< 128 stereo samples).
 *   hitches   — gap between audioEndFrame calls exceeded 50ms.
 * Counters are monotonic since process start. Any out parameter may be NULL.
 * Set `Audio.VerboseLog = 1` in pd.ini for per-event log lines.
 */
void audioGetB141Counters(u32 *drops, u32 *underruns, u32 *hitches);

#endif
