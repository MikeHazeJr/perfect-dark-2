#ifndef PD_SMOKE_READINESS_H
#define PD_SMOKE_READINESS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum smoke_readiness_condition {
	SMOKE_READINESS_INVALID = 0,
	SMOKE_READINESS_TITLE_SEQUENCE_READY,
	SMOKE_READINESS_NETWORK_LISTEN_READY,
	SMOKE_READINESS_NETWORK_STAGE_LIVE,
	SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE,
	SMOKE_READINESS_CUTSCENE_SKIP_READY,
	SMOKE_READINESS_GAMEPLAY_READY,
	SMOKE_READINESS_OFFLINE_GAMEPLAY_READY,
	SMOKE_READINESS_VEHICLE_DRIVER_READY,
	SMOKE_READINESS_ENDSCREEN_VISIBLE,
	SMOKE_READINESS_AGENT_SELECT_READY,
	SMOKE_READINESS_AGENT_CREATE_READY,
	SMOKE_READINESS_MAIN_MENU_PLAY_READY,
	SMOKE_READINESS_MAIN_MENU_SETTINGS_FOCUSED,
	SMOKE_READINESS_SETTINGS_VIDEO_READY,
	SMOKE_READINESS_SETTINGS_INTERFACE_READY,
	SMOKE_READINESS_SETTINGS_AUDIO_READY,
	SMOKE_READINESS_SETTINGS_INPUT_READY,
	SMOKE_READINESS_SETTINGS_GAME_READY,
	SMOKE_READINESS_NETWORK_LOBBY_READY,
	SMOKE_READINESS_CORE_BOOT_READY
} smoke_readiness_condition_t;

/* Production state is projected into this pure decision seam by the smoke
 * harness. Keeping the policy free of game globals makes every rejected
 * partial state directly testable without creating a second runtime path. */
typedef struct smoke_readiness_facts {
	int boot_complete;
	int title_stage;
	int title_initial_mode;
	int title_transition_idle;
	int title_stage_transition_idle;
	int network_active;
	int network_listen_ready;
	int network_reconnect_available;
	int local_client_in_game;
	int local_client_in_lobby;
	int gameplay_stage;
	int stage_ready_epoch;
	int multiplayer_running;
	int local_player_present;
	int local_player_spawned;
	int stage_tick_active;
	int scene_cutscene_layer;
	int scene_gameplay_layer;
	int cutscene_in_progress;
	int cutscene_frame_ready;
	int cutscene_authority_ready;
	int gameplay_tick_normal;
	int gameplay_updates_active;
	int player_in_cutscene;
	int player_has_control;
	int player_unpaused;
	int player_alive;
	int player_walk_mode;
	int player_bike_mode;
	int player_hoverbike;
	int vehicle_driver_layer_active;
	int endscreen;
	int endscreen_menu_active;
	int menu_input_active;
	int agent_select_current;
	int agent_select_pool_active;
	int agent_create_current;
	int agent_create_pool_active;
	int menu_input_top;
	int menu_keyboard_ready;
	int agent_select_imgui_owner;
	int agent_create_imgui_owner;
	int main_menu_current;
	int main_menu_pool_active;
	int settings_pool_active;
	int main_menu_imgui_ready;
	int main_menu_view;
	int main_menu_submitted_tab;
	int main_menu_play_focused;
	int main_menu_settings_focused;
	int active_agent;
} smoke_readiness_facts_t;

smoke_readiness_condition_t smokeReadinessConditionFromName(
	const char *name);
const char *smokeReadinessConditionName(
	smoke_readiness_condition_t condition);
int smokeReadinessConditionMet(smoke_readiness_condition_t condition,
	const smoke_readiness_facts_t *facts);

#ifdef __cplusplus
}
#endif

#endif /* PD_SMOKE_READINESS_H */
