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

/* Gateway sanity check. The caller (Grid picker commit, future mod
 * entry points) is the authority on which stage to launch; this check
 * only catches real data bugs where an ineligible stagenum reached the
 * gateway. No silent fallbacks -- LOG_ERROR and refuse so the caller
 * sees the bug and the user doesn't end up in a "why am I in CI Training"
 * situation. */
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

	/* Default to CI Training only when the caller passed 0 / negative --
	 * the "no stage in mind" path. Any explicit stagenum must be
	 * eligible; otherwise refuse loudly rather than silently rewriting
	 * the caller's intent. */
	s32 target = (stagenum > 0) ? stagenum : (s32)STAGE_CITRAINING;

	if (!forgeStageIsGridEligible(target)) {
		sysLogPrintf(LOG_ERROR,
				"GRID: start ABORT -- stagenum 0x%02x is not Grid-eligible "
				"(system stage or unregistered). Caller supplied a bad stagenum; "
				"fix the data source rather than silently rewriting the target.",
				(u32)target);
		return 0;
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
