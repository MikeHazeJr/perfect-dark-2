#ifndef PD_SMOKE_TRANSITION_H
#define PD_SMOKE_TRANSITION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum smoke_transition_assist_state {
	SMOKE_TRANSITION_ASSIST_DISABLED = 0,
	SMOKE_TRANSITION_ASSIST_ARMED,
	SMOKE_TRANSITION_ASSIST_HELD,
	SMOKE_TRANSITION_ASSIST_RELEASED
} smoke_transition_assist_state_t;

typedef enum smoke_transition_terminal {
	SMOKE_TRANSITION_ACTIVE = 0,
	SMOKE_TRANSITION_SATISFIED,
	SMOKE_TRANSITION_TIMED_OUT
} smoke_transition_terminal_t;

typedef struct smoke_transition_config {
	uint32_t timeout_ms;
	uint32_t stable_ms;
	uint32_t assist_hold_ms;
	int assist_enabled;
} smoke_transition_config_t;

typedef struct smoke_transition_state {
	uint32_t started_ms;
	uint32_t stable_started_ms;
	uint32_t stable_elapsed_ms;
	uint32_t assist_pressed_ms;
	int initialized;
	int stable_active;
	smoke_transition_assist_state_t assist_state;
	smoke_transition_terminal_t terminal;
} smoke_transition_state_t;

typedef struct smoke_transition_input {
	int target_met;
	int assist_condition_met;
} smoke_transition_input_t;

typedef struct smoke_transition_plan {
	int invalid;
	int stability_started;
	int stability_reset;
	int press_assist;
	int release_assist;
	int satisfied;
	int timed_out;
	uint32_t stable_elapsed_ms;
} smoke_transition_plan_t;

/* Pure real-time wait policy used by the smoke harness. Expiration wins on
 * the exact boundary. A target must remain continuously true for stable_ms;
 * any false sample resets that window. An optional assist can press once,
 * remains held for at most assist_hold_ms, and can never be re-armed. */
int smokeTransitionConfigValid(const smoke_transition_config_t *config);
void smokeTransitionInit(smoke_transition_state_t *state,
	const smoke_transition_config_t *config, uint32_t now_ms);
smoke_transition_plan_t smokeTransitionTick(smoke_transition_state_t *state,
	const smoke_transition_config_t *config,
	const smoke_transition_input_t *input, uint32_t now_ms);
int smokeTransitionAssistIsHeld(const smoke_transition_state_t *state);
int smokeTransitionAssistWasUsed(const smoke_transition_state_t *state);
int smokeTransitionForceRelease(smoke_transition_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* PD_SMOKE_TRANSITION_H */
