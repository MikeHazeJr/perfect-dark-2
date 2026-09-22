#include "catch.hpp"
#include "voice.h"
#include "net/group_session.h"
#include <cstring>
extern "C" {
int voiceFixtureReset(void);
void voiceFixtureCaptureAmplitude(s16);
void voiceFixtureDeviceFailure(int, int);
void voiceFixtureDeviceStopped(int, int);
void voiceFixtureEncoderFailure(int);
int voiceFixtureCaptureOpen(void);
void voiceFixtureSetState(int);
void voiceFixtureSetOwner(u32);
void voiceFixtureSetMute(int);
void voiceFixtureSetBlocked(int);
void voiceFixtureSetRngFailure(int);
void voiceFixtureAdvance(u32);
u32 voiceFixtureQueued(void);
u32 voiceFixtureSent(void);
const u8 *voiceFixturePacket(void);
u32 voiceFixturePacketLength(void);
int voiceFixtureVerifySent(void);
int voiceFixtureAnswerHello(void);
u32 voiceFixturePeerAudio(u8 *, u32);
int voiceFixtureResignPeer(u8 *, u32);
void voiceFixtureSetSequence(u8 *, u32);
void voiceFixtureMakeStop(u8 *, u32);
void voiceFixtureReplayHello(const u8 *);
void voiceFixtureForceSequenceWrap(void);
void voiceFixtureEnableSecondPeer(void);
u32 voiceFixturePeerTone(u8 *, u32, s32);
const s16 *voiceFixtureOutput(void);
u32 voiceFixtureOutputFrames(void);
u32 voiceFixtureBuffered(void);
u32 voiceFixtureNextSequence(u32);
void voiceFixtureSetPlaybackQueued(u32);
void voiceFixtureSetCaptureQueued(u32);
u32 voiceFixtureCaptureQueued(void);
}
static void play(const u8 *packet, u32 n) {
    voiceReceiveFrame(packet, n);
    voiceFixtureAdvance(40);
}

TEST_CASE("Voice production ingress validates signed recipient and accepted audience before decode", "[net][voice][transport]") {
    REQUIRE(voiceFixtureReset()); u8 packet[550];
    auto n = voiceFixturePeerAudio(packet, 1001); REQUIRE(n > 150);
    packet[n-1] ^= 1; voiceReceiveFrame(packet, n); REQUIRE(voiceFixtureQueued() == 0);
    REQUIRE(voiceFixtureResignPeer(packet, n));
    voiceFixtureSetState(GROUP_PEER_INVITED); voiceReceiveFrame(packet, n);
    REQUIRE(voiceFixtureQueued() == 0); REQUIRE(voiceStatsRxFrames() == 0);
    voiceFixtureSetState(GROUP_PEER_CONNECTED); voiceReceiveFrame(packet, n);
    REQUIRE(voiceFixtureQueued() == 0); REQUIRE(voiceStatsRxFrames() == 0);
    voiceFixtureAdvance(40); REQUIRE(voiceFixtureQueued() == 640); REQUIRE(voiceStatsRxFrames() == 1);
}

TEST_CASE("Voice production ingress rejects broadcast other recipients malformed kinds and truncation", "[net][voice][transport]") {
    REQUIRE(voiceFixtureReset()); u8 packet[550];
    for (u32 recipient : {0u, 999u}) {
        auto n = voiceFixturePeerAudio(packet, recipient); REQUIRE(n > 150);
        voiceReceiveFrame(packet, n); REQUIRE(voiceFixtureQueued() == 0);
    }
    auto n = voiceFixturePeerAudio(packet, 1001); REQUIRE(n > 150);
    for (u32 length = 0; length < n; ++length) voiceReceiveFrame(packet, length);
    REQUIRE(voiceFixtureBuffered() == 0);
    packet[6] = 9; REQUIRE(voiceFixtureResignPeer(packet, n));
    voiceReceiveFrame(packet, n); REQUIRE(voiceFixtureBuffered() == 0);
}

TEST_CASE("Voice production send targets accepted peer and suppresses unaccepted capture", "[net][voice][transport]") {
    REQUIRE(voiceFixtureReset()); voiceFixtureSetState(GROUP_PEER_INVITED); voicePttBegin();
    REQUIRE_FALSE(voicePttActive()); REQUIRE(voiceFixtureSent() == 0);
    voiceFixtureSetState(GROUP_PEER_CONNECTED); voicePttBegin();
    REQUIRE(voicePttActive()); REQUIRE(voiceFixtureSent() == 1);
    auto packet = voiceFixturePacket(); REQUIRE(voiceFixturePacketLength() == 150);
    REQUIRE(packet[12] == (2002 & 255)); REQUIRE(packet[13] == (2002 >> 8));
    REQUIRE(packet[14] == 0); REQUIRE(packet[15] == 0); REQUIRE(voiceFixtureVerifySent());
    REQUIRE(voiceFixtureAnswerHello()); voicePttEnd(); REQUIRE(voiceFixtureSent() == 2);
}

TEST_CASE("Voice production Agent change revokes playback capture and previous group consent", "[net][voice][transport]") {
    REQUIRE(voiceFixtureReset()); u8 packet[550];
    auto n = voiceFixturePeerAudio(packet, 1001); REQUIRE(n > 150);
    play(packet, n); REQUIRE(voiceFixtureQueued() == 640);
    voicePttBegin(); REQUIRE(voicePttActive()); voiceFixtureSetOwner(999); voiceTick();
    REQUIRE_FALSE(voiceEnabled()); REQUIRE_FALSE(voicePttActive());
    REQUIRE(voiceFixtureQueued() == 0); REQUIRE(voiceFixtureBuffered() == 0);
    voiceSetEnabled(1); voicePttBegin(); REQUIRE_FALSE(voicePttActive());
}

TEST_CASE("Voice production mute block removal and disable clear queued playback", "[net][voice][transport]") {
    for (int revoke = 0; revoke < 4; ++revoke) {
        REQUIRE(voiceFixtureReset()); u8 packet[550];
        auto n = voiceFixturePeerAudio(packet, 1001); REQUIRE(n > 150);
        play(packet, n); REQUIRE(voiceFixtureQueued() == 640);
        if (revoke == 0) voiceFixtureSetMute(1);
        if (revoke == 1) voiceFixtureSetBlocked(1);
        if (revoke == 2) voiceFixtureSetState(GROUP_PEER_FAILED);
        if (revoke == 3) voiceSetEnabled(0);
        voiceTick(); REQUIRE(voiceFixtureQueued() == 0); REQUIRE(voiceFixtureBuffered() == 0);
        voiceReceiveFrame(packet, n); REQUIRE(voiceFixtureBuffered() == 0);
    }
}

TEST_CASE("Voice rejects duplicate media and old sessions without replacing current playback", "[net][voice][session]") {
    REQUIRE(voiceFixtureReset()); u8 oldPacket[550], current[550];
    auto oldLength = voiceFixturePeerAudio(oldPacket, 1001); REQUIRE(oldLength > 150);
    play(oldPacket, oldLength); REQUIRE(voiceFixtureQueued() == 640); REQUIRE(voiceStatsRxFrames() == 1);
    voiceReceiveFrame(oldPacket, oldLength); REQUIRE(voiceFixtureQueued() == 640); REQUIRE(voiceFixtureBuffered() == 0);
    auto currentLength = voiceFixturePeerAudio(current, 1001); REQUIRE(currentLength > 150);
    REQUIRE(voiceFixtureQueued() == 640);
    play(current, currentLength); REQUIRE(voiceFixtureQueued() == 640); REQUIRE(voiceStatsRxFrames() == 2);
    voiceFixtureReplayHello(oldPacket); REQUIRE(voiceFixtureQueued() == 640);
    voiceReceiveFrame(oldPacket, oldLength); REQUIRE(voiceFixtureBuffered() == 0);
    voiceFixtureMakeStop(oldPacket, 2); voiceReceiveFrame(oldPacket, 150); REQUIRE(voiceFixtureQueued() == 640);
    voiceFixtureMakeStop(current, 2); voiceReceiveFrame(current, 150);
    REQUIRE(voiceFixtureQueued() == 0); REQUIRE_FALSE(voicePeerIsTalking(2002));
}

TEST_CASE("Voice requires the fresh receiver challenge and fails closed without randomness", "[net][voice][session]") {
    REQUIRE(voiceFixtureReset()); u8 packet[550];
    auto n = voiceFixturePeerAudio(packet, 1001); REQUIRE(n > 150);
    packet[38] ^= 1; REQUIRE(voiceFixtureResignPeer(packet, n));
    voiceReceiveFrame(packet, n); REQUIRE(voiceFixtureBuffered() == 0);
    packet[38] ^= 1; REQUIRE(voiceFixtureResignPeer(packet, n));
    play(packet, n); REQUIRE(voiceFixtureQueued() == 640);
    REQUIRE(voiceFixtureReset()); voiceFixtureSetRngFailure(1); voicePttBegin();
    REQUIRE_FALSE(voicePttActive()); REQUIRE(voiceFixtureSent() == 0);
    REQUIRE(voiceFixturePeerAudio(packet, 1001) == 0); REQUIRE(voiceFixtureBuffered() == 0);
}

TEST_CASE("Voice replay window rejects duplicates stale frames and expired receive sessions", "[net][voice][session]") {
    REQUIRE(voiceFixtureReset()); u8 packet[550];
    auto n = voiceFixturePeerAudio(packet, 1001); REQUIRE(n > 150);
    play(packet, n); REQUIRE(voiceStatsRxFrames() == 1);
    voiceFixtureSetSequence(packet, 70); REQUIRE(voiceFixtureResignPeer(packet, n));
    play(packet, n); REQUIRE(voiceStatsRxFrames() == 2);
    for (u32 sequence : {69u, 70u, 1u}) {
        voiceFixtureSetSequence(packet, sequence); REQUIRE(voiceFixtureResignPeer(packet, n));
        voiceReceiveFrame(packet, n); REQUIRE(voiceStatsRxFrames() == 2); REQUIRE(voiceFixtureBuffered() == 0);
    }
    voiceFixtureAdvance(1000); REQUIRE(voiceFixtureQueued() == 0);
    voiceFixtureSetSequence(packet, 71); REQUIRE(voiceFixtureResignPeer(packet, n));
    voiceReceiveFrame(packet, n); REQUIRE(voiceFixtureBuffered() == 0);
}

TEST_CASE("Voice rotates its sender epoch before sequence wrap and requires a new challenge", "[net][voice][session]") {
    REQUIRE(voiceFixtureReset()); voicePttBegin(); REQUIRE(voiceFixtureSent() == 1);
    u8 epoch[16]; std::memcpy(epoch, voiceFixturePacket() + 22, 16);
    REQUIRE(voiceFixtureAnswerHello()); voiceFixtureForceSequenceWrap(); REQUIRE(voiceFixtureSent() == 2);
    REQUIRE(voiceFixturePacket()[6] == 1); REQUIRE(std::memcmp(epoch, voiceFixturePacket() + 22, 16) != 0);
    for (int i = 16; i < 20; ++i) REQUIRE(voiceFixturePacket()[i] == 0);
}

TEST_CASE("Voice mixes simultaneous real Opus speakers into one saturating output slice", "[net][voice][playout]") {
    s16 isolated[2][320]; u8 packets[2][550]; u32 lengths[2];
    for (int i = 0; i < 2; ++i) {
        REQUIRE(voiceFixtureReset()); voiceFixtureEnableSecondPeer();
        auto n = voiceFixturePeerTone(packets[i], i ? 3003 : 2002, 30000); REQUIRE(n > 150);
        play(packets[i], n); REQUIRE(voiceFixtureOutputFrames() == 1);
        std::memcpy(isolated[i], voiceFixtureOutput(), sizeof(isolated[i]));
    }
    REQUIRE(voiceFixtureReset()); voiceFixtureEnableSecondPeer();
    lengths[0] = voiceFixturePeerTone(packets[0], 2002, 30000);
    lengths[1] = voiceFixturePeerTone(packets[1], 3003, 30000);
    REQUIRE(lengths[0] > 150); REQUIRE(lengths[1] > 150);
    voiceReceiveFrame(packets[0], lengths[0]); voiceReceiveFrame(packets[1], lengths[1]);
    REQUIRE(voiceFixtureQueued() == 0); voiceFixtureAdvance(40);
    REQUIRE(voiceFixtureQueued() == 640); REQUIRE(voiceFixtureOutputFrames() == 1); REQUIRE(voiceStatsRxFrames() == 2);
    bool clipped = false;
    for (int i = 0; i < 320; ++i) {
        int sum = int(isolated[0][i]) + int(isolated[1][i]);
        int expected = sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum;
        clipped |= sum != expected; REQUIRE(voiceFixtureOutput()[i] == expected);
    }
    REQUIRE(clipped);
    REQUIRE(voiceFixtureReset()); voiceFixtureEnableSecondPeer();
    lengths[0] = voiceFixturePeerTone(packets[0], 2002, 30000); REQUIRE(lengths[0] > 150);
    play(packets[0], lengths[0]); REQUIRE(voiceFixtureQueued() == 640);
    lengths[1] = voiceFixturePeerTone(packets[1], 3003, 30000); REQUIRE(lengths[1] > 150);
    voiceReceiveFrame(packets[1], lengths[1]); REQUIRE(voiceFixtureQueued() == 640);
}

TEST_CASE("Voice buffers reordered encoded packets until their ordered playout slots", "[net][voice][playout]") {
    REQUIRE(voiceFixtureReset()); u8 first[550], second[550], third[550];
    auto n = voiceFixturePeerAudio(first, 1001); REQUIRE(n > 150);
    std::memcpy(second, first, 550); std::memcpy(third, first, 550);
    voiceFixtureSetSequence(second, 2); voiceFixtureSetSequence(third, 3);
    REQUIRE(voiceFixtureResignPeer(second, n)); REQUIRE(voiceFixtureResignPeer(third, n));
    voiceReceiveFrame(first, n); voiceReceiveFrame(third, n); voiceReceiveFrame(second, n);
    REQUIRE(voiceStatsRxFrames() == 0); REQUIRE(voiceFixtureBuffered() == 3);
    voiceFixtureAdvance(40); REQUIRE(voiceStatsRxFrames() == 1); REQUIRE(voiceFixtureNextSequence(2002) == 2);
    voiceFixtureAdvance(20); REQUIRE(voiceStatsRxFrames() == 2); REQUIRE(voiceFixtureNextSequence(2002) == 3);
    voiceFixtureAdvance(20); REQUIRE(voiceStatsRxFrames() == 3); REQUIRE(voiceFixtureNextSequence(2002) == 4);
    voiceReceiveFrame(second, n); REQUIRE(voiceFixtureBuffered() == 0); REQUIRE(voiceStatsRxFrames() == 3);
}

TEST_CASE("Voice bounds loss concealment backlog and delayed output after a stall", "[net][voice][playout]") {
    REQUIRE(voiceFixtureReset()); u8 packet[550];
    auto n = voiceFixturePeerAudio(packet, 1001); REQUIRE(n > 150);
    voiceReceiveFrame(packet, n); voiceFixtureSetSequence(packet, 3); REQUIRE(voiceFixtureResignPeer(packet, n));
    voiceReceiveFrame(packet, n); voiceFixtureAdvance(40); REQUIRE(voiceStatsRxFrames() == 1);
    voiceFixtureAdvance(20); REQUIRE(voiceStatsRxFrames() == 1); REQUIRE(voiceFixtureQueued() == 640);
    voiceFixtureAdvance(20); REQUIRE(voiceStatsRxFrames() == 2);
    for (int i = 0; i < 4; ++i) voiceFixtureAdvance(20);
    REQUIRE(voiceFixtureQueued() == 0); REQUIRE(voiceStatsRxFrames() == 2);
    for (u32 seq = 100; seq <= 200; ++seq) {
        voiceFixtureSetSequence(packet, seq); REQUIRE(voiceFixtureResignPeer(packet, n));
        voiceReceiveFrame(packet, n); REQUIRE(voiceFixtureBuffered() <= 8);
    }
    voiceFixtureSetPlaybackQueued(64000); voiceTick(); REQUIRE(voiceFixtureQueued() <= 1280);
    voiceFixtureAdvance(40); REQUIRE(voiceFixtureQueued() <= 1280);
    voiceFixtureAdvance(1000); REQUIRE(voiceFixtureQueued() == 0); REQUIRE(voiceFixtureBuffered() == 0);
}

TEST_CASE("Voice clears idle capture and drops excessive capture backlog before sending", "[net][voice][playout]") {
    REQUIRE(voiceFixtureReset()); voiceFixtureSetCaptureQueued(65536); voiceTick(); REQUIRE(voiceFixtureCaptureQueued() == 0);
    voicePttBegin(); REQUIRE(voiceFixtureSent() == 1); REQUIRE(voiceFixtureAnswerHello());
    voiceFixtureSetCaptureQueued(65536); voiceTick(); REQUIRE(voiceFixtureCaptureQueued() == 0); REQUIRE(voiceFixtureSent() == 1);
    voiceFixtureSetCaptureQueued(1280); voiceTick(); REQUIRE(voiceFixtureCaptureQueued() == 0); REQUIRE(voiceFixtureSent() == 3);
    voiceSetCaptureMode(VOICE_CAPTURE_OFF); REQUIRE(voiceFixtureSent() == 4); REQUIRE(voiceFixturePacket()[6] == 2);
    voiceSetCaptureMode(VOICE_CAPTURE_PUSH_TO_TALK); voicePttBegin(); REQUIRE(voiceFixtureSent() == 5);
    REQUIRE(voiceFixtureAnswerHello()); voiceSetEnabled(0); REQUIRE(voiceFixtureSent() == 6); REQUIRE(voiceFixturePacket()[6] == 2);
    REQUIRE_FALSE(voiceEnabled());
}

static void captureFrame(s16 amplitude) {
    voiceFixtureCaptureAmplitude(amplitude);
    voiceFixtureSetCaptureQueued(640);
    voiceFixtureAdvance(20);
}
TEST_CASE("Voice activity uses sensitivity hysteresis and a bounded silence hold", "[net][voice][vad]") {
    REQUIRE(voiceFixtureReset()); voiceSetCaptureMode(VOICE_CAPTURE_VOICE_ACTIVE);
    captureFrame(500); REQUIRE_FALSE(voiceLocalIsTransmitting()); REQUIRE(voiceFixtureSent() == 0);
    captureFrame(1000); REQUIRE(voiceLocalIsTransmitting()); REQUIRE_FALSE(voicePttActive());
    REQUIRE(voiceFixtureAnswerHello()); captureFrame(500);
    REQUIRE(voiceStatsTxFrames() == 1); REQUIRE(voiceInputLevel() > 0.01f);
    for (int i = 0; i < 14; ++i) captureFrame(0);
    REQUIRE(voiceLocalIsTransmitting());
    voiceFixtureCaptureAmplitude(0); voiceFixtureSetCaptureQueued(1280); voiceFixtureAdvance(40);
    REQUIRE(voiceFixtureCaptureQueued() == 0);
    REQUIRE_FALSE(voiceLocalIsTransmitting()); REQUIRE(voiceFixturePacket()[6] == 2);
    voiceSetSensitivity(0); captureFrame(1000); REQUIRE_FALSE(voiceLocalIsTransmitting());
    voiceSetSensitivity(100); captureFrame(150); REQUIRE(voiceLocalIsTransmitting());
    voiceFixtureAdvance(300); REQUIRE_FALSE(voiceLocalIsTransmitting()); REQUIRE(voiceInputLevel() == 0);
    captureFrame(150); REQUIRE(voiceLocalIsTransmitting());
    voiceFixtureSetCaptureQueued(3200); voiceTick();
    REQUIRE_FALSE(voiceLocalIsTransmitting()); REQUIRE(voiceFixtureCaptureQueued() == 0);
    voiceSetSensitivity(-20); REQUIRE(voiceGetSensitivity() == 0);
    voiceSetSensitivity(500); REQUIRE(voiceGetSensitivity() == 100);
}
TEST_CASE("Voice activity respects audience and mode ownership during queued capture", "[net][voice][vad]") {
    REQUIRE(voiceFixtureReset()); voiceSetCaptureMode(VOICE_CAPTURE_VOICE_ACTIVE);
    voiceFixtureSetState(GROUP_PEER_INVITED); captureFrame(30000);
    REQUIRE_FALSE(voiceLocalIsTransmitting()); REQUIRE(voiceFixtureSent() == 0);
    REQUIRE(voiceInputLevel() > 0.9f);
    voiceFixtureSetState(GROUP_PEER_CONNECTED); captureFrame(30000);
    REQUIRE(voiceLocalIsTransmitting()); REQUIRE(voiceFixtureAnswerHello());
    captureFrame(30000); REQUIRE(voiceStatsTxFrames() == 1);
    voiceSetCaptureMode(VOICE_CAPTURE_OFF);
    REQUIRE_FALSE(voiceLocalIsTransmitting()); REQUIRE(voiceFixturePacket()[6] == 2);
    REQUIRE(voiceEnabled()); REQUIRE(voiceInputLevel() == 0);
    voiceSetCaptureMode(VOICE_CAPTURE_VOICE_ACTIVE); captureFrame(30000);
    voiceFixtureSetOwner(9009); voiceTick();
    REQUIRE_FALSE(voiceEnabled()); REQUIRE_FALSE(voiceLocalIsTransmitting());
    REQUIRE(voiceInputLevel() == 0); REQUIRE(voiceGetSensitivity() == 50);
}
TEST_CASE("Voice enable and hot device failure report capability and listen-only fallback", "[net][voice][devices]") {
    REQUIRE(voiceFixtureReset()); REQUIRE(voiceCodecAvailable());
    voiceSetEnabled(0); voiceSetCaptureMode(VOICE_CAPTURE_OFF); voiceSetEnabled(1);
    REQUIRE(voiceEnabled()); REQUIRE_FALSE(voiceFixtureCaptureOpen());
    voiceSetEnabled(0); voiceSetCaptureMode(VOICE_CAPTURE_PUSH_TO_TALK);
    voiceFixtureEncoderFailure(1); voiceSetEnabled(1);
    REQUIRE(voiceEnabled()); REQUIRE(voiceGetCaptureMode() == VOICE_CAPTURE_OFF);
    REQUIRE_FALSE(voiceFixtureCaptureOpen()); REQUIRE(std::strlen(voiceLastError()) > 0);
    voiceFixtureEncoderFailure(0); voiceSetEnabled(0); voiceSetCaptureMode(VOICE_CAPTURE_PUSH_TO_TALK);
    voiceSetEnabled(0); voiceFixtureDeviceFailure(0, 1); voiceSetEnabled(1);
    REQUIRE_FALSE(voiceEnabled()); REQUIRE(std::strlen(voiceLastError()) > 0);
    voiceFixtureDeviceFailure(1, 0); voiceSetEnabled(1);
    REQUIRE(voiceEnabled()); REQUIRE(voiceGetCaptureMode() == VOICE_CAPTURE_OFF);
    REQUIRE(std::strlen(voiceLastError()) > 0); voicePttBegin(); REQUIRE_FALSE(voicePttActive());
    voiceFixtureDeviceFailure(0, 0); voiceSetCaptureMode(VOICE_CAPTURE_PUSH_TO_TALK);
    REQUIRE(std::strlen(voiceLastError()) == 0); voicePttBegin(); REQUIRE(voicePttActive());
    voiceFixtureDeviceStopped(1, 0); voiceTick();
    REQUIRE(voiceEnabled()); REQUIRE(voiceGetCaptureMode() == VOICE_CAPTURE_OFF);
    REQUIRE_FALSE(voiceLocalIsTransmitting()); REQUIRE(std::strlen(voiceLastError()) > 0);
    voiceFixtureDeviceStopped(0, 1); voiceTick();
    REQUIRE_FALSE(voiceEnabled()); REQUIRE(std::strlen(voiceLastError()) > 0);
}
