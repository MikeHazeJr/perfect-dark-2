/*
 * Campaign auto-runner (c126, 2026-05-18).
 *
 * See port/include/autocampaign.h for the API contract.
 *
 * Implementation summary:
 *
 *   State machine ticked once per frame from pdmain.c::mainTick.
 *   Inert no-op when not armed.
 *
 *   AC_STATE_BOOT
 *     - Push solo stage at s_target_start_idx via mainChangeToStage.
 *     - Force-zero coop/anti, force solo, set debug flags.
 *     - Move to WAIT_LOAD.
 *
 *   AC_STATE_WAIT_LOAD
 *     - Wait until g_MainIsEndscreen is false, g_Vars.stagenum
 *       resolves into a campaign solo index, g_Vars.bond is set,
 *       and lvframenum > 30 (allow a few frames of post-load settle).
 *     - Move to DWELL_PRE. Watchdog AC_DWELL_LOAD_TIMEOUT.
 *
 *   AC_STATE_DWELL_PRE
 *     - Hold while playerAnyInCutscene() reports true (intro is
 *       playing). When the cutscene ends, count down s_dwell_frames
 *       (AC_DWELL_PRE_FRAMES base, AC_FAST_DWELL with --auto-campaign-
 *       fast) so the player sees a brief gameplay establishing shot
 *       before the endscreen pops.
 *     - Move to FORCE_END.
 *
 *   AC_STATE_FORCE_END
 *     - Clear isdead / aborted on the bond player so endscreen takes
 *       the success branch.
 *     - Call objectivesCheckAll() and mainEndStage().
 *     - Move to WAIT_END.
 *
 *   AC_STATE_WAIT_END
 *     - Wait for g_MainIsEndscreen to flip true.
 *     - Move to DWELL_END. Watchdog 600 frames; retries FORCE_END
 *       once if endscreen never appears (rare; usually a race in the
 *       same frame).
 *
 *   AC_STATE_DWELL_END
 *     - Hold AC_DWELL_END_FRAMES so the endscreen results are
 *       visible to the user.
 *     - Move to ADVANCE.
 *
 *   AC_STATE_ADVANCE
 *     - If g_Vars.stagenum == STAGE_SKEDARRUINS: endscreenContinue(2)
 *       routes to Credits per the existing endscreen logic. Move to
 *       DONE.
 *     - Otherwise: endscreenContinue(2) pops the endscreen and
 *       pushes the next-mission briefing dialog. Move to DWELL_BRIEF.
 *
 *   AC_STATE_DWELL_BRIEF
 *     - Hold AC_DWELL_BRIEF_FRAMES so the user can read the briefing.
 *     - Move to ACCEPT_NEXT.
 *
 *   AC_STATE_ACCEPT_NEXT
 *     - Call menuhandlerAcceptMission(MENUOP_SET, NULL, NULL). This
 *       is the same entry point that the briefing's "Accept" button
 *       uses; it runs the full mission-start plumbing
 *       (menuStop / romdataFileFreeForSolo / titleSetNextStage /
 *       setNumPlayers / lvSetDifficulty / titleSetNextMode /
 *       mainChangeToStage).
 *     - Move to WAIT_LOAD.
 *
 *   AC_STATE_DONE
 *     - Stay here. Credits are loading; debug flags stay set.
 *
 * Hierarchical log channel: CAMPAIGN.AUTO.* . State transitions
 * + advance markers go to LOG_NOTE; load/wait timeouts go to
 * LOG_WARNING.
 */

#include <stdlib.h>
#include <string.h>

#include <ultra64.h>

#include "platform.h"
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "system.h"

#include "game/lv.h"
#include "game/objectives.h"
#include "game/player.h"
#include "game/pdmode.h"
#include "game/title.h"
#include "game/endscreen.h"
#include "game/mainmenu.h"
#include "game/stagetable.h"
#include "lib/main.h"

#include "assetcatalog.h"
#include "autocampaign.h"

extern bool g_DebugObjectives;
extern bool g_DebugSetComplete;
extern struct solostage g_SoloStages[];
extern s32 g_MainChangeToStageNum;

#define ACLOG(fmt, ...)  sysLogPrintf(LOG_NOTE,    "CAMPAIGN.AUTO: " fmt, ##__VA_ARGS__)
#define ACWARN(fmt, ...) sysLogPrintf(LOG_WARNING, "CAMPAIGN.AUTO: " fmt, ##__VA_ARGS__)

enum acState {
	AC_STATE_IDLE = 0,
	AC_STATE_BOOT,
	AC_STATE_WAIT_LOAD,
	AC_STATE_DWELL_PRE,
	AC_STATE_FORCE_END,
	AC_STATE_WAIT_END,
	AC_STATE_DWELL_END,
	AC_STATE_ADVANCE,
	AC_STATE_DWELL_BRIEF,
	AC_STATE_ACCEPT_NEXT,
	AC_STATE_DONE,
	AC_STATE__COUNT
};

static const char *kStateName[AC_STATE__COUNT] = {
	"IDLE",
	"BOOT",
	"WAIT_LOAD",
	"DWELL_PRE",
	"FORCE_END",
	"WAIT_END",
	"DWELL_END",
	"ADVANCE",
	"DWELL_BRIEF",
	"ACCEPT_NEXT",
	"DONE",
};

static enum acState s_state = AC_STATE_IDLE;
static u32 s_frames_in_state = 0;
static u32 s_dwell_frames = 0;
static u32 s_flags = 0;
static s32 s_target_start_idx = 0;
static u32 s_completed_count = 0;
static s32 s_last_stagenum_processed = -1;
static bool s_retried_force_end = false;

#define AC_DWELL_PRE_FRAMES   240u   /* 4 sec base */
#define AC_DWELL_END_FRAMES   240u
#define AC_DWELL_BRIEF_FRAMES 240u
#define AC_DWELL_LOAD_TIMEOUT 3600u  /* 60 sec safety */
#define AC_DWELL_END_TIMEOUT  600u   /* 10 sec watchdog after force-end */
#define AC_FAST_DWELL          30u   /* 0.5 sec @ 60 Hz */

static u32 dwellFor(u32 base)
{
	return (s_flags & AUTOCAMPAIGN_FLAG_FAST) ? AC_FAST_DWELL : base;
}

static void setState(enum acState s)
{
	if (s != s_state) {
		ACLOG("state %s -> %s lvframenum=%u stagenum=0x%02x idx=%d isend=%d",
			kStateName[s_state], kStateName[s], (u32)g_Vars.lvframenum,
			(u32)g_Vars.stagenum, (s32)g_MissionConfig.stageindex,
			g_MainIsEndscreen ? 1 : 0);
		s_state = s;
		s_frames_in_state = 0;
	}
}

static bool stageIsCampaignSolo(s32 stagenum)
{
	return soloStageGetIndex(stagenum) >= 0;
}

static void armDebugFlags(void)
{
	g_DebugSetComplete = true;
	g_DebugObjectives = true;
}

static void clearDebugFlags(void)
{
	g_DebugSetComplete = false;
	g_DebugObjectives = false;
}

/*
 * Drive an explicit mission start at the given solo index. Mirrors the
 * subset of menuhandlerAcceptMission state plumbing relevant to solo:
 * mission config, player slots, difficulty, title mode, stage change.
 * Used only from the BOOT state -- the chained-mission path goes
 * through menuhandlerAcceptMission directly via ACCEPT_NEXT.
 */
static void startStageAt(s32 solo_idx)
{
	if (solo_idx < 0) {
		solo_idx = 0;
	}
	if (solo_idx >= NUM_SOLOSTAGES) {
		solo_idx = NUM_SOLOSTAGES - 1;
	}

	g_MissionConfig.stageindex = (u8)solo_idx;
	g_MissionConfig.difficulty = DIFF_A;
	g_MissionConfig.iscoop = 0;
	g_MissionConfig.isanti = 0;
	g_MissionConfig.pdmode = 0;

	const char *cid = g_SoloStages[solo_idx].catalog_id;
	if (cid && cid[0]) {
		strncpy(g_MissionConfig.stage_id, cid, sizeof(g_MissionConfig.stage_id) - 1);
		g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
		catalog_stage_result_t r;
		if (catalogResolveStage(cid, &r)) {
			g_MissionConfig.stagenum = (u8)r.stagenum;
		} else {
			g_MissionConfig.stagenum = (u8)g_SoloStages[solo_idx].stagenum;
		}
	} else {
		g_MissionConfig.stage_id[0] = '\0';
		g_MissionConfig.stagenum = (u8)g_SoloStages[solo_idx].stagenum;
	}

	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;
	setNumPlayers(1);

	titleSetNextStage((s32)g_MissionConfig.stagenum);
	lvSetDifficulty(g_MissionConfig.difficulty);
	titleSetNextMode(TITLEMODE_SKIP);
	mainChangeToStage((s32)g_MissionConfig.stagenum);

	ACLOG("boot stage_id='%s' stagenum=0x%02x idx=%d",
		g_MissionConfig.stage_id[0] ? g_MissionConfig.stage_id : "(empty)",
		(u32)g_MissionConfig.stagenum, solo_idx);
}

void autocampaignArm(int start_solo_index, unsigned int flags)
{
	s_target_start_idx = start_solo_index >= 0 ? start_solo_index : 0;
	s_flags = flags;
	s_completed_count = 0;
	s_last_stagenum_processed = -1;
	s_retried_force_end = false;
	armDebugFlags();
	setState(AC_STATE_BOOT);
	ACLOG("armed start_idx=%d flags=0x%x", s_target_start_idx, s_flags);
}

void autocampaignDisarm(void)
{
	if (s_state == AC_STATE_IDLE) {
		return;
	}
	clearDebugFlags();
	ACLOG("disarmed (completed_count=%u state=%s)",
		s_completed_count, kStateName[s_state]);
	s_state = AC_STATE_IDLE;
	s_frames_in_state = 0;
}

int autocampaignIsActive(void)
{
	return s_state != AC_STATE_IDLE && s_state != AC_STATE_DONE;
}

void autocampaignTick(void)
{
	if (s_state == AC_STATE_IDLE) {
		return;
	}
	s_frames_in_state++;

	switch (s_state) {
	case AC_STATE_BOOT:
		startStageAt(s_target_start_idx);
		setState(AC_STATE_WAIT_LOAD);
		break;

	case AC_STATE_WAIT_LOAD:
		/* WAIT_LOAD waits for a *new* campaign solo stage to be live.
		 * s_last_stagenum_processed is set by FORCE_END to the stagenum
		 * we just finished; the check below makes sure we don't
		 * re-trigger on the same stage while mainChangeToStage is still
		 * pending (between ACCEPT_NEXT and the actual stage swap). */
		if (!g_MainIsEndscreen
				&& stageIsCampaignSolo(g_Vars.stagenum)
				&& (s32)g_Vars.stagenum != s_last_stagenum_processed
				&& g_Vars.bond
				&& g_Vars.lvframenum > 30u) {
			/* Re-arm in case any subsystem cleared them on stage load. */
			armDebugFlags();
			s_retried_force_end = false;
			s_dwell_frames = dwellFor(AC_DWELL_PRE_FRAMES);
			setState(AC_STATE_DWELL_PRE);
		} else if (s_frames_in_state > AC_DWELL_LOAD_TIMEOUT) {
			ACWARN("load timeout at stagenum=0x%02x lvframenum=%u; disarming",
				(u32)g_Vars.stagenum, (u32)g_Vars.lvframenum);
			autocampaignDisarm();
		}
		break;

	case AC_STATE_DWELL_PRE:
		/* While the intro cutscene is playing, let it run. */
		if (playerAnyInCutscene()) {
			break;
		}
		if (s_dwell_frames > 0u) {
			s_dwell_frames--;
		} else {
			setState(AC_STATE_FORCE_END);
		}
		break;

	case AC_STATE_FORCE_END:
		s_last_stagenum_processed = (s32)g_Vars.stagenum;
		armDebugFlags();
		if (g_Vars.bond) {
			g_Vars.bond->isdead = false;
			g_Vars.bond->aborted = false;
		}
		ACLOG("force-end stagenum=0x%02x idx=%d completed_so_far=%u",
			(u32)g_Vars.stagenum, (s32)g_MissionConfig.stageindex, s_completed_count);
		objectivesCheckAll();
		mainEndStage();
		setState(AC_STATE_WAIT_END);
		break;

	case AC_STATE_WAIT_END:
		if (g_MainIsEndscreen) {
			s_dwell_frames = dwellFor(AC_DWELL_END_FRAMES);
			setState(AC_STATE_DWELL_END);
		} else if (s_frames_in_state > AC_DWELL_END_TIMEOUT) {
			if (!s_retried_force_end) {
				ACWARN("endscreen never appeared after mainEndStage; retrying FORCE_END once");
				s_retried_force_end = true;
				setState(AC_STATE_FORCE_END);
			} else {
				ACWARN("endscreen still missing after retry; disarming to avoid hang");
				autocampaignDisarm();
			}
		}
		break;

	case AC_STATE_DWELL_END:
		if (s_dwell_frames > 0u) {
			s_dwell_frames--;
		} else {
			setState(AC_STATE_ADVANCE);
		}
		break;

	case AC_STATE_ADVANCE:
	{
		s_completed_count++;
		ACLOG("advance after stagenum=0x%02x (completed=%u)",
			(u32)g_Vars.stagenum, s_completed_count);

		/* End-of-campaign: Skedar Ruins routes to Credits via
		 * endscreenContinue(2), which itself calls mainChangeToStage
		 * directly (no briefing dialog). */
		if (g_Vars.stagenum == STAGE_SKEDARRUINS) {
			s32 pending_before = g_MainChangeToStageNum;
			endscreenContinue(2);
			s32 pending_after = g_MainChangeToStageNum;
			(void)pending_before;
			(void)pending_after;
			ACLOG("Skedar Ruins -> Credits, campaign complete");
			setState(AC_STATE_DONE);
			break;
		}

		/* For every other stage we bypass the briefing dialog entirely.
		 *
		 * Calling endscreenContinue(2) pushes the next-mission briefing
		 * dialog on top of the endscreen, and the briefing's MENUOP_OPEN
		 * runs setupLoadBriefing for the next mission. Empirically the
		 * briefing render race produces an AV (smoke run, 2026-05-18)
		 * shortly after the menu pool transition (endscreen_solo ->
		 * solo_mission with deferred IMC removal). The briefing display
		 * is not a cutscene -- it is a text screen with the mission
		 * objectives -- so skipping it does not violate the "play the
		 * cutscenes" requirement; the in-mission intro cutscene still
		 * plays on stage load.
		 *
		 * The replacement flow: increment stageindex, set stage_id +
		 * stagenum from the catalog, and call menuhandlerAcceptMission
		 * directly with NULL data. menuhandlerAcceptMission calls
		 * menuStop() which tears down the endscreen root cleanly, then
		 * runs the full mission-start plumbing
		 * (romdataFileFreeForSolo, titleSetNextStage, setNumPlayers,
		 * lvSetDifficulty, titleSetNextMode, mainChangeToStage). The
		 * endscreen menu state is the one being torn down -- the same
		 * stack shape menuStop is used to handling -- rather than the
		 * shorter-lived briefing dialog that triggered the AV. */
		s32 next_idx = (s32)g_MissionConfig.stageindex + 1;
		if (next_idx >= NUM_SOLOSTAGES) {
			next_idx = NUM_SOLOSTAGES - 1;
		}
		g_MissionConfig.stageindex = (u8)next_idx;
		const char *cid = g_SoloStages[next_idx].catalog_id;
		if (cid && cid[0]) {
			strncpy(g_MissionConfig.stage_id, cid, sizeof(g_MissionConfig.stage_id) - 1);
			g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
			catalog_stage_result_t r;
			if (catalogResolveStage(cid, &r)) {
				g_MissionConfig.stagenum = (u8)r.stagenum;
			} else {
				g_MissionConfig.stagenum = (u8)g_SoloStages[next_idx].stagenum;
			}
		} else {
			g_MissionConfig.stage_id[0] = '\0';
			g_MissionConfig.stagenum = (u8)g_SoloStages[next_idx].stagenum;
		}
		ACLOG("queue next mission idx=%d stagenum=0x%02x stage_id='%s'",
			next_idx, (u32)g_MissionConfig.stagenum,
			g_MissionConfig.stage_id);
		menuhandlerAcceptMission(MENUOP_SET, NULL, NULL);
		setState(AC_STATE_WAIT_LOAD);
		break;
	}

	case AC_STATE_DWELL_BRIEF:
	case AC_STATE_ACCEPT_NEXT:
		/* Unreachable in the consolidated ADVANCE flow (2026-05-18).
		 * Retained as enum values so the kStateName table stays in sync
		 * if the briefing-dwell flow is restored later. Treat as a
		 * no-op to be safe. */
		setState(AC_STATE_WAIT_LOAD);
		break;

	case AC_STATE_DONE:
		if (s_frames_in_state == 1u) {
			ACLOG("campaign complete: %u missions ran (last=0x%02x)",
				s_completed_count, (u32)s_last_stagenum_processed);
		}
		break;

	case AC_STATE_IDLE:
	case AC_STATE__COUNT:
	default:
		break;
	}
}

void autocampaignInitFromCli(void)
{
	if (!sysArgCheck("--auto-campaign")) {
		return;
	}
	s32 idx = sysArgGetInt("--auto-campaign", 0);
	if (idx < 0) {
		idx = 0;
	}
	if (idx >= NUM_SOLOSTAGES) {
		idx = NUM_SOLOSTAGES - 1;
	}
	unsigned int flags = 0u;
	if (sysArgCheck("--auto-campaign-fast")) {
		flags |= AUTOCAMPAIGN_FLAG_FAST;
	}
	autocampaignArm((int)idx, flags);
}
