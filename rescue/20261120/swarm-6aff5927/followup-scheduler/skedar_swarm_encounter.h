#ifndef PORT_SKEDAR_SWARM_ENCOUNTER_H
#define PORT_SKEDAR_SWARM_ENCOUNTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	SKEDAR_SWARM_TUNNEL_COUNT = 3,
	SKEDAR_SWARM_ALL_TUNNELS = (1u << SKEDAR_SWARM_TUNNEL_COUNT) - 1,
	SKEDAR_SWARM_MAX_WAVES = 16,
	SKEDAR_SWARM_MAX_RAIDERS = 4096,
};

enum skedar_swarm_phase {
	SKEDAR_SWARM_INACTIVE,
	SKEDAR_SWARM_DEFENSE,
	SKEDAR_SWARM_DRAINING,
	SKEDAR_SWARM_FINALE,
	SKEDAR_SWARM_COMPLETE,
	SKEDAR_SWARM_FAILED,
};

enum skedar_swarm_spawn_kind {
	SKEDAR_SWARM_RAIDER,
	SKEDAR_SWARM_GIANT,
};

enum skedar_swarm_spawn_result {
	SKEDAR_SWARM_SPAWN_STALE,
	SKEDAR_SWARM_SPAWN_COMMITTED,
	SKEDAR_SWARM_SPAWN_RETRY,
	SKEDAR_SWARM_SPAWN_FAILED,
};

enum skedar_swarm_retirement {
	SKEDAR_SWARM_DEFEATED,
	SKEDAR_SWARM_REMOVED,
};

/* A configured mask uses runtime tunnel ordinals. The stage adapter resolves
 * authored tunnel IDs and pads before calling the scheduler. */
struct skedar_swarm_wave {
	uint16_t raiders;
	uint8_t tunnel_mask;
	uint32_t delay_ticks;
};

enum skedar_swarm_attack_kind {
	SKEDAR_SWARM_MELEE,
	SKEDAR_SWARM_JUMP,
	SKEDAR_SWARM_DROP,
	SKEDAR_SWARM_ATTACK_KIND_COUNT,
};

enum {
	SKEDAR_SWARM_TARGET_LIVE = 1u << 0,
	SKEDAR_SWARM_TARGET_HOSTILE = 1u << 1,
	SKEDAR_SWARM_TARGET_VISIBLE = 1u << 2,
	SKEDAR_SWARM_TARGET_IN_RANGE = 1u << 3,
	SKEDAR_SWARM_TARGET_QUALIFIED = (1u << 4) - 1,
};

struct skedar_swarm_attack_limits {
	uint16_t active;
	uint16_t airborne;
	uint16_t active_by_kind[SKEDAR_SWARM_ATTACK_KIND_COUNT];
	uint16_t starts_per_tick;
	uint32_t telegraph_ticks;
	uint32_t duration_ticks;
	uint32_t cooldown_ticks;
};

struct skedar_swarm_config {
	uint16_t max_live_raiders;
	uint8_t wave_count;
	uint8_t max_placement_failures;
	uint32_t spawn_interval_ticks;
	struct skedar_swarm_attack_limits attacks;
	struct skedar_swarm_wave waves[SKEDAR_SWARM_MAX_WAVES];
};

struct skedar_swarm_actor {
	uint32_t prop_index;
	uint64_t prop_generation;
};

/* At most one placement is outstanding. Index -1 denotes the authored Giant
 * pad, which the stage adapter resolves separately. */
struct skedar_swarm_ticket {
	uint64_t stage_epoch;
	uint32_t sequence;
	uint32_t issued_tick;
	enum skedar_swarm_spawn_kind kind;
	int tunnel_index;
	int wave_index;
};

/* The stage adapter samples the same live target, LOS and range again at
 * the damage commit. A target slot and its generation form one identity. */
struct skedar_swarm_target {
	struct skedar_swarm_actor actor;
	uint32_t tick;
	uint8_t flags;
};

struct skedar_swarm_attack {
	uint64_t stage_epoch;
	uint32_t sequence;
	enum skedar_swarm_attack_kind kind;
	struct skedar_swarm_actor attacker;
	struct skedar_swarm_actor target;
	uint32_t started_tick;
};

struct skedar_swarm_actor_slot {
	struct skedar_swarm_actor actor;
	struct skedar_swarm_attack attack;
	uint32_t next_attack_tick;
	uint8_t hit_committed;
};

struct skedar_swarm_encounter {
	struct skedar_swarm_config config;
	uint64_t stage_epoch;
	enum skedar_swarm_phase phase;
	uint8_t closed_mask;
	uint8_t wave_index;
	uint8_t next_tunnel;
	uint8_t placement_failures;
	uint16_t wave_admitted;
	uint16_t live_raiders;
	uint32_t next_spawn_tick;
	uint32_t wave_ready_tick;
	uint32_t last_request_tick;
	uint32_t last_spawn_tick;
	uint32_t sequence;
	struct skedar_swarm_ticket pending;
	uint32_t attack_sequence;
	uint32_t last_attack_tick;
	uint16_t active_attacks;
	uint16_t active_by_kind[SKEDAR_SWARM_ATTACK_KIND_COUNT];
	uint16_t starts_this_tick;
	struct skedar_swarm_actor_slot raiders[SKEDAR_SWARM_MAX_RAIDERS];
	struct skedar_swarm_actor_slot giant;
};

/* Reinitialization always uses a fresh nonzero stage epoch. Stop follows
 * the adapter's retirement of engine actors and pending prepared props. */
int skedarSwarmInit(struct skedar_swarm_encounter *state,
		const struct skedar_swarm_config *config, uint64_t stage_epoch, uint32_t tick);
void skedarSwarmStop(struct skedar_swarm_encounter *state);

/* Only the stage authority calls these functions. A tunnel is closed after its
 * physical blocker commits. Resolve or drop each placement in the issuing
 * tick. The next tick retires any lost callback. A stale or failed ticket
 * may never publish a prepared prop. */
int skedarSwarmCloseTunnel(struct skedar_swarm_encounter *state,
		uint64_t stage_epoch, int tunnel_index, int blocker_committed);
int skedarSwarmRequestSpawn(struct skedar_swarm_encounter *state,
		uint64_t stage_epoch, uint32_t tick, struct skedar_swarm_ticket *ticket);
enum skedar_swarm_spawn_result skedarSwarmResolveSpawn(struct skedar_swarm_encounter *state,
		const struct skedar_swarm_ticket *ticket, uint32_t tick, struct skedar_swarm_actor actor);
int skedarSwarmRetireActor(struct skedar_swarm_encounter *state, uint64_t stage_epoch,
		struct skedar_swarm_actor actor, enum skedar_swarm_retirement retirement);

/* Advance every authoritative tick, including ticks without new attacks. A
 * successful hit authorizes one damage commit by the caller. */
int skedarSwarmAdvanceAttacks(struct skedar_swarm_encounter *state, uint64_t stage_epoch, uint32_t tick);
int skedarSwarmBeginAttack(struct skedar_swarm_encounter *state, uint64_t stage_epoch,
		uint32_t tick, struct skedar_swarm_actor attacker, enum skedar_swarm_attack_kind kind,
		const struct skedar_swarm_target *target, struct skedar_swarm_attack *attack);
int skedarSwarmAcceptHit(struct skedar_swarm_encounter *state, uint32_t tick,
		const struct skedar_swarm_attack *attack, const struct skedar_swarm_target *target);
int skedarSwarmEndAttack(struct skedar_swarm_encounter *state, uint32_t tick,
		const struct skedar_swarm_attack *attack);

#ifdef __cplusplus
}
#endif
#endif
