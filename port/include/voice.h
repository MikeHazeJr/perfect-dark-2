/**
 * voice.h -- Phase 5 push-to-talk voice chat scaffold.
 *
 * Voice is opt-in, default off, gated behind a Settings toggle. Push-to-
 * talk is the default capture mode (configurable to voice-activated in a
 * follow-up). Per-friend mute integrates with `socialFriend.muted`
 * from the existing Q9 sidebar UX.
 *
 * Codec: Opus is the documented default (industry-standard low-latency
 * narrowband-to-fullband codec, ITU-T G.193 interoperable, BSD-licensed
 * reference implementation, ~80 kbps for high-quality voice and as low
 * as 6 kbps for narrow-band). The encoder + decoder integration ships
 * in a follow-up commit because libopus is a new third-party dependency
 * that needs CMakeLists.txt vetting on Mike's side. The state machine,
 * settings, and UI scaffold ship now so the voice subsystem can be
 * tested end-to-end on a single client and extended cleanly when the
 * codec lands.
 *
 * Wire format (planned, not yet bumping NET_PROTOCOL_VER):
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDVOC"
 *    5  1    version (1)
 *    6  1    kind (0=opus_frame, 1=ptt_start, 2=ptt_stop)
 *    7  1    flags
 *    8  4    sender handle
 *   12  4    target handle (group room id when broadcast)
 *   16  2    seq (per-sender)
 *   18  2    payload_len
 *   20  N    opus encoded frame (typically 60-160 bytes per 20 ms slice)
 *   20+N 32  sender pubkey
 *   52+N 64  signature
 *
 * Per-frame signing is intentionally heavy for voice; the production
 * cut may move to per-burst signature (every Nth frame) to amortise the
 * Ed25519 cost. The follow-up that lands the codec wires the actual
 * cadence; this header documents the design.
 */

#ifndef _IN_VOICE_H
#define _IN_VOICE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	VOICE_CAPTURE_OFF        = 0,  /* not transmitting */
	VOICE_CAPTURE_PUSH_TO_TALK = 1, /* default; transmit while key held */
	VOICE_CAPTURE_VOICE_ACTIVE = 2, /* transmit when level above threshold */
} voice_capture_mode_t;

void voiceInit(void);
void voiceShutdown(void);
void voiceTick(void);

/** Master enable / disable. Disabled by default. The toggle lives in
 *  the social Settings tab and persists via socialNotifMaskGet's
 *  pattern (same JSON file). */
s32  voiceEnabled(void);
void voiceSetEnabled(s32 on);

/** Capture mode. PTT is default; VA is a follow-up with a level
 *  threshold + hysteresis. */
voice_capture_mode_t voiceGetCaptureMode(void);
void voiceSetCaptureMode(voice_capture_mode_t m);

/** PTT keypress hook. Called by the voice UI on key down/up. The actual
 *  audio capture / encode / send wiring lives in the codec follow-up. */
void voicePttBegin(void);
void voicePttEnd(void);
s32  voicePttActive(void);

/** True if the local user is currently being heard by friends in the
 *  active group session. Surfaced in the status indicator pill so the
 *  user always knows when their mic is hot. */
s32  voiceLocalIsTransmitting(void);

/** Per-friend transmit state. Returns 1 if `friend_handle` is currently
 *  audible on this client (i.e. friend has voice enabled, is transmitting,
 *  and is not per-friend muted via socialFriend.muted). */
s32  voicePeerIsTalking(u32 friend_handle);

/** Diagnostic: total voice frames received this session. */
u32  voiceStatsRxFrames(void);
u32  voiceStatsTxFrames(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_VOICE_H */
