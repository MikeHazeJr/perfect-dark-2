/**
 * Voice: optional Opus, SDL capture/playback and signed recipient-specific PDVOC
 * frames on the discovered presence transport. Accepted current-Agent group
 * membership gates ingress and egress. Devices default off; PTT is implemented.
 * Signed challenge/epoch negotiation gates media; ordered jitter buffers feed
 * one bounded mixed output timeline. VAD uses energy hysteresis and silence hold.
 * All state and callbacks run on the main thread.
 */

#include "voice.h"
#include "presence.h"
#include "net/group_session_policy.h"
#include "social.h"
#include "identity.h"
#include "ed25519.h"
#include "system.h"
#include "net/net_candidate.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_OPUS
  #include <opus.h>
#endif

/* -------------------------------------------------------------------------
 * Wire format
 * ------------------------------------------------------------------------- */

#define VOICE_MAGIC            "PDVOC"
#define VOICE_MAGIC_LEN        5
#define VOICE_VERSION          2
#define VOICE_HEADER_LEN       54  /* up to payload */
#define VOICE_PUBKEY_LEN       32
#define VOICE_SIG_LEN          64
#define VOICE_DOMAIN           "pd-voice-v2"
#define VOICE_DOMAIN_LEN       11

#define VOICE_KIND_OPUS_FRAME  0
#define VOICE_KIND_PTT_START   1
#define VOICE_KIND_PTT_STOP    2
#define VOICE_KIND_CHALLENGE   3
#define VOICE_TOKEN_BYTES      16

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
static u32                  s_LocalHandle;
static s32 voiceOwnerCurrent(void);
static s32 s_VadActive, s_VadQuietMs, s_Sensitivity = 50;
static float s_InputLevel;
static u32 s_InputLastMs;
static const char *s_LastError = "";
static void voiceStopTransmission(void);

static s32 voicePeerAllowed(u32 handle)
{
    return groupSessionPeerAccepted(groupSessionGet(), socialMyHandle(), handle)
        && socialFriendByHandle(handle) && !socialBlockIsHandle(handle);
}

static s32 voiceHasAudience(void)
{
    const group_session_t *group = groupSessionGet();
    for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; ++i)
        if (voicePeerAllowed(group->peers[i].handle)) return 1;
    return 0;
}

#ifdef HAVE_OPUS

#define VOICE_PEER_DECODERS_MAX 8
#define VOICE_JITTER_FRAMES 8
#define VOICE_PREFILL_MS 40u
#define VOICE_PCM_BYTES (VOICE_FRAME_SAMPLES * sizeof(opus_int16))
typedef struct {
    u32 sequence;
    u16 length;
    u8 bytes[VOICE_OPUS_MAX_BYTES];
} voice_encoded_frame;
typedef struct {
	u32          handle;
	OpusDecoder *dec;
	u32          last_recv_ms;
    u8 tx_challenge[VOICE_TOKEN_BYTES];
    u8 rx_epoch[VOICE_TOKEN_BYTES], rx_challenge[VOICE_TOKEN_BYTES];
    u8 pending_epoch[VOICE_TOKEN_BYTES], pending_challenge[VOICE_TOKEN_BYTES];
    u64 rx_seen;
    u32 rx_high, hello_ms, pending_ms;
    u8 tx_ready, rx_active, pending;
    voice_encoded_frame frames[VOICE_JITTER_FRAMES];
    u32 play_sequence, play_due_ms;
    u8 play_started, play_output, play_missing;

} voice_peer_t;

static voice_peer_t s_Peers[VOICE_PEER_DECODERS_MAX];
static SDL_AudioDeviceID s_CaptureDev;
static SDL_AudioDeviceID s_PlaybackDev;
static OpusEncoder      *s_Encoder;
static u32               s_NextSeq;
static u8 s_TxEpoch[VOICE_TOKEN_BYTES];
static u32 s_MixDueMs;
static s32 s_MixClock;
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
	memset(&s_Peers[oldest], 0, sizeof(s_Peers[oldest]));
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
 * Outbound: capture -> encode -> sign -> accepted group peers only
 * ------------------------------------------------------------------------- */

static s32 tokenNonzero(const u8 *token)
{
    u8 combined = 0;
    for (s32 i = 0; i < VOICE_TOKEN_BYTES; ++i) combined |= token[i];
    return combined != 0;
}

static s32 beginTxEpoch(void)
{
    u8 token[VOICE_TOKEN_BYTES];
    if (!netCandidateRandomBytes(token, sizeof(token)) || !tokenNonzero(token)) return 0;
    memcpy(s_TxEpoch, token, sizeof(token));
    s_NextSeq = 1;
    for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; ++i) {
        s_Peers[i].tx_ready = 0;
        s_Peers[i].hello_ms = 0;
        memset(s_Peers[i].tx_challenge, 0, VOICE_TOKEN_BYTES);
    }
    return 1;
}

static s32 sendVoicePacket(u32 target, u8 kind, u32 seq, const u8 *epoch,
        const u8 *challenge, const u8 *payload, u16 payload_len)
{
    if (!voicePeerAllowed(target) || !presenceVoiceTransportReady()
            || payload_len > VOICE_OPUS_MAX_BYTES || (payload_len && !payload)) return 0;
    const u32 body_len = VOICE_HEADER_LEN + payload_len + VOICE_PUBKEY_LEN;
    u8 packet[VOICE_WIRE_MAX_FRAME_LEN] = {0};
    u8 *w = packet;
    memcpy(w, VOICE_MAGIC, VOICE_MAGIC_LEN); w += VOICE_MAGIC_LEN;
    wU8(&w, VOICE_VERSION); wU8(&w, kind); wU8(&w, 0);
    wU32(&w, socialMyHandle()); wU32(&w, target);
    wU32(&w, seq); wU16(&w, payload_len);
    memcpy(w, epoch, VOICE_TOKEN_BYTES); w += VOICE_TOKEN_BYTES;
    if (challenge) memcpy(w, challenge, VOICE_TOKEN_BYTES);
    w += VOICE_TOKEN_BYTES;
    if (payload_len) { memcpy(w, payload, payload_len); w += payload_len; }
    memcpy(w, identityGetPubkey(), VOICE_PUBKEY_LEN); w += VOICE_PUBKEY_LEN;
    return signFrame(packet, body_len, w)
        && presenceSendVoiceFrame(target, packet, body_len + VOICE_SIG_LEN) == 0;
}

static void resetPeerPlayout(voice_peer_t *peer)
{
    memset(peer->frames, 0, sizeof(peer->frames));
    peer->play_started = peer->play_output = peer->play_missing = 0;
    peer->play_sequence = peer->play_due_ms = 0;
}

static s32 queueVoiceFrame(voice_peer_t *peer, u32 sequence,
        const u8 *payload, u16 length, u32 now)
{
    if (peer->play_started && sequence < peer->play_sequence) {
        if (peer->play_output || peer->play_sequence - sequence >= VOICE_JITTER_FRAMES) return 0;
        peer->play_sequence = sequence;
    }
    if (!peer->play_started || sequence - peer->play_sequence >= VOICE_JITTER_FRAMES
            || (s32)(now - peer->play_due_ms) > 120) {
        resetPeerPlayout(peer);
        if (peer->dec) { opus_decoder_destroy(peer->dec); peer->dec = NULL; }
        peer->play_sequence = sequence;
        peer->play_due_ms = now + VOICE_PREFILL_MS;
        peer->play_started = 1;
    }
    voice_encoded_frame *frame = &peer->frames[sequence % VOICE_JITTER_FRAMES];
    frame->sequence = sequence; frame->length = length;
    memcpy(frame->bytes, payload, length);
    peer->last_recv_ms = now;
    if (!s_MixClock) { s_MixClock = 1; s_MixDueMs = now + VOICE_PREFILL_MS; }
    return 1;
}

/* Each speaker contributes at most one ordered20ms frame to the same slice. */
static s32 mixVoiceSlice(u32 slice_ms)
{
    s32 mixed[VOICE_FRAME_SAMPLES] = {0};
    s32 contributors = 0;
    for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; ++i) {
        voice_peer_t *peer = &s_Peers[i];
        if (!peer->rx_active || !peer->play_started || (s32)(slice_ms - peer->play_due_ms) < 0) continue;
        voice_encoded_frame *frame = &peer->frames[peer->play_sequence % VOICE_JITTER_FRAMES];
        const s32 present = frame->length && frame->sequence == peer->play_sequence;
        if (!present && peer->play_missing >= 3) { peer->play_started = 0; continue; }
        OpusDecoder *decoder = decoderFor(peer->handle);
        opus_int16 pcm[VOICE_FRAME_SAMPLES] = {0};
        int decoded = decoder ? opus_decode(decoder, present ? frame->bytes : NULL,
            present ? frame->length : 0, pcm, VOICE_FRAME_SAMPLES, 0) : -1;
        if (present) frame->length = 0;
        if (decoded == VOICE_FRAME_SAMPLES) {
            ++contributors;
            for (s32 sample = 0; sample < VOICE_FRAME_SAMPLES; ++sample) mixed[sample] += pcm[sample];
            if (present) ++s_RxFrames;
        }
        peer->play_missing = present ? 0 : peer->play_missing + 1;
        peer->play_output = 1;
        if (peer->play_sequence == 0xffffffffu) peer->play_started = 0;
        else ++peer->play_sequence;
        peer->play_due_ms += VOICE_FRAME_MS;
    }
    if (!contributors) return 0;
    opus_int16 output[VOICE_FRAME_SAMPLES];
    for (s32 i = 0; i < VOICE_FRAME_SAMPLES; ++i)
        output[i] = (opus_int16)(mixed[i] > 32767 ? 32767 : mixed[i] < -32768 ? -32768 : mixed[i]);
    if (audioPlaybackOpen() < 0) return 0;
    return SDL_QueueAudio(s_PlaybackDev, output, sizeof(output)) == 0;
}

static void pumpVoicePlayback(void)
{
    if (!s_MixClock) return;
    const u32 now = SDL_GetTicks();
    if (s_PlaybackDev && SDL_GetQueuedAudioSize(s_PlaybackDev) > VOICE_PCM_BYTES * 2)
        SDL_ClearQueuedAudio(s_PlaybackDev);
    if ((s32)(now - s_MixDueMs) > 120) {
        /* A paused main thread must not replay an accumulated conversation. */
        if (s_PlaybackDev) SDL_ClearQueuedAudio(s_PlaybackDev);
        for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; ++i) {
            voice_peer_t *peer = &s_Peers[i];
            voice_encoded_frame latest = {0};
            for (s32 j = 0; j < VOICE_JITTER_FRAMES; ++j)
                if (peer->frames[j].length && peer->frames[j].sequence > latest.sequence) latest = peer->frames[j];
            resetPeerPlayout(peer);
            if (peer->dec) { opus_decoder_destroy(peer->dec); peer->dec = NULL; }
            if (latest.length && now - peer->last_recv_ms <= 120) {
                peer->frames[latest.sequence % VOICE_JITTER_FRAMES] = latest;
                peer->play_started = 1; peer->play_sequence = latest.sequence; peer->play_due_ms = now;
            }
        }
        s_MixDueMs = now;
    }
    for (s32 slices = 0; slices < 3 && (s32)(now - s_MixDueMs) >= 0; ++slices) {
        if (s_PlaybackDev && SDL_GetQueuedAudioSize(s_PlaybackDev) > VOICE_PCM_BYTES) break;
        (void)mixVoiceSlice(s_MixDueMs);
        s_MixDueMs += VOICE_FRAME_MS;
    }
    s32 pending = 0;
    for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; ++i) pending |= s_Peers[i].play_started;
    if (!pending) s_MixClock = 0;
}

static void revokePeerReceive(voice_peer_t *peer)
{
    peer->rx_active = peer->pending = 0;
    resetPeerPlayout(peer);
    peer->rx_seen = 0; peer->rx_high = 0; peer->last_recv_ms = 0;
    memset(peer->rx_epoch, 0, VOICE_TOKEN_BYTES);
    memset(peer->rx_challenge, 0, VOICE_TOKEN_BYTES);
    memset(peer->pending_epoch, 0, VOICE_TOKEN_BYTES);
    memset(peer->pending_challenge, 0, VOICE_TOKEN_BYTES);
    if (peer->dec) { opus_decoder_destroy(peer->dec); peer->dec = NULL; }
    if (s_PlaybackDev) SDL_ClearQueuedAudio(s_PlaybackDev);
}

/* Called only after signature, key, recipient and current audience checks. */
static s32 acceptMediaSequence(voice_peer_t *peer, u32 sequence)
{
    if (!sequence) return 0;
    if (!peer->rx_seen || sequence > peer->rx_high) {
        const u32 delta = sequence - peer->rx_high;
        peer->rx_seen = !peer->rx_seen || delta >= 64 ? 1 : (peer->rx_seen << delta) | 1;
        peer->rx_high = sequence;
        return 1;
    }
    const u32 age = peer->rx_high - sequence;
    if (age >= 64 || (peer->rx_seen & ((u64)1 << age))) return 0;
    peer->rx_seen |= (u64)1 << age;
    return 1;
}

static s32 broadcastVoiceFrame(u8 kind, const u8 *payload, u16 payload_len)
{
    if (!voiceOwnerCurrent() || !tokenNonzero(s_TxEpoch)) return 0;
    if (kind == VOICE_KIND_OPUS_FRAME && s_NextSeq == 0xffffffffu && !beginTxEpoch()) {
        s_PttActive = s_VadActive = 0;
        memset(s_TxEpoch, 0, sizeof(s_TxEpoch));
        return 0;
    }
    const u32 seq = kind == VOICE_KIND_PTT_START ? 0 : s_NextSeq++;
    const u32 now = SDL_GetTicks();
    s32 delivered = 0;
    const group_session_t *group = groupSessionGet();
    for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; ++i) {
        const u32 target = group->peers[i].handle;
        if (!voicePeerAllowed(target)) continue;
        voice_peer_t *peer = findOrAllocPeer(target);
        if (!peer) continue;
        if (kind == VOICE_KIND_PTT_START || !peer->tx_ready) {
            if (kind == VOICE_KIND_PTT_STOP) continue;
            if (kind == VOICE_KIND_PTT_START || !peer->hello_ms || now - peer->hello_ms >= 250) {
                peer->hello_ms = now;
                (void)sendVoicePacket(target, VOICE_KIND_PTT_START, 0, s_TxEpoch, NULL, NULL, 0);
            }
            continue;
        }
        if (kind == VOICE_KIND_OPUS_FRAME && now - peer->hello_ms >= 1000) {
            peer->hello_ms = now;
            (void)sendVoicePacket(target, VOICE_KIND_PTT_START, 0, s_TxEpoch, NULL, NULL, 0);
        }
        delivered += sendVoicePacket(target, kind, seq, s_TxEpoch, peer->tx_challenge, payload, payload_len);
    }
    return delivered;
}

/* Energy threshold in PCM units; separate stop threshold avoids chatter.
 * Quiet duration advances by captured samples, independent of render cadence. */
static void updateVoiceActivity(const opus_int16 *pcm)
{
    u64 energy = 0;
    s32 peak = 0;
    for (s32 i = 0; i < VOICE_FRAME_SAMPLES; ++i) {
        const s32 sample = pcm[i];
        const s32 magnitude = sample < 0 ? -sample : sample;
        energy += (u64)((s64)sample * sample);
        if (magnitude > peak) peak = magnitude;
    }
    s_InputLevel = peak / 32768.0f;
    s_InputLastMs = SDL_GetTicks();
    if (s_Mode != VOICE_CAPTURE_VOICE_ACTIVE) return;
    if (!voiceHasAudience()) { voiceStopTransmission(); return; }
    s32 threshold = s_Sensitivity >= 50 ? 600 - 10 * (s_Sensitivity - 50)
        : 600 + 60 * (50 - s_Sensitivity);
    if (s_VadActive) threshold = threshold * 3 / 5;
    const s32 speech = energy >= (u64)threshold * threshold * VOICE_FRAME_SAMPLES;
    if (speech) {
        s_VadQuietMs = 0;
        if (!s_VadActive && beginTxEpoch()) {
            s_VadActive = 1;
            broadcastVoiceFrame(VOICE_KIND_PTT_START, NULL, 0);
        }
    } else if (s_VadActive) {
        s_VadQuietMs += VOICE_FRAME_MS;
        if (s_VadQuietMs >= 300) voiceStopTransmission();
    }
}

static void captureEncodeSend(void)
{
	if (!s_CaptureDev || !s_Encoder) return;
    if (SDL_GetQueuedAudioSize(s_CaptureDev) > VOICE_PCM_BYTES * 4) {
        SDL_ClearQueuedAudio(s_CaptureDev); s_CaptureScratchUsed = 0;
        if (s_VadActive) voiceStopTransmission();
        s_InputLevel = 0; return;
    }

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
        updateVoiceActivity(pcm);
        const opus_int32 enc = (s_PttActive || s_VadActive)
            ? opus_encode(s_Encoder, pcm, VOICE_FRAME_SAMPLES, opus_buf, sizeof(opus_buf)) : 0;
		if (enc > 0) {
			if (broadcastVoiceFrame(VOICE_KIND_OPUS_FRAME, opus_buf, (u16)enc) > 0)
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

#endif /* HAVE_OPUS */

/* -------------------------------------------------------------------------
 * Public API (always defined; codec branches on HAVE_OPUS)
 * ------------------------------------------------------------------------- */

void voiceReceiveFrame(const u8 *packet, u32 n)
{
#ifdef HAVE_OPUS
    if (!voiceOwnerCurrent() || !s_Enabled || !packet || n > VOICE_WIRE_MAX_FRAME_LEN) return;
	if (n < (int)(VOICE_HEADER_LEN + VOICE_PUBKEY_LEN + VOICE_SIG_LEN)) return;
	if (memcmp(packet, VOICE_MAGIC, VOICE_MAGIC_LEN) != 0) return;

	const u8 *r = packet + VOICE_MAGIC_LEN;
	u8 ver  = rU8(&r);
	u8 kind = rU8(&r);
	u8 flags = rU8(&r);
	u32 from_handle = rU32(&r);
	u32 to_handle   = rU32(&r);
	u32 seq = rU32(&r);
	u16 payload_len = rU16(&r);
    const u8 *epoch = r; r += VOICE_TOKEN_BYTES;
    const u8 *challenge = r; r += VOICE_TOKEN_BYTES;

	if (ver != VOICE_VERSION || flags || kind > VOICE_KIND_CHALLENGE) return;
	if ((kind == VOICE_KIND_OPUS_FRAME) != (payload_len > 0)) return;
    if (!tokenNonzero(epoch)) return;
    if (kind == VOICE_KIND_PTT_START) {
        if (seq || tokenNonzero(challenge)) return;
    } else if (kind == VOICE_KIND_CHALLENGE) {
        if (seq || !tokenNonzero(challenge)) return;
    } else if (!seq || !tokenNonzero(challenge)) return;
        if (to_handle != socialMyHandle()) return;
        if (!voicePeerAllowed(from_handle)) return;
	if (payload_len > VOICE_OPUS_MAX_BYTES) return;
	const u32 expected = VOICE_HEADER_LEN + payload_len + VOICE_PUBKEY_LEN + VOICE_SIG_LEN;
	if ((u32)n != expected) return;

	const u8 *payload = packet + VOICE_HEADER_LEN;
	const u8 *pubkey  = payload + payload_len;
	const u8 *sig     = pubkey + VOICE_PUBKEY_LEN;

	const social_friend_t *f = socialFriendByHandle(from_handle);
	if (!f) return;
	if (f->muted && kind != VOICE_KIND_CHALLENGE) return;
	if (socialBlockIsHandle(from_handle)) return;
	if (!socialHandleBindsPubkey(from_handle, pubkey)) return;
	if (!verifyFrame(packet, VOICE_HEADER_LEN + payload_len + VOICE_PUBKEY_LEN, sig, pubkey)) return;
	if (socialFriendBindPubkey(from_handle, pubkey) < 0) return;

    voice_peer_t *peer = findOrAllocPeer(from_handle);
    if (!peer) return;
    const u32 now = SDL_GetTicks();
    if (peer->rx_active && now - peer->last_recv_ms >= 1000) revokePeerReceive(peer);
    if (kind == VOICE_KIND_OPUS_FRAME
            && opus_packet_get_nb_samples(payload, payload_len, VOICE_SAMPLE_RATE) != VOICE_FRAME_SAMPLES) return;
    if (kind == VOICE_KIND_PTT_START) {
        const u8 *reply = NULL;
        if (peer->rx_active && !memcmp(peer->rx_epoch, epoch, VOICE_TOKEN_BYTES)) {
            reply = peer->rx_challenge;
        } else if (peer->pending && now - peer->pending_ms < 3000
                && !memcmp(peer->pending_epoch, epoch, VOICE_TOKEN_BYTES)) {
            reply = peer->pending_challenge;
        } else {
            if (peer->pending && now - peer->pending_ms < 250) return;
            u8 fresh[VOICE_TOKEN_BYTES];
            if (!netCandidateRandomBytes(fresh, sizeof(fresh)) || !tokenNonzero(fresh)) return;
            memcpy(peer->pending_epoch, epoch, VOICE_TOKEN_BYTES);
            memcpy(peer->pending_challenge, fresh, VOICE_TOKEN_BYTES);
            peer->pending_ms = now; peer->pending = 1;
            reply = peer->pending_challenge;
        }
        (void)sendVoicePacket(from_handle, VOICE_KIND_CHALLENGE, 0, epoch, reply, NULL, 0);
        return;
    }
    if (kind == VOICE_KIND_CHALLENGE) {
        if (!(s_PttActive || s_VadActive) || memcmp(epoch, s_TxEpoch, VOICE_TOKEN_BYTES)) return;
        memcpy(peer->tx_challenge, challenge, VOICE_TOKEN_BYTES);
        peer->tx_ready = 1;
        return;
    }
    if (!peer->rx_active || memcmp(peer->rx_epoch, epoch, VOICE_TOKEN_BYTES)
            || memcmp(peer->rx_challenge, challenge, VOICE_TOKEN_BYTES)) {
        if (kind != VOICE_KIND_OPUS_FRAME || !peer->pending || now - peer->pending_ms >= 3000
                || memcmp(peer->pending_epoch, epoch, VOICE_TOKEN_BYTES)
                || memcmp(peer->pending_challenge, challenge, VOICE_TOKEN_BYTES)) return;
        const s32 replacing_audio = peer->rx_active || peer->play_started || peer->dec != NULL;
        memcpy(peer->rx_epoch, epoch, VOICE_TOKEN_BYTES);
        memcpy(peer->rx_challenge, challenge, VOICE_TOKEN_BYTES);
        peer->rx_active = 1; peer->pending = 0; peer->rx_seen = 0; peer->rx_high = 0;
        resetPeerPlayout(peer);
        if (peer->dec) { opus_decoder_destroy(peer->dec); peer->dec = NULL; }
        if (replacing_audio && s_PlaybackDev) SDL_ClearQueuedAudio(s_PlaybackDev);
    }
    if (!acceptMediaSequence(peer, seq)) return;
    if (kind == VOICE_KIND_PTT_STOP) {
        revokePeerReceive(peer);
        return;
    }


    if (kind == VOICE_KIND_OPUS_FRAME)
        (void)queueVoiceFrame(peer, seq, payload, payload_len, now);
#else
    (void)packet; (void)n;
#endif
}

void voiceInit(void)
{
	s_LocalHandle = socialMyHandle();
	s_Enabled = 0;
	s_Mode = VOICE_CAPTURE_PUSH_TO_TALK;
	s_PttActive = 0;
	s_VadActive = s_VadQuietMs = 0; s_InputLevel = 0; s_Sensitivity = 50; s_LastError = "";
	s_RxFrames = 0;
	s_TxFrames = 0;

#ifdef HAVE_OPUS
	memset(s_Peers, 0, sizeof(s_Peers));
	s_Encoder = NULL;
	s_CaptureDev = 0;
	s_PlaybackDev = 0;
	s_NextSeq = 0;
    s_MixClock = 0; s_MixDueMs = 0;
    memset(s_TxEpoch, 0, sizeof(s_TxEpoch));
	s_CaptureScratchUsed = 0;
	sysLogPrintf(LOG_NOTE, "VOICE: codec available (Opus)");
#else
	sysLogPrintf(LOG_NOTE, "VOICE: codec unavailable in this build");
#endif
}

void voiceShutdown(void)
{
	s_Enabled = 0;
	s_PttActive = s_VadActive = s_VadQuietMs = 0;
    s_InputLevel = 0;
#ifdef HAVE_OPUS
	audioCaptureClose();
	audioPlaybackClose();
	encoderDestroy();
    s_MixClock = 0; s_MixDueMs = 0;
    memset(s_TxEpoch, 0, sizeof(s_TxEpoch));
	for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; i++) {
		if (s_Peers[i].dec) opus_decoder_destroy(s_Peers[i].dec);
	}
	memset(s_Peers, 0, sizeof(s_Peers));
#endif
}

static s32 voiceOwnerCurrent(void)
{
    const u32 current = socialMyHandle();
    if (current == s_LocalHandle) return current != 0;
    voiceShutdown();
    s_LocalHandle = current;
    s_LastError = ""; s_Sensitivity = 50;
    s_Mode = VOICE_CAPTURE_PUSH_TO_TALK;
    s_RxFrames = s_TxFrames = 0;
    return 0;
}

s32 voiceEnabled(void) { return voiceOwnerCurrent() ? s_Enabled : 0; }

s32 voiceCodecAvailable(void)
{
#ifdef HAVE_OPUS
    return 1;
#else
    return 0;
#endif
}
const char *voiceLastError(void) { (void)voiceOwnerCurrent(); return s_LastError; }
s32 voiceGetSensitivity(void) { (void)voiceOwnerCurrent(); return s_Sensitivity; }
void voiceSetSensitivity(s32 value) {
    (void)voiceOwnerCurrent(); s_Sensitivity = value < 0 ? 0 : value > 100 ? 100 : value;
}
float voiceInputLevel(void) { (void)voiceOwnerCurrent(); return s_InputLevel; }

static void voiceStopTransmission(void)
{
#ifdef HAVE_OPUS
    if (s_PttActive || s_VadActive) broadcastVoiceFrame(VOICE_KIND_PTT_STOP, NULL, 0);
    memset(s_TxEpoch, 0, sizeof(s_TxEpoch));
#endif
    s_PttActive = s_VadActive = s_VadQuietMs = 0;
    /* Capture loop owns scratch consumption; clearing it here would underflow. */
}

#ifdef HAVE_OPUS
static s32 voiceOpenInput(void)
{
    if (ensureEncoder() == 0 && audioCaptureOpen() == 0) return 1;
    audioCaptureClose(); encoderDestroy();
    s_Mode = VOICE_CAPTURE_OFF;
    s_LastError = "Microphone or encoder unavailable; listening only.";
    return 0;
}
#endif

void voiceSetEnabled(s32 on)
{
    (void)voiceOwnerCurrent();
    if (on && !socialMyHandle()) { s_LastError = "Select an Agent to enable voice."; return; }
    if (!on) { voiceStopTransmission(); voiceShutdown(); s_LastError = ""; return; }
    if (s_Enabled) return;
    s_LastError = "";
#ifdef HAVE_OPUS
    if (audioPlaybackOpen() != 0) {
        s_LastError = "Audio output unavailable. Voice could not be enabled.";
        return;
    }
    s_Enabled = 1;
    if (s_Mode != VOICE_CAPTURE_OFF) (void)voiceOpenInput();
#else
    s_LastError = "Voice support is unavailable in this build.";
    s_Enabled = 0;
#endif
}

voice_capture_mode_t voiceGetCaptureMode(void) { (void)voiceOwnerCurrent(); return s_Mode; }

void voiceSetCaptureMode(voice_capture_mode_t m)
{
    (void)voiceOwnerCurrent();
    if (m != VOICE_CAPTURE_OFF && m != VOICE_CAPTURE_PUSH_TO_TALK && m != VOICE_CAPTURE_VOICE_ACTIVE) return;
    if (m == s_Mode) return;
    voiceStopTransmission(); s_Mode = m; s_InputLevel = 0; s_LastError = "";
#ifdef HAVE_OPUS
    s_CaptureScratchUsed = 0;
    if (s_CaptureDev) SDL_ClearQueuedAudio(s_CaptureDev);
    if (m == VOICE_CAPTURE_OFF) { audioCaptureClose(); encoderDestroy(); }
    else if (s_Enabled) (void)voiceOpenInput();
#endif
}

void voicePttBegin(void)
{
	if (!voiceOwnerCurrent() || !voiceHasAudience()) return;
	if (!s_Enabled) return;
	if (s_Mode != VOICE_CAPTURE_PUSH_TO_TALK) return;
	if (s_PttActive) return;
#ifdef HAVE_OPUS
    if (!s_CaptureDev || !s_Encoder || !beginTxEpoch()) return;
#endif
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
    if (!voiceOwnerCurrent() || !s_PttActive) return;
    voiceStopTransmission();
#ifdef HAVE_OPUS
    s_CaptureScratchUsed = 0;
#endif
}

s32 voicePttActive(void) { return voiceOwnerCurrent() ? s_PttActive : 0; }

s32 voiceLocalIsTransmitting(void)
{
    if (!voiceOwnerCurrent() || !voiceHasAudience()) return 0;
#ifndef HAVE_OPUS
    return 0;
#else
    if (!s_CaptureDev || !s_Encoder || !presenceVoiceTransportReady()) return 0;
#endif
	if (!s_Enabled) return 0;
	if (s_Mode == VOICE_CAPTURE_PUSH_TO_TALK) return s_PttActive;
	if (s_Mode == VOICE_CAPTURE_VOICE_ACTIVE) return s_VadActive;
	return 0;
}

s32 voicePeerIsTalking(u32 friend_handle)
{
	if (!voiceOwnerCurrent() || !voicePeerAllowed(friend_handle)) return 0;
	if (friend_handle == 0) return 0;
	if (!s_Enabled) return 0;
	const social_friend_t *f = socialFriendByHandle(friend_handle);
	if (!f) return 0;
	if (f->muted) return 0;
#ifdef HAVE_OPUS
	for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; i++) {
		if (s_Peers[i].handle == friend_handle && s_Peers[i].rx_active && s_Peers[i].last_recv_ms != 0) {
			const u32 age = SDL_GetTicks() - s_Peers[i].last_recv_ms;
			return age < 500u ? 1 : 0;
		}
	}
#endif
	return 0;
}

u32 voiceStatsRxFrames(void) { return voiceOwnerCurrent() ? s_RxFrames : 0; }
u32 voiceStatsTxFrames(void) { return voiceOwnerCurrent() ? s_TxFrames : 0; }

void voiceTick(void)
{
    if (!voiceOwnerCurrent() || !s_Enabled) return;
#ifdef HAVE_OPUS
    if (SDL_GetTicks() - s_InputLastMs > 200) s_InputLevel = 0;
    if (s_VadActive && SDL_GetTicks() - s_InputLastMs >= 300) voiceStopTransmission();

    s32 revoked = 0;
    for (s32 i = 0; i < VOICE_PEER_DECODERS_MAX; ++i) {
        voice_peer_t *peer = &s_Peers[i];
        const social_friend_t *friend = peer->handle
            ? socialFriendByHandle(peer->handle) : NULL;
        if (peer->handle && (!voicePeerAllowed(peer->handle) || !friend)) {
            if (peer->dec) opus_decoder_destroy(peer->dec);
            memset(peer, 0, sizeof(*peer));
            revoked = 1;
        } else if (peer->handle && ((friend->muted && (peer->rx_active || peer->pending || peer->dec))
                || (peer->rx_active && SDL_GetTicks() - peer->last_recv_ms >= 1000))) {
            revokePeerReceive(peer);
        }
    }
    /* The current shared queue cannot remove one speaker's old samples. */
    if (revoked && s_PlaybackDev) SDL_ClearQueuedAudio(s_PlaybackDev);
    if (s_PlaybackDev && SDL_GetAudioDeviceStatus(s_PlaybackDev) == SDL_AUDIO_STOPPED) {
        voiceStopTransmission(); voiceShutdown();
        s_LastError = "Audio output disconnected. Voice has been disabled."; return;
    }
    if (s_CaptureDev && SDL_GetAudioDeviceStatus(s_CaptureDev) == SDL_AUDIO_STOPPED) {
        voiceStopTransmission(); audioCaptureClose(); encoderDestroy();
        s_Mode = VOICE_CAPTURE_OFF; s_InputLevel = 0;
        s_LastError = "Microphone disconnected; listening only.";
    }
    if (!voiceHasAudience()) voiceStopTransmission();
    if (s_PttActive || s_Mode == VOICE_CAPTURE_VOICE_ACTIVE) {
        captureEncodeSend();
    } else {
        s_CaptureScratchUsed = 0; s_InputLevel = 0;
        if (s_CaptureDev) SDL_ClearQueuedAudio(s_CaptureDev);
    }
    pumpVoicePlayback();
#endif
}
