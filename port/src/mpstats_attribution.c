#include "mpstats_attribution.h"

#include <stddef.h>

s32 mpstatsAttributionResolve(s32 runtime_index, s32 runtime_count,
		s32 config_slot, s32 player_slot_count, s32 config_slot_count,
		mpstats_actor_identity_t *out_identity)
{
	if (out_identity != NULL) {
		out_identity->runtime_index = -1;
		out_identity->config_slot = -1;
		out_identity->kind = MPSTATS_ACTOR_INVALID;
	}

	if (out_identity == NULL
			|| runtime_count < 0
			|| runtime_index < 0 || runtime_index >= runtime_count
			|| player_slot_count < 0
			|| config_slot_count < player_slot_count
			|| config_slot < 0 || config_slot >= config_slot_count) {
		return 0;
	}

	out_identity->runtime_index = runtime_index;
	out_identity->config_slot = config_slot;
	out_identity->kind = config_slot < player_slot_count
		? MPSTATS_ACTOR_PLAYER : MPSTATS_ACTOR_BOT;
	return 1;
}

s32 mpstatsAttributionChooseShooterRuntimeIndex(
		s32 wire_attacker_runtime_index, s32 live_attacker_runtime_index,
		s32 victim_runtime_index, s32 runtime_count)
{
	if (runtime_count <= 0 || victim_runtime_index < 0
			|| victim_runtime_index >= runtime_count) {
		return -1;
	}

	if (wire_attacker_runtime_index >= 0
			&& wire_attacker_runtime_index < runtime_count
			&& wire_attacker_runtime_index != victim_runtime_index) {
		return wire_attacker_runtime_index;
	}

	if (live_attacker_runtime_index >= 0
			&& live_attacker_runtime_index < runtime_count) {
		return live_attacker_runtime_index;
	}

	return victim_runtime_index;
}

const char *mpstatsActorKindString(mpstats_actor_kind_t kind)
{
	switch (kind) {
	case MPSTATS_ACTOR_PLAYER:
		return "player";
	case MPSTATS_ACTOR_BOT:
		return "bot";
	default:
		return "invalid";
	}
}
