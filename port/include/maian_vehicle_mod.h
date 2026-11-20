#ifndef MAIAN_VEHICLE_MOD_H
#define MAIAN_VEHICLE_MOD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standalone turret cadence/admission and projectile guidance-permission
 * component. This is not a native or client ABI. It has no global state,
 * heap use, I/O, hidden timing, trajectory, velocity or engine/object pointers.
 * The motion envelope below uses only caller-supplied elapsed time.
 *
 * Flags are uint8_t: zero is false, any nonzero value is true.
 * Enum-like and status fields are int32_t with the named constants below.
 * Every function returns a status that the caller must check. On any
 * non-MVM_OK status, nothing is written through any pointer argument.
 */

/* Status codes */
#define MVM_OK            ((int32_t)0)
#define MVM_ERR_NULL      ((int32_t)1)
#define MVM_REFUSED_BOOST ((int32_t)2)
#define MVM_ERR_BAD_STATE ((int32_t)3)
#define MVM_ERR_BAD_PARAMETER ((int32_t)4)

/* Symbolic cadence selection; mapping to real timing is host-defined. */
#define MVM_CADENCE_ORDINARY        ((int32_t)0)
#define MVM_CADENCE_MAINTAINED_SLOW ((int32_t)1)

/* Projectile steering states */
#define MVM_STEER_UNGUIDED ((int32_t)0)
#define MVM_STEER_ACTIVE   ((int32_t)1)
#define MVM_STEER_STOPPED  ((int32_t)2)

/*
 * Opaque target reference. Both fields are compared exactly; every value,
 * including zero and the maximum, is an ordinary valid identity when the
 * associated presence flag is set.
 */
typedef struct mvm_target_ref {
    uint64_t id;
    uint64_t generation;
} mvm_target_ref;

/*
 * Immutable caller-built snapshot. Never written by this component.
 * target is meaningful only when has_target is nonzero.
 * A lock counts only when lock_valid and has_target are both nonzero.
 * Maintained means aim_maintained plus a counted lock, with boost inactive.
 * Boost breaks lock; stale caller lock flags cannot override it.
 */
typedef struct mvm_turret_input {
    uint8_t boost_active;
    uint8_t aim_maintained;
    uint8_t lock_valid;
    uint8_t has_target;
    mvm_target_ref target;
} mvm_turret_input;

/*
 * fire_permitted: 0 while boost is active (no new shots), otherwise 1.
 * stow_requested: always 0; this component never requests stowing.
 * cadence: MVM_CADENCE_MAINTAINED_SLOW when maintained, else ORDINARY.
 */
typedef struct mvm_turret_decision {
    uint8_t fire_permitted;
    uint8_t stow_requested;
    int32_t cadence;
} mvm_turret_decision;

/*
 * Valid state invariants:
 *   MVM_STEER_UNGUIDED: has_original_target == 0.
 *   MVM_STEER_ACTIVE:   has_original_target != 0.
 *   MVM_STEER_STOPPED:  has_original_target != 0.
 * Any other steer_state value, or a violated invariant, is malformed.
 * original_target is written only at launch and never changed afterwards.
 */
typedef struct mvm_projectile_state {
    int32_t steer_state;
    uint8_t has_original_target;
    mvm_target_ref original_target;
} mvm_projectile_state;

/*
 * Caller-selected mod tuning; there are deliberately no defaults.
 * Reductions must be finite and strictly between 0 and 1 (fraction of
 * ordinary speed/acceleration removed at full penalty). Attack, recovery
 * and recovery-hold durations must be finite and strictly positive, using
 * the same time unit as elapsed below. Attack/recovery are full-range ramp
 * durations. Hold provides a supplied anti-tap linger before recovery.
 * Changes in tuning do not clear stored penalty or shorten a stored hold;
 * authoritative callers own tuning/lifecycle/serialization policy.
 */
typedef struct mvm_motion_parameters {
    double top_speed_reduction;
    double acceleration_reduction;
    double attack_duration;
    double recovery_duration;
    double recovery_hold_duration;
} mvm_motion_parameters;

/*
 * Piecewise-constant gunner activity over the supplied elapsed interval.
 * firing OR actively_trying_lock causes attack; boost suppresses both.
 * charging is descriptive only. Charge alone does not imply either activity:
 * the authoritative caller must explicitly supply firing/trying-lock if a
 * charging action actually includes them. No charge eligibility is decided.
 * There is no projectile-lifetime/count input; existing shots cannot extend
 * the penalty. Split intervals at actual activity/boost transitions.
 */
typedef struct mvm_motion_input {
    uint8_t boost_active;
    uint8_t firing;
    uint8_t actively_trying_lock;
    uint8_t charging;
    double elapsed;
} mvm_motion_input;

/*
 * Per-vehicle persistent state; {0, 0} is valid initial rest state.
 * penalty must be finite in [0,1]; recovery_hold_remaining finite >= 0.
 * No reinitialization on trigger release, reacquisition or boost: repeated
 * taps resume the stored penalty and renew hold instead of resetting it.
 */
typedef struct mvm_motion_state {
    double penalty;
    double recovery_hold_remaining;
} mvm_motion_state;

typedef struct mvm_motion_decision {
    double top_speed_multiplier;
    double acceleration_multiplier;
    double steering_multiplier; /* Always 1: steering remains responsive. */
    uint8_t activity_drives_penalty;
} mvm_motion_decision;

/*
 * Validates all pointers, tuning, elapsed (finite >= 0), and state before
 * writing anything. On success, advances penalty linearly towards 1 during
 * effective activity; otherwise consumes remaining hold then ramps to 0.
 * Output describes end-of-interval multipliers, not integrated displacement.
 * elapsed == 0 preserves state, including hold, and reports its multipliers.
 * This never applies velocity, steering, boost, ammo or engine effects.
 * Preconditions: pointed-to objects independent and non-overlapping.
 * Returns MVM_OK, MVM_ERR_NULL, MVM_ERR_BAD_PARAMETER or MVM_ERR_BAD_STATE.
 */
int32_t mvm_motion_update(mvm_motion_state* state,
                           const mvm_motion_parameters* parameters,
                           const mvm_motion_input* input,
                           mvm_motion_decision* out);

/*
 * Selects admission and symbolic cadence for a potential new shot.
 * Preconditions: input and out are non-null, independent and
 * non-overlapping.
 * Returns MVM_OK or MVM_ERR_NULL.
 */
int32_t mvm_evaluate_turret(const mvm_turret_input* input,
                            mvm_turret_decision* out);

/*
 * Creates projectile steering state for a new shot.
 * Boost active: returns MVM_REFUSED_BOOST and leaves *out unchanged.
 * Maintained: ACTIVE, capturing input->target once as original_target.
 * Otherwise: UNGUIDED with has_original_target = 0 and a zeroed ref.
 * Preconditions: input and out are non-null, independent and
 * non-overlapping.
 * Returns MVM_OK, MVM_ERR_NULL or MVM_REFUSED_BOOST.
 */
int32_t mvm_projectile_launch(const mvm_turret_input* input,
                              mvm_projectile_state* out);

/*
 * Reports whether an existing projectile may still be steered.
 * UNGUIDED and STOPPED: permit 0, state unchanged.
 * ACTIVE: permit 1 only while maintained and input->target matches
 * original_target in both id and generation; otherwise the state becomes
 * STOPPED permanently and permit is 0. Boost immediately causes that loss,
 * even when the caller's aim/lock flags still report the original target.
 * original_target is never modified.
 * Preconditions: all pointers non-null, independent and non-overlapping;
 * *state is a valid state per the invariants above.
 * Returns MVM_OK, MVM_ERR_NULL or MVM_ERR_BAD_STATE.
 */
int32_t mvm_projectile_update(mvm_projectile_state* state,
                              const mvm_turret_input* input,
                              uint8_t* out_steer_permitted);

#ifdef __cplusplus
}
#endif

#endif /* MAIAN_VEHICLE_MOD_H */
