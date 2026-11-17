/*
 * tests/support/mpconfig_table_probe.c -- C accessors over g_MpConfigs
 * (src/game/mpconfigs.c) for tests/test_mpconfig_table.cpp.
 */

#include <string.h>
#include <ultra64.h>
#include "constants.h"
#include "types.h"

#include "mpconfig_table_probe.h"

extern struct mpconfig g_MpConfigs[];

s32 mpconfigProbeNumConfigs(void) { return MPCONFIG_CHALLENGE30 + 1; }
s32 mpconfigProbeMaxBots(void) { return MAX_BOTS; }
s32 mpconfigProbeMaxPlayers(void) { return MAX_PLAYERS; }
s32 mpconfigProbeNumWeaponSlots(void) { return NUM_MPWEAPONSLOTS; }

s32 mpconfigProbeConst(const char *name)
{
	static const struct { const char *name; s32 value; } k[] = {
		{ "BOTDIFF_DISABLED", BOTDIFF_DISABLED },
		{ "BOTDIFF_MEAT", BOTDIFF_MEAT },
		{ "BOTDIFF_NORMAL", BOTDIFF_NORMAL },
		{ "BOTDIFF_HARD", BOTDIFF_HARD },
		{ "BOTDIFF_PERFECT", BOTDIFF_PERFECT },
		{ "BOTDIFF_DARK", BOTDIFF_DARK },
		{ "MPSCENARIO_COMBAT", MPSCENARIO_COMBAT },
		{ "MPSCENARIO_CAPTURETHECASE", MPSCENARIO_CAPTURETHECASE },
		{ "MPSCENARIO_KINGOFTHEHILL", MPSCENARIO_KINGOFTHEHILL },
		{ "MPCONFIG_TEMPLE", MPCONFIG_TEMPLE },
		{ "MPCONFIG_SKEDARKING", MPCONFIG_SKEDARKING },
		{ "MPCONFIG_CHALLENGE20", MPCONFIG_CHALLENGE20 },
		{ "STAGE_MP_TEMPLE", STAGE_MP_TEMPLE },
		{ "STAGE_MP_SKEDAR", STAGE_MP_SKEDAR },
		{ "MPWEAPON_DEVASTATOR", MPWEAPON_DEVASTATOR },
		{ "MPWEAPON_SUPERDRAGON", MPWEAPON_SUPERDRAGON },
		{ "MPWEAPON_DISABLED", MPWEAPON_DISABLED },
		{ "MPHEAD_GRANT", MPHEAD_GRANT },
		{ "MPBODY_PILOTAF1", MPBODY_PILOTAF1 },
		{ "MPTEAM_1", MPTEAM_1 },
		{ "MPOPTION_TEAMSENABLED", MPOPTION_TEAMSENABLED },
		{ "MPOPTION_KOH_MOBILEHILL", MPOPTION_KOH_MOBILEHILL },
	};
	u32 i;

	for (i = 0; i < ARRAYCOUNT(k); i++) {
		if (strcmp(k[i].name, name) == 0) {
			return k[i].value;
		}
	}

	return -1;
}

s32 mpconfigProbeStagenum(s32 config) { return g_MpConfigs[config].setup.stagenum; }
s32 mpconfigProbeStageIdEmpty(s32 config) { return g_MpConfigs[config].setup.stage_id[0] == '\0'; }
s32 mpconfigProbeScenario(s32 config) { return g_MpConfigs[config].setup.scenario; }
s32 mpconfigProbeTimelimit(s32 config) { return g_MpConfigs[config].setup.timelimit; }
s32 mpconfigProbeScorelimit(s32 config) { return g_MpConfigs[config].setup.scorelimit; }
s32 mpconfigProbeTeamscorelimit(s32 config) { return g_MpConfigs[config].setup.teamscorelimit; }
u32 mpconfigProbeOptions(s32 config) { return g_MpConfigs[config].setup.options; }
s32 mpconfigProbeWeapon(s32 config, s32 slot) { return g_MpConfigs[config].setup.weapons[slot]; }

s32 mpconfigProbeSimType(s32 config, s32 sim) { return g_MpConfigs[config].simulants[sim].type; }
s32 mpconfigProbeSimHead(s32 config, s32 sim) { return g_MpConfigs[config].simulants[sim].mpheadnum; }
s32 mpconfigProbeSimBody(s32 config, s32 sim) { return g_MpConfigs[config].simulants[sim].mpbodynum; }
s32 mpconfigProbeSimTeam(s32 config, s32 sim) { return g_MpConfigs[config].simulants[sim].team; }
s32 mpconfigProbeSimHeadIdEmpty(s32 config, s32 sim) { return g_MpConfigs[config].simulants[sim].head_id[0] == '\0'; }

s32 mpconfigProbeSimDifficulty(s32 config, s32 sim, s32 playercountindex)
{
	return g_MpConfigs[config].simulants[sim].difficulties[playercountindex];
}
