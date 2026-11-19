/*
 * The game's native modules for mission and level graphs (decision 2, CORE,
 * 2026-11-15; object-events.md slices 3 and 4). Each og.* kind and native
 * gate.* kind a graph may wire is one row here; the runner refuses a graph
 * that names any other og.* or gate.* kind when it compiles.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mission_graph_modules.h"
#include "ai_catalog_refs.h"
#include "bss.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/propobj.h"
#include "game/prop.h"
#include "game/setuputils.h"
#include "game/endscreen.h"
#include "game/objectives.h"
#include "net/net.h"
#include "scenario_source_runtime.h"
#include "system.h"

/* og.hud.objective_status_message: the original "Objective N: Completed",
 * "Incomplete" or "Failed" message, shown to every player
 * (objectivesShowStatusMessage, src/game/objectives.c). */
static s32 s_runObjectiveStatusMessage(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	(void)params;
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
static s32 s_runStartNextStage(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	(void)params;
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
static s32 s_runStartCredits(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	(void)params;
	if (event->kind != MISSION_GRAPH_EVENT_MISSION_CONTINUE || event->seam != MISSION_GRAPH_SEAM_ENDSCREEN) {
		return 0;
	}
	endscreenStartCredits();
	return 1;
}

/* og.progress.unlock_alt_title: completing the mission unlocks the alternate
 * title screen once (the original Skedar Ruins rule). */
static s32 s_runUnlockAltTitle(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	(void)params;
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

static s32 s_runEndMissionFailed(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	(void)params;
	return s_runEndMission(event, 0);
}

static s32 s_runEndMissionCompleted(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	(void)params;
	return s_runEndMission(event, 1);
}

/* ---- object-events.md slice 3: objects ---- */

/* The object a node's "object" parameter names: an objects.json record_id,
 * "setup_0123", the setup command index. NULL when there is none. */
static struct defaultobj *s_paramObject(const mission_graph_params_t *params)
{
	const char *ref = missionGraphParamText(params, "object");
	unsigned index = 0;
	char tail = 0;
	struct defaultobj *obj;

	if (!ref || sscanf(ref, "setup_%u%c", &index, &tail) != 1) {
		return NULL;
	}
	obj = setupGetObjByCmdIndex(index);
	return obj && obj->prop ? obj : NULL;
}

static struct doorobj *s_paramDoor(const mission_graph_params_t *params)
{
	struct defaultobj *obj = s_paramObject(params);

	return obj && obj->prop->type == PROPTYPE_DOOR ? (struct doorobj *)obj : NULL;
}

/* A node's "bits" parameter: the door key flags, 1 to 255. */
static s32 s_paramBits(const mission_graph_params_t *params)
{
	const char *text = missionGraphParamText(params, "bits");
	long bits = text ? strtol(text, NULL, 0) : 0;

	return bits >= 1 && bits <= 255 ? (s32)bits : 0;
}

/* og.door.open / og.door.close: as the AI's open_door and close_door (a lift
 * door calls its lift instead). */
static s32 s_runDoorOpen(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	struct doorobj *door = s_paramDoor(params);

	(void)event;
	if (!door || g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	if (!doorCallLift(door->base.prop, false)) {
		doorsRequestMode(door, DOORMODE_OPENING);
	}
	return 1;
}

static s32 s_runDoorClose(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	struct doorobj *door = s_paramDoor(params);

	(void)event;
	if (!door || g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	doorsRequestMode(door, DOORMODE_CLOSING);
	return 1;
}

/* og.door.lock / og.door.unlock: as the AI's lock_door and unlock_door, on
 * the door's key flags ("bits"). */
static s32 s_runDoorLock(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	struct doorobj *door = s_paramDoor(params);
	const s32 bits = s_paramBits(params);

	(void)event;
	if (!door || !bits || g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	door->keyflags |= (u8)bits;
	return 1;
}

static s32 s_runDoorUnlock(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	struct doorobj *door = s_paramDoor(params);
	const s32 bits = s_paramBits(params);

	(void)event;
	if (!door || !bits || g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	door->keyflags &= (u8)~bits;
	return 1;
}

/* og.pickup.give: gives "object" to the event's player, as the AI's
 * give_object_to_chr gives one to a player. */
static s32 s_runPickupGive(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	struct defaultobj *obj = s_paramObject(params);
	s32 prevplayernum;
	s32 result;

	if (!obj || event->player < 0 || event->player >= PLAYERCOUNT()
			|| g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	/* A successful pickup can clear obj->prop before its tick operation
	 * releases the original prop slot. */
	struct prop *prop = obj->prop;
	prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(event->player);
	if (prop->parent) {
		objDetach(prop);
		objFreeEmbedmentOrProjectile(prop);
		propActivate(prop);
	}
	result = propPickupByPlayer(prop, 0);
	propExecuteTickOperation(prop, result);
	obj->hidden = ((u32)event->player << 28) | (obj->hidden & 0x0fffffff);
	setCurrentPlayerNum(prevplayernum);
	return 1;
}

/* gate.object.kind: the event's object is of "object_kind" (an
 * objects.json kind; the node's own "kind" is gate.object.kind). */
static s32 s_gateObjectKind(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	const char *kind = missionGraphParamText(params, "object_kind");

	return kind && event->object_kind && strcmp(kind, event->object_kind) == 0;
}

/* gate.player.has_item: the event's player carries "item", a weapon or
 * item catalog id. */
static const char kHasItemParam[] = "item";

static s32 s_gatePlayerHasItem(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	const char *item = missionGraphParamText(params, kHasItemParam);
	int64_t weaponnum = 0;
	s32 prevplayernum;
	s32 has;

	if (!item || event->player < 0 || event->player >= PLAYERCOUNT()
			|| !aiCatalogRefResolve(AI_OPERAND_WEAPON, item, &weaponnum)) {
		return 0;
	}
	prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(event->player);
	has = invHasSingleWeaponIncAllGuns((s32)weaponnum) ? 1 : 0;
	setCurrentPlayerNum(prevplayernum);
	return has;
}

/* ---- object-events.md slice 4: the native pickup and door rules ---- */

/* The rule event being decided (NULL outside one). */
static mission_graph_object_rule_t *s_Rule;

mission_graph_object_rule_t *missionGraphModulesRuleBegin(mission_graph_object_rule_t *rule)
{
	mission_graph_object_rule_t *outer = s_Rule;

	s_Rule = rule;
	return outer;
}

void missionGraphModulesRuleEnd(mission_graph_object_rule_t *outer)
{
	s_Rule = outer;
}

/* The rule being decided, when the event is that rule's own event of one of
 * the two kinds (the same kind twice for one); NULL anywhere else. */
static mission_graph_object_rule_t *s_ruleFor(const mission_graph_event_t *event, s32 kind, s32 other)
{
	if (!s_Rule || !s_Rule->prop || (event->kind != kind && event->kind != other)) {
		return NULL;
	}
	return s_Rule;
}

static struct doorobj *s_ruleDoor(mission_graph_object_rule_t *rule)
{
	return rule && rule->user && rule->prop->type == PROPTYPE_DOOR ? rule->prop->door : NULL;
}

/* gate.pickup.native_rules: the original pickup rules (objPickupRulesAllow)
 * for the touched object and the current player. */
static s32 s_gatePickupNativeRules(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	mission_graph_object_rule_t *rule = s_ruleFor(event, MISSION_GRAPH_EVENT_PICKUP_TOUCHED,
		MISSION_GRAPH_EVENT_PICKUP_TOUCHED);

	(void)params;
	return rule && objPickupRulesAllow(rule->prop) ? 1 : 0;
}

/* og.pickup.collect: the player collects the touched object (with its HUD
 * message); the seam returns the tick operation. Once per touch. */
static s32 s_runPickupCollect(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	mission_graph_object_rule_t *rule = s_ruleFor(event, MISSION_GRAPH_EVENT_PICKUP_TOUCHED,
		MISSION_GRAPH_EVENT_PICKUP_TOUCHED);

	(void)params;
	if (!rule || g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	if (!rule->collected) {
		rule->collected = 1;
		rule->tickop = propPickupByPlayer(rule->prop, true);
	}
	return 1;
}

/* gate.door.native_unlocked: doorIsUnlocked for whoever uses or approaches
 * the door (key flags, locked front or back, padlocks). */
static s32 s_gateDoorNativeUnlocked(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	mission_graph_object_rule_t *rule = s_ruleFor(event, MISSION_GRAPH_EVENT_DOOR_USE,
		MISSION_GRAPH_EVENT_DOOR_APPROACH);

	(void)params;
	return s_ruleDoor(rule) && doorIsUnlocked(rule->user, rule->prop) ? 1 : 0;
}

/* og.door.activate: the used door opens or closes for its user (swing
 * direction, doorsActivate). Once per use. */
static s32 s_runDoorActivate(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	mission_graph_object_rule_t *rule = s_ruleFor(event, MISSION_GRAPH_EVENT_DOOR_USE,
		MISSION_GRAPH_EVENT_DOOR_USE);
	struct doorobj *door = s_ruleDoor(rule);

	(void)params;
	if (!door || g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	if (!rule->activated) {
		rule->activated = 1;
		doorsChooseSwingDirection(rule->user, door);
		doorsActivate(rule->prop, true);
	}
	return 1;
}

/* og.door.locked_message: the original refusal of a used door (doorShowLocked:
 * the locked message while it is shut, who tried, the pending activation). */
static s32 s_runDoorLockedMessage(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	mission_graph_object_rule_t *rule = s_ruleFor(event, MISSION_GRAPH_EVENT_DOOR_USE,
		MISSION_GRAPH_EVENT_DOOR_USE);
	struct doorobj *door = s_ruleDoor(rule);

	(void)params;
	if (!door || g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	if (!rule->locked_message) {
		rule->locked_message = 1;
		doorShowLocked(door);
	}
	return 1;
}

/* og.door.activate_automatic: an automatic door the player approaches opens
 * (doorOpenAutomatic). Once per approach. */
static s32 s_runDoorActivateAutomatic(const mission_graph_params_t *params,
	const mission_graph_event_t *event)
{
	mission_graph_object_rule_t *rule = s_ruleFor(event, MISSION_GRAPH_EVENT_DOOR_APPROACH,
		MISSION_GRAPH_EVENT_DOOR_APPROACH);
	struct doorobj *door = s_ruleDoor(rule);

	(void)params;
	if (!door || g_NetMode == NETMODE_CLIENT) {
		return 0;
	}
	if (!rule->activated) {
		rule->activated = 1;
		doorOpenAutomatic(door);
	}
	return 1;
}

static const struct {
	const char *kind;
	s32 (*run)(const mission_graph_params_t *params, const mission_graph_event_t *event);
	const char *asset_param;
	asset_type_e asset_type;
} s_MissionGraphModules[] = {
	{ "og.hud.objective_status_message", s_runObjectiveStatusMessage },
	{ "og.mission.start_next_stage", s_runStartNextStage },
	{ "og.mission.start_credits", s_runStartCredits },
	{ "og.progress.unlock_alt_title", s_runUnlockAltTitle },
	{ "og.mission.end_failed", s_runEndMissionFailed },
	{ "og.mission.end_completed", s_runEndMissionCompleted },
	/* object-events.md slice 3 */
	{ "og.door.open", s_runDoorOpen },
	{ "og.door.close", s_runDoorClose },
	{ "og.door.lock", s_runDoorLock },
	{ "og.door.unlock", s_runDoorUnlock },
	{ "og.pickup.give", s_runPickupGive },
	{ "gate.object.kind", s_gateObjectKind },
	{ "gate.player.has_item", s_gatePlayerHasItem, kHasItemParam, ASSET_WEAPON },
	/* object-events.md slice 4: the native pickup and door rules */
	{ "gate.pickup.native_rules", s_gatePickupNativeRules },
	{ "og.pickup.collect", s_runPickupCollect },
	{ "gate.door.native_unlocked", s_gateDoorNativeUnlocked },
	{ "og.door.activate", s_runDoorActivate },
	{ "og.door.locked_message", s_runDoorLockedMessage },
	{ "og.door.activate_automatic", s_runDoorActivateAutomatic },
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

s32 missionGraphModuleAssetRef(const char *kind, const mission_graph_params_t *params,
	const char **id, asset_type_e *type)
{
	if (!kind || !id || !type) return -1;
	for (size_t i = 0; i < sizeof(s_MissionGraphModules) / sizeof(s_MissionGraphModules[0]); i++) {
		if (strcmp(kind, s_MissionGraphModules[i].kind) != 0) continue;
		const char *key = s_MissionGraphModules[i].asset_param;
		if (!key) return 0;
		*id = missionGraphParamText(params, key);
		*type = s_MissionGraphModules[i].asset_type;
		return *id && **id ? 1 : -1;
	}
	return -1;
}

s32 missionGraphModuleRun(const char *kind, const char *node_id,
	const mission_graph_params_t *params, const mission_graph_event_t *event, void *user)
{
	size_t i;

	(void)node_id;
	(void)user;
	if (!kind || !event) return 0;
	for (i = 0; i < sizeof(s_MissionGraphModules) / sizeof(s_MissionGraphModules[0]); i++) {
		if (strcmp(kind, s_MissionGraphModules[i].kind) == 0) {
			return s_MissionGraphModules[i].run(params, event);
		}
	}
	return 0;
}
