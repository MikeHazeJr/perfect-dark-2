#include <PR/ultratypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <libaudio.h>
#include "platform.h"
#include "config.h"
#include "audio.h"
#include "modmusic.h"
#include "assetcatalog.h"
#include "fs.h"
#include "system.h"
#include "types.h"

#define AL_SNDP_STOP_EVT  0x0002
#define AL_SNDP_PAN_EVT   0x0004
#define AL_SNDP_VOL_EVT   0x0008
#define AL_SNDP_PITCH_EVT 0x0010
#define AL_SNDP_0400_EVT  0x0400
#define AL_SNDP_1000_EVT  0x1000

/* Network externs for music tick (avoid pulling in full net headers) */
extern s32 g_NetMode;
extern s32 g_NetLocalBotAuthority; /* true if we are the host/bot authority */
#define NETMODE_SERVER_AUDIO 1
/* From netmsg.h -- broadcast next track to room. Issue 4b: gained
 * match_clock_offset_ms after track_id so the host can publish drift-
 * correction updates as well as track-change events. */
extern void netMusicBroadcastAdvance(const char *track_id, u8 room_id, u32 match_clock_offset_ms);
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

/* pd.ini baseline: volumes captured at audioNotifyEngineReady before any
 * per-agent sidecar overlays them.  audioResetToDefaults() restores these
 * so that loading an agent with no [Audio] block reverts to pd.ini values. */
static f32 g_AudioBaselineMaster   = 1.0f;
static f32 g_AudioBaselineMusic    = 1.0f;
static f32 g_AudioBaselineGameplay = 1.0f;
static f32 g_AudioBaselineUi       = 1.0f;

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

/* S301 B-141 DIAG expansion: track mixer-state & scheduling-gap signals
 * that let us narrow "audio skips" into one of four buckets:
 *   - render producer starved (nextBuf NULL when audioEndFrame fires)
 *   - consumer stalled (buffered never drains even though producer is fine)
 *   - large scheduling gap between audioEndFrame calls (OS jitter)
 *   - mix buffer overrun (s_MixBuf capacity exceeded)
 * All counters are monotonic; included in the 30s summary and the first
 * DIAG log line of each summary window carries the min/max gap + peak
 * buffered for more forensic info. */
static u32 s_AudioDiagNullProducerCount = 0;  /* audioEndFrame with nextBuf==NULL */
static u32 s_AudioDiagMixBufOverflowCount = 0;/* numSamples > mix buffer size */
static u32 s_AudioDiagMaxGapMs = 0;           /* peak gap seen this window */
static u32 s_AudioDiagMaxBufferedSamples = 0; /* peak queue depth this window */
static u32 s_AudioDiagMinBufferedSamples = 0; /* floor queue depth this window */
static u64 s_AudioDiagSumGapMs = 0;           /* for mean gap calc */
static u32 s_AudioDiagSumGapCount = 0;        /* denominator for mean */
static u32 s_AudioDiagFrameCount = 0;         /* audioEndFrame calls this window */

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

	/* S301 DIAG: log the audio device's actual spec (drivers may grant a
	 * different config than we asked for — sample-rate mismatch has
	 * historically caused the B-82 22020/22050 bug). */
	sysLogPrintf(LOG_NOTE,
		"AUDIO.DIAG: init dev=%u wanted(freq=%d fmt=0x%04x ch=%d samples=%d) "
		"got(freq=%d fmt=0x%04x ch=%d samples=%d size=%u) queueLimit=%d",
		(unsigned)dev, want.freq, want.format, want.channels, want.samples,
		have.freq, have.format, have.channels, have.samples,
		(unsigned)have.size, queueLimit);
	if (have.freq != want.freq) {
		sysLogPrintf(LOG_WARNING,
			"AUDIO.DIAG: SAMPLE RATE MISMATCH wanted=%d got=%d — "
			"expect pitch-shifted audio (B-82 class)",
			want.freq, have.freq);
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

#define AUDIO_FILE_SOUND_MAX 32

typedef struct audio_file_sound_s {
	s32 allocated;
	struct sndstate state;
	s16 *pcm;
	u32 frames;
	f32 cursor;
	f32 step;
	u16 volume;
	u8 pan;
	u32 loop_start_frame;
	u32 loop_end_frame;
	u32 loop_count;
	s32 has_loop;
	s32 has_envelope;
	u32 age_frames;
	u32 attack_frames;
	u32 decay_frames;
	u32 release_frames;
	f32 attack_volume_scale;
	f32 decay_volume_scale;
	s32 releasing;
	u32 release_elapsed_frames;
	f32 release_start_scale;
	struct sndstate **handle;
} audio_file_sound_t;

static audio_file_sound_t s_FileSounds[AUDIO_FILE_SOUND_MAX];

static audio_file_sound_t *audioFindFileSound(const struct sndstate *handle)
{
	s32 i;
	for (i = 0; i < AUDIO_FILE_SOUND_MAX; i++) {
		if (s_FileSounds[i].allocated && &s_FileSounds[i].state == handle) {
			return &s_FileSounds[i];
		}
	}
	return NULL;
}

s32 audioFileSoundOwnsHandle(const struct sndstate *handle)
{
	return audioFindFileSound(handle) != NULL;
}

s32 audioFileSoundGetState(const struct sndstate *handle)
{
	audio_file_sound_t *slot = audioFindFileSound(handle);
	return slot ? slot->state.state : AL_STOPPED;
}

static void audioFileSoundRelease(audio_file_sound_t *slot)
{
	if (!slot) return;
	if (slot->pcm) {
		SDL_free(slot->pcm);
		slot->pcm = NULL;
	}
	slot->frames = 0;
	slot->cursor = 0.0f;
	slot->state.state = AL_STOPPED;
	slot->allocated = 0;
	if (slot->handle && *slot->handle == &slot->state) {
		*slot->handle = NULL;
	}
	slot->handle = NULL;
}

static f32 audioFileSoundEnvelopeScale(const audio_file_sound_t *slot)
{
	f32 scale;
	u32 decay_pos;
	u32 total_decay_start;

	if (!slot || !slot->has_envelope) {
		return 1.0f;
	}

	if (slot->releasing) {
		if (slot->release_frames == 0 ||
				slot->release_elapsed_frames >= slot->release_frames) {
			return 0.0f;
		}
		scale = 1.0f - ((f32)slot->release_elapsed_frames /
				(f32)slot->release_frames);
		return slot->release_start_scale * scale;
	}

	if (slot->attack_frames > 0 && slot->age_frames < slot->attack_frames) {
		scale = (f32)(slot->age_frames + 1) / (f32)slot->attack_frames;
		return slot->attack_volume_scale * scale;
	}

	total_decay_start = slot->attack_frames;
	if (slot->decay_frames > 0 &&
			slot->age_frames < total_decay_start + slot->decay_frames) {
		decay_pos = slot->age_frames > total_decay_start
			? slot->age_frames - total_decay_start : 0;
		scale = (f32)decay_pos / (f32)slot->decay_frames;
		return slot->attack_volume_scale +
			(slot->decay_volume_scale - slot->attack_volume_scale) * scale;
	}

	return slot->decay_volume_scale;
}

static void audioFileSoundBeginRelease(audio_file_sound_t *slot)
{
	if (!slot || slot->state.state == AL_STOPPED) {
		return;
	}

	if (!slot->has_envelope || slot->release_frames == 0) {
		audioFileSoundRelease(slot);
		return;
	}

	slot->release_start_scale = audioFileSoundEnvelopeScale(slot);
	slot->release_elapsed_frames = 0;
	slot->releasing = 1;
	slot->state.state = AL_STOPPING;
}

void audioFileSoundStop(struct sndstate *handle)
{
	audio_file_sound_t *slot = audioFindFileSound(handle);
	if (slot) {
		audioFileSoundBeginRelease(slot);
	}
}

void audioFileSoundPostEvent(struct sndstate *handle, s16 type, s32 data)
{
	audio_file_sound_t *slot = audioFindFileSound(handle);
	if (!slot || (slot->state.state != AL_PLAYING && slot->state.state != AL_STOPPING)) {
		return;
	}

	switch (type) {
	case AL_SNDP_VOL_EVT:
		slot->volume = (u16)data;
		slot->state.vol = (s16)data;
		break;
	case AL_SNDP_PAN_EVT:
		if (data < 0) data = 0;
		if (data > 127) data = 127;
		slot->pan = (u8)data;
		slot->state.pan = (u8)data;
		break;
	case AL_SNDP_PITCH_EVT:
		memcpy(&slot->step, &data, sizeof(slot->step));
		if (slot->step <= 0.0f) slot->step = 1.0f;
		slot->state.pitch = slot->step;
		break;
	case AL_SNDP_STOP_EVT:
	case AL_SNDP_0400_EVT:
	case AL_SNDP_1000_EVT:
		audioFileSoundBeginRelease(slot);
		break;
	default:
		break;
	}
}

static s32 audioFileSoundsActive(void)
{
	s32 i;
	for (i = 0; i < AUDIO_FILE_SOUND_MAX; i++) {
		if ((s_FileSounds[i].state.state == AL_PLAYING ||
				s_FileSounds[i].state.state == AL_STOPPING) &&
				s_FileSounds[i].pcm) {
			return 1;
		}
	}
	return 0;
}

static s32 audioLoadFilePcmStereo22050(const char *path, s16 **out_pcm,
		u32 *out_frames, s32 *out_source_rate)
{
	SDL_AudioSpec wavSpec;
	Uint8 *wavBuf = NULL;
	Uint32 wavLen = 0;
	u32 fileSize = 0;
	void *fileBytes = fsFileLoad(path, &fileSize);

	*out_pcm = NULL;
	*out_frames = 0;
	*out_source_rate = 22050;

	if (fileBytes && fileSize > 0 && fileSize <= 0x7fffffffU) {
		SDL_RWops *rw = SDL_RWFromConstMem(fileBytes, (int)fileSize);
		if (rw) {
			SDL_LoadWAV_RW(rw, 1, &wavSpec, &wavBuf, &wavLen);
		}
		free(fileBytes);
	} else if (fileBytes) {
		free(fileBytes);
	}

	if (!wavBuf) {
		if (SDL_LoadWAV(path, &wavSpec, &wavBuf, &wavLen) == NULL) {
			return 0;
		}
	}

	*out_source_rate = wavSpec.freq > 0 ? wavSpec.freq : 22050;

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
		pcm = wavBuf;
		pcmLen = wavLen;
	}

	*out_pcm = (s16 *)pcm;
	*out_frames = pcmLen / (sizeof(s16) * 2u);
	return *out_frames > 0;
}

static u32 audioScaleSourceFrame(u32 source_frame, s32 source_rate, u32 frames)
{
	u64 scaled;
	if (source_rate <= 0) source_rate = 22050;
	scaled = ((u64)source_frame * 22050u + (u32)(source_rate / 2)) / (u32)source_rate;
	if (scaled > frames) scaled = frames;
	return (u32)scaled;
}

static u32 audioEnvelopeFrames(u32 time_us, f32 pitch)
{
	f32 frames;

	if (time_us == 0) {
		return 0;
	}
	if (pitch <= 0.0f) {
		pitch = 1.0f;
	}

	frames = (((f32)time_us / pitch) * 22050.0f) / 1000000.0f;
	if (frames < 1.0f) {
		return 1;
	}
	if (frames > 4294967295.0f) {
		return 0xffffffffu;
	}
	return (u32)(frames + 0.5f);
}

static void audioMixFileSoundsInto(s16 *dst, u32 frames)
{
	s32 slot_index;
	for (slot_index = 0; slot_index < AUDIO_FILE_SOUND_MAX; slot_index++) {
		audio_file_sound_t *slot = &s_FileSounds[slot_index];
		u32 i;
		f32 baseVolScale;
		f32 panPos;

		if ((slot->state.state != AL_PLAYING && slot->state.state != AL_STOPPING) ||
				!slot->pcm || slot->frames == 0) {
			continue;
		}

		baseVolScale = ((f32)slot->volume / (f32)0x7fff) *
			g_AudioMasterVolume * g_AudioGameplayVolume;
		panPos = (f32)slot->pan / 127.0f;

		for (i = 0; i < frames; i++) {
			f32 volScale;
			f32 leftScale;
			f32 rightScale;
			u32 src_frame;
			s32 l;
			s32 r;
			if (slot->cursor >= (f32)slot->frames) {
				audioFileSoundRelease(slot);
				break;
			}

			volScale = baseVolScale * audioFileSoundEnvelopeScale(slot);
			leftScale = volScale * (1.0f - panPos) * 2.0f;
			rightScale = volScale * panPos * 2.0f;
			if (leftScale > 1.0f) leftScale = 1.0f;
			if (rightScale > 1.0f) rightScale = 1.0f;

			src_frame = (u32)slot->cursor;
			l = dst[i * 2] + (s32)((f32)slot->pcm[src_frame * 2] * leftScale);
			r = dst[i * 2 + 1] + (s32)((f32)slot->pcm[src_frame * 2 + 1] * rightScale);
			if (l > 32767) l = 32767;
			if (l < -32768) l = -32768;
			if (r > 32767) r = 32767;
			if (r < -32768) r = -32768;
			dst[i * 2] = (s16)l;
			dst[i * 2 + 1] = (s16)r;

			slot->cursor += slot->step;
			if (slot->releasing) {
				slot->release_elapsed_frames++;
				if (slot->release_elapsed_frames >= slot->release_frames) {
					audioFileSoundRelease(slot);
					break;
				}
			} else {
				slot->age_frames++;
			}
			if (slot->has_loop && slot->loop_end_frame > slot->loop_start_frame
					&& slot->cursor >= (f32)slot->loop_end_frame) {
				const f32 loop_len = (f32)(slot->loop_end_frame - slot->loop_start_frame);
				if (slot->loop_count == 0xffffffffu) {
					while (slot->cursor >= (f32)slot->loop_end_frame) {
						slot->cursor -= loop_len;
					}
					if (slot->cursor < (f32)slot->loop_start_frame) {
						slot->cursor = (f32)slot->loop_start_frame;
					}
				} else if (slot->loop_count > 0) {
					slot->loop_count--;
					while (slot->cursor >= (f32)slot->loop_end_frame) {
						slot->cursor -= loop_len;
					}
					if (slot->cursor < (f32)slot->loop_start_frame) {
						slot->cursor = (f32)slot->loop_start_frame;
					}
				}
			}
		}
	}
}

void audioEndFrame(void)
{
	const u32 now = SDL_GetTicks();

	s_AudioDiagFrameCount++;

	/* B-141: hitch detection — gap between consecutive pushes exceeds
	 * threshold. Main loop stalled; audio will underrun right after. */
	if (s_AudioLastEndTick != 0) {
		const u32 delta = now - s_AudioLastEndTick;
		/* S301 DIAG: accumulate scheduler-gap stats regardless of
		 * whether the gap is a "hitch" — peak + mean across the window
		 * is useful even when no event fires. */
		if (delta > s_AudioDiagMaxGapMs) s_AudioDiagMaxGapMs = delta;
		s_AudioDiagSumGapMs += delta;
		s_AudioDiagSumGapCount++;

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

		/* S301 DIAG: track peak/floor queue depth across the window. */
		if ((u32)buffered > s_AudioDiagMaxBufferedSamples) {
			s_AudioDiagMaxBufferedSamples = (u32)buffered;
		}
		if (s_AudioDiagMinBufferedSamples == 0
				|| (u32)buffered < s_AudioDiagMinBufferedSamples) {
			s_AudioDiagMinBufferedSamples = (u32)buffered;
		}

		/* B-141: underrun detection — consumer chewed through the queue. */
		if (buffered < AUDIO_UNDERRUN_THRESHOLD_SAMPLES) {
			g_AudioUnderrunCount++;
			if (g_AudioVerboseLog) {
				sysLogPrintf(LOG_WARNING, "AUDIO[B-141]: underrun buffered=%d", buffered);
			}
		}

		if (buffered < queueLimit) {
			const s32 file_sounds_active = audioFileSoundsActive();
			if (modMusicIsPlaying() || file_sounds_active) {
				/* Copy N64 output into writable buffer, mix source-backed
				 * music and file SFX on top, then queue one combined frame. */
				u32 numSamples = nextSize / sizeof(s16);
				u32 numFrames = numSamples / 2;

				if (numSamples > sizeof(s_MixBuf) / sizeof(s16)) {
					s_AudioDiagMixBufOverflowCount++;
					if (g_AudioVerboseLog) {
						sysLogPrintf(LOG_WARNING,
							"AUDIO.DIAG: mix buffer OVERFLOW req=%u cap=%zu — truncating",
							(unsigned)numSamples,
							sizeof(s_MixBuf) / sizeof(s16));
					}
					numSamples = sizeof(s_MixBuf) / sizeof(s16);
					numFrames = numSamples / 2;
				}

				memcpy(s_MixBuf, nextBuf, numSamples * sizeof(s16));
				if (modMusicIsPlaying()) {
					modMusicMixInto(s_MixBuf, numFrames);
				}
				if (file_sounds_active) {
					audioMixFileSoundsInto(s_MixBuf, numFrames);
				}
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
	} else {
		/* S301 DIAG: audioEndFrame fired but no producer buffer was
		 * queued this frame. Normal for paused states; excessive in a
		 * running match means the RSP driver is not producing PCM
		 * (musicTick / sndTick not firing). */
		s_AudioDiagNullProducerCount++;
	}

	/* B-141: periodic 30s summary if anything happened in the window */
	if (now - s_AudioLastSummaryTick > AUDIO_SUMMARY_INTERVAL_MS) {
		s_AudioLastSummaryTick = now;
		if (g_AudioDropCount || g_AudioUnderrunCount || g_AudioHitchCount
				|| s_AudioDiagNullProducerCount || s_AudioDiagMixBufOverflowCount) {
			const u32 meanGap = s_AudioDiagSumGapCount
				? (u32)(s_AudioDiagSumGapMs / s_AudioDiagSumGapCount) : 0;
			sysLogPrintf(LOG_NOTE,
				"AUDIO[B-141]: 30s summary drops=%u underruns=%u hitches=%u "
				"nullProducer=%u mixOverflow=%u frames=%u gap(ms) max=%u mean=%u "
				"buffered(samples) min=%u max=%u",
				(unsigned)g_AudioDropCount,
				(unsigned)g_AudioUnderrunCount,
				(unsigned)g_AudioHitchCount,
				(unsigned)s_AudioDiagNullProducerCount,
				(unsigned)s_AudioDiagMixBufOverflowCount,
				(unsigned)s_AudioDiagFrameCount,
				(unsigned)s_AudioDiagMaxGapMs,
				(unsigned)meanGap,
				(unsigned)s_AudioDiagMinBufferedSamples,
				(unsigned)s_AudioDiagMaxBufferedSamples);
		}
		/* Reset per-window aggregates but keep monotonic counters. */
		s_AudioDiagNullProducerCount = 0;
		s_AudioDiagMixBufOverflowCount = 0;
		s_AudioDiagMaxGapMs = 0;
		s_AudioDiagMaxBufferedSamples = 0;
		s_AudioDiagMinBufferedSamples = 0;
		s_AudioDiagSumGapMs = 0;
		s_AudioDiagSumGapCount = 0;
		s_AudioDiagFrameCount = 0;
	}

	/* Issue 4b (2026-04-24): client-side music sync correction. Runs
	 * once per audio output frame, after the mix has been queued, so
	 * any rate adjustment lands on the next mixInto call. No-op when
	 * not playing or when no SVC_MUSIC_ADVANCE has been received yet. */
	audioMusicSyncCorrectionTick();
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
	/* Snapshot pd.ini values before any per-agent sidecar can overlay them. */
	g_AudioBaselineMaster   = g_AudioMasterVolume;
	g_AudioBaselineMusic    = g_AudioMusicVolume;
	g_AudioBaselineGameplay = g_AudioGameplayVolume;
	g_AudioBaselineUi       = g_AudioUiVolume;
	audioApplyVolumes();
}

void audioResetToDefaults(void)
{
	audioSetMasterVolume(g_AudioBaselineMaster);
	audioSetMusicVolume(g_AudioBaselineMusic);
	audioSetGameplayVolume(g_AudioBaselineGameplay);
	audioSetUiVolume(g_AudioBaselineUi);
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
s32 audioPlayFileSound(const char *path, u16 volume, u8 pan, f32 pitch)
{
    SDL_AudioSpec wavSpec;
    Uint8 *wavBuf = NULL;
    Uint32 wavLen = 0;
    u32 fileSize = 0;
    void *fileBytes = fsFileLoad(path, &fileSize);

    if (fileBytes && fileSize > 0 && fileSize <= 0x7fffffffU) {
        SDL_RWops *rw = SDL_RWFromConstMem(fileBytes, (int)fileSize);
        if (rw) {
            SDL_LoadWAV_RW(rw, 1, &wavSpec, &wavBuf, &wavLen);
        }
        free(fileBytes);
    } else if (fileBytes) {
        free(fileBytes);
    }

    if (!wavBuf) {
        if (SDL_LoadWAV(path, &wavSpec, &wavBuf, &wavLen) == NULL) {
            return 0;
        }
    }

    if (pitch <= 0.0f) {
        pitch = 1.0f;
    }

    s32 pitchFreq = (s32)((f32)wavSpec.freq * pitch + 0.5f);
    if (pitchFreq < 1000) {
        pitchFreq = 1000;
    } else if (pitchFreq > 192000) {
        pitchFreq = 192000;
    }

    /* Convert to device format: 22050 Hz, AUDIO_S16SYS, 2-channel.
     * Raising the declared source frequency shortens the converted stream,
     * matching the native sound-player pitch behavior for file sources. */
    SDL_AudioCVT cvt;
    const int cvtResult = SDL_BuildAudioCVT(&cvt,
        wavSpec.format, wavSpec.channels, pitchFreq,
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
        /* Apply gameplay channel volume (master × gameplay) so mod WAV SFX
         * respect the same volume layers as ROM sounds scaled by g_SfxVolume. */
        const f32 volScale  = ((f32)volume / (f32)0x7fff) * g_AudioMasterVolume * g_AudioGameplayVolume;
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

struct sndstate *audioStartFileSound(const char *path, u16 volume, u8 pan,
		f32 pitch, s32 has_loop, u32 loop_start_samples,
		u32 loop_end_samples, u32 loop_count, s32 has_envelope,
		u32 attack_time_us, u32 decay_time_us, u32 release_time_us,
		s32 attack_volume, s32 decay_volume, struct sndstate **handle)
{
	s16 *pcm = NULL;
	u32 frames = 0;
	s32 source_rate = 22050;
	audio_file_sound_t *slot = NULL;
	s32 i;

	if (pitch <= 0.0f) {
		pitch = 1.0f;
	}

	if (!audioLoadFilePcmStereo22050(path, &pcm, &frames, &source_rate)) {
		return NULL;
	}

	for (i = 0; i < AUDIO_FILE_SOUND_MAX; i++) {
		if (s_FileSounds[i].state.state == AL_STOPPED) {
			slot = &s_FileSounds[i];
			audioFileSoundRelease(slot);
			break;
		}
	}

	if (!slot) {
		SDL_free(pcm);
		return NULL;
	}

	memset(&slot->state, 0, sizeof(slot->state));
	slot->allocated = 1;
	slot->pcm = pcm;
	slot->frames = frames;
	slot->cursor = 0.0f;
	slot->step = pitch;
	slot->volume = volume;
	slot->pan = pan;
	slot->loop_start_frame = audioScaleSourceFrame(loop_start_samples, source_rate, frames);
	slot->loop_end_frame = audioScaleSourceFrame(loop_end_samples, source_rate, frames);
	slot->loop_count = loop_count;
	slot->has_loop = has_loop
		&& slot->loop_end_frame > slot->loop_start_frame
		&& slot->loop_start_frame < frames;
	slot->has_envelope = has_envelope;
	slot->age_frames = 0;
	slot->attack_frames = audioEnvelopeFrames(attack_time_us, pitch);
	slot->decay_frames = audioEnvelopeFrames(decay_time_us, pitch);
	slot->release_frames = audioEnvelopeFrames(release_time_us, pitch);
	if (attack_volume < 0) attack_volume = 0;
	if (attack_volume > 127) attack_volume = 127;
	if (decay_volume < 0) decay_volume = 0;
	if (decay_volume > 127) decay_volume = 127;
	slot->attack_volume_scale = (f32)attack_volume / 127.0f;
	slot->decay_volume_scale = (f32)decay_volume / 127.0f;
	slot->releasing = 0;
	slot->release_elapsed_frames = 0;
	slot->release_start_scale = 1.0f;
	slot->handle = handle;
	slot->state.state = AL_PLAYING;
	slot->state.vol = (s16)volume;
	slot->state.pan = pan;
	slot->state.pitch = pitch;
	slot->state.flags = 0;

	if (handle) {
		*handle = &slot->state;
	}

	return &slot->state;
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
 *
 * Issue 4b (2026-04-24): also re-broadcast every MUSIC_SYNC_REBROADCAST_MS
 * with the host's authoritative track offset so clients can lerp drift away.
 * --------------------------------------------------------------------------- */

static s32  s_MusicWasPlaying       = 0;  /* edge detector: was music playing last frame? */
static char s_MusicCurrentTrackId[CATALOG_ID_LEN] = {0};
static u64  s_MusicTrackStartedAtMs = 0;  /* SDL_GetTicks64 when the current track began on host */
static u64  s_MusicLastSyncTxMs     = 0;  /* SDL_GetTicks64 of the most recent sync re-broadcast */

#define MUSIC_SYNC_REBROADCAST_MS 2000u  /* re-broadcast cadence */

void audioNetworkMusicTick(void)
{
	/* Only the host/listen-server advances the playlist */
	if (g_NetMode != NETMODE_SERVER_AUDIO) return;
	if (g_AudioModPlaylistCount <= 0) return;

	s32 playing = modMusicIsPlaying();
	u64 now = SDL_GetTicks64();

	if (s_MusicWasPlaying && !playing) {
		/* Track just ended — advance to next */
		const char *next = audioPickNextPlaylistTrack();
		if (next && next[0]) {
			/* Resolve file path and play locally */
			const asset_entry_t *ae = assetCatalogResolve(next);
			if (ae && ae->ext.audio.file_path[0]) {
				char fpathBuf[FS_MAXPATH + 1];
				const char *fpath = fsFullPath(ae->ext.audio.file_path, fpathBuf, sizeof(fpathBuf));
				modMusicPlay(fpath ? fpath : ae->ext.audio.file_path);
			}

			/* Issue 4b: track_change event = offset 0; capture wall-clock
			 * for subsequent drift-correction broadcasts. */
			strncpy(s_MusicCurrentTrackId, next, sizeof(s_MusicCurrentTrackId) - 1);
			s_MusicCurrentTrackId[sizeof(s_MusicCurrentTrackId) - 1] = '\0';
			s_MusicTrackStartedAtMs = now;
			s_MusicLastSyncTxMs     = now;

			netMusicBroadcastAdvance(next, g_LocalRoomId, 0u);

			sysLogPrintf(LOG_NOTE, "AUDIO: playlist auto-advance -> '%s'", next);
		}
	}

	/* Issue 4b: periodic re-broadcast of (track_id, current offset) so
	 * clients can compute drift and lerp. Skip if no track is loaded
	 * or the track is between songs. */
	if (playing && s_MusicCurrentTrackId[0] &&
	    (now - s_MusicLastSyncTxMs) >= MUSIC_SYNC_REBROADCAST_MS) {
		u32 offset_ms;
		if (now > s_MusicTrackStartedAtMs) {
			offset_ms = (u32)(now - s_MusicTrackStartedAtMs);
		} else {
			offset_ms = 0u;
		}
		netMusicBroadcastAdvance(s_MusicCurrentTrackId, g_LocalRoomId, offset_ms);
		s_MusicLastSyncTxMs = now;
	}

	s_MusicWasPlaying = playing;
}

/* ---------------------------------------------------------------------------
 * Issue 4b (2026-04-24): client-side music sync receiver + correction tick.
 *
 * On every SVC_MUSIC_ADVANCE the client lands here. The packet either
 * announces a track change (start fresh playback) or refreshes the
 * authoritative offset for the same track (drift-update). The receiver
 * stores enough state for audioMusicSyncCorrectionTick() to compute
 * drift each frame and apply rate-lerp or hard-seek as needed.
 *
 * Why store wall-clock offsets and not absolute server time: the client
 * does not share a clock with the host. Instead we record (1) the local
 * wall-clock at the moment the packet was received, and (2) the host's
 * reported offset at that moment. Any later moment T's expected offset
 * is then  `host_offset_at_recv + (T - recv_local_wallclock)`.
 * --------------------------------------------------------------------------- */

#define MUSIC_SYNC_LERP_GAIN_K       0.001f  /* drift_ms * K -> rate adjustment */
#define MUSIC_SYNC_LERP_BAND_MS      30      /* drift smaller than this is ignored */
#define MUSIC_SYNC_HARD_SEEK_MS      5000    /* drift larger than this triggers hard-seek */
#define MUSIC_SYNC_LOG_INTERVAL_MS   2000    /* throttle "MUSIC.SYNC: drift= ... rate= ..." */

static char s_MusicSyncTrackId[CATALOG_ID_LEN] = {0};
static u64  s_MusicSyncRecvWallclockMs = 0;
static u32  s_MusicSyncOffsetAtRecvMs  = 0;
static s32  s_MusicSyncActive          = 0;
static u64  s_MusicSyncLastLogMs       = 0;

void audioMusicSyncReceive(const char *track_id, u32 match_clock_offset_ms)
{
	if (!track_id || !track_id[0]) return;

	const s32 sameTrack = (s_MusicSyncTrackId[0] != '\0' &&
	                       strncmp(s_MusicSyncTrackId, track_id, sizeof(s_MusicSyncTrackId)) == 0);

	/* Always refresh the (track_id, recv_wallclock, recorded_offset)
	 * triple. On a track-change we also start the track locally and
	 * hard-seek to the server's current offset (for late-joiners or
	 * mid-track tune-ins). */
	const u64 now = SDL_GetTicks64();
	strncpy(s_MusicSyncTrackId, track_id, sizeof(s_MusicSyncTrackId) - 1);
	s_MusicSyncTrackId[sizeof(s_MusicSyncTrackId) - 1] = '\0';
	s_MusicSyncRecvWallclockMs = now;
	s_MusicSyncOffsetAtRecvMs  = match_clock_offset_ms;
	s_MusicSyncActive          = 1;

	if (!sameTrack) {
		audioSetModTrackId(track_id);
		const asset_entry_t *ae = assetCatalogResolve(track_id);
		if (ae && ae->ext.audio.file_path[0]) {
			char fpathBuf[FS_MAXPATH + 1];
			const char *fpath = fsFullPath(ae->ext.audio.file_path, fpathBuf, sizeof(fpathBuf));
			modMusicPlay(fpath ? fpath : ae->ext.audio.file_path);
			/* Late-join: skip ahead to the host's current offset so
			 * the listener doesn't replay 30 seconds of intro. */
			if (match_clock_offset_ms > 0u) {
				modMusicSetPositionMs(match_clock_offset_ms);
			}
			sysLogPrintf(LOG_NOTE, "MUSIC.SYNC: track-change -> '%s' offset=%u ms",
			             track_id, match_clock_offset_ms);
		} else {
			sysLogPrintf(LOG_WARNING, "MUSIC.SYNC: track '%s' not in catalog", track_id);
			s_MusicSyncActive = 0;
		}
	}
	/* same-track receive: drift-update only. The correction tick below
	 * will compute drift on the next audio frame and adjust rate. */
}

void audioMusicSyncCorrectionTick(void)
{
	if (!s_MusicSyncActive) return;
	if (!modMusicIsPlaying()) {
		/* Track ended locally before the next sync packet; reset to
		 * idle so a fresh track-change message starts cleanly. */
		s_MusicSyncActive = 0;
		modMusicSetRate(1.0f);
		return;
	}

	const u64 now = SDL_GetTicks64();

	/* Expected offset at the host right now: the offset they sent us
	 * at recv-time, plus however much wall-clock has elapsed since. */
	u64 elapsed_since_recv = (now > s_MusicSyncRecvWallclockMs)
	                         ? (now - s_MusicSyncRecvWallclockMs) : 0u;
	u64 expected_offset_ms = (u64)s_MusicSyncOffsetAtRecvMs + elapsed_since_recv;

	u32 local_offset_ms = modMusicGetPositionMs();
	s64 drift_ms        = (s64)expected_offset_ms - (s64)local_offset_ms;

	/* Hard-seek for drift larger than the lerp window. */
	if (drift_ms >  MUSIC_SYNC_HARD_SEEK_MS ||
	    drift_ms < -MUSIC_SYNC_HARD_SEEK_MS) {
		u32 target = (expected_offset_ms < (u64)0xFFFFFFFFu)
		             ? (u32)expected_offset_ms : 0xFFFFFFFFu;
		sysLogPrintf(LOG_WARNING,
		             "MUSIC.SYNC: hard-seek (drift=%lld ms exceeded +-%d ms window) target=%u",
		             (long long)drift_ms, MUSIC_SYNC_HARD_SEEK_MS, target);
		modMusicSetPositionMs(target);
		modMusicSetRate(1.0f);
		return;
	}

	/* Inside the lerp dead-band, hold rate at 1.0 so we do not jitter
	 * around perfect alignment. */
	if (drift_ms > -MUSIC_SYNC_LERP_BAND_MS && drift_ms < MUSIC_SYNC_LERP_BAND_MS) {
		modMusicSetRate(1.0f);
		return;
	}

	/* Compute the rate. drift > 0 means client is BEHIND the host =>
	 * speed up (rate > 1). drift < 0 means client is AHEAD => slow
	 * down (rate < 1). modMusicSetRate already clamps to [0.97, 1.03]. */
	f32 rate = 1.0f + ((f32)drift_ms) * MUSIC_SYNC_LERP_GAIN_K;
	modMusicSetRate(rate);

	/* Throttle the log line so we don't spam the file every audio
	 * frame; the rate is still applied every tick. */
	if ((now - s_MusicSyncLastLogMs) >= MUSIC_SYNC_LOG_INTERVAL_MS) {
		f32 effective = modMusicGetRate();
		sysLogPrintf(LOG_NOTE, "MUSIC.SYNC: drift=%lld ms rate=%.3f",
		             (long long)drift_ms, effective);
		s_MusicSyncLastLogMs = now;
	}
}
