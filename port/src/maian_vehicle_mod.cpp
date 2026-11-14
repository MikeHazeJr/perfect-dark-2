#include "maian_vehicle_mod.h"

#include <cmath>

namespace {

bool flag(uint8_t value)
{
    return value != 0;
}

bool target_locked(const mvm_turret_input& input)
{
    return flag(input.lock_valid) && flag(input.has_target);
}

bool aim_and_lock_maintained(const mvm_turret_input& input)
{
    return !flag(input.boost_active) && flag(input.aim_maintained) &&
        target_locked(input);
}

bool same_target(const mvm_target_ref& a, const mvm_target_ref& b)
{
    return a.id == b.id && a.generation == b.generation;
}

bool state_valid(const mvm_projectile_state& state)
{
    switch (state.steer_state) {
    case MVM_STEER_UNGUIDED:
        return !flag(state.has_original_target);
    case MVM_STEER_ACTIVE:
    case MVM_STEER_STOPPED:
        return flag(state.has_original_target);
    default:
        return false;
    }
}

} // namespace

int32_t mvm_motion_update(mvm_motion_state* state,
                           const mvm_motion_parameters* parameters,
                           const mvm_motion_input* input,
                           mvm_motion_decision* out)
{
    if (state == nullptr || parameters == nullptr || input == nullptr || out == nullptr) {
        return MVM_ERR_NULL;
    }
    const mvm_motion_parameters& p = *parameters;
    if (!std::isfinite(p.top_speed_reduction) || p.top_speed_reduction <= 0.0 ||
        p.top_speed_reduction >= 1.0 ||
        !std::isfinite(p.acceleration_reduction) || p.acceleration_reduction <= 0.0 ||
        p.acceleration_reduction >= 1.0 ||
        !std::isfinite(p.attack_duration) || p.attack_duration <= 0.0 ||
        !std::isfinite(p.recovery_duration) || p.recovery_duration <= 0.0 ||
        !std::isfinite(p.recovery_hold_duration) || p.recovery_hold_duration <= 0.0 ||
        !std::isfinite(input->elapsed) || input->elapsed < 0.0) {
        return MVM_ERR_BAD_PARAMETER;
    }
    if (!std::isfinite(state->penalty) || state->penalty < 0.0 || state->penalty > 1.0 ||
        !std::isfinite(state->recovery_hold_remaining) ||
        state->recovery_hold_remaining < 0.0) {
        return MVM_ERR_BAD_STATE;
    }

    const bool active = !flag(input->boost_active) &&
        (flag(input->firing) || flag(input->actively_trying_lock));
    mvm_motion_state next = *state;
    if (input->elapsed > 0.0) {
        if (active) {
            // Compare before dividing so finite extreme inputs cannot overflow.
            const double rise = input->elapsed >= p.attack_duration
                ? 1.0 : input->elapsed / p.attack_duration;
            next.penalty = next.penalty + rise >= 1.0 ? 1.0 : next.penalty + rise;
            next.recovery_hold_remaining = next.recovery_hold_remaining >= p.recovery_hold_duration
                ? next.recovery_hold_remaining : p.recovery_hold_duration;
        } else if (input->elapsed >= next.recovery_hold_remaining) {
            const double recovering = input->elapsed - next.recovery_hold_remaining;
            next.recovery_hold_remaining = 0.0;
            const double fall = recovering >= p.recovery_duration
                ? 1.0 : recovering / p.recovery_duration;
            next.penalty = fall >= next.penalty ? 0.0 : next.penalty - fall;
        } else {
            next.recovery_hold_remaining -= input->elapsed;
        }
    }
    mvm_motion_decision decision;
    decision.top_speed_multiplier = 1.0 - p.top_speed_reduction * next.penalty;
    decision.acceleration_multiplier = 1.0 - p.acceleration_reduction * next.penalty;
    decision.steering_multiplier = 1.0;
    decision.activity_drives_penalty = active ? 1 : 0;
    *state = next;
    *out = decision;
    return MVM_OK;
}

int32_t mvm_evaluate_turret(const mvm_turret_input* input,
                            mvm_turret_decision* out)
{
    if (input == nullptr || out == nullptr) {
        return MVM_ERR_NULL;
    }

    mvm_turret_decision decision;
    decision.fire_permitted =
        flag(input->boost_active) ? static_cast<uint8_t>(0) : static_cast<uint8_t>(1);
    decision.stow_requested = 0;
    decision.cadence = aim_and_lock_maintained(*input)
        ? MVM_CADENCE_MAINTAINED_SLOW
        : MVM_CADENCE_ORDINARY;

    *out = decision;
    return MVM_OK;
}

int32_t mvm_projectile_launch(const mvm_turret_input* input,
                              mvm_projectile_state* out)
{
    if (input == nullptr || out == nullptr) {
        return MVM_ERR_NULL;
    }
    if (flag(input->boost_active)) {
        return MVM_REFUSED_BOOST;
    }

    mvm_projectile_state state;
    if (aim_and_lock_maintained(*input)) {
        state.steer_state = MVM_STEER_ACTIVE;
        state.has_original_target = 1;
        state.original_target = input->target;
    } else {
        state.steer_state = MVM_STEER_UNGUIDED;
        state.has_original_target = 0;
        state.original_target.id = 0;
        state.original_target.generation = 0;
    }

    *out = state;
    return MVM_OK;
}

int32_t mvm_projectile_update(mvm_projectile_state* state,
                              const mvm_turret_input* input,
                              uint8_t* out_steer_permitted)
{
    if (state == nullptr || input == nullptr || out_steer_permitted == nullptr) {
        return MVM_ERR_NULL;
    }
    if (!state_valid(*state)) {
        return MVM_ERR_BAD_STATE;
    }

    if (state->steer_state != MVM_STEER_ACTIVE) {
        *out_steer_permitted = 0;
        return MVM_OK;
    }

    const bool keep_steering = aim_and_lock_maintained(*input) &&
        same_target(input->target, state->original_target);

    if (keep_steering) {
        *out_steer_permitted = 1;
    } else {
        state->steer_state = MVM_STEER_STOPPED;
        *out_steer_permitted = 0;
    }
    return MVM_OK;
}
