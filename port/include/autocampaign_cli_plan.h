#ifndef _IN_AUTOCAMPAIGN_CLI_PLAN_H
#define _IN_AUTOCAMPAIGN_CLI_PLAN_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

enum autocampaign_cli_plan_result {
	AUTOCAMPAIGN_CLI_PLAN_OK = 0,
	AUTOCAMPAIGN_CLI_PLAN_CONFLICTING_MODES,
	AUTOCAMPAIGN_CLI_PLAN_THROUGH_LOAD_WITHOUT_RUN,
};

/** Validate option relationships before any campaign runner is armed. */
enum autocampaign_cli_plan_result autocampaignCliPlanValidate(
		s32 run_requested, s32 verify_requested, s32 through_load_requested);

#ifdef __cplusplus
}
#endif

#endif /* _IN_AUTOCAMPAIGN_CLI_PLAN_H */
