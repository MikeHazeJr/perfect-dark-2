#include <PR/ultratypes.h>
#include <stdio.h>
#include <SDL.h>
#include "platform.h"
#include "config.h"
#include "audio.h"
#include "system.h"

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

s32 audioInit(void)
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		sysLogPrintf(LOG_ERROR, "SDL audio init error: %s", SDL_GetError());
		return -1;
	}

	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 22020; // TODO: this might cause trouble for some platforms
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

void audioEndFrame(void)
{
	if (nextBuf && nextSize) {
		if (audioGetSamplesBuffered() < queueLimit) {
			SDL_QueueAudio(dev, nextBuf, nextSize);
		}
		nextBuf = NULL;
		nextSize = 0;
	}
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
 * Loads a WAV file from disk, converts it to the device format (22020 Hz,
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

    /* Convert to device format: 22020 Hz, AUDIO_S16SYS, 2-channel */
    SDL_AudioCVT cvt;
    const int cvtResult = SDL_BuildAudioCVT(&cvt,
        wavSpec.format, wavSpec.channels, wavSpec.freq,
        AUDIO_S16SYS, 2, 22020);

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

PD_CONSTRUCTOR static void audioConfigInit(void)
{
	configRegisterInt("Audio.BufferSize", &bufferSize, 0, 1 * 1024 * 1024);
	configRegisterInt("Audio.QueueLimit", &queueLimit, 0, 1 * 1024 * 1024);

	/* Volume layers — persisted as floats 0.0–1.0 */
	configRegisterFloat("Audio.MasterVolume",   &g_AudioMasterVolume,   0.0f, 1.0f);
	configRegisterFloat("Audio.MusicVolume",    &g_AudioMusicVolume,    0.0f, 1.0f);
	configRegisterFloat("Audio.GameplayVolume", &g_AudioGameplayVolume, 0.0f, 1.0f);
	configRegisterFloat("Audio.UIVolume",       &g_AudioUiVolume,       0.0f, 1.0f);
}
