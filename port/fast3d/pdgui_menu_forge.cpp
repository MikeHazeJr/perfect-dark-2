/**
 * pdgui_menu_forge.cpp -- Forge entry point shims (Phase F0).
 *
 * F0 deliberately ships only a single "start a forge session" call so the
 * main menu button has somewhere to dispatch to.  The base-stage browser
 * (currently hard-wired to CI Training) lands in F3 alongside save/load.
 */

#include "pdgui_forge.h"

extern "C" {
#include <PR/ultratypes.h>
#include "constants.h"
#include "system.h"
#include "game/forgemode.h"

void mainChangeToStage(s32 stagenum);
extern s32 g_MainChangeToStageNum;
extern s32 g_StageNum;
}

/* Issue 5 (2026-04-24): third line of defense against the "Grid loads a
 * system stage and crashes" bug.  The picker filter + gridCommitEnter
 * validate the stagenum at the submenu layer; this entry point ALSO
 * validates because it is called from other code paths (F0-era callers,
 * future mod entry points) that may not share the picker's guarantees.
 * Any system / title / bootpak / credits stage is rejected outright. */
extern "C" s32 stageGetIndex(s32 stagenum);

static s32 forgeStageIsGridEligible(s32 stagenum)
{
	/* STAGE_TITLE=0x5a, STAGE_BOOTPAKMENU=0x5b, STAGE_CREDITS=0x5c.
	 * Mirror of STAGE_IS_SYSTEM from src/include/constants.h:4106 to
	 * avoid pulling the whole chain into this small TU. */
	if (stagenum == 0x5a || stagenum == 0x5b || stagenum == 0x5c) return 0;
	if (stagenum <= 0) return 0;
	if (stageGetIndex(stagenum) < 0) return 0;
	return 1;
}

s32 pdguiForgeStartSessionOn(s32 stagenum)
{
	if (g_MainChangeToStageNum >= 0) {
		sysLogPrintf(LOG_WARNING,
				"GRID: start rejected -- stage transition already pending (%d)",
				g_MainChangeToStageNum);
		return 0;
	}

	/* Default to CI Training only when the caller passed 0 / negative.
	 * Any non-default stagenum must survive the eligibility check. */
	s32 target = (stagenum > 0) ? stagenum : (s32)STAGE_CITRAINING;

	if (!forgeStageIsGridEligible(target)) {
		sysLogPrintf(LOG_WARNING,
				"GRID: start rejected -- stagenum 0x%02x is not Grid-eligible "
				"(system stage or unregistered); falling back to CI Training",
				(u32)target);
		target = (s32)STAGE_CITRAINING;
		if (!forgeStageIsGridEligible(target)) {
			sysLogPrintf(LOG_WARNING,
					"GRID: fallback CI Training also not eligible -- abort");
			return 0;
		}
	}

	sysLogPrintf(LOG_NOTE, "GRID: launching session (base stage 0x%02x)",
			(u32)target);

	forgeRequestEnterSession();
	/* B-216: re-entering the same stage used to queue a full stage transition
	 * (inputCtxShutdown + reload), breaking menus. Skip redundant
	 * mainChangeToStage -- forgemode activates on the next tick. */
	if (g_StageNum != target) {
		mainChangeToStage(target);
	}
	return 1;
}

s32 pdguiForgeStartSession(void)
{
	return pdguiForgeStartSessionOn((s32)STAGE_CITRAINING);
}
