#ifndef PD_SMOKE_READINESS_H
#define PD_SMOKE_READINESS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum smoke_readiness_condition {
	SMOKE_READINESS_INVALID = 0,
	SMOKE_READINESS_NETWORK_LISTEN_READY,
	SMOKE_READINESS_NETWORK_STAGE_LIVE,
	SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE,
	SMOKE_READINESS_CUTSCENE_SKIP_READY,
	SMOKE_READINESS_GAMEPLAY_READY
} smoke_readiness_condition_t;

/* Production state is projected into this pure decision seam by the smoke
 * harness. Keeping the policy free of game globals makes every rejected
 * partial state directly testable without creating a second runtime path. */
typedef struct smoke_readiness_facts {
	int network_active;
	int network_listen_ready;
	int network_reconnect_available;
	int local_client_in_game;
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
	int endscreen;
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
