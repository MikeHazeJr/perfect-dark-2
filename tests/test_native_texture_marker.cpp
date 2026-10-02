#include "catch.hpp"
#include "texture_marker.h"
#include "types.h"

TEST_CASE("native texture markers retain wide primary and secondary IDs", "[texture][texture-marker]")
{
    const uintptr_t control = textureMarkerNativeControl(0xc0fed203);
    const auto payload = textureMarkerDecode(control,
        textureMarkerNativePayload(65534, 32768, 255));
    REQUIRE(payload.primary == 65534);
    REQUIRE(payload.secondary == 32768);
    REQUIRE(payload.minimum == 255);
    REQUIRE(static_cast<uint32_t>(control) == 0xc0fed203);
    const auto other = textureMarkerDecode(control,
        textureMarkerNativePayload(65534 - 4096, 32768 + 4096, 0));
    REQUIRE(other.primary != payload.primary);
    REQUIRE(other.secondary != payload.secondary);
    REQUIRE(other.minimum == 0);
}

TEST_CASE("legacy texture markers preserve both 12-bit identities and minimum", "[texture][texture-marker]")
{
    const auto payload = textureMarkerDecode(0xc0fed203,
        (uintptr_t(231) << 24) | (uintptr_t(3502) << 12) | 1234);
    REQUIRE(payload.primary == 1234);
    REQUIRE(payload.secondary == 3502);
    REQUIRE(payload.minimum == 231);
}

TEST_CASE("texture native records and reverse prefixes preserve unsigned identities", "[texture][texture-marker]")
{
    struct tex native = {};
    struct texcacheitem cached = {};
    struct hitthing hit = {};
    uint16_t prefix = 65534;
    native.texturenum = prefix;
    cached.texturenum = native.texturenum;
    hit.texturenum = cached.texturenum;
    REQUIRE(native.texturenum == 65534);
    REQUIRE(cached.texturenum == 65534);
    REQUIRE(hit.texturenum == 65534);
    hit.texturenum = -1;
    REQUIRE(hit.texturenum < 0);
    hit.texturenum = TEXTURE_HIT_GLASS;
    REQUIRE(hit.texturenum >= TEXTURE_NATIVE_SLOT_LIMIT);
}
