/**
 * vehicle_graph_runner.h -- pd.vehicle_graph.v1, a vehicle's behavior graph.
 *
 * The schema is context/designs/modding/vehicle-graph-v1.md, accepted by CORE
 * on 2026-11-16 with the answers recorded at its end.
 *
 * Compile. A .pdvehicle's behavior.graph.json and its optional
 * behavior/variables.json (pd.vehicle_variables.v1) compile into one
 * immutable program. The compile rules are the mission graph runner's
 * (port/src/mission_graph_runner.cpp on week-pd/core): execution edges carry
 * "input": "exec"; a gate leaves by "pass" or "fail", an event or a module by
 * "out" (or no output); an execution edge starts at an event, a gate or a
 * module and ends at a gate or a module; any other kind is data, and data
 * nodes and structural edges are checked but never walked. Every executable
 * kind is known here with its params, which are checked and converted to
 * native units when the program compiles; a "$name" param takes the
 * variable's value whole. A module may run only on the events it is made for
 * (og.vehicle.hover_drive on event.vehicle.tick, ...), and a graph whose
 * events can reach a module on the wrong event is refused. One event may
 * reach one mount, one dismount and one switch per seat. A broken rule
 * refuses the program, and a refused program runs nothing.
 *
 * Seats. A vehicle has a driver seat and may have a gunner seat. Three
 * requests change who sits where, each naming the seat it is about:
 * mount_requested (the seat asked for), dismount_requested (the seat left)
 * and switch_seat_requested (the seat a rider asks to move to). The graph's
 * gates decide (gate.vehicle.seat, gate.vehicle.seat_free) and its seat
 * modules ask the host to do it (og.vehicle.mount_seat, dismount_seat and
 * switch_seat).
 *
 * Run. One instance per vehicle holds the program, the per-node state and the
 * pending events (D-006A's shape). A native seam fires an event; the walk is
 * breadth first in authored order and runs each reached node at most once per
 * activation; a module that refuses does not continue to its successors.
 * Events a module raises (og.vehicle.boost_heat raises
 * event.vehicle.heat_changed when its phase changes) are queued and delivered
 * after the walk, in order, each as its own activation. The compiler refuses a
 * graph in which an event's walk can reach a module that raises an event
 * already being delivered (directly or through other raised events), so
 * delivery always ends, and it proves how many events one activation can lead
 * to; the instance's queue has exactly that room and nothing is allocated
 * after the instance is made.
 *
 * Tick. event.vehicle.tick carries the update's 60 and 240 Hz ticks and the
 * driver's inputs. Its modules only record this update's drive (whether to
 * hover and drive, extra forward speed, steering scale); the host's movement
 * (the original hoverbikeUpdateMovement) reads it after the walk, so the
 * physics order stays the original's whatever order the modules are authored
 * in.
 */
#ifndef _IN_VEHICLE_GRAPH_RUNNER_H
#define _IN_VEHICLE_GRAPH_RUNNER_H

#include <PR/ultratypes.h>
#include <stddef.h>

#include "vehicle_boost_heat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VEHICLE_GRAPH_SCHEMA "pd.vehicle_graph.v1"
#define VEHICLE_VARIABLES_SCHEMA "pd.vehicle_variables.v1"

/* Compile limits, as the mission graph runner's; larger is refused. */
#define VEHICLE_GRAPH_MAX_NODES 4096
#define VEHICLE_GRAPH_MAX_EDGES 16384
/* Distinct og.vehicle.turret_lockout reasons one graph may name. */
#define VEHICLE_GRAPH_MAX_LOCKOUT_REASONS 32
/* Follow-up events one fired event may lead to, proved at compile time. */
#define VEHICLE_GRAPH_MAX_PENDING 64

/* Event kinds (vehicle_graph_event_t.kind). */
#define VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED 1
#define VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED 2
#define VEHICLE_GRAPH_EVENT_TICK 3
#define VEHICLE_GRAPH_EVENT_HEAT_CHANGED 4 /* raised by og.vehicle.boost_heat only */
#define VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED 5 /* a rider asks for the other seat */
#define VEHICLE_GRAPH_EVENT_KIND_END 6

/* Seats. */
#define VEHICLE_GRAPH_SEAT_NONE (-1)
#define VEHICLE_GRAPH_SEAT_DRIVER 0
#define VEHICLE_GRAPH_SEAT_GUNNER 1
#define VEHICLE_GRAPH_SEAT_COUNT 2

typedef struct vehicle_graph_event {
	s32 kind;           /* VEHICLE_GRAPH_EVENT_* */
	/* A seat request's seat: the seat a mount asks for, the seat a dismount
	 * leaves, the seat a switch asks to move to. Else SEAT_NONE. */
	s32 seat;
	s32 player;         /* a seat request's player number; else -1 */
	/* tick */
	s32 ticks240;       /* this update's 240 Hz ticks (g_Vars.lvupdate240) */
	f32 ticks60;        /* this update's 60 Hz ticks (g_Vars.lvupdate60freal) */
	f32 throttle;       /* the driver's forward input, -1..1 (0 with no driver) */
	s32 boost_held;     /* the driver holds the boost action this update */
	f32 speed_frac;     /* forward speed over the top non-boost speed */
	/* heat_changed */
	s32 phase;          /* vehicle_boost_heat_phase_t */
	s32 previous_phase;
} vehicle_graph_event_t;

/* What one tick's walk asks of the movement this update. */
typedef struct vehicle_graph_drive {
	s32 hover;          /* og.vehicle.hover_drive ran: hover and move */
	s32 drive;          /* ... taking the driver's inputs */
	f32 extra_speed;    /* the boost modules' extra forward speed (units per 60 Hz tick) */
	f32 steer_scale;    /* the boost modules' steering scale, 1 = unchanged */
} vehicle_graph_drive_t;

/* The native effects a graph asks for. Each returns 1 when done and 0 when
 * it refused. A NULL callback refuses (a gate on it fails). */
typedef struct vehicle_graph_host {
	void *context;
	s32 (*seat_free)(void *context, s32 seat);
	s32 (*mount_seat)(void *context, s32 seat, const vehicle_graph_event_t *event);
	s32 (*dismount_seat)(void *context, s32 seat, const vehicle_graph_event_t *event);
	/* og.vehicle.switch_seat: move the event's player, who rides this
	 * vehicle, to this seat. */
	s32 (*switch_seat)(void *context, s32 seat, const vehicle_graph_event_t *event);
	s32 (*set_part_visible)(void *context, s32 part, s32 visible);
} vehicle_graph_host_t;

/* At compile time: whether the vehicle's model has this part. NULL skips the
 * check (unit tests). */
typedef s32 (*vehicle_graph_has_part_fn)(void *context, s32 part);

typedef struct vehicle_graph_program vehicle_graph_program_t;
typedef struct vehicle_graph_instance vehicle_graph_instance_t;

/* Whether this text is a pd.vehicle_graph.v1 document (its "schema"), so the
 * caller can tell it from a v2 behavior file before compiling. */
s32 vehicleGraphIsV1(const char *json, size_t json_size);

/* The executable kinds the runner knows (events, then gates, then modules),
 * from index 0; NULL past the end. The conformance tool's mirror is checked
 * against this list. */
const char *vehicleGraphKnownKind(s32 index);

/*
 * Compile behavior.graph.json with its behavior/variables.json (NULL and 0
 * when the archive has none). catalog_id, when not NULL, must equal the
 * graph's "catalog_id" (and the variables' when they name one). Returns NULL
 * and writes the reason to err when anything is wrong.
 */
vehicle_graph_program_t *vehicleGraphProgramCompile(const char *graph_json,
	size_t graph_size, const char *variables_json, size_t variables_size,
	const char *catalog_id, vehicle_graph_has_part_fn has_part,
	void *has_part_context, char *err, size_t err_cap);
void vehicleGraphProgramFree(vehicle_graph_program_t *program);

s32 vehicleGraphProgramNodeCount(const vehicle_graph_program_t *program);
s32 vehicleGraphProgramExecEdgeCount(const vehicle_graph_program_t *program);
/* Whether an event of this kind can reach a module of this og.* kind along
 * execution edges (both branches of every gate, raised events included). */
s32 vehicleGraphProgramEventReachesModule(const vehicle_graph_program_t *program,
	s32 event_kind, const char *module_kind);
/* The seats a request of this kind can act on, one bit per seat (1 << seat):
 * the seats a mount can take (og.vehicle.mount_seat), a dismount can leave
 * (og.vehicle.dismount_seat) or a switch can move to (og.vehicle.switch_seat),
 * along both branches of every gate. 0 for any other event. A vehicle has a
 * gunner seat when a mount request reaches a mount for it. */
u32 vehicleGraphProgramEventSeats(const vehicle_graph_program_t *program, s32 event_kind);
/* The most follow-up events one fired event can lead to. */
s32 vehicleGraphProgramPendingBound(const vehicle_graph_program_t *program);
/* The og.vehicle.boost_heat params, or NULL when the graph has none. */
const vehicle_boost_heat_params_t *vehicleGraphProgramBoostHeatParams(
	const vehicle_graph_program_t *program);
/* A turret lockout reason's bit (0-based), or -1 when the graph has none. */
s32 vehicleGraphProgramLockoutReason(const vehicle_graph_program_t *program,
	const char *reason);
/* The distinct model parts the graph's og.vehicle.part_visible nodes name, in
 * authored order, so the caller can check them against the vehicle's model
 * once it is built. */
s32 vehicleGraphProgramPartCount(const vehicle_graph_program_t *program);
s32 vehicleGraphProgramPart(const vehicle_graph_program_t *program, s32 index);

/* One vehicle's state; the program must outlive it. NULL when out of memory. */
vehicle_graph_instance_t *vehicleGraphInstanceCreate(const vehicle_graph_program_t *program);
void vehicleGraphInstanceFree(vehicle_graph_instance_t *instance);
const vehicle_graph_program_t *vehicleGraphInstanceProgram(
	const vehicle_graph_instance_t *instance);

/*
 * Fire mount_requested, dismount_requested, switch_seat_requested or tick,
 * then deliver what it raises. Returns the number of modules run, or -1 when
 * the event is malformed or a module refused (the other modules still ran).
 */
s32 vehicleGraphInstanceFire(vehicle_graph_instance_t *instance,
	const vehicle_graph_event_t *event, const vehicle_graph_host_t *host);

/* After a tick: the drive for this update (no hover before the first tick). */
void vehicleGraphInstanceDrive(const vehicle_graph_instance_t *instance,
	vehicle_graph_drive_t *out);
/* The original rest reset (an empty vehicle came to a stop): the throttle
 * bonus timers go back to 0, as the original zeroes maxspeedtime240. */
void vehicleGraphInstanceRest(vehicle_graph_instance_t *instance);

/* The heat phase: cool when the graph has no og.vehicle.boost_heat. */
s32 vehicleGraphInstanceHeatPhase(const vehicle_graph_instance_t *instance);
/* The boost and heat state, or NULL when the graph has none. */
const vehicle_boost_heat_state_t *vehicleGraphInstanceBoostHeat(
	const vehicle_graph_instance_t *instance);
/* The set turret lockouts as bits over the program's reasons. */
u32 vehicleGraphInstanceTurretLockMask(const vehicle_graph_instance_t *instance);

#ifdef __cplusplus
}
#endif

#endif
