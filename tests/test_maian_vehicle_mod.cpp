#include "maian_vehicle_mod.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <initializer_list>

#define CHECK(cond)          \
    do {                     \
        if (!(cond)) {       \
            ++failures;      \
        }                    \
    } while (0)

namespace {

const uint64_t kMax64 = ~static_cast<uint64_t>(0);

mvm_turret_input make_input(uint8_t boost, uint8_t aim, uint8_t lock, uint8_t has,
                            uint64_t id, uint64_t generation)
{
    mvm_turret_input input;
    std::memset(&input, 0, sizeof input);
    input.boost_active = boost;
    input.aim_maintained = aim;
    input.lock_valid = lock;
    input.has_target = has;
    input.target.id = id;
    input.target.generation = generation;
    return input;
}

mvm_turret_input maintained(uint64_t id, uint64_t generation)
{
    return make_input(0, 1, 1, 1, id, generation);
}

bool input_equal(const mvm_turret_input& a, const mvm_turret_input& b)
{
    return a.boost_active == b.boost_active &&
           a.aim_maintained == b.aim_maintained &&
           a.lock_valid == b.lock_valid &&
           a.has_target == b.has_target &&
           a.target.id == b.target.id &&
           a.target.generation == b.target.generation;
}

bool original_is(const mvm_projectile_state& s, uint64_t id, uint64_t generation)
{
    return s.has_original_target != 0 &&
           s.original_target.id == id &&
           s.original_target.generation == generation;
}

mvm_projectile_state launch_active(int& failures, uint64_t id, uint64_t generation)
{
    mvm_turret_input input = maintained(id, generation);
    mvm_projectile_state state;
    std::memset(&state, 0, sizeof state);
    CHECK(mvm_projectile_launch(&input, &state) == MVM_OK);
    CHECK(state.steer_state == MVM_STEER_ACTIVE);
    CHECK(original_is(state, id, generation));
    return state;
}

void test_cadence_and_admission(int& failures)
{
    for (int mask = 0; mask < 16; ++mask) {
        const uint8_t boost = static_cast<uint8_t>((mask >> 0) & 1);
        const uint8_t aim = static_cast<uint8_t>((mask >> 1) & 1);
        const uint8_t lock = static_cast<uint8_t>((mask >> 2) & 1);
        const uint8_t has = static_cast<uint8_t>((mask >> 3) & 1);
        const bool is_maintained = !boost && aim && lock && has;

        const mvm_turret_input input = make_input(boost, aim, lock, has, 7, 3);
        const mvm_turret_input before = input;

        mvm_turret_decision decision;
        std::memset(&decision, 0xA5, sizeof decision);
        CHECK(mvm_evaluate_turret(&input, &decision) == MVM_OK);
        CHECK(decision.fire_permitted == (boost ? 0 : 1));
        CHECK(decision.stow_requested == 0);
        CHECK(decision.cadence == (is_maintained ? MVM_CADENCE_MAINTAINED_SLOW
                                                 : MVM_CADENCE_ORDINARY));
        CHECK(input_equal(input, before));

        mvm_projectile_state state;
        std::memset(&state, 0xA5, sizeof state);
        mvm_projectile_state state_before;
        std::memcpy(&state_before, &state, sizeof state);
        const int32_t status = mvm_projectile_launch(&input, &state);
        if (boost) {
            CHECK(status == MVM_REFUSED_BOOST);
            CHECK(std::memcmp(&state, &state_before, sizeof state) == 0);
        } else if (is_maintained) {
            CHECK(status == MVM_OK);
            CHECK(state.steer_state == MVM_STEER_ACTIVE);
            CHECK(state.has_original_target == 1);
            CHECK(original_is(state, 7, 3));
        } else {
            CHECK(status == MVM_OK);
            CHECK(state.steer_state == MVM_STEER_UNGUIDED);
            CHECK(state.has_original_target == 0);
        }
        CHECK(input_equal(input, before));
    }
}

void test_boost_refusal_keeps_candidate(int& failures)
{
    mvm_projectile_state candidate = launch_active(failures, 11, 12);
    mvm_projectile_state before;
    std::memcpy(&before, &candidate, sizeof candidate);

    const mvm_turret_input input = make_input(1, 1, 1, 1, 99, 98);
    CHECK(mvm_projectile_launch(&input, &candidate) == MVM_REFUSED_BOOST);
    CHECK(std::memcmp(&candidate, &before, sizeof candidate) == 0);
}

void test_noncanonical_true_flags(int& failures)
{
    const uint8_t values[] = {2, 0x80, 0xFF};
    for (unsigned i = 0; i < sizeof values; ++i) {
        const uint8_t t = values[i];

        mvm_turret_input input = make_input(0, t, t, t, 5, 6);
        mvm_turret_decision decision;
        CHECK(mvm_evaluate_turret(&input, &decision) == MVM_OK);
        CHECK(decision.fire_permitted == 1);
        CHECK(decision.cadence == MVM_CADENCE_MAINTAINED_SLOW);

        mvm_projectile_state state;
        CHECK(mvm_projectile_launch(&input, &state) == MVM_OK);
        CHECK(state.steer_state == MVM_STEER_ACTIVE);

        uint8_t permit = 0xA5;
        CHECK(mvm_projectile_update(&state, &input, &permit) == MVM_OK);
        CHECK(permit == 1);

        state.has_original_target = t;
        CHECK(mvm_projectile_update(&state, &input, &permit) == MVM_OK);
        CHECK(permit == 1);

        mvm_turret_input boosted = make_input(t, 1, 1, 1, 5, 6);
        CHECK(mvm_evaluate_turret(&boosted, &decision) == MVM_OK);
        CHECK(decision.fire_permitted == 0);
        CHECK(decision.stow_requested == 0);
        CHECK(mvm_projectile_launch(&boosted, &state) == MVM_REFUSED_BOOST);
    }
}

void test_boost_during_guidance(int& failures)
{
    mvm_projectile_state state = launch_active(failures, 21, 22);

    const mvm_turret_input boosted = make_input(1, 1, 1, 1, 21, 22);
    for (int i = 0; i < 3; ++i) {
        uint8_t permit = 0xA5;
        CHECK(mvm_projectile_update(&state, &boosted, &permit) == MVM_OK);
        CHECK(permit == 0);
        CHECK(state.steer_state == MVM_STEER_STOPPED);
        CHECK(original_is(state, 21, 22));
    }

    mvm_turret_decision decision;
    CHECK(mvm_evaluate_turret(&boosted, &decision) == MVM_OK);
    CHECK(decision.fire_permitted == 0);
    CHECK(decision.stow_requested == 0);
    CHECK(decision.cadence == MVM_CADENCE_ORDINARY);

    const mvm_turret_input boosted_lost = make_input(1, 0, 1, 1, 21, 22);
    uint8_t permit = 0xA5;
    CHECK(mvm_projectile_update(&state, &boosted_lost, &permit) == MVM_OK);
    CHECK(permit == 0);
    CHECK(state.steer_state == MVM_STEER_STOPPED);
    CHECK(original_is(state, 21, 22));

    const mvm_turret_input unboosted = maintained(21, 22);
    CHECK(mvm_projectile_update(&state, &unboosted, &permit) == MVM_OK);
    CHECK(permit == 0);
    CHECK(state.steer_state == MVM_STEER_STOPPED);
}

void expect_stop(int& failures, const mvm_turret_input& input)
{
    mvm_projectile_state state = launch_active(failures, 31, 32);
    const mvm_turret_input before = input;
    uint8_t permit = 0xA5;
    CHECK(mvm_projectile_update(&state, &input, &permit) == MVM_OK);
    CHECK(permit == 0);
    CHECK(state.steer_state == MVM_STEER_STOPPED);
    CHECK(original_is(state, 31, 32));
    CHECK(input_equal(input, before));
}

void test_loss_conditions(int& failures)
{
    expect_stop(failures, maintained(31, 33));
    expect_stop(failures, maintained(30, 32));
    expect_stop(failures, maintained(99, 99));
    expect_stop(failures, make_input(0, 0, 1, 1, 31, 32));
    expect_stop(failures, make_input(0, 1, 0, 1, 31, 32));
    expect_stop(failures, make_input(0, 1, 1, 0, 31, 32));
    expect_stop(failures, make_input(0, 0, 0, 0, 31, 32));
}

void test_permanent_stop(int& failures)
{
    mvm_projectile_state state = launch_active(failures, 41, 42);

    uint8_t permit = 0xA5;
    const mvm_turret_input lost = make_input(0, 1, 0, 1, 41, 42);
    CHECK(mvm_projectile_update(&state, &lost, &permit) == MVM_OK);
    CHECK(permit == 0);
    CHECK(state.steer_state == MVM_STEER_STOPPED);

    const mvm_turret_input same = maintained(41, 42);
    const mvm_turret_input other = maintained(43, 44);
    for (int i = 0; i < 4; ++i) {
        permit = 0xA5;
        CHECK(mvm_projectile_update(&state, &same, &permit) == MVM_OK);
        CHECK(permit == 0);
        CHECK(state.steer_state == MVM_STEER_STOPPED);
        CHECK(original_is(state, 41, 42));

        permit = 0xA5;
        CHECK(mvm_projectile_update(&state, &other, &permit) == MVM_OK);
        CHECK(permit == 0);
        CHECK(state.steer_state == MVM_STEER_STOPPED);
        CHECK(original_is(state, 41, 42));
    }
}

void test_repeated_maintained_updates(int& failures)
{
    mvm_projectile_state state = launch_active(failures, 51, 52);
    const mvm_turret_input input = maintained(51, 52);
    for (int i = 0; i < 8; ++i) {
        uint8_t permit = 0xA5;
        CHECK(mvm_projectile_update(&state, &input, &permit) == MVM_OK);
        CHECK(permit == 1);
        CHECK(state.steer_state == MVM_STEER_ACTIVE);
        CHECK(original_is(state, 51, 52));
    }
}

void test_unguided_launch(int& failures)
{
    const mvm_turret_input no_lock = make_input(0, 1, 0, 1, 61, 62);
    mvm_projectile_state state;
    std::memset(&state, 0xA5, sizeof state);
    CHECK(mvm_projectile_launch(&no_lock, &state) == MVM_OK);
    CHECK(state.steer_state == MVM_STEER_UNGUIDED);
    CHECK(state.has_original_target == 0);

    mvm_projectile_state before;
    std::memcpy(&before, &state, sizeof state);

    const mvm_turret_input later = maintained(61, 62);
    for (int i = 0; i < 3; ++i) {
        uint8_t permit = 0xA5;
        CHECK(mvm_projectile_update(&state, &later, &permit) == MVM_OK);
        CHECK(permit == 0);
        CHECK(std::memcmp(&state, &before, sizeof state) == 0);
    }
}

void test_zero_and_max_identities(int& failures)
{
    const uint64_t ids[] = {0, kMax64};
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned j = 0; j < 2; ++j) {
            const uint64_t id = ids[i];
            const uint64_t gen = ids[j];

            mvm_projectile_state state = launch_active(failures, id, gen);
            const mvm_turret_input same = maintained(id, gen);
            uint8_t permit = 0xA5;
            CHECK(mvm_projectile_update(&state, &same, &permit) == MVM_OK);
            CHECK(permit == 1);
            CHECK(state.steer_state == MVM_STEER_ACTIVE);

            const mvm_turret_input other_gen = maintained(id, ids[1 - j]);
            CHECK(mvm_projectile_update(&state, &other_gen, &permit) == MVM_OK);
            CHECK(permit == 0);
            CHECK(state.steer_state == MVM_STEER_STOPPED);
            CHECK(original_is(state, id, gen));
        }
    }

    mvm_projectile_state state = launch_active(failures, kMax64, kMax64);
    const mvm_turret_input near = maintained(kMax64 - 1, kMax64);
    uint8_t permit = 0xA5;
    CHECK(mvm_projectile_update(&state, &near, &permit) == MVM_OK);
    CHECK(permit == 0);
    CHECK(original_is(state, kMax64, kMax64));
}

void test_null_arguments(int& failures)
{
    const mvm_turret_input input = maintained(71, 72);
    const mvm_turret_input input_before = input;

    mvm_turret_decision decision;
    std::memset(&decision, 0xA5, sizeof decision);
    mvm_turret_decision decision_before;
    std::memcpy(&decision_before, &decision, sizeof decision);
    CHECK(mvm_evaluate_turret(nullptr, &decision) == MVM_ERR_NULL);
    CHECK(mvm_evaluate_turret(&input, nullptr) == MVM_ERR_NULL);
    CHECK(mvm_evaluate_turret(nullptr, nullptr) == MVM_ERR_NULL);
    CHECK(std::memcmp(&decision, &decision_before, sizeof decision) == 0);

    mvm_projectile_state launched;
    std::memset(&launched, 0xA5, sizeof launched);
    mvm_projectile_state launched_before;
    std::memcpy(&launched_before, &launched, sizeof launched);
    CHECK(mvm_projectile_launch(nullptr, &launched) == MVM_ERR_NULL);
    CHECK(mvm_projectile_launch(&input, nullptr) == MVM_ERR_NULL);
    CHECK(mvm_projectile_launch(nullptr, nullptr) == MVM_ERR_NULL);
    CHECK(std::memcmp(&launched, &launched_before, sizeof launched) == 0);

    const mvm_turret_input boosted = make_input(1, 1, 1, 1, 71, 72);
    CHECK(mvm_projectile_launch(&boosted, nullptr) == MVM_ERR_NULL);

    mvm_projectile_state state = launch_active(failures, 71, 72);
    mvm_projectile_state state_before;
    std::memcpy(&state_before, &state, sizeof state);
    const mvm_turret_input lost = make_input(0, 0, 0, 0, 0, 0);
    uint8_t permit = 0xA5;
    CHECK(mvm_projectile_update(nullptr, &lost, &permit) == MVM_ERR_NULL);
    CHECK(mvm_projectile_update(&state, nullptr, &permit) == MVM_ERR_NULL);
    CHECK(mvm_projectile_update(&state, &lost, nullptr) == MVM_ERR_NULL);
    CHECK(mvm_projectile_update(nullptr, nullptr, nullptr) == MVM_ERR_NULL);
    CHECK(permit == 0xA5);
    CHECK(std::memcmp(&state, &state_before, sizeof state) == 0);

    CHECK(input_equal(input, input_before));
}

void expect_bad_state(int& failures, int32_t steer_state, uint8_t has_original)
{
    mvm_projectile_state state;
    std::memset(&state, 0, sizeof state);
    state.steer_state = steer_state;
    state.has_original_target = has_original;
    state.original_target.id = 81;
    state.original_target.generation = 82;

    mvm_projectile_state before;
    std::memcpy(&before, &state, sizeof state);

    const mvm_turret_input input = maintained(81, 82);
    const mvm_turret_input input_before = input;
    uint8_t permit = 0xA5;
    CHECK(mvm_projectile_update(&state, &input, &permit) == MVM_ERR_BAD_STATE);
    CHECK(permit == 0xA5);
    CHECK(std::memcmp(&state, &before, sizeof state) == 0);
    CHECK(input_equal(input, input_before));
}

void test_malformed_state(int& failures)
{
    const int32_t int32_max = static_cast<int32_t>(0x7FFFFFFF);
    const int32_t int32_min = -int32_max - 1;

    expect_bad_state(failures, 3, 1);
    expect_bad_state(failures, -1, 1);
    expect_bad_state(failures, int32_max, 1);
    expect_bad_state(failures, int32_min, 0);
    expect_bad_state(failures, MVM_STEER_UNGUIDED, 1);
    expect_bad_state(failures, MVM_STEER_UNGUIDED, 0xFF);
    expect_bad_state(failures, MVM_STEER_ACTIVE, 0);
    expect_bad_state(failures, MVM_STEER_STOPPED, 0);
}

void test_velocity_canary(int& failures)
{
    // Memory-boundary check only: confirms writes stay inside the state
    // object. It is not physics or gameplay evidence.
    struct Guarded {
        mvm_projectile_state state;
        double velocity[3];
    };

    Guarded guarded;
    std::memset(&guarded, 0, sizeof guarded);
    guarded.velocity[0] = 1.5;
    guarded.velocity[1] = -2.25;
    guarded.velocity[2] = 4.0;
    double velocity_before[3];
    std::memcpy(velocity_before, guarded.velocity, sizeof velocity_before);

    const mvm_turret_input input = maintained(91, 92);
    CHECK(mvm_projectile_launch(&input, &guarded.state) == MVM_OK);
    CHECK(std::memcmp(guarded.velocity, velocity_before, sizeof velocity_before) == 0);

    uint8_t permit = 0xA5;
    CHECK(mvm_projectile_update(&guarded.state, &input, &permit) == MVM_OK);
    CHECK(std::memcmp(guarded.velocity, velocity_before, sizeof velocity_before) == 0);

    const mvm_turret_input lost = make_input(0, 1, 1, 1, 91, 93);
    CHECK(mvm_projectile_update(&guarded.state, &lost, &permit) == MVM_OK);
    CHECK(guarded.state.steer_state == MVM_STEER_STOPPED);
    CHECK(std::memcmp(guarded.velocity, velocity_before, sizeof velocity_before) == 0);
}

// Values below are deliberately synthetic test vectors, never mod defaults.
mvm_motion_parameters motion_parameters()
{
    return mvm_motion_parameters{0.25, 0.5, 4.0, 8.0, 2.0};
}

mvm_motion_input motion_input(double elapsed, uint8_t firing = 0,
    uint8_t locking = 0, uint8_t boost = 0, uint8_t charging = 0)
{
    return mvm_motion_input{boost, firing, locking, charging, elapsed};
}

void motion_step(int& failures, mvm_motion_state& state,
    const mvm_motion_input& input, mvm_motion_decision& decision)
{
    const mvm_motion_parameters p = motion_parameters();
    CHECK(mvm_motion_update(&state, &p, &input, &decision) == MVM_OK);
    CHECK(decision.steering_multiplier == 1.0);
}

void test_motion_attack_recovery(int& failures)
{
    mvm_motion_state state{0.0, 0.0};
    mvm_motion_decision out{};
    motion_step(failures, state, motion_input(1.0, 1), out);
    CHECK(state.penalty == 0.25);
    CHECK(state.recovery_hold_remaining == 2.0);
    CHECK(out.top_speed_multiplier == 0.9375);
    CHECK(out.acceleration_multiplier == 0.875);
    CHECK(out.activity_drives_penalty == 1);
    motion_step(failures, state, motion_input(3.0, 0, 1), out);
    CHECK(state.penalty == 1.0);
    CHECK(out.top_speed_multiplier == 0.75);
    CHECK(out.acceleration_multiplier == 0.5);
    motion_step(failures, state, motion_input(1.0), out);
    CHECK(state.penalty == 1.0);
    CHECK(state.recovery_hold_remaining == 1.0);
    CHECK(out.activity_drives_penalty == 0);
    motion_step(failures, state, motion_input(3.0), out);
    CHECK(state.penalty == 0.75); // Remaining 1 hold, then 2 recovery.
    motion_step(failures, state, motion_input(100.0), out);
    CHECK(state.penalty == 0.0);
    CHECK(state.recovery_hold_remaining == 0.0);
    CHECK(out.top_speed_multiplier == 1.0);
    CHECK(out.acceleration_multiplier == 1.0);
}

void test_motion_taps_resume_and_renew(int& failures)
{
    mvm_motion_state state{0.0, 0.0};
    mvm_motion_decision out{};
    for (int i = 1; i <= 4; ++i) {
        motion_step(failures, state, motion_input(0.5, 1), out);
        CHECK(state.penalty == i * 0.125);
        CHECK(state.recovery_hold_remaining == 2.0);
        motion_step(failures, state, motion_input(0.5), out);
        CHECK(state.penalty == i * 0.125);
        CHECK(state.recovery_hold_remaining == 1.5);
    }
    motion_step(failures, state, motion_input(2.5), out);
    CHECK(state.penalty == 0.375);
    motion_step(failures, state, motion_input(0.5, 0, 1), out);
    CHECK(state.penalty == 0.5); // Does not start again from rest.
    CHECK(state.recovery_hold_remaining == 2.0);
}

void test_motion_activity_charge_boost(int& failures)
{
    mvm_motion_decision out{};
    for (int mask = 0; mask < 16; ++mask) {
        mvm_motion_state state{0.0, 0.0};
        const mvm_motion_input in = motion_input(1.0,
            (mask & 1) ? 0xFF : 0, (mask & 2) ? 0x80 : 0,
            (mask & 4) ? 2 : 0, (mask & 8) ? 1 : 0);
        const bool active = !in.boost_active && (in.firing || in.actively_trying_lock);
        motion_step(failures, state, in, out);
        CHECK(state.penalty == (active ? 0.25 : 0.0));
        CHECK(out.activity_drives_penalty == (active ? 1 : 0));
    }
    mvm_motion_state state{0.0, 0.0};
    motion_step(failures, state, motion_input(1.0, 0, 0, 0, 1), out);
    CHECK(state.penalty == 0.0); // Charge alone makes no policy assumption.
    motion_step(failures, state, motion_input(1.0, 0, 1, 0, 1), out);
    CHECK(state.penalty == 0.25); // Charging while actually trying a lock.
    motion_step(failures, state, motion_input(1.0, 1), out);
    CHECK(state.penalty == 0.5);
    motion_step(failures, state, motion_input(1.0, 1, 1, 1), out);
    CHECK(state.penalty == 0.5); // Boost enters recovery, never resets penalty.
    CHECK(state.recovery_hold_remaining == 1.0);
    motion_step(failures, state, motion_input(3.0, 1, 1, 1), out);
    CHECK(state.penalty == 0.25);
}

void test_motion_zero_time_and_partition(int& failures)
{
    const mvm_motion_parameters p = motion_parameters();
    mvm_motion_state a{0.25, 0.75};
    const mvm_motion_state before = a;
    mvm_motion_decision out{};
    for (int active = 0; active < 2; ++active) {
        motion_step(failures, a, motion_input(0.0, static_cast<uint8_t>(active)), out);
        CHECK(a.penalty == before.penalty);
        CHECK(a.recovery_hold_remaining == before.recovery_hold_remaining);
    }
    mvm_motion_state b = a;
    motion_step(failures, a, motion_input(2.0, 1), out);
    for (int i = 0; i < 8; ++i) motion_step(failures, b, motion_input(0.25, 1), out);
    CHECK(a.penalty == b.penalty);
    CHECK(a.recovery_hold_remaining == b.recovery_hold_remaining);
    motion_step(failures, a, motion_input(5.0), out);
    for (int i = 0; i < 20; ++i) motion_step(failures, b, motion_input(0.25), out);
    CHECK(a.penalty == b.penalty);
    CHECK(a.recovery_hold_remaining == b.recovery_hold_remaining);
    // A tuning change must not erase historical hold/penalty.
    mvm_motion_parameters changed = p;
    changed.recovery_hold_duration = 0.25;
    a = mvm_motion_state{0.25, 2.0};
    const mvm_motion_input active = motion_input(0.25, 1);
    CHECK(mvm_motion_update(&a, &changed, &active, &out) == MVM_OK);
    CHECK(a.penalty == 0.3125);
    CHECK(a.recovery_hold_remaining == 2.0);
}

void expect_motion_refusal(int& failures, const mvm_motion_parameters& p,
    const mvm_motion_input& input, const mvm_motion_state& initial, int32_t status)
{
    mvm_motion_state state;
    std::memcpy(&state, &initial, sizeof state);
    mvm_motion_decision out;
    std::memset(&out, 0xA5, sizeof out);
    mvm_motion_decision before;
    std::memcpy(&before, &out, sizeof out);
    mvm_motion_parameters parameter_before;
    mvm_motion_input input_before;
    std::memcpy(&parameter_before, &p, sizeof p);
    std::memcpy(&input_before, &input, sizeof input);
    CHECK(mvm_motion_update(&state, &p, &input, &out) == status);
    CHECK(std::memcmp(&state, &initial, sizeof state) == 0);
    CHECK(std::memcmp(&out, &before, sizeof out) == 0);
    CHECK(std::memcmp(&p, &parameter_before, sizeof p) == 0);
    CHECK(std::memcmp(&input, &input_before, sizeof input) == 0);
}

void test_motion_invalid_domains(int& failures)
{
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const mvm_motion_state valid{0.5, 1.0};
    const mvm_motion_input in = motion_input(1.0, 1);
    const double bad[] = {-1.0, inf, -inf, nan, 0.0};
    double mvm_motion_parameters::* fields[] = {
        &mvm_motion_parameters::top_speed_reduction,
        &mvm_motion_parameters::acceleration_reduction,
        &mvm_motion_parameters::attack_duration,
        &mvm_motion_parameters::recovery_duration,
        &mvm_motion_parameters::recovery_hold_duration
    };
    for (auto field : fields) {
        for (double value : bad) {
            mvm_motion_parameters p = motion_parameters();
            p.*field = value;
            expect_motion_refusal(failures, p, in, valid, MVM_ERR_BAD_PARAMETER);
        }
    }
    for (double value : {1.0, 1.5}) {
        mvm_motion_parameters p = motion_parameters();
        p.top_speed_reduction = value;
        expect_motion_refusal(failures, p, in, valid, MVM_ERR_BAD_PARAMETER);
        p = motion_parameters();
        p.acceleration_reduction = value;
        expect_motion_refusal(failures, p, in, valid, MVM_ERR_BAD_PARAMETER);
    }
    const mvm_motion_parameters p = motion_parameters();
    for (double value : {-1.0, inf, -inf, nan}) {
        mvm_motion_input invalid = in;
        invalid.elapsed = value;
        expect_motion_refusal(failures, p, invalid, valid, MVM_ERR_BAD_PARAMETER);
        mvm_motion_state state = valid;
        state.penalty = value;
        expect_motion_refusal(failures, p, in, state, MVM_ERR_BAD_STATE);
        state = valid;
        state.recovery_hold_remaining = value;
        expect_motion_refusal(failures, p, in, state, MVM_ERR_BAD_STATE);
    }
    expect_motion_refusal(failures, p, in, mvm_motion_state{1.1, 0}, MVM_ERR_BAD_STATE);
}

void test_motion_nulls(int& failures)
{
    mvm_motion_state state{0.25, 1.0};
    const mvm_motion_state before = state;
    const mvm_motion_parameters p = motion_parameters();
    const mvm_motion_input in = motion_input(1.0, 1);
    mvm_motion_decision out;
    std::memset(&out, 0xA5, sizeof out);
    mvm_motion_decision out_before;
    std::memcpy(&out_before, &out, sizeof out);
    CHECK(mvm_motion_update(nullptr, &p, &in, &out) == MVM_ERR_NULL);
    CHECK(mvm_motion_update(&state, nullptr, &in, &out) == MVM_ERR_NULL);
    CHECK(mvm_motion_update(&state, &p, nullptr, &out) == MVM_ERR_NULL);
    CHECK(mvm_motion_update(&state, &p, &in, nullptr) == MVM_ERR_NULL);
    CHECK(mvm_motion_update(nullptr, nullptr, nullptr, nullptr) == MVM_ERR_NULL);
    CHECK(std::memcmp(&state, &before, sizeof state) == 0);
    CHECK(std::memcmp(&out, &out_before, sizeof out) == 0);
}

void test_motion_extreme_finite_inputs(int& failures)
{
    mvm_motion_parameters p = motion_parameters();
    p.attack_duration = std::numeric_limits<double>::min();
    p.recovery_duration = std::numeric_limits<double>::min();
    mvm_motion_state state{0.0, 0.0};
    mvm_motion_decision out{};
    mvm_motion_input in = motion_input(std::numeric_limits<double>::max(), 1);
    CHECK(mvm_motion_update(&state, &p, &in, &out) == MVM_OK);
    CHECK(state.penalty == 1.0);
    CHECK(std::isfinite(out.top_speed_multiplier));
    in.firing = 0;
    CHECK(mvm_motion_update(&state, &p, &in, &out) == MVM_OK);
    CHECK(state.penalty == 0.0);
    CHECK(state.recovery_hold_remaining == 0.0);
}

void test_independent_vehicles_and_projectiles(int& failures)
{
    mvm_motion_state a{0.0, 0.0};
    mvm_motion_state b{0.5, 1.0};
    const mvm_motion_state b_before = b;
    mvm_motion_decision out{};
    motion_step(failures, a, motion_input(1.0, 1), out);
    CHECK(std::memcmp(&b, &b_before, sizeof b) == 0);
    mvm_projectile_state first = launch_active(failures, 100, 1);
    mvm_projectile_state second = launch_active(failures, 100, 2);
    mvm_projectile_state second_before;
    std::memcpy(&second_before, &second, sizeof second);
    const mvm_turret_input boosted = make_input(1, 1, 1, 1, 100, 1);
    uint8_t permit = 0xA5;
    CHECK(mvm_projectile_update(&first, &boosted, &permit) == MVM_OK);
    CHECK(first.steer_state == MVM_STEER_STOPPED);
    CHECK(std::memcmp(&second, &second_before, sizeof second) == 0);
    const mvm_turret_input maintained_second = maintained(100, 2);
    CHECK(mvm_projectile_update(&second, &maintained_second, &permit) == MVM_OK);
    CHECK(permit == 1);
    // An active projectile cannot keep an idle vehicle penalized.
    motion_step(failures, b, motion_input(10.0), out);
    CHECK(b.penalty == 0.0);
    CHECK(second.steer_state == MVM_STEER_ACTIVE);
}

} // namespace

int main()
{
    int failures = 0;

    test_cadence_and_admission(failures);
    test_boost_refusal_keeps_candidate(failures);
    test_noncanonical_true_flags(failures);
    test_boost_during_guidance(failures);
    test_loss_conditions(failures);
    test_permanent_stop(failures);
    test_repeated_maintained_updates(failures);
    test_unguided_launch(failures);
    test_zero_and_max_identities(failures);
    test_null_arguments(failures);
    test_malformed_state(failures);
    test_velocity_canary(failures);
    test_motion_attack_recovery(failures);
    test_motion_taps_resume_and_renew(failures);
    test_motion_activity_charge_boost(failures);
    test_motion_zero_time_and_partition(failures);
    test_motion_invalid_domains(failures);
    test_motion_nulls(failures);
    test_motion_extreme_finite_inputs(failures);
    test_independent_vehicles_and_projectiles(failures);

    return failures == 0 ? 0 : 1;
}
