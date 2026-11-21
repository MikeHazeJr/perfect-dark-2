#include "skedar_swarm_encounter.h"

#include <string.h>

static int is_active(const struct skedar_swarm_encounter *state, uint64_t epoch)
{
	return epoch != 0 && state->stage_epoch == epoch
		&& (state->phase == SKEDAR_SWARM_DEFENSE
			|| state->phase == SKEDAR_SWARM_DRAINING
			|| state->phase == SKEDAR_SWARM_FINALE);
}

static uint32_t later_tick(uint32_t tick, uint32_t delay)
{
	return UINT32_MAX - tick < delay ? UINT32_MAX : tick + delay;
}

static int config_valid(const struct skedar_swarm_config *config)
{
	if (!config || !config->max_live_raiders
			|| config->max_live_raiders > SKEDAR_SWARM_MAX_RAIDERS
			|| !config->wave_count || config->wave_count > SKEDAR_SWARM_MAX_WAVES
			|| !config->max_placement_failures || !config->spawn_interval_ticks) {
		return 0;
	}
	const struct skedar_swarm_attack_limits *limits = &config->attacks;
	if (!limits->active || limits->active > config->max_live_raiders
			|| limits->airborne > limits->active
			|| !limits->starts_per_tick || limits->starts_per_tick > limits->active
			|| !limits->telegraph_ticks || limits->duration_ticks <= limits->telegraph_ticks
			|| !limits->cooldown_ticks) {
		return 0;
	}
	for (int i = 0; i < SKEDAR_SWARM_ATTACK_KIND_COUNT; i++) {
		if (limits->active_by_kind[i] > limits->active) {
			return 0;
		}
	}
	for (int i = 0; i < config->wave_count; i++) {
		const struct skedar_swarm_wave *wave = &config->waves[i];
		if (!wave->raiders || !wave->tunnel_mask
				|| (wave->tunnel_mask & ~SKEDAR_SWARM_ALL_TUNNELS)) {
			return 0;
		}
	}
	return 1;
}

static void refresh_phase(struct skedar_swarm_encounter *state)
{
	if ((state->phase == SKEDAR_SWARM_DEFENSE || state->phase == SKEDAR_SWARM_DRAINING)
			&& state->closed_mask == SKEDAR_SWARM_ALL_TUNNELS) {
		state->phase = state->live_raiders ? SKEDAR_SWARM_DRAINING : SKEDAR_SWARM_FINALE;
		state->placement_failures = 0;
	}
}

static int same_actor(struct skedar_swarm_actor a, struct skedar_swarm_actor b)
{
	return a.prop_generation == b.prop_generation && a.prop_index == b.prop_index;
}

static int same_ticket(const struct skedar_swarm_ticket *a, const struct skedar_swarm_ticket *b)
{
	return a->stage_epoch == b->stage_epoch && a->sequence == b->sequence
		&& a->issued_tick == b->issued_tick
		&& a->kind == b->kind && a->tunnel_index == b->tunnel_index
		&& a->wave_index == b->wave_index;
}

int skedarSwarmInit(struct skedar_swarm_encounter *state,
		const struct skedar_swarm_config *config, uint64_t stage_epoch, uint32_t tick)
{
	if (!stage_epoch || !config_valid(config)) {
		return 0;
	}
	const struct skedar_swarm_config next = *config;
	memset(state, 0, sizeof(*state));
	state->config = next;
	state->stage_epoch = stage_epoch;
	state->phase = SKEDAR_SWARM_DEFENSE;
	state->last_request_tick = tick;
	state->last_attack_tick = tick;
	state->next_spawn_tick = tick;
	state->wave_ready_tick = later_tick(tick, next.waves[0].delay_ticks);
	return 1;
}

void skedarSwarmStop(struct skedar_swarm_encounter *state)
{
	memset(state, 0, sizeof(*state));
}

int skedarSwarmCloseTunnel(struct skedar_swarm_encounter *state,
		uint64_t stage_epoch, int tunnel_index, int blocker_committed)
{
	if (!is_active(state, stage_epoch) || state->phase != SKEDAR_SWARM_DEFENSE
			|| tunnel_index < 0 || tunnel_index >= SKEDAR_SWARM_TUNNEL_COUNT
			|| !blocker_committed) {
		return 0;
	}
	const uint8_t mask = 1u << tunnel_index;
	if (state->closed_mask & mask) {
		return 0;
	}
	state->closed_mask |= mask;
	if (state->pending.sequence && state->pending.kind == SKEDAR_SWARM_RAIDER
			&& state->pending.tunnel_index == tunnel_index) {
		state->pending.sequence = 0;
	}
	refresh_phase(state);
	return 1;
}

/* A completed wave drains its live raiders before advancing. A wave with no
 * open authored route admits nobody and advances once that wave drains. */
static unsigned next_allowed_tunnels(struct skedar_swarm_encounter *state, uint32_t tick)
{
	while (state->wave_index < state->config.wave_count) {
		const struct skedar_swarm_wave *wave = &state->config.waves[state->wave_index];
		const unsigned allowed = wave->tunnel_mask & ~state->closed_mask;
		if (allowed && state->wave_admitted < wave->raiders) {
			return allowed;
		}
		if (state->live_raiders) {
			return 0;
		}
		state->wave_admitted = 0;
		state->wave_index++;
		if (state->wave_index < state->config.wave_count) {
			state->wave_ready_tick = later_tick(tick, state->config.waves[state->wave_index].delay_ticks);
		}
	}
	return 0;
}

static enum skedar_swarm_spawn_result placement_failed(struct skedar_swarm_encounter *state,
		const struct skedar_swarm_ticket *ticket)
{
	if (ticket->kind == SKEDAR_SWARM_RAIDER) {
		state->next_tunnel = (ticket->tunnel_index + 1) % SKEDAR_SWARM_TUNNEL_COUNT;
	}
	if (++state->placement_failures == state->config.max_placement_failures) {
		state->phase = SKEDAR_SWARM_FAILED;
		return SKEDAR_SWARM_SPAWN_FAILED;
	}
	return SKEDAR_SWARM_SPAWN_RETRY;
}

int skedarSwarmRequestSpawn(struct skedar_swarm_encounter *state,
		uint64_t stage_epoch, uint32_t tick, struct skedar_swarm_ticket *ticket)
{
	if (!ticket || !is_active(state, stage_epoch) || tick < state->last_request_tick) {
		return 0;
	}
	state->last_request_tick = tick;
	if (state->pending.sequence && tick > state->pending.issued_tick) {
		const struct skedar_swarm_ticket expired = state->pending;
		state->pending.sequence = 0;
		if (placement_failed(state, &expired) == SKEDAR_SWARM_SPAWN_FAILED) {
			return 0;
		}
	}
	if (state->pending.sequence || tick < state->next_spawn_tick
			|| (state->sequence && tick == state->last_spawn_tick)) {
		return 0;
	}
	enum skedar_swarm_spawn_kind kind;
	int tunnel = -1;
	int wave = -1;
	if (state->phase == SKEDAR_SWARM_DEFENSE) {
		if (state->live_raiders >= state->config.max_live_raiders) {
			return 0;
		}
		const unsigned allowed = next_allowed_tunnels(state, tick);
		if (!allowed || tick < state->wave_ready_tick) {
			return 0;
		}
		for (int offset = 0; offset < SKEDAR_SWARM_TUNNEL_COUNT; offset++) {
			const int candidate = (state->next_tunnel + offset) % SKEDAR_SWARM_TUNNEL_COUNT;
			if (allowed & (1u << candidate)) {
				tunnel = candidate;
				break;
			}
		}
		kind = SKEDAR_SWARM_RAIDER;
		wave = state->wave_index;
	} else if (state->phase == SKEDAR_SWARM_FINALE && !state->giant.actor.prop_generation) {
		kind = SKEDAR_SWARM_GIANT;
	} else {
		return 0;
	}
	if (state->sequence == UINT32_MAX) {
		state->phase = SKEDAR_SWARM_FAILED;
		return 0;
	}
	state->pending = (struct skedar_swarm_ticket){stage_epoch, ++state->sequence, tick, kind, tunnel, wave};
	state->last_spawn_tick = tick;
	state->next_spawn_tick = later_tick(tick, state->config.spawn_interval_ticks);
	*ticket = state->pending;
	return 1;
}

enum skedar_swarm_spawn_result skedarSwarmResolveSpawn(struct skedar_swarm_encounter *state,
		const struct skedar_swarm_ticket *ticket, uint32_t tick, struct skedar_swarm_actor actor)
{
	if (!ticket || !ticket->sequence || !is_active(state, ticket->stage_epoch)
			|| !same_ticket(ticket, &state->pending) || tick < state->last_request_tick) {
		return SKEDAR_SWARM_SPAWN_STALE;
	}
	const struct skedar_swarm_ticket resolved = *ticket;
	state->pending.sequence = 0;
	state->last_request_tick = tick;
	if (tick != resolved.issued_tick) {
		placement_failed(state, &resolved);
		return SKEDAR_SWARM_SPAWN_STALE;
	}
	if (!actor.prop_generation) {
		return placement_failed(state, &resolved);
	}

	if (resolved.kind == SKEDAR_SWARM_GIANT) {
		state->giant.actor = actor;
	} else {
		int free_index = -1;
		for (int i = 0; i < state->config.max_live_raiders; i++) {
			if (!state->raiders[i].actor.prop_generation) {
				if (free_index < 0) {
					free_index = i;
				}
			} else if (state->raiders[i].actor.prop_index == actor.prop_index) {
				return placement_failed(state, &resolved);
			}
		}
		if (free_index < 0) {
			return placement_failed(state, &resolved);
		}
		state->raiders[free_index].actor = actor;
		state->wave_admitted++;
		state->live_raiders++;
		state->next_tunnel = (resolved.tunnel_index + 1) % SKEDAR_SWARM_TUNNEL_COUNT;
	}
	state->placement_failures = 0;
	return SKEDAR_SWARM_SPAWN_COMMITTED;
}

static struct skedar_swarm_actor_slot *find_actor(struct skedar_swarm_encounter *state,
		struct skedar_swarm_actor actor)
{
	if (!actor.prop_generation) {
		return NULL;
	}
	if (same_actor(state->giant.actor, actor)) {
		return &state->giant;
	}
	for (int i = 0; i < state->config.max_live_raiders; i++) {
		if (same_actor(state->raiders[i].actor, actor)) {
			return &state->raiders[i];
		}
	}
	return NULL;
}

static void clear_attack(struct skedar_swarm_encounter *state, struct skedar_swarm_actor_slot *slot)
{
	if (slot->attack.sequence) {
		state->active_attacks--;
		state->active_by_kind[slot->attack.kind]--;
		slot->attack.sequence = 0;
		slot->hit_committed = 0;
	}
}

int skedarSwarmRetireActor(struct skedar_swarm_encounter *state, uint64_t stage_epoch,
		struct skedar_swarm_actor actor, enum skedar_swarm_retirement retirement)
{
	if (!stage_epoch || state->stage_epoch != stage_epoch || state->phase == SKEDAR_SWARM_INACTIVE
			|| (retirement != SKEDAR_SWARM_DEFEATED && retirement != SKEDAR_SWARM_REMOVED)) {
		return 0;
	}
	struct skedar_swarm_actor_slot *slot = find_actor(state, actor);
	if (!slot) {
		return 0;
	}
	clear_attack(state, slot);
	*slot = (struct skedar_swarm_actor_slot){0};
	if (slot == &state->giant) {
		state->phase = state->phase != SKEDAR_SWARM_FAILED && retirement == SKEDAR_SWARM_DEFEATED
			? SKEDAR_SWARM_COMPLETE : SKEDAR_SWARM_FAILED;
	} else {
		state->live_raiders--;
		refresh_phase(state);
	}
	return 1;
}

static void expire_attack(struct skedar_swarm_encounter *state,
		struct skedar_swarm_actor_slot *slot, uint32_t tick)
{
	if (slot->attack.sequence && tick - slot->attack.started_tick >= state->config.attacks.duration_ticks) {
		clear_attack(state, slot);
	}
}

int skedarSwarmAdvanceAttacks(struct skedar_swarm_encounter *state, uint64_t stage_epoch, uint32_t tick)
{
	if (!is_active(state, stage_epoch) || tick < state->last_attack_tick) {
		return 0;
	}
	if (tick == state->last_attack_tick) {
		return 1;
	}
	state->last_attack_tick = tick;
	state->starts_this_tick = 0;
	if (state->active_attacks) {
		expire_attack(state, &state->giant, tick);
		for (int i = 0; i < state->config.max_live_raiders && state->active_attacks; i++) {
			expire_attack(state, &state->raiders[i], tick);
		}
	}
	return 1;
}

static int target_qualified(const struct skedar_swarm_target *target, uint32_t tick)
{
	return target && target->actor.prop_generation && target->tick == tick
		&& (target->flags & SKEDAR_SWARM_TARGET_QUALIFIED) == SKEDAR_SWARM_TARGET_QUALIFIED;
}

int skedarSwarmBeginAttack(struct skedar_swarm_encounter *state, uint64_t stage_epoch,
		uint32_t tick, struct skedar_swarm_actor attacker, enum skedar_swarm_attack_kind kind,
		const struct skedar_swarm_target *target, struct skedar_swarm_attack *attack)
{
	if (!attack || (unsigned)kind >= SKEDAR_SWARM_ATTACK_KIND_COUNT
			|| !target_qualified(target, tick) || attacker.prop_index == target->actor.prop_index
			|| !skedarSwarmAdvanceAttacks(state, stage_epoch, tick)) {
		return 0;
	}
	const struct skedar_swarm_attack_limits *limits = &state->config.attacks;
	const unsigned airborne = state->active_by_kind[SKEDAR_SWARM_JUMP] + state->active_by_kind[SKEDAR_SWARM_DROP];
	if (state->active_attacks >= limits->active || state->active_by_kind[kind] >= limits->active_by_kind[kind]
			|| (kind != SKEDAR_SWARM_MELEE && airborne >= limits->airborne)
			|| state->starts_this_tick >= limits->starts_per_tick || state->attack_sequence == UINT32_MAX) {
		return 0;
	}
	struct skedar_swarm_actor_slot *slot = find_actor(state, attacker);
	if (!slot || slot->attack.sequence || tick < slot->next_attack_tick) {
		return 0;
	}
	slot->attack = (struct skedar_swarm_attack){stage_epoch, ++state->attack_sequence,
		kind, attacker, target->actor, tick};
	slot->next_attack_tick = later_tick(tick, limits->cooldown_ticks);
	state->active_attacks++;
	state->active_by_kind[kind]++;
	state->starts_this_tick++;
	*attack = slot->attack;
	return 1;
}

static struct skedar_swarm_actor_slot *find_attack(struct skedar_swarm_encounter *state,
		uint32_t tick, const struct skedar_swarm_attack *attack)
{
	if (!attack || !attack->sequence || !skedarSwarmAdvanceAttacks(state, attack->stage_epoch, tick)) {
		return NULL;
	}
	struct skedar_swarm_actor_slot *slot = find_actor(state, attack->attacker);
	if (!slot || slot->attack.sequence != attack->sequence
			|| slot->attack.kind != attack->kind || slot->attack.started_tick != attack->started_tick
			|| !same_actor(slot->attack.target, attack->target)) {
		return NULL;
	}
	return slot;
}

int skedarSwarmAcceptHit(struct skedar_swarm_encounter *state, uint32_t tick,
		const struct skedar_swarm_attack *attack, const struct skedar_swarm_target *target)
{
	struct skedar_swarm_actor_slot *slot = find_attack(state, tick, attack);
	if (!slot || slot->hit_committed || !target_qualified(target, tick)
			|| !same_actor(slot->attack.target, target->actor)
			|| tick - slot->attack.started_tick < state->config.attacks.telegraph_ticks) {
		return 0;
	}
	slot->hit_committed = 1;
	return 1;
}

int skedarSwarmEndAttack(struct skedar_swarm_encounter *state, uint32_t tick,
		const struct skedar_swarm_attack *attack)
{
	struct skedar_swarm_actor_slot *slot = find_attack(state, tick, attack);
	if (!slot) {
		return 0;
	}
	clear_attack(state, slot);
	return 1;
}
