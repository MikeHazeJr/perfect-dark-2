#include "catch.hpp"

#include <string>

extern "C" {
#include "smoke_readiness.h"
}

namespace {
smoke_readiness_facts_t stageLiveFacts()
{
	smoke_readiness_facts_t facts{};
	facts.network_active = 1;
	facts.local_client_in_game = 1;
	facts.gameplay_stage = 1;
	facts.stage_ready_epoch = 1;
	facts.multiplayer_running = 1;
	facts.local_player_present = 1;
	facts.local_player_spawned = 1;
	facts.stage_tick_active = 1;
	return facts;
}
}

TEST_CASE("smoke readiness condition names are exact and fail closed",
	"[smoke][readiness][b1085]")
{
	REQUIRE(smokeReadinessConditionFromName("network_listen_ready") ==
		SMOKE_READINESS_NETWORK_LISTEN_READY);
	REQUIRE(smokeReadinessConditionFromName("network_stage_live") ==
		SMOKE_READINESS_NETWORK_STAGE_LIVE);
	REQUIRE(smokeReadinessConditionFromName("network_reconnect_available") ==
		SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE);
	REQUIRE(smokeReadinessConditionFromName("cutscene_skip_ready") ==
		SMOKE_READINESS_CUTSCENE_SKIP_READY);
	REQUIRE(smokeReadinessConditionFromName("gameplay_ready") ==
		SMOKE_READINESS_GAMEPLAY_READY);
	REQUIRE(smokeReadinessConditionFromName("GAMEPLAY_READY") ==
		SMOKE_READINESS_INVALID);
	REQUIRE(smokeReadinessConditionFromName(0) == SMOKE_READINESS_INVALID);
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_NETWORK_LISTEN_READY)) == "network_listen_ready");
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_NETWORK_STAGE_LIVE)) == "network_stage_live");
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE)) ==
		"network_reconnect_available");
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_CUTSCENE_SKIP_READY)) == "cutscene_skip_ready");
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_GAMEPLAY_READY)) == "gameplay_ready");
}

TEST_CASE("network listen readiness starts peer deadlines at the published host",
	"[smoke][readiness][network][b1095]")
{
	smoke_readiness_facts_t facts{};

	facts.network_active = 1;
	facts.network_listen_ready = 1;
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_LISTEN_READY, &facts));

	facts.network_listen_ready = 0;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_LISTEN_READY, &facts));
	facts.network_listen_ready = 1;
	facts.network_active = 0;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_LISTEN_READY, &facts));
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_LISTEN_READY, nullptr));
}

TEST_CASE("network reconnect readiness requires a retained credential while offline",
	"[smoke][readiness][reconnect][b1064]")
{
	smoke_readiness_facts_t facts{};
	facts.network_reconnect_available = 1;
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE, &facts));
	facts.network_active = 1;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE, &facts));
	facts.network_active = 0;
	facts.network_reconnect_available = 0;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE, &facts));
}

TEST_CASE("network stage-live readiness rejects every incomplete base fact",
	"[smoke][readiness][b1085]")
{
	auto facts = stageLiveFacts();
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_STAGE_LIVE, &facts));

	int *required[] = {
		&facts.network_active,
		&facts.local_client_in_game,
		&facts.gameplay_stage,
		&facts.stage_ready_epoch,
		&facts.multiplayer_running,
		&facts.local_player_present,
		&facts.local_player_spawned,
		&facts.stage_tick_active,
	};
	for (int *field : required) {
		*field = 0;
		REQUIRE_FALSE(smokeReadinessConditionMet(
			SMOKE_READINESS_NETWORK_STAGE_LIVE, &facts));
		*field = 1;
	}
	facts.endscreen = 1;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_STAGE_LIVE, &facts));
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_STAGE_LIVE, nullptr));
}

TEST_CASE("cutscene skip readiness matches the production request aperture",
	"[smoke][readiness][b1085]")
{
	auto facts = stageLiveFacts();
	facts.scene_cutscene_layer = 1;
	facts.player_in_cutscene = 1;
	facts.cutscene_in_progress = 1;
	facts.cutscene_frame_ready = 1;
	facts.cutscene_authority_ready = 1;

	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_CUTSCENE_SKIP_READY, &facts));

	int *required[] = {
		&facts.scene_cutscene_layer,
		&facts.player_in_cutscene,
		&facts.cutscene_in_progress,
		&facts.cutscene_frame_ready,
		&facts.cutscene_authority_ready,
	};
	for (int *field : required) {
		*field = 0;
		REQUIRE_FALSE(smokeReadinessConditionMet(
			SMOKE_READINESS_CUTSCENE_SKIP_READY, &facts));
		*field = 1;
	}
}

TEST_CASE("gameplay readiness requires an updated controllable walking player",
	"[smoke][readiness][b1085]")
{
	auto facts = stageLiveFacts();

	/* A live cutscene is intentionally enough for the preceding skip barrier,
	 * but never enough for gameplay input. */
	facts.player_in_cutscene = 1;
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_STAGE_LIVE, &facts));
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_GAMEPLAY_READY, &facts));

	facts.player_in_cutscene = 0;
	facts.scene_gameplay_layer = 1;
	facts.gameplay_tick_normal = 1;
	facts.gameplay_updates_active = 1;
	facts.player_has_control = 1;
	facts.player_unpaused = 1;
	facts.player_alive = 1;
	facts.player_walk_mode = 1;
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_GAMEPLAY_READY, &facts));

	int *required[] = {
		&facts.scene_gameplay_layer,
		&facts.gameplay_tick_normal,
		&facts.gameplay_updates_active,
		&facts.player_has_control,
		&facts.player_unpaused,
		&facts.player_alive,
		&facts.player_walk_mode,
	};
	for (int *field : required) {
		*field = 0;
		REQUIRE_FALSE(smokeReadinessConditionMet(
			SMOKE_READINESS_GAMEPLAY_READY, &facts));
		*field = 1;
	}

	facts.cutscene_in_progress = 1;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_GAMEPLAY_READY, &facts));
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_INVALID, &facts));
}

TEST_CASE("stage-live without normal tick mode cannot admit reconnect gameplay",
	"[smoke][readiness][network][reconnect][b1099]")
{
	auto facts = stageLiveFacts();
	facts.scene_gameplay_layer = 1;
	facts.gameplay_updates_active = 1;
	facts.player_has_control = 1;
	facts.player_unpaused = 1;
	facts.player_alive = 1;
	facts.player_walk_mode = 1;

	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_STAGE_LIVE, &facts));
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_GAMEPLAY_READY, &facts));

	facts.gameplay_tick_normal = 1;
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_GAMEPLAY_READY, &facts));
}
