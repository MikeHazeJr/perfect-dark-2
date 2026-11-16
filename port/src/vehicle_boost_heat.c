/**
 * vehicle_boost_heat.c -- the Maian vehicle's boost and heat: see
 * vehicle_boost_heat.h.
 */
#include <string.h>

#include <PR/ultratypes.h>

#include "vehicle_boost_heat.h"

/* Float sums of 1/60 and 1/240 steps land a hair off 1 or 0; snap within this. */
#define VBH_EPSILON 1e-5f

void vehicleBoostHeatDefaultParams(vehicle_boost_heat_params_t *params)
{
	if (!params) {
		return;
	}
	params->boost_speed = 7.0f;            /* the base hoverbike bonus's top speed */
	params->ramp_ticks240 = 240.0f;        /* about 1 s */
	params->overheat_ticks240 = 960.0f;    /* about 4 s */
	params->overheated_boost = 0.5f;       /* above base, below full */
	params->steer_limit = 0.5f;
	params->steer_limit_ticks240 = 180.0f; /* 0.75 s, "briefly" */
	params->recover_ticks240 = 480.0f;     /* 2 s standing still */
	params->recover_speed_scale = 2.0f;    /* twice as long at full speed */
}

static s32 vbhInRange(f32 value, f32 lo, f32 hi)
{
	return value >= lo && value <= hi; /* false for NaN */
}

s32 vehicleBoostHeatParamsValid(const vehicle_boost_heat_params_t *params, const char **why)
{
	const char *bad = NULL;

	if (!params) {
		bad = "params";
	} else if (!vbhInRange(params->boost_speed, 0.0f, 1000.0f)) {
		bad = "boost_speed";
	} else if (!(params->ramp_ticks240 > 0.0f)) {
		bad = "ramp_time";
	} else if (!(params->overheat_ticks240 > 0.0f)) {
		bad = "overheat_after";
	} else if (!vbhInRange(params->overheated_boost, 0.0f, 1.0f)) {
		bad = "overheated_boost";
	} else if (!vbhInRange(params->steer_limit, 0.0f, 1.0f)) {
		bad = "steer_limit";
	} else if (!(params->steer_limit_ticks240 >= 0.0f)) {
		bad = "steer_limit_time";
	} else if (!(params->recover_ticks240 > 0.0f)) {
		bad = "recover_time_stopped";
	} else if (!(params->recover_speed_scale > 0.0f)) {
		bad = "recover_speed_scale";
	}
	if (why) {
		*why = bad;
	}
	return bad == NULL;
}

void vehicleBoostHeatReset(vehicle_boost_heat_state_t *state)
{
	if (state) {
		memset(state, 0, sizeof(*state));
		state->phase = VEHICLE_BOOST_HEAT_COOL;
	}
}

static f32 vbhApproach(f32 value, f32 target, f32 step)
{
	if (value < target) {
		value += step;
		if (value >= target - VBH_EPSILON) {
			value = target;
		}
	} else if (value > target) {
		value -= step;
		if (value <= target + VBH_EPSILON) {
			value = target;
		}
	}
	return value;
}

/* Heat lost this update: a full recovery takes recover_ticks240 standing
 * still, recover_speed_scale times that at full non-boost speed. */
static f32 vbhRecoverStep(const vehicle_boost_heat_params_t *params, f32 speed_frac, f32 dt240)
{
	f32 ticks = params->recover_ticks240 *
		(1.0f + (params->recover_speed_scale - 1.0f) * speed_frac);

	if (!(ticks >= 1.0f)) {
		ticks = 1.0f;
	}
	return dt240 / ticks;
}

s32 vehicleBoostHeatStep(vehicle_boost_heat_state_t *state,
	const vehicle_boost_heat_params_t *params, s32 held, f32 speed_frac,
	f32 dt240, s32 *prev_phase)
{
	s32 old_phase;
	f32 ramp;

	if (!state || !params) {
		return 0;
	}
	if (!(dt240 > 0.0f)) {
		dt240 = 0.0f;
	}
	if (!(speed_frac > 0.0f)) {
		speed_frac = 0.0f;
	} else if (speed_frac > 1.0f) {
		speed_frac = 1.0f;
	}
	old_phase = state->phase;
	ramp = dt240 / params->ramp_ticks240;

	if (state->steer_limit_left > 0.0f) {
		state->steer_limit_left -= dt240;
		if (state->steer_limit_left < 0.0f) {
			state->steer_limit_left = 0.0f;
		}
	}

	if (state->overheated) {
		/* Black crystals: boost is capped until cool again, and the heat
		 * falls only once the driver has let go of boost at least once. */
		if (!held) {
			state->released = 1;
		}
		state->boost = vbhApproach(state->boost, held ? params->overheated_boost : 0.0f, ramp);
		if (state->released) {
			state->heat -= vbhRecoverStep(params, speed_frac, dt240);
			if (state->heat <= VBH_EPSILON) {
				state->heat = 0.0f;
				state->overheated = 0;
				state->released = 0;
			}
		}
	} else if (held) {
		state->boost = vbhApproach(state->boost, 1.0f, ramp);
		state->heat += dt240 / params->overheat_ticks240;
		if (state->heat >= 1.0f - VBH_EPSILON) {
			state->heat = 1.0f;
			state->overheated = 1;
			state->released = 0;
			state->steer_limit_left = params->steer_limit_ticks240;
		}
	} else {
		state->boost = vbhApproach(state->boost, 0.0f, ramp);
		state->heat -= vbhRecoverStep(params, speed_frac, dt240);
		if (state->heat <= VBH_EPSILON) {
			state->heat = 0.0f;
		}
	}

	if (state->overheated) {
		state->phase = state->released ? VEHICLE_BOOST_HEAT_RECOVERING : VEHICLE_BOOST_HEAT_OVERHEATED;
	} else if (held) {
		state->phase = state->boost >= 1.0f ? VEHICLE_BOOST_HEAT_FULL : VEHICLE_BOOST_HEAT_RAMPING;
	} else {
		state->phase = VEHICLE_BOOST_HEAT_COOL;
	}

	if (state->phase != old_phase) {
		if (prev_phase) {
			*prev_phase = old_phase;
		}
		return 1;
	}
	return 0;
}

f32 vehicleBoostHeatExtraSpeed(const vehicle_boost_heat_state_t *state,
	const vehicle_boost_heat_params_t *params)
{
	if (!state || !params) {
		return 0.0f;
	}
	return state->boost * params->boost_speed;
}

f32 vehicleBoostHeatSteerScale(const vehicle_boost_heat_state_t *state,
	const vehicle_boost_heat_params_t *params)
{
	if (!state || !params || !(state->steer_limit_left > 0.0f)) {
		return 1.0f;
	}
	return params->steer_limit;
}

s32 vehicleBoostHeatCrystalsBlack(const vehicle_boost_heat_state_t *state)
{
	return state && state->overheated;
}

s32 vehicleBoostHeatTurretLocked(const vehicle_boost_heat_state_t *state)
{
	return state && (state->phase == VEHICLE_BOOST_HEAT_FULL ||
		state->phase == VEHICLE_BOOST_HEAT_OVERHEATED ||
		state->phase == VEHICLE_BOOST_HEAT_RECOVERING);
}

const char *vehicleBoostHeatPhaseName(s32 phase)
{
	static const char *const names[VEHICLE_BOOST_HEAT_PHASE_COUNT] = {
		"cool", "ramping", "full", "overheated", "recovering",
	};

	if (phase < 0 || phase >= VEHICLE_BOOST_HEAT_PHASE_COUNT) {
		return "?";
	}
	return names[phase];
}

s32 vehicleBoostHeatTimeToTicks240(f32 value, const char *unit, f32 *out)
{
	f32 scale;

	if (!unit || !out || !(value >= 0.0f)) {
		return 0;
	}
	if (strcmp(unit, "ticks60") == 0) {
		scale = 4.0f;
	} else if (strcmp(unit, "ticks240") == 0) {
		scale = 1.0f;
	} else if (strcmp(unit, "centiseconds") == 0) {
		scale = 2.4f;
	} else {
		return 0;
	}
	*out = value * scale;
	return 1;
}
