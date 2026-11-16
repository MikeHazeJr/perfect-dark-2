/**
 * vehicle_graph_runtime.h -- one pd.vehicle_graph.v1 instance per vehicle.
 *
 * A record is keyed by the vehicle's prop and the prop's lifecycle_generation
 * (CORE's answer 1, context/designs/modding/vehicle-graph-v1.md), so a prop
 * slot that a new vehicle reuses gets a new record. The record carries the
 * driver's boost input from the input read to the movement update and holds
 * the graph instance, made on the vehicle's first tick from the program its
 * catalog binding compiled. lv.c clears the table at every stage load, and
 * assetRuntimeReset clears it before it frees the programs, so no instance
 * outlives its vehicle or its program.
 */
#ifndef _IN_VEHICLE_GRAPH_RUNTIME_H
#define _IN_VEHICLE_GRAPH_RUNTIME_H

#include <PR/ultratypes.h>

#include "vehicle_graph_runner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VEHICLE_GRAPH_RUNTIME_MAX 64

/* Free every instance and forget every record. */
void vehicleGraphRuntimeReset(void);

/*
 * The instance for this vehicle, made from program on first use (and made
 * again when the program changed). Returns NULL when program is NULL, the
 * record was refused, or there is no room (logged by the caller). *created is
 * set to 1 when the instance is new (may be NULL).
 */
vehicle_graph_instance_t *vehicleGraphRuntimeInstance(const void *prop, u64 generation,
	const vehicle_graph_program_t *program, s32 *created);

/* The vehicle's model cannot run its graph (a missing part): free its
 * instance and make none again for this prop and generation. */
void vehicleGraphRuntimeRefuse(const void *prop, u64 generation);

/* The driver holds boost this update (the input read calls it every update
 * while mounted); the movement update takes it, clearing it, so a vehicle
 * that nobody drives never boosts. */
void vehicleGraphRuntimeSetBoostHeld(const void *prop, u64 generation, s32 held);
s32 vehicleGraphRuntimeTakeBoostHeld(const void *prop, u64 generation);

/*
 * The vehicle's speed over the ground, for the boost's diagnostics: call once
 * per update with the vehicle's position (x and z) and this update's 60 Hz
 * ticks. Returns the previous update's speed in world units per 60 Hz tick
 * (the distance moved since the previous call over that call's ticks; 0 on a
 * record's first call), and keeps the highest one since the last take.
 */
f32 vehicleGraphRuntimeGroundSpeed(const void *prop, u64 generation, f32 x, f32 z, f32 ticks60);
/* The highest ground speed since the last take (0 when none), then 0 again. */
f32 vehicleGraphRuntimeTakePeakGround(const void *prop, u64 generation);

/* Records in use (tests and diagnostics). */
s32 vehicleGraphRuntimeCount(void);

#ifdef __cplusplus
}
#endif

#endif
