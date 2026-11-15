/*
 * The game's native modules for mission graphs (decision 2, CORE, 2026-11-15).
 * Each og.* kind a mission graph may wire is one row here; the runner refuses
 * a graph that names any other og.* kind when it compiles.
 */
#include <string.h>

#include "mission_graph_modules.h"
#include "game/objectives.h"

/* og.hud.objective_status_message: the original "Objective N: Completed",
 * "Incomplete" or "Failed" message, shown to every player
 * (objectivesShowStatusMessage, src/game/objectives.c). */
static s32 s_runObjectiveStatusMessage(const mission_graph_event_t *event)
{
	if (event->kind != MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED || event->objective_number < 1) {
		return 0;
	}
	objectivesShowStatusMessage(event->objective_number, event->status);
	return 1;
}

static const struct {
	const char *kind;
	s32 (*run)(const mission_graph_event_t *event);
} s_MissionGraphModules[] = {
	{ "og.hud.objective_status_message", s_runObjectiveStatusMessage },
};

s32 missionGraphModuleKnown(const char *kind)
{
	size_t i;

	if (!kind) return 0;
	for (i = 0; i < sizeof(s_MissionGraphModules) / sizeof(s_MissionGraphModules[0]); i++) {
		if (strcmp(kind, s_MissionGraphModules[i].kind) == 0) return 1;
	}
	return 0;
}

s32 missionGraphModuleRun(const char *kind, const char *node_id,
	const mission_graph_event_t *event, void *user)
{
	size_t i;

	(void)node_id;
	(void)user;
	if (!kind || !event) return 0;
	for (i = 0; i < sizeof(s_MissionGraphModules) / sizeof(s_MissionGraphModules[0]); i++) {
		if (strcmp(kind, s_MissionGraphModules[i].kind) == 0) {
			return s_MissionGraphModules[i].run(event);
		}
	}
	return 0;
}
