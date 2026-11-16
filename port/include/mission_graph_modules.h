#ifndef _IN_MISSION_GRAPH_MODULES_H
#define _IN_MISSION_GRAPH_MODULES_H

#include <PR/ultratypes.h>
#include "mission_graph_runner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's native modules for mission graphs (og.*), the module table the
 * mission graph runner compiles and fires against. */
s32 missionGraphModuleKnown(const char *kind);
s32 missionGraphModuleRun(const char *kind, const char *node_id,
	const mission_graph_event_t *event, void *user);

#ifdef __cplusplus
}
#endif

#endif
