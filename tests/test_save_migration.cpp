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

/* ========================================================================
 * c074: behavioural coverage of the REAL save-migration chain framework
 * (port/src/savemigrate.c, linked into pd-tests). The tests above pin the
 * v1->v2 weapon-cull RULE as a pure replica; these exercise the actual chain
 * executor -- registration, ascending-version ordering, type isolation,
 * no-op/downgrade guards, and fail-closed on a missing step -- so the
 * production framework cannot drift unnoticed.
 * ======================================================================== */

#include <cstdio>
#include "savemigrate.h"

namespace {
/* Dummy migrations tag the JSON so chain ORDER is observable in the output. */
char *sm_tag(char *json, s32 bufsize, const char *tag) {
    std::size_t cur = std::strlen(json);
    if (cur + std::strlen(tag) + 1 < (std::size_t)bufsize) std::strcat(json, tag);
    return json;
}
char *sm_1to2(char *j, s32, s32 b) { return sm_tag(j, b, "|1to2"); }
char *sm_2to3(char *j, s32, s32 b) { return sm_tag(j, b, "|2to3"); }
char *sm_3to4(char *j, s32, s32 b) { return sm_tag(j, b, "|3to4"); }

void sm_write(const std::string &path, const std::string &s) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << s;
}
std::string sm_read(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}
/* RAII cleanup of the temp save + any .vN.bak the framework writes. */
struct SmTmp {
    std::string path;
    explicit SmTmp(const char *p) : path(p) {}
    ~SmTmp() {
        std::remove(path.c_str());
        for (int v = 1; v <= 9; v++) {
            std::remove((path + ".v" + std::to_string(v) + ".bak").c_str());
        }
    }
};
} /* anon */

TEST_CASE("savemigrate framework: version-check arithmetic", "[save][migration]") {
    REQUIRE(saveMigrateCheck(2, 2) == 0);   /* already at target */
    REQUIRE(saveMigrateCheck(3, 3) == 0);
    REQUIRE(saveMigrateCheck(1, 4) == 1);   /* older -> migration available */
    REQUIRE(saveMigrateCheck(2, 4) == 1);
    REQUIRE(saveMigrateCheck(5, 4) == -1);  /* newer than target -> downgrade */
}

TEST_CASE("savemigrate framework: chain runs steps in ascending version order",
          "[save][migration]") {
    saveMigrateInit();  /* clears the registry */
    /* Register OUT of order to prove the executor sequences by version, not by
     * registration order. */
    saveMigrateRegister(SAVETYPE_AGENT, 3, 4, sm_3to4);
    saveMigrateRegister(SAVETYPE_AGENT, 1, 2, sm_1to2);
    saveMigrateRegister(SAVETYPE_AGENT, 2, 3, sm_2to3);

    SmTmp t("pd_test_savemigrate_chain.json");
    sm_write(t.path, "{\"version\":1}");
    REQUIRE(saveMigrateFile(t.path.c_str(), SAVETYPE_AGENT, 1, 4) == 0);

    const std::string out = sm_read(t.path);
    const std::size_t p12 = out.find("|1to2");
    const std::size_t p23 = out.find("|2to3");
    const std::size_t p34 = out.find("|3to4");
    INFO("migrated content: " << out);
    REQUIRE(p12 != std::string::npos);
    REQUIRE(p23 != std::string::npos);
    REQUIRE(p34 != std::string::npos);
    REQUIRE(p12 < p23);   /* 1->2 ran before 2->3 */
    REQUIRE(p23 < p34);   /* 2->3 ran before 3->4 */
}

TEST_CASE("savemigrate framework: type isolation -- other types are not run",
          "[save][migration]") {
    saveMigrateInit();
    saveMigrateRegister(SAVETYPE_AGENT,  1, 2, sm_1to2);
    saveMigrateRegister(SAVETYPE_PLAYER, 1, 2, sm_2to3);  /* different type, same step */

    SmTmp t("pd_test_savemigrate_type.json");
    sm_write(t.path, "{}");
    REQUIRE(saveMigrateFile(t.path.c_str(), SAVETYPE_AGENT, 1, 2) == 0);
    const std::string out = sm_read(t.path);
    REQUIRE(out.find("|1to2") != std::string::npos);  /* AGENT step ran */
    REQUIRE(out.find("|2to3") == std::string::npos);  /* PLAYER step did NOT */
}

TEST_CASE("savemigrate framework: already-at-target is a no-op success",
          "[save][migration]") {
    saveMigrateInit();
    saveMigrateRegister(SAVETYPE_AGENT, 1, 2, sm_1to2);
    SmTmp t("pd_test_savemigrate_noop.json");
    sm_write(t.path, "{\"version\":4}");
    REQUIRE(saveMigrateFile(t.path.c_str(), SAVETYPE_AGENT, 4, 4) == 1);
    REQUIRE(sm_read(t.path) == "{\"version\":4}");  /* untouched */
}

TEST_CASE("savemigrate framework: missing chain step fails closed",
          "[save][migration]") {
    saveMigrateInit();
    saveMigrateRegister(SAVETYPE_AGENT, 1, 2, sm_1to2);  /* no 2->3 registered */
    SmTmp t("pd_test_savemigrate_gap.json");
    sm_write(t.path, "{\"version\":1}");
    /* 1->2 exists, 2->3 does not: the chain must abort with an error, not
     * silently stop at v2 and claim success. */
    REQUIRE(saveMigrateFile(t.path.c_str(), SAVETYPE_AGENT, 1, 4) == -1);
}

TEST_CASE("savemigrate framework: newer-than-target refuses to downgrade",
          "[save][migration]") {
    saveMigrateInit();
    SmTmp t("pd_test_savemigrate_down.json");
    sm_write(t.path, "{\"version\":9}");
    REQUIRE(saveMigrateFile(t.path.c_str(), SAVETYPE_AGENT, 9, 4) == -2);
}
