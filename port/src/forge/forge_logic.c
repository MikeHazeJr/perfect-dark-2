/**
 * forge_logic.c -- The Grid visual scripting runtime (F5).
 *
 * Edit-time: nodes and wires live in forge_core pools. This file is the
 * execution side -- fires events, evaluates conditions, dispatches actions.
 *
 * Scope for this session: the dispatcher walks the node graph from an event
 * trigger forward along wires, evaluates conditions, and executes actions
 * that mutate channels, objects, and objectives. Real engine wire-ins (open
 * an actual door, teleport a real chr) remain log-placeholders because
 * those bindings depend on the F3 map-load path that instantiates catalog
 * objects as engine props -- that comes online with playtest.
 */

#include "forge/forge_core.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "system.h"

static s32 s_recursion_guard;

/* Forward decls. */
static s32 forgeLogicEvalCondition(const forge_logic_node_t *c);
static void forgeLogicFollowOutputs(u32 from_uid);
static void forgeLogicExecuteAction(forge_logic_node_t *a);

/* Reset per-frame executed flag so the same action can trigger again next frame. */
void forgeLogicResetPerFrameFlags(void)
{
	s32 count = forgeLogicNodeCount();
	(void)count;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		forge_logic_node_t *n = forgeLogicNodeGet(i);
		if (n && n->in_use) n->executed_this_frame = 0;
	}
}

/* ============================================================
 * Cycle detector (§7.4 — flagged at save time)
 * ============================================================ */

static s32 forgeLogicDfsHasCycle(u32 uid, u8 *visiting, u8 *visited)
{
	forge_logic_node_t *node = forgeLogicNodeFindByUid(uid);
	if (!node) return 0;
	s32 self_idx = -1;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		if (forgeLogicNodeGet(i) == node) { self_idx = i; break; }
	}
	if (self_idx < 0) return 0;
	if (visiting[self_idx]) return 1;
	if (visited[self_idx]) return 0;
	visiting[self_idx] = 1;
	for (s32 w = 0; w < FORGE_MAX_LOGIC_WIRES; ++w) {
		forge_logic_wire_t *wire = forgeLogicWireGet(w);
		if (!wire || !wire->in_use) continue;
		if (wire->src_node_uid == uid) {
			if (forgeLogicDfsHasCycle(wire->dst_node_uid, visiting, visited)) {
				visiting[self_idx] = 0;
				visited[self_idx] = 1;
				return 1;
			}
		}
	}
	visiting[self_idx] = 0;
	visited[self_idx] = 1;
	return 0;
}

void forgeLogicDetectCycles(s32 *out_warn_count)
{
	u8 visiting[FORGE_MAX_LOGIC_NODES];
	u8 visited[FORGE_MAX_LOGIC_NODES];
	memset(visiting, 0, sizeof(visiting));
	memset(visited, 0, sizeof(visited));
	s32 warn = 0;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		forge_logic_node_t *n = forgeLogicNodeGet(i);
		if (!n || !n->in_use) continue;
		if (!visited[i] && forgeLogicDfsHasCycle(n->uid, visiting, visited)) {
			++warn;
			sysLogPrintf(LOG_WARNING, "GRID.LOGIC: cycle detected involving node uid=%u",
					n->uid);
		}
	}
	if (out_warn_count) *out_warn_count = warn;
}

/* ============================================================
 * Condition evaluation
 * ============================================================ */

static s32 forgeLogicEvalCondition(const forge_logic_node_t *c)
{
	if (!c) return 0;
	switch (c->op) {
	case FORGE_OP_CHANNEL_STATE: {
		forge_channel_t *ch = forgeChannelFind(c->param_text_a);
		return (ch && ch->state) ? 1 : 0;
	}
	case FORGE_OP_RANDOM: {
		/* param_int_a = probability 0..100 */
		s32 p = c->param_int_a;
		if (p <= 0) return 0;
		if (p >= 100) return 1;
		s32 r = rand() % 100;
		return (r < p) ? 1 : 0;
	}
	case FORGE_OP_KILL_COUNT_GE:
	case FORGE_OP_TEAM_SCORE_GE:
	case FORGE_OP_PLAYER_COUNT:
	case FORGE_OP_TIMER_ELAPSED:
	case FORGE_OP_SWITCH_STATE:
	case FORGE_OP_HAS_ITEM:
	case FORGE_OP_ALL_ENEMIES_DEAD:
		/* Full runtime hooks not yet wired; treat as pass-through in the
		 * edit-time test harness. */
		return 1;
	default:
		return 0;
	}
}

/* ============================================================
 * Action dispatch
 * ============================================================ */

static void forgeLogicExecuteAction(forge_logic_node_t *a)
{
	if (!a) return;
	if (a->executed_this_frame) return;
	a->executed_this_frame = 1;

	switch (a->op) {
	case FORGE_OP_SET_CHANNEL: {
		u8 state = (a->param_int_a != 0);
		forgeChannelSet(a->param_text_a, state);
		break;
	}
	case FORGE_OP_OPEN_DOOR: {
		forge_object_t *o = forgeObjectFindByUid(a->target_uid_a);
		if (o && o->category == FORGE_CAT_INTERACTABLE) {
			sysLogPrintf(LOG_NOTE, "GRID.LOGIC: open door uid=%u '%s'", o->uid, o->label);
			o->props.door.locked = 0;
		}
		break;
	}
	case FORGE_OP_CLOSE_DOOR: {
		forge_object_t *o = forgeObjectFindByUid(a->target_uid_a);
		if (o && o->category == FORGE_CAT_INTERACTABLE) {
			sysLogPrintf(LOG_NOTE, "GRID.LOGIC: close door uid=%u", o->uid);
		}
		break;
	}
	case FORGE_OP_LOCK_DOOR: {
		forge_object_t *o = forgeObjectFindByUid(a->target_uid_a);
		if (o && o->category == FORGE_CAT_INTERACTABLE) o->props.door.locked = 1;
		break;
	}
	case FORGE_OP_UNLOCK_DOOR: {
		forge_object_t *o = forgeObjectFindByUid(a->target_uid_a);
		if (o && o->category == FORGE_CAT_INTERACTABLE) o->props.door.locked = 0;
		break;
	}
	case FORGE_OP_ENABLE_OBJECT: {
		forge_object_t *o = forgeObjectFindByUid(a->target_uid_a);
		if (o) o->enabled = 1;
		break;
	}
	case FORGE_OP_DISABLE_OBJECT: {
		forge_object_t *o = forgeObjectFindByUid(a->target_uid_a);
		if (o) o->enabled = 0;
		break;
	}
	case FORGE_OP_DESTROY_OBJECT: {
		forge_object_t *o = forgeObjectFindByUid(a->target_uid_a);
		if (o) {
			sysLogPrintf(LOG_NOTE, "GRID.LOGIC: destroy uid=%u '%s'", o->uid, o->catalog_id);
			o->in_use = 0;
		}
		break;
	}
	case FORGE_OP_SHOW_MESSAGE:
		sysLogPrintf(LOG_NOTE, "GRID.LOGIC: HUD message '%s' (recipient=%d dur=%.1f)",
				a->param_text_a, a->param_int_a, a->param_float_a);
		break;
	case FORGE_OP_PLAY_SOUND:
		sysLogPrintf(LOG_NOTE, "GRID.LOGIC: play sound '%s' vol=%.2f",
				a->param_text_a, a->param_float_a);
		break;
	case FORGE_OP_AWARD_SCORE:
		sysLogPrintf(LOG_NOTE, "GRID.LOGIC: award %d to team %d",
				a->param_int_a, a->param_int_b);
		break;
	case FORGE_OP_END_MISSION:
		sysLogPrintf(LOG_NOTE, "GRID.LOGIC: end mission success=%d msg='%s'",
				a->param_int_a, a->param_text_a);
		break;
	case FORGE_OP_OBJECTIVE_COMPLETE: {
		s32 idx = a->param_int_a;
		forgeObjectiveSetStatus(idx, FORGE_OBJ_COMPLETE);
		break;
	}
	case FORGE_OP_OBJECTIVE_FAIL: {
		s32 idx = a->param_int_a;
		forgeObjectiveSetStatus(idx, FORGE_OBJ_FAILED);
		break;
	}
	case FORGE_OP_SPAWN_WAVE: {
		/* dispatched to forge_gametype runtime */
		extern void forgeGametypeTriggerWave(s32 wave_index);
		forgeGametypeTriggerWave(a->param_int_a);
		break;
	}
	case FORGE_OP_TRIGGER_BOSS_PHASE:
		sysLogPrintf(LOG_NOTE, "GRID.LOGIC: trigger boss phase %d", a->param_int_a);
		break;
	case FORGE_OP_TELEPORT_PLAYER: {
		forge_object_t *o = forgeObjectFindByUid(a->target_uid_a);
		if (o) sysLogPrintf(LOG_NOTE, "GRID.LOGIC: teleport to uid=%u (%.0f,%.0f,%.0f)",
				o->uid, o->pos[0], o->pos[1], o->pos[2]);
		break;
	}
	case FORGE_OP_SPAWN_OBJECT: {
		forge_object_t *proto = forgeObjectFindByUid(a->target_uid_a);
		if (proto) {
			forge_object_t *n = forgeObjectAllocate((forge_category_t)proto->category, proto->catalog_id);
			if (n) {
				u32 uid = n->uid;
				*n = *proto;
				n->uid = uid;
				n->in_use = 1;
			}
		}
		break;
	}
	case FORGE_OP_SPAWN_AI: {
		forge_object_t *proto = forgeObjectFindByUid(a->target_uid_a);
		if (proto) {
			forge_object_t *n = forgeObjectAllocate(FORGE_CAT_AI, proto->catalog_id);
			if (n) {
				u32 uid = n->uid;
				*n = *proto;
				n->uid = uid;
				n->in_use = 1;
			}
		}
		break;
	}
	case FORGE_OP_SET_TIMER:
	case FORGE_OP_CHANGE_ZONE:
	case FORGE_OP_CAMERA_EVENT:
	case FORGE_OP_ACTIVATE_ELEVATOR:
		sysLogPrintf(LOG_NOTE, "GRID.LOGIC: action op=%d (runtime placeholder)", (int)a->op);
		break;
	default:
		break;
	}

	/* Actions can chain further actions via OR gate. */
	forgeLogicFollowOutputs(a->uid);
}

/* ============================================================
 * Graph traversal
 * ============================================================ */

static void forgeLogicFollowOutputs(u32 from_uid)
{
	if (s_recursion_guard > 128) {
		sysLogPrintf(LOG_WARNING, "GRID.LOGIC: recursion depth cap hit");
		return;
	}
	++s_recursion_guard;
	for (s32 w = 0; w < FORGE_MAX_LOGIC_WIRES; ++w) {
		forge_logic_wire_t *wire = forgeLogicWireGet(w);
		if (!wire || !wire->in_use) continue;
		if (wire->src_node_uid != from_uid) continue;
		forge_logic_node_t *tgt = forgeLogicNodeFindByUid(wire->dst_node_uid);
		if (!tgt) continue;
		if (tgt->kind == FORGE_LOGIC_CONDITION) {
			if (forgeLogicEvalCondition(tgt)) {
				forgeLogicFollowOutputs(tgt->uid);
			}
		} else if (tgt->kind == FORGE_LOGIC_ACTION) {
			forgeLogicExecuteAction(tgt);
		}
	}
	--s_recursion_guard;
}

/* ============================================================
 * Public event-fire API
 * ============================================================ */

void forgeLogicFireEvent(forge_logic_op_t event_op, u32 related_uid)
{
	s_recursion_guard = 0;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		forge_logic_node_t *n = forgeLogicNodeGet(i);
		if (!n || !n->in_use) continue;
		if (n->kind != FORGE_LOGIC_EVENT) continue;
		if (n->op != (u8)event_op) continue;
		/* If event carries a target uid filter and it's set, match it. */
		if (n->target_uid_a && related_uid && n->target_uid_a != related_uid) continue;
		forgeLogicFollowOutputs(n->uid);
	}
}

/* Channel-change event fires ON_CHANNEL nodes whose channel name matches.
 * Called from forge_core::forgeChannelSet. */
void forgeLogicFireChannelChange(const char *name, u8 state)
{
	s_recursion_guard = 0;
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		forge_logic_node_t *n = forgeLogicNodeGet(i);
		if (!n || !n->in_use) continue;
		if (n->kind != FORGE_LOGIC_EVENT) continue;
		if (n->op != FORGE_OP_ON_CHANNEL) continue;
		if (strcmp(n->param_text_a, name) != 0) continue;
		/* param_int_a = 0 any, 1 on, 2 off */
		if (n->param_int_a == 1 && !state) continue;
		if (n->param_int_a == 2 && state)  continue;
		forgeLogicFollowOutputs(n->uid);
	}
}

/* Per-tick hook: nothing to do for now beyond per-frame flag reset, but
 * timer-event evaluation could live here. */
void forgeLogicTick(void)
{
}
