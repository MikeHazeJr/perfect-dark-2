#ifndef _IN_MISSION_GRAPH_MODULES_H
#define _IN_MISSION_GRAPH_MODULES_H

#include <PR/ultratypes.h>
#include "mission_graph_runner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's native modules for mission and level graphs (og.*) and its
 * native gates (gate.*, object-events.md slice 3), the table the graph
 * runner compiles and fires against. */
s32 missionGraphModuleKnown(const char *kind);
s32 missionGraphModuleRun(const char *kind, const char *node_id,
	const mission_graph_params_t *params, const mission_graph_event_t *event, void *user);

/* object-events.md slice 3: the answer to a query event
 * (MISSION_GRAPH_EVENT_PICKUP_REQUESTED, _DOOR_OPEN_REQUESTED). Begin before
 * firing it (it returns the answer of any query it nests in); End returns
 * whether og.pickup.refuse or og.door.refuse ran, and restores that outer
 * answer. */
s32 missionGraphModulesQueryBegin(void);
s32 missionGraphModulesQueryEnd(s32 outer);

#ifdef __cplusplus
}
#endif

#endif
