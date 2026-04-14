#include <PR/ultratypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "platform.h"
#include "config.h"
#include "audio.h"
#include "modmusic.h"
#include "assetcatalog.h"
#include "fs.h"
#include "system.h"

/* Network externs for music tick (avoid pulling in full net headers) */
extern s32 g_NetMode;
extern s32 g_NetLocalBotAuthority; /* true if we are the host/bot authority */
#define NETMODE_SERVER_AUDIO 1
/* From netmsg.h — broadcast next track to room */
extern void netMusicBroadcastAdvance(const char *track_id, u8 room_id);
extern u8 g_LocalRoomId;

static SDL_AudioDeviceID dev;
static const s16 *nextBuf;
static u32 nextSize = 0;

static s32 bufferSize = 512;
static s32 queueLimit = 8192;

/* ========================================================================
 * Volume layer system
 *
 * Four independent volume layers: Master, Music, Gameplay (SFX), UI.
 * All stored as 0.0–1.0 floats and persisted to pd.ini.
 *
 * Effective volume for each category:
 *   music_effective    = master * music
 *   gameplay_effective = master * gameplay
 *   ui_effective       = master * ui
 *
 * These are applied at the engine's existing volume choke points:
 *   - Music:    musicSetVolume() in music.c (scales g_MusicVolume)
 *   - Gameplay: sndSetSfxVolume() in snd.c (scales g_SfxVolume)
 *   - UI:       pdguiPlaySound() applies post-creation volume event
 * ======================================================================== */

static f32 g_AudioMasterVolume   = 1.0f;
static f32 g_AudioMusicVolume    = 1.0f;
static f32 g_AudioGameplayVolume = 1.0f;
static f32 g_AudioUiVolume       = 1.0f;

/* The "user-intended" raw volumes before layer scaling.
 * These are what the engine would use at master=1.0, layer=1.0.
 * We store them so we can recompute composite values on any change. */
static u16 g_AudioRawMusicVol    = 0x5000;
static u16 g_AudioRawSfxVol      = 0x5000;

/* Forward declarations for engine volume functions */
extern void musicSetVolume(u16 volume);
extern u16  musicGetVolume(void);
extern void sndSetSfxVolume(u16 volume);
extern u16  g_SfxVolume;
extern u16  g_MusicVolume;

/* Guard flag: set to 1 once the game engine's sound system (sndInit) has
 * been called and it's safe to write to audio structures. audioApplyVolumes
 * is a no-op before this is set, preventing crashes from accessing
 * uninitialized sequencer/sound channel data during early init. */
static s32 g_AudioEngineReady = 0;

/* ========================================================================
 * B-141 telemetry — audio path diagnostic counters.
 *
 * B-141: "audio skips / pauses intermittently during gameplay" — 2026-04-13
 * playtest, not reproducible on demand. We count three symptoms at the only
 * place they can all be observed (the per-frame audioEndFrame push):
 *
 *   drops     — SDL queue was full at push time (buffered >= queueLimit).
 *               Producer outrunning consumer, or consumer stalled.
 *   underruns — SDL queue was near-empty at push time (< threshold).
 *               Consumer chewed through everything; next few ms = silence.
 *   hitches   — gap between consecutive audioEndFrame calls exceeded the
 *               threshold. Main-loop stall. Audio will underrun soon after.
 *
 * Counters are always maintained (cheap: a few adds + one SDL_GetTicks per
 * frame). Per-event log lines are gated on `Audio.VerboseLog` (pd.ini).
 * A 30-second summary fires automatically if any event happened in that
 * window. Zero-activity windows are silent.
 * ======================================================================== */

#define AUDIO_HITCH_THRESHOLD_MS 50          /* gap between pushes > 50ms = hitch */
#define AUDIO_UNDERRUN_THRESHOLD_SAMPLES 128 /* < 128 stereo samples = ~3ms buffer */
#define AUDIO_SUMMARY_INTERVAL_MS 30000      /* summary cadence when active */

static s32 g_AudioVerboseLog = 0;            /* pd.ini: Audio.VerboseLog */
static u32 g_AudioDropCount = 0;
static u32 g_AudioUnderrunCount = 0;
static u32 g_AudioHitchCount = 0;
static u32 s_AudioLastEndTick = 0;           /* for hitch detection */
static u32 s_AudioLastSummaryTick = 0;       /* for periodic summary */

s32 audioInit(void)
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		sysLogPrintf(LOG_ERROR, "SDL audio init error: %s", SDL_GetError());
		return -1;
	}

	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 22050; /* standard half-rate (44100/2) — 22050 was a typo */
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = bufferSize;
	want.callback = NULL;

	nextBuf = NULL;

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (dev == 0) {
		sysLogPrintf(LOG_ERROR, "SDL_OpenAudio error: %s", SDL_GetError());
		return -1;
	}

	SDL_PauseAudioDevice(dev, 0);

	/* NOTE: We do NOT call audioApplyVolumes() here because the game
	 * engine's sound system (sndInit) hasn't been called yet at this
	 * point in the boot sequence. Calling musicSetVolume/sndSetSfxVolume
	 * before sndInit would access uninitialized audio structures and crash.
	 * Volumes will be applied when audioNotifyEngineReady() is called
	 * after sndInit completes. */

	return 0;
}

s32 audioGetBytesBuffered(void)
{
	return SDL_GetQueuedAudioSize(dev);
}

s32 audioGetSamplesBuffered(void)
{
	return audioGetBytesBuffered() / 4;
}

void audioSetNextBuffer(const s16 *buf, u32 len)
{
	nextBuf = buf;
	nextSize = len;
}

/* Writable mix buffer for blending mod music into the N64 RSP output.
 * nextBuf is const (RSP output is read-only), so when mod music is
 * active we copy into this buffer, mix mod PCM on top, then queue it. */
static s16 s_MixBuf[8192]; /* 8192 samples = 4096 stereo frames — covers any realistic frame */

void audioEndFrame(void)
{
	const u32 now = SDL_GetTicks();

	/* B-141: hitch detection — gap between consecutive pushes exceeds
	 * threshold. Main loop stalled; audio will underrun right after. */
	if (s_AudioLastEndTick != 0) {
		const u32 delta = now - s_AudioLastEndTick;
		if (delta > AUDIO_HITCH_THRESHOLD_MS) {
			g_AudioHitchCount++;
			if (g_AudioVerboseLog) {
				sysLogPrintf(LOG_WARNING, "AUDIO[B-141]: frame hitch delta=%ums", (unsigned)delta);
			}
		}
	}
	s_AudioLastEndTick = now;

	if (nextBuf && nextSize) {
		const s32 buffered = audioGetSamplesBuffered();

		/* B-141: underrun detection — consumer chewed through the queue. */
		if (buffered < AUDIO_UNDERRUN_THRESHOLD_SAMPLES) {
			g_AudioUnderrunCount++;
			if (g_AudioVerboseLog) {
				sysLogPrintf(LOG_WARNING, "AUDIO[B-141]: underrun buffered=%d", buffered);
			}
		}

		if (buffered < queueLimit) {
			if (modMusicIsPlaying()) {
				/* Copy N64 output into writable buffer, mix mod music on top */
				u32 numSamples = nextSize / sizeof(s16);
				u32 numFrames = numSamples / 2;

				if (numSamples > sizeof(s_MixBuf) / sizeof(s16)) {
					numSamples = sizeof(s_MixBuf) / sizeof(s16);
					numFrames = numSamples / 2;
				}

				memcpy(s_MixBuf, nextBuf, numSamples * sizeof(s16));
				modMusicMixInto(s_MixBuf, numFrames);
				SDL_QueueAudio(dev, s_MixBuf, numSamples * sizeof(s16));
			} else {
				SDL_QueueAudio(dev, nextBuf, nextSize);
			}
		} else {
			/* B-141: queue-limit drop — producer outrunning consumer; frame
			 * of audio is discarded, usually audible as a skip. */
			g_AudioDropCount++;
			if (g_AudioVerboseLog) {
				sysLogPrintf(LOG_WARNING, "AUDIO[B-141]: drop buffered=%d limit=%d",
				             buffered, queueLimit);
			}
		}
		nextBuf = NULL;
		nextSize = 0;
	}

	/* B-141: periodic 30s summary if anything happened in the window */
	if (now - s_AudioLastSummaryTick > AUDIO_SUMMARY_INTERVAL_MS) {
		s_AudioLastSummaryTick = now;
		if (g_AudioDropCount || g_AudioUnderrunCount || g_AudioHitchCount) {
			sysLogPrintf(LOG_NOTE, "AUDIO[B-141]: 30s summary drops=%u underruns=%u hitches=%u",
			             (unsigned)g_AudioDropCount,
			             (unsigned)g_AudioUnderrunCount,
			             (unsigned)g_AudioHitchCount);
		}
	}
}

/* B-141 telemetry getters. Any out parameter may be NULL. */
void audioGetB141Counters(u32 *drops, u32 *underruns, u32 *hitches)
{
	if (drops) *drops = g_AudioDropCount;
	if (underruns) *underruns = g_AudioUnderrunCount;
	if (hitches) *hitches = g_AudioHitchCount;
}

/* ========================================================================
 * Volume layer getters and setters
 * ======================================================================== */

static inline f32 clampf(f32 v, f32 lo, f32 hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

f32 audioGetMasterVolume(void)   { return g_AudioMasterVolume; }
f32 audioGetMusicVolume(void)    { return g_AudioMusicVolume; }
f32 audioGetGameplayVolume(void) { return g_AudioGameplayVolume; }
f32 audioGetUiVolume(void)       { return g_AudioUiVolume; }

/**
 * Signal that the game engine's sound system (sndInit) has completed
 * and it's safe to write to audio structures. Call this once after
 * sndInit() finishes. Applies any persisted volume layers from pd.ini.
 */
void audioNotifyEngineReady(void)
{
	g_AudioEngineReady = 1;
	audioApplyVolumes();
}

void audioApplyVolumes(void)
{
	/* Don't touch engine audio structures before sndInit has run */
	if (!g_AudioEngineReady) {
		return;
	}

	/* Music: scale raw music volume by master * music layer.
	 * The raw volume is the "user-intended" music volume at 100%/100%.
	 * We read the current engine value as the raw if we haven't captured it. */
	f32 musicScale = g_AudioMasterVolume * g_AudioMusicVolume;
	u16 musicEffective = (u16)(g_AudioRawMusicVol * musicScale);
	if (musicEffective > 0x5000) musicEffective = 0x5000;
	musicSetVolume(musicEffective);

	/* Gameplay SFX: scale raw SFX volume by master * gameplay layer */
	f32 sfxScale = g_AudioMasterVolume * g_AudioGameplayVolume;
	u16 sfxEffective = (u16)(g_AudioRawSfxVol * sfxScale);
	if (sfxEffective > 0x5000) sfxEffective = 0x5000;
	sndSetSfxVolume(sfxEffective);

	/* UI volume is applied per-sound in pdguiPlaySound — no global push needed */
}

void audioSetMasterVolume(f32 vol)
{
	g_AudioMasterVolume = clampf(vol, 0.0f, 1.0f);
	audioApplyVolumes();
}

void audioSetMusicVolume(f32 vol)
{
	g_AudioMusicVolume = clampf(vol, 0.0f, 1.0f);
	audioApplyVolumes();
}

void audioSetGameplayVolume(f32 vol)
{
	g_AudioGameplayVolume = clampf(vol, 0.0f, 1.0f);
	audioApplyVolumes();
}

void audioSetUiVolume(f32 vol)
{
	g_AudioUiVolume = clampf(vol, 0.0f, 1.0f);
	/* UI volume is applied per-sound, no global push */
}

/* Get the effective UI volume as a 0x0000–0x5000 scale value
 * for use by the sound bridge */
u16 audioGetUiVolumeScaled(void)
{
	f32 scale = g_AudioMasterVolume * g_AudioUiVolume;
	u16 vol = (u16)(0x5000 * scale);
	if (vol > 0x5000) vol = 0x5000;
	return vol;
}

/* ========================================================================
 * File-based sound playback (C-7 mod SFX override)
 *
 * Loads a WAV file from disk, converts it to the device format (22050 Hz,
 * AUDIO_S16SYS, stereo), applies the engine's volume and pan values, and
 * queues the PCM directly via SDL_QueueAudio.
 *
 * This bypasses the N64 ADPCM/RSP pipeline — appropriate because mod sound
 * files are standard WAV, not ADPCM-encoded N64 SFX.
 *
 * volume: 0–0x7fff (AL_VOL_FULL = 0x7fff)
 * pan:    0–127   (AL_PAN_CENTER = 64; 0 = full left, 127 = full right)
 * Returns 1 on success, 0 on any failure (caller falls back to ROM sound).
 * ======================================================================== */
s32 audioPlayFileSound(const char *path, u16 volume, u8 pan)
{
    SDL_AudioSpec wavSpec;
    Uint8 *wavBuf = NULL;
    Uint32 wavLen = 0;

    if (SDL_LoadWAV(path, &wavSpec, &wavBuf, &wavLen) == NULL) {
        return 0;
    }

    /* Convert to device format: 22050 Hz, AUDIO_S16SYS, 2-channel */
    SDL_AudioCVT cvt;
    const int cvtResult = SDL_BuildAudioCVT(&cvt,
        wavSpec.format, wavSpec.channels, wavSpec.freq,
        AUDIO_S16SYS, 2, 22050);

    if (cvtResult < 0) {
        SDL_FreeWAV(wavBuf);
        return 0;
    }

    Uint8 *pcm;
    Uint32 pcmLen;

    if (cvtResult > 0) {
        /* Conversion required: allocate expanded buffer and convert in-place */
        const Uint32 cvtBufLen = wavLen * (Uint32)cvt.len_mult;
        pcm = SDL_malloc(cvtBufLen);
        if (!pcm) {
            SDL_FreeWAV(wavBuf);
            return 0;
        }
        memcpy(pcm, wavBuf, wavLen);
        SDL_FreeWAV(wavBuf);
        cvt.buf = pcm;
        cvt.len = (int)wavLen;
        if (SDL_ConvertAudio(&cvt) < 0) {
            SDL_free(pcm);
            return 0;
        }
        pcmLen = (Uint32)cvt.len_cvt;
    } else {
        /* No conversion needed — take ownership of wavBuf (SDL_FreeWAV = SDL_free) */
        pcm = wavBuf;
        pcmLen = wavLen;
    }

    /* Apply volume and stereo pan.
     *
     * volume is the engine scale 0–0x7fff.  Convert to float 0.0–1.0.
     * pan is 0 (full left) … 64 (centre) … 127 (full right).  Map to a
     * stereo position 0.0–1.0 and use a linear-taper panning law:
     *   leftScale  = volScale * (1 - panPos) * 2  clamped to 1.0
     *   rightScale = volScale * panPos        * 2  clamped to 1.0
     * At centre (pan=64): panPos≈0.504, left≈0.992, right≈1.0 — ~equal. */
    {
        const f32 volScale  = (f32)volume / (f32)0x7fff;
        const f32 panPos    = (f32)pan / 127.0f;
        f32 leftScale  = volScale * (1.0f - panPos) * 2.0f;
        f32 rightScale = volScale * panPos * 2.0f;
        if (leftScale  > 1.0f) leftScale  = 1.0f;
        if (rightScale > 1.0f) rightScale = 1.0f;

        s16 *samples = (s16 *)pcm;
        const Uint32 numSamples = pcmLen / sizeof(s16);
        Uint32 i;
        /* Stereo interleaved: L0 R0 L1 R1 … */
        for (i = 0; i + 1 < numSamples; i += 2) {
            samples[i]     = (s16)((s32)samples[i]     * leftScale);
            samples[i + 1] = (s16)((s32)samples[i + 1] * rightScale);
        }
    }

    SDL_QueueAudio(dev, pcm, pcmLen);
    SDL_free(pcm);
    return 1;
}

/* ========================================================================
 * Mod track playlist — persisted to pd.ini (A-4 single → playlist upgrade)
 * ======================================================================== */

static char g_AudioModTrackId[64] = "";  /* backward compat: current/first track */

/* Playlist: up to AUDIO_MAX_PLAYLIST catalog IDs. Serialized as semicolon-
 * delimited string in pd.ini (Audio.ModPlaylist). */
static char g_AudioModPlaylist[AUDIO_MAX_PLAYLIST][64];
static s32  g_AudioModPlaylistCount = 0;
static s32  g_AudioModShuffle = 1;  /* default: shuffle on */
static s32  g_AudioModPlaylistSeqIdx = 0;  /* sequential playback index */

/* pd.ini serialization buffer for playlist (semicolon-delimited) */
static char g_AudioModPlaylistStr[AUDIO_MAX_PLAYLIST * 65] = "";

static void audioPlaylistSerialize(void);  /* forward decl */

const char *audioGetModTrackId(void)
{
	/* Backward compat: return first playlist entry if playlist has entries */
	if (g_AudioModPlaylistCount > 0) {
		return g_AudioModPlaylist[0];
	}
	return g_AudioModTrackId;
}

void audioSetModTrackId(const char *id)
{
	if (id && id[0]) {
		snprintf(g_AudioModTrackId, sizeof(g_AudioModTrackId), "%s", id);
	} else {
		g_AudioModTrackId[0] = '\0';
	}
}

/* ---- Playlist API ---- */

s32 audioGetModPlaylistCount(void) { return g_AudioModPlaylistCount; }

const char *audioGetModPlaylistEntry(s32 idx)
{
	if (idx < 0 || idx >= g_AudioModPlaylistCount) return "";
	return g_AudioModPlaylist[idx];
}

s32 audioSetModPlaylistEntry(s32 idx, const char *catalog_id)
{
	if (idx < 0 || idx >= AUDIO_MAX_PLAYLIST || !catalog_id) return -1;
	snprintf(g_AudioModPlaylist[idx], 64, "%s", catalog_id);
	if (idx >= g_AudioModPlaylistCount) g_AudioModPlaylistCount = idx + 1;
	return 0;
}

s32 audioAddModPlaylistEntry(const char *catalog_id)
{
	if (!catalog_id || !catalog_id[0]) return -1;
	if (g_AudioModPlaylistCount >= AUDIO_MAX_PLAYLIST) return -1;
	/* Check for duplicates */
	for (s32 i = 0; i < g_AudioModPlaylistCount; i++) {
		if (strcmp(g_AudioModPlaylist[i], catalog_id) == 0) return i;
	}
	snprintf(g_AudioModPlaylist[g_AudioModPlaylistCount], 64, "%s", catalog_id);
	g_AudioModPlaylistCount++;
	audioPlaylistSerialize();
	return g_AudioModPlaylistCount - 1;
}

s32 audioRemoveModPlaylistEntry(const char *catalog_id)
{
	if (!catalog_id || !catalog_id[0]) return -1;
	for (s32 i = 0; i < g_AudioModPlaylistCount; i++) {
		if (strcmp(g_AudioModPlaylist[i], catalog_id) == 0) {
			/* Shift remaining entries down */
			for (s32 j = i; j < g_AudioModPlaylistCount - 1; j++) {
				memcpy(g_AudioModPlaylist[j], g_AudioModPlaylist[j + 1], 64);
			}
			g_AudioModPlaylistCount--;
			g_AudioModPlaylist[g_AudioModPlaylistCount][0] = '\0';
			if (g_AudioModPlaylistSeqIdx >= g_AudioModPlaylistCount) {
				g_AudioModPlaylistSeqIdx = 0;
			}
			audioPlaylistSerialize();
			return 0;
		}
	}
	return -1;
}

void audioClearModPlaylist(void)
{
	for (s32 i = 0; i < AUDIO_MAX_PLAYLIST; i++) {
		g_AudioModPlaylist[i][0] = '\0';
	}
	g_AudioModPlaylistCount = 0;
	g_AudioModPlaylistSeqIdx = 0;
	g_AudioModTrackId[0] = '\0';
	audioPlaylistSerialize();
}

s32 audioIsInModPlaylist(const char *catalog_id)
{
	if (!catalog_id || !catalog_id[0]) return 0;
	for (s32 i = 0; i < g_AudioModPlaylistCount; i++) {
		if (strcmp(g_AudioModPlaylist[i], catalog_id) == 0) return 1;
	}
	return 0;
}

s32 audioGetModShuffle(void) { return g_AudioModShuffle; }
void audioSetModShuffle(s32 on) { g_AudioModShuffle = on ? 1 : 0; }

const char *audioPickNextPlaylistTrack(void)
{
	if (g_AudioModPlaylistCount <= 0) return "";

	if (g_AudioModShuffle) {
		/* Random pick from playlist */
		s32 idx = rand() % g_AudioModPlaylistCount;
		/* Update backward-compat field */
		snprintf(g_AudioModTrackId, sizeof(g_AudioModTrackId), "%s",
		         g_AudioModPlaylist[idx]);
		return g_AudioModPlaylist[idx];
	}

	/* Sequential: advance index, wrap around */
	if (g_AudioModPlaylistSeqIdx >= g_AudioModPlaylistCount) {
		g_AudioModPlaylistSeqIdx = 0;
	}
	s32 idx = g_AudioModPlaylistSeqIdx;
	g_AudioModPlaylistSeqIdx++;
	/* Update backward-compat field */
	snprintf(g_AudioModTrackId, sizeof(g_AudioModTrackId), "%s",
	         g_AudioModPlaylist[idx]);
	return g_AudioModPlaylist[idx];
}

void audioResetPlaylistIndex(void)
{
	g_AudioModPlaylistSeqIdx = 0;
}

/* ---- Playlist serialization to/from pd.ini ---- */

static void audioPlaylistSerialize(void)
{
	/* Build semicolon-delimited string from playlist array */
	g_AudioModPlaylistStr[0] = '\0';
	s32 pos = 0;
	for (s32 i = 0; i < g_AudioModPlaylistCount; i++) {
		if (g_AudioModPlaylist[i][0]) {
			if (pos > 0 && pos < (s32)sizeof(g_AudioModPlaylistStr) - 1) {
				g_AudioModPlaylistStr[pos++] = ';';
			}
			s32 len = (s32)strlen(g_AudioModPlaylist[i]);
			if (pos + len < (s32)sizeof(g_AudioModPlaylistStr)) {
				memcpy(&g_AudioModPlaylistStr[pos], g_AudioModPlaylist[i], len);
				pos += len;
			}
		}
	}
	g_AudioModPlaylistStr[pos] = '\0';
}

static void audioPlaylistDeserialize(void)
{
	g_AudioModPlaylistCount = 0;
	if (!g_AudioModPlaylistStr[0]) return;

	const char *p = g_AudioModPlaylistStr;
	while (*p && g_AudioModPlaylistCount < AUDIO_MAX_PLAYLIST) {
		const char *semi = strchr(p, ';');
		s32 len = semi ? (s32)(semi - p) : (s32)strlen(p);
		if (len > 0 && len < 64) {
			memcpy(g_AudioModPlaylist[g_AudioModPlaylistCount], p, len);
			g_AudioModPlaylist[g_AudioModPlaylistCount][len] = '\0';
			g_AudioModPlaylistCount++;
		}
		if (!semi) break;
		p = semi + 1;
	}

	/* Update backward-compat single-track field */
	if (g_AudioModPlaylistCount > 0) {
		snprintf(g_AudioModTrackId, sizeof(g_AudioModTrackId), "%s",
		         g_AudioModPlaylist[0]);
	}
}

PD_CONSTRUCTOR static void audioConfigInit(void)
{
	configRegisterInt("Audio.BufferSize", &bufferSize, 0, 1 * 1024 * 1024);
	configRegisterInt("Audio.QueueLimit", &queueLimit, 0, 1 * 1024 * 1024);

	/* B-141: opt-in per-event log (default off to keep logs clean).
	 * Counters + 30s summary are always on regardless. */
	configRegisterInt("Audio.VerboseLog", &g_AudioVerboseLog, 0, 1);

	/* Volume layers — persisted as floats 0.0–1.0 */
	configRegisterFloat("Audio.MasterVolume",   &g_AudioMasterVolume,   0.0f, 1.0f);
	configRegisterFloat("Audio.MusicVolume",    &g_AudioMusicVolume,    0.0f, 1.0f);
	configRegisterFloat("Audio.GameplayVolume", &g_AudioGameplayVolume, 0.0f, 1.0f);
	configRegisterFloat("Audio.UIVolume",       &g_AudioUiVolume,       0.0f, 1.0f);

	/* Mod track playlist — semicolon-delimited catalog IDs */
	configRegisterString("Audio.ModPlaylist", g_AudioModPlaylistStr,
	                     sizeof(g_AudioModPlaylistStr));
	configRegisterInt("Audio.ModShuffle", &g_AudioModShuffle, 0, 1);

	/* Legacy single-track field (kept for backward compat with old pd.ini) */
	configRegisterString("Audio.ModTrackId", g_AudioModTrackId,
	                     sizeof(g_AudioModTrackId));

	/* After config load, deserialize playlist from string.
	 * If playlist is empty but legacy ModTrackId has a value, migrate it. */
	audioPlaylistDeserialize();
	if (g_AudioModPlaylistCount == 0 && g_AudioModTrackId[0]) {
		audioAddModPlaylistEntry(g_AudioModTrackId);
		audioPlaylistSerialize();
	}
}

/* ---------------------------------------------------------------------------
 * v34: Host-side per-frame music tick for networked playlist advancement.
 *
 * When the host is in-game with a playlist and the current track finishes,
 * pick the next track, start it locally, and broadcast SVC_MUSIC_ADVANCE
 * so all clients sync to the same track.
 * --------------------------------------------------------------------------- */

static s32 s_MusicWasPlaying = 0;  /* edge detector: was music playing last frame? */

void audioNetworkMusicTick(void)
{
	/* Only the host/listen-server advances the playlist */
	if (g_NetMode != NETMODE_SERVER_AUDIO) return;
	if (g_AudioModPlaylistCount <= 0) return;

	s32 playing = modMusicIsPlaying();

	if (s_MusicWasPlaying && !playing) {
		/* Track just ended — advance to next */
		const char *next = audioPickNextPlaylistTrack();
		if (next && next[0]) {
			/* Resolve file path and play locally */
			const asset_entry_t *ae = assetCatalogResolve(next);
			if (ae && ae->ext.audio.file_path[0]) {
				const char *fpath = fsFullPath(ae->ext.audio.file_path);
				modMusicPlay(fpath ? fpath : ae->ext.audio.file_path);
			}

			/* Broadcast to all clients in the room */
			netMusicBroadcastAdvance(next, g_LocalRoomId);

			sysLogPrintf(LOG_NOTE, "AUDIO: playlist auto-advance -> '%s'", next);
		}
	}

	s_MusicWasPlaying = playing;
}
