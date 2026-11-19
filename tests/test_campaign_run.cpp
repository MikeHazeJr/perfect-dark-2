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

// The four special assignments have no next mission: each one ends at the main
// menu, and the next is started from there. A plan switched to that terminal
// must see the menu after every mission and never accepts Credits.
static const char *const kSpecialPlan[] = {
	"base:mbr",
	"base:maiansos",
	"base:war",
	"base:duel",
};

TEST_CASE("special assignment plan ends every mission at the main menu",
	"[campaign-run][specials][t-tests-002]")
{
	campaign_run_t run;
	REQUIRE(campaignRunInit(&run, 17, 0, "smoke", kSpecialPlan, 4) == 1);
	REQUIRE(run.terminal == CAMPAIGN_RUN_TERMINAL_CREDITS);
	REQUIRE(campaignRunSetTerminalMenu(&run) == 1);
	REQUIRE(run.terminal == CAMPAIGN_RUN_TERMINAL_MENU);

	for (int i = 0; i < 4; i++) {
		REQUIRE(campaignRunRecordLoaded(&run, 17 + i, kSpecialPlan[i],
			0x40 + i, 0, 3, 2, 1, 1) == 1);
		REQUIRE(campaignRunRecordCompleted(&run, 17 + i, kSpecialPlan[i],
			2, static_cast<u32>(i + 1), 0, static_cast<u16>(20 + i), 1) == 1);
		// Nothing has to be unlocked by a special assignment.
		REQUIRE(campaignRunRecordRoute(&run, 17 + i, kSpecialPlan[i],
			"system:menu", 0, 0) == 1);
		REQUIRE(run.phase == CAMPAIGN_RUN_PHASE_WAIT_MENU);
		REQUIRE(run.missions[i].route_is_menu == 1);
		REQUIRE(campaignRunRecordMenu(&run, 1, 1) == 1);
		REQUIRE(run.missions[i].menu_reached == 1);
		REQUIRE(run.phase == (i == 3 ? CAMPAIGN_RUN_PHASE_COMPLETE
			: CAMPAIGN_RUN_PHASE_WAIT_LOAD));
	}

	REQUIRE(run.completed_count == 4);
	REQUIRE(run.menu_verified == 1);
	REQUIRE(run.credits_verified == 0);
	REQUIRE(std::strcmp(campaignRunTerminalName(run.terminal), "menu") == 0);
	REQUIRE(std::strcmp(campaignRunPhaseName(CAMPAIGN_RUN_PHASE_WAIT_MENU),
		"wait_menu") == 0);
}

TEST_CASE("special assignment plan fails closed on a wrong route or a skipped menu",
	"[campaign-run][specials][t-tests-002]")
{
	// Credits is not a way out of a special assignment.
	campaign_run_t credits;
	REQUIRE(campaignRunInit(&credits, 17, 0, "smoke", kSpecialPlan, 1) == 1);
	REQUIRE(campaignRunSetTerminalMenu(&credits) == 1);
	REQUIRE(campaignRunRecordLoaded(&credits, 17, kSpecialPlan[0],
		0x40, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&credits, 17, kSpecialPlan[0],
		2, 1, 0, 9, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&credits, 17, kSpecialPlan[0],
		"system:credits", 1, 0) == 0);
	REQUIRE(credits.failure == CAMPAIGN_RUN_FAILURE_ROUTE);

	// Nor is another mission.
	campaign_run_t chained;
	REQUIRE(campaignRunInit(&chained, 17, 0, "smoke", kSpecialPlan, 2) == 1);
	REQUIRE(campaignRunSetTerminalMenu(&chained) == 1);
	REQUIRE(campaignRunRecordLoaded(&chained, 17, kSpecialPlan[0],
		0x40, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&chained, 17, kSpecialPlan[0],
		2, 1, 0, 9, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&chained, 17, kSpecialPlan[0],
		kSpecialPlan[1], 0, 1) == 0);
	REQUIRE(chained.failure == CAMPAIGN_RUN_FAILURE_ROUTE);

	// The next mission cannot load before the menu was seen.
	campaign_run_t skipped;
	REQUIRE(campaignRunInit(&skipped, 17, 0, "smoke", kSpecialPlan, 2) == 1);
	REQUIRE(campaignRunSetTerminalMenu(&skipped) == 1);
	REQUIRE(campaignRunRecordLoaded(&skipped, 17, kSpecialPlan[0],
		0x40, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&skipped, 17, kSpecialPlan[0],
		2, 1, 0, 9, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&skipped, 17, kSpecialPlan[0],
		"system:menu", 0, 0) == 1);
	REQUIRE(campaignRunRecordLoaded(&skipped, 18, kSpecialPlan[1],
		0x41, 0, 3, 2, 1, 1) == 0);
	REQUIRE(skipped.failure == CAMPAIGN_RUN_FAILURE_EVENT_ORDER);

	// A menu that never became live, or a presence that stayed in a match.
	campaign_run_t dead;
	REQUIRE(campaignRunInit(&dead, 17, 0, "smoke", kSpecialPlan, 1) == 1);
	REQUIRE(campaignRunSetTerminalMenu(&dead) == 1);
	REQUIRE(campaignRunRecordLoaded(&dead, 17, kSpecialPlan[0],
		0x40, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&dead, 17, kSpecialPlan[0],
		2, 1, 0, 9, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&dead, 17, kSpecialPlan[0],
		"system:menu", 0, 0) == 1);
	REQUIRE(campaignRunRecordMenu(&dead, 0, 1) == 0);
	REQUIRE(dead.failure == CAMPAIGN_RUN_FAILURE_ROUTE);

	campaign_run_t busy;
	REQUIRE(campaignRunInit(&busy, 17, 0, "smoke", kSpecialPlan, 1) == 1);
	REQUIRE(campaignRunSetTerminalMenu(&busy) == 1);
	REQUIRE(campaignRunRecordLoaded(&busy, 17, kSpecialPlan[0],
		0x40, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&busy, 17, kSpecialPlan[0],
		2, 1, 0, 9, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&busy, 17, kSpecialPlan[0],
		"system:menu", 0, 0) == 1);
	REQUIRE(campaignRunRecordMenu(&busy, 1, 0) == 0);
	REQUIRE(busy.failure == CAMPAIGN_RUN_FAILURE_SOCIAL_STATE);
}

TEST_CASE("the two plan terminals do not accept each other's events",
	"[campaign-run][specials][t-tests-002]")
{
	// A campaign plan never reaches a menu event.
	campaign_run_t campaign;
	REQUIRE(campaignRunInit(&campaign, 0, 0, "smoke", kCampaignPlan, 1) == 1);
	REQUIRE(campaignRunRecordLoaded(&campaign, 0, kCampaignPlan[0],
		0x30, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&campaign, 0, kCampaignPlan[0],
		2, 1, 0, 9, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&campaign, 0, kCampaignPlan[0],
		"system:menu", 0, 0) == 0);
	REQUIRE(campaign.failure == CAMPAIGN_RUN_FAILURE_ROUTE);

	campaign_run_t menu_on_campaign;
	REQUIRE(campaignRunInit(&menu_on_campaign, 0, 0, "smoke", kCampaignPlan, 1) == 1);
	REQUIRE(campaignRunRecordMenu(&menu_on_campaign, 1, 1) == 0);
	REQUIRE(menu_on_campaign.failure == CAMPAIGN_RUN_FAILURE_EVENT_ORDER);

	// The terminal is fixed once the first mission has loaded.
	campaign_run_t late;
	REQUIRE(campaignRunInit(&late, 17, 0, "smoke", kSpecialPlan, 1) == 1);
	REQUIRE(campaignRunRecordLoaded(&late, 17, kSpecialPlan[0],
		0x40, 0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunSetTerminalMenu(&late) == 0);
	REQUIRE(late.failure == CAMPAIGN_RUN_FAILURE_EVENT_ORDER);
}
