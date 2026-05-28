#include <ultra64.h>
#include "constants.h"
#include "game/chraction.h"
#include "game/debug.h"
#include "game/prop.h"
#include "game/setuputils.h"
#include "game/objectives.h"
#include "game/tex.h"
#include "game/camera.h"
#include "game/hudmsg.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/lv.h"
#include "game/training.h"
#include "game/lang.h"
#include "game/propobj.h"
#include "bss.h"
#include "lib/dma.h"
#include "lib/memp.h"
#include "lib/rng.h"
#include "lib/str.h"
#include "lib/mtx.h"
#include "data.h"
#include "types.h"
#include "platform.h"
#include "system.h"
#include "net/net.h"
#include "net/netmsg.h"
#include "scenario_source_runtime.h"

struct objective *g_Objectives[MAX_OBJECTIVES];
u32 g_ObjectiveStatuses[MAX_OBJECTIVES];
struct tag *g_TagsLinkedList;
struct briefingobj *g_BriefingObjs;
struct criteria_roomentered *g_RoomEnteredCriterias;
struct criteria_throwinroom *g_ThrowInRoomCriterias;
struct criteria_holograph *g_HolographCriterias;
s32 g_NumTags;
struct tag **g_TagPtrs;
u32 var8009d0cc;

s32 g_ObjectiveLastIndex = -1;
bool g_ObjectiveChecksDisabled = false;
bool g_DebugForceCompleteCurrentMissionObjectives = false;

static bool objectiveGraphRequirementUsesObjectState(u8 type);
static void objectiveSeedGraphObjectStates(void);

#if PIRACYCHECKS
u32 xorBaffbeff(u32 value)
{
	return value ^ 0xbaffbeff;
}

u32 xorBabeffff(u32 value)
{
	return value ^ 0xbabeffff;
}

u32 xorBoobless(u32 value)
{
	return value ^ 0xb00b1e55;
}

void func0f095350(u32 arg0, u32 *arg1)
{
	volatile u32 *ptr;
	u32 value;

	__osPiGetAccess();

	ptr = (u32 *)(xorBoobless(0x04600010 ^ 0xb00b1e55) | 0xa0000000);

	value = *ptr;

	while (value & 3) {
		value = *ptr;
	}

	*arg1 = *(u32 *)((uintptr_t)osRomBase | arg0 | 0xa0000000);

	__osPiRelAccess();
}
#endif

void tagsReset(void)
{
	s32 index = 0;
	struct tag *tag = g_TagsLinkedList;

	while (tag) {
		if (tag->tagnum >= index) {
			index = tag->tagnum + 1;
		}

		tag = tag->next;
	}

	g_NumTags = index;

	if (g_NumTags) {
		u32 size = index * sizeof(uintptr_t);
		g_TagPtrs = mempAlloc(ALIGN16(size), MEMPOOL_STAGE);

		for (index = 0; index < g_NumTags; index++) {
			g_TagPtrs[index] = NULL;
		}
	}

	tag = g_TagsLinkedList;

	while (tag) {
		g_TagPtrs[tag->tagnum] = tag;
		tag = tag->next;
	}

	objectiveSeedGraphObjectStates();

#if PIRACYCHECKS
	{
		// mtxGetObfuscatedRomBase() returns the value at ROM offset 0xa5c.
		// This value should be 0x1740fff9.
		u32 dummy = xorBaffbeff(0xb0000a5c ^ 0xbaffbeff);
		u32 expected = xorBabeffff(0x1740fff9 ^ 0xbabeffff);

		if (mtxGetObfuscatedRomBase() != expected) {
			// Read 4KB from a random ROM location within 128KB from the start of
			// the ROM, and write it to a random memory location between 0x80010000
			// and 0x80030ff8. This will corrupt instructions in the lib segment.
			dmaExec((u8 *)((rngRandom() & 0x1fff8) + 0x80010000), rngRandom() & 0x1fffe, 0x1000);
		}
	}
#endif
}

struct tag *tagFindById(s32 tag_id)
{
	struct tag *tag = NULL;

	if (tag_id >= 0 && tag_id < g_NumTags) {
		tag = g_TagPtrs[tag_id];
	}

	return tag;
}

s32 objGetTagNum(struct defaultobj *obj)
{
	struct tag *tag = g_TagsLinkedList;

	if (obj && (obj->hidden & OBJHFLAG_TAGGED)) {
		while (tag) {
			if (obj == tag->obj) {
				return tag->tagnum;
			}

			tag = tag->next;
		}
	}

	return -1;
}

struct defaultobj *objFindByTagId(s32 tag_id)
{
	struct tag *tag = tagFindById(tag_id);
	struct defaultobj *obj = NULL;

	if (tag) {
		obj = tag->obj;
	}

	if (obj && (obj->hidden & OBJHFLAG_TAGGED) == 0) {
		obj = NULL;
	}

	return obj;
}

static s32 objectivePropHeldByMissionPlayer(struct prop *prop)
{
	s32 held = false;
	s32 prevplayernum;
	s32 i;

	if (!prop) {
		return false;
	}

	prevplayernum = g_Vars.currentplayernum;

	for (i = 0; i < PLAYERCOUNT(); i++) {
		if (g_Vars.players[i] &&
				(g_Vars.players[i] == g_Vars.bond ||
				g_Vars.players[i] == g_Vars.coop)) {
			setCurrentPlayerNum(i);

			if (invHasProp(prop)) {
				held = true;
				break;
			}
		}
	}

	setCurrentPlayerNum(prevplayernum);
	return held;
}

void objectiveRecordObjectState(struct defaultobj *obj)
{
	s32 tag_id;
	s32 present;
	s32 healthy;
	s32 held_by_player;

	if (!scenarioSourceObjectiveGraphIsActive() || !obj) {
		return;
	}

	tag_id = objGetTagNum(obj);
	if (tag_id < 0) {
		return;
	}

	present = obj->prop != NULL;
	healthy = present && objIsHealthy(obj);
	held_by_player = present && objectivePropHeldByMissionPlayer(obj->prop);

	scenarioSourceObjectiveGraphRecordObjectState(tag_id,
		present, healthy, held_by_player);
}

void objectiveRecordPropState(struct prop *prop)
{
	if (prop && prop->obj) {
		objectiveRecordObjectState(prop->obj);
	}
}

static void objectiveRecordMissingObjectState(s32 tag_id)
{
	if (scenarioSourceObjectiveGraphIsActive() && tag_id >= 0) {
		scenarioSourceObjectiveGraphRecordObjectState(tag_id,
			false, false, false);
	}
}

static void objectiveSeedGraphObjectStates(void)
{
	s32 index;

	if (!scenarioSourceObjectiveGraphIsActive()) {
		return;
	}

	for (index = 0; index < ARRAYCOUNT(g_Objectives); index++) {
		s32 criteria_count =
			scenarioSourceObjectiveGraphGetCriterionCount(index);
		s32 i;

		if (criteria_count < 0) {
			continue;
		}

		for (i = 0; i < criteria_count; i++) {
			scenario_source_objective_operand_t operand;
			struct defaultobj *obj;

			if (!scenarioSourceObjectiveGraphGetCriterionOperand(
					index, i, &operand) ||
					!objectiveGraphRequirementUsesObjectState(
					operand.type)) {
				continue;
			}

			obj = objFindByTagId(operand.tag_id);
			if (obj) {
				objectiveRecordObjectState(obj);
			} else {
				objectiveRecordMissingObjectState(operand.tag_id);
			}
		}
	}
}

s32 objectiveGetCount(void)
{
	return g_ObjectiveLastIndex + 1;
}

char *objectiveGetText(s32 index)
{
	if (index < 10 && g_Objectives[index]) {
		return langGet(g_Objectives[index]->text);
	}

	return NULL;
}

u32 objectiveGetDifficultyBits(s32 index)
{
	if (index < 10 && g_Objectives[index]) {
		return g_Objectives[index]->difficulties;
	}

	return DIFFBIT_A | DIFFBIT_SA | DIFFBIT_PA | DIFFBIT_PD;
}

static s32 objectiveMergeRequirementStatus(s32 objstatus, s32 reqstatus)
{
	if (objstatus == OBJECTIVE_COMPLETE) {
		if (reqstatus != OBJECTIVE_COMPLETE) {
			return reqstatus;
		}
	} else if (objstatus == OBJECTIVE_INCOMPLETE) {
		if (reqstatus == OBJECTIVE_FAILED) {
			return reqstatus;
		}
	}

	return objstatus;
}

static s32 objectiveEvaluateRequirement(u8 type, u32 *cmd)
{
	s32 reqstatus = OBJECTIVE_COMPLETE;

	switch (type) {
	case OBJECTIVETYPE_DESTROYOBJ:
		{
			struct defaultobj *obj = objFindByTagId(cmd[1]);
			if (obj && obj->prop && objIsHealthy(obj)) {
				reqstatus = OBJECTIVE_INCOMPLETE;
			}
		}
		break;
	case OBJECTIVETYPE_COMPFLAGS:
		if (!chrHasStageFlag(NULL, cmd[1])) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJECTIVETYPE_FAILFLAGS:
		if (chrHasStageFlag(NULL, cmd[1])) {
			reqstatus = OBJECTIVE_FAILED;
		}
		break;
	case OBJECTIVETYPE_COLLECTOBJ:
		{
			struct defaultobj *obj = objFindByTagId(cmd[1]);
			s32 prevplayernum;
			s32 collected = false;
			s32 i;

			if (!obj || !obj->prop || !objIsHealthy(obj)) {
				reqstatus = OBJECTIVE_FAILED;
			} else {
				prevplayernum = g_Vars.currentplayernum;

				for (i = 0; i < PLAYERCOUNT(); i++) {
					if (g_Vars.players[i] == g_Vars.bond || g_Vars.players[i] == g_Vars.coop) {
						setCurrentPlayerNum(i);

						if (invHasProp(obj->prop)) {
							collected = true;
							break;
						}
					}
				}

				setCurrentPlayerNum(prevplayernum);

				if (!collected) {
					reqstatus = OBJECTIVE_INCOMPLETE;
				}
			}
		}
		break;
	case OBJECTIVETYPE_THROWOBJ:
		{
			struct defaultobj *obj = objFindByTagId(cmd[1]);

			if (obj && obj->prop) {
				s32 i;
				s32 prevplayernum = g_Vars.currentplayernum;

				for (i = 0; i < PLAYERCOUNT(); i++) {
					if (g_Vars.players[i] == g_Vars.bond || g_Vars.players[i] == g_Vars.coop) {
						setCurrentPlayerNum(i);

						if (invHasProp(obj->prop)) {
							reqstatus = OBJECTIVE_INCOMPLETE;
							break;
						}
					}
				}

				setCurrentPlayerNum(prevplayernum);
			}
		}
		break;
	case OBJECTIVETYPE_HOLOGRAPH:
		{
			struct defaultobj *obj = objFindByTagId(cmd[1]);

			if (cmd[2] == 0) {
				if (!obj || !obj->prop || !objIsHealthy(obj)) {
					reqstatus = OBJECTIVE_FAILED;
				} else {
					reqstatus = OBJECTIVE_INCOMPLETE;
				}
			}
		}
		break;
	case OBJECTIVETYPE_ENTERROOM:
		if (cmd[2] == 0) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJECTIVETYPE_THROWINROOM:
		if (cmd[3] == 0) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJTYPE_BEGINOBJECTIVE:
	case OBJTYPE_ENDOBJECTIVE:
		break;
	}

	return reqstatus;
}

static bool objectiveGraphRequirementUsesRuntimeStatus(u8 type)
{
	return type == OBJECTIVETYPE_HOLOGRAPH ||
		type == OBJECTIVETYPE_ENTERROOM ||
		type == OBJECTIVETYPE_THROWINROOM;
}

static bool objectiveGraphRequirementUsesMissionFlags(u8 type)
{
	return type == OBJECTIVETYPE_COMPFLAGS ||
		type == OBJECTIVETYPE_FAILFLAGS;
}

static bool objectiveGraphRequirementUsesObjectState(u8 type)
{
	return type == OBJECTIVETYPE_DESTROYOBJ ||
		type == OBJECTIVETYPE_COLLECTOBJ ||
		type == OBJECTIVETYPE_THROWOBJ ||
		type == OBJECTIVETYPE_HOLOGRAPH;
}

static s32 objectiveEvaluateGraphRequirement(
	const scenario_source_objective_operand_t *operand)
{
	s32 reqstatus = OBJECTIVE_COMPLETE;
	s32 has_stage_flag = false;

	if (!operand) {
		return OBJECTIVE_INCOMPLETE;
	}

	switch (operand->type) {
	case OBJECTIVETYPE_DESTROYOBJ:
		if (operand->runtime_object_present &&
				operand->runtime_object_healthy) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJECTIVETYPE_COMPFLAGS:
		scenarioSourceObjectiveGraphHasStageFlag(
			operand->stage_flag_mask, &has_stage_flag);
		if (!has_stage_flag) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJECTIVETYPE_FAILFLAGS:
		scenarioSourceObjectiveGraphHasStageFlag(
			operand->stage_flag_mask, &has_stage_flag);
		if (has_stage_flag) {
			reqstatus = OBJECTIVE_FAILED;
		}
		break;
	case OBJECTIVETYPE_COLLECTOBJ:
		if (!operand->runtime_object_present ||
				!operand->runtime_object_healthy) {
			reqstatus = OBJECTIVE_FAILED;
		} else if (!operand->runtime_object_held_by_player) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJECTIVETYPE_THROWOBJ:
		if (operand->runtime_object_present &&
				operand->runtime_object_held_by_player) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJECTIVETYPE_HOLOGRAPH:
		if (operand->runtime_status_valid &&
				operand->runtime_status ==
				OBJECTIVE_INCOMPLETE) {
			if (!operand->runtime_object_present ||
					!operand->runtime_object_healthy) {
				reqstatus = OBJECTIVE_FAILED;
			} else {
				reqstatus = OBJECTIVE_INCOMPLETE;
			}
		}
		break;
	case OBJECTIVETYPE_ENTERROOM:
		if (operand->runtime_status_valid &&
				operand->runtime_status == OBJECTIVE_INCOMPLETE) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJECTIVETYPE_THROWINROOM:
		if (operand->runtime_status_valid &&
				operand->runtime_status == OBJECTIVE_INCOMPLETE) {
			reqstatus = OBJECTIVE_INCOMPLETE;
		}
		break;
	case OBJTYPE_BEGINOBJECTIVE:
	case OBJTYPE_ENDOBJECTIVE:
		break;
	}

	return reqstatus;
}

static bool objectiveCheckGraphSource(s32 index, s32 *out_status)
{
	u32 *cmd;
	s32 objstatus = OBJECTIVE_COMPLETE;
	s32 criteria_count;
	s32 i;

	if (!out_status || !scenarioSourceObjectiveGraphIsActive()) {
		return false;
	}
	if (index < 0 || index >= ARRAYCOUNT(g_Objectives)
			|| g_Objectives[index] == NULL) {
		scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
			"evaluate", "missing runtime objective");
		return false;
	}

	criteria_count = scenarioSourceObjectiveGraphGetCriterionCount(index);
	if (criteria_count < 0) {
		scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
			"evaluate", "missing objective source node");
		return false;
	}

	cmd = (u32 *)g_Objectives[index];
	if ((u8)PD_BE32(cmd[0]) != OBJTYPE_BEGINOBJECTIVE) {
		scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
			"evaluate", "objective command stream missing begin node");
		return false;
	}
	cmd = cmd + setupGetCmdLength(cmd);

	for (i = 0; i < criteria_count; i++) {
		u8 expected_type;
		u8 runtime_type = (u8)PD_BE32(cmd[0]);
		scenario_source_objective_operand_t operand;

		if (!scenarioSourceObjectiveGraphGetCriterionType(index, i,
				&expected_type)) {
			scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
				"evaluate", "missing criteria source node");
			return false;
		}
		if (runtime_type == OBJTYPE_ENDOBJECTIVE) {
			scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
				"evaluate", "runtime has fewer criteria than graph source");
			return false;
		}
		if (runtime_type != expected_type) {
			scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
				"evaluate", "criteria type differs from graph source");
			return false;
		}
		if (!scenarioSourceObjectiveGraphGetCriterionOperand(index, i,
				&operand)) {
			scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
				"evaluate", "missing criteria graph operand");
			return false;
		}
		if (objectiveGraphRequirementUsesRuntimeStatus(operand.type) &&
				!operand.runtime_status_valid) {
			scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
				"evaluate", "missing criteria graph runtime status");
			return false;
		}
		if (objectiveGraphRequirementUsesMissionFlags(operand.type) &&
				!scenarioSourceObjectiveGraphHasStageFlag(
				operand.stage_flag_mask, NULL)) {
			scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
				"evaluate", "missing graph mission flag state");
			return false;
		}
		if (objectiveGraphRequirementUsesObjectState(operand.type) &&
				!operand.runtime_object_state_valid) {
			scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
				"evaluate", "missing graph object state");
			return false;
		}

		objstatus = objectiveMergeRequirementStatus(objstatus,
			objectiveEvaluateGraphRequirement(&operand));
		cmd = cmd + setupGetCmdLength(cmd);
	}

	if ((u8)PD_BE32(cmd[0]) != OBJTYPE_ENDOBJECTIVE) {
		scenarioSourceObjectiveGraphReportRuntimeMismatch(index,
			"evaluate", "runtime has more criteria than graph source");
		return false;
	}

	if (!scenarioSourceObjectiveGraphRecordEvaluate(index,
			criteria_count)) {
		return false;
	}

	*out_status = objstatus;
	return true;
}

/**
 * Check if an objective is complete.
 *
 * It starts be setting the objective's status to complete, then iterates each
 * requirement in the objective to decide whether to change it to incomplete or
 * failed.
 */
s32 objectiveCheck(s32 index)
{
	u32 stack[5];
	s32 objstatus = OBJECTIVE_COMPLETE;

	if (g_DebugForceCompleteCurrentMissionObjectives) {
		return OBJECTIVE_COMPLETE;
	}

	if (index < ARRAYCOUNT(g_Objectives)) {
		if (g_Objectives[index] == NULL) {
			objstatus = g_ObjectiveStatuses[index];
		} else if (objectiveCheckGraphSource(index, &objstatus)) {
			/* Graph-driven path used. */
		} else {
			// Note: This is setting the cmd pointer to the start of the
			// beginobjective macro in the stage's setup file. The first
			// iteration of the while loop below will skip past it.
			u32 *cmd = (u32 *)g_Objectives[index];

			while ((u8)PD_BE32(cmd[0]) != OBJTYPE_ENDOBJECTIVE) {
				objstatus = objectiveMergeRequirementStatus(objstatus,
					objectiveEvaluateRequirement((u8)PD_BE32(cmd[0]), cmd));

				cmd = cmd + setupGetCmdLength(cmd);
			}
		}
	}

	if (debugForceAllObjectivesComplete()) {
		objstatus = OBJECTIVE_COMPLETE;
	}

	return scenarioSourceObjectiveGraphRecordCheck(index, objstatus);
}

s32 objectivesDebugCompleteCurrentMission(void)
{
	s32 i;
	s32 completed = 0;

	g_DebugForceCompleteCurrentMissionObjectives = true;

	for (i = 0; i < objectiveGetCount(); i++) {
		if (objectiveGetDifficultyBits(i) & (1 << lvGetDifficulty())) {
			g_ObjectiveStatuses[i] = OBJECTIVE_COMPLETE;
			completed++;
		}
	}

	objectivesCheckAll();

	return completed;
}

bool objectiveIsAllComplete(void)
{
	s32 i;

	for (i = 0; i < objectiveGetCount(); i++) {
		u32 diffbits = objectiveGetDifficultyBits(i);

		if (1 << lvGetDifficulty() & diffbits) {
			s32 status;
			// In network co-op, client uses cached statuses from SVC_OBJ_STATUS
			// instead of re-evaluating criteria (which may depend on server state).
			if (g_NetMode == NETMODE_CLIENT
					&& (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI)) {
				status = g_ObjectiveStatuses[i];
				sysLogPrintf(LOG_NOTE, "NET: objectiveIsAllComplete using cached status[%d]=%u (client)", i, status);
			} else
			{
				status = objectiveCheck(i);
			}

			if (status != OBJECTIVE_COMPLETE) {
				scenarioSourceMissionGraphRecordPhase(
					status == OBJECTIVE_FAILED ? "failed" : "active",
					"objectiveIsAllComplete");
				return false;
			}
		}
	}

	scenarioSourceMissionGraphRecordPhase("complete",
		"objectiveIsAllComplete");
	return true;
}

void objectivesDisableChecking(void)
{
	g_ObjectiveChecksDisabled = true;
}

#if VERSION >= VERSION_NTSC_1_0
void objectivesShowHudmsg(char *buffer, s32 hudmsgtype)
{
	s32 prevplayernum = g_Vars.currentplayernum;
	s32 i;

	for (i = 0; i < MAX_PLAYERS; i++) {
		if (!g_Vars.players[i]) {
			continue;
		}
		setCurrentPlayerNum(i);

		if (g_Vars.currentplayer == g_Vars.bond || g_Vars.currentplayer == g_Vars.coop) {
			hudmsgCreateWithFlags(buffer, hudmsgtype, HUDMSGFLAG_DELAY | HUDMSGFLAG_ALLOWDUPES);
		}
	}

	setCurrentPlayerNum(prevplayernum);
}
#endif

void objectivesCheckAll(void)
{
	s32 availableindex = 0;
	s32 any_active_objective = 0;
	s32 any_failed_objective = 0;
	s32 all_active_objectives_complete = 1;
	s32 i;
	char buffer[50] = "";

	// In network co-op, objective evaluation is server-authoritative.
	// Clients receive status updates via SVC_OBJ_STATUS messages.
	if (g_NetMode == NETMODE_CLIENT
			&& (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI)) {
		sysLogPrintf(LOG_NOTE, "NET: objectivesCheckAll skipped on client (server-authoritative)");
		return;
	}

	if (!g_ObjectiveChecksDisabled) {
		for (i = 0; i <= g_ObjectiveLastIndex; i++) {
			// Safety: clamp to MAX_OBJECTIVES to prevent out-of-bounds access
			if (i >= MAX_OBJECTIVES) break;
			s32 status = objectiveCheck(i);

			if (g_ObjectiveStatuses[i] != status) {
				g_ObjectiveStatuses[i] = status;

				// In co-op mode, broadcast objective status changes to clients
				if (g_NetMode == NETMODE_SERVER
						&& (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI)) {
					netmsgSvcObjStatusWrite(&g_NetMsgRel, (u8)i, (u8)status);
				}

				if (objectiveGetDifficultyBits(i) & (1 << lvGetDifficulty())) {
#if VERSION >= VERSION_JPN_FINAL
					u8 jpnstr[] = {0, 0, 0};
					jpnstr[0] = 0x80;
					jpnstr[1] = 0x80 | (0x11 + availableindex);
					snprintf(buffer, sizeof(buffer), "%s %s: ", langGet(L_MISC_044), jpnstr); // "Objective"
#else
					snprintf(buffer, sizeof(buffer), "%s %d: ", langGet(L_MISC_044), availableindex + 1); // "Objective"
#endif

#if VERSION >= VERSION_NTSC_1_0
					// NTSC 1.0 and above shows objective messages to everyone,
					// while beta only shows them to the current player.
					if (status == OBJECTIVE_COMPLETE) {
						strcat(buffer, langGet(L_MISC_045)); // "Completed"
						objectivesShowHudmsg(buffer, HUDMSGTYPE_OBJECTIVECOMPLETE);
					} else if (status == OBJECTIVE_INCOMPLETE) {
						strcat(buffer, langGet(L_MISC_046)); // "Incomplete"
						objectivesShowHudmsg(buffer, HUDMSGTYPE_OBJECTIVECOMPLETE);
					} else if (status == OBJECTIVE_FAILED) {
						strcat(buffer, langGet(L_MISC_047)); // "Failed"
						objectivesShowHudmsg(buffer, HUDMSGTYPE_OBJECTIVEFAILED);
					}
#else
					if (status == OBJECTIVE_COMPLETE) {
						strcat(buffer, langGet(L_MISC_045)); // "Completed"
						hudmsgCreateWithFlags(buffer, HUDMSGTYPE_OBJECTIVECOMPLETE, HUDMSGFLAG_ALLOWDUPES);
					} else if (status == OBJECTIVE_INCOMPLETE) {
						strcat(buffer, langGet(L_MISC_046)); // "Incomplete"
						hudmsgCreateWithFlags(buffer, HUDMSGTYPE_OBJECTIVECOMPLETE, HUDMSGFLAG_ALLOWDUPES);
					} else if (status == OBJECTIVE_FAILED) {
						strcat(buffer, langGet(L_MISC_047)); // "Failed"
						hudmsgCreateWithFlags(buffer, HUDMSGTYPE_OBJECTIVEFAILED, HUDMSGFLAG_ALLOWDUPES);
					}
#endif
				}
			}

			if (objectiveGetDifficultyBits(i) & (1 << lvGetDifficulty())) {
				any_active_objective = 1;
				if (status == OBJECTIVE_FAILED) {
					any_failed_objective = 1;
				}
				if (status != OBJECTIVE_COMPLETE) {
					all_active_objectives_complete = 0;
				}
				availableindex++;
			}
		}

		if (any_active_objective) {
			scenarioSourceMissionGraphRecordPhase(
				any_failed_objective ? "failed" :
					(all_active_objectives_complete ? "complete" :
						"active"),
				"objectives.check");
		}
	}
}

void objectiveCheckRoomEntered(s32 currentroom)
{
	struct criteria_roomentered *criteria = g_RoomEnteredCriterias;

	while (criteria) {
		if (criteria->status == OBJECTIVE_INCOMPLETE) {
			s32 graph_matched = 0;
			s32 graph_checked =
				scenarioSourceLevelGraphCheckPadRoom(criteria->pad,
					currentroom, "objective.enter_room",
					&graph_matched);
			s32 legacy_room = graph_checked ? -1 :
				chrGetPadRoom(NULL, criteria->pad);

			if (graph_checked ? graph_matched :
					(legacy_room >= 0 && legacy_room == currentroom)) {
				criteria->status = OBJECTIVE_COMPLETE;
				scenarioSourceObjectiveGraphRecordCriterionStatus(
					criteria, criteria->status);
			}
		}

		criteria = criteria->next;
	}
}

void objectiveCheckThrowInRoom(s32 arg0, RoomNum *inrooms)
{
	struct criteria_throwinroom *criteria = g_ThrowInRoomCriterias;

	while (criteria) {
		if (criteria->status == OBJECTIVE_INCOMPLETE && criteria->unk04 == arg0) {
			s32 graph_checked = 0;
			s32 graph_matched = 0;
			s32 complete = 0;

			if (inrooms) {
				s32 i;

				for (i = 0; inrooms[i] != -1; i++) {
					s32 room_matched = 0;

					if (scenarioSourceLevelGraphCheckPadRoom(
							criteria->pad, inrooms[i],
							"objective.throw_in_room",
							&room_matched)) {
						graph_checked = 1;
						if (room_matched) {
							graph_matched = 1;
							break;
						}
					}
				}
			}

			if (graph_checked) {
				complete = graph_matched;
			} else {
				s32 room = chrGetPadRoom(NULL, criteria->pad);

				if (room >= 0) {
					RoomNum requirerooms[2];
					requirerooms[0] = room;
					requirerooms[1] = -1;
					complete = arrayIntersects(requirerooms, inrooms);
				}
			}

			if (complete) {
				criteria->status = OBJECTIVE_COMPLETE;
				scenarioSourceObjectiveGraphRecordCriterionStatus(
					criteria, criteria->status);
			}
		}

		criteria = criteria->next;
	}
}

void objectiveCheckHolograph(f32 maxdist)
{
	struct criteria_holograph *criteria = g_HolographCriterias;

	while (criteria) {
		if (g_Vars.stagenum == STAGE_CITRAINING) {
			criteria->status = OBJECTIVE_INCOMPLETE;
			scenarioSourceObjectiveGraphRecordCriterionStatus(
				criteria, criteria->status);
		}

		if (criteria->status == OBJECTIVE_INCOMPLETE) {
			struct defaultobj *obj = objFindByTagId(criteria->obj);

			if (obj && obj->prop
					&& (obj->prop->flags & PROPFLAG_ONTHISSCREENTHISTICK)
					&& obj->prop->z >= 0
					&& objIsHealthy(obj)) {
				struct coord sp9c;
				f32 sp94[2];
				f32 sp8c[2];
				f32 dist = -1;

				if (maxdist != 0.0f) {
					f32 xdiff = obj->prop->pos.x - g_Vars.currentplayer->cam_pos.x;
					f32 zdiff = obj->prop->pos.z - g_Vars.currentplayer->cam_pos.z;
					dist = xdiff * xdiff + zdiff * zdiff;
					maxdist = maxdist * maxdist;
				}

				if (dist < maxdist && func0f0899dc(obj->prop, &sp9c, sp94, sp8c)) {
					f32 sp78[2];
					f32 sp70[2];
					func0f06803c(&sp9c, sp94, sp8c, sp78, sp70);

					if (sp78[0] > camGetScreenLeft()
							&& sp78[0] < camGetScreenLeft() + camGetScreenWidth()
							&& sp70[0] > camGetScreenLeft()
							&& sp70[0] < camGetScreenLeft() + camGetScreenWidth()
							&& sp78[1] > camGetScreenTop()
							&& sp78[1] < camGetScreenTop() + camGetScreenHeight()
							&& sp70[1] > camGetScreenTop()
							&& sp70[1] < camGetScreenTop() + camGetScreenHeight()) {
						criteria->status = OBJECTIVE_COMPLETE;
						scenarioSourceObjectiveGraphRecordCriterionStatus(
							criteria, criteria->status);

						if (g_Vars.stagenum == STAGE_CITRAINING) {
							struct trainingdata *data = dtGetData();
							data->holographedpc = true;
						}
					}
				}
			}
		}

		criteria = criteria->next;
	}
}
