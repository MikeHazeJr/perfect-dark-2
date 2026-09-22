/* Production chat state machine with a deterministic clock, captured route
 * and no filesystem writes. Ed25519 sign/verify are the real implementation.
 * Friend discovery/key lookup are fixture boundaries, not discovery proof. */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "social.h"
#include "identity.h"
#include "presence.h"
#include "ed25519.h"
#include "system.h"
#include "fs.h"
#include "save_atomic.h"

static u32 fixtureNow;
static u32 fixtureLocalHandle;
static int fixtureLoaded, fixtureBlocked, fixtureSendFailure;
static u8 localSeed[32], localPub[32], peerSeed[32], peerPub[32];
static social_friend_t fixtureFriend;
static u8 fixturePackets[128][320];
static int fixturePacketCount;
static Uint32 chatFixtureTicks(void) { return fixtureNow; }
static Uint64 chatFixtureCounter(void) { return 100; }
static FILE *chatFixtureOpen(const char *path, const char *mode) { (void)path; (void)mode; return NULL; }
static void chatFixtureHome(char *path, u32 size) { snprintf(path, size, "unused-chat-fixture"); }
static s32 chatFixtureCreateDir(const char *path) { (void)path; return 0; }
static s32 chatFixtureAtomicBegin(save_atomic_file_t *transaction, const char *path) { (void)transaction; (void)path; return -1; }

#define SDL_GetTicks chatFixtureTicks
#define SDL_GetPerformanceCounter chatFixtureCounter
#define fopen chatFixtureOpen
#define sysGetHomePath chatFixtureHome
#define fsCreateDir chatFixtureCreateDir
#define saveAtomicBegin chatFixtureAtomicBegin
#include "../port/src/chat.c"
#undef SDL_GetTicks
#undef SDL_GetPerformanceCounter
#undef fopen
#undef sysGetHomePath
#undef fsCreateDir
#undef saveAtomicBegin

u32 socialMyHandle(void) { return fixtureLocalHandle; }
const u8 *identityGetPubkey(void) { return localPub; }
s32 identitySign(const void *message, u32 length, u8 *signature) {
    return ed25519Sign(localSeed, message, length, signature);
}
s32 socialFriendCount(void) { return 1; }
const social_friend_t *socialFriendAt(s32 index) { return index == 0 ? &fixtureFriend : NULL; }
const social_friend_t *socialFriendByHandle(u32 handle) { return handle == 2002 ? &fixtureFriend : NULL; }
s32 socialBlockIsHandle(u32 handle) { return handle == 2002 && fixtureBlocked; }
s32 socialHandleBindsPubkey(u32 handle, const u8 *pubkey) { return handle == 2002 && !memcmp(pubkey, peerPub, 32); }
s32 socialFriendBindPubkey(u32 handle, const u8 *pubkey) { return socialHandleBindsPubkey(handle, pubkey) ? 0 : -1; }
s32 presenceIsAgentLoaded(void) { return fixtureLoaded; }
s32 presenceSendChatFrame(u32 handle, const u8 *packet, u32 length) {
    if (fixtureSendFailure || handle != 2002 || length != 320) return -1;
    if (fixturePacketCount < 128) memcpy(fixturePackets[fixturePacketCount++], packet, length);
    return 0;
}

int chatFixtureReset(void) {
    fixtureNow = 100;
    fixtureLocalHandle = 1001;
    fixtureLoaded = 1;
    fixtureBlocked = fixtureSendFailure = fixturePacketCount = 0;
    memset(localSeed, 1, sizeof(localSeed));
    memset(peerSeed, 2, sizeof(peerSeed));
    if (!ed25519DerivePubkey(localSeed, localPub) || !ed25519DerivePubkey(peerSeed, peerPub)) return 0;
    memset(&fixtureFriend, 0, sizeof(fixtureFriend));
    fixtureFriend.handle = 2002;
    fixtureFriend.has_pubkey = 1;
    memcpy(fixtureFriend.pubkey, peerPub, 32);
    chatInit();
    return 1;
}
void chatFixtureAdvance(u32 milliseconds) { fixtureNow += milliseconds; chatTick(); }
void chatFixtureSetBlocked(int blocked) { fixtureBlocked = blocked; }
void chatFixtureSetLoaded(int loaded) { fixtureLoaded = loaded; }
void chatFixtureSetLocalHandle(u32 handle) { fixtureLocalHandle = handle; }
void chatFixtureSetSendFailure(int fail) { fixtureSendFailure = fail; }
int chatFixturePacketCount(void) { return fixturePacketCount; }
const u8 *chatFixturePacket(int index) { return index >= 0 && index < fixturePacketCount ? fixturePackets[index] : NULL; }
int chatFixtureSignPeer(u8 *packet) {
    u8 signedBytes[CHAT_BODY_LEN + CHAT_DOMAIN_LEN];
    memcpy(signedBytes, packet, CHAT_BODY_LEN);
    memcpy(signedBytes + CHAT_BODY_LEN, CHAT_DOMAIN_TAG, CHAT_DOMAIN_LEN);
    memcpy(packet + CHAT_PUBKEY_OFFSET, peerPub, 32);
    return ed25519Sign(peerSeed, signedBytes, sizeof(signedBytes), packet + CHAT_SIG_OFFSET);
}
int chatFixturePeerFrame(u8 *packet, u8 kind, u64 id, u16 sequence, u16 count, const char *text, u16 length) {
    if (!packet || length > CHAT_FRAME_PAYLOAD_LEN) return 0;
    memset(packet, 0, CHAT_FRAME_LEN);
    u8 *write = packet;
    memcpy(write, CHAT_MAGIC, 5); write += 5;
    wU8(&write, CHAT_VERSION); wU8(&write, kind); wU8(&write, 0);
    wU32(&write, 2002); wU32(&write, 1001); wU64(&write, id);
    wU16(&write, length); wU16(&write, sequence); wU16(&write, count); wU16(&write, 0);
    if (length) memcpy(packet + 32, text, length);
    return chatFixtureSignPeer(packet);
}
int chatFixtureParseId(const char *text, u64 *id) {
    jread_t reader = {text, text};
    return jReadUint64(&reader, id);
}
int chatFixtureRoundtripText(const char *text, char *out, u32 capacity) {
    char encoded[CHAT_TEXT_MAX * 6 + 4];
    size_t length = 0;
    appendJsonString(encoded, sizeof(encoded), &length, text);
    if (length >= sizeof(encoded)) return 0;
    encoded[length] = 0;
    jread_t reader = {encoded, encoded};
    return jReadString(&reader, out, capacity);
}
