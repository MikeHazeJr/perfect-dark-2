#include "catch.hpp"

extern "C" {
#include "autocampaign_cli_plan.h"
}

TEST_CASE("campaign CLI rejects invalid option relationships before arming",
	"[campaign-run][b-1062][t-tests-002]")
{
	REQUIRE(autocampaignCliPlanValidate(1, 1, 0)
		== AUTOCAMPAIGN_CLI_PLAN_CONFLICTING_MODES);
	REQUIRE(autocampaignCliPlanValidate(0, 1, 1)
		== AUTOCAMPAIGN_CLI_PLAN_THROUGH_LOAD_WITHOUT_RUN);
	REQUIRE(autocampaignCliPlanValidate(0, 0, 1)
		== AUTOCAMPAIGN_CLI_PLAN_THROUGH_LOAD_WITHOUT_RUN);
}

TEST_CASE("campaign CLI accepts each supported run shape",
	"[campaign-run][b-1062][t-tests-002]")
{
	REQUIRE(autocampaignCliPlanValidate(0, 0, 0)
		== AUTOCAMPAIGN_CLI_PLAN_OK);
	REQUIRE(autocampaignCliPlanValidate(1, 0, 0)
		== AUTOCAMPAIGN_CLI_PLAN_OK);
	REQUIRE(autocampaignCliPlanValidate(1, 0, 1)
		== AUTOCAMPAIGN_CLI_PLAN_OK);
	REQUIRE(autocampaignCliPlanValidate(0, 1, 0)
		== AUTOCAMPAIGN_CLI_PLAN_OK);
}
