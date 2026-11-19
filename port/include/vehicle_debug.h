/**
 * vehicle_debug.h -- put a mod vehicle in place of a stage's hoverbikes.
 *
 * Until levels can place vehicles, `--debug-swap-vehicle <catalog id>` gives
 * every hoverbike a stage creates the model of the named vehicle, so a mod
 * vehicle can be driven in CI training, Crash Site, Infiltration or Escape.
 * The game finds a vehicle by its model number (assetRuntimeVehicleForModelnum
 * looks up the vehicle binding whose runtime id is the object's modelnum), so
 * giving the object the vehicle's model is what makes it that vehicle.
 */
#ifndef _IN_VEHICLE_DEBUG_H
#define _IN_VEHICLE_DEBUG_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set the vehicle that replaces hoverbikes; NULL or "" clears it. */
void vehicleDebugSetSwap(const char *catalog_id);
const char *vehicleDebugSwapId(void);

/*
 * The model number a hoverbike created with `modelnum` should use: the swap
 * vehicle's model when a swap is set and that vehicle loads for this stage,
 * else `modelnum` unchanged. Loads the vehicle and its model for the stage
 * (one stage reference, released with the stage).
 */
s32 vehicleDebugSwapModelnum(s32 modelnum);

/*
 * `--debug-mount-seat <driver|gunner>`: the seat every mount of a two-seat
 * vehicle asks for, instead of the seat the player's place and view choose
 * (src/game/propobj.c, hoverbikeChooseMountSeat). 0 is the driver seat, 1 the
 * gunner seat, -1 clears it (VEHICLE_GRAPH_SEAT_* in vehicle_graph_runner.h).
 * A vehicle whose graph has no gunner seat is not affected.
 */
#define VEHICLE_DEBUG_MOUNT_SEAT_NONE   (-1)
#define VEHICLE_DEBUG_MOUNT_SEAT_DRIVER 0
#define VEHICLE_DEBUG_MOUNT_SEAT_GUNNER 1
void hoverbikeDebugSetMountSeat(s32 seat);

#ifdef __cplusplus
}
#endif

#endif /* _IN_VEHICLE_DEBUG_H */
