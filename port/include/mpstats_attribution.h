#ifndef PD_MPSTATS_ATTRIBUTION_H
#define PD_MPSTATS_ATTRIBUTION_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mpstats_actor_kind {
	MPSTATS_ACTOR_INVALID = 0,
	MPSTATS_ACTOR_PLAYER,
	MPSTATS_ACTOR_BOT,
} mpstats_actor_kind_t;

typedef struct mpstats_actor_identity {
	s32 runtime_index;
	s32 config_slot;
	mpstats_actor_kind_t kind;
} mpstats_actor_identity_t;

/*
 * Validate the compact runtime-roster domain and classify the already-resolved
 * stable config slot. The helper is globals-free so sparse participant layouts
 * are covered by the same code used by the live mpstats consumer.
 */
s32 mpstatsAttributionResolve(s32 runtime_index, s32 runtime_count,
	s32 config_slot, s32 player_slot_count, s32 config_slot_count,
	mpstats_actor_identity_t *out_identity);

/*
 * Choose one compact runtime-roster shooter index. Wire authority wins for a
 * non-suicide; otherwise a valid live attacker is used, then the victim as the
 * suicide fallback. Stable player/config slots fail the runtime-count check.
 */
s32 mpstatsAttributionChooseShooterRuntimeIndex(
	s32 wire_attacker_runtime_index, s32 live_attacker_runtime_index,
	s32 victim_runtime_index, s32 runtime_count);

const char *mpstatsActorKindString(mpstats_actor_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* PD_MPSTATS_ATTRIBUTION_H */
