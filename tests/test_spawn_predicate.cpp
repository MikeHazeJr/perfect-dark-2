/*
 * tests/test_spawn_predicate.cpp -- INV-2 (player-init-architectural-fixes
 * 2026-04-26): single-source-of-truth predicate for the spawn-with-weapon
 * vs INTROCMD_WEAPON mutual-exclusion gate.
 *
 * Source under test:
 *   port/src/spawn_predicate.c::spawnWithWeaponShouldApply
 *
 * The bug shape, restated for the regression record. Pre-INV-2, three
 * sites observed the same precondition with different predicates:
 *
 *   playerreset.c:225 -- (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON))
 *   player.c:1790    -- (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)  [normmplayer guard dropped]
 *   bot.c:506        -- (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)  [normmplayer guard dropped]
 *
 * In Co-Op or Counter-Op Bond with MPOPTION_SPAWNWITHWEAPON set in
 * g_MpSetup.options (the default per matchsetup.c:101 + matchsetup.c:703
 * propagation), the asymmetric guards meant playerreset.c kept INTROCMD_WEAPON
 * AND player.c / bot.c also fired spawn-with-weapon, dual-adding inventory.
 * The final equip is the spawn weapon (it overrides), but the inventory
 * state holds both -- a stale-state class.
 *
 * INV-2 makes all four sites consult the same predicate. The tests below
 * pin the predicate's truth table so any future change applies to all
 * consumers simultaneously.
 *
 * Decision: D-1 selected Option A (re-add the normmplayer guard
 * symmetrically) over Options B/C. Smallest behavioral footprint.
 */

#include "catch.hpp"

extern "C" {
#include "spawn_predicate.h"
}

/* MPOPTION_SPAWNWITHWEAPON value pinned from src/include/constants.h.
 * Matches the value used by callers in src/game/. If the constant ever
 * moves, update this pin alongside it (and add a static_assert in
 * src/game/playerreset.c that the values still align). */
#define TEST_SPAWN_WITH_WEAPON_BIT  0x00000001u

TEST_CASE("spawnWithWeaponShouldApply: normal MP + bit set -> true",
          "[spawn][predicate][regression]") {
	REQUIRE(spawnWithWeaponShouldApply(1, TEST_SPAWN_WITH_WEAPON_BIT, TEST_SPAWN_WITH_WEAPON_BIT) == true);
	/* Other unrelated bits also set: still true. */
	REQUIRE(spawnWithWeaponShouldApply(1, 0xFFFFFFFFu, TEST_SPAWN_WITH_WEAPON_BIT) == true);
}

TEST_CASE("spawnWithWeaponShouldApply: normal MP + bit clear -> false",
          "[spawn][predicate][regression]") {
	REQUIRE(spawnWithWeaponShouldApply(1, 0u, TEST_SPAWN_WITH_WEAPON_BIT) == false);
	/* Other bits set but not SPAWNWITHWEAPON. */
	REQUIRE(spawnWithWeaponShouldApply(1, ~TEST_SPAWN_WITH_WEAPON_BIT, TEST_SPAWN_WITH_WEAPON_BIT) == false);
}

TEST_CASE("spawnWithWeaponShouldApply: not normal MP + bit set -> false",
          "[spawn][predicate][regression]") {
	/* Co-Op or Counter-Op (g_Vars.normmplayerisrunning == false). The
	 * MPOPTION_SPAWNWITHWEAPON bit may be set in g_MpSetup.options because
	 * it propagates from g_MatchConfig.options (default ON, see
	 * matchsetup.c:101) but spawn-with-weapon must NOT fire here -- the
	 * mission's INTROCMD_WEAPON owns the loadout. */
	REQUIRE(spawnWithWeaponShouldApply(0, TEST_SPAWN_WITH_WEAPON_BIT, TEST_SPAWN_WITH_WEAPON_BIT) == false);
	REQUIRE(spawnWithWeaponShouldApply(0, 0xFFFFFFFFu, TEST_SPAWN_WITH_WEAPON_BIT) == false);
}

TEST_CASE("spawnWithWeaponShouldApply: not normal MP + bit clear -> false",
          "[spawn][predicate][regression]") {
	REQUIRE(spawnWithWeaponShouldApply(0, 0u, TEST_SPAWN_WITH_WEAPON_BIT) == false);
}

TEST_CASE("spawnWithWeaponShouldApply: bit position parameterized",
          "[spawn][predicate][regression]") {
	/* Predicate accepts the bit as a parameter so callers can pass the
	 * project's MPOPTION_SPAWNWITHWEAPON constant directly without the
	 * predicate TU needing to know what value the constant holds.
	 * Future renumbering of the option flags is a one-call-site change
	 * at the call site, not in the predicate. */
	REQUIRE(spawnWithWeaponShouldApply(1, 0x00000004u, 0x00000004u) == true);
	REQUIRE(spawnWithWeaponShouldApply(1, 0x80000000u, 0x80000000u) == true);
	REQUIRE(spawnWithWeaponShouldApply(1, 0x00000004u, 0x00000008u) == false);
}

TEST_CASE("spawnWithWeaponShouldApply: mutual exclusion contract",
          "[spawn][predicate][regression]") {
	/* The contract: predicate=true means BOTH (a) playerreset.c skips
	 * INTROCMD_WEAPON AND (b) player.c / bot.c fire spawn-with-weapon.
	 * predicate=false means BOTH (a) INTROCMD_WEAPON runs AND (b)
	 * spawn-with-weapon does NOT fire. Independent decisions break the
	 * mutual exclusion and reintroduce the F-1 / F-5 / F-8 dual-add bug.
	 *
	 * This test does not directly test mutual exclusion (which lives in
	 * the call sites); it pins the predicate's truth table that the call
	 * sites consume so any future change to the predicate is caught. */
	const u32 bit = TEST_SPAWN_WITH_WEAPON_BIT;

	/* Truth table:
	 *   normmp | bit | result
	 *   F      | F   | F
	 *   F      | T   | F
	 *   T      | F   | F
	 *   T      | T   | T
	 */
	REQUIRE(spawnWithWeaponShouldApply(0, 0u,  bit) == false);
	REQUIRE(spawnWithWeaponShouldApply(0, bit, bit) == false);
	REQUIRE(spawnWithWeaponShouldApply(1,  0u,  bit) == false);
	REQUIRE(spawnWithWeaponShouldApply(1,  bit, bit) == true);
}
