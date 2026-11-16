/*
 * The game's native modules for mission graphs (decision 2, CORE, 2026-11-15).
 * Each og.* kind a mission graph may wire is one row here; the runner refuses
 * a graph that names any other og.* kind when it compiles.
 */
#include <string.h>

#include "mission_graph_modules.h"
#include "game/endscreen.h"
#include "game/objectives.h"
#include "bss.h"
#include "net/net.h"
#include "scenario_source_runtime.h"
#include "system.h"

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

/* og.mission.end_failed / og.mission.end_completed (mission-flow.md): end the
 * mission now with that outcome, from an objective or phase event. The host
 * decides: refused on a network client, and a forced completion is refused in
 * co-op and counter-op until the stage end carries the outcome (the clients
 * judge it from their cached objective statuses). */
static s32 s_runEndMission(const mission_graph_event_t *event, s32 completed)
{
	if (event->kind != MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED
			&& event->kind != MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED) {
		return 0;
	}
	if (g_NetMode == NETMODE_CLIENT) {
		sysLogPrintf(LOG_WARNING,
			"MISSION.GRAPH: og.mission.end_%s refused on a network client; the host ends the mission",
			completed ? "completed" : "failed");
		return 0;
	}
	if (completed && (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0)) {
		sysLogPrintf(LOG_WARNING,
			"MISSION.GRAPH: og.mission.end_completed refused in co-op and counter-op: the stage end does not carry the outcome yet");
		return 0;
	}
	return scenarioSourceMissionGraphRequestEnd(completed);
}

static s32 s_runEndMissionFailed(const mission_graph_event_t *event)
{
	return s_runEndMission(event, 0);
}

static s32 s_runEndMissionCompleted(const mission_graph_event_t *event)
{
	return s_runEndMission(event, 1);
}

static const struct {
	const char *kind;
	s32 (*run)(const mission_graph_event_t *event);
} s_MissionGraphModules[] = {
	{ "og.hud.objective_status_message", s_runObjectiveStatusMessage },
	{ "og.mission.start_next_stage", s_runStartNextStage },
	{ "og.mission.start_credits", s_runStartCredits },
	{ "og.progress.unlock_alt_title", s_runUnlockAltTitle },
	{ "og.mission.end_failed", s_runEndMissionFailed },
	{ "og.mission.end_completed", s_runEndMissionCompleted },
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
