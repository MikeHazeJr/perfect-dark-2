#include "catch.hpp"

extern "C" {
#include "campaign_run.h"
}

#include <cstring>

static const char *const kCampaignPlan[] = {
	"base:defection",
	"base:investigation",
	"base:extraction",
};

TEST_CASE("campaign run accepts one exact ordered persisted route to Credits",
	"[campaign-run][t-tests-002]")
{
	campaign_run_t run;
	REQUIRE(campaignRunInit(&run, 0, 0, "smoke", kCampaignPlan, 3) == 1);

	for (int i = 0; i < 3; i++) {
		REQUIRE(campaignRunRecordLoaded(&run, i, kCampaignPlan[i],
			0x30 + i, 0, 4, 3, 1, 1) == 1);
		REQUIRE(campaignRunRecordCompleted(&run, i, kCampaignPlan[i],
			3, static_cast<u32>(i + 1), 0, static_cast<u16>(10 + i), 1) == 1);
		const bool final = i == 2;
		REQUIRE(campaignRunRecordRoute(&run, i, kCampaignPlan[i],
			final ? "system:credits" : kCampaignPlan[i + 1],
			final ? 1 : 0, final ? 0 : 1) == 1);
	}

	REQUIRE(campaignRunRecordCredits(&run, 1, 1) == 1);
	REQUIRE(run.phase == CAMPAIGN_RUN_PHASE_COMPLETE);
	REQUIRE(run.completed_count == 3);
	REQUIRE(run.credits_verified == 1);
}

TEST_CASE("campaign run fails closed on skipped or substituted stages",
	"[campaign-run][t-tests-002]")
{
	campaign_run_t skipped;
	REQUIRE(campaignRunInit(&skipped, 0, 0, "smoke", kCampaignPlan, 3) == 1);
	REQUIRE(campaignRunRecordLoaded(&skipped, 1, kCampaignPlan[1],
		0x33, 0, 3, 2, 1, 1) == 0);
	REQUIRE(skipped.failure == CAMPAIGN_RUN_FAILURE_STAGE_ORDER);
	REQUIRE(skipped.phase == CAMPAIGN_RUN_PHASE_FAILED);

	campaign_run_t substituted;
	REQUIRE(campaignRunInit(&substituted, 0, 0, "smoke", kCampaignPlan, 3) == 1);
	REQUIRE(campaignRunRecordLoaded(&substituted, 0, "base:wrong",
		0x30, 0, 3, 2, 1, 1) == 0);
	REQUIRE(substituted.failure == CAMPAIGN_RUN_FAILURE_STAGE_ID);
}

TEST_CASE("campaign run rejects missing objective save unlock and social evidence",
	"[campaign-run][t-tests-002]")
{
	campaign_run_t objectives;
	REQUIRE(campaignRunInit(&objectives, 0, 0, "smoke", kCampaignPlan, 3) == 1);
	REQUIRE(campaignRunRecordLoaded(&objectives, 0, kCampaignPlan[0],
		0x30, 0, 0, 0, 1, 1) == 0);
	REQUIRE(objectives.failure == CAMPAIGN_RUN_FAILURE_OBJECTIVES);

	campaign_run_t save;
	REQUIRE(campaignRunInit(&save, 0, 0, "smoke", kCampaignPlan, 3) == 1);
	REQUIRE(campaignRunRecordLoaded(&save, 0, kCampaignPlan[0],
		0x30, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&save, 0, kCampaignPlan[0],
		2, 0, -1, 1, 1) == 0);
	REQUIRE(save.failure == CAMPAIGN_RUN_FAILURE_SAVE);

	campaign_run_t unlock;
	REQUIRE(campaignRunInit(&unlock, 0, 0, "smoke", kCampaignPlan, 3) == 1);
	REQUIRE(campaignRunRecordLoaded(&unlock, 0, kCampaignPlan[0],
		0x30, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&unlock, 0, kCampaignPlan[0],
		2, 1, 0, 1, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&unlock, 0, kCampaignPlan[0],
		kCampaignPlan[1], 0, 0) == 0);
	REQUIRE(unlock.failure == CAMPAIGN_RUN_FAILURE_UNLOCK);

	campaign_run_t social;
	REQUIRE(campaignRunInit(&social, 0, 0, "smoke", kCampaignPlan, 3) == 1);
	REQUIRE(campaignRunRecordLoaded(&social, 0, kCampaignPlan[0],
		0x30, 0, 3, 2, 1, 0) == 0);
	REQUIRE(social.failure == CAMPAIGN_RUN_FAILURE_SOCIAL_STATE);
}

TEST_CASE("campaign run rejects early or uncommitted Credits",
	"[campaign-run][t-tests-002]")
{
	campaign_run_t run;
	REQUIRE(campaignRunInit(&run, 0, 0, "smoke", kCampaignPlan, 1) == 1);
	REQUIRE(campaignRunRecordCredits(&run, 1, 1) == 0);
	REQUIRE(run.failure == CAMPAIGN_RUN_FAILURE_EVENT_ORDER);

	REQUIRE(std::strcmp(campaignRunPhaseName(CAMPAIGN_RUN_PHASE_COMPLETE),
		"complete") == 0);
	REQUIRE(std::strcmp(campaignRunFailureName(CAMPAIGN_RUN_FAILURE_SAVE),
		"save") == 0);
}

TEST_CASE("campaign plan rejects duplicate identities and truncated profiles",
	"[campaign-run][t-tests-002]")
{
	const char *const duplicate[] = { "base:defection", "base:defection" };
	campaign_run_t run;
	REQUIRE(campaignRunInit(&run, 0, 0, "smoke", duplicate, 2) == 0);
	REQUIRE(run.failure == CAMPAIGN_RUN_FAILURE_INVALID_PLAN);

	char long_profile[CAMPAIGN_RUN_PROFILE_MAX + 8];
	std::memset(long_profile, 'x', sizeof(long_profile));
	long_profile[sizeof(long_profile) - 1] = '\0';
	REQUIRE(campaignRunInit(&run, 0, 0, long_profile, kCampaignPlan, 3) == 0);
	REQUIRE(run.failure == CAMPAIGN_RUN_FAILURE_INVALID_PLAN);
}

TEST_CASE("campaign run rejects a stale save receipt",
	"[campaign-run][t-tests-002]")
{
	campaign_run_t run;
	REQUIRE(campaignRunInit(&run, 0, 0, "smoke", kCampaignPlan, 3) == 1);
	REQUIRE(campaignRunRecordLoaded(&run, 0, kCampaignPlan[0],
		0x30, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&run, 0, kCampaignPlan[0],
		2, 9, 0, 1, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&run, 0, kCampaignPlan[0],
		kCampaignPlan[1], 0, 1) == 1);
	REQUIRE(campaignRunRecordLoaded(&run, 1, kCampaignPlan[1],
		0x31, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&run, 1, kCampaignPlan[1],
		2, 9, 0, 1, 1) == 0);
	REQUIRE(run.failure == CAMPAIGN_RUN_FAILURE_SAVE);
}
