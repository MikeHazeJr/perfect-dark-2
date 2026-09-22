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
	REQUIRE(smokeReadinessConditionFromName("title_sequence_ready") ==
		SMOKE_READINESS_TITLE_SEQUENCE_READY);
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
	REQUIRE(smokeReadinessConditionFromName("offline_gameplay_ready") ==
		SMOKE_READINESS_OFFLINE_GAMEPLAY_READY);
	REQUIRE(smokeReadinessConditionFromName("vehicle_driver_ready") ==
		SMOKE_READINESS_VEHICLE_DRIVER_READY);
	REQUIRE(smokeReadinessConditionFromName("endscreen_visible") ==
		SMOKE_READINESS_ENDSCREEN_VISIBLE);
	REQUIRE(smokeReadinessConditionFromName("GAMEPLAY_READY") ==
		SMOKE_READINESS_INVALID);
	REQUIRE(smokeReadinessConditionFromName(0) == SMOKE_READINESS_INVALID);
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_TITLE_SEQUENCE_READY)) == "title_sequence_ready");
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
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_OFFLINE_GAMEPLAY_READY)) ==
		"offline_gameplay_ready");
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_VEHICLE_DRIVER_READY)) == "vehicle_driver_ready");
	REQUIRE(std::string(smokeReadinessConditionName(
		SMOKE_READINESS_ENDSCREEN_VISIBLE)) == "endscreen_visible");
}

TEST_CASE("title readiness begins fixture time at the applied production sequence",
	"[smoke][readiness][title][b1086]")
{
	smoke_readiness_facts_t facts{};
	int *required[] = {
		&facts.boot_complete,
		&facts.title_stage,
		&facts.title_initial_mode,
		&facts.title_transition_idle,
		&facts.title_stage_transition_idle,
	};

	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_TITLE_SEQUENCE_READY, &facts));
	for (int *field : required) {
		*field = 1;
	}
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_TITLE_SEQUENCE_READY, &facts));
	for (int *field : required) {
		*field = 0;
		REQUIRE_FALSE(smokeReadinessConditionMet(
			SMOKE_READINESS_TITLE_SEQUENCE_READY, &facts));
		*field = 1;
	}
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

TEST_CASE("network lobby readiness waits for completed boot and authenticated lobby state",
    "[smoke][readiness][network][lobby][T-NETWORKING-011]")
{
    const auto condition = SMOKE_READINESS_NETWORK_LOBBY_READY;
    REQUIRE(smokeReadinessConditionFromName("network_lobby_ready") == condition);
    REQUIRE(std::string(smokeReadinessConditionName(condition)) == "network_lobby_ready");
    smoke_readiness_facts_t facts{};
    REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
    facts.network_active = 1;
    facts.local_client_in_lobby = 1;
    REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
    facts.boot_complete = 1;
    REQUIRE(smokeReadinessConditionMet(condition, &facts));
    facts.local_client_in_lobby = 0;
    facts.local_client_in_game = 1;
    REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
    facts.local_client_in_game = 0;
    facts.local_client_in_lobby = 1;
    facts.network_active = 0;
    REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
    REQUIRE_FALSE(smokeReadinessConditionMet(condition, nullptr));
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

	/* Offline campaign has no authority generation. The same stage-live
	 * cutscene aperture remains valid without weakening the network predicate
	 * above. */
	auto offline = stageLiveFacts();
	offline.network_active = 0;
	offline.local_client_in_game = 0;
	offline.multiplayer_running = 0;
	offline.cutscene_authority_ready = 0;
	offline.scene_cutscene_layer = 1;
	offline.player_in_cutscene = 1;
	offline.cutscene_in_progress = 1;
	offline.cutscene_frame_ready = 1;
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_CUTSCENE_SKIP_READY, &offline));
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

TEST_CASE("offline gameplay readiness uses the same complete local stage boundary",
	"[smoke][readiness][offline][combat-sim][b1076]")
{
	auto facts = stageLiveFacts();
	facts.network_active = 0;
	facts.local_client_in_game = 0;
	facts.multiplayer_running = 0;
	facts.scene_gameplay_layer = 1;
	facts.gameplay_tick_normal = 1;
	facts.gameplay_updates_active = 1;
	facts.player_has_control = 1;
	facts.player_unpaused = 1;
	facts.player_alive = 1;
	facts.player_walk_mode = 1;

	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_OFFLINE_GAMEPLAY_READY, &facts));
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_NETWORK_STAGE_LIVE, &facts));

	int *required[] = {
		&facts.gameplay_stage,
		&facts.stage_ready_epoch,
		&facts.local_player_present,
		&facts.local_player_spawned,
		&facts.stage_tick_active,
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
			SMOKE_READINESS_OFFLINE_GAMEPLAY_READY, &facts));
		*field = 1;
	}
	facts.network_active = 1;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_OFFLINE_GAMEPLAY_READY, &facts));
}

TEST_CASE("vehicle driver readiness requires a complete mounted offline player",
	"[smoke][readiness][offline][vehicle][b1099]")
{
	auto facts = stageLiveFacts();
	facts.network_active = 0;
	facts.local_client_in_game = 0;
	facts.multiplayer_running = 0;
	/* Mounted input owns the top layer, so scene_gameplay_layer is false here.
	 * The driver fact represents an exact top-layer projection, not a buried
	 * LAYER_VEHICLE_DRIVER entry. */
	facts.scene_gameplay_layer = 0;
	facts.gameplay_tick_normal = 1;
	facts.gameplay_updates_active = 1;
	facts.player_has_control = 1;
	facts.player_unpaused = 1;
	facts.player_alive = 1;
	facts.player_bike_mode = 1;
	facts.player_hoverbike = 1;
	facts.vehicle_driver_layer_active = 1;

	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_VEHICLE_DRIVER_READY, &facts));

	int *required[] = {
		&facts.gameplay_stage,
		&facts.stage_ready_epoch,
		&facts.local_player_present,
		&facts.local_player_spawned,
		&facts.stage_tick_active,
		&facts.gameplay_tick_normal,
		&facts.gameplay_updates_active,
		&facts.player_has_control,
		&facts.player_unpaused,
		&facts.player_alive,
		&facts.player_bike_mode,
		&facts.player_hoverbike,
		&facts.vehicle_driver_layer_active,
	};
	for (int *field : required) {
		*field = 0;
		REQUIRE_FALSE(smokeReadinessConditionMet(
			SMOKE_READINESS_VEHICLE_DRIVER_READY, &facts));
		*field = 1;
	}

	facts.player_in_cutscene = 1;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_VEHICLE_DRIVER_READY, &facts));
	facts.player_in_cutscene = 0;
	facts.cutscene_in_progress = 1;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_VEHICLE_DRIVER_READY, &facts));
	facts.cutscene_in_progress = 0;

	/* A gameplay overlay or absent/buried driver layer cannot satisfy the
	 * mounted barrier, even when every player/vehicle fact is otherwise live. */
	facts.scene_gameplay_layer = 1;
	facts.vehicle_driver_layer_active = 0;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_VEHICLE_DRIVER_READY, &facts));
	facts.scene_gameplay_layer = 0;
	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_VEHICLE_DRIVER_READY, &facts));
}

TEST_CASE("endscreen readiness requires visible state, menu ownership, and input",
		"[smoke][readiness][endscreen][b1076]")
{
	smoke_readiness_facts_t facts{};
	int *required[] = {
		&facts.endscreen,
		&facts.endscreen_menu_active,
		&facts.menu_input_active,
	};

	REQUIRE_FALSE(smokeReadinessConditionMet(
		SMOKE_READINESS_ENDSCREEN_VISIBLE, &facts));
	for (int *field : required) {
		*field = 1;
	}
	REQUIRE(smokeReadinessConditionMet(
		SMOKE_READINESS_ENDSCREEN_VISIBLE, &facts));
	for (int *field : required) {
		*field = 0;
		REQUIRE_FALSE(smokeReadinessConditionMet(
			SMOKE_READINESS_ENDSCREEN_VISIBLE, &facts));
		*field = 1;
	}

}

TEST_CASE("Settings readiness requires submitted content and an active Agent on the owning menu",
    "[smoke][readiness][menus]")
{
    const char *names[] = {"main_menu_play_ready", "main_menu_settings_focused",
        "settings_video_ready", "settings_interface_ready", "settings_audio_ready",
        "settings_input_ready", "settings_game_ready"};
    for (int index = 0; index < 7; ++index) {
        const auto condition = static_cast<smoke_readiness_condition_t>(
            SMOKE_READINESS_MAIN_MENU_PLAY_READY + index);
        INFO(names[index]);
        REQUIRE(smokeReadinessConditionFromName(names[index]) == condition);
        REQUIRE(std::string(smokeReadinessConditionName(condition)) == names[index]);
        smoke_readiness_facts_t facts{};
        int *required[] = {&facts.boot_complete, &facts.main_menu_current,
            &facts.main_menu_pool_active, &facts.menu_input_top,
            &facts.menu_keyboard_ready, &facts.main_menu_imgui_ready, &facts.active_agent};
        for (int *field : required) *field = 1;
        facts.main_menu_view = index < 2 ? 0 : 2;
        facts.main_menu_submitted_tab = index - 2;
        facts.main_menu_play_focused = index == 0;
        facts.main_menu_settings_focused = index == 1;
        facts.settings_pool_active = index >= 2;
        REQUIRE(smokeReadinessConditionMet(condition, &facts));
        for (int *field : required) {
            *field = 0;
            REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
            *field = 1;
        }
        facts.main_menu_view = 1;
        REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
        facts.main_menu_view = index < 2 ? 0 : 2;
        if (index < 2) {
            facts.main_menu_play_focused = !facts.main_menu_play_focused;
            facts.main_menu_settings_focused = !facts.main_menu_settings_focused;
            REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
        } else {
            facts.main_menu_submitted_tab = -1;
            REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
            facts.main_menu_submitted_tab = (index - 1) % 5;
            REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
            facts.main_menu_submitted_tab = index - 2;
            facts.settings_pool_active = 0;
            REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
        }
        REQUIRE_FALSE(smokeReadinessConditionMet(condition, nullptr));
    }
}

TEST_CASE("Core boot readiness never grants ordinary menu input authority",
    "[smoke][readiness][modmgr][core]")
{
    smoke_readiness_facts_t facts{};
    REQUIRE(smokeReadinessConditionFromName("core_boot_ready") == SMOKE_READINESS_CORE_BOOT_READY);
    REQUIRE(std::string(smokeReadinessConditionName(SMOKE_READINESS_CORE_BOOT_READY)) == "core_boot_ready");
    REQUIRE_FALSE(smokeReadinessConditionMet(SMOKE_READINESS_CORE_BOOT_READY, nullptr));
    REQUIRE_FALSE(smokeReadinessConditionMet(SMOKE_READINESS_CORE_BOOT_READY, &facts));
    facts.boot_complete = 1;
    facts.agent_select_current = 1;
    facts.agent_select_pool_active = 1;
    facts.menu_input_top = 1;
    REQUIRE(smokeReadinessConditionMet(SMOKE_READINESS_CORE_BOOT_READY, &facts));
    REQUIRE_FALSE(smokeReadinessConditionMet(SMOKE_READINESS_AGENT_SELECT_READY, &facts));
    facts.menu_keyboard_ready = 1;
    REQUIRE_FALSE(smokeReadinessConditionMet(SMOKE_READINESS_AGENT_SELECT_READY, &facts));
    facts.agent_select_imgui_owner = 1;
    REQUIRE(smokeReadinessConditionMet(SMOKE_READINESS_AGENT_SELECT_READY, &facts));
}

TEST_CASE("menu readiness names round-trip and incomplete ownership fails closed",
    "[smoke][readiness][menus]")
{
    REQUIRE(smokeReadinessConditionFromName("agent_select_ready") == SMOKE_READINESS_AGENT_SELECT_READY);
    REQUIRE(smokeReadinessConditionFromName("agent_create_ready") == SMOKE_READINESS_AGENT_CREATE_READY);
    REQUIRE(std::string(smokeReadinessConditionName(SMOKE_READINESS_AGENT_SELECT_READY)) == "agent_select_ready");
    REQUIRE(std::string(smokeReadinessConditionName(SMOKE_READINESS_AGENT_CREATE_READY)) == "agent_create_ready");
    for (const auto condition : {SMOKE_READINESS_AGENT_SELECT_READY, SMOKE_READINESS_AGENT_CREATE_READY}) {
        smoke_readiness_facts_t facts{};
        const bool select = condition == SMOKE_READINESS_AGENT_SELECT_READY;
        int *required[] = {
            &facts.boot_complete,
            select ? &facts.agent_select_current : &facts.agent_create_current,
            select ? &facts.agent_select_pool_active : &facts.agent_create_pool_active,
            &facts.menu_input_top,
            &facts.menu_keyboard_ready,
            select ? &facts.agent_select_imgui_owner : &facts.agent_create_imgui_owner,
        };
        REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
        for (int *field : required) *field = 1;
        REQUIRE(smokeReadinessConditionMet(condition, &facts));
        for (int *field : required) {
            *field = 0;
            REQUIRE_FALSE(smokeReadinessConditionMet(condition, &facts));
            *field = 1;
        }
        REQUIRE_FALSE(smokeReadinessConditionMet(select ? SMOKE_READINESS_AGENT_CREATE_READY
            : SMOKE_READINESS_AGENT_SELECT_READY, &facts));
        REQUIRE_FALSE(smokeReadinessConditionMet(condition, nullptr));
    }
}
