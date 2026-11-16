/**
 * vehicle_boost_heat.h -- the state machine of og.vehicle.boost_heat.
 *
 * og.vehicle.boost_heat is a native module of the vehicle behavior graph
 * (pd.vehicle_graph.v1, context/designs/modding/vehicle-graph-v1.md). A held
 * boost ramps to full; after a set time of boost the vehicle overheats; boost
 * is then capped, steering is briefly limited, and the heat falls only once
 * the driver has let go, faster when stopped than at speed. Mike's design for
 * the Maian hover vehicle (context/designs/modding/maian-vehicle-and-veteran.md)
 * is one set of params for it.
 *
 * What is not here: the numbers (they are the module's params in the vehicle's
 * graph, usually taken from its behavior/variables.json) and what a phase means
 * for the vehicle (which part shows while overheated and whether the turret may
 * fire are the graph's gate.vehicle.heat_phase gates). This file holds no game
 * state either: the graph runner keeps one state per vehicle and advances it
 * once per update. Times are 240 Hz ticks.
 */
#ifndef _IN_VEHICLE_BOOST_HEAT_H
#define _IN_VEHICLE_BOOST_HEAT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The phases a graph's gate.vehicle.heat_phase gates name. */
typedef enum vehicle_boost_heat_phase {
	VEHICLE_BOOST_HEAT_COOL = 0,     /* boost not held, not overheated */
	VEHICLE_BOOST_HEAT_RAMPING,      /* boost held, ramping up */
	VEHICLE_BOOST_HEAT_FULL,         /* boost held, at full */
	VEHICLE_BOOST_HEAT_OVERHEATED,   /* overheated, boost not let go since */
	VEHICLE_BOOST_HEAT_RECOVERING,   /* overheated, let go: the heat is falling */
	VEHICLE_BOOST_HEAT_PHASE_COUNT
} vehicle_boost_heat_phase_t;

/* The module's params, as the graph compiler read them. */
typedef struct vehicle_boost_heat_params {
	f32 boost_speed;          /* extra forward speed at full boost (world units per 60 Hz tick) */
	f32 ramp_ticks240;        /* 0 to full boost (and full to 0) while held (released) */
	f32 overheat_ticks240;    /* time boost is held until the vehicle overheats */
	f32 overheated_boost;     /* boost cap from overheating until cool again, 0..1 */
	f32 steer_limit;          /* steering scale right after overheating, 0..1 */
	f32 steer_limit_ticks240; /* how long the steering limit lasts */
	f32 recover_ticks240;     /* full heat to cool when stopped */
	f32 recover_speed_scale;  /* recovery time multiplier at full non-boost speed (>= 1 slows it) */
} vehicle_boost_heat_params_t;

/* One vehicle's boost and heat. */
typedef struct vehicle_boost_heat_state {
	f32 boost;                /* 0..1 */
	f32 heat;                 /* 0..1; 1 means overheated */
	f32 steer_limit_left;     /* 240 Hz ticks of steering limit left */
	s32 phase;                /* vehicle_boost_heat_phase_t */
	s32 overheated;           /* set from overheating until heat is back to 0 */
	s32 released;             /* the boost was released since overheating */
} vehicle_boost_heat_state_t;

/* 1 when every param is in range (times > 0, fractions in 0..1, speed >= 0,
 * recover_speed_scale > 0); else 0 and *why names the first bad param. */
s32 vehicleBoostHeatParamsValid(const vehicle_boost_heat_params_t *params, const char **why);

/* A new vehicle's state: no boost, no heat, cool. */
void vehicleBoostHeatReset(vehicle_boost_heat_state_t *state);

/*
 * Advance by dt240 ticks. held: the driver's boost action is held this
 * update. speed_frac: current over max non-boost speed (clamped to 0..1).
 * Returns 1 when the phase changed, with the old phase in *prev_phase (may be
 * NULL), else 0.
 */
s32 vehicleBoostHeatStep(vehicle_boost_heat_state_t *state,
	const vehicle_boost_heat_params_t *params, s32 held, f32 speed_frac,
	f32 dt240, s32 *prev_phase);

/* Extra forward speed this update: boost x boost_speed. */
f32 vehicleBoostHeatExtraSpeed(const vehicle_boost_heat_state_t *state,
	const vehicle_boost_heat_params_t *params);

/* Steering scale this update: steer_limit while the limit lasts, else 1. */
f32 vehicleBoostHeatSteerScale(const vehicle_boost_heat_state_t *state,
	const vehicle_boost_heat_params_t *params);

/* "cool", "ramping", "full", "overheated", "recovering", or "?" */
const char *vehicleBoostHeatPhaseName(s32 phase);

/*
 * A time value in a graph's units to 240 Hz ticks: "ticks60" x 4,
 * "ticks240" x 1, "centiseconds" x 2.4. Returns 1 and sets *out, or 0 for an
 * unknown unit or a negative value.
 */
s32 vehicleBoostHeatTimeToTicks240(f32 value, const char *unit, f32 *out);

#ifdef __cplusplus
}
#endif

#endif
