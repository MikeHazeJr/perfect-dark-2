#include <ultra64.h>
#include <string.h>
#include "constants.h"
#include "game/body.h"
#include "game/cheats.h"
#include "game/chrai.h"
#include "game/game_00b820.h"
#include "game/playerreset.h"
#include "game/setuputils.h"
#include "bss.h"
#include "lib/memp.h"
#include "lib/rng.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "assetcatalog.h"
#include "bodies_headcount.h" /* INV-5: pure stage_id-to-head-count lookup */

void bodiesReset(s32 stagenum)
{
	s32 *headsavailablelist;
	s32 headsavailablelen;
	bool done;
	s32 i;
	s32 j;
	s32 whichteamlist = 1;
	s32 index;

	sysLogPrintf(LOG_NOTE, "BODIES: enter stagenum=0x%02x normmplay=%d NumBondBodies=%d MaleHeads=%d FemaleHeads=%d",
		stagenum, g_Vars.normmplayerisrunning, g_NumBondBodies, g_NumMaleGuardHeads, g_NumFemaleGuardHeads);

	catalogResetAllModeldefs(); /* SA-5f */

	sysLogPrintf(LOG_NOTE, "BODIES: modeldef clear done");

	/* In multiplayer (Combat Sim and all MP scenarios) there are no guards —
	 * only players and bots. Guard head/body randomization is unused and the
	 * guard head/body model data is not loaded for MP arenas. Skip the whole
	 * randomization pass to avoid a modulo-by-zero if guard lists are empty
	 * and to avoid loading guard models that won't be used. */
	if (g_Vars.normmplayerisrunning) {
		g_ActiveMaleHeadsIndex = 0;
		g_ActiveFemaleHeadsIndex = 0;
		sysLogPrintf(LOG_NOTE, "BODIES: normmplay active — skipping guard body randomization");
		return;
	}

	sysLogPrintf(LOG_NOTE, "BODIES: rng bond body select NumBondBodies=%d", g_NumBondBodies);
	var80062c80 = rngRandom() % g_NumBondBodies;
	var80062b14 = 0;
	var80062b18 = 0;

	if (PLAYERCOUNT() >= 2) {
		g_NumActiveHeadsPerGender = 4;
	} else {
		/* M0.1a: catalog-first stage identity for head count overrides.
		 *
		 * INV-5 / Cohort E.2 (player-init-architectural-fixes-2026-04-26):
		 * stage_id-to-head-count override moved into pure helper
		 * bodiesGetActiveHeadCountForStageId so the override map is
		 * locked by pd-tests. Pre-INV-5 the strcmp literal chain was
		 * silent for any stage_id not matching base:infiltration|rescue|escape
		 * (e.g. mod stages, AllInOne missions). Add WARNING when an
		 * empty or unknown non-empty stage_id falls through to the
		 * default so mods can be registered explicitly. */
		const s32 default_count = 8;
		g_NumActiveHeadsPerGender =
			bodiesGetActiveHeadCountForStageId(g_MissionConfig.stage_id, default_count);

		if (g_NumActiveHeadsPerGender == default_count
				&& g_MissionConfig.stage_id[0] != '\0') {
			sysLogPrintf(LOG_WARNING,
				"BODIES.HEADCOUNT: stage_id='%s' not in override map "
				"(base:infiltration|rescue|escape), defaulting to %d -- "
				"guards may use unintended head set",
				g_MissionConfig.stage_id, g_NumActiveHeadsPerGender);
		}
	}

	sysLogPrintf(LOG_NOTE, "BODIES: selecting male heads NumActivePerGender=%d", g_NumActiveHeadsPerGender);
	// Male heads
	if (cheatIsActive(CHEAT_TEAMHEADSONLY)) {
		if (whichteamlist) {
			headsavailablelist = g_MaleGuardTeamHeads;
			headsavailablelen = g_NumMaleGuardTeamHeads;
		} else {
			headsavailablelist = g_MaleGuardTeamHeads;
			headsavailablelen = var80062b14;
		}
	} else {
		headsavailablelist = g_MaleGuardHeads;
		headsavailablelen = g_NumMaleGuardHeads;
	}

	for (i = 0; i < g_NumActiveHeadsPerGender; i++) {
		do {
			done = true;
			g_ActiveMaleHeads[i] = headsavailablelist[rngRandom() % headsavailablelen];

			if (headsavailablelen > g_NumActiveHeadsPerGender) {
				for (j = 0; j < i; j++) {
					if (g_ActiveMaleHeads[j] == g_ActiveMaleHeads[i]) {
						done = false;
					}
				}
				if (j && j && j);
			}
		} while (!done);
	}

	sysLogPrintf(LOG_NOTE, "BODIES: selecting female heads headsavailablelen=%d", headsavailablelen);
	// Female heads
	if (cheatIsActive(CHEAT_TEAMHEADSONLY)) {
		if (whichteamlist) {
			headsavailablelist = g_FemaleGuardTeamHeads;
			headsavailablelen = g_NumFemaleGuardTeamHeads;
		} else {
			headsavailablelist = g_FemaleGuardTeamHeads;
			headsavailablelen = var80062b18;
		}
	} else {
		headsavailablelist = g_FemaleGuardHeads;
		headsavailablelen = g_NumFemaleGuardHeads;
	}

	for (i = 0; i < g_NumActiveHeadsPerGender; i++) {
		do {
			done = true;
			g_ActiveFemaleHeads[i] = headsavailablelist[rngRandom() % headsavailablelen];

			if (headsavailablelen > g_NumActiveHeadsPerGender) {
				for (j = 0; j < i; j++) {
					if (g_ActiveFemaleHeads[j] == g_ActiveFemaleHeads[i]) {
						done = false;
					}
				}
			}
		} while (!done);
	}

	g_ActiveMaleHeadsIndex = 0;
	g_ActiveFemaleHeadsIndex = 0;

	sysLogPrintf(LOG_NOTE, "BODIES: bodiesReset complete");

	for (i = 0; i < g_NumActiveHeadsPerGender; i++);
	for (i = 0; i < g_NumActiveHeadsPerGender; i++);
}
