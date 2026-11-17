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

/* Seat parts a vehicle model may carry: position nodes with these part
 * numbers say where a rider's eye is, in model units. No base model has
 * them. */
#define VEHICLE_MODEL_PART_DRIVER_SEAT 160
#define VEHICLE_MODEL_PART_GUNNER_SEAT 161

/* The turret's two pivots on a vehicle with a gunner seat: the yaw part turns
 * about its vertical axis and carries the gunner's eye (part 161 is under
 * it); the pitch part, under the yaw part, tilts the cannon. The Maian
 * vehicle's model has them (dev-mods/maian_hover_vehicle/build_vehicle_mesh.py).
 * og.vehicle.turret_aim will name them in the vehicle's graph (vehicle-graph-
 * v1.md, "Not in v1"); until then these are the parts. */
#define VEHICLE_MODEL_PART_TURRET_YAW 1
#define VEHICLE_MODEL_PART_TURRET_PITCH 2

/*
 * Where the rider of this seat (VEHICLE_GRAPH_SEAT_*) has their eye on this
 * vehicle's model, in model units: the model's seat part for that seat.
 * Returns 1 and fills *pos, or 0 when the model has no such part (the caller
 * then uses the original hoverbike's driver position). It reads the model's
 * node positions, not its render matrices, so it works for a vehicle that was
 * not drawn this frame.
 */
s32 hoverbikeSeatModelPos(struct defaultobj *obj, s32 seat, struct coord *pos);

/* The same point in the world, where the vehicle stands now (its position
 * and rotation, as the ride places the rider). Returns 0 when the model has
 * no seat part for this seat. */
s32 hoverbikeSeatWorldPos(struct defaultobj *obj, s32 seat, struct coord *pos);

/*
 * The gunner's eye, `eye` in model units, once the turret has turned `yaw`
 * radians (vehicleTurretTurnPoint about the yaw part's position). Returns 1
 * and turns `eye` when the model's turret yaw part carries the gunner's seat
 * part; 0 and leaves `eye` as it was otherwise.
 */
s32 hoverbikeTurretEyeModelPos(struct defaultobj *obj, f32 yaw, struct coord *eye);

/*
 * Draws the turret where its gunner aims (Maian vehicle V5a): called by
 * objInitMatrices right after a hoverbike's matrices are built. With an aim
 * recorded for the vehicle (vehicleGraphRuntimeSetTurretAim) and a model whose
 * turret yaw part carries the gunner's seat, the yaw part and everything under
 * it turn by the aim's yaw, and the pitch part and everything under it tilt by
 * its pitch. Any other vehicle is left as it was drawn.
 */
void hoverbikeApplyTurretAim(struct prop *prop);

/* What og.vehicle.mount_seat, og.vehicle.dismount_seat or
 * og.vehicle.switch_seat does for the seat the module names
 * (VEHICLE_GRAPH_SEAT_*): the request's own mount, dismount or change of
 * seat. Returns 1 when it did it, and 0 when it could not, which includes a
 * seat this action does not serve. */
typedef s32 (*hoverbike_seat_action_fn)(void *context, s32 seat);

/*
 * Fires a seat request (VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED,
 * _DISMOUNT_REQUESTED or _SWITCH_SEAT_REQUESTED) for the current player at
 * this vehicle's graph. seat is the seat the request is about: the one to
 * take, to leave, or to move to. The graph's gates decide; its seat module
 * then runs action(context, the module's seat) once. Returns 1 when the seat
 * module ran and did it, 0 when the graph or the action refused, and -1 when
 * the vehicle has no v1 graph (the caller takes the original path).
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
