/* Compile the production adapter's no-codec branch in every native test build. */
#undef HAVE_OPUS
#include <stddef.h>
#include "social.h"
#include "net/group_session.h"
static u32 noCodecHandle(void) { return 1001; }
static const group_session_t *noCodecGroup(void) { static group_session_t group; return &group; }
static const social_friend_t *noCodecFriend(u32 handle) { (void)handle; return NULL; }
static s32 noCodecBlocked(u32 handle) { (void)handle; return 0; }
#define socialMyHandle noCodecHandle
#define groupSessionGet noCodecGroup
#define socialFriendByHandle noCodecFriend
#define socialBlockIsHandle noCodecBlocked
#define voiceInit noCodec_voiceInit
#define voiceShutdown noCodec_voiceShutdown
#define voiceTick noCodec_voiceTick
#define voiceReceiveFrame noCodec_voiceReceiveFrame
#define voiceEnabled noCodec_voiceEnabled
#define voiceSetEnabled noCodec_voiceSetEnabled
#define voiceCodecAvailable noCodec_voiceCodecAvailable
#define voiceLastError noCodec_voiceLastError
#define voiceGetSensitivity noCodec_voiceGetSensitivity
#define voiceSetSensitivity noCodec_voiceSetSensitivity
#define voiceInputLevel noCodec_voiceInputLevel
#define voiceGetCaptureMode noCodec_voiceGetCaptureMode
#define voiceSetCaptureMode noCodec_voiceSetCaptureMode
#define voicePttBegin noCodec_voicePttBegin
#define voicePttEnd noCodec_voicePttEnd
#define voicePttActive noCodec_voicePttActive
#define voiceLocalIsTransmitting noCodec_voiceLocalIsTransmitting
#define voicePeerIsTalking noCodec_voicePeerIsTalking
#define voiceStatsRxFrames noCodec_voiceStatsRxFrames
#define voiceStatsTxFrames noCodec_voiceStatsTxFrames
#include "../port/src/voice.c"
