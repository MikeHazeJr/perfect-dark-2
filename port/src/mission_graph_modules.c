/*
 * The game's native modules for mission graphs (decision 2, CORE, 2026-11-15).
 * Each og.* kind a mission graph may wire is one row here; the runner refuses
 * a graph that names any other og.* kind when it compiles.
 */
#include <string.h>

#include "mission_graph_modules.h"
#include "game/endscreen.h"
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

/* og.mission.start_next_stage: the mission leads straight into the next solo
 * stage (the original Deep Sea rule). From the end screen it starts that stage,
 * or closes the end screen when the stage is not unlocked at this difficulty;
 * at a co-op stage end it starts it without the co-op end screen. */
static s32 s_runStartNextStage(const mission_graph_event_t *event)
{
	if (event->kind != MISSION_GRAPH_EVENT_MISSION_CONTINUE) {
		return 0;
	}
	if (event->seam == MISSION_GRAPH_SEAM_ENDSCREEN) {
		endscreenStartNextSoloStage();
		return 1;
	}
	if (event->seam == MISSION_GRAPH_SEAM_COOP_STAGE_END) {
		menuTickCoopStartNextStage();
		return 1;
	}
	return 0;
}

/* og.mission.start_credits: the mission leads to the credits (the original
 * Skedar Ruins rule), from its end screen. */
static s32 s_runStartCredits(const mission_graph_event_t *event)
{
	if (event->kind != MISSION_GRAPH_EVENT_MISSION_CONTINUE || event->seam != MISSION_GRAPH_SEAM_ENDSCREEN) {
		return 0;
	}
	endscreenStartCredits();
	return 1;
}

/* og.progress.unlock_alt_title: completing the mission unlocks the alternate
 * title screen once (the original Skedar Ruins rule). */
static s32 s_runUnlockAltTitle(const mission_graph_event_t *event)
{
	if (event->kind != MISSION_GRAPH_EVENT_MISSION_COMPLETED) {
		return 0;
	}
	endscreenUnlockAltTitle();
	return 1;
}

static const struct {
	const char *kind;
	s32 (*run)(const mission_graph_event_t *event);
} s_MissionGraphModules[] = {
	{ "og.hud.objective_status_message", s_runObjectiveStatusMessage },
	{ "og.mission.start_next_stage", s_runStartNextStage },
	{ "og.mission.start_credits", s_runStartCredits },
	{ "og.progress.unlock_alt_title", s_runUnlockAltTitle },
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
