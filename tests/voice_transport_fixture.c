/* Includes the production voice adapter. Real Opus and Ed25519; deterministic
 * social state, captured datagrams and fake SDL devices. No microphone access. */
#include <SDL.h>
#include <opus.h>
#include <string.h>
#include "social.h"
#include "identity.h"
#include "presence.h"
#include "net/group_session.h"
#include "ed25519.h"
#include "voice.h"
#include "net/net_candidate.h"

static u32 vfHandle, vfNow, vfQueued, vfSent;
static s32 vfBlocked, vfReady, vfSendFail;
static group_session_t vfGroup;
static social_friend_t vfFriend, vfFriend2;
static s32 vfSecondEnabled, vfTone;
static u32 vfBuildingPeer = 2002, vfCaptureQueued, vfOutputFrames;
static s16 vfOutput[320];
static u8 vfPeerSeed2[32], vfPeerPub2[32];
static u8 vfSeed[32], vfPub[32], vfPeerSeed[32], vfPeerPub[32];
static u8 vfPacket[550];
static u32 vfLength;
static int vfRngFailure, vfCaptureFail, vfPlaybackFail, vfCaptureStopped, vfPlaybackStopped;
static s16 vfCaptureAmplitude;
static int vfEncoderFail;
static OpusEncoder *vfCreateEncoder(opus_int32 rate, int channels, int application, int *error) {
    if (vfEncoderFail) { *error = OPUS_ALLOC_FAIL; return NULL; }
    return opus_encoder_create(rate, channels, application, error);
}
static u8 vfRngCounter, vfPeerEpoch;
static s32 vfRandom(u8 *out, size_t length) {
    if (vfRngFailure) return 0;
    memset(out, ++vfRngCounter, length); return 1;
}
static Uint32 vfTicks(void) { return vfNow; }
static SDL_AudioDeviceID vfOpen(const char *name, int capture,
        const SDL_AudioSpec *want, SDL_AudioSpec *got, int flags) {
    (void)name; (void)flags; *got = *want;
    if (capture ? vfCaptureFail : vfPlaybackFail) return 0;
    return capture ? 1 : 2;
}
static void vfClose(SDL_AudioDeviceID id) { if (id == 2) vfQueued = 0; }
static void vfPause(SDL_AudioDeviceID id, int pause) { (void)id; (void)pause; }
static void vfClear(SDL_AudioDeviceID id) { if (id == 2) vfQueued = 0; else vfCaptureQueued = 0; }
static Uint32 vfGetQueued(SDL_AudioDeviceID id) { return id == 2 ? vfQueued : vfCaptureQueued; }
static Uint32 vfDequeue(SDL_AudioDeviceID id, void *data, Uint32 size) {
    if (id != 1) return 0;
    if (size > vfCaptureQueued) size = vfCaptureQueued;
    for (Uint32 i = 0; i < size / 2; ++i) ((s16 *)data)[i] = vfCaptureAmplitude;
    vfCaptureQueued -= size; return size;
}
static int vfQueue(SDL_AudioDeviceID id, const void *data, Uint32 size) {
    if (id == 2) {
        vfQueued += size; ++vfOutputFrames;
        if (size == sizeof(vfOutput)) memcpy(vfOutput, data, size);
    }
    return 0;
}
static u32 vfMyHandle(void) { return vfHandle; }
static const group_session_t *vfGetGroup(void) { return &vfGroup; }
static const social_friend_t *vfFriendByHandle(u32 handle) { return handle == 2002 ? &vfFriend : handle == 3003 && vfSecondEnabled ? &vfFriend2 : NULL; }
static s32 vfIsBlocked(u32 handle) { return handle == 2002 && vfBlocked; }
static s32 vfBinds(u32 handle, const u8 *key) { return handle == 2002 ? !memcmp(key, vfPeerPub, 32) : handle == 3003 && vfSecondEnabled && !memcmp(key, vfPeerPub2, 32); }
static s32 vfBind(u32 handle, const u8 *key) { return vfBinds(handle, key) ? 0 : -1; }
static const u8 *vfPublic(void) { return vfPub; }
static s32 vfSign(const void *message, u32 length, u8 *sig) { return ed25519Sign(vfSeed, message, length, sig); }
static s32 vfTransportReady(void) { return vfReady; }
static s32 vfSend(u32 handle, const u8 *packet, u32 length) {
    if (vfSendFail || !vfReady || (handle != 2002 && handle != 3003) || length > sizeof(vfPacket)) return -1;
    memcpy(vfPacket, packet, length); vfLength = length; ++vfSent; return 0;
}
static SDL_AudioStatus vfStatus(SDL_AudioDeviceID id) {
    return (id == 1 ? vfCaptureStopped : vfPlaybackStopped) ? SDL_AUDIO_STOPPED : SDL_AUDIO_PLAYING;
}
#define SDL_GetAudioDeviceStatus vfStatus
#define opus_encoder_create vfCreateEncoder
#define netCandidateRandomBytes vfRandom
#define SDL_GetQueuedAudioSize vfGetQueued
#define SDL_GetTicks vfTicks
#define SDL_OpenAudioDevice vfOpen
#define SDL_CloseAudioDevice vfClose
#define SDL_PauseAudioDevice vfPause
#define SDL_ClearQueuedAudio vfClear
#define SDL_DequeueAudio vfDequeue
#define SDL_QueueAudio vfQueue
#define socialMyHandle vfMyHandle
#define groupSessionGet vfGetGroup
#define socialFriendByHandle vfFriendByHandle
#define socialBlockIsHandle vfIsBlocked
#define socialHandleBindsPubkey vfBinds
#define socialFriendBindPubkey vfBind
#define identityGetPubkey vfPublic
#define identitySign vfSign
#define presenceVoiceTransportReady vfTransportReady
#define presenceSendVoiceFrame vfSend
#include "../port/src/voice.c"

int voiceFixtureReset(void) {
    voiceShutdown();
    vfHandle = 1001; vfNow = 100; vfQueued = vfSent = vfLength = 0;
    vfReady = 1; vfBlocked = vfSendFail = 0;
    vfCaptureFail = vfPlaybackFail = vfCaptureStopped = vfPlaybackStopped = vfEncoderFail = 0; vfCaptureAmplitude = 0;
    vfRngFailure = 0; vfRngCounter = 1; vfPeerEpoch = 100;
    vfSecondEnabled = vfTone = 0; vfBuildingPeer = 2002;
    vfCaptureQueued = vfOutputFrames = 0; memset(vfOutput, 0, sizeof(vfOutput));
    memset(vfPeerSeed2, 3, 32);
    if (!ed25519DerivePubkey(vfPeerSeed2, vfPeerPub2)) return 0;
    memset(&vfFriend2, 0, sizeof(vfFriend2)); vfFriend2.handle = 3003;
    memset(vfSeed, 1, 32); memset(vfPeerSeed, 2, 32);
    if (!ed25519DerivePubkey(vfSeed, vfPub) || !ed25519DerivePubkey(vfPeerSeed, vfPeerPub)) return 0;
    memset(&vfFriend, 0, sizeof(vfFriend)); vfFriend.handle = 2002;
    memset(&vfGroup, 0, sizeof(vfGroup));
    vfGroup.local_handle = 1001; vfGroup.in_session = 1;
    vfGroup.peers[0].handle = 2002; vfGroup.peers[0].state = GROUP_PEER_CONNECTED;
    voiceInit(); voiceSetEnabled(1); return voiceEnabled();
}
int voiceFixtureResignPeer(u8 *packet, u32 length);
void voiceFixtureSetState(int state) { vfGroup.peers[0].state = (group_peer_state_t)state; }
void voiceFixtureSetOwner(u32 handle) { vfHandle = handle; }
void voiceFixtureSetMute(int muted) { vfFriend.muted = muted; }
void voiceFixtureSetBlocked(int blocked) { vfBlocked = blocked; }
void voiceFixtureSetSendFail(int fail) { vfSendFail = fail; }
void voiceFixtureSetRngFailure(int fail) { vfRngFailure = fail; }
void voiceFixtureAdvance(u32 elapsed) {
    const u64 consumed = (u64)elapsed * 32;
    vfQueued = consumed >= vfQueued ? 0 : vfQueued - (u32)consumed;
    vfNow += elapsed; voiceTick();
}
void voiceFixtureEnableSecondPeer(void) {
    vfSecondEnabled = 1; vfGroup.peers[1].handle = 3003;
    vfGroup.peers[1].state = GROUP_PEER_CONNECTED;
}
const s16 *voiceFixtureOutput(void) { return vfOutput; }
u32 voiceFixtureOutputFrames(void) { return vfOutputFrames; }
u32 voiceFixtureCaptureQueued(void) { return vfCaptureQueued; }
void voiceFixtureSetCaptureQueued(u32 bytes) { vfCaptureQueued = bytes; }
void voiceFixtureSetPlaybackQueued(u32 bytes) { vfQueued = bytes; }
u32 voiceFixtureBuffered(void) {
    u32 count = 0;
    for (u32 p = 0; p < VOICE_PEER_DECODERS_MAX; ++p)
        for (u32 f = 0; f < VOICE_JITTER_FRAMES; ++f) count += s_Peers[p].frames[f].length != 0;
    return count;
}
u32 voiceFixtureNextSequence(u32 handle) {
    for (u32 i = 0; i < VOICE_PEER_DECODERS_MAX; ++i)
        if (s_Peers[i].handle == handle) return s_Peers[i].play_sequence;
    return 0;
}
void voiceFixtureSetSequence(u8 *packet, u32 sequence) { u8 *w = packet + 16; wU32(&w, sequence); }
void voiceFixtureMakeStop(u8 *packet, u32 sequence) {
    packet[6] = VOICE_KIND_PTT_STOP;
    u8 *w = packet + 16; wU32(&w, sequence); wU16(&w, 0);
    voiceFixtureResignPeer(packet, 150);
}
u32 voiceFixtureQueued(void) { return vfQueued; }
u32 voiceFixtureSent(void) { return vfSent; }
const u8 *voiceFixturePacket(void) { return vfPacket; }
u32 voiceFixturePacketLength(void) { return vfLength; }
int voiceFixtureVerifySent(void) {
    if (vfLength < 150 || vfLength > 550) return 0;
    const u32 body = vfLength - 64;
    u8 signedBytes[550];
    memcpy(signedBytes, vfPacket, body);
    memcpy(signedBytes + body, VOICE_DOMAIN, VOICE_DOMAIN_LEN);
    return ed25519Verify(vfPacket + body, signedBytes,
        body + VOICE_DOMAIN_LEN, vfPub) == 1;
}
int voiceFixtureResignPeer(u8 *packet, u32 length) {
    if (length < 150 || length > 550) return 0;
    const u32 body = length - 64;
    const u8 *read = packet + 8;
    const u32 sender = rU32(&read);
    const u8 *key = sender == 3003 ? vfPeerPub2 : vfPeerPub;
    const u8 *seed = sender == 3003 ? vfPeerSeed2 : vfPeerSeed;
    memcpy(packet + body - 32, key, 32);
    u8 signedBytes[550];
    memcpy(signedBytes, packet, body);
    memcpy(signedBytes + body, VOICE_DOMAIN, VOICE_DOMAIN_LEN);
    return ed25519Sign(seed, signedBytes, body + VOICE_DOMAIN_LEN, packet + body);
}
static void peerHeader(u8 *packet, u8 kind, u32 target, u32 sequence,
        const u8 *epoch, const u8 *challenge, u16 payloadLength)
{
    memset(packet, 0, 550); u8 *w = packet;
    memcpy(w, VOICE_MAGIC, 5); w += 5;
    wU8(&w, VOICE_VERSION); wU8(&w, kind); wU8(&w, 0);
    wU32(&w, vfBuildingPeer); wU32(&w, target); wU32(&w, sequence); wU16(&w, payloadLength);
    memcpy(w, epoch, 16); w += 16;
    if (challenge) memcpy(w, challenge, 16);
}
int voiceFixtureAnswerHello(void) {
    if (vfLength != 150 || vfPacket[6] != VOICE_KIND_PTT_START) return 0;
    u8 reply[550], epoch[16], challenge[16];
    memcpy(epoch, vfPacket + 22, 16); memset(challenge, 90, 16);
    peerHeader(reply, VOICE_KIND_CHALLENGE, vfHandle, 0, epoch, challenge, 0);
    if (!voiceFixtureResignPeer(reply, 150)) return 0;
    voiceReceiveFrame(reply, 150); return 1;
}
void voiceFixtureReplayHello(const u8 *media) {
    u8 hello[550];
    peerHeader(hello, VOICE_KIND_PTT_START, vfHandle, 0, media + 22, NULL, 0);
    if (voiceFixtureResignPeer(hello, 150)) voiceReceiveFrame(hello, 150);
}
void voiceFixtureForceSequenceWrap(void) {
    s_NextSeq = 0xffffffffu;
    const u8 payload = 0;
    (void)broadcastVoiceFrame(VOICE_KIND_OPUS_FRAME, &payload, 1);
}
u32 voiceFixturePeerAudio(u8 *packet, u32 target) {
    u8 epoch[16], challenge[16], hello[550];
    voice_peer_t *existing = NULL;
    for (u32 i = 0; i < VOICE_PEER_DECODERS_MAX; ++i)
        if (s_Peers[i].handle == vfBuildingPeer) existing = &s_Peers[i];
    if (existing && existing->pending && vfNow - existing->pending_ms < 3000)
        memcpy(epoch, existing->pending_epoch, 16);
    else memset(epoch, ++vfPeerEpoch, 16);
    peerHeader(hello, VOICE_KIND_PTT_START, vfHandle, 0, epoch, NULL, 0);
    if (!voiceFixtureResignPeer(hello, 150)) return 0;
    const u32 before = vfSent;
    voiceReceiveFrame(hello, 150);
    if (vfSent != before + 1 || vfPacket[6] != VOICE_KIND_CHALLENGE
            || memcmp(vfPacket + 22, epoch, 16)) return 0;
    memcpy(challenge, vfPacket + 38, 16);
    int err = 0;
    OpusEncoder *encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &err);
    if (!encoder || err != OPUS_OK) return 0;
    opus_int16 pcm[320]; u8 payload[400];
    for (int i = 0; i < 320; ++i) pcm[i] = (opus_int16)((i % 40) < 20 ? vfTone : -vfTone);
    const int length = opus_encode(encoder, pcm, 320, payload, sizeof(payload));
    opus_encoder_destroy(encoder);
    if (length <= 0) return 0;
    peerHeader(packet, VOICE_KIND_OPUS_FRAME, target, 1, epoch, challenge, (u16)length);
    memcpy(packet + VOICE_HEADER_LEN, payload, length);
    const u32 frameLength = 150 + length;
    return voiceFixtureResignPeer(packet, frameLength) ? frameLength : 0;
}

u32 voiceFixturePeerTone(u8 *packet, u32 peer, s32 amplitude) {
    vfBuildingPeer = peer; vfTone = amplitude;
    const u32 length = voiceFixturePeerAudio(packet, 1001);
    vfBuildingPeer = 2002; vfTone = 0; return length;
}

void voiceFixtureCaptureAmplitude(s16 value) { vfCaptureAmplitude = value; }
void voiceFixtureDeviceFailure(int capture, int playback) { vfCaptureFail = capture; vfPlaybackFail = playback; }
void voiceFixtureDeviceStopped(int capture, int playback) { vfCaptureStopped = capture; vfPlaybackStopped = playback; }
void voiceFixtureEncoderFailure(int fail) { vfEncoderFail = fail; }
int voiceFixtureCaptureOpen(void) { return s_CaptureDev != 0; }
