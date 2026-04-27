/*
 * test_menu_stack.cpp -- Menu pool / stack invariants (cohort 2).
 *
 * Drives menupool_pure.{c,h} to assert the structural-dedup contract:
 *   - One instance per type (acquire on active type returns 0, no state mutation).
 *   - Idempotent release (release on free type returns 0, no warning).
 *   - ReleaseAll bulk-frees every active slot.
 *   - Generation counter increments only on fresh acquire.
 *   - Out-of-range type returns -1.
 *
 * Logging channel reserved for menu-stack diagnostics: MENU.STACK.*
 *
 * @SYNC port/src/menupool.c (menupoolAcquire / Release / IsActive / ReleaseAll)
 * @SYNC context/designs/menu-stack-architecture.md §2.2 invariants I1-I5
 */

#include "catch.hpp"

extern "C" {
#include "menupool_pure.h"
}

TEST_CASE("menupool: empty pool has no active slots", "[menupool][stack]")
{
    menupoolPureReset();
    REQUIRE(menupoolPureCountActive() == 0);
    for (int t = MP_TYPE_NONE + 1; t < MP_TYPE_COUNT; t++) {
        REQUIRE(menupoolPureIsActive((mp_type_t)t) == 0);
    }
}

TEST_CASE("menupool: acquire on free slot returns 1, slot becomes active",
          "[menupool][acquire]")
{
    menupoolPureReset();
    REQUIRE(menupoolPureAcquire(MP_TYPE_MAIN_MENU) == 1);
    REQUIRE(menupoolPureIsActive(MP_TYPE_MAIN_MENU) == 1);
    REQUIRE(menupoolPureCountActive() == 1);
}

TEST_CASE("menupool: acquire on already-active slot returns 0, no state change",
          "[menupool][dedup][I2]")
{
    /* Invariant I2 in menu-stack-architecture.md: one instance per type.
     * Structural dedup is enforced by acquire returning 0 with no mutation. */
    menupoolPureReset();
    menupoolPureAcquire(MP_TYPE_ROOM);
    u32 gen0 = menupoolPureGeneration(MP_TYPE_ROOM);

    REQUIRE(menupoolPureAcquire(MP_TYPE_ROOM) == 0);
    REQUIRE(menupoolPureGeneration(MP_TYPE_ROOM) == gen0);  /* unchanged */
    REQUIRE(menupoolPureIsActive(MP_TYPE_ROOM) == 1);
    REQUIRE(menupoolPureCountActive() == 1);
}

TEST_CASE("menupool: release of active slot returns 1, slot becomes free",
          "[menupool][release]")
{
    menupoolPureReset();
    menupoolPureAcquire(MP_TYPE_ROOM);
    REQUIRE(menupoolPureRelease(MP_TYPE_ROOM) == 1);
    REQUIRE(menupoolPureIsActive(MP_TYPE_ROOM) == 0);
    REQUIRE(menupoolPureCountActive() == 0);
}

TEST_CASE("menupool: release of free slot returns 0 (idempotent)",
          "[menupool][release][idempotent]")
{
    /* Force-close paths call menupoolReleaseAll() and individual releases
     * blanket-style without knowing which slots are live. The contract:
     * release on a free slot is silently a no-op. */
    menupoolPureReset();
    REQUIRE(menupoolPureRelease(MP_TYPE_ROOM) == 0);
    REQUIRE(menupoolPureIsActive(MP_TYPE_ROOM) == 0);

    /* And after a real release, a second release is also 0. */
    menupoolPureAcquire(MP_TYPE_ROOM);
    menupoolPureRelease(MP_TYPE_ROOM);
    REQUIRE(menupoolPureRelease(MP_TYPE_ROOM) == 0);
}

TEST_CASE("menupool: out-of-range type returns -1", "[menupool][bounds]")
{
    menupoolPureReset();
    REQUIRE(menupoolPureAcquire(MP_TYPE_NONE) == -1);
    REQUIRE(menupoolPureAcquire((mp_type_t)MP_TYPE_COUNT) == -1);
    REQUIRE(menupoolPureAcquire((mp_type_t)(MP_TYPE_COUNT + 5)) == -1);
    REQUIRE(menupoolPureRelease(MP_TYPE_NONE) == -1);
    REQUIRE(menupoolPureRelease((mp_type_t)MP_TYPE_COUNT) == -1);
    REQUIRE(menupoolPureIsActive(MP_TYPE_NONE) == 0);
    REQUIRE(menupoolPureIsActive((mp_type_t)MP_TYPE_COUNT) == 0);
}

TEST_CASE("menupool: generation increments on each fresh acquire, not duplicates",
          "[menupool][generation]")
{
    menupoolPureReset();
    REQUIRE(menupoolPureGeneration(MP_TYPE_CHEATS) == 0);

    menupoolPureAcquire(MP_TYPE_CHEATS);
    REQUIRE(menupoolPureGeneration(MP_TYPE_CHEATS) == 1);

    /* Duplicate - no increment */
    menupoolPureAcquire(MP_TYPE_CHEATS);
    REQUIRE(menupoolPureGeneration(MP_TYPE_CHEATS) == 1);

    menupoolPureRelease(MP_TYPE_CHEATS);
    REQUIRE(menupoolPureGeneration(MP_TYPE_CHEATS) == 1);  /* release doesn't touch gen */

    menupoolPureAcquire(MP_TYPE_CHEATS);
    REQUIRE(menupoolPureGeneration(MP_TYPE_CHEATS) == 2);  /* fresh acquire */
}

TEST_CASE("menupool: ReleaseAll frees every active slot", "[menupool][release_all]")
{
    menupoolPureReset();
    menupoolPureAcquire(MP_TYPE_MAIN_MENU);
    menupoolPureAcquire(MP_TYPE_ROOM);
    menupoolPureAcquire(MP_TYPE_MP_SETUP);
    menupoolPureAcquire(MP_TYPE_MP_BOT_SETUP);
    REQUIRE(menupoolPureCountActive() == 4);

    menupoolPureReleaseAll();
    REQUIRE(menupoolPureCountActive() == 0);
    REQUIRE(menupoolPureIsActive(MP_TYPE_MAIN_MENU) == 0);
    REQUIRE(menupoolPureIsActive(MP_TYPE_ROOM) == 0);
    REQUIRE(menupoolPureIsActive(MP_TYPE_MP_SETUP) == 0);
    REQUIRE(menupoolPureIsActive(MP_TYPE_MP_BOT_SETUP) == 0);
}

TEST_CASE("menupool: ReleaseAll on empty pool is a no-op", "[menupool][release_all][idempotent]")
{
    menupoolPureReset();
    menupoolPureReleaseAll();
    REQUIRE(menupoolPureCountActive() == 0);
}

TEST_CASE("menupool: independent slots do not interfere", "[menupool][independence]")
{
    menupoolPureReset();
    menupoolPureAcquire(MP_TYPE_MAIN_MENU);
    REQUIRE(menupoolPureAcquire(MP_TYPE_SOLO_MISSION) == 1);  /* sibling type */
    REQUIRE(menupoolPureCountActive() == 2);

    menupoolPureRelease(MP_TYPE_MAIN_MENU);
    REQUIRE(menupoolPureIsActive(MP_TYPE_SOLO_MISSION) == 1);  /* sibling unaffected */
    REQUIRE(menupoolPureCountActive() == 1);
}

TEST_CASE("menupool: cascade close pattern (parent + descendants)",
          "[menupool][cascade]")
{
    /* Stage transitions, match-start, disconnect handlers all call
     * menupoolReleaseAll() which is the cascade-close primitive. The
     * tree-stack design (menu-stack-architecture.md §2) requires that
     * popping the root tears down all descendants atomically. */
    menupoolPureReset();
    menupoolPureAcquire(MP_TYPE_MAIN_MENU);
    menupoolPureAcquire(MP_TYPE_NETWORK);
    menupoolPureAcquire(MP_TYPE_ROOM);
    menupoolPureAcquire(MP_TYPE_MP_SETUP);
    menupoolPureAcquire(MP_TYPE_WARNING_MODAL);
    REQUIRE(menupoolPureCountActive() == 5);

    menupoolPureReleaseAll();
    REQUIRE(menupoolPureCountActive() == 0);
}

TEST_CASE("menupool: re-acquire after release is fresh, not duplicate",
          "[menupool][lifecycle]")
{
    menupoolPureReset();
    REQUIRE(menupoolPureAcquire(MP_TYPE_THEME_EDITOR) == 1);
    REQUIRE(menupoolPureRelease(MP_TYPE_THEME_EDITOR) == 1);
    REQUIRE(menupoolPureAcquire(MP_TYPE_THEME_EDITOR) == 1);  /* fresh, not 0 */
    REQUIRE(menupoolPureGeneration(MP_TYPE_THEME_EDITOR) == 2);
}
