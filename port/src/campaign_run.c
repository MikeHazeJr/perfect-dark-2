#include <string.h>

#include "campaign_run.h"

static void copyText(char *dst, s32 dst_size, const char *src)
{
	if (!dst || dst_size <= 0) {
		return;
	}

	if (!src) {
		src = "";
	}

	strncpy(dst, src, (size_t)dst_size - 1u);
	dst[dst_size - 1] = '\0';
}

const char *campaignRunPhaseName(campaign_run_phase_t phase)
{
	switch (phase) {
	case CAMPAIGN_RUN_PHASE_IDLE: return "idle";
	case CAMPAIGN_RUN_PHASE_WAIT_LOAD: return "wait_load";
	case CAMPAIGN_RUN_PHASE_WAIT_COMPLETE: return "wait_complete";
	case CAMPAIGN_RUN_PHASE_WAIT_ROUTE: return "wait_route";
	case CAMPAIGN_RUN_PHASE_WAIT_CREDITS: return "wait_credits";
	case CAMPAIGN_RUN_PHASE_COMPLETE: return "complete";
	case CAMPAIGN_RUN_PHASE_FAILED: return "failed";
	default: return "unknown";
	}
}

const char *campaignRunFailureName(campaign_run_failure_t failure)
{
	switch (failure) {
	case CAMPAIGN_RUN_FAILURE_NONE: return "none";
	case CAMPAIGN_RUN_FAILURE_INVALID_PLAN: return "invalid_plan";
	case CAMPAIGN_RUN_FAILURE_EVENT_ORDER: return "event_order";
	case CAMPAIGN_RUN_FAILURE_STAGE_ORDER: return "stage_order";
	case CAMPAIGN_RUN_FAILURE_STAGE_ID: return "stage_id";
	case CAMPAIGN_RUN_FAILURE_DIFFICULTY: return "difficulty";
	case CAMPAIGN_RUN_FAILURE_PROFILE: return "profile";
	case CAMPAIGN_RUN_FAILURE_SOCIAL_STATE: return "social_state";
	case CAMPAIGN_RUN_FAILURE_OBJECTIVES: return "objectives";
	case CAMPAIGN_RUN_FAILURE_COMPLETION: return "completion";
	case CAMPAIGN_RUN_FAILURE_SAVE: return "save";
	case CAMPAIGN_RUN_FAILURE_UNLOCK: return "unlock";
	case CAMPAIGN_RUN_FAILURE_ROUTE: return "route";
	case CAMPAIGN_RUN_FAILURE_CREDITS: return "credits";
	case CAMPAIGN_RUN_FAILURE_TIMEOUT: return "timeout";
	case CAMPAIGN_RUN_FAILURE_EVIDENCE: return "evidence";
	default: return "unknown";
	}
}

int campaignRunFail(campaign_run_t *run,
	campaign_run_failure_t failure,
	const char *detail)
{
	if (!run) {
		return 0;
	}

	if (run->phase != CAMPAIGN_RUN_PHASE_FAILED) {
		run->failure = failure == CAMPAIGN_RUN_FAILURE_NONE
			? CAMPAIGN_RUN_FAILURE_EVENT_ORDER : failure;
		copyText(run->failure_detail, sizeof(run->failure_detail), detail);
		run->phase = CAMPAIGN_RUN_PHASE_FAILED;
	}

	return 0;
}

static int idsEqual(const char *a, const char *b)
{
	return a && b && a[0] && b[0] && strcmp(a, b) == 0;
}

int campaignRunInit(campaign_run_t *run,
	s32 start_solo_index,
	s32 difficulty,
	const char *profile,
	const char *const *expected_ids,
	s32 mission_count)
{
	s32 i;
	s32 j;

	if (!run) {
		return 0;
	}

	memset(run, 0, sizeof(*run));
	run->phase = CAMPAIGN_RUN_PHASE_IDLE;
	run->current_plan_index = -1;

	if (start_solo_index < 0
			|| difficulty < 0
			|| !profile
			|| !profile[0]
			|| !expected_ids
			|| mission_count <= 0
			|| mission_count > CAMPAIGN_RUN_MAX_MISSIONS) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
			"invalid start, difficulty, profile, or mission count");
	}

	copyText(run->profile, sizeof(run->profile), profile);
	if (strcmp(run->profile, profile) != 0) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
			"profile name exceeds campaign run capacity");
	}

	for (i = 0; i < mission_count; i++) {
		if (!expected_ids[i] || !expected_ids[i][0]) {
			return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
				"campaign plan contains an empty stage ID");
		}
		copyText(run->expected_ids[i], sizeof(run->expected_ids[i]),
			expected_ids[i]);
		if (strcmp(run->expected_ids[i], expected_ids[i]) != 0) {
			return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
				"campaign stage ID exceeds run capacity");
		}
		for (j = 0; j < i; j++) {
			if (strcmp(run->expected_ids[j], run->expected_ids[i]) == 0) {
				return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
					"campaign plan contains a duplicate stage ID");
			}
		}
		run->missions[i].solo_index = start_solo_index + i;
		run->missions[i].difficulty = difficulty;
		copyText(run->missions[i].stage_id,
			sizeof(run->missions[i].stage_id), expected_ids[i]);
	}

	run->start_solo_index = start_solo_index;
	run->final_solo_index = start_solo_index + mission_count - 1;
	run->difficulty = difficulty;
	run->mission_count = mission_count;
	run->phase = CAMPAIGN_RUN_PHASE_WAIT_LOAD;
	return 1;
}

int campaignRunRecordLoaded(campaign_run_t *run,
	s32 solo_index,
	const char *stage_id,
	s32 runtime_stagenum,
	s32 difficulty,
	s32 objective_count,
	s32 active_objective_count,
	s32 agent_confirmed,
	s32 social_in_match)
{
	s32 plan_index;
	campaign_run_mission_t *mission;

	if (!run || run->phase != CAMPAIGN_RUN_PHASE_WAIT_LOAD) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_EVENT_ORDER,
			"mission loaded outside wait-load phase");
	}

	plan_index = run->completed_count;
	if (plan_index < 0 || plan_index >= run->mission_count) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_STAGE_ORDER,
			"mission loaded after the campaign plan ended");
	}
	if (solo_index != run->start_solo_index + plan_index) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_STAGE_ORDER,
			"loaded solo index skipped or repeated a planned mission");
	}
	if (!idsEqual(stage_id, run->expected_ids[plan_index])) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_STAGE_ID,
			"loaded catalog stage ID does not match the plan");
	}
	if (difficulty != run->difficulty) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_DIFFICULTY,
			"loaded mission difficulty changed during the run");
	}
	if (!agent_confirmed || !run->profile[0]) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_PROFILE,
			"mission loaded without the planned agent profile");
	}
	if (!social_in_match) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_SOCIAL_STATE,
			"presence did not enter in-match for the loaded mission");
	}
	if (objective_count <= 0
			|| active_objective_count <= 0
			|| active_objective_count > objective_count) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_OBJECTIVES,
			"loaded mission has no valid difficulty-active objectives");
	}

	mission = &run->missions[plan_index];
	if (mission->loaded) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_STAGE_ORDER,
			"campaign mission was loaded more than once");
	}
	mission->solo_index = solo_index;
	mission->runtime_stagenum = runtime_stagenum;
	mission->difficulty = difficulty;
	mission->objective_count = objective_count;
	mission->active_objective_count = active_objective_count;
	mission->loaded_social_in_match = social_in_match ? 1 : 0;
	mission->loaded = 1;
	copyText(mission->stage_id, sizeof(mission->stage_id), stage_id);
	run->current_plan_index = plan_index;
	run->phase = CAMPAIGN_RUN_PHASE_WAIT_COMPLETE;
	return 1;
}

int campaignRunRecordCompleted(campaign_run_t *run,
	s32 solo_index,
	const char *stage_id,
	s32 completed_objective_count,
	u32 save_serial,
	s32 save_result,
	u16 besttime,
	s32 social_in_match)
{
	campaign_run_mission_t *mission;

	if (!run || run->phase != CAMPAIGN_RUN_PHASE_WAIT_COMPLETE
			|| run->current_plan_index < 0
			|| run->current_plan_index >= run->mission_count) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_EVENT_ORDER,
			"mission completed outside wait-complete phase");
	}

	mission = &run->missions[run->current_plan_index];
	if (solo_index != mission->solo_index) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_STAGE_ORDER,
			"completed solo index differs from the loaded mission");
	}
	if (!idsEqual(stage_id, mission->stage_id)) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_STAGE_ID,
			"completed catalog stage ID differs from the loaded mission");
	}
	if (completed_objective_count != mission->active_objective_count) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_COMPLETION,
			"debug completion did not cover every active objective");
	}
	if (!social_in_match) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_SOCIAL_STATE,
			"presence left in-match before mission completion committed");
	}
	if (save_serial == 0 || save_result != 0) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_SAVE,
			"authoritative agent save did not commit successfully");
	}
	if (run->current_plan_index > 0
			&& save_serial <= run->missions[run->current_plan_index - 1].save_serial) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_SAVE,
			"agent save receipt did not advance monotonically");
	}
	if (besttime == 0) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_COMPLETION,
			"mission completion did not publish a nonzero best time");
	}

	mission->completed_objective_count = completed_objective_count;
	mission->completion_social_in_match = social_in_match ? 1 : 0;
	mission->save_serial = save_serial;
	mission->save_result = save_result;
	mission->besttime = besttime;
	mission->completed = 1;
	run->completed_count++;
	run->phase = CAMPAIGN_RUN_PHASE_WAIT_ROUTE;
	return 1;
}

int campaignRunRecordRoute(campaign_run_t *run,
	s32 solo_index,
	const char *stage_id,
	const char *route_to,
	s32 route_is_credits,
	s32 next_unlocked)
{
	campaign_run_mission_t *mission;
	s32 current;

	if (!run || run->phase != CAMPAIGN_RUN_PHASE_WAIT_ROUTE
			|| run->current_plan_index < 0
			|| run->current_plan_index >= run->mission_count) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_EVENT_ORDER,
			"campaign route recorded outside wait-route phase");
	}

	current = run->current_plan_index;
	mission = &run->missions[current];
	if (solo_index != mission->solo_index || !idsEqual(stage_id, mission->stage_id)) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_ROUTE,
			"route source differs from the completed mission");
	}

	if (current == run->mission_count - 1) {
		if (!route_is_credits || !route_to || strcmp(route_to, "system:credits") != 0) {
			return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_ROUTE,
				"final campaign mission did not route to Credits");
		}
		mission->route_is_credits = 1;
		run->phase = CAMPAIGN_RUN_PHASE_WAIT_CREDITS;
	} else {
		if (route_is_credits
				|| !idsEqual(route_to, run->expected_ids[current + 1])) {
			return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_ROUTE,
				"campaign route skipped or substituted the next catalog mission");
		}
		if (!next_unlocked) {
			return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_UNLOCK,
				"completed mission did not unlock the planned next mission");
		}
		mission->next_unlocked = 1;
		run->phase = CAMPAIGN_RUN_PHASE_WAIT_LOAD;
	}

	mission->routed = 1;
	copyText(mission->route_to, sizeof(mission->route_to), route_to);
	return 1;
}

int campaignRunRecordCredits(campaign_run_t *run,
	s32 credits_live,
	s32 social_idle)
{
	s32 i;

	if (!run || run->phase != CAMPAIGN_RUN_PHASE_WAIT_CREDITS) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_EVENT_ORDER,
			"Credits recorded outside wait-credits phase");
	}
	if (!credits_live) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_CREDITS,
			"Credits transition was requested but never became live");
	}
	if (!social_idle) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_SOCIAL_STATE,
			"presence did not return to online-idle for Credits");
	}
	if (run->completed_count != run->mission_count) {
		return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_CREDITS,
			"Credits became live before every planned mission completed");
	}
	for (i = 0; i < run->mission_count; i++) {
		if (!run->missions[i].loaded
				|| !run->missions[i].completed
				|| !run->missions[i].routed) {
			return campaignRunFail(run, CAMPAIGN_RUN_FAILURE_CREDITS,
				"campaign evidence is incomplete at Credits");
		}
	}

	run->credits_verified = 1;
	run->phase = CAMPAIGN_RUN_PHASE_COMPLETE;
	return 1;
}
