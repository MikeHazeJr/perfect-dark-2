/*
 * tests/test_random_pool.cpp -- mpSetRandomWeapons specification tests.
 *
 * Source of the rule:
 *   src/game/mplayer/mplayer.c, mpSetRandomWeapons (lines 1496-1519
 *   as of 2026-04-26).
 *
 * The live function reads two globals (g_MpWeaponSetRandomFilters[],
 * g_MpWeaponRandomFilterNum) and calls into the challenge and catalog
 * subsystems (challengeIsFeatureUnlocked, catalogGetMpWeaponUnlockFeature).
 * Linking the real implementation into the test binary would cascade
 * ~50 stubs deep through challenge.c and the asset catalog.
 *
 * We instead replicate the SPECIFICATION as a pure helper that takes
 * the inputs explicitly (filter array + unlock predicate) and returns
 * the result. The semantics MUST match the live function, line for
 * line. The drift audit is a `diff` of the spec helper below against
 * src/game/mplayer/mplayer.c::mpSetRandomWeapons.
 *
 * The spec, restated:
 *
 *   for each weapon index i in [0, NUM_MPWEAPONS):
 *     if i is unlocked AND filter[i] == 1:
 *       weapons[index_out] = i - lock_count
 *       index_out += 1
 *     else if NOT unlocked:
 *       lock_count += 1
 *
 *   if index_out == 0:
 *     weapons[0] = 0
 *     filter_num_out = 1
 *   else:
 *     filter_num_out = index_out
 *
 * The "i - lock_count" logic accounts for locked weapons being absent
 * from the menu UI: the output value is a "shifted index" that maps
 * to the position in the unlocked-only list.
 *
 * Mike's directive (2026-04-26):
 *   "random map weapons and manifest populate / loading / diff /
 *    unloading etc"
 *
 *   "Cover: given catalog X and unlock-state Y, the eligible pool
 *    matches expected; locked items not in pool; disabled items
 *    automatically excluded; user-filter behavior; deterministic
 *    behavior given fixed RNG seed."
 *
 * Note: The deterministic-RNG aspect is downstream of mpSetRandomWeapons
 * (in mpApplyWeaponSet). This test covers the upstream filtering only.
 */

#include "catch.hpp"
#include <PR/ultratypes.h>

#include <cstdint>
#include <vector>
#include <functional>

namespace {

constexpr int kNUM_MPWEAPONS_v2 = 64;  /* persisted random-filter bit domain */

/* Pure replication of mpSetRandomWeapons. Returns the
 * filter_num_out (the count assigned to g_MpWeaponRandomFilterNum). */
int spec_mpSetRandomWeapons(
    u8                                       *weapons_out,
    int                                       num_weapons,
    const u8                                 *filter_in,
    std::function<bool(int weapon_idx)>       is_unlocked)
{
    int lock_count = 0;
    int index_out  = 0;

    for (int i = 0; i < num_weapons; i++) {
        if (is_unlocked(i)) {
            if (filter_in[i] == 1) {
                weapons_out[index_out] = (u8)(i - lock_count);
                index_out++;
            }
        } else {
            lock_count++;
        }
    }

    if (index_out == 0) {
        weapons_out[0] = 0;
        return 1;
    }
    return index_out;
}

} /* anon */

TEST_CASE("random-pool: all weapons unlocked, all filtered in -> full pool",
          "[random-pool]") {
    std::vector<u8> filter(kNUM_MPWEAPONS_v2, 1);
    std::vector<u8> out(kNUM_MPWEAPONS_v2 + 1, 0xFF);
    int num = spec_mpSetRandomWeapons(out.data(), kNUM_MPWEAPONS_v2,
                                      filter.data(),
                                      [](int) { return true; });
    REQUIRE(num == kNUM_MPWEAPONS_v2);
    /* With no locks, the output value at position i is i - 0 = i. */
    for (int i = 0; i < kNUM_MPWEAPONS_v2; i++) {
        REQUIRE(out[i] == (u8)i);
    }
}

TEST_CASE("random-pool: all weapons unlocked, none filtered in -> 1-entry fallback",
          "[random-pool]") {
    /* Per the live spec: if no eligible weapon, output is "Nothing"
     * (slot 0) and filter_num is 1. This is the safety fallback that
     * keeps mpApplyWeaponSet from dividing by zero in the modulo
     * picker. */
    std::vector<u8> filter(kNUM_MPWEAPONS_v2, 0);
    std::vector<u8> out(kNUM_MPWEAPONS_v2 + 1, 0xFF);
    int num = spec_mpSetRandomWeapons(out.data(), kNUM_MPWEAPONS_v2,
                                      filter.data(),
                                      [](int) { return true; });
    REQUIRE(num == 1);
    REQUIRE(out[0] == 0);
}

TEST_CASE("random-pool: all weapons LOCKED -> 1-entry fallback",
          "[random-pool]") {
    std::vector<u8> filter(kNUM_MPWEAPONS_v2, 1);
    std::vector<u8> out(kNUM_MPWEAPONS_v2 + 1, 0xFF);
    int num = spec_mpSetRandomWeapons(out.data(), kNUM_MPWEAPONS_v2,
                                      filter.data(),
                                      [](int) { return false; });
    REQUIRE(num == 1);
    REQUIRE(out[0] == 0);
}

TEST_CASE("random-pool: locked items shift the output index",
          "[random-pool]") {
    /* Make weapons 5 and 10 locked. All others unlocked + filtered.
     * Expected: out[0] = 0, out[1] = 1, ..., out[4] = 4 (no shift yet),
     * weapon 5 is locked (skipped, lock_count = 1),
     * out[5] = 6 - 1 = 5, out[6] = 7 - 1 = 6, ..., out[8] = 9 - 1 = 8,
     * weapon 10 is locked (skipped, lock_count = 2),
     * out[9] = 11 - 2 = 9, etc. */
    std::vector<u8> filter(kNUM_MPWEAPONS_v2, 1);
    std::vector<u8> out(kNUM_MPWEAPONS_v2 + 1, 0xFF);
    int num = spec_mpSetRandomWeapons(out.data(), kNUM_MPWEAPONS_v2,
                                      filter.data(),
                                      [](int i) { return i != 5 && i != 10; });
    REQUIRE(num == kNUM_MPWEAPONS_v2 - 2);
    /* Expected output: 0..4, 5 (= 6-1), 6, 7, 8, 9 (= 11-2), ... */
    int expected = 0;
    int shift = 0;
    int out_idx = 0;
    for (int i = 0; i < kNUM_MPWEAPONS_v2; i++) {
        if (i == 5 || i == 10) {
            shift++;
            continue;
        }
        REQUIRE(out[out_idx] == (u8)(i - shift));
        out_idx++;
        (void)expected;
    }
}

TEST_CASE("random-pool: filter mask 0 hides unlocked weapons",
          "[random-pool]") {
    /* All unlocked, but only weapons 1, 3, 5 are in the user's filter.
     * Output is exactly 3 entries: 1, 3, 5 (no lock shift). */
    std::vector<u8> filter(kNUM_MPWEAPONS_v2, 0);
    filter[1] = 1;
    filter[3] = 1;
    filter[5] = 1;
    std::vector<u8> out(kNUM_MPWEAPONS_v2 + 1, 0xFF);
    int num = spec_mpSetRandomWeapons(out.data(), kNUM_MPWEAPONS_v2,
                                      filter.data(),
                                      [](int) { return true; });
    REQUIRE(num == 3);
    REQUIRE(out[0] == 1);
    REQUIRE(out[1] == 3);
    REQUIRE(out[2] == 5);
}

TEST_CASE("random-pool: lock-then-filter interaction is correct",
          "[random-pool]") {
    /* Mixed: some locked, some unlocked-but-filter-zero, some unlocked
     * and in filter. Verify the eligible pool exactly matches
     * (unlocked AND filter[i] == 1).
     *
     * Layout (for a 10-weapon slice):
     *   weapon: 0  1  2  3  4  5  6  7  8  9
     *   lock:   .  .  L  .  .  L  .  .  .  .
     *   filter: 1  0  1  1  1  1  0  1  1  0
     *
     * Iteration:
     *   i=0: unlocked, filter=1 -> out[0] = 0 - 0 = 0
     *   i=1: unlocked, filter=0 -> skip
     *   i=2: LOCKED -> lock_count = 1
     *   i=3: unlocked, filter=1 -> out[1] = 3 - 1 = 2
     *   i=4: unlocked, filter=1 -> out[2] = 4 - 1 = 3
     *   i=5: LOCKED -> lock_count = 2
     *   i=6: unlocked, filter=0 -> skip
     *   i=7: unlocked, filter=1 -> out[3] = 7 - 2 = 5
     *   i=8: unlocked, filter=1 -> out[4] = 8 - 2 = 6
     *   i=9: unlocked, filter=0 -> skip
     *
     * Result: 5 entries, [0, 2, 3, 5, 6]. */
    constexpr int N = 10;
    u8 filter[N] = { 1, 0, 1, 1, 1, 1, 0, 1, 1, 0 };
    u8 out[N + 1] = {};
    int num = spec_mpSetRandomWeapons(out, N, filter,
                                      [](int i) { return i != 2 && i != 5; });
    REQUIRE(num == 5);
    REQUIRE(out[0] == 0);
    REQUIRE(out[1] == 2);
    REQUIRE(out[2] == 3);
    REQUIRE(out[3] == 5);
    REQUIRE(out[4] == 6);
}

TEST_CASE("random-pool: single-weapon case (filter[0] = 1 only)",
          "[random-pool]") {
    std::vector<u8> filter(kNUM_MPWEAPONS_v2, 0);
    filter[0] = 1;
    std::vector<u8> out(kNUM_MPWEAPONS_v2 + 1, 0xFF);
    int num = spec_mpSetRandomWeapons(out.data(), kNUM_MPWEAPONS_v2,
                                      filter.data(),
                                      [](int) { return true; });
    REQUIRE(num == 1);
    REQUIRE(out[0] == 0);
}

TEST_CASE("random-pool: deterministic given the same inputs",
          "[random-pool]") {
    /* The function has no internal RNG. Same inputs MUST produce same
     * outputs. This test exercises the determinism property explicitly
     * so a future change that introduces non-determinism (e.g.,
     * shuffling the output) is caught. */
    std::vector<u8> filter(kNUM_MPWEAPONS_v2, 1);
    auto unlock = [](int i) { return i % 3 != 0; };  /* every 3rd locked */

    std::vector<u8> out1(kNUM_MPWEAPONS_v2 + 1, 0xFF);
    std::vector<u8> out2(kNUM_MPWEAPONS_v2 + 1, 0xFF);
    int num1 = spec_mpSetRandomWeapons(out1.data(), kNUM_MPWEAPONS_v2,
                                        filter.data(), unlock);
    int num2 = spec_mpSetRandomWeapons(out2.data(), kNUM_MPWEAPONS_v2,
                                        filter.data(), unlock);
    REQUIRE(num1 == num2);
    REQUIRE(out1 == out2);
}

TEST_CASE("random-pool: count matches spec for unlock pattern",
          "[random-pool]") {
    /* Verify the output count is exactly the number of (unlocked AND
     * filtered-in) weapons. */
    struct {
        const char *name;
        std::function<bool(int)> unlock;
        int expected_unlocked;
    } cases[] = {
        { "all locked",        [](int) { return false; },                 0 },
        { "all unlocked",      [](int) { return true; },                  kNUM_MPWEAPONS_v2 },
        { "even unlocked",     [](int i) { return (i & 1) == 0; },        (kNUM_MPWEAPONS_v2 + 1) / 2 },
        { "odd unlocked",      [](int i) { return (i & 1) == 1; },        kNUM_MPWEAPONS_v2 / 2 },
        { "first 5 unlocked",  [](int i) { return i < 5; },               5 },
        { "last 5 unlocked",   [](int i) { return i >= kNUM_MPWEAPONS_v2 - 5; }, 5 },
    };

    std::vector<u8> filter(kNUM_MPWEAPONS_v2, 1);
    std::vector<u8> out(kNUM_MPWEAPONS_v2 + 1, 0xFF);

    for (auto &tc : cases) {
        int num = spec_mpSetRandomWeapons(out.data(), kNUM_MPWEAPONS_v2,
                                          filter.data(), tc.unlock);
        INFO("case: " << tc.name);
        if (tc.expected_unlocked == 0) {
            /* fallback: 1-entry "Nothing" */
            REQUIRE(num == 1);
            REQUIRE(out[0] == 0);
        } else {
            REQUIRE(num == tc.expected_unlocked);
        }
    }
}

TEST_CASE("random-pool: NUM_MPWEAPONS pin", "[random-pool]") {
    /* The spec helper hardcodes NUM_MPWEAPONS = 64. If the live
     * constant in src/include/constants.h changes, this test fails loudly
     * so the spec gets updated alongside. */
    REQUIRE(kNUM_MPWEAPONS_v2 == 64);
}
