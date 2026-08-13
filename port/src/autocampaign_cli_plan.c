#include "autocampaign_cli_plan.h"

enum autocampaign_cli_plan_result autocampaignCliPlanValidate(
		s32 run_requested, s32 verify_requested, s32 through_load_requested)
{
	if (run_requested && verify_requested) {
		return AUTOCAMPAIGN_CLI_PLAN_CONFLICTING_MODES;
	}
	if (through_load_requested && !run_requested) {
		return AUTOCAMPAIGN_CLI_PLAN_THROUGH_LOAD_WITHOUT_RUN;
	}
	return AUTOCAMPAIGN_CLI_PLAN_OK;
}
