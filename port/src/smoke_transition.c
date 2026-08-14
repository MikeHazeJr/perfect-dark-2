#include <string.h>

#include "smoke_transition.h"

int smokeTransitionConfigValid(const smoke_transition_config_t *config)
{
	if (!config || config->timeout_ms == 0
			|| config->stable_ms >= config->timeout_ms) {
		return 0;
	}
	if (config->assist_enabled) {
		return config->stable_ms > 0
			&& config->assist_hold_ms > 0
			&& config->assist_hold_ms < config->timeout_ms;
	}
	return config->assist_hold_ms == 0;
}

void smokeTransitionInit(smoke_transition_state_t *state,
		const smoke_transition_config_t *config, uint32_t now_ms)
{
	if (!state) {
		return;
	}
	memset(state, 0, sizeof(*state));
	if (!smokeTransitionConfigValid(config)) {
		return;
	}
	state->initialized = 1;
	state->started_ms = now_ms;
	state->assist_state = config->assist_enabled
		? SMOKE_TRANSITION_ASSIST_ARMED
		: SMOKE_TRANSITION_ASSIST_DISABLED;
	state->terminal = SMOKE_TRANSITION_ACTIVE;
}

smoke_transition_plan_t smokeTransitionTick(smoke_transition_state_t *state,
		const smoke_transition_config_t *config,
		const smoke_transition_input_t *input, uint32_t now_ms)
{
	smoke_transition_plan_t plan;
	uint32_t elapsed_ms;
	int target_stable;

	memset(&plan, 0, sizeof(plan));
	if (!state || !input || !state->initialized
			|| !smokeTransitionConfigValid(config)) {
		plan.invalid = 1;
		return plan;
	}
	if (state->terminal == SMOKE_TRANSITION_SATISFIED) {
		plan.satisfied = 1;
		plan.stable_elapsed_ms = state->stable_elapsed_ms;
		return plan;
	}
	if (state->terminal == SMOKE_TRANSITION_TIMED_OUT) {
		plan.timed_out = 1;
		plan.stable_elapsed_ms = state->stable_elapsed_ms;
		return plan;
	}

	if (state->stable_active) {
		state->stable_elapsed_ms = now_ms - state->stable_started_ms;
	}
	plan.stable_elapsed_ms = state->stable_elapsed_ms;
	elapsed_ms = now_ms - state->started_ms;
	/* Exact-deadline timeout is evaluated before target or assist state. */
	if (elapsed_ms >= config->timeout_ms) {
		if (state->assist_state == SMOKE_TRANSITION_ASSIST_HELD) {
			state->assist_state = SMOKE_TRANSITION_ASSIST_RELEASED;
			plan.release_assist = 1;
		}
		state->terminal = SMOKE_TRANSITION_TIMED_OUT;
		plan.timed_out = 1;
		return plan;
	}

	if (input->target_met) {
		if (!state->stable_active) {
			state->stable_active = 1;
			state->stable_started_ms = now_ms;
			state->stable_elapsed_ms = 0;
			plan.stability_started = 1;
		}
	} else if (state->stable_active) {
		state->stable_active = 0;
		state->stable_started_ms = 0;
		state->stable_elapsed_ms = 0;
		plan.stability_reset = 1;
	}
	if (state->stable_active) {
		state->stable_elapsed_ms = now_ms - state->stable_started_ms;
	}
	plan.stable_elapsed_ms = state->stable_elapsed_ms;

	if (state->assist_state == SMOKE_TRANSITION_ASSIST_HELD
			&& now_ms - state->assist_pressed_ms >= config->assist_hold_ms) {
		state->assist_state = SMOKE_TRANSITION_ASSIST_RELEASED;
		plan.release_assist = 1;
	}

	if (state->assist_state == SMOKE_TRANSITION_ASSIST_ARMED
			&& !input->target_met && input->assist_condition_met) {
		state->assist_state = SMOKE_TRANSITION_ASSIST_HELD;
		state->assist_pressed_ms = now_ms;
		plan.press_assist = 1;
	}

	target_stable = state->stable_active
		&& state->stable_elapsed_ms >= config->stable_ms;
	if (target_stable
			&& state->assist_state != SMOKE_TRANSITION_ASSIST_HELD) {
		state->terminal = SMOKE_TRANSITION_SATISFIED;
		plan.satisfied = 1;
	}
	return plan;
}

int smokeTransitionAssistIsHeld(const smoke_transition_state_t *state)
{
	return state
		&& state->assist_state == SMOKE_TRANSITION_ASSIST_HELD;
}

int smokeTransitionAssistWasUsed(const smoke_transition_state_t *state)
{
	return state
		&& (state->assist_state == SMOKE_TRANSITION_ASSIST_HELD
			|| state->assist_state == SMOKE_TRANSITION_ASSIST_RELEASED);
}

int smokeTransitionForceRelease(smoke_transition_state_t *state)
{
	if (!smokeTransitionAssistIsHeld(state)) {
		return 0;
	}
	state->assist_state = SMOKE_TRANSITION_ASSIST_RELEASED;
	return 1;
}
