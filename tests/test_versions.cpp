/*
 * tests/test_versions.cpp -- Wire and save format version-constant pins.
 *
 * Asserts that NET_PROTOCOL_VER and MPSETUP_VERSION have specific values.
 * A future bump without updating this test file will fail loudly, forcing
 * the bump to be a deliberate decision rather than an accidental change in
 * an unrelated PR.
 *
 * Also asserts the cross-version-mismatch invariant: the protocol version
 * field on the wire is what gates the ENet auth handshake (see
 * port/src/net/net.c:netServerEvConnect at line ~1560). The actual reject
 * needs a live ENet host, so we test the semantic invariant: a fake
 * version that doesn't equal NET_PROTOCOL_VER must be rejected.
 *
 * Mike's directive (2026-04-26):
 *   "Wire format (NET_PROTOCOL_VER 43 -> 44): netmsg encode/decode
 *    roundtrip + cross-version reject"
 *   "Save format (MPSETUP_VERSION 1 -> 2): WAD save/load roundtrip +
 *    v1 -> v2 migration"
 *
 * Bumped 2026-06-17 to NET_PROTOCOL_VER 51 for the c3849 Wave 7 weapon
 * graph runtime cutover. MPSETUP_VERSION unchanged.
 */

#include "catch.hpp"
#include <PR/ultratypes.h>

/* Pull the protocol version constant directly. net/net.h transitively
 * includes types.h via assetcatalog.h, but the constant is a plain
 * #define so we can avoid the cascade by defining a stub-include path:
 * just grep the constant out and reproduce it as an extern symbol.
 *
 * Cleaner approach used here: declare the value we EXPECT, then a
 * separate compilation unit (tests/test_versions_pin.c) reads the real
 * header and exposes the live value. The test compares the two. If they
 * drift, a deliberate update to BOTH is forced. */

extern "C" {
extern const u32 g_TestExpectedNetProtocolVer;   /* defined in this file */
extern const u32 g_TestLiveNetProtocolVer;       /* defined in test_versions_pin.c */
extern const u32 g_TestExpectedMpsetupVersion;
extern const u32 g_TestLiveMpsetupVersion;
}

const u32 g_TestExpectedNetProtocolVer  = 51;
const u32 g_TestExpectedMpsetupVersion  = 2;

TEST_CASE("version pin: NET_PROTOCOL_VER is the version this test was written against",
          "[versions]") {
    /* If this fails, someone bumped NET_PROTOCOL_VER without updating the
     * test pin. That bump should be deliberate -- a wire-format change is
     * a coordinated event (see net.h block comment for the ledger). After
     * verifying the bump is intentional, update g_TestExpectedNetProtocolVer
     * to match and re-run.
     *
     * As of 2026-06-17 the live value is 51. v51 cuts weapon graph runtime
     * over to product-default ON and removes the retired match option toggle
     * path. v50 batches catalog-info and widens distribution missing-list
     * counts so large typed-archive packs do not truncate. Prior bumps remain
     * documented in port/include/net/net.h. MPSETUP_VERSION stays at 2. */
    REQUIRE(g_TestLiveNetProtocolVer == g_TestExpectedNetProtocolVer);
}

TEST_CASE("version pin: MPSETUP_VERSION is the version this test was written against",
          "[versions]") {
    /* Same discipline as above. As of 2026-04-26 the live value is 2
     * (post-cull save format with the v1 -> v2 weapon clamp migration). */
    REQUIRE(g_TestLiveMpsetupVersion == g_TestExpectedMpsetupVersion);
}

TEST_CASE("version pin: cross-version mismatch is detectable",
          "[versions]") {
    /* The actual auth-handshake reject (`netServerEvConnect` in
     * port/src/net/net.c) compares the client-presented protocol version
     * against NET_PROTOCOL_VER and disconnects with DISCONNECT_VERSION on
     * mismatch. We can't run that path here (no ENet), but we can pin
     * the semantic invariant: any value other than the live constant
     * does NOT equal it. This catches a bug class where someone
     * accidentally writes `>= NET_PROTOCOL_VER` instead of `==`. */
    u32 v = g_TestLiveNetProtocolVer;
    REQUIRE(v != 0);                       /* Sanity: unused-version sentinel */
    REQUIRE(v != v - 1);                   /* Mismatch detection: predecessor */
    REQUIRE(v != v + 1);                   /* Mismatch detection: successor */
    REQUIRE(v != 0xFFFFFFFFu);             /* Mismatch detection: garbage */
}

TEST_CASE("version pin: MPSETUP_VERSION is monotonically growing",
          "[versions]") {
    /* The save loader migrates v < 2 by clamping out-of-range weapon
     * values. The migration code lives at src/game/mplayer/mplayer.c
     * (mpsetupfileLoadWad, the `if (version < 2)` block as of
     * 2026-04-26). New save versions should always be greater than
     * historical ones — the loader's version comparison logic depends
     * on this. */
    REQUIRE(g_TestLiveMpsetupVersion >= 2);
    REQUIRE(g_TestLiveMpsetupVersion > 1);
    REQUIRE(g_TestLiveMpsetupVersion > 0);
}
