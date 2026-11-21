#ifndef _IN_MISSION_GRAPH_RUNNER_H
#define _IN_MISSION_GRAPH_RUNNER_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mission graph runner (decision 2, CORE, 2026-11-15; D-006 option A).
 *
 * A mission graph (pd2.mission.graph.v1, mission.graph.json) holds data nodes
 * (phases, objectives) joined by structural edges, and executable nodes joined
 * by execution edges: edges whose "input" is "exec". When the mission graph
 * activates, its nodes and execution edges compile into a program. A gameplay
 * seam fires an event into the program; the runner walks the execution edges
 * from every event node of that kind, in authored order, and runs each gate
 * and native module it reaches at most once per event. A gate follows its
 * "pass" edges when it passes and its "fail" edges when it does not; an event
 * or a module follows its "out" edges (an execution edge without "output"
 * counts as "out"). Structural edges are checked but never walked.
 *
 * Executable kinds, validated when the program compiles:
 *   event.objective.status_changed  An objective that applies at the current
 *       difficulty changed status. Optional "objective": "any" (the default)
 *       or the id of a mission.objective.source node.
 *   event.mission.completed  A solo mission was completed without cheats; the
 *       game has just recorded the completion, before its end screen shows.
 *   event.mission.continue  What a completed mission leads to. Seam ENDSCREEN:
 *       the player continues from its end screen (or it continues by itself
 *       on a first completion); a mission with a handler for this event skips
 *       the next-mission menu. Seam COOP_STAGE_END: a co-op stage of a mission
 *       whose continue reaches og.mission.start_next_stage has just ended; the
 *       co-op end screen is skipped (the original Deep Sea rule).
 *   event.mission.phase_changed  The mission's phase moved to "active",
 *       "complete" or "failed" (mission-flow.md): the runtime delivers it from
 *       the objectives check after the objectives' own events, never while
 *       another event is being delivered.
 *   event.volume.entered / event.volume.left  A player went into or out of
 *       a box of the stage's volumes.json (trigger-volume-events.md).
 *       Required "volume": the volume_id of the row (no "any"); the
 *       runtime binds it when the mission graph activates and delivers the
 *       event from the objectives check, after the phase event.
 *   gate.objective.status  Passes when the event's status is its "status"
 *       ("complete", "incomplete" or "failed"); optional "objective" as above.
 *   gate.mission.phase  Passes when a phase event's phase is its "phase"
 *       ("active", "complete" or "failed"); never for any other event.
 *   og.*  A native module from the caller's module table, for example
 *       og.hud.objective_status_message (the original "Objective N: Completed"
 *       message). An og.* kind the table does not know refuses the program.
 *   event.pickup.collected, event.door.opened, event.door.closed,
 *       event.object.used, event.glass.broken (object-events.md slices 2 and
 *       5) and the rule events event.pickup.touched, event.door.use,
 *       event.door.approach, event.lift.called, event.lift.idle,
 *       event.lift.arrived, event.glass.damaged (slices 4 and 5, level
 *       programs only): an object's event; optional "object": "any" (the
 *       default) or an objects.json record_id.
 *   event.projectile.spawned, .bounced, .hit_target, .landed, .ended,
 *       .detonated and .timer_expired (projectile-events.md slice 2):
 *       projectile and entity programs only
 *       (missionGraphProgramCompileProjectile); the event's instance is the
 *       weapon object. A mission or level graph that names one is refused,
 *       and a projectile program refuses every other event.
 *   event.weapon.trigger_pressed and .ready_while_held
 *       (held-weapon-events.md slice 1): weapon programs only
 *       (missionGraphProgramCompileWeapon); the event's instance is the
 *       hand's executor. Any other graph that names one is refused, and a
 *       weapon program refuses every other program event.
 * Any other event.* kind may only have structural edges (event.mission.load).
 * Mission events carry no objective; "objective" on their nodes is refused,
 * and an objective gate never passes for them.
 */

/* Objective statuses, the native OBJECTIVE_* values. */
#define MISSION_GRAPH_STATUS_INCOMPLETE 0
#define MISSION_GRAPH_STATUS_COMPLETE 1
#define MISSION_GRAPH_STATUS_FAILED 2

#define MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED 1
#define MISSION_GRAPH_EVENT_MISSION_COMPLETED 2
#define MISSION_GRAPH_EVENT_MISSION_CONTINUE 3
#define MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED 4
#define MISSION_GRAPH_EVENT_VOLUME_ENTERED 5
#define MISSION_GRAPH_EVENT_VOLUME_LEFT 6
/* object-events.md slice 2: from the native seams, after the native decision
 * and effect; an event node may filter one object by its record_id. */
#define MISSION_GRAPH_EVENT_PICKUP_COLLECTED 7
#define MISSION_GRAPH_EVENT_DOOR_OPENED 8
#define MISSION_GRAPH_EVENT_DOOR_CLOSED 9
#define MISSION_GRAPH_EVENT_OBJECT_USED 10
/* object-events.md slice 4: the rule events. On a scenario-source stage the
 * native seams decide nothing; the level program does, through
 * gate.pickup.native_rules and og.pickup.collect, gate.door.native_unlocked,
 * og.door.activate, og.door.locked_message and og.door.activate_automatic.
 * Level programs only: a mission graph that names one is refused. */
#define MISSION_GRAPH_EVENT_PICKUP_TOUCHED 11
#define MISSION_GRAPH_EVENT_DOOR_USE 12
#define MISSION_GRAPH_EVENT_DOOR_APPROACH 13
/* object-events.md slice 5: the lift and glass rule events (gate.lift.*,
 * og.lift.*, gate.glass.native_broken, og.glass.break), level programs
 * only, and the record event after a glass object breaks. */
#define MISSION_GRAPH_EVENT_LIFT_CALLED 14
#define MISSION_GRAPH_EVENT_LIFT_IDLE 15
#define MISSION_GRAPH_EVENT_LIFT_ARRIVED 16
#define MISSION_GRAPH_EVENT_GLASS_DAMAGED 17
#define MISSION_GRAPH_EVENT_GLASS_BROKEN 18
/* projectile-events.md slice 2: the projectile events, projectile and entity
 * programs only. */
#define MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED 19
#define MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED 20
#define MISSION_GRAPH_EVENT_PROJECTILE_HIT_TARGET 21
#define MISSION_GRAPH_EVENT_PROJECTILE_LANDED 22
#define MISSION_GRAPH_EVENT_PROJECTILE_ENDED 23
#define MISSION_GRAPH_EVENT_PROJECTILE_DETONATED 24
#define MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED 25
/* projectile-events.md slice 3: the contact rule events. A projectile's
 * sweep struck something (contact); its first step, from where it was made
 * to the muzzle, was blocked (launch_blocked); it lies or all but lies still
 * while still in flight (came_to_rest). With bounced and landed they choose
 * the contact modules (projectile_rules.h). */
#define MISSION_GRAPH_EVENT_PROJECTILE_CONTACT 26
#define MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED 27
#define MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST 28
/* projectile-events.md slice 4: the device rule events. Something is within
 * reach of an armed proximity item (proximity); a remote mine's owner
 * pressed the detonator (remote_signal); a weapon object took damage
 * (damaged). They choose og.projectile.spend_fuse; timer_expired then
 * chooses what goes off. */
#define MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY 29
#define MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL 30
#define MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED 31
/* projectile-events.md slice 5: the sentry rule events. A throw is about to
 * make its object (thrown: no object exists yet, and the event's instance is
 * the thrower's gset); a deployed sentry gun has its target in its sights
 * (sentry_aimed). They choose og.projectile.deploy_sentry and
 * og.projectile.sentry_fire. */
#define MISSION_GRAPH_EVENT_PROJECTILE_THROWN 32
#define MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED 33
/* projectile-events.md slice 6: the arm rule event. A weapon object is about
 * to be ticked for the first time (armed). It chooses the arm weaponTick
 * runs for it from then on (og.projectile.arm_*): what its timer means and
 * what it listens for. */
#define MISSION_GRAPH_EVENT_PROJECTILE_ARMED 34
/* held-weapon-events.md slice 1: the held weapon events, weapon programs
 * only (missionGraphProgramCompileWeapon). A weapon function's hand saw a new
 * trigger press, or is ready again with the trigger still held; the event's
 * instance is the hand's executor. */
#define MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED 35
#define MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD 36
/* held-weapon-events.md slice 2: an idle hand's trigger is up and its clip
 * is below what a shot needs while there is reserve (clip_empty, a rule
 * event); the trigger came up (trigger_released) and a reload finished
 * (reload_done), records. weapon_rules.h says what each event applies. */
#define MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY 37
#define MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED 38
#define MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE 39

/* The phases a phase event carries (mission_graph_event_t.phase). The phase
 * table's load and end are not delivered: nothing runs before the stage starts
 * or while it ends. */
#define MISSION_GRAPH_PHASE_ACTIVE 1
#define MISSION_GRAPH_PHASE_COMPLETE 2
#define MISSION_GRAPH_PHASE_FAILED 3

/* Where a mission event came from (mission_graph_event_t.seam). */
#define MISSION_GRAPH_SEAM_NONE 0
#define MISSION_GRAPH_SEAM_ENDSCREEN 1
#define MISSION_GRAPH_SEAM_COOP_STAGE_END 2

/* Compile limits; a larger graph is refused, not truncated. */
#define MISSION_GRAPH_MAX_NODES 4096
#define MISSION_GRAPH_MAX_EDGES 16384
#define MISSION_GRAPH_MAX_VOLUMES 32 /* distinct volumes event.volume.* names */

typedef struct mission_graph_event {
	s32 kind;              /* MISSION_GRAPH_EVENT_* */
	s32 objective_index;   /* runtime objective index */
	s32 objective_number;  /* 1-based among the objectives that apply */
	s32 status;            /* MISSION_GRAPH_STATUS_* */
	s32 previous_status;
	const char *objective_node; /* its mission.objective.source id, or NULL */
	s32 seam;              /* MISSION_GRAPH_SEAM_*, for mission events */
	s32 phase;             /* MISSION_GRAPH_PHASE_*, for phase events */
	const char *volume;    /* volume_id, for volume events */
	s32 player;            /* player index, for volume and object events (-1: none) */
	/* object events (object-events.md slice 2) */
	const char *object;      /* the objects.json record_id ("setup_0123"); "" if none */
	s32 tag;                 /* the id of the tag record that names it, or -1 */
	const char *object_kind; /* its objects.json kind ("door", "weapon_pickup", ...) */
	const char *catalog_id;  /* a pickup's weapon, else the object's model; may be NULL */
	/* projectile events (projectile-events.md slice 2) */
	void *instance;          /* the object (struct defaultobj *), or for event.projectile.thrown the thrower's gset; never NULL */
} mission_graph_event_t;

/* "active", "complete" or "failed" as a MISSION_GRAPH_PHASE_* value, or -1. */
s32 missionGraphPhaseFromText(const char *text);

typedef struct mission_graph_program mission_graph_program_t;

/* object-events.md slice 3: a program node's parameters, its scalar fields
 * other than id and kind, as text: a string as it is, a whole number in
 * decimal, a boolean as "true" or "false". NULL for a field it lacks. */
typedef struct mission_graph_params mission_graph_params_t;
const char *missionGraphParamText(const mission_graph_params_t *params, const char *key);

/* The caller's native module table: whether an og.* kind (or a native
 * gate.* kind, object-events.md slice 3) exists, and running one. For a
 * module, run returns 1 when it ran and 0 when it refused the event; for a
 * native gate, 1 when it passes and 0 when it fails. params are the node's. */
typedef s32 (*mission_graph_module_known_fn)(const char *kind);
typedef s32 (*mission_graph_module_run_fn)(const char *kind, const char *node_id,
	const mission_graph_params_t *params, const mission_graph_event_t *event, void *user);

/* Compile mission.graph.json text. Returns NULL and writes the reason to err
 * when the graph is malformed or names something the runner cannot execute. */
mission_graph_program_t *missionGraphProgramCompile(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap);
/* object-events.md slice 1: compile a level graph's program
 * (level.graph.json). Only its event.*, gate.* and og.* nodes are the
 * program, at most MISSION_GRAPH_MAX_NODES of them; every other node is
 * level data (AI lists, setup, rooms), skipped and not counted, and an edge
 * without "input" may name one. An execution edge still joins program
 * nodes. A level graph without program nodes compiles to an empty program. */
mission_graph_program_t *missionGraphProgramCompileLevel(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap);
/* projectile-events.md slice 2: compile a projectile or entity archive's
 * program (its behavior.graph.json). As for a level graph, only the event.*,
 * gate.* and og.* nodes are the program and every other node is data
 * (projectile.spawn_state, entity.*), skipped and not counted; its events
 * are the projectile events and no other. A graph without program nodes
 * compiles to an empty program. */
mission_graph_program_t *missionGraphProgramCompileProjectile(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap);
/*
 * held-weapon-events.md slice 1: compile a held weapon function's program (a
 * .pdweapon function graph, pd.weapon_graph.v2). As for a projectile graph,
 * only the event.*, gate.* and og.* nodes are the program; every other node
 * is the function's record (fire.hitscan, melee.strike, ...), skipped and not
 * counted. Its events are the weapon events (event.weapon.*) and no other; an
 * event.* node of the record (event.trigger_pressed) is inert and may only
 * have structural edges.
 *
 * A weapon program's native modules may finish later than the walk that
 * reached them (a shot is accepted now and delivered ticks later). For such a
 * module, module_known returns MISSION_GRAPH_MODULE_ASYNC instead of 1; it
 * leaves by its outcome, "completed", "blocked", "failed" or "cancelled",
 * and never by "out". Its run returns the outcome
 * (MISSION_GRAPH_RUN_COMPLETED .. _CANCELLED), or MISSION_GRAPH_RUN_PENDING
 * while the work goes on; the caller later routes the outcome into the same
 * activation (missionGraphActivationRoute). A gate of a weapon program may
 * also leave by "failed": its run returns MISSION_GRAPH_RUN_GATE_FAILED when
 * it cannot be answered, and then neither "pass" nor "fail" is followed.
 */
mission_graph_program_t *missionGraphProgramCompileWeapon(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap);
void missionGraphProgramFree(mission_graph_program_t *program);

/* module_known's answer for a weapon program's module that finishes later
 * (see missionGraphProgramCompileWeapon); 1 is a module or gate that is done
 * when its run returns. */
#define MISSION_GRAPH_MODULE_ASYNC 2

/* What a run returns. Every program: 0 refused, MISSION_GRAPH_RUN_OUT ran (a
 * module) or passed (a gate). A weapon program's asynchronous module returns
 * one of the outcomes below instead, and a weapon program's gate may return
 * MISSION_GRAPH_RUN_GATE_FAILED. */
#define MISSION_GRAPH_RUN_GATE_FAILED (-1)
#define MISSION_GRAPH_RUN_OUT 1
#define MISSION_GRAPH_RUN_PENDING 2
#define MISSION_GRAPH_RUN_COMPLETED 3
#define MISSION_GRAPH_RUN_BLOCKED 4
#define MISSION_GRAPH_RUN_FAILED 5
#define MISSION_GRAPH_RUN_CANCELLED 6

/* held-weapon-events.md slice 1: whether the kind is a weapon event
 * (MISSION_GRAPH_EVENT_WEAPON_*). */
s32 missionGraphEventIsWeapon(s32 event_kind);

/* A program node's place (0 .. node count - 1) by its id, or -1; and the id
 * and kind at a place, or NULL. Places are the program's own: a record node
 * a weapon or projectile program skipped has none. */
s32 missionGraphProgramNodeIndex(const mission_graph_program_t *program, const char *node_id);
const char *missionGraphProgramNodeId(const mission_graph_program_t *program, s32 index);
const char *missionGraphProgramNodeKind(const mission_graph_program_t *program, s32 index);

/*
 * Activations (held-weapon-events.md slice 1). missionGraphProgramFire walks
 * one event and forgets it. An activation keeps what one event reached, so a
 * walk that stops at a pending module can go on from it later: every node
 * still runs at most once per activation, and a node an outcome reaches
 * joins the same breadth-first order the event started. One activation
 * belongs to one caller (a hand); it must not outlive its program.
 *
 *   Begin  every node may run again; the event's roots are queued (each
 *          matching event node leaves by "out").
 *   Route  a pending module's outcome is queued: its edges of that port.
 *   Walk   the queue runs to empty, breadth first in authored order. Returns
 *          the modules run, or -1 when the event is malformed or a module
 *          refused it (the walk still runs to empty, as Fire's does).
 *
 * The optional trace sees every port the activation routes, in order: the
 * node's id and "out", "pass", "fail", "failed", "completed", "blocked" or
 * "cancelled". missionGraphProgramFire is Begin and Walk on a fresh
 * activation.
 */
typedef struct mission_graph_activation mission_graph_activation_t;
typedef void (*mission_graph_trace_fn)(const char *node_id, const char *port, void *user);
mission_graph_activation_t *missionGraphActivationCreate(const mission_graph_program_t *program);
void missionGraphActivationFree(mission_graph_activation_t *activation);
void missionGraphActivationSetTrace(mission_graph_activation_t *activation,
	mission_graph_trace_fn trace, void *user);
s32 missionGraphActivationBegin(mission_graph_activation_t *activation, const mission_graph_event_t *event);
s32 missionGraphActivationRoute(mission_graph_activation_t *activation, s32 node_index, s32 outcome);
s32 missionGraphActivationWalk(mission_graph_activation_t *activation, const mission_graph_event_t *event,
	mission_graph_module_run_fn run, void *user);

s32 missionGraphProgramNodeCount(const mission_graph_program_t *program);
s32 missionGraphProgramExecEdgeCount(const mission_graph_program_t *program);
s32 missionGraphProgramEventNodeCount(const mission_graph_program_t *program, s32 event_kind);
s32 missionGraphProgramModuleNodeCount(const mission_graph_program_t *program);
typedef s32 (*mission_graph_module_param_visitor_t)(const char *kind,
	const mission_graph_params_t *params, void *userdata);
s32 missionGraphProgramVisitModuleParams(const mission_graph_program_t *program,
	mission_graph_module_param_visitor_t visit, void *userdata);
/* The distinct volumes event.volume.* nodes name, in authored order. */
s32 missionGraphProgramVolumeCount(const mission_graph_program_t *program);
const char *missionGraphProgramVolumeAt(const mission_graph_program_t *program, s32 index);

/* Whether firing an event of this kind would run anything: some event node of
 * the kind has an execution edge. */
s32 missionGraphProgramHandlesEvent(const mission_graph_program_t *program, s32 event_kind);

/* object-events.md slices 4 and 5: whether the kind is a rule event
 * (MISSION_GRAPH_EVENT_PICKUP_TOUCHED, _DOOR_USE, _DOOR_APPROACH,
 * _LIFT_CALLED, _LIFT_IDLE, _LIFT_ARRIVED, _GLASS_DAMAGED). */
s32 missionGraphEventIsObjectRule(s32 event_kind);

/* projectile-events.md slice 2: whether the kind is a projectile event
 * (MISSION_GRAPH_EVENT_PROJECTILE_*). */
s32 missionGraphEventIsProjectile(s32 event_kind);

/* Whether an event of this kind can reach a native module of this og.* kind,
 * or a native gate of this gate.* kind, along execution edges (both branches
 * of every gate). */
s32 missionGraphProgramEventReachesModule(const mission_graph_program_t *program,
	s32 event_kind, const char *module_kind);

/* Fire one event. Returns the number of modules run (0 when no event node of
 * that kind matches), or -1 when the event is malformed or a module refused
 * it; the modules before the refusal have run. */
s32 missionGraphProgramFire(const mission_graph_program_t *program,
	const mission_graph_event_t *event, mission_graph_module_run_fn run, void *user);

/* Compare the same live level graph under its two verified catalog IDs.
 * Only its root and the native settings node may change identity. */
s32 missionGraphLevelLayoutCompatible(const char *outgoing, size_t outgoing_size, const char *outgoing_id,
	const char *incoming, size_t incoming_size, const char *incoming_id);

#ifdef __cplusplus
}
#endif

#endif
