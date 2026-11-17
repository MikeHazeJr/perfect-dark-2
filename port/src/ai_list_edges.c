/*
 * ai_list_edges.c -- the control flow of one AI list as edges (see
 * ai_list_edges.h; context/designs/modding/ai-list-graphs.md, slice 1).
 */

#include "ai_list_edges.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct ai_list_branch_op {
	uint16_t opcode;
	uint8_t label_byte; /* the label is cmd[label_byte] */
} ai_list_branch_op_t;

/*
 * The branching commands, in opcode order: every handler g_CommandPointers
 * (src/game/chrai.c) names whose body in src/game/chraicommands.c or
 * src/game/mplayer/mpaicommands.c calls
 * chraiGoToLabel(g_Vars.ailist, g_Vars.aioffset, cmd[n]) -- the if_*
 * conditions (aiIfBotRespawning among them) and the commands that act and
 * then branch (try_*, find_cover*, spawn_chr_*, detect_enemy*, ...) -- with
 * go_to_next (0x0000) left to the interpreter's own rules. The label is the
 * command's last byte except for if_chr_teleport_full_white (0x01c0), which
 * reads it from cmd[2]. pd-tests re-derives this table from those files and
 * compares the copy in tools/asset_archive_conformance.py with it; a new
 * branching handler is added here and there.
 */
static const ai_list_branch_op_t s_AiListBranchOps[] = {
	{ 0x000c, 2 }, /* aiIfIdle */
	{ 0x000f, 2 }, /* aiTrySidestep */
	{ 0x0010, 2 }, /* aiTryJumpOut */
	{ 0x0011, 2 }, /* aiTryRunSideways */
	{ 0x0012, 2 }, /* aiTryAttackWalk */
	{ 0x0013, 2 }, /* aiTryAttackRun */
	{ 0x0014, 2 }, /* aiTryAttackRoll */
	{ 0x0015, 6 }, /* aiTryAttackStand */
	{ 0x0016, 6 }, /* aiTryAttackKneel */
	{ 0x0017, 6 }, /* aiTryModifyAttack */
	{ 0x0018, 6 }, /* aiFaceEntity */
	{ 0x001b, 6 }, /* aiConsiderGrenadeThrow */
	{ 0x001c, 5 }, /* aiDropItem */
	{ 0x0023, 2 }, /* aiIfPatrolling */
	{ 0x0027, 4 }, /* aiTryStartAlarm */
	{ 0x002a, 2 }, /* aiTryRunFromTarget */
	{ 0x002b, 2 }, /* aiTryJogToTargetProp */
	{ 0x002c, 2 }, /* aiTryWalkToTargetProp */
	{ 0x002d, 2 }, /* aiTryRunToTargetProp */
	{ 0x002e, 2 }, /* aiTryGoToCoverProp */
	{ 0x002f, 3 }, /* aiTryJogToChr */
	{ 0x0030, 3 }, /* aiTryWalkToChr */
	{ 0x0031, 3 }, /* aiTryRunToChr */
	{ 0x0032, 2 }, /* aiIfStopped */
	{ 0x0033, 3 }, /* aiIfChrDead */
	{ 0x0034, 3 }, /* aiIfChrDeathAnimationFinished */
	{ 0x0035, 2 }, /* aiIfCanSeeTarget */
	{ 0x0037, 3 }, /* aiIfRandomLessThan */
	{ 0x0038, 3 }, /* aiIfRandomGreaterThan */
	{ 0x0039, 2 }, /* aiIfCanHearAlarm */
	{ 0x003a, 2 }, /* aiIfAlarmActive */
	{ 0x003b, 2 }, /* aiIfGasActive */
	{ 0x003c, 2 }, /* aiIfHearsTarget */
	{ 0x003d, 3 }, /* aiIfSawInjury */
	{ 0x003e, 3 }, /* aiIfSawDeath */
	{ 0x003f, 2 }, /* aiIfLosToTarget */
	{ 0x0040, 6 }, /* aiIfTargetNearlyInSight */
	{ 0x0041, 6 }, /* aiIfNearlyInTargetsSight */
	{ 0x0042, 2 }, /* aiSetPadPresetToPadOnRouteToTarget */
	{ 0x0043, 2 }, /* aiIfSawTargetRecently */
	{ 0x0044, 2 }, /* aiIfHeardTargetRecently */
	{ 0x0045, 3 }, /* aiIfLosToChr */
	{ 0x0046, 2 }, /* aiIfNeverBeenOnScreen */
	{ 0x0047, 2 }, /* aiIfOnScreen */
	{ 0x0048, 3 }, /* aiIfChrInOnScreenRoom */
	{ 0x0049, 4 }, /* aiIfRoomIsOnScreen */
	{ 0x004a, 2 }, /* aiIfTargetAimingAtMe */
	{ 0x004b, 2 }, /* aiIfNearMiss */
	{ 0x004c, 2 }, /* aiIfSeesSuspiciousItem */
	{ 0x004d, 3 }, /* aiIfTargetInFovLeft */
	{ 0x004e, 5 }, /* aiIfCheckFovWithTarget */
	{ 0x004f, 3 }, /* aiIfTargetOutOfFovLeft */
	{ 0x0050, 3 }, /* aiIfTargetInFov */
	{ 0x0051, 3 }, /* aiIfTargetOutOfFov */
	{ 0x0052, 4 }, /* aiIfDistanceToTargetLessThan */
	{ 0x0053, 4 }, /* aiIfDistanceToTargetGreaterThan */
	{ 0x0054, 7 }, /* aiIfChrDistanceToPadLessThan */
	{ 0x0055, 7 }, /* aiIfChrDistanceToPadGreaterThan */
	{ 0x0056, 5 }, /* aiIfDistanceToChrLessThan */
	{ 0x0057, 5 }, /* aiIfDistanceToChrGreaterThan */
	{ 0x0058, 4 }, /* ai0058 */
	{ 0x0059, 6 }, /* aiIfDistanceFromTargetToPadLessThan */
	{ 0x005a, 6 }, /* aiIfDistanceFromTargetToPadGreaterThan */
	{ 0x005b, 6 }, /* aiIfChrInRoom */
	{ 0x005c, 4 }, /* aiIfTargetInRoom */
	{ 0x005d, 4 }, /* aiIfChrHasObject */
	{ 0x005e, 3 }, /* aiIfWeaponThrown */
	{ 0x005f, 4 }, /* aiIfWeaponThrownOnObject */
	{ 0x0060, 4 }, /* aiIfChrHasWeaponEquipped */
	{ 0x0061, 4 }, /* aiIfGunUnclaimed */
	{ 0x0062, 3 }, /* aiIfObjectHealthy */
	{ 0x0063, 4 }, /* aiIfChrActivatedObject */
	{ 0x006e, 4 }, /* aiIfDoorState */
	{ 0x006f, 3 }, /* aiIfObjectIsDoor */
	{ 0x0072, 4 }, /* aiIfDoorLocked */
	{ 0x0073, 3 }, /* aiIfObjectiveComplete */
	{ 0x0074, 3 }, /* aiIfObjectiveFailed */
	{ 0x0075, 3 }, /* ai0075 */
	{ 0x0076, 3 }, /* aiSetPadPresetToTargetQuadrant */
	{ 0x0077, 3 }, /* aiIfDifficultyLessThan */
	{ 0x0078, 3 }, /* aiIfDifficultyGreaterThan */
	{ 0x0079, 4 }, /* aiIfStageTimerLessThan */
	{ 0x007a, 4 }, /* aiIfStageTimerGreaterThan */
	{ 0x007b, 3 }, /* aiIfStageIdLessThan */
	{ 0x007c, 3 }, /* aiIfStageIdGreaterThan */
	{ 0x007d, 3 }, /* aiIfNumArghsLessThan */
	{ 0x007e, 3 }, /* aiIfNumArghsGreaterThan */
	{ 0x007f, 3 }, /* aiIfNumCloseArghsLessThan */
	{ 0x0080, 3 }, /* aiIfNumCloseArghsGreaterThan */
	{ 0x0081, 4 }, /* aiIfChrHealthGreaterThan */
	{ 0x0082, 4 }, /* aiIfChrHealthLessThan */
	{ 0x0083, 3 }, /* aiIfInjured */
	{ 0x0088, 3 }, /* aiIfMoraleLessThan */
	{ 0x0089, 2 }, /* aiIfMoraleLessThanRandom */
	{ 0x008e, 4 }, /* aiIfAlertness */
	{ 0x008f, 4 }, /* aiIfChrAlertnessLessThan */
	{ 0x0090, 2 }, /* aiIfAlertnessLessThanRandom */
	{ 0x009d, 8 }, /* aiIfHasFlag */
	{ 0x00a0, 8 }, /* aiIfChrHasFlag */
	{ 0x00a3, 7 }, /* aiIfStageFlagEq */
	{ 0x00a6, 6 }, /* aiIfHasChrflag */
	{ 0x00a9, 7 }, /* aiIfChrHasChrflag */
	{ 0x00ac, 7 }, /* aiIfObjHasFlag */
	{ 0x00af, 7 }, /* aiIfObjHasFlag2 */
	{ 0x00ba, 2 }, /* aiIfTimerStopped */
	{ 0x00bb, 2 }, /* aiIfTimerGreaterThanRandom */
	{ 0x00bc, 5 }, /* aiIfTimerLessThan */
	{ 0x00bd, 5 }, /* aiIfTimerGreaterThan */
	{ 0x00c3, 2 }, /* aiIfCountdownTimerStopped */
	{ 0x00c4, 4 }, /* aiIfCountdownTimerLessThan */
	{ 0x00c5, 4 }, /* aiIfCountdownTimerGreaterThan */
	{ 0x00c6, 12 }, /* aiSpawnChrAtPad */
	{ 0x00c7, 11 }, /* aiSpawnChrAtChr */
	{ 0x00c8, 9 }, /* aiTryEquipWeapon */
	{ 0x00c9, 8 }, /* aiTryEquipHat */
	{ 0x00ca, 9 }, /* aiDuplicateChr */
	{ 0x00d4, 5 }, /* aiIfObjectSoundVolumeLessThan */
	{ 0x00e2, 6 }, /* aiChrMoveToPad */
	{ 0x00e5, 3 }, /* aiIfColourFadeComplete */
	{ 0x00ea, 3 }, /* aiIfNumPlayersLessThan */
	{ 0x00eb, 5 }, /* aiIfChrAmmoQuantityLessThan */
	{ 0x00ef, 5 }, /* aiIfObjInRoom */
	{ 0x00f0, 2 }, /* ai00f0 */
	{ 0x00f1, 2 }, /* aiIfAttacking */
	{ 0x00f6, 2 }, /* ai00f6 */
	{ 0x00f7, 2 }, /* aiIfAllObjectivesComplete */
	{ 0x00f8, 3 }, /* aiIfPlayerIsInvincible */
	{ 0x00fc, 3 }, /* aiIfKillCountGreaterThan */
	{ 0x00fd, 3 }, /* ai00fd */
	{ 0x0103, 2 }, /* aiIfPropPresetIsBlockingSightToTarget */
	{ 0x0105, 4 }, /* aiIfPropPresetHeightLessThan */
	{ 0x0107, 2 }, /* aiIfPresetsTargetIsNotMyTarget */
	{ 0x0108, 5 }, /* aiIfChrTarget */
	{ 0x0109, 5 }, /* aiSetChrPresetToChrNearSelf */
	{ 0x010a, 7 }, /* aiSetChrPresetToChrNearPad */
	{ 0x010c, 3 }, /* aiIfCompareChrPresetsTeam */
	{ 0x010f, 5 }, /* aiIfChrShieldLessThan */
	{ 0x0110, 5 }, /* aiIfChrShieldGreaterThan */
	{ 0x0113, 2 }, /* aiIfInCutscene */
	{ 0x011a, 7 }, /* aiIfObjHasFlag3 */
	{ 0x011d, 7 }, /* aiIfChrHasHiddenFlag */
	{ 0x011e, 3 }, /* aiIfHuman */
	{ 0x011f, 3 }, /* aiIfSkedar */
	{ 0x0120, 3 }, /* aiIfSafety2LessThan */
	{ 0x0121, 4 }, /* aiFindCover */
	{ 0x0122, 8 }, /* aiFindCoverWithinDist */
	{ 0x0123, 8 }, /* aiFindCoverOutsideDist */
	{ 0x0125, 2 }, /* aiCheckCoverOutOfSight */
	{ 0x0126, 2 }, /* aiIfPlayerUsingCmpOrAr34 */
	{ 0x0127, 2 }, /* aiDetectEnemyOnSameFloor */
	{ 0x0128, 3 }, /* aiDetectEnemy */
	{ 0x0129, 3 }, /* aiIfSafetyLessThan */
	{ 0x012a, 3 }, /* aiIfTargetMovingSlowly */
	{ 0x012b, 2 }, /* aiIfTargetMovingCloser */
	{ 0x012c, 2 }, /* aiIfTargetMovingAway */
	{ 0x0133, 3 }, /* aiSetTeamOrders */
	{ 0x0134, 4 }, /* aiIfOrders */
	{ 0x0135, 2 }, /* aiIfHasOrders */
	{ 0x0137, 3 }, /* aiIfChrInSquadronDoingAction */
	{ 0x0138, 3 }, /* aiIfChannelFree */
	{ 0x013a, 4 }, /* aiSetChrPresetToUnalertedTeammate */
	{ 0x013c, 2 }, /* aiFaceCover */
	{ 0x013d, 3 }, /* aiIfDangerousObjectNearby */
	{ 0x013f, 2 }, /* aiIfHeliWeaponsArmed */
	{ 0x0140, 4 }, /* aiIfHoverbotNextStep */
	{ 0x0147, 3 }, /* aiIfSquadronIsDead */
	{ 0x0149, 5 }, /* aiIfChrListening */
	{ 0x014a, 5 }, /* aiIfTrue */
	{ 0x014b, 2 }, /* aiIfNotListening */
	{ 0x0152, 4 }, /* aiIfNumChrsInSquadronGreaterThan */
	{ 0x0165, 3 }, /* aiIfChrInjured */
	{ 0x0166, 3 }, /* aiIfAction */
	{ 0x0168, 3 }, /* aiIfShieldDamaged */
	{ 0x0169, 3 }, /* aiIfNaturalAnim */
	{ 0x016a, 6 }, /* aiIfY */
	{ 0x016f, 4 }, /* aiIfChrHasGun */
	{ 0x0170, 3 }, /* aiDoGunCommand */
	{ 0x0171, 4 }, /* aiIfDistanceToGunLessThan */
	{ 0x0172, 3 }, /* aiRecoverGun */
	{ 0x0173, 3 }, /* aiChrCopyProperties */
	{ 0x0174, 2 }, /* aiIfCutsceneButtonPressed */
	{ 0x0176, 2 }, /* aiIfBotRespawning */
	{ 0x0178, 3 }, /* aiIfPlayerAutoWalkFinished */
	{ 0x017a, 2 }, /* aiIfLosToAttackTarget */
	{ 0x017b, 3 }, /* aiIfChrKnockedOut */
	{ 0x0181, 4 }, /* aiIfPlayerLookingAtObject */
	{ 0x0182, 3 }, /* aiPunchOrKick */
	{ 0x0183, 2 }, /* aiIfTargetIsPlayer */
	{ 0x0186, 5 }, /* aiIfSoundTimer */
	{ 0x0187, 2 }, /* aiSetTargetToEyespyIfInSight */
	{ 0x0188, 3 }, /* aiIfLiftStationary */
	{ 0x018a, 4 }, /* aiIfLiftAtStop */
	{ 0x018e, 6 }, /* aiMiniSkedarTryPounce */
	{ 0x018f, 7 }, /* aiIfObjectDistanceToPadLessThan */
	{ 0x0192, 3 }, /* aiIfSavefileFlagIsSet */
	{ 0x0193, 3 }, /* aiIfSavefileFlagIsUnset */
	{ 0x019e, 5 }, /* aiIfObjHealthLessThan */
	{ 0x01a5, 2 }, /* aiIfUsingLift */
	{ 0x01a6, 3 }, /* aiIfTargetYDifferenceLessThan */
	{ 0x01a7, 3 }, /* aiIfChrNotTalking */
	{ 0x01aa, 2 }, /* ai01aa */
	{ 0x01ab, 4 }, /* aiIfNumKnockedOutChrs */
	{ 0x01b4, 2 }, /* ai01b4 */
	{ 0x01ba, 6 }, /* aiTryAttackLie */
	{ 0x01bc, 3 }, /* ai01bc */
	{ 0x01bd, 2 }, /* aiIfTrainingPcHolographed */
	{ 0x01be, 4 }, /* aiIfPlayerUsingDevice */
	{ 0x01c0, 2 }, /* aiIfChrTeleportFullWhite */
	{ 0x01c9, 2 }, /* aiTryExitTitle */
	{ 0x01cc, 2 }, /* aiIfFadeComplete */
	{ 0x01d7, 4 }, /* aiIfDistanceToTarget2LessThan */
	{ 0x01d8, 4 }, /* aiIfDistanceToTarget2GreaterThan */
	{ 0x01dd, 3 }, /* aiIfMusicEventQueueIsEmpty */
	{ 0x01de, 2 }, /* aiIfCoopMode */
	{ 0x01df, 7 }, /* aiIfChrSameFloorDistanceToPadLessThan */
};

#define AI_LIST_BRANCH_OP_COUNT \
	((uint32_t)(sizeof(s_AiListBranchOps) / sizeof(s_AiListBranchOps[0])))

int32_t aiListEdgesBranchLabelByte(uint16_t opcode)
{
	uint32_t lo = 0;
	uint32_t hi = AI_LIST_BRANCH_OP_COUNT;

	while (lo < hi) {
		const uint32_t mid = lo + (hi - lo) / 2;

		if (s_AiListBranchOps[mid].opcode == opcode) {
			return s_AiListBranchOps[mid].label_byte;
		}
		if (s_AiListBranchOps[mid].opcode < opcode) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return -1;
}

uint32_t aiListEdgesBranchOpcodeCount(void)
{
	return AI_LIST_BRANCH_OP_COUNT;
}

int aiListEdgesBranchOpcodeAt(uint32_t index, uint16_t *out_opcode,
	uint8_t *out_label_byte)
{
	if (index >= AI_LIST_BRANCH_OP_COUNT) {
		return 0;
	}
	if (out_opcode) {
		*out_opcode = s_AiListBranchOps[index].opcode;
	}
	if (out_label_byte) {
		*out_label_byte = s_AiListBranchOps[index].label_byte;
	}
	return 1;
}

const char *aiListEdgeKindName(ai_list_edge_kind_t kind)
{
	switch (kind) {
	case AI_LIST_EDGE_NEXT: return "next";
	case AI_LIST_EDGE_BRANCH: return "branch";
	case AI_LIST_EDGE_GOTO: return "goto";
	case AI_LIST_EDGE_RESTART: return "restart";
	}
	return NULL;
}

const char *aiListEdgesResultName(ai_list_edges_result_t result)
{
	switch (result) {
	case AI_LIST_EDGES_OK: return "ok";
	case AI_LIST_EDGES_INVALID_ARGUMENT: return "invalid argument";
	case AI_LIST_EDGES_NO_ROOM: return "edge buffer too small";
	case AI_LIST_EDGES_MISSING_OPERAND: return "command operand missing";
	case AI_LIST_EDGES_NO_MEMORY: return "out of memory";
	}
	return "unknown";
}

/* The operand bytes a command's control flow reads: the label of label,
 * go_to_next, go_to_first and the branching commands, the chr of set_list. */
static uint32_t s_operandBytesNeeded(uint16_t opcode)
{
	int32_t label_byte;

	switch (opcode) {
	case AI_LIST_OP_GO_TO_NEXT:
	case AI_LIST_OP_GO_TO_FIRST:
	case AI_LIST_OP_LABEL:
	case AI_LIST_OP_SET_LIST:
		return 1;
	default:
		break;
	}
	label_byte = aiListEdgesBranchLabelByte(opcode);
	return label_byte >= 2 ? (uint32_t)label_byte - 1 : 0;
}

/* chraiGoToLabel: the first label numbered `label` at or after `start`; the
 * list's end stops the scan (the native scan then returns offset 0). */
static int s_findLabel(const ai_list_command_t *commands, uint32_t count,
	uint32_t start, uint8_t label, uint32_t *out_index)
{
	for (uint32_t i = start; i < count; i++) {
		const ai_list_command_t *cmd = &commands[i];

		if (cmd->opcode == AI_LIST_OP_LABEL) {
			if (cmd->operands[0] == label) {
				*out_index = i;
				return 1;
			}
		} else if (cmd->opcode == AI_LIST_OP_END) {
			return 0;
		}
	}
	return 0;
}

static int s_addEdge(ai_list_edge_t *edges, uint32_t edge_capacity,
	uint32_t *edge_count, uint32_t from, uint32_t to,
	ai_list_edge_kind_t kind)
{
	if (*edge_count >= edge_capacity) {
		return 0;
	}
	edges[*edge_count].from = from;
	edges[*edge_count].to = to;
	edges[*edge_count].kind = kind;
	(*edge_count)++;
	return 1;
}

ai_list_edges_result_t aiListEdgesCompute(const ai_list_command_t *commands,
	uint32_t count, ai_list_edge_t *edges, uint32_t edge_capacity,
	uint32_t *out_edge_count, uint8_t *node_flags, uint32_t *out_bad_command)
{
	uint32_t edge_count = 0;
	uint32_t *first_edge;
	uint32_t *stack;
	uint32_t top = 0;

	if (out_edge_count) {
		*out_edge_count = 0;
	}
	if (out_bad_command) {
		*out_bad_command = 0;
	}
	if (count == 0) {
		return AI_LIST_EDGES_OK;
	}
	if (!commands || !node_flags || (!edges && edge_capacity > 0)) {
		return AI_LIST_EDGES_INVALID_ARGUMENT;
	}

	for (uint32_t i = 0; i < count; i++) {
		const uint32_t needed = s_operandBytesNeeded(commands[i].opcode);

		if (needed > 0 && (!commands[i].operands
				|| commands[i].operand_len < needed)) {
			if (out_bad_command) {
				*out_bad_command = i;
			}
			return AI_LIST_EDGES_MISSING_OPERAND;
		}
	}

	first_edge = (uint32_t *)malloc(((size_t)count + 1) * sizeof(*first_edge));
	if (!first_edge) {
		return AI_LIST_EDGES_NO_MEMORY;
	}

	for (uint32_t i = 0; i < count; i++) {
		const ai_list_command_t *cmd = &commands[i];
		int has_next = 1;
		int has_jump = 0;
		uint32_t jump_to = 0;
		ai_list_edge_kind_t jump_kind = AI_LIST_EDGE_BRANCH;
		uint8_t flags = 0;
		int32_t label_byte;

		first_edge[i] = edge_count;
		switch (cmd->opcode) {
		case AI_LIST_OP_GO_TO_NEXT:
		case AI_LIST_OP_GO_TO_FIRST:
			/* aiGoToNext scans from itself, aiGoToFirst from the top. */
			has_next = 0;
			has_jump = 1;
			jump_kind = s_findLabel(commands, count,
				cmd->opcode == AI_LIST_OP_GO_TO_NEXT ? i : 0,
				cmd->operands[0], &jump_to)
				? AI_LIST_EDGE_GOTO : AI_LIST_EDGE_RESTART;
			break;
		case AI_LIST_OP_YIELD:
			/* Ends the tick; the next tick resumes at the next command. */
			flags |= AI_LIST_NODE_YIELDS;
			break;
		case AI_LIST_OP_END:
			has_next = 0;
			flags |= AI_LIST_NODE_ENDS;
			break;
		case AI_LIST_OP_RETURN:
			has_next = 0;
			flags |= AI_LIST_NODE_RETURNS;
			break;
		case AI_LIST_OP_SET_LIST:
			/* On CHR_SELF the running list switches; on another chr the
			 * list continues with the next command. */
			if (cmd->operands[0] == AI_LIST_CHR_SELF) {
				has_next = 0;
				flags |= AI_LIST_NODE_SWITCHES_LIST;
			}
			break;
		default:
			label_byte = aiListEdgesBranchLabelByte(cmd->opcode);
			if (label_byte >= 2) {
				/* chraiGoToLabel from the command's own offset. */
				has_jump = 1;
				jump_kind = s_findLabel(commands, count, i,
					cmd->operands[label_byte - 2], &jump_to)
					? AI_LIST_EDGE_BRANCH : AI_LIST_EDGE_RESTART;
			}
			break;
		}
		if (has_jump && jump_kind == AI_LIST_EDGE_RESTART) {
			jump_to = 0;
		}
		if ((has_next && i + 1 < count && !s_addEdge(edges, edge_capacity,
				&edge_count, i, i + 1, AI_LIST_EDGE_NEXT))
				|| (has_jump && !s_addEdge(edges, edge_capacity,
				&edge_count, i, jump_to, jump_kind))) {
			free(first_edge);
			return AI_LIST_EDGES_NO_ROOM;
		}
		node_flags[i] = flags;
	}
	first_edge[count] = edge_count;

	/* Reachable: every command a path of edges from the first one reaches. */
	stack = (uint32_t *)malloc((size_t)count * sizeof(*stack));
	if (!stack) {
		free(first_edge);
		return AI_LIST_EDGES_NO_MEMORY;
	}
	node_flags[0] |= AI_LIST_NODE_REACHABLE;
	stack[top++] = 0;
	while (top > 0) {
		const uint32_t node = stack[--top];

		for (uint32_t e = first_edge[node]; e < first_edge[node + 1]; e++) {
			const uint32_t to = edges[e].to;

			if (!(node_flags[to] & AI_LIST_NODE_REACHABLE)) {
				node_flags[to] |= AI_LIST_NODE_REACHABLE;
				stack[top++] = to;
			}
		}
	}
	free(stack);
	free(first_edge);

	if (out_edge_count) {
		*out_edge_count = edge_count;
	}
	return AI_LIST_EDGES_OK;
}
