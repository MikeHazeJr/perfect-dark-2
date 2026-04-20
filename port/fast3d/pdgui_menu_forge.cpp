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

s32 pdguiForgeStartSession(void)
{
	if (g_MainChangeToStageNum >= 0) {
		sysLogPrintf(LOG_WARNING,
				"GRID: start rejected -- stage transition already pending (%d)",
				g_MainChangeToStageNum);
		return 0;
	}

	sysLogPrintf(LOG_NOTE, "GRID: launching session (base stage = CITRAINING 0x%02x)",
			(u32)STAGE_CITRAINING);

	forgeRequestEnterSession();
	/* B-216: re-entering CI while already in CI used to queue a full stage
	 * transition (inputCtxShutdown + reload), breaking menus. Skip redundant
	 * mainChangeToStage — forgemode activates on the next tick. */
	if (g_StageNum != STAGE_CITRAINING) {
		mainChangeToStage(STAGE_CITRAINING);
	}
	return 1;
}
