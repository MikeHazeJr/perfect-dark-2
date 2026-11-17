/**
 * vehiclegraph.c -- the game side of pd.vehicle_graph.v1.
 *
 * A vehicle whose .pdvehicle carries a v1 behavior graph
 * (context/designs/modding/vehicle-graph-v1.md) is run by that graph. The game
 * fires the graph's events from the places the original code acted, and gives
 * the graph's native modules what they need of the game (the host table
 * below). The graph decides; the work itself is the original code.
 *
 *   event                fired from                          what its modules do
 *   mount_requested      currentPlayerTryMountHoverbike      the original mount (propobj.c)
 *   dismount_requested   bbikeHandleActivate, the PC exit    the original dismounts (bondbike.c)
 *   tick                 hoverbikeUpdateMovement, and the    say what the movement does; run
 *                        idle tick of a parked vehicle       the boost and heat machine
 *   heat_changed         raised by og.vehicle.boost_heat     show or hide model parts, set
 *                                                            the turret lock
 *
 * One graph instance per vehicle prop is kept by port/src/vehicle_graph_runtime.c
 * and compiled programs by port/src/asset_runtime.c. A vehicle with a v2
 * behavior file has no graph: every function here then does nothing (or
 * returns -1) and the caller runs the original path.
 */
#include <ultra64.h>
#include <math.h>
#include <string.h>
#include "constants.h"
#include "asset_runtime.h"
#include "vehicle_graph_runtime.h"
#include "system.h"
#include "game/vehiclegraph.h"
#include "bss.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "data.h"
#include "types.h"

/* ---------------------------------------------------------------------------
 * The host: what the graph's native modules ask of the game
 * ------------------------------------------------------------------------- */

/* The host's context for one fired event. */
typedef struct hoverbike_graph_host {
	struct hoverbikeobj *bike;
	struct prop *prop;
	hoverbike_seat_action_fn action; /* a seat request's mount, dismount or switch */
	void *action_context;
	s32 seat_done;                   /* the seat module did it */
} hoverbike_graph_host_t;

/* The model's toggle node for a part number, or NULL when the model has no
 * such part or it is not a toggle. */
static struct modelnode *hoverbikeTogglePart(struct hoverbikeobj *bike, s32 part)
{
	struct model *model = bike->base.model;
	struct modelnode *node;

	if (!model || !model->definition || part < 0) {
		return NULL;
	}
	node = modelGetPart(model->definition, part);
	return node && node->type == MODELNODETYPE_TOGGLE ? node : NULL;
}

/* gate.vehicle.seat_free: nobody is in that seat. The driver seat is taken
 * while the vehicle is mounted (OBJHFLAG_MOUNTED, the original flag, which the
 * physics, the doors and the network all read, so it also covers a remote
 * driver). The gunner seat is in the vehicle's graph record. */
static s32 hoverbikeGraphSeatFree(void *context, s32 seat)
{
	hoverbike_graph_host_t *host = (hoverbike_graph_host_t *)context;

	if (seat == VEHICLE_GRAPH_SEAT_DRIVER) {
		return (host->bike->base.hidden & OBJHFLAG_MOUNTED) == 0;
	}
	if (seat == VEHICLE_GRAPH_SEAT_GUNNER) {
		return vehicleGraphRuntimeSeatOccupant(host->prop, host->prop->lifecycle_generation,
			VEHICLE_GRAPH_SEAT_GUNNER) < 0;
	}
	return 0;
}

/* og.vehicle.mount_seat, og.vehicle.dismount_seat and og.vehicle.switch_seat:
 * run the request's action for the seat the module names, once. Which seats
 * an action serves is the action's own business: it returns 0 for a seat it
 * has nothing for (the gunner seat, until the gunner's ride is built). */
static s32 hoverbikeGraphSeatAction(void *context, s32 seat, const vehicle_graph_event_t *event)
{
	hoverbike_graph_host_t *host = (hoverbike_graph_host_t *)context;

	(void)event;
	/* Once one seat module has acted in this request the rider has changed
	 * (after a dismount, player->hoverbike is NULL), so a second does
	 * nothing. The compiler refuses a second one for one seat on one event;
	 * this keeps the host safe whatever the graph (CORE's review,
	 * 2026-11-16). */
	if (host->seat_done || !host->action
			|| (seat != VEHICLE_GRAPH_SEAT_DRIVER && seat != VEHICLE_GRAPH_SEAT_GUNNER)
			|| !host->action(host->action_context, seat)) {
		return 0;
	}
	host->seat_done = 1;
	return 1;
}

/* og.vehicle.part_visible: show or hide one of the model's toggle parts. */
static s32 hoverbikeGraphPartVisible(void *context, s32 part, s32 visible)
{
	hoverbike_graph_host_t *host = (hoverbike_graph_host_t *)context;
	struct modelnode *node = hoverbikeTogglePart(host->bike, part);
	union modelrwdata *rwdata;

	if (!node) {
		return 0;
	}
	rwdata = modelGetNodeRwData(host->bike->base.model, node);
	if ((rwdata->toggle.visible ? 1 : 0) == (visible ? 1 : 0)) {
		return 1; /* already so: no change, no line (a tick may ask every update) */
	}
	rwdata->toggle.visible = visible ? true : false;
	sysLogPrintf(LOG_NOTE, "VEHICLE.GRAPH: modelnum=%d part %d %s",
		host->bike->base.modelnum, part, visible ? "shown" : "hidden");
	return 1;
}

/* The host table for one fired event on this vehicle. */
static void hoverbikeGraphHostInit(vehicle_graph_host_t *table, hoverbike_graph_host_t *host,
		struct hoverbikeobj *bike, struct prop *prop)
{
	memset(host, 0, sizeof(*host));
	host->bike = bike;
	host->prop = prop;
	/* A callback left NULL would refuse its module. The three seat modules
	 * share one: each runs the action of the request that reached it. */
	memset(table, 0, sizeof(*table));
	table->context = host;
	table->seat_free = hoverbikeGraphSeatFree;
	table->mount_seat = hoverbikeGraphSeatAction;
	table->dismount_seat = hoverbikeGraphSeatAction;
	table->switch_seat = hoverbikeGraphSeatAction;
	table->set_part_visible = hoverbikeGraphPartVisible;
}

/* ---------------------------------------------------------------------------
 * Seat parts: where a rider's eye is on the model
 * ------------------------------------------------------------------------- */

s32 hoverbikeSeatModelPos(struct defaultobj *obj, s32 seat, struct coord *pos)
{
	struct model *model = obj ? obj->model : NULL;
	struct modelnode *node;
	s32 part;

	if (!model || !model->definition || !pos) {
		return 0;
	}
	if (seat == VEHICLE_GRAPH_SEAT_DRIVER) {
		part = VEHICLE_MODEL_PART_DRIVER_SEAT;
	} else if (seat == VEHICLE_GRAPH_SEAT_GUNNER) {
		part = VEHICLE_MODEL_PART_GUNNER_SEAT;
	} else {
		return 0;
	}
	node = modelGetPart(model->definition, part);
	if (!node || (node->type & 0xff) != MODELNODETYPE_POSITION) {
		return 0;
	}
	/* The sum of the node's own position and its parents' (the gunner's eye
	 * hangs off the turret's yaw node), in model units. */
	modelNodeGetModelRelativePosition(model, node, pos);
	return 1;
}

s32 hoverbikeSeatWorldPos(struct defaultobj *obj, s32 seat, struct coord *pos)
{
	struct coord local;
	Mtxf mtx;

	if (!obj || !obj->prop || !pos || !hoverbikeSeatModelPos(obj, seat, &local)) {
		return 0;
	}
	/* The ride's own transform (bbikeTick): the vehicle's rotation, which
	 * carries the model scale, and its position. */
	mtx3ToMtx4(obj->realrot, &mtx);
	mtx4SetTranslation(&obj->prop->pos, &mtx);
	mtx4TransformVec(&mtx, &local, pos);
	return 1;
}

/* ---------------------------------------------------------------------------
 * The vehicle's instance
 * ------------------------------------------------------------------------- */

/* This vehicle's graph instance, made on first use. The catalog checked the
 * graph's parts against the mesh's part tables when the vehicle registered
 * (assetRuntimeVehicleGraphAdmits); the model is built by now, so the same
 * parts are checked on it as a guard: a missing toggle part refuses the
 * instance (logged), and the vehicle neither moves nor takes a rider. The
 * parts start visible. */
vehicle_graph_instance_t *hoverbikeGraphInstance(struct hoverbikeobj *bike,
		const asset_runtime_binding_t *source)
{
	struct prop *prop = bike ? bike->base.prop : NULL;
	const vehicle_graph_program_t *program = source ? source->vehicle_graph : NULL;
	const vehicle_boost_heat_params_t *heat;
	vehicle_graph_instance_t *instance;
	s32 created = 0;
	s32 i;

	if (!program || !prop) {
		return NULL;
	}
	instance = vehicleGraphRuntimeInstance(prop, prop->lifecycle_generation, program, &created);
	if (!instance) {
		static s32 s_WarnedNoInstance = 0;
		if (!s_WarnedNoInstance) {
			s_WarnedNoInstance = 1;
			sysLogPrintf(LOG_WARNING,
				"VEHICLE.GRAPH: modelnum=%d has no graph instance (refused, or more than %d vehicles); it neither moves nor takes a rider",
				bike->base.modelnum, VEHICLE_GRAPH_RUNTIME_MAX);
		}
		return NULL;
	}
	if (!created) {
		return instance;
	}
	for (i = 0; i < vehicleGraphProgramPartCount(program); i++) {
		const s32 part = vehicleGraphProgramPart(program, i);
		struct modelnode *node = hoverbikeTogglePart(bike, part);
		if (!node) {
			sysLogPrintf(LOG_ERROR,
				"VEHICLE.GRAPH: modelnum=%d refused its graph: part %d is not a toggle part of its model",
				bike->base.modelnum, part);
			vehicleGraphRuntimeRefuse(prop, prop->lifecycle_generation);
			return NULL;
		}
		{
			union modelrwdata *rwdata = modelGetNodeRwData(bike->base.model, node);
			rwdata->toggle.visible = true;
		}
	}
	heat = vehicleGraphProgramBoostHeatParams(program);
	sysLogPrintf(LOG_NOTE,
		"VEHICLE.GRAPH: modelnum=%d graph instance made: nodes=%d exec_edges=%d pending=%d parts=%d boost_heat=%d",
		bike->base.modelnum, vehicleGraphProgramNodeCount(program),
		vehicleGraphProgramExecEdgeCount(program), vehicleGraphProgramPendingBound(program),
		vehicleGraphProgramPartCount(program), heat ? 1 : 0);
	if (heat) {
		sysLogPrintf(LOG_NOTE,
			"VEHICLE.BOOST: modelnum=%d boost and heat on: boost_speed=%.2f ramp=%.0f overheat=%.0f overheated_boost=%.2f recover=%.0f ticks240",
			bike->base.modelnum, heat->boost_speed, heat->ramp_ticks240,
			heat->overheat_ticks240, heat->overheated_boost, heat->recover_ticks240);
	}
	return instance;
}

/* ---------------------------------------------------------------------------
 * The tick: once per update, where the movement runs
 * ------------------------------------------------------------------------- */

/* The top forward speed without boost, in world units per 60 Hz tick: full
 * input thrust against the drag, the same recurrence as
 * hoverbikeUpdateMovement's drag loop. */
static f32 hoverbikeTopSpeed(const asset_runtime_binding_t *source)
{
	const f32 drag = PAL ? source->vehicle_drag_pal : source->vehicle_drag_ntsc;
	const f32 thrust = source->vehicle_forward_input_scale * PALUPF(source->vehicle_forward_thrust);

	return drag < 1.0f ? thrust / (1.0f - drag) : 0.0f;
}

/* Forward speed over its top value without boost. The boost's extra speed
 * (speedrel) is not in speedabs, so this stays at 1 or below while boosting. */
static f32 hoverbikeSpeedFrac(struct hoverbikeobj *bike, const asset_runtime_binding_t *source)
{
	const f32 topspeed = hoverbikeTopSpeed(source);
	const f32 speed = sqrtf(bike->speedabs[0] * bike->speedabs[0]
		+ bike->speedabs[1] * bike->speedabs[1]);

	return topspeed > 0.0f ? speed / topspeed : 0.0f;
}

/* Fire this update's event.vehicle.tick and read the drive it asks for. A
 * change of heat phase is logged with how fast the vehicle really moved. */
void hoverbikeGraphTick(struct hoverbikeobj *bike, const asset_runtime_binding_t *source,
		vehicle_graph_instance_t *instance, f32 throttle, vehicle_graph_drive_t *drive)
{
	struct prop *prop = bike->base.prop;
	hoverbike_graph_host_t host;
	vehicle_graph_host_t table;
	vehicle_graph_event_t event;
	const vehicle_boost_heat_state_t *heat;
	s32 before;
	s32 after;
	f32 ground;

	hoverbikeGraphHostInit(&table, &host, bike, prop);
	memset(&event, 0, sizeof(event));
	event.kind = VEHICLE_GRAPH_EVENT_TICK;
	event.seat = VEHICLE_GRAPH_SEAT_NONE;
	event.player = -1;
	event.ticks240 = g_Vars.lvupdate240;
	event.ticks60 = g_Vars.lvupdate60freal;
	event.throttle = throttle;
	event.boost_held = vehicleGraphRuntimeTakeBoostHeld(prop, prop->lifecycle_generation);
	event.speed_frac = hoverbikeSpeedFrac(bike, source);
	/* How fast it really moved last update (walls included), for the phase
	 * lines: boosting on open ground takes it over its top speed. */
	ground = vehicleGraphRuntimeGroundSpeed(prop, prop->lifecycle_generation,
		prop->pos.x, prop->pos.z, g_Vars.lvupdate60freal);

	before = vehicleGraphInstanceHeatPhase(instance);
	if (vehicleGraphInstanceFire(instance, &event, &table) < 0) {
		static s32 s_WarnedTick = 0;
		if (!s_WarnedTick) {
			s_WarnedTick = 1;
			sysLogPrintf(LOG_WARNING, "VEHICLE.GRAPH: modelnum=%d tick: a module refused",
				bike->base.modelnum);
		}
	}
	after = vehicleGraphInstanceHeatPhase(instance);
	heat = vehicleGraphInstanceBoostHeat(instance);
	if (after != before && heat) {
		/* peak_ground: the fastest it moved over the ground during the phase
		 * that just ended; top: its top speed without boost (both in world
		 * units per 60 Hz tick). */
		sysLogPrintf(LOG_NOTE,
			"VEHICLE.BOOST: modelnum=%d phase %s -> %s boost=%.2f heat=%.2f speed=%.2f turret_locked=%d ground=%.2f peak_ground=%.2f top=%.2f",
			bike->base.modelnum, vehicleBoostHeatPhaseName(before),
			vehicleBoostHeatPhaseName(after), heat->boost, heat->heat, event.speed_frac,
			vehicleGraphInstanceTurretLockMask(instance) != 0, ground,
			vehicleGraphRuntimeTakePeakGround(prop, prop->lifecycle_generation),
			hoverbikeTopSpeed(source));
	}
	vehicleGraphInstanceDrive(instance, drive);
}

/* An empty vehicle at rest still runs its graph every update (its heat
 * cools); only its movement waits for a rider or a drift. */
void hoverbikeGraphIdleTick(struct hoverbikeobj *bike)
{
	const asset_runtime_binding_t *source = assetRuntimeVehicleForModelnum(bike->base.modelnum);
	vehicle_graph_instance_t *instance;
	vehicle_graph_drive_t drive;

	if (!source || !source->vehicle_graph) {
		return;
	}
	instance = hoverbikeGraphInstance(bike, source);
	if (instance) {
		hoverbikeGraphTick(bike, source, instance, 0.0f, &drive);
	}
}

/* ---------------------------------------------------------------------------
 * Seat requests: mounting, dismounting and switching seat
 * ------------------------------------------------------------------------- */

s32 hoverbikeGraphSeatRequest(struct prop *prop, s32 kind, s32 seat,
		hoverbike_seat_action_fn action, void *context)
{
	struct hoverbikeobj *bike;
	const asset_runtime_binding_t *source;
	vehicle_graph_instance_t *instance;
	hoverbike_graph_host_t host;
	vehicle_graph_host_t table;
	vehicle_graph_event_t event;

	if (!prop || prop->type != PROPTYPE_OBJ || !prop->obj
			|| prop->obj->type != OBJTYPE_HOVERBIKE) {
		return -1;
	}
	bike = (struct hoverbikeobj *)prop->obj;
	source = assetRuntimeVehicleForModelnum(bike->base.modelnum);
	if (!source || !source->vehicle_graph) {
		return -1;
	}
	instance = hoverbikeGraphInstance(bike, source);
	if (!instance) {
		return 0;
	}
	hoverbikeGraphHostInit(&table, &host, bike, prop);
	host.action = action;
	host.action_context = context;
	memset(&event, 0, sizeof(event));
	event.kind = kind;
	event.seat = seat;
	event.player = g_Vars.currentplayernum;
	vehicleGraphInstanceFire(instance, &event, &table);
	return host.seat_done;
}
