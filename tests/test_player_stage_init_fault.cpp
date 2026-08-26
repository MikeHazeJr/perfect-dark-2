#include "catch.hpp"

#include <cstring>

extern "C" {
#include "player_stage_init_fault.h"
}

TEST_CASE("stage-init fault plan accepts exact bounded reset and spawn forms",
		"[player-init][transaction][fault][B-1104][T-ENGINE-004]")
{
	player_stage_init_fault_plan_t plan;

	REQUIRE(playerStageInitFaultPlanParse("reset:0", 4, &plan) ==
		PLAYER_STAGE_INIT_FAULT_OK);
	REQUIRE(plan.phase == PLAYER_STAGE_INIT_FAULT_RESET);
	REQUIRE(plan.playernum == 0);
	REQUIRE(plan.armed);
	REQUIRE_FALSE(plan.consumed);

	REQUIRE(playerStageInitFaultPlanParse("spawn:3", 4, &plan) ==
		PLAYER_STAGE_INIT_FAULT_OK);
	REQUIRE(plan.phase == PLAYER_STAGE_INIT_FAULT_SPAWN);
	REQUIRE(plan.playernum == 3);
}

TEST_CASE("stage-init fault plan rejects noncanonical and out-of-range input disabled",
		"[player-init][transaction][fault][B-1104][T-ENGINE-004]")
{
	const char *invalid[] = {
		"", "spawn", "spawn:", "spawn:-1", "spawn:4", "spawn:01",
		"spawn:01x",
		"reset:999999999999999999", "SPAWN:1", "other:1", " spawn:1"
	};
	player_stage_init_fault_plan_t plan;

	for (const char *spec : invalid) {
		std::memset(&plan, 0x7f, sizeof(plan));
		INFO("spec: " << spec);
		REQUIRE(playerStageInitFaultPlanParse(spec, 4, &plan) !=
			PLAYER_STAGE_INIT_FAULT_OK);
		REQUIRE_FALSE(plan.armed);
		REQUIRE_FALSE(plan.consumed);
		REQUIRE(plan.phase == PLAYER_STAGE_INIT_FAULT_NONE);
	}

	REQUIRE(playerStageInitFaultPlanParse(nullptr, 4, &plan) ==
		PLAYER_STAGE_INIT_FAULT_INVALID_ARGUMENT);
	REQUIRE(playerStageInitFaultPlanParse("spawn:1", 0, &plan) ==
		PLAYER_STAGE_INIT_FAULT_INVALID_ARGUMENT);
	REQUIRE(playerStageInitFaultPlanParse("spawn:1", 4, nullptr) ==
		PLAYER_STAGE_INIT_FAULT_INVALID_ARGUMENT);
}

TEST_CASE("stage-init fault mismatch is inert and exact match consumes once",
		"[player-init][transaction][fault][B-1104][T-ENGINE-004]")
{
	player_stage_init_fault_plan_t plan;
	REQUIRE(playerStageInitFaultPlanParse("spawn:1", 4, &plan) ==
		PLAYER_STAGE_INIT_FAULT_OK);

	REQUIRE_FALSE(playerStageInitFaultPlanConsume(&plan,
		PLAYER_STAGE_INIT_FAULT_RESET, 1));
	REQUIRE_FALSE(playerStageInitFaultPlanConsume(&plan,
		PLAYER_STAGE_INIT_FAULT_SPAWN, 0));
	REQUIRE(plan.armed);
	REQUIRE_FALSE(plan.consumed);

	REQUIRE(playerStageInitFaultPlanConsume(&plan,
		PLAYER_STAGE_INIT_FAULT_SPAWN, 1));
	REQUIRE_FALSE(plan.armed);
	REQUIRE(plan.consumed);
	REQUIRE_FALSE(playerStageInitFaultPlanConsume(&plan,
		PLAYER_STAGE_INIT_FAULT_SPAWN, 1));
}

TEST_CASE("process fault wrapper is inaccessible outside an active smoke",
		"[player-init][transaction][fault][B-1104][T-ENGINE-004]")
{
	playerStageInitFaultClear();
	REQUIRE(playerStageInitFaultConfigure("spawn:1", false, 4) ==
		PLAYER_STAGE_INIT_FAULT_DISABLED);
	REQUIRE_FALSE(playerStageInitFaultConsume(
		PLAYER_STAGE_INIT_FAULT_SPAWN, 1));

	REQUIRE(playerStageInitFaultConfigure("spawn:1", true, 4) ==
		PLAYER_STAGE_INIT_FAULT_OK);
	REQUIRE(playerStageInitFaultConsume(
		PLAYER_STAGE_INIT_FAULT_SPAWN, 1));
	REQUIRE_FALSE(playerStageInitFaultConsume(
		PLAYER_STAGE_INIT_FAULT_SPAWN, 1));
	playerStageInitFaultClear();
}
