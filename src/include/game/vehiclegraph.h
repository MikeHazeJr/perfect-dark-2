#ifndef IN_GAME_VEHICLEGRAPH_H
#define IN_GAME_VEHICLEGRAPH_H
#include <ultra64.h>
#include "asset_runtime.h"
#include "vehicle_graph_runtime.h"
#include "data.h"
#include "types.h"

/*
 * The game side of pd.vehicle_graph.v1 (src/game/vehiclegraph.c): the game
 * fires a vehicle's graph events through these, and a vehicle with no v1 graph
 * (a v2 behavior file) is left to the original code by each of them.
 */

/* What og.vehicle.mount_seat or og.vehicle.dismount_seat does for the seat a
 * request names: the original mount or dismount. Returns 1 when it did it. */
typedef s32 (*hoverbike_seat_action_fn)(void *context);

/*
 * Fires a seat request (VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED or
 * _DISMOUNT_REQUESTED) for the current player at this vehicle's graph. The
 * graph's gates decide; its seat module then runs action(context) once.
 * Returns 1 when the seat module ran and did it, 0 when the graph or the
 * action refused, and -1 when the vehicle has no v1 graph (the caller takes
 * the original path).
 */
s32 hoverbikeGraphSeatRequest(struct prop *prop, s32 kind, s32 seat,
		hoverbike_seat_action_fn action, void *context);

/*
 * This vehicle's graph instance, made on first use, or NULL when it has none
 * (its graph was refused for this model, or the table is full; logged once).
 * source is the vehicle's hydrated .pdvehicle binding with a v1 graph.
 */
vehicle_graph_instance_t *hoverbikeGraphInstance(struct hoverbikeobj *bike,
		const asset_runtime_binding_t *source);

/*
 * Fires this update's event.vehicle.tick and returns in *drive what the graph
 * asks of the movement: hover or not, take the driver's inputs or not, the
 * extra forward speed and the steering scale. throttle is the driver's
 * forward input.
 */
void hoverbikeGraphTick(struct hoverbikeobj *bike, const asset_runtime_binding_t *source,
		vehicle_graph_instance_t *instance, f32 throttle, vehicle_graph_drive_t *drive);

/* The tick of a parked vehicle that nothing moves: its graph still runs every
 * update (its heat cools). Does nothing for a vehicle with no v1 graph. */
void hoverbikeGraphIdleTick(struct hoverbikeobj *bike);

#endif
