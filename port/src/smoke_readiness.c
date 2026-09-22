#include <string.h>

#include "smoke_readiness.h"

static const char *const mainMenuConditions[] = {
	"main_menu_play_ready", "main_menu_settings_focused",
	"settings_video_ready", "settings_interface_ready", "settings_audio_ready",
	"settings_input_ready", "settings_game_ready"
};
_Static_assert(SMOKE_READINESS_SETTINGS_GAME_READY - SMOKE_READINESS_MAIN_MENU_PLAY_READY + 1 ==
	sizeof(mainMenuConditions) / sizeof(mainMenuConditions[0]), "menu readiness registry drift");

smoke_readiness_condition_t smokeReadinessConditionFromName(
	const char *name)
{
	if (!name) {
		return SMOKE_READINESS_INVALID;
	}
	if (strcmp(name, "title_sequence_ready") == 0) {
		return SMOKE_READINESS_TITLE_SEQUENCE_READY;
	}
	if (strcmp(name, "core_boot_ready") == 0) return SMOKE_READINESS_CORE_BOOT_READY;
	if (strcmp(name, "network_listen_ready") == 0) {
		return SMOKE_READINESS_NETWORK_LISTEN_READY;
	}
	if (strcmp(name, "network_lobby_ready") == 0) {
		return SMOKE_READINESS_NETWORK_LOBBY_READY;
	}
	if (strcmp(name, "network_stage_live") == 0) {
		return SMOKE_READINESS_NETWORK_STAGE_LIVE;
	}
	if (strcmp(name, "network_reconnect_available") == 0) {
		return SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE;
	}
	if (strcmp(name, "cutscene_skip_ready") == 0) {
		return SMOKE_READINESS_CUTSCENE_SKIP_READY;
	}
	if (strcmp(name, "gameplay_ready") == 0) {
		return SMOKE_READINESS_GAMEPLAY_READY;
	}
	if (strcmp(name, "offline_gameplay_ready") == 0) {
		return SMOKE_READINESS_OFFLINE_GAMEPLAY_READY;
	}
	if (strcmp(name, "vehicle_driver_ready") == 0) {
		return SMOKE_READINESS_VEHICLE_DRIVER_READY;
	}
	if (strcmp(name, "endscreen_visible") == 0) {
		return SMOKE_READINESS_ENDSCREEN_VISIBLE;
	}
	if (strcmp(name, "agent_select_ready") == 0) {
		return SMOKE_READINESS_AGENT_SELECT_READY;
	}
	if (strcmp(name, "agent_create_ready") == 0) {
		return SMOKE_READINESS_AGENT_CREATE_READY;
	}
	for (unsigned int i = 0; i < sizeof(mainMenuConditions) / sizeof(mainMenuConditions[0]); ++i) {
		if (strcmp(name, mainMenuConditions[i]) == 0)
			return (smoke_readiness_condition_t)(SMOKE_READINESS_MAIN_MENU_PLAY_READY + i);
	}
	return SMOKE_READINESS_INVALID;
}

const char *smokeReadinessConditionName(
	smoke_readiness_condition_t condition)
{
	if (condition >= SMOKE_READINESS_MAIN_MENU_PLAY_READY && condition <= SMOKE_READINESS_SETTINGS_GAME_READY)
		return mainMenuConditions[condition - SMOKE_READINESS_MAIN_MENU_PLAY_READY];
	switch (condition) {
	case SMOKE_READINESS_CORE_BOOT_READY:
		return "core_boot_ready";
	case SMOKE_READINESS_TITLE_SEQUENCE_READY:
		return "title_sequence_ready";
	case SMOKE_READINESS_NETWORK_LISTEN_READY:
		return "network_listen_ready";
	case SMOKE_READINESS_NETWORK_LOBBY_READY:
		return "network_lobby_ready";
	case SMOKE_READINESS_NETWORK_STAGE_LIVE:
		return "network_stage_live";
	case SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE:
		return "network_reconnect_available";
	case SMOKE_READINESS_CUTSCENE_SKIP_READY:
		return "cutscene_skip_ready";
	case SMOKE_READINESS_GAMEPLAY_READY:
		return "gameplay_ready";
	case SMOKE_READINESS_OFFLINE_GAMEPLAY_READY:
		return "offline_gameplay_ready";
	case SMOKE_READINESS_VEHICLE_DRIVER_READY:
		return "vehicle_driver_ready";
	case SMOKE_READINESS_ENDSCREEN_VISIBLE:
		return "endscreen_visible";
	case SMOKE_READINESS_AGENT_SELECT_READY:
		return "agent_select_ready";
	case SMOKE_READINESS_AGENT_CREATE_READY:
		return "agent_create_ready";
	default:
		return "invalid";
	}
}

int smokeReadinessConditionMet(smoke_readiness_condition_t condition,
	const smoke_readiness_facts_t *facts)
{
	int listen_ready;
	int stage_live;
	int cutscene_skip_ready;
	int gameplay_ready;
	int offline_stage_live;
	int offline_gameplay_ready;
	int vehicle_driver_ready;

	if (!facts) {
		return 0;
	}
	if (condition >= SMOKE_READINESS_MAIN_MENU_PLAY_READY && condition <= SMOKE_READINESS_SETTINGS_GAME_READY) {
		if (!facts->boot_complete || !facts->main_menu_current || !facts->main_menu_pool_active ||
			!facts->menu_input_top || !facts->menu_keyboard_ready || !facts->main_menu_imgui_ready ||
			!facts->active_agent) return 0;
		if (condition == SMOKE_READINESS_MAIN_MENU_PLAY_READY)
			return facts->main_menu_view == 0 && facts->main_menu_play_focused;
		if (condition == SMOKE_READINESS_MAIN_MENU_SETTINGS_FOCUSED)
			return facts->main_menu_view == 0 && facts->main_menu_settings_focused;
		return facts->main_menu_view == 2 && facts->settings_pool_active &&
			facts->main_menu_submitted_tab == condition - SMOKE_READINESS_SETTINGS_VIDEO_READY;
	}

	listen_ready = facts->network_active
		&& facts->network_listen_ready;
	stage_live = facts->network_active
		&& facts->local_client_in_game
		&& facts->gameplay_stage
		&& facts->stage_ready_epoch
		&& facts->multiplayer_running
		&& facts->local_player_present
		&& facts->local_player_spawned
		&& facts->stage_tick_active
		&& !facts->endscreen;
	offline_stage_live = !facts->network_active
		&& facts->gameplay_stage
		&& facts->stage_ready_epoch
		&& facts->local_player_present
		&& facts->local_player_spawned
		&& facts->stage_tick_active
		&& !facts->endscreen;
	cutscene_skip_ready = (stage_live || offline_stage_live)
		&& facts->scene_cutscene_layer
		&& facts->player_in_cutscene
		&& facts->cutscene_in_progress
		&& facts->cutscene_frame_ready
		&& (!facts->network_active || facts->cutscene_authority_ready);
	gameplay_ready = stage_live
		&& facts->scene_gameplay_layer
		&& facts->gameplay_tick_normal
		&& facts->gameplay_updates_active
		&& !facts->player_in_cutscene
		&& !facts->cutscene_in_progress
		&& facts->player_has_control
		&& facts->player_unpaused
		&& facts->player_alive
		&& facts->player_walk_mode;
	offline_gameplay_ready = offline_stage_live
		&& facts->scene_gameplay_layer
		&& facts->gameplay_tick_normal
		&& facts->gameplay_updates_active
		&& !facts->player_in_cutscene
		&& !facts->cutscene_in_progress
		&& facts->player_has_control
		&& facts->player_unpaused
		&& facts->player_alive
		&& facts->player_walk_mode;
	vehicle_driver_ready = (stage_live || offline_stage_live)
		&& facts->gameplay_tick_normal
		&& facts->gameplay_updates_active
		&& !facts->player_in_cutscene
		&& !facts->cutscene_in_progress
		&& facts->player_has_control
		&& facts->player_unpaused
		&& facts->player_alive
		&& facts->player_bike_mode
		&& facts->player_hoverbike
		&& facts->vehicle_driver_layer_active;

	switch (condition) {
	case SMOKE_READINESS_CORE_BOOT_READY:
		return facts->boot_complete;
	case SMOKE_READINESS_TITLE_SEQUENCE_READY:
		return facts->boot_complete
			&& facts->title_stage
			&& facts->title_initial_mode
			&& facts->title_transition_idle
			&& facts->title_stage_transition_idle;
	case SMOKE_READINESS_NETWORK_LISTEN_READY:
		return listen_ready;
	case SMOKE_READINESS_NETWORK_LOBBY_READY:
		return facts->boot_complete && facts->network_active
			&& facts->local_client_in_lobby;
	case SMOKE_READINESS_NETWORK_STAGE_LIVE:
		return stage_live;
	case SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE:
		return !facts->network_active && facts->network_reconnect_available;
	case SMOKE_READINESS_CUTSCENE_SKIP_READY:
		return cutscene_skip_ready;
	case SMOKE_READINESS_GAMEPLAY_READY:
		return gameplay_ready;
	case SMOKE_READINESS_OFFLINE_GAMEPLAY_READY:
		return offline_gameplay_ready;
	case SMOKE_READINESS_VEHICLE_DRIVER_READY:
		return vehicle_driver_ready;
	case SMOKE_READINESS_ENDSCREEN_VISIBLE:
		return facts->endscreen
			&& facts->endscreen_menu_active
			&& facts->menu_input_active;
	case SMOKE_READINESS_AGENT_SELECT_READY:
		return facts->boot_complete && facts->agent_select_current
			&& facts->agent_select_pool_active && facts->menu_input_top
			&& facts->menu_keyboard_ready && facts->agent_select_imgui_owner;
	case SMOKE_READINESS_AGENT_CREATE_READY:
		return facts->boot_complete && facts->agent_create_current
			&& facts->agent_create_pool_active && facts->menu_input_top
			&& facts->menu_keyboard_ready && facts->agent_create_imgui_owner;
	default:
		return 0;
	}
}
