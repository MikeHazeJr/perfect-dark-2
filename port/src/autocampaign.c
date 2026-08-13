/*
 * Strict release campaign runner.
 *
 * This module accelerates objective completion, but it does not manufacture
 * campaign progression. It observes the ordinary endscreen, PC-native save,
 * unlock, ImGui next-mission bridge, and live Credits transition. Every
 * mission is matched against one exact ordered catalog plan.
 */

#include <stdarg.h>
#include <stdio.h>
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
#include "game/mainmenu.h"
#include "game/objectives.h"
#include "game/player.h"
#include "game/stagetable.h"
#include "lib/main.h"

#include "assetcatalog.h"
#include "autocampaign.h"
#include "autocampaign_cli_plan.h"
#include "campaign_evidence.h"
#include "campaign_run.h"
#include "pdgui_campaign_bridge.h"
#include "presence.h"
#include "savefile.h"
#include "smoke_harness.h"

extern struct solostage g_SoloStages[];
extern s32 g_MainChangeToStageNum;

#define ACLOG(fmt, ...)  sysLogPrintf(LOG_NOTE,    "CAMPAIGN.AUTO: " fmt, ##__VA_ARGS__)
#define ACWARN(fmt, ...) sysLogPrintf(LOG_WARNING, "CAMPAIGN.AUTO: " fmt, ##__VA_ARGS__)

enum acState {
	AC_STATE_IDLE = 0,
	AC_STATE_WAIT_PROFILE,
	AC_STATE_BOOT,
	AC_STATE_WAIT_LOAD,
	AC_STATE_DWELL_PRE,
	AC_STATE_FORCE_END,
	AC_STATE_WAIT_END,
	AC_STATE_DWELL_END,
	AC_STATE_ADVANCE,
	AC_STATE_WAIT_CREDITS,
	AC_STATE_DONE,
	AC_STATE_FAILED,
	AC_STATE__COUNT
};

static const char *const kStateName[AC_STATE__COUNT] = {
	"IDLE",
	"WAIT_PROFILE",
	"BOOT",
	"WAIT_LOAD",
	"DWELL_PRE",
	"FORCE_END",
	"WAIT_END",
	"DWELL_END",
	"ADVANCE",
	"WAIT_CREDITS",
	"DONE",
	"FAILED",
};

#define AC_DWELL_PRE_FRAMES       240u
#define AC_DWELL_END_FRAMES       240u
#define AC_FAST_DWELL              30u
#define AC_FAST_SKIP_REQUEST_FRAME 45u
#define AC_PROFILE_TIMEOUT       3600u
#define AC_LOAD_TIMEOUT          7200u
#define AC_CUTSCENE_TIMEOUT      7200u
#define AC_ENDSCREEN_TIMEOUT      600u
#define AC_CREDITS_TIMEOUT       3600u

static enum acState s_state = AC_STATE_IDLE;
static u32 s_frames_in_state;
static u32 s_dwell_frames;
static s32 s_cutscene_skip_requested;
static u32 s_flags;
static s32 s_target_start_idx;
static s32 s_target_final_idx;
static s32 s_stop_after_final_load;
static s32 s_difficulty;
static campaign_run_t s_run;
static u16 s_persisted_besttimes[CAMPAIGN_RUN_MAX_MISSIONS];
static char s_report_path[512];

static u32 dwellFor(u32 base)
{
	return (s_flags & AUTOCAMPAIGN_FLAG_FAST) ? AC_FAST_DWELL : base;
}

static void setState(enum acState state)
{
	if (state == s_state) {
		return;
	}
	ACLOG("state %s -> %s lvframenum=%u stagenum=0x%02x idx=%d isend=%d",
		kStateName[s_state], kStateName[state], (u32)g_Vars.lvframenum,
		(u32)g_Vars.stagenum, (s32)g_MissionConfig.stageindex,
		g_MainIsEndscreen ? 1 : 0);
	s_state = state;
	s_frames_in_state = 0;
	if (state == AC_STATE_WAIT_LOAD) {
		s_cutscene_skip_requested = 0;
	}
}

static void refreshPersistedBesttimes(void)
{
	s32 i;
	memset(s_persisted_besttimes, 0, sizeof(s_persisted_besttimes));
	if (s_difficulty < DIFF_A || s_difficulty > DIFF_PA) {
		return;
	}
	for (i = 0; i < s_run.mission_count; i++) {
		s32 solo_index = s_run.start_solo_index + i;
		if (solo_index >= 0 && solo_index < NUM_SOLOSTAGES) {
			s_persisted_besttimes[i] =
				g_GameFile.besttimes[solo_index][s_difficulty];
		}
	}
}

static s32 writeEvidence(const char *status, char *error, s32 error_capacity)
{
	campaign_evidence_options_t options;
	refreshPersistedBesttimes();
	memset(&options, 0, sizeof(options));
	options.path = s_report_path;
	options.status = status;
	options.verify_only =
		(s_flags & AUTOCAMPAIGN_FLAG_VERIFY_ONLY) ? 1 : 0;
	options.persisted_besttimes = s_persisted_besttimes;
	options.persisted_besttime_count = s_run.mission_count;
	return campaignEvidenceWrite(&s_run, &options, error, error_capacity);
}

static void stopFailed(void)
{
	char evidence_error[CAMPAIGN_RUN_DETAIL_MAX];
	if (s_run.mission_count > 0) {
		if (writeEvidence("failed", evidence_error,
				(s32)sizeof(evidence_error)) != 0) {
			ACWARN("failed to publish failure evidence: %s", evidence_error);
		}
	}
	setState(AC_STATE_FAILED);
	ACWARN("failed reason=%s detail='%s' completed=%d/%d",
		campaignRunFailureName(s_run.failure), s_run.failure_detail,
		s_run.completed_count, s_run.mission_count);
	if (smokeHarnessIsActive()) {
		smokeHarnessExit(2, "campaign_failed");
	}
}

static void failRun(campaign_run_failure_t failure, const char *format, ...)
{
	char detail[CAMPAIGN_RUN_DETAIL_MAX];
	va_list args;
	va_start(args, format);
	vsnprintf(detail, sizeof(detail), format, args);
	va_end(args);
	campaignRunFail(&s_run, failure, detail);
	stopFailed();
}

static s32 publishEvidenceOrFail(const char *status)
{
	char error[CAMPAIGN_RUN_DETAIL_MAX];
	if (writeEvidence(status, error, (s32)sizeof(error)) == 0) {
		return 1;
	}
	failRun(CAMPAIGN_RUN_FAILURE_EVIDENCE, "%s", error);
	return 0;
}

static s32 resolvePlannedStage(s32 plan_index, catalog_stage_result_t *out)
{
	if (plan_index < 0 || plan_index >= s_run.mission_count || !out) {
		return 0;
	}
	if (!catalogResolveStage(s_run.expected_ids[plan_index], out)) {
		return 0;
	}
	return out->stagenum ==
		g_SoloStages[s_run.start_solo_index + plan_index].stagenum;
}

static s32 buildCampaignPlan(void)
{
	const char *ids[CAMPAIGN_RUN_MAX_MISSIONS];
	s32 mission_count;
	s32 i;

	if (g_NetMode != NETMODE_NONE || g_Vars.normmplayerisrunning) {
		failRun(CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
			"release campaign runner is offline solo only");
		return 0;
	}
	if (s_target_start_idx < 0
			|| s_target_start_idx > SOLOSTAGEINDEX_SKEDARRUINS
			|| s_target_final_idx < s_target_start_idx
			|| s_target_final_idx > SOLOSTAGEINDEX_SKEDARRUINS
			|| (s_stop_after_final_load
				&& s_target_final_idx == s_target_start_idx)
			|| (s_stop_after_final_load
				&& (s_flags & AUTOCAMPAIGN_FLAG_VERIFY_ONLY))
			|| s_difficulty < DIFF_A
			|| s_difficulty > DIFF_PA) {
		failRun(CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
			"campaign bounds %d..%d, terminal mode %d, or difficulty %d are invalid",
			s_target_start_idx, s_target_final_idx,
			s_stop_after_final_load, s_difficulty);
		return 0;
	}

	mission_count = s_target_final_idx - s_target_start_idx + 1;
	for (i = 0; i < mission_count; i++) {
		s32 solo_index = s_target_start_idx + i;
		const char *id = g_SoloStages[solo_index].catalog_id;
		catalog_stage_result_t result;
		if (!id || !id[0] || strlen(id) >= CAMPAIGN_RUN_ID_MAX) {
			failRun(CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
				"solo index %d has no bounded catalog identity", solo_index);
			return 0;
		}
		if (!catalogResolveStage(id, &result)
				|| result.stagenum != g_SoloStages[solo_index].stagenum
				|| soloStageGetIndex(result.stagenum) != solo_index) {
			failRun(CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
				"catalog identity '%s' does not resolve exactly to solo index %d",
				id, solo_index);
			return 0;
		}
		ids[i] = id;
	}

	if (!campaignRunInit(&s_run, s_target_start_idx, s_difficulty,
			g_GameFile.name, ids, mission_count)) {
		stopFailed();
		return 0;
	}
	ACLOG("plan profile='%s' difficulty=%d start=%d final=%d missions=%d",
		s_run.profile, s_run.difficulty, s_run.start_solo_index,
		s_run.final_solo_index, s_run.mission_count);
	return publishEvidenceOrFail("running");
}

static s32 queueFirstMission(void)
{
	catalog_stage_result_t stage;
	if (!resolvePlannedStage(0, &stage)) {
		failRun(CAMPAIGN_RUN_FAILURE_STAGE_ID,
			"first catalog mission no longer resolves exactly");
		return 0;
	}

	g_MissionConfig.stageindex = (u8)s_run.start_solo_index;
	g_MissionConfig.difficulty = (u8)s_run.difficulty;
	g_MissionConfig.iscoop = 0;
	g_MissionConfig.isanti = 0;
	g_MissionConfig.pdmode = 0;
	strncpy(g_MissionConfig.stage_id, s_run.expected_ids[0],
		sizeof(g_MissionConfig.stage_id) - 1);
	g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
	g_MissionConfig.stagenum = (u8)stage.stagenum;

	menuhandlerAcceptMission(MENUOP_SET, NULL, NULL);
	if (g_MainChangeToStageNum != stage.stagenum) {
		failRun(CAMPAIGN_RUN_FAILURE_ROUTE,
			"production mission-start entry did not queue first stage '%s'",
			s_run.expected_ids[0]);
		return 0;
	}
	ACLOG("queued first mission through menuhandlerAcceptMission plan=0 idx=%d stage_id='%s' stagenum=0x%02x",
		s_run.start_solo_index, s_run.expected_ids[0], (u32)stage.stagenum);
	return 1;
}

static s32 currentStageMatchesPlan(s32 plan_index,
	s32 *objective_count,
	s32 *active_objective_count)
{
	catalog_stage_result_t expected;
	s32 i;
	s32 active = 0;
	s32 count = objectiveGetCount();
	s32 solo_index = s_run.start_solo_index + plan_index;

	if (!resolvePlannedStage(plan_index, &expected)
			|| soloStageGetIndex(g_Vars.stagenum) != solo_index
			|| (s32)g_MissionConfig.stageindex != solo_index
			|| strcmp(g_MissionConfig.stage_id,
				s_run.expected_ids[plan_index]) != 0
			|| (s32)(u8)g_MissionConfig.stagenum != expected.stagenum
			|| g_Vars.stagenum != expected.stagenum
			|| lvGetDifficulty() != s_run.difficulty) {
		return 0;
	}
	for (i = 0; i < count; i++) {
		if (objectiveGetDifficultyBits(i) & (1u << s_run.difficulty)) {
			active++;
		}
	}
	if (objective_count) {
		*objective_count = count;
	}
	if (active_objective_count) {
		*active_objective_count = active;
	}
	return 1;
}

static s32 completedObjectiveCount(void)
{
	s32 i;
	s32 completed = 0;
	for (i = 0; i < objectiveGetCount(); i++) {
		if ((objectiveGetDifficultyBits(i) & (1u << s_run.difficulty))
				&& g_ObjectiveStatuses[i] == OBJECTIVE_COMPLETE) {
			completed++;
		}
	}
	return completed;
}

static void verifyPersistedCampaign(void)
{
	s32 i;
	refreshPersistedBesttimes();
	for (i = 0; i < s_run.mission_count; i++) {
		if (s_persisted_besttimes[i] == 0) {
			failRun(CAMPAIGN_RUN_FAILURE_SAVE,
				"restart verification found no persisted best time for plan %d '%s'",
				i, s_run.expected_ids[i]);
			return;
		}
		ACLOG("persisted plan=%d idx=%d stage_id='%s' difficulty=%d besttime=%u",
			i, s_run.start_solo_index + i, s_run.expected_ids[i],
			s_run.difficulty, (u32)s_persisted_besttimes[i]);
	}
	if (g_GameFile.autostageindex != SOLOSTAGEINDEX_SKEDARRUINS) {
		failRun(CAMPAIGN_RUN_FAILURE_UNLOCK,
			"restart verification expected autostageindex=%d but found %d",
			SOLOSTAGEINDEX_SKEDARRUINS, g_GameFile.autostageindex);
		return;
	}
	if (!publishEvidenceOrFail("verify_complete")) {
		return;
	}
	setState(AC_STATE_DONE);
	ACLOG("persisted verification complete profile='%s' missions=%d",
		s_run.profile, s_run.mission_count);
	if (smokeHarnessIsActive()) {
		smokeHarnessExit(0, "campaign_verified");
	}
}

static void armCampaignPlanAtDifficulty(int start_solo_index,
	int final_solo_index, int stop_after_final_load, int difficulty,
	unsigned int flags)
{
	const char *save_dir = saveGetDir();
	memset(&s_run, 0, sizeof(s_run));
	memset(s_persisted_besttimes, 0, sizeof(s_persisted_besttimes));
	s_target_start_idx = start_solo_index;
	s_target_final_idx = final_solo_index;
	s_stop_after_final_load = stop_after_final_load ? 1 : 0;
	s_difficulty = difficulty;
	s_flags = flags;
	s_frames_in_state = 0;
	s_dwell_frames = 0;
	s_cutscene_skip_requested = 0;
	snprintf(s_report_path, sizeof(s_report_path), "%s/%s",
		save_dir ? save_dir : ".",
		(flags & AUTOCAMPAIGN_FLAG_VERIFY_ONLY)
			? "campaign_release_verify.json"
			: "campaign_release_run.json");
	setState(AC_STATE_WAIT_PROFILE);
	ACLOG("armed start_idx=%d final_idx=%d terminal=%s difficulty=%d flags=0x%x evidence='%s'",
		s_target_start_idx, s_target_final_idx,
		s_stop_after_final_load ? "load" : "credits",
		s_difficulty, s_flags, s_report_path);
}

void autocampaignArmAtDifficulty(int start_solo_index, int difficulty,
	unsigned int flags)
{
	armCampaignPlanAtDifficulty(start_solo_index,
		SOLOSTAGEINDEX_SKEDARRUINS, 0, difficulty, flags);
}

void autocampaignArmThroughLoadAtDifficulty(int start_solo_index,
	int final_load_solo_index, int difficulty, unsigned int flags)
{
	armCampaignPlanAtDifficulty(start_solo_index, final_load_solo_index,
		1, difficulty, flags);
}

void autocampaignArm(int start_solo_index, unsigned int flags)
{
	autocampaignArmAtDifficulty(start_solo_index, DIFF_A, flags);
}

void autocampaignDisarm(void)
{
	if (s_state == AC_STATE_IDLE) {
		return;
	}
	ACLOG("disarmed completed=%d/%d state=%s",
		s_run.completed_count, s_run.mission_count, kStateName[s_state]);
	s_state = AC_STATE_IDLE;
	s_frames_in_state = 0;
}

int autocampaignIsActive(void)
{
	return s_state != AC_STATE_IDLE
		&& s_state != AC_STATE_DONE
		&& s_state != AC_STATE_FAILED;
}

void autocampaignTick(void)
{
	s32 plan_index;
	s_frames_in_state++;

	switch (s_state) {
	case AC_STATE_WAIT_PROFILE:
		if (presenceIsAgentLoaded() && g_GameFile.name[0]) {
			if (!buildCampaignPlan()) {
				break;
			}
			if (s_flags & AUTOCAMPAIGN_FLAG_VERIFY_ONLY) {
				verifyPersistedCampaign();
			} else {
				setState(AC_STATE_BOOT);
			}
		} else if (s_frames_in_state > AC_PROFILE_TIMEOUT) {
			failRun(CAMPAIGN_RUN_FAILURE_PROFILE,
				"no confirmed agent profile loaded before timeout");
		}
		break;

	case AC_STATE_BOOT:
		if (queueFirstMission()) {
			setState(AC_STATE_WAIT_LOAD);
		}
		break;

	case AC_STATE_WAIT_LOAD:
		plan_index = s_run.completed_count;
		if (!g_MainIsEndscreen && g_Vars.bond && g_Vars.lvframenum > 30u) {
			s32 objective_count = 0;
			s32 active_objective_count = 0;
			s32 terminal_load;
			if (currentStageMatchesPlan(plan_index, &objective_count,
					&active_objective_count)) {
				s32 agent_confirmed = presenceIsAgentLoaded()
					&& strcmp(g_GameFile.name, s_run.profile) == 0;
				s32 social_in_match =
					presenceGetLocalState() == PRESENCE_IN_MATCH;
				if (!campaignRunRecordLoaded(&s_run,
						s_run.start_solo_index + plan_index,
						s_run.expected_ids[plan_index],
						g_Vars.stagenum,
						lvGetDifficulty(),
						objective_count,
						active_objective_count,
						agent_confirmed,
						social_in_match)) {
					stopFailed();
					break;
				}
				ACLOG("loaded plan=%d idx=%d stage_id='%s' stagenum=0x%02x difficulty=%d objectives=%d/%d profile='%s' social=in-match",
					plan_index, s_run.start_solo_index + plan_index,
					s_run.expected_ids[plan_index], (u32)g_Vars.stagenum,
					s_run.difficulty, active_objective_count,
					objective_count, s_run.profile);
				terminal_load = s_stop_after_final_load
					&& plan_index == s_run.mission_count - 1;
				if (!publishEvidenceOrFail(terminal_load
						? "transition_complete" : "running")) {
					break;
				}
				if (terminal_load) {
					setState(AC_STATE_DONE);
					ACLOG("transition verified profile='%s' start=%d target=%d stage_id='%s' completed=%d loaded=%d difficulty=%d",
						s_run.profile, s_run.start_solo_index,
						s_run.final_solo_index,
						s_run.expected_ids[plan_index],
						s_run.completed_count, plan_index + 1,
						s_run.difficulty);
					if (smokeHarnessIsActive()) {
						smokeHarnessExit(0, "campaign_transition_verified");
					}
					break;
				}
				s_dwell_frames = dwellFor(AC_DWELL_PRE_FRAMES);
				setState(AC_STATE_DWELL_PRE);
			} else if (g_MainChangeToStageNum < 0
					&& soloStageGetIndex(g_Vars.stagenum) >= 0) {
				failRun(CAMPAIGN_RUN_FAILURE_STAGE_ORDER,
					"live mission does not match expected plan %d '%s'",
					plan_index, s_run.expected_ids[plan_index]);
			}
		}
		if (s_state == AC_STATE_WAIT_LOAD
				&& s_frames_in_state > AC_LOAD_TIMEOUT) {
			failRun(CAMPAIGN_RUN_FAILURE_TIMEOUT,
				"mission load timed out at plan %d '%s'",
				plan_index,
				plan_index >= 0 && plan_index < s_run.mission_count
					? s_run.expected_ids[plan_index] : "(out-of-range)");
		}
		break;

	case AC_STATE_DWELL_PRE:
		if (playerAnyInCutscene()) {
			if ((s_flags & AUTOCAMPAIGN_FLAG_FAST)
					&& !s_cutscene_skip_requested
					&& s_frames_in_state >= AC_FAST_SKIP_REQUEST_FRAME
					&& playerRequestCutsceneSkip(g_Vars.currentplayernum, false)) {
				s_cutscene_skip_requested = 1;
				ACLOG("requested production cutscene skip plan=%d idx=%d stage_id='%s'",
					s_run.current_plan_index,
					s_run.start_solo_index + s_run.current_plan_index,
					s_run.expected_ids[s_run.current_plan_index]);
			}
			if (s_frames_in_state > AC_CUTSCENE_TIMEOUT) {
				failRun(CAMPAIGN_RUN_FAILURE_TIMEOUT,
					"intro cutscene did not finish before timeout");
			}
			break;
		}
		if (s_dwell_frames > 0) {
			s_dwell_frames--;
		} else {
			setState(AC_STATE_FORCE_END);
		}
		break;

	case AC_STATE_FORCE_END:
	{
		struct saveagentwritereceipt before;
		struct saveagentwritereceipt after;
		s32 forced_count;
		s32 completed_count;
		campaign_run_mission_t *mission;
		plan_index = s_run.current_plan_index;
		if (plan_index < 0 || plan_index >= s_run.mission_count
				|| !currentStageMatchesPlan(plan_index, NULL, NULL)) {
			failRun(CAMPAIGN_RUN_FAILURE_STAGE_ORDER,
				"mission identity changed before authoritative completion");
			break;
		}
		mission = &s_run.missions[plan_index];
		saveGetLastAgentWriteReceipt(&before);
		if (g_Vars.bond) {
			g_Vars.bond->isdead = false;
			g_Vars.bond->aborted = false;
		}
		forced_count = objectivesDebugCompleteCurrentMission();
		completed_count = completedObjectiveCount();
		if (forced_count != mission->active_objective_count
				|| completed_count != mission->active_objective_count
				|| !objectiveIsAllComplete()) {
			failRun(CAMPAIGN_RUN_FAILURE_OBJECTIVES,
				"scoped objective completion covered %d/%d active objectives",
				completed_count, mission->active_objective_count);
			break;
		}
		mainEndStage();
		saveGetLastAgentWriteReceipt(&after);
		if (after.serial == before.serial
				|| strcmp(after.name, s_run.profile) != 0) {
			failRun(CAMPAIGN_RUN_FAILURE_SAVE,
				"endscreen progression did not write the expected agent profile");
			break;
		}
		if (!campaignRunRecordCompleted(&s_run, mission->solo_index,
				mission->stage_id, completed_count, after.serial,
				after.result,
				g_GameFile.besttimes[mission->solo_index][s_run.difficulty],
				presenceGetLocalState() == PRESENCE_IN_MATCH)) {
			stopFailed();
			break;
		}
		ACLOG("completed plan=%d idx=%d stage_id='%s' objectives=%d save_serial=%u save_result=%d besttime=%u social=in-match",
			plan_index, mission->solo_index, mission->stage_id,
			completed_count, (u32)after.serial, after.result,
			(u32)mission->besttime);
		if (!publishEvidenceOrFail("running")) {
			break;
		}
		setState(AC_STATE_WAIT_END);
		break;
	}

	case AC_STATE_WAIT_END:
		if (g_MainIsEndscreen) {
			s_dwell_frames = dwellFor(AC_DWELL_END_FRAMES);
			setState(AC_STATE_DWELL_END);
		} else if (s_frames_in_state > AC_ENDSCREEN_TIMEOUT) {
			failRun(CAMPAIGN_RUN_FAILURE_TIMEOUT,
				"successful completion did not publish the endscreen");
		}
		break;

	case AC_STATE_DWELL_END:
		if (s_dwell_frames > 0) {
			s_dwell_frames--;
		} else {
			setState(AC_STATE_ADVANCE);
		}
		break;

	case AC_STATE_ADVANCE:
	{
		campaign_run_mission_t *mission;
		s32 final;
		s32 next_unlocked = 0;
		const char *route_to;
		catalog_stage_result_t next_stage;
		plan_index = s_run.current_plan_index;
		if (plan_index < 0 || plan_index >= s_run.mission_count
				|| !g_MainIsEndscreen
				|| !pdguiEndscreenHasNextMission()) {
			failRun(CAMPAIGN_RUN_FAILURE_ROUTE,
				"production endscreen cannot advance the completed mission");
			break;
		}
		mission = &s_run.missions[plan_index];
		final = plan_index == s_run.mission_count - 1;
		route_to = final ? "system:credits"
			: s_run.expected_ids[plan_index + 1];
		if (!final) {
			next_unlocked = isStageDifficultyUnlocked(
				mission->solo_index + 1, s_run.difficulty) ? 1 : 0;
			if (!next_unlocked
					|| !resolvePlannedStage(plan_index + 1, &next_stage)) {
				failRun(CAMPAIGN_RUN_FAILURE_UNLOCK,
					"next planned mission '%s' is not exactly resolved and unlocked",
					route_to);
				break;
			}
		}

		pdguiEndscreenNextMission();
		if (final) {
			if (g_MainChangeToStageNum != STAGE_CREDITS) {
				failRun(CAMPAIGN_RUN_FAILURE_ROUTE,
					"final production endscreen action did not queue Credits");
				break;
			}
		} else if ((s32)g_MissionConfig.stageindex != mission->solo_index + 1
				|| strcmp(g_MissionConfig.stage_id, route_to) != 0
				|| (s32)(u8)g_MissionConfig.stagenum != next_stage.stagenum
				|| g_MainChangeToStageNum != next_stage.stagenum) {
			failRun(CAMPAIGN_RUN_FAILURE_ROUTE,
				"production endscreen action did not queue exact next mission '%s'",
				route_to);
			break;
		}
		if (!campaignRunRecordRoute(&s_run, mission->solo_index,
				mission->stage_id, route_to, final, next_unlocked)) {
			stopFailed();
			break;
		}
		ACLOG("routed plan=%d idx=%d stage_id='%s' route_to='%s' unlocked=%d",
			plan_index, mission->solo_index, mission->stage_id,
			route_to, next_unlocked);
		if (!publishEvidenceOrFail("running")) {
			break;
		}
		setState(final ? AC_STATE_WAIT_CREDITS : AC_STATE_WAIT_LOAD);
		break;
	}

	case AC_STATE_WAIT_CREDITS:
		if (g_Vars.stagenum == STAGE_CREDITS
				&& g_Vars.lvframenum > 4u) {
			if (!campaignRunRecordCredits(&s_run, 1,
					presenceGetLocalState() == PRESENCE_ONLINE_IDLE)) {
				stopFailed();
				break;
			}
			if (!publishEvidenceOrFail("complete")) {
				break;
			}
			setState(AC_STATE_DONE);
			ACLOG("credits verified live profile='%s' missions=%d difficulty=%d",
				s_run.profile, s_run.mission_count, s_run.difficulty);
			if (smokeHarnessIsActive()) {
				smokeHarnessExit(0, "campaign_complete");
			}
		} else if (s_frames_in_state > AC_CREDITS_TIMEOUT) {
			failRun(CAMPAIGN_RUN_FAILURE_TIMEOUT,
				"Credits did not become live before timeout");
		}
		break;

	case AC_STATE_IDLE:
	case AC_STATE_DONE:
	case AC_STATE_FAILED:
	case AC_STATE__COUNT:
	default:
		break;
	}
}

void autocampaignInitFromCli(void)
{
	s32 run_requested = sysArgCheck("--auto-campaign");
	s32 verify_requested = sysArgCheck("--auto-campaign-verify");
	s32 through_load_requested =
		sysArgCheck("--auto-campaign-through-load");
	s32 start_index;
	s32 final_load_index;
	s32 difficulty;
	u32 flags = 0;
	enum autocampaign_cli_plan_result plan_result;

	plan_result = autocampaignCliPlanValidate(run_requested, verify_requested,
		through_load_requested);
	if (plan_result == AUTOCAMPAIGN_CLI_PLAN_CONFLICTING_MODES) {
		failRun(CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
			"--auto-campaign and --auto-campaign-verify are mutually exclusive");
		return;
	}
	if (plan_result == AUTOCAMPAIGN_CLI_PLAN_THROUGH_LOAD_WITHOUT_RUN) {
		failRun(CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
			"--auto-campaign-through-load requires --auto-campaign");
		return;
	}
	if (!run_requested && !verify_requested) return;
	start_index = verify_requested
		? sysArgGetInt("--auto-campaign-verify", 0)
		: sysArgGetInt("--auto-campaign", 0);
	difficulty = sysArgGetInt("--auto-campaign-difficulty", DIFF_A);
	if (sysArgCheck("--auto-campaign-fast")) {
		flags |= AUTOCAMPAIGN_FLAG_FAST;
	}
	if (verify_requested) {
		flags |= AUTOCAMPAIGN_FLAG_VERIFY_ONLY;
	}
	final_load_index = sysArgGetInt("--auto-campaign-through-load", -1);
	if (run_requested && !verify_requested && through_load_requested) {
		autocampaignArmThroughLoadAtDifficulty(start_index,
			final_load_index, difficulty, flags);
	} else {
		autocampaignArmAtDifficulty(start_index, difficulty, flags);
	}
}
