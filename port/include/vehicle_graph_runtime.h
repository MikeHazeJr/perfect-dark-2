/**
 * vehicle_graph_runtime.h -- one pd.vehicle_graph.v1 instance per vehicle.
 *
 * A record is keyed by the vehicle's prop and the prop's lifecycle_generation
 * (CORE's answer 1, context/designs/modding/vehicle-graph-v1.md), so a prop
 * slot that a new vehicle reuses gets a new record. The record carries the
 * driver's boost input from the input read to the movement update, says who
 * sits in which seat, and holds the graph instance, made on the vehicle's
 * first tick from the program its catalog binding compiled. lv.c clears the
 * table at every stage load, and
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

/* A program is about to be freed (its binding is released or replaced): free
 * every instance made from it and forget those records; other vehicles keep
 * theirs. A record with a rider in a seat is kept without its instance, so
 * the riders stay where they sit and the next program finds them there. */
void vehicleGraphRuntimeForgetProgram(const vehicle_graph_program_t *program);

/* The vehicle's prop is freed (propFree): free its instance and its record,
 * so a stage that destroys and respawns vehicles never fills the table. A
 * record of another generation in that slot is left alone. */
void vehicleGraphRuntimeRelease(const void *prop, u64 generation);

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

/*
 * Seats: who sits where on this vehicle (VEHICLE_GRAPH_SEAT_DRIVER,
 * VEHICLE_GRAPH_SEAT_GUNNER). The game's seat actions keep it, and
 * gate.vehicle.seat_free reads it through the host. The seats belong to the
 * vehicle, not to its program: a program that is freed and compiled again
 * (vehicleGraphRuntimeForgetProgram) leaves the riders where they sit.
 *
 * SeatOccupant: the player number in the seat, or -1 when it is empty (or the
 * vehicle has no record, or seat is not a seat).
 * SetSeatOccupant: put a player in a seat, or empty it with player -1. A
 * player sits in one seat of a vehicle, so seating a player who is recorded
 * in its other seat empties that one (a seat switch is one call). Returns 1
 * when it is so now, and 0 when it refused: seat or player out of range, the
 * seat holds another player, or the table is full.
 * SeatOfPlayer: the seat this player is recorded in, or
 * VEHICLE_GRAPH_SEAT_NONE.
 */
s32 vehicleGraphRuntimeSeatOccupant(const void *prop, u64 generation, s32 seat);
s32 vehicleGraphRuntimeSetSeatOccupant(const void *prop, u64 generation, s32 seat, s32 player);
s32 vehicleGraphRuntimeSeatOfPlayer(const void *prop, u64 generation, s32 player);

/*
 * The turret's aim (Maian vehicle V5a): where the gunner's view points,
 * relative to the vehicle. yaw is radians about the vehicle's up axis in the
 * sense of its turn angle (0 is straight ahead); pitch is radians, positive
 * up. The gunner's update sets it every update; the vehicle keeps the last one
 * when the gunner leaves, so the turret stays where it was pointed. A value
 * that is not finite is ignored. TurretAim returns 1 with the aim when one was
 * set for this vehicle, else 0 (the turret is drawn as modelled).
 */
void vehicleGraphRuntimeSetTurretAim(const void *prop, u64 generation, f32 yaw, f32 pitch);
s32 vehicleGraphRuntimeTurretAim(const void *prop, u64 generation, f32 *yaw, f32 *pitch);

/* Records in use (tests and diagnostics). */
s32 vehicleGraphRuntimeCount(void);

#ifdef __cplusplus
}
#endif

#endif
