/**
 * voice.c -- Phase 5 voice chat (Opus codec + SDL audio + PDVOC wire).
 *
 * When HAVE_OPUS is defined, this module:
 *   - Opens an SDL audio capture device (16 kHz mono S16) when voice
 *     is enabled.
 *   - Opens an SDL audio playback device for received voice.
 *   - Wraps libopus encoder + decoder.
 *   - Runs a dedicated signed UDP socket on port 27108 for PDVOC
 *     frames.
 *   - Encodes 20 ms slices when transmitting, signs + sends per-frame.
 *   - Verifies inbound frames (handle bind + signature + per-friend
 *     mute) and feeds the decoder + audio playback queue.
 *
 * When HAVE_OPUS is NOT defined, the scaffold from the original Phase 5
 * commit is preserved: state + settings + UI hooks all work; PTT
 * still toggles voiceLocalIsTransmitting; encode / decode / wire are
 * no-ops. This lets the build succeed even before Mike's MSYS2 install
 * runs `pacman -S mingw-w64-x86_64-opus`.
 *
 * Single-writer hygiene: this module owns the encoder, decoders, audio
 * device handles, and the per-peer last-frame-recv state. Inbound
 * verification follows the same drop-pipeline as chat / file_transfer:
 * friend allowlist -> handle/key bind -> signature verify -> TOFU lock
 * -> per-friend mute -> decode.
 */

#include "voice.h"
#include "social.h"
#include "identity.h"
#include "ed25519.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_OPUS
  #include <opus.h>
#endif

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #define closesocket close
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
#endif

/* -------------------------------------------------------------------------
 * Wire format
 * ------------------------------------------------------------------------- */

#define VOICE_PORT             27108
#define VOICE_MAGIC            "PDVOC"
#define VOICE_MAGIC_LEN        5
#define VOICE_VERSION          1
#define VOICE_HEADER_LEN       20  /* up to payload */
#define VOICE_PUBKEY_LEN       32
#define VOICE_SIG_LEN          64
#define VOICE_DOMAIN           "pd-voice-v1"
#define VOICE_DOMAIN_LEN       11

#define VOICE_KIND_OPUS_FRAME  0
#define VOICE_KIND_PTT_START   1
#define VOICE_KIND_PTT_STOP    2

/* -------------------------------------------------------------------------
 * Audio config
 * ------------------------------------------------------------------------- */

#define VOICE_SAMPLE_RATE      16000  /* wide-band */
#define VOICE_FRAME_MS         20
#define VOICE_FRAME_SAMPLES    ((VOICE_SAMPLE_RATE * VOICE_FRAME_MS) / 1000)
#define VOICE_OPUS_BITRATE     24000
#define VOICE_OPUS_MAX_BYTES   400  /* generous upper bound for one frame */

/* -------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

static s32                  s_Enabled;
static voice_capture_mode_t s_Mode = VOICE_CAPTURE_PUSH_TO_TALK;
static s32                  s_PttActive;
static u32                  s_RxFrames;
static u32                  s_TxFrames;

#ifdef HAVE_OPUS

#define VOICE_PEER_DECODERS_MAX 8
typedef struct {
	u32          handle;
	OpusDecoder *dec;
	u32          last_recv_ms;
} voice_peer_t;

static voice_peer_t s_Peers[VOICE_PEER_DECODERS_MAX];
static SDL_AudioDeviceID s_CaptureDev;
static SDL_AudioDeviceID s_PlaybackDev;
static OpusEncoder      *s_Encoder;
static SOCKET            s_Sock = INVALID_SOCKET;
static s32               s_SocketReady;
static u16               s_NextSeq;
static u32               s_CaptureBytesPerFrame;
static u8                s_CaptureScratch[VOICE_FRAME_SAMPLES * 2 * 4];
static u32               s_CaptureScratchUsed;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static void wU8(u8 **p, u8 v)   { *(*p)++ = v; }
static void wU16(u8 **p, u16 v) { (*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); *p+=2; }
static void wU32(u8 **p, u32 v) {
	(*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); (*p)[2]=(u8)(v>>16); (*p)[3]=(u8)(v>>24); *p+=4;
}
static u8  rU8 (const u8 **p) { return *(*p)++; }
static u16 rU16(const u8 **p) { u16 v = (u16)((*p)[0]) | ((u16)((*p)[1])<<8); *p+=2; return v; }
static u32 rU32(const u8 **p) {
	u32 v = ((u32)((*p)[0])      ) | ((u32)((*p)[1])<< 8) |
	        ((u32)((*p)[2]) << 16) | ((u32)((*p)[3])<<24); *p+=4; return v;
}

static s32 socketSetNonblock(SOCKET s)
{
#ifdef _WIN32
	u_long mode = 1; return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
#else
	int fl = fcntl(s, F_GETFL, 0); if (fl < 0) return -1;
	return fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static SOCKET ensureSocket(void)
{
	if (s_SocketReady) return s_Sock;
	s_Sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_Sock == INVALID_SOCKET) return INVALID_SOCKET;
	int yes = 1;
	setsockopt(s_Sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
	socketSetNonblock(s_Sock);
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(VOICE_PORT);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		addr.sin_port = 0;
		if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			closesocket(s_Sock); s_Sock = INVALID_SOCKET; return INVALID_SOCKET;
		}
	}
	s_SocketReady = 1;
	sysLogPrintf(LOG_NOTE, "VOICE: socket bound on UDP %u", (unsigned)VOICE_PORT);
	return s_Sock;
}

static voice_peer_t *findOrAllocPeer(u32 handle)
{
	for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; i++) {
		if (s_Peers[i].handle == handle) return &s_Peers[i];
	}
	for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; i++) {
		if (s_Peers[i].handle == 0) {
			s_Peers[i].handle = handle;
			s_Peers[i].dec = NULL;
			s_Peers[i].last_recv_ms = 0;
			return &s_Peers[i];
		}
	}
	/* Evict oldest. */
	s32 oldest = 0;
	for (s32 i = 1; i < VOICE_PEER_DECODERS_MAX; i++) {
		if (s_Peers[i].last_recv_ms < s_Peers[oldest].last_recv_ms) oldest = i;
	}
	if (s_Peers[oldest].dec) opus_decoder_destroy(s_Peers[oldest].dec);
	s_Peers[oldest].handle = handle;
	s_Peers[oldest].dec = NULL;
	s_Peers[oldest].last_recv_ms = 0;
	return &s_Peers[oldest];
}

/* -------------------------------------------------------------------------
 * Audio devices
 * ------------------------------------------------------------------------- */

static void audioCaptureClose(void)
{
	if (s_CaptureDev) {
		SDL_PauseAudioDevice(s_CaptureDev, 1);
		SDL_CloseAudioDevice(s_CaptureDev);
		s_CaptureDev = 0;
	}
}

static void audioPlaybackClose(void)
{
	if (s_PlaybackDev) {
		SDL_CloseAudioDevice(s_PlaybackDev);
		s_PlaybackDev = 0;
	}
}

static s32 audioCaptureOpen(void)
{
	if (s_CaptureDev) return 0;
	SDL_AudioSpec want, got;
	memset(&want, 0, sizeof(want));
	want.freq = VOICE_SAMPLE_RATE;
	want.format = AUDIO_S16LSB;
	want.channels = 1;
	want.samples = (Uint16)VOICE_FRAME_SAMPLES;
	want.callback = NULL; /* queue-based capture */
	s_CaptureDev = SDL_OpenAudioDevice(NULL, /* iscapture */ 1,
	                                    &want, &got, 0);
	if (!s_CaptureDev) {
		sysLogPrintf(LOG_WARNING, "VOICE: SDL_OpenAudioDevice (capture) failed: %s",
		             SDL_GetError());
		return -1;
	}
	s_CaptureBytesPerFrame = (u32)VOICE_FRAME_SAMPLES * (u32)got.channels * 2;
	SDL_PauseAudioDevice(s_CaptureDev, 0);
	sysLogPrintf(LOG_NOTE,
	             "VOICE: capture opened (freq=%d, ch=%d, fmt=0x%04x, bytes/frame=%u)",
	             got.freq, got.channels, got.format,
	             (unsigned)s_CaptureBytesPerFrame);
	return 0;
}

static s32 audioPlaybackOpen(void)
{
	if (s_PlaybackDev) return 0;
	SDL_AudioSpec want, got;
	memset(&want, 0, sizeof(want));
	want.freq = VOICE_SAMPLE_RATE;
	want.format = AUDIO_S16LSB;
	want.channels = 1;
	want.samples = (Uint16)VOICE_FRAME_SAMPLES;
	want.callback = NULL;
	s_PlaybackDev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
	if (!s_PlaybackDev) {
		sysLogPrintf(LOG_WARNING, "VOICE: SDL_OpenAudioDevice (playback) failed: %s",
		             SDL_GetError());
		return -1;
	}
	SDL_PauseAudioDevice(s_PlaybackDev, 0);
	sysLogPrintf(LOG_NOTE, "VOICE: playback opened");
	return 0;
}

/* -------------------------------------------------------------------------
 * Encoder + decoder lifecycle
 * ------------------------------------------------------------------------- */

static s32 ensureEncoder(void)
{
	if (s_Encoder) return 0;
	int err = OPUS_OK;
	s_Encoder = opus_encoder_create(VOICE_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &err);
	if (!s_Encoder || err != OPUS_OK) {
		sysLogPrintf(LOG_WARNING, "VOICE: opus_encoder_create failed err=%d", err);
		s_Encoder = NULL;
		return -1;
	}
	opus_encoder_ctl(s_Encoder, OPUS_SET_BITRATE(VOICE_OPUS_BITRATE));
	opus_encoder_ctl(s_Encoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(s_Encoder, OPUS_SET_PACKET_LOSS_PERC(10));
	return 0;
}

static void encoderDestroy(void)
{
	if (s_Encoder) {
		opus_encoder_destroy(s_Encoder);
		s_Encoder = NULL;
	}
}

static OpusDecoder *decoderFor(u32 handle)
{
	voice_peer_t *p = findOrAllocPeer(handle);
	if (!p) return NULL;
	if (!p->dec) {
		int err = OPUS_OK;
		p->dec = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &err);
		if (err != OPUS_OK) {
			sysLogPrintf(LOG_WARNING, "VOICE: opus_decoder_create failed err=%d", err);
			p->dec = NULL;
		}
	}
	p->last_recv_ms = SDL_GetTicks();
	return p->dec;
}

/* -------------------------------------------------------------------------
 * Sign / verify
 * ------------------------------------------------------------------------- */

static s32 signFrame(const u8 *body, u32 body_len, u8 *outSig)
{
	if (!identityGetPubkey()) return 0;
	u8 buf[VOICE_HEADER_LEN + VOICE_OPUS_MAX_BYTES + VOICE_PUBKEY_LEN + VOICE_DOMAIN_LEN];
	memcpy(buf, body, body_len);
	memcpy(buf + body_len, VOICE_DOMAIN, VOICE_DOMAIN_LEN);
	return identitySign(buf, body_len + VOICE_DOMAIN_LEN, outSig);
}

static s32 verifyFrame(const u8 *body, u32 body_len, const u8 *sig, const u8 *pubkey)
{
	u8 buf[VOICE_HEADER_LEN + VOICE_OPUS_MAX_BYTES + VOICE_PUBKEY_LEN + VOICE_DOMAIN_LEN];
	memcpy(buf, body, body_len);
	memcpy(buf + body_len, VOICE_DOMAIN, VOICE_DOMAIN_LEN);
	return ed25519Verify(sig, buf, body_len + VOICE_DOMAIN_LEN, pubkey) == 1 ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Outbound: capture -> encode -> sign -> sendto every friend
 * ------------------------------------------------------------------------- */

static void broadcastVoiceFrame(u8 kind, const u8 *opus_payload, u16 payload_len)
{
	if (!s_SocketReady) return;
	if (payload_len > VOICE_OPUS_MAX_BYTES) return;

	const u32 body_len = VOICE_HEADER_LEN + payload_len + VOICE_PUBKEY_LEN;
	u8 packet[VOICE_HEADER_LEN + VOICE_OPUS_MAX_BYTES + VOICE_PUBKEY_LEN + VOICE_SIG_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, VOICE_MAGIC, VOICE_MAGIC_LEN); w += VOICE_MAGIC_LEN;
	wU8(&w, VOICE_VERSION);
	wU8(&w, kind);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, 0); /* target = group broadcast */
	wU16(&w, s_NextSeq++);
	wU16(&w, payload_len);
	if (payload_len > 0 && opus_payload) {
		memcpy(w, opus_payload, payload_len);
		w += payload_len;
	}
	memcpy(w, identityGetPubkey(), VOICE_PUBKEY_LEN); w += VOICE_PUBKEY_LEN;
	if (!signFrame(packet, body_len, w)) return;

	/* Send to every friend with a cached endpoint. */
	const s32 nf = socialFriendCount();
	for (s32 i = 0; i < nf; i++) {
		const social_friend_t *f = socialFriendAt(i);
		if (!f) continue;
		u32 ipv4 = 0; u16 port = 0;
		if (!socialFriendGetEndpoint(f->handle, &ipv4, &port)) continue;
		struct sockaddr_in dst;
		memset(&dst, 0, sizeof(dst));
		dst.sin_family = AF_INET;
		dst.sin_addr.s_addr = htonl(ipv4);
		dst.sin_port = htons(VOICE_PORT);
		(void)sendto(s_Sock, (const char *)packet,
		             body_len + VOICE_SIG_LEN, 0,
		             (struct sockaddr *)&dst, sizeof(dst));
	}
}

static void captureEncodeSend(void)
{
	if (!s_CaptureDev || !s_Encoder) return;

	const u32 want = s_CaptureBytesPerFrame;
	if (s_CaptureScratchUsed < want) {
		const u32 free_bytes = (u32)sizeof(s_CaptureScratch) - s_CaptureScratchUsed;
		if (free_bytes == 0) return;
		const u32 got = SDL_DequeueAudio(s_CaptureDev,
		                                  s_CaptureScratch + s_CaptureScratchUsed,
		                                  free_bytes);
		s_CaptureScratchUsed += got;
	}

	while (s_CaptureScratchUsed >= want) {
		u8 opus_buf[VOICE_OPUS_MAX_BYTES];
		const opus_int16 *pcm = (const opus_int16 *)s_CaptureScratch;
		const opus_int32 enc = opus_encode(s_Encoder, pcm, VOICE_FRAME_SAMPLES,
		                                    opus_buf, sizeof(opus_buf));
		if (enc > 0) {
			broadcastVoiceFrame(VOICE_KIND_OPUS_FRAME, opus_buf, (u16)enc);
			s_TxFrames++;
		}
		/* Slide scratch buffer left. */
		const u32 remaining = s_CaptureScratchUsed - want;
		if (remaining > 0) {
			memmove(s_CaptureScratch, s_CaptureScratch + want, remaining);
		}
		s_CaptureScratchUsed = remaining;
	}
}

/* -------------------------------------------------------------------------
 * Inbound: drain -> verify -> decode -> queue
 * ------------------------------------------------------------------------- */

static void drainReceive(void)
{
	if (!s_SocketReady) return;

	for (;;) {
		u8 packet[2048];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n < (int)(VOICE_HEADER_LEN + VOICE_PUBKEY_LEN + VOICE_SIG_LEN)) continue;
		if (memcmp(packet, VOICE_MAGIC, VOICE_MAGIC_LEN) != 0) continue;

		const u8 *r = packet + VOICE_MAGIC_LEN;
		u8 ver  = rU8(&r);
		u8 kind = rU8(&r);
		(void)rU8(&r);
		u32 from_handle = rU32(&r);
		u32 to_handle   = rU32(&r); (void)to_handle;
		u16 seq         = rU16(&r); (void)seq;
		u16 payload_len = rU16(&r);

		if (ver != VOICE_VERSION) continue;
		if (payload_len > VOICE_OPUS_MAX_BYTES) continue;
		const u32 expected = VOICE_HEADER_LEN + payload_len + VOICE_PUBKEY_LEN + VOICE_SIG_LEN;
		if ((u32)n != expected) continue;

		const u8 *payload = packet + VOICE_HEADER_LEN;
		const u8 *pubkey  = payload + payload_len;
		const u8 *sig     = pubkey + VOICE_PUBKEY_LEN;

		const social_friend_t *f = socialFriendByHandle(from_handle);
		if (!f) continue;
		if (f->muted) continue;
		if (socialBlockIsHandle(from_handle)) continue;
		if (!socialHandleBindsPubkey(from_handle, pubkey)) continue;
		if (!verifyFrame(packet, VOICE_HEADER_LEN + payload_len + VOICE_PUBKEY_LEN, sig, pubkey)) continue;
		if (socialFriendBindPubkey(from_handle, pubkey) < 0) continue;

		if (kind == VOICE_KIND_OPUS_FRAME && payload_len > 0) {
			OpusDecoder *dec = decoderFor(from_handle);
			if (!dec) continue;
			(void)audioPlaybackOpen();
			if (!s_PlaybackDev) continue;
			opus_int16 pcm[VOICE_FRAME_SAMPLES];
			const opus_int32 dec_n = opus_decode(dec, payload, (opus_int32)payload_len,
			                                      pcm, VOICE_FRAME_SAMPLES, 0);
			if (dec_n > 0) {
				SDL_QueueAudio(s_PlaybackDev, pcm, (Uint32)(dec_n * 2));
				s_RxFrames++;
			}
		}
	}
}

#endif /* HAVE_OPUS */

/* -------------------------------------------------------------------------
 * Public API (always defined; codec branches on HAVE_OPUS)
 * ------------------------------------------------------------------------- */

void voiceInit(void)
{
	s_Enabled = 0;
	s_Mode = VOICE_CAPTURE_PUSH_TO_TALK;
	s_PttActive = 0;
	s_RxFrames = 0;
	s_TxFrames = 0;

#ifdef HAVE_OPUS
	memset(s_Peers, 0, sizeof(s_Peers));
	s_Encoder = NULL;
	s_CaptureDev = 0;
	s_PlaybackDev = 0;
	s_NextSeq = 0;
	s_CaptureScratchUsed = 0;
	sysLogPrintf(LOG_NOTE, "VOICE: codec available (Opus)");
#else
	sysLogPrintf(LOG_NOTE, "VOICE: codec not built (HAVE_OPUS not defined; scaffold only)");
#endif
}

void voiceShutdown(void)
{
	s_Enabled = 0;
	s_PttActive = 0;
#ifdef HAVE_OPUS
	audioCaptureClose();
	audioPlaybackClose();
	encoderDestroy();
	for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; i++) {
		if (s_Peers[i].dec) opus_decoder_destroy(s_Peers[i].dec);
	}
	memset(s_Peers, 0, sizeof(s_Peers));
	if (s_SocketReady) {
		closesocket(s_Sock); s_Sock = INVALID_SOCKET; s_SocketReady = 0;
	}
#endif
}

s32  voiceEnabled(void) { return s_Enabled; }

void voiceSetEnabled(s32 on)
{
	const s32 want = on ? 1 : 0;
	if (want == s_Enabled) return;
	s_Enabled = want;
#ifdef HAVE_OPUS
	if (want) {
		(void)ensureSocket();
		(void)ensureEncoder();
		(void)audioCaptureOpen();
		(void)audioPlaybackOpen();
	} else {
		s_PttActive = 0;
		audioCaptureClose();
		/* keep playback open so any in-flight late frames can drain */
	}
#else
	if (!want) s_PttActive = 0;
#endif
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
	if (s_PttActive) return;
	s_PttActive = 1;
#ifdef HAVE_OPUS
	/* Reset scratch on PTT-down so leftover audio from the previous burst
	 * doesn't bleed into this one. */
	s_CaptureScratchUsed = 0;
	if (s_CaptureDev) SDL_ClearQueuedAudio(s_CaptureDev);
	broadcastVoiceFrame(VOICE_KIND_PTT_START, NULL, 0);
#endif
}

void voicePttEnd(void)
{
	if (!s_PttActive) return;
	s_PttActive = 0;
#ifdef HAVE_OPUS
	broadcastVoiceFrame(VOICE_KIND_PTT_STOP, NULL, 0);
	s_CaptureScratchUsed = 0;
#endif
}

s32 voicePttActive(void) { return s_PttActive; }

s32 voiceLocalIsTransmitting(void)
{
	if (!s_Enabled) return 0;
	if (s_Mode == VOICE_CAPTURE_PUSH_TO_TALK) return s_PttActive;
	if (s_Mode == VOICE_CAPTURE_VOICE_ACTIVE) {
		/* Voice-activated needs a level estimator that's beyond this
		 * commit's scope; the toggle remains for forward-compat. */
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
#ifdef HAVE_OPUS
	for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; i++) {
		if (s_Peers[i].handle == friend_handle && s_Peers[i].last_recv_ms != 0) {
			const u32 age = SDL_GetTicks() - s_Peers[i].last_recv_ms;
			return age < 500u ? 1 : 0;
		}
	}
#endif
	return 0;
}

u32 voiceStatsRxFrames(void) { return s_RxFrames; }
u32 voiceStatsTxFrames(void) { return s_TxFrames; }

void voiceTick(void)
{
	if (!s_Enabled) return;
#ifdef HAVE_OPUS
	if (!s_SocketReady) (void)ensureSocket();
	if (!s_SocketReady) return;

	if (s_PttActive) {
		captureEncodeSend();
	}
	drainReceive();
#endif
}
