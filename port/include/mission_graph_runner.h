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
	s32 player;            /* player index, for volume events */
} mission_graph_event_t;

/* "active", "complete" or "failed" as a MISSION_GRAPH_PHASE_* value, or -1. */
s32 missionGraphPhaseFromText(const char *text);

typedef struct mission_graph_program mission_graph_program_t;

/* The caller's native module table: whether an og.* kind exists, and running
 * one. run returns 1 when the module ran and 0 when it refused the event. */
typedef s32 (*mission_graph_module_known_fn)(const char *kind);
typedef s32 (*mission_graph_module_run_fn)(const char *kind, const char *node_id,
	const mission_graph_event_t *event, void *user);

/* Compile mission.graph.json text. Returns NULL and writes the reason to err
 * when the graph is malformed or names something the runner cannot execute. */
mission_graph_program_t *missionGraphProgramCompile(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap);
void missionGraphProgramFree(mission_graph_program_t *program);

s32 missionGraphProgramNodeCount(const mission_graph_program_t *program);
s32 missionGraphProgramExecEdgeCount(const mission_graph_program_t *program);
s32 missionGraphProgramEventNodeCount(const mission_graph_program_t *program, s32 event_kind);
s32 missionGraphProgramModuleNodeCount(const mission_graph_program_t *program);
/* The distinct volumes event.volume.* nodes name, in authored order. */
s32 missionGraphProgramVolumeCount(const mission_graph_program_t *program);
const char *missionGraphProgramVolumeAt(const mission_graph_program_t *program, s32 index);

/* Whether firing an event of this kind would run anything: some event node of
 * the kind has an execution edge. */
s32 missionGraphProgramHandlesEvent(const mission_graph_program_t *program, s32 event_kind);

/* Whether an event of this kind can reach a native module of this og.* kind
 * along execution edges (both branches of every gate). */
s32 missionGraphProgramEventReachesModule(const mission_graph_program_t *program,
	s32 event_kind, const char *module_kind);

/* Fire one event. Returns the number of modules run (0 when no event node of
 * that kind matches), or -1 when the event is malformed or a module refused
 * it; the modules before the refusal have run. */
s32 missionGraphProgramFire(const mission_graph_program_t *program,
	const mission_graph_event_t *event, mission_graph_module_run_fn run, void *user);

#ifdef __cplusplus
}
#endif

#endif
