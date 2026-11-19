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
 * the production connection decoder rejects every mismatched wire version.
 *
 * Mike's directive (2026-04-26):
 *   "Wire format (NET_PROTOCOL_VER 43 -> 44): netmsg encode/decode
 *    roundtrip + cross-version reject"
 *   "Save format (MPSETUP_VERSION 1 -> 2): WAD save/load roundtrip +
 *    v1 -> v2 migration"
 *
 * Bumped 2026-11-17 to NET_PROTOCOL_VER 65 for D-027 Grid matches and creator traffic.
 * Bumped 2026-11-16 to NET_PROTOCOL_VER 64 for host-authoritative scenario
 * state (SVC_SCENARIO_STATE, SVC_BOT_PICKUP; MULTIPLAYER's v62, renumbered
 * after dev's v63). Bumped 2026-11-15 to NET_PROTOCOL_VER 63 for
 * UCMD_GUNROLL_LEFT/RIGHT (T-MODDING-008).
 * Bumped 2026-09-08 to NET_PROTOCOL_VER 61 for authenticated lobby rosters.
 * Version 60 added room initial snapshots,
 * operation results and access-aware room lists. MPSETUP_VERSION remains 3.
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
#include "net/net_reconnect.h"
extern const u32 g_TestExpectedNetProtocolVer;   /* defined in this file */
extern const u32 g_TestLiveNetProtocolVer;       /* defined in test_versions_pin.c */
extern const u32 g_TestExpectedMpsetupVersion;
extern const u32 g_TestLiveMpsetupVersion;
}

const u32 g_TestExpectedNetProtocolVer  = 65;
const u32 g_TestExpectedMpsetupVersion  = 3;

TEST_CASE("version pin: NET_PROTOCOL_VER is the version this test was written against",
          "[versions]") {
    /* If this fails, someone bumped NET_PROTOCOL_VER without updating the
     * test pin. That bump should be deliberate -- a wire-format change is
     * a coordinated event (see net.h block comment for the ledger). After
     * verifying the bump is intentional, update g_TestExpectedNetProtocolVer
     * to match and re-run.
     *
     * As of 2026-11-17 v65 carries exact Grid match offers, native arena
     * data and private creator traffic. v64 added objective scenario state. v61 authenticated the lobby roster;
     * room messages carry initial settings, access and results (v60). The
     * v59 hoverbike and v58 stage epoch invariants remain required. */
    REQUIRE(g_TestLiveNetProtocolVer == g_TestExpectedNetProtocolVer);
}

TEST_CASE("version pin: MPSETUP_VERSION is the version this test was written against",
          "[versions]") {
    /* Same discipline as above. As of 2026-07-30 the live value is 3:
     * setup blocks are large enough for permanent bot-profile catalog IDs,
     * and v0-v2 blocks are migrated on load. */
    REQUIRE(g_TestLiveMpsetupVersion == g_TestExpectedMpsetupVersion);
}

TEST_CASE("version pin: the online connection decoder rejects older and newer protocols",
          "[versions]") {
    const u16 current = static_cast<u16>(g_TestLiveNetProtocolVer);
    const u8 no_slot = 0xff;
    s32 reconnect = -1;
    u8 client = 0;
    REQUIRE(netReconnectConnectDataDecode(current, current, no_slot, &reconnect, &client)
        == NET_RECONNECT_CONNECT_OK);
    REQUIRE(reconnect == 0);
    REQUIRE(client == no_slot);

    const u16 preceding = current - 1;
    const u32 old_reconnect = netReconnectConnectDataEncode(preceding, 1, 1, no_slot);
    REQUIRE(netReconnectConnectDataDecode(preceding, current, no_slot, &reconnect, &client)
        == NET_RECONNECT_CONNECT_PROTOCOL_MISMATCH);
    REQUIRE(netReconnectConnectDataDecode(old_reconnect, current, no_slot, &reconnect, &client)
        == NET_RECONNECT_CONNECT_PROTOCOL_MISMATCH);
    REQUIRE(netReconnectConnectDataDecode(current + 1, current, no_slot, &reconnect, &client)
        == NET_RECONNECT_CONNECT_PROTOCOL_MISMATCH);
}

TEST_CASE("version pin: MPSETUP_VERSION is monotonically growing",
          "[versions]") {
    /* The save loader preserves the v1 -> v2 weapon clamp and now migrates
     * all v0-v2 blocks to v3 bot-profile identity records. New versions must
     * always grow monotonically. */
    REQUIRE(g_TestLiveMpsetupVersion >= 3);
    REQUIRE(g_TestLiveMpsetupVersion > 2);
    REQUIRE(g_TestLiveMpsetupVersion > 1);
    REQUIRE(g_TestLiveMpsetupVersion > 0);
}
