/**
 * Optional Opus voice with explicit current-Agent group audience admission.
 * Signed PDVOC v2 frames use the discovered presence socket; each frame names
 * one recipient. Format: magic5, version1, kind1, flags1, sender4, recipient4,
 * sequence4, payload-length2, sender-epoch16, receiver-challenge16, payload0..400, public-key32, signature64.
 * PTT, fresh-session replay admission and ordered bounded mixed playback are
 * implemented, including energy VAD with sensitivity and silence hold. Local transmit state does not prove audibility.
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

/* Signed v2 packet bounds, including the public key and signature. */
#define VOICE_WIRE_MIN_FRAME_LEN 150u
#define VOICE_WIRE_MAX_FRAME_LEN 550u
/* Presence dispatch; validates recipient, signature and audience before audio. */
void voiceReceiveFrame(const u8 *packet, u32 length);

/** Master enable / disable. Disabled by default. Codec/output failure cannot
 * report enabled; input failure falls back to listen-only with an error. */
s32  voiceEnabled(void);
void voiceSetEnabled(s32 on);

s32 voiceCodecAvailable(void);
const char *voiceLastError(void);
s32 voiceGetSensitivity(void);
void voiceSetSensitivity(s32 value);
float voiceInputLevel(void);

/** Capture mode. PTT is default; VAD uses hysteresis and a 300ms silence hold.
 * OFF closes microphone capture but permits listening while enabled. */
voice_capture_mode_t voiceGetCaptureMode(void);
void voiceSetCaptureMode(voice_capture_mode_t m);

/** PTT keypress hook. Called by the voice UI on key down/up. The actual
 *  capture/encode/send path requires the optional codec and an accepted peer. */
void voicePttBegin(void);
void voicePttEnd(void);
s32  voicePttActive(void);

/** True if local capture is configured to transmit to peers in the
 *  active group session. Surfaced in the status indicator pill so the
 *  user always knows when their mic is hot. */
s32  voiceLocalIsTransmitting(void);

/** Per-friend transmit state. Returns 1 if `friend_handle` has recently supplied accepted voice frames
 *  on this client (i.e. friend has voice enabled, is transmitting,
 *  and is not per-friend muted via socialFriend.muted). */
s32  voicePeerIsTalking(u32 friend_handle);

/** Diagnostic: total voice frames received this session. */
u32  voiceStatsRxFrames(void);
u32  voiceStatsTxFrames(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_VOICE_H */
