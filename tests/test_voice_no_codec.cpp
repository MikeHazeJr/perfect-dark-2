#include "catch.hpp"
#include "voice.h"
#include <cstring>
extern "C" {
void noCodec_voiceInit(void);
void noCodec_voiceSetEnabled(int);
int noCodec_voiceEnabled(void);
int noCodec_voiceCodecAvailable(void);
int noCodec_voiceLocalIsTransmitting(void);
const char *noCodec_voiceLastError(void);
void noCodec_voicePttBegin(void);
void noCodec_voiceSetCaptureMode(voice_capture_mode_t);
}
TEST_CASE("Voice without a codec cannot report enabled or transmitting", "[net][voice][no-codec]") {
    noCodec_voiceInit(); REQUIRE_FALSE(noCodec_voiceCodecAvailable());
    noCodec_voiceSetEnabled(1); REQUIRE_FALSE(noCodec_voiceEnabled());
    REQUIRE(std::strlen(noCodec_voiceLastError()) > 0);
    noCodec_voicePttBegin(); REQUIRE_FALSE(noCodec_voiceLocalIsTransmitting());
    noCodec_voiceSetCaptureMode(VOICE_CAPTURE_VOICE_ACTIVE);
    noCodec_voiceSetEnabled(1); REQUIRE_FALSE(noCodec_voiceEnabled());
    REQUIRE_FALSE(noCodec_voiceLocalIsTransmitting());
}
