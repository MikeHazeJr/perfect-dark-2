/*
 * tests/test_save_migration.cpp -- v1 -> v2 weapon-cull migration semantics.
 *
 * Source of the migration rule:
 *   src/game/mplayer/mplayer.c, mpsetupfileLoadWad, the
 *   `if (version < 2)` block (around lines 4474-4493 as of 2026-04-26).
 *
 * The real `mpsetupfileLoadWad` has heavy global dependencies
 * (g_MpSetup, g_BotConfigsArray, scenario state, etc.) that make
 * unit-testing it directly impractical. We instead test the migration
 * RULE in isolation, replicated as a pure helper. This catches the
 * regression class "someone changed the clamp threshold or the
 * sentinel value during a future weapon-cull rev."
 *
 * The rule, restated:
 *
 *   Weapons culled in v2: 8 Goldfinger 64 imports, MPWEAPON_PP9I..MPWEAPON_RCP45,
 *   old slots 0x27..0x2E.
 *   MPWEAPON_SHIELD shifted from 0x2F to 0x27.
 *   MPWEAPON_DISABLED shifted from 0x30 to 0x28.
 *
 *   Migration: any saved weapons[] value at or above the old PP9I slot
 *   (0x27) is now either a removed weapon, a moved sentinel, or out of
 *   range post-cull. Clamp to MPWEAPON_DISABLED. Clear the random-filter
 *   mask entirely so the user re-selects rather than inheriting bits
 *   whose meaning shifted under them.
 *
 * If the source rule changes (e.g., further weapon culls in v3), update
 * BOTH the source AND this test so they stay aligned. A static guard at
 * the bottom pins the live loader's version gate around this destructive
 * migration.
 */

#include "catch.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
/* The constants below mirror the post-cull (v2) layout in
 * src/include/mplayer/mpweapons.h (effectively). Direct includes are
 * avoided to keep this TU free of game-types pull. If the source
 * constants change, update here. */
constexpr std::uint8_t kMPWEAPON_DISABLED_V2 = 0x28;
constexpr std::uint8_t kV2_PP9I_OLD_SLOT     = 0x27;  /* and above */

/* Pure migration: take a v1 weapons[] array (length up to NUM_MPWEAPONSLOTS)
 * and apply the v < 2 clamp rule. Returns the number of values that were
 * clamped (for assertion purposes). */
int migrate_v1_to_v2(std::uint8_t *weapons, std::size_t count) {
    int clamped = 0;
    for (std::size_t i = 0; i < count; i++) {
        if (weapons[i] >= kV2_PP9I_OLD_SLOT) {
            weapons[i] = kMPWEAPON_DISABLED_V2;
            clamped++;
        }
    }
    return clamped;
}

/* The random-filter mask in v1 was a 64-bit packed bitmask of which
 * weapons participate in WEAPONSET_RANDOM picks. Post-cull, the
 * meaning of bits 0x27..0x2E shifted; the migration zeros the entire
 * mask so the user re-selects. */
std::uint64_t migrate_random_filter_mask_v1_to_v2(std::uint64_t /*v1_mask*/) {
    return 0ull;
}

std::string read_text_file(const char *path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} /* anon */

TEST_CASE("save migration: weapon values below 0x27 are unchanged",
          "[save][migration]") {
    /* The cull only affects values >= 0x27. Everything below should
     * pass through unchanged. */
    std::vector<std::uint8_t> weapons = {
        0x00, 0x01, 0x05, 0x10, 0x20, 0x25, 0x26
    };
    auto original = weapons;
    int clamped = migrate_v1_to_v2(weapons.data(), weapons.size());
    REQUIRE(clamped == 0);
    REQUIRE(weapons == original);
}

TEST_CASE("save migration: weapon values 0x27..0x2E (PP9I..RCP45) clamp to DISABLED",
          "[save][migration]") {
    /* All 8 Goldfinger 64 weapons that were culled. */
    std::vector<std::uint8_t> weapons = {
        0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E
    };
    int clamped = migrate_v1_to_v2(weapons.data(), weapons.size());
    REQUIRE(clamped == 8);
    for (auto w : weapons) {
        REQUIRE(w == kMPWEAPON_DISABLED_V2);
    }
}

TEST_CASE("save migration: pre-cull SHIELD/DISABLED slots clamp",
          "[save][migration]") {
    /* In v1, SHIELD was at 0x2F and DISABLED at 0x30. In v2 they shift
     * down to 0x27 and 0x28. A v1 save reading these values would
     * resolve to a different (or invalid) post-cull weapon if not
     * migrated. The clamp rule treats both as out-of-range and replaces
     * with v2 DISABLED. */
    std::vector<std::uint8_t> weapons = { 0x2F, 0x30 };
    int clamped = migrate_v1_to_v2(weapons.data(), weapons.size());
    REQUIRE(clamped == 2);
    REQUIRE(weapons[0] == kMPWEAPON_DISABLED_V2);
    REQUIRE(weapons[1] == kMPWEAPON_DISABLED_V2);
}

TEST_CASE("save migration: high-byte garbage values clamp safely",
          "[save][migration]") {
    /* A malformed v1 save might have stored garbage > 0x30. The 7-bit
     * field on the wire caps at 0x7F, so the realistic range is
     * 0x27..0x7F for "unknown post-cull, must clamp". */
    std::vector<std::uint8_t> weapons = {
        0x31, 0x40, 0x55, 0x70, 0x7F
    };
    int clamped = migrate_v1_to_v2(weapons.data(), weapons.size());
    REQUIRE(clamped == 5);
    for (auto w : weapons) {
        REQUIRE(w == kMPWEAPON_DISABLED_V2);
    }
}

TEST_CASE("save migration: mixed-validity array clamps only the affected slots",
          "[save][migration]") {
    /* Real saves have 6 weapon slots; some may be valid (< 0x27) and
     * some may be in the culled range. Only the latter should change. */
    std::vector<std::uint8_t> weapons = {
        0x00, /* nothing */
        0x05, /* Falcon 2 */
        0x27, /* PP9I (culled) */
        0x10, /* CMP150 */
        0x2E, /* RCP45 (culled) */
        0x26, /* slipayer (just below cull threshold) */
    };
    int clamped = migrate_v1_to_v2(weapons.data(), weapons.size());
    REQUIRE(clamped == 2);
    REQUIRE(weapons[0] == 0x00);
    REQUIRE(weapons[1] == 0x05);
    REQUIRE(weapons[2] == kMPWEAPON_DISABLED_V2);
    REQUIRE(weapons[3] == 0x10);
    REQUIRE(weapons[4] == kMPWEAPON_DISABLED_V2);
    REQUIRE(weapons[5] == 0x26);
}

TEST_CASE("save migration: random-filter mask is zeroed (zero-mask filter behavior)",
          "[save][migration]") {
    /* Per the source comment: "Clear the random filter mask entirely so
     * the user re-selects rather than inheriting bits whose meaning
     * shifted under them." */
    REQUIRE(migrate_random_filter_mask_v1_to_v2(0xFFFFFFFFFFFFFFFFull) == 0ull);
    REQUIRE(migrate_random_filter_mask_v1_to_v2(0x0000000000000001ull) == 0ull);
    REQUIRE(migrate_random_filter_mask_v1_to_v2(0xDEADBEEFCAFEBABEull) == 0ull);
    REQUIRE(migrate_random_filter_mask_v1_to_v2(0ull) == 0ull);
}

TEST_CASE("save migration: idempotent (v2 input passes through unchanged)",
          "[save][migration]") {
    /* Applying the migration to a v2 weapons[] array should produce
     * the same array. v2 SHIELD = 0x27 collides with the old PP9I
     * threshold; v2 DISABLED = 0x28 collides with old PP7. So a v2
     * save WOULD be misinterpreted by the migration rule if the
     * loader incorrectly applied it. The version check guards
     * against that — but the rule itself, applied to v2 data,
     * IS destructive. This test pins that fact, so a future bug
     * "always run migration regardless of version" is loud. */
    std::vector<std::uint8_t> v2_weapons = {
        0x00, 0x05, 0x10, 0x20, 0x27 /* v2 SHIELD */, 0x28 /* v2 DISABLED */
    };
    auto with_migration = v2_weapons;
    int clamped = migrate_v1_to_v2(with_migration.data(), with_migration.size());
    /* Two values (SHIELD at 0x27 and DISABLED at 0x28) are >= 0x27 and
     * would be clamped if the migration ran on v2 data. This is the
     * expected MISBEHAVIOR — the loader must NOT call the migration
     * when version >= 2. */
    REQUIRE(clamped == 2);
    /* Pin the misbehavior: SHIELD slot becomes DISABLED. */
    REQUIRE(with_migration[4] == kMPWEAPON_DISABLED_V2);
    REQUIRE(with_migration[5] == kMPWEAPON_DISABLED_V2);
}

TEST_CASE("save migration: clamp sentinel matches MPWEAPON_DISABLED_V2",
          "[save][migration]") {
    /* The sentinel value MUST be the post-cull MPWEAPON_DISABLED, not
     * some other "invalid" marker. If a future change uses 0xFF or 0
     * the loaded save will silently change behavior (0 = "Nothing"
     * weapon = different gameplay). */
    std::uint8_t one = 0x27;
    migrate_v1_to_v2(&one, 1);
    REQUIRE(one == 0x28);
    REQUIRE(one != 0x00);
    REQUIRE(one != 0xFF);
}

TEST_CASE("save migration: production weapon-cull migration remains version-gated",
          "[save][migration][static]") {
    const std::string source = read_text_file("src/game/mplayer/mplayer.c");
    const std::size_t unpack = source.find("unpackWeaponSetRandomFilters(wpnRndPacked)");
    const std::size_t gate = source.find("if (version < 2)");
    const std::size_t next_field = source.find("g_MpWeaponSetNum = savebufferReadBits", gate);

    REQUIRE(unpack != std::string::npos);
    REQUIRE(gate != std::string::npos);
    REQUIRE(next_field != std::string::npos);
    REQUIRE(unpack < gate);

    const std::string block = source.substr(gate, next_field - gate);
    REQUIRE(block.find("g_MpSetup.weapons[i] >= 0x27") != std::string::npos);
    REQUIRE(block.find("g_MpSetup.weapons[i] = MPWEAPON_DISABLED") != std::string::npos);
    REQUIRE(block.find("g_MpWeaponSetRandomFilters[i] = 0") != std::string::npos);
}
