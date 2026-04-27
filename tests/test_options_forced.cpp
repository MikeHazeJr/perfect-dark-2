/*
 * tests/test_options_forced.cpp -- INV-4 / Cohort D: pure helpers for
 * the options + options_engine_forced bit math.
 *
 * Source under test:
 *   port/src/options_forced.c::matchOptionsUserView
 *   port/src/options_forced.c::matchOptionsEngineView
 *   port/src/options_forced.c::matchOptionsRestoreUserOriginal
 *   port/src/options_forced.c::matchOptionsForceBit
 *
 * The bug shape, restated for the regression record. The B-181
 * fallback in src/game/setup.c historically OR'd
 * MPOPTION_SPAWNWITHWEAPON onto g_MatchConfig.options + g_MpSetup.options
 * with no record that the engine had forced the bit. If the user had
 * explicitly disabled spawn-with-weapon via the menu, the engine
 * silently re-enabled it, and the override persisted across matches
 * because g_MatchConfig is rarely reset.
 *
 * INV-4 / Cohort D adds a parallel options_engine_forced u32 field.
 * matchOptionsForceBit records a bit in the forced-mask AT THE SAME
 * TIME it sets the bit in options. matchOptionsRestoreUserOriginal
 * (called from matchStart) clears all forced bits from options and
 * resets the forced-mask, restoring the user's original choices for
 * the next match. The pure helpers exercised here are the algebra;
 * the call sites (setup.c B-181 + matchsetup.c matchStart) consume
 * them.
 */

#include "catch.hpp"

extern "C" {
#include "options_forced.h"
}

/* Pinned MPOPTION_* values used in tests. Keep in sync with constants.h. */
#define TEST_OPT_SPAWNWITHWEAPON  0x00000001u
#define TEST_OPT_TEAMSENABLED     0x00000002u
#define TEST_OPT_ONEHITKILLS      0x00000004u

TEST_CASE("matchOptionsUserView: forced bit subtracted, others preserved",
          "[options][forced][regression]") {
	/* User-original picks user_view = options & ~forced. The forced
	 * mask records what the engine added; subtracting it recovers the
	 * user's original choice. */
	REQUIRE(matchOptionsUserView(TEST_OPT_SPAWNWITHWEAPON, TEST_OPT_SPAWNWITHWEAPON) == 0u);
	REQUIRE(matchOptionsUserView(TEST_OPT_TEAMSENABLED | TEST_OPT_SPAWNWITHWEAPON,
	                             TEST_OPT_SPAWNWITHWEAPON) == TEST_OPT_TEAMSENABLED);
	REQUIRE(matchOptionsUserView(0xFFFFFFFFu, TEST_OPT_SPAWNWITHWEAPON)
	        == (0xFFFFFFFFu & ~TEST_OPT_SPAWNWITHWEAPON));
}

TEST_CASE("matchOptionsUserView: empty forced-mask returns options unchanged",
          "[options][forced][regression]") {
	REQUIRE(matchOptionsUserView(TEST_OPT_TEAMSENABLED, 0u) == TEST_OPT_TEAMSENABLED);
	REQUIRE(matchOptionsUserView(0xFFFFFFFFu, 0u) == 0xFFFFFFFFu);
	REQUIRE(matchOptionsUserView(0u, 0u) == 0u);
}

TEST_CASE("matchOptionsEngineView: forced bits OR'd into options",
          "[options][forced][regression]") {
	/* Engine view applies forced bits even if not in options yet
	 * (deferred-apply support; today the B-181 path applies at the
	 * same time it marks). */
	REQUIRE(matchOptionsEngineView(0u, TEST_OPT_SPAWNWITHWEAPON)
	        == TEST_OPT_SPAWNWITHWEAPON);
	REQUIRE(matchOptionsEngineView(TEST_OPT_TEAMSENABLED, TEST_OPT_SPAWNWITHWEAPON)
	        == (TEST_OPT_TEAMSENABLED | TEST_OPT_SPAWNWITHWEAPON));
	/* Bit already in options: idempotent. */
	REQUIRE(matchOptionsEngineView(TEST_OPT_SPAWNWITHWEAPON, TEST_OPT_SPAWNWITHWEAPON)
	        == TEST_OPT_SPAWNWITHWEAPON);
}

TEST_CASE("matchOptionsRestoreUserOriginal: clears forced bits + resets mask",
          "[options][forced][regression]") {
	u32 options = TEST_OPT_TEAMSENABLED | TEST_OPT_SPAWNWITHWEAPON;
	u32 forced  = TEST_OPT_SPAWNWITHWEAPON;

	matchOptionsRestoreUserOriginal(&options, &forced);

	REQUIRE(options == TEST_OPT_TEAMSENABLED);
	REQUIRE(forced  == 0u);
}

TEST_CASE("matchOptionsRestoreUserOriginal: idempotent on empty forced-mask",
          "[options][forced][regression]") {
	u32 options = TEST_OPT_TEAMSENABLED | TEST_OPT_ONEHITKILLS;
	u32 forced  = 0u;

	matchOptionsRestoreUserOriginal(&options, &forced);

	REQUIRE(options == (TEST_OPT_TEAMSENABLED | TEST_OPT_ONEHITKILLS));
	REQUIRE(forced  == 0u);
}

TEST_CASE("matchOptionsRestoreUserOriginal: NULL pointers are safe no-ops",
          "[options][forced][regression]") {
	u32 options = TEST_OPT_SPAWNWITHWEAPON;
	u32 forced  = TEST_OPT_SPAWNWITHWEAPON;

	/* Defensive: passing NULL must not crash. */
	matchOptionsRestoreUserOriginal(NULL, &forced);
	REQUIRE(forced == TEST_OPT_SPAWNWITHWEAPON);

	matchOptionsRestoreUserOriginal(&options, NULL);
	REQUIRE(options == TEST_OPT_SPAWNWITHWEAPON);
}

TEST_CASE("matchOptionsForceBit: marks AND applies atomically",
          "[options][forced][regression]") {
	u32 options = 0u;
	u32 forced  = 0u;

	matchOptionsForceBit(&options, &forced, TEST_OPT_SPAWNWITHWEAPON);

	REQUIRE(options == TEST_OPT_SPAWNWITHWEAPON);
	REQUIRE(forced  == TEST_OPT_SPAWNWITHWEAPON);
}

TEST_CASE("matchOptionsForceBit: preserves prior options + accumulates forced bits",
          "[options][forced][regression]") {
	u32 options = TEST_OPT_TEAMSENABLED;
	u32 forced  = 0u;

	matchOptionsForceBit(&options, &forced, TEST_OPT_SPAWNWITHWEAPON);

	REQUIRE(options == (TEST_OPT_TEAMSENABLED | TEST_OPT_SPAWNWITHWEAPON));
	REQUIRE(forced  == TEST_OPT_SPAWNWITHWEAPON);

	/* Force a second bit: both forced bits accumulate. */
	matchOptionsForceBit(&options, &forced, TEST_OPT_ONEHITKILLS);

	REQUIRE(options == (TEST_OPT_TEAMSENABLED | TEST_OPT_SPAWNWITHWEAPON | TEST_OPT_ONEHITKILLS));
	REQUIRE(forced  == (TEST_OPT_SPAWNWITHWEAPON | TEST_OPT_ONEHITKILLS));
}

TEST_CASE("matchOptionsForceBit + Restore: full B-181 -> matchStart cycle",
          "[options][forced][regression]") {
	/* Walk the realistic lifecycle the B-181 fallback creates:
	 *   1. User opens menu, sets only TEAMSENABLED.
	 *   2. Stage loads, B-181 fallback fires force-setting SPAWNWITHWEAPON.
	 *   3. Match plays out.
	 *   4. matchStart for next match clears forced bits.
	 *   5. User-original options (just TEAMSENABLED) restored.
	 * Pin this whole cycle in one case so any future regression that
	 * leaves engine-forced bits stuck across matches is caught. */
	u32 options = TEST_OPT_TEAMSENABLED;  /* user pick */
	u32 forced  = 0u;

	/* Step 2: B-181 force-enables SPAWNWITHWEAPON. */
	matchOptionsForceBit(&options, &forced, TEST_OPT_SPAWNWITHWEAPON);
	REQUIRE(options == (TEST_OPT_TEAMSENABLED | TEST_OPT_SPAWNWITHWEAPON));
	REQUIRE(forced  == TEST_OPT_SPAWNWITHWEAPON);
	/* During the match, the engine reads the union (it has the bit). */
	REQUIRE(matchOptionsEngineView(options, forced) == options);
	/* User-facing display still shows just the user pick. */
	REQUIRE(matchOptionsUserView(options, forced) == TEST_OPT_TEAMSENABLED);

	/* Step 4: matchStart restore. */
	matchOptionsRestoreUserOriginal(&options, &forced);

	/* Step 5: user-original recovered, forced-mask empty. */
	REQUIRE(options == TEST_OPT_TEAMSENABLED);
	REQUIRE(forced  == 0u);
}

TEST_CASE("matchOptionsForceBit: idempotent when bit already forced",
          "[options][forced][regression]") {
	u32 options = TEST_OPT_SPAWNWITHWEAPON;
	u32 forced  = TEST_OPT_SPAWNWITHWEAPON;

	matchOptionsForceBit(&options, &forced, TEST_OPT_SPAWNWITHWEAPON);

	REQUIRE(options == TEST_OPT_SPAWNWITHWEAPON);
	REQUIRE(forced  == TEST_OPT_SPAWNWITHWEAPON);
}
