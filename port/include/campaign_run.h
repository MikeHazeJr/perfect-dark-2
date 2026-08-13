#ifndef _IN_CAMPAIGN_RUN_H
#define _IN_CAMPAIGN_RUN_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMPAIGN_RUN_MAX_MISSIONS 32
#define CAMPAIGN_RUN_ID_MAX       64
#define CAMPAIGN_RUN_PROFILE_MAX  32
#define CAMPAIGN_RUN_DETAIL_MAX   160

typedef enum campaign_run_phase {
	CAMPAIGN_RUN_PHASE_IDLE = 0,
	CAMPAIGN_RUN_PHASE_WAIT_LOAD,
	CAMPAIGN_RUN_PHASE_WAIT_COMPLETE,
	CAMPAIGN_RUN_PHASE_WAIT_ROUTE,
	CAMPAIGN_RUN_PHASE_WAIT_CREDITS,
	CAMPAIGN_RUN_PHASE_COMPLETE,
	CAMPAIGN_RUN_PHASE_FAILED,
} campaign_run_phase_t;

typedef enum campaign_run_failure {
	CAMPAIGN_RUN_FAILURE_NONE = 0,
	CAMPAIGN_RUN_FAILURE_INVALID_PLAN,
	CAMPAIGN_RUN_FAILURE_EVENT_ORDER,
	CAMPAIGN_RUN_FAILURE_STAGE_ORDER,
	CAMPAIGN_RUN_FAILURE_STAGE_ID,
	CAMPAIGN_RUN_FAILURE_DIFFICULTY,
	CAMPAIGN_RUN_FAILURE_PROFILE,
	CAMPAIGN_RUN_FAILURE_SOCIAL_STATE,
	CAMPAIGN_RUN_FAILURE_OBJECTIVES,
	CAMPAIGN_RUN_FAILURE_COMPLETION,
	CAMPAIGN_RUN_FAILURE_SAVE,
	CAMPAIGN_RUN_FAILURE_UNLOCK,
	CAMPAIGN_RUN_FAILURE_ROUTE,
	CAMPAIGN_RUN_FAILURE_CREDITS,
	CAMPAIGN_RUN_FAILURE_TIMEOUT,
	CAMPAIGN_RUN_FAILURE_EVIDENCE,
} campaign_run_failure_t;

typedef struct campaign_run_mission {
	s32 solo_index;
	s32 runtime_stagenum;
	s32 difficulty;
	s32 objective_count;
	s32 active_objective_count;
	s32 completed_objective_count;
	s32 loaded_social_in_match;
	s32 completion_social_in_match;
	u32 save_serial;
	s32 save_result;
	u16 besttime;
	s32 next_unlocked;
	s32 route_is_credits;
	s32 loaded;
	s32 completed;
	s32 routed;
	char stage_id[CAMPAIGN_RUN_ID_MAX];
	char route_to[CAMPAIGN_RUN_ID_MAX];
} campaign_run_mission_t;

typedef struct campaign_run {
	campaign_run_phase_t phase;
	campaign_run_failure_t failure;
	s32 start_solo_index;
	s32 final_solo_index;
	s32 difficulty;
	s32 mission_count;
	s32 current_plan_index;
	s32 completed_count;
	s32 credits_verified;
	char profile[CAMPAIGN_RUN_PROFILE_MAX];
	char failure_detail[CAMPAIGN_RUN_DETAIL_MAX];
	char expected_ids[CAMPAIGN_RUN_MAX_MISSIONS][CAMPAIGN_RUN_ID_MAX];
	campaign_run_mission_t missions[CAMPAIGN_RUN_MAX_MISSIONS];
} campaign_run_t;

int campaignRunInit(campaign_run_t *run,
	s32 start_solo_index,
	s32 difficulty,
	const char *profile,
	const char *const *expected_ids,
	s32 mission_count);

int campaignRunRecordLoaded(campaign_run_t *run,
	s32 solo_index,
	const char *stage_id,
	s32 runtime_stagenum,
	s32 difficulty,
	s32 objective_count,
	s32 active_objective_count,
	s32 agent_confirmed,
	s32 social_in_match);

int campaignRunRecordCompleted(campaign_run_t *run,
	s32 solo_index,
	const char *stage_id,
	s32 completed_objective_count,
	u32 save_serial,
	s32 save_result,
	u16 besttime,
	s32 social_in_match);

int campaignRunRecordRoute(campaign_run_t *run,
	s32 solo_index,
	const char *stage_id,
	const char *route_to,
	s32 route_is_credits,
	s32 next_unlocked);

int campaignRunRecordCredits(campaign_run_t *run,
	s32 credits_live,
	s32 social_idle);

int campaignRunFail(campaign_run_t *run,
	campaign_run_failure_t failure,
	const char *detail);

const char *campaignRunPhaseName(campaign_run_phase_t phase);
const char *campaignRunFailureName(campaign_run_failure_t failure);

#ifdef __cplusplus
}
#endif

#endif /* _IN_CAMPAIGN_RUN_H */
