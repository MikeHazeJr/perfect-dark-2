/**
 * vehicle_boost_runtime.h -- one boost and heat state per vehicle in the stage.
 *
 * The state machine is vehicle_boost_heat.h; this table keeps its state per
 * vehicle (keyed by the hoverbike object) and carries the driver's boost
 * input from the input read to the movement update. lv.c clears it at every
 * stage load, so no state outlives its vehicle.
 */
#ifndef _IN_VEHICLE_BOOST_RUNTIME_H
#define _IN_VEHICLE_BOOST_RUNTIME_H

#include <PR/ultratypes.h>

#include "vehicle_boost_heat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VEHICLE_BOOST_RUNTIME_SLOTS 16

/* Clear every state (stage load). */
void vehicleBoostRuntimeReset(void);

/*
 * The state for `vehicle`, made (cool) on first use. Returns NULL when every
 * slot is taken; *created is set to 1 when the state is new (may be NULL).
 */
vehicle_boost_heat_state_t *vehicleBoostRuntimeState(const void *vehicle, s32 *created);

/* The driver holds boost this update (the input read calls it every update
 * while mounted). */
void vehicleBoostRuntimeSetHeld(const void *vehicle, s32 held);

/* The boost input for this update, cleared as it is read, so a vehicle that
 * nobody drives never boosts. */
s32 vehicleBoostRuntimeTakeHeld(const void *vehicle);

#ifdef __cplusplus
}
#endif

#endif
