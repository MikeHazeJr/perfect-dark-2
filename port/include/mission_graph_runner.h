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
 *   gate.objective.status  Passes when the event's status is its "status"
 *       ("complete", "incomplete" or "failed"); optional "objective" as above.
 *   og.*  A native module from the caller's module table, for example
 *       og.hud.objective_status_message (the original "Objective N: Completed"
 *       message). An og.* kind the table does not know refuses the program.
 * Any other event.* kind may only have structural edges (event.mission.load).
 */

/* Objective statuses, the native OBJECTIVE_* values. */
#define MISSION_GRAPH_STATUS_INCOMPLETE 0
#define MISSION_GRAPH_STATUS_COMPLETE 1
#define MISSION_GRAPH_STATUS_FAILED 2

#define MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED 1

/* Compile limits; a larger graph is refused, not truncated. */
#define MISSION_GRAPH_MAX_NODES 4096
#define MISSION_GRAPH_MAX_EDGES 16384

typedef struct mission_graph_event {
	s32 kind;              /* MISSION_GRAPH_EVENT_* */
	s32 objective_index;   /* runtime objective index */
	s32 objective_number;  /* 1-based among the objectives that apply */
	s32 status;            /* MISSION_GRAPH_STATUS_* */
	s32 previous_status;
	const char *objective_node; /* its mission.objective.source id, or NULL */
} mission_graph_event_t;

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

/* Fire one event. Returns the number of modules run (0 when no event node of
 * that kind matches), or -1 when the event is malformed or a module refused
 * it; the modules before the refusal have run. */
s32 missionGraphProgramFire(const mission_graph_program_t *program,
	const mission_graph_event_t *event, mission_graph_module_run_fn run, void *user);

#ifdef __cplusplus
}
#endif

#endif
