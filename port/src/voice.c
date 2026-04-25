/**
 * voice.c -- Phase 5 voice-chat scaffold.
 *
 * Single-writer hygiene: this module owns the enable / capture-mode /
 * PTT state. The codec follow-up will own the per-peer talking state
 * via a dedicated peer table; for the scaffold the talking-state query
 * always returns 0 (no codec means no audible peers). The settings
 * toggle and PTT key hooks are real -- the user can enable + key the
 * mic right now, and voiceLocalIsTransmitting flips appropriately for
 * the status-pill indicator. The actual audio capture / encode / send
 * pipeline is the deferred wiring.
 */

#include "voice.h"
#include "social.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

static s32                  s_Enabled;
static voice_capture_mode_t s_Mode = VOICE_CAPTURE_PUSH_TO_TALK;
static s32                  s_PttActive;
static u32                  s_RxFrames;
static u32                  s_TxFrames;

void voiceInit(void)
{
	s_Enabled = 0;
	s_Mode = VOICE_CAPTURE_PUSH_TO_TALK;
	s_PttActive = 0;
	s_RxFrames = 0;
	s_TxFrames = 0;
}

void voiceShutdown(void)
{
	s_Enabled = 0;
	s_PttActive = 0;
}

s32  voiceEnabled(void) { return s_Enabled; }

void voiceSetEnabled(s32 on)
{
	const s32 want = on ? 1 : 0;
	if (want == s_Enabled) return;
	s_Enabled = want;
	if (!want) s_PttActive = 0;
	sysLogPrintf(LOG_NOTE, "VOICE: enabled=%d", (int)s_Enabled);
}

voice_capture_mode_t voiceGetCaptureMode(void) { return s_Mode; }

void voiceSetCaptureMode(voice_capture_mode_t m)
{
	if (m != VOICE_CAPTURE_OFF && m != VOICE_CAPTURE_PUSH_TO_TALK &&
	    m != VOICE_CAPTURE_VOICE_ACTIVE) return;
	if (m == s_Mode) return;
	s_Mode = m;
	if (m == VOICE_CAPTURE_OFF) s_PttActive = 0;
	sysLogPrintf(LOG_NOTE, "VOICE: capture mode = %d", (int)m);
}

void voicePttBegin(void)
{
	if (!s_Enabled) return;
	if (s_Mode != VOICE_CAPTURE_PUSH_TO_TALK) return;
	s_PttActive = 1;
}

void voicePttEnd(void)
{
	if (!s_PttActive) return;
	s_PttActive = 0;
}

s32 voicePttActive(void) { return s_PttActive; }

s32 voiceLocalIsTransmitting(void)
{
	if (!s_Enabled) return 0;
	if (s_Mode == VOICE_CAPTURE_PUSH_TO_TALK) return s_PttActive;
	if (s_Mode == VOICE_CAPTURE_VOICE_ACTIVE) {
		/* The codec follow-up samples the SDL audio capture and decides
		 * whether the level crosses the activation threshold. The
		 * scaffold reports false. */
		return 0;
	}
	return 0;
}

s32 voicePeerIsTalking(u32 friend_handle)
{
	if (friend_handle == 0) return 0;
	if (!s_Enabled) return 0;
	const social_friend_t *f = socialFriendByHandle(friend_handle);
	if (!f) return 0;
	if (f->muted) return 0;
	/* Codec follow-up: replace with a real lookup against the per-peer
	 * decoder state (last_frame_recv_ms within ~250 ms). The scaffold
	 * never reports a peer as talking. */
	return 0;
}

u32 voiceStatsRxFrames(void) { return s_RxFrames; }
u32 voiceStatsTxFrames(void) { return s_TxFrames; }

void voiceTick(void)
{
	if (!s_Enabled) return;
	/* The codec follow-up:
	 *
	 *   if (voiceLocalIsTransmitting()) {
	 *       capture 20 ms of audio from SDL audio device
	 *       encode via opus_encode -> 60-160 byte frame
	 *       sign + broadcast to current group session peers
	 *       s_TxFrames++;
	 *   }
	 *   for each inbound voice packet:
	 *       verify (handle bind + signature + per-friend mute)
	 *       opus_decode
	 *       SDL audio device queue
	 *       s_RxFrames++;
	 */
}
