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

/* forge_level_io.h: a session starts with no level file open and no arena
 * named. A caller that knows which arena it started the session on (The
 * Grid's Enter, forgeLevelStartForge) names it right after a start succeeds. */
extern "C" void forgeLevelSessionBegin(const char *arena_id);

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

static s32 forgeStartOnStage(s32 target, bool canvas)
{
	if (g_MainChangeToStageNum >= 0) {
		sysLogPrintf(LOG_WARNING, "GRID: start rejected -- stage transition already pending (%d)",
			g_MainChangeToStageNum);
		return 0;
	}
	if (!forgeStageIsGridEligible(target)) {
		sysLogPrintf(LOG_ERROR, "GRID: start rejected -- stage 0x%02x is not Grid-eligible",
			(u32)target);
		return 0;
	}

	/* A different stage is queued synchronously but loads later. Do not
	 * close the current document or arm Forge if preparation refused or
	 * chose a fallback stage. Preserve the in-place path for the current
	 * stage, so it neither reloads nor tears down its input context. */
	if (g_StageNum != target) {
		mainChangeToStage(target);
		if (g_MainChangeToStageNum != target) {
			sysLogPrintf(LOG_WARNING, "GRID: stage 0x%02x was not queued (pending=%d)",
				(u32)target, g_MainChangeToStageNum);
			return 0;
		}
	}

	sysLogPrintf(LOG_NOTE, canvas ? "GRID: launching CANVAS session (base stage 0x%02x)"
		: "GRID: launching session (base stage 0x%02x)", (u32)target);
	forgeLevelSessionBegin("");
	if (canvas) forgeRequestEnterSessionCanvas();
	else forgeRequestEnterSession();
	return 1;
}

s32 pdguiForgeStartSessionOn(s32 stagenum)
{
	return forgeStartOnStage(stagenum > 0 ? stagenum : (s32)STAGE_CITRAINING, false);
}

s32 pdguiForgeStartSession(void)
{
	return pdguiForgeStartSessionOn((s32)STAGE_CITRAINING);
}

s32 pdguiForgeStartSessionOnCanvas(s32 stagenum)
{
	return forgeStartOnStage(stagenum, true);
}
