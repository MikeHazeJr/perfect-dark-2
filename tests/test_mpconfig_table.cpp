/*
 * tests/test_mpconfig_table.cpp -- the challenge and preset table lands every
 * value in the field it was written for (week plan finding 10).
 *
 * Source under test: src/game/mpconfigs.c (g_MpConfigs), read through
 * tests/support/mpconfig_table_probe.c.
 *
 * The table was positional for the N64 structs. The PC added
 * mpsetup.stage_id[64] after scenario and mpconfigsim.head_id[64]/body_id[64]
 * after type, so the stage, limits and weapons landed inside stage_id and
 * every simulant's head, body, team and difficulties inside head_id, leaving
 * difficulty 0 (MeatSim, enabled) everywhere. simulants[] also grew from the
 * 8 a config authors to MAX_BOTS = 32, and a zeroed slot is an enabled
 * MeatSim at every player count. Designated initializers fix both; these
 * cases fail on the positional table.
 */

#include "catch.hpp"

extern "C" {
#include "support/mpconfig_table_probe.h"
}

namespace {

s32 k(const char *name)
{
	const s32 value = mpconfigProbeConst(name);
	INFO("constant " << name);
	REQUIRE(value >= 0);
	return value;
}

} // namespace

TEST_CASE("mpconfig table: every config names a stage and a scenario in their own fields",
          "[mpconfig][challenges][finding10]") {
	for (s32 c = 0; c < mpconfigProbeNumConfigs(); c++) {
		INFO("config " << c);
		REQUIRE(mpconfigProbeStagenum(c) != 0);
		REQUIRE(mpconfigProbeScenario(c) >= k("MPSCENARIO_COMBAT"));
		REQUIRE(mpconfigProbeScenario(c) <= k("MPSCENARIO_CAPTURETHECASE"));
		/* The catalog id is filled from stagenum when the config is applied. */
		REQUIRE(mpconfigProbeStageIdEmpty(c));
		/* Time limits are minutes minus one, at most 60 in the original. */
		REQUIRE(mpconfigProbeTimelimit(c) <= 60);
	}
}

TEST_CASE("mpconfig table: Temple keeps its stage, limits and weapons",
          "[mpconfig][challenges][finding10]") {
	const s32 c = k("MPCONFIG_TEMPLE");

	REQUIRE(mpconfigProbeStagenum(c) == k("STAGE_MP_TEMPLE"));
	REQUIRE(mpconfigProbeScenario(c) == k("MPSCENARIO_COMBAT"));
	REQUIRE(mpconfigProbeTimelimit(c) == 9);
	REQUIRE(mpconfigProbeScorelimit(c) == 100);
	REQUIRE(mpconfigProbeTeamscorelimit(c) == 400);
	REQUIRE(mpconfigProbeWeapon(c, 0) == k("MPWEAPON_DEVASTATOR"));
	REQUIRE(mpconfigProbeWeapon(c, 2) == k("MPWEAPON_SUPERDRAGON"));
	REQUIRE(mpconfigProbeWeapon(c, mpconfigProbeNumWeaponSlots() - 1) == k("MPWEAPON_DISABLED"));

	for (s32 s = 0; s < 8; s++) {
		INFO("simulant " << s);
		REQUIRE(mpconfigProbeSimTeam(c, s) == k("MPTEAM_1"));
		for (s32 p = 0; p < mpconfigProbeMaxPlayers(); p++) {
			REQUIRE(mpconfigProbeSimDifficulty(c, s, p) == k("BOTDIFF_DISABLED"));
		}
	}
}

TEST_CASE("mpconfig table: Skedar King is King of the Hill on Skedar with a mobile hill",
          "[mpconfig][challenges][finding10]") {
	const s32 c = k("MPCONFIG_SKEDARKING");

	REQUIRE(mpconfigProbeStagenum(c) == k("STAGE_MP_SKEDAR"));
	REQUIRE(mpconfigProbeScenario(c) == k("MPSCENARIO_KINGOFTHEHILL"));
	REQUIRE((mpconfigProbeOptions(c) & (u32)k("MPOPTION_KOH_MOBILEHILL")) != 0);
	REQUIRE((mpconfigProbeOptions(c) & (u32)k("MPOPTION_TEAMSENABLED")) != 0);
	REQUIRE(mpconfigProbeTimelimit(c) == 9);
	REQUIRE(mpconfigProbeScorelimit(c) == 100);
	REQUIRE(mpconfigProbeTeamscorelimit(c) == 9);
}

TEST_CASE("mpconfig table: a challenge simulant keeps its head, body, team and difficulties",
          "[mpconfig][challenges][finding10]") {
	const s32 c = k("MPCONFIG_CHALLENGE20");

	REQUIRE(mpconfigProbeSimHead(c, 0) == k("MPHEAD_GRANT"));
	REQUIRE(mpconfigProbeSimBody(c, 0) == k("MPBODY_PILOTAF1"));
	REQUIRE(mpconfigProbeSimTeam(c, 0) == k("MPTEAM_1"));
	REQUIRE(mpconfigProbeSimHeadIdEmpty(c, 0));
	REQUIRE(mpconfigProbeSimDifficulty(c, 0, 0) == k("BOTDIFF_HARD"));
	REQUIRE(mpconfigProbeSimDifficulty(c, 0, 1) == k("BOTDIFF_PERFECT"));
	REQUIRE(mpconfigProbeSimDifficulty(c, 0, 3) == k("BOTDIFF_PERFECT"));
	/* 5 to 8 players use the 4-player difficulty (the original stops at 4). */
	for (s32 p = 4; p < mpconfigProbeMaxPlayers(); p++) {
		REQUIRE(mpconfigProbeSimDifficulty(c, 0, p) == k("BOTDIFF_PERFECT"));
	}
}

TEST_CASE("mpconfig table: simulant slots past the eight a config authors are disabled",
          "[mpconfig][challenges][finding10]") {
	REQUIRE(mpconfigProbeMaxBots() > 8);

	for (s32 c = 0; c < mpconfigProbeNumConfigs(); c++) {
		for (s32 s = 8; s < mpconfigProbeMaxBots(); s++) {
			for (s32 p = 0; p < mpconfigProbeMaxPlayers(); p++) {
				INFO("config " << c << " simulant " << s << " player count index " << p);
				REQUIRE(mpconfigProbeSimDifficulty(c, s, p) == k("BOTDIFF_DISABLED"));
			}
		}
	}
}
