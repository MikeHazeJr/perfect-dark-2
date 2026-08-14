#include <string.h>

#include "smoke_readiness.h"

smoke_readiness_condition_t smokeReadinessConditionFromName(
	const char *name)
{
	if (!name) {
		return SMOKE_READINESS_INVALID;
	}
	if (strcmp(name, "network_listen_ready") == 0) {
		return SMOKE_READINESS_NETWORK_LISTEN_READY;
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
	return SMOKE_READINESS_INVALID;
}

const char *smokeReadinessConditionName(
	smoke_readiness_condition_t condition)
{
	switch (condition) {
	case SMOKE_READINESS_NETWORK_LISTEN_READY:
		return "network_listen_ready";
	case SMOKE_READINESS_NETWORK_STAGE_LIVE:
		return "network_stage_live";
	case SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE:
		return "network_reconnect_available";
	case SMOKE_READINESS_CUTSCENE_SKIP_READY:
		return "cutscene_skip_ready";
	case SMOKE_READINESS_GAMEPLAY_READY:
		return "gameplay_ready";
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

	if (!facts) {
		return 0;
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
	cutscene_skip_ready = stage_live
		&& facts->scene_cutscene_layer
		&& facts->player_in_cutscene
		&& facts->cutscene_in_progress
		&& facts->cutscene_frame_ready
		&& facts->cutscene_authority_ready;
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

	switch (condition) {
	case SMOKE_READINESS_NETWORK_LISTEN_READY:
		return listen_ready;
	case SMOKE_READINESS_NETWORK_STAGE_LIVE:
		return stage_live;
	case SMOKE_READINESS_NETWORK_RECONNECT_AVAILABLE:
		return !facts->network_active && facts->network_reconnect_available;
	case SMOKE_READINESS_CUTSCENE_SKIP_READY:
		return cutscene_skip_ready;
	case SMOKE_READINESS_GAMEPLAY_READY:
		return gameplay_ready;
	default:
		return 0;
	}
}
