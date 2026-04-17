/**
 * forge_undo.c -- Undo/redo ring buffer for The Grid editor (F1).
 *
 * Record an op (place/delete/transform/prop) before AND after the mutation
 * so the stack can replay in either direction. The ring is a single array
 * with head/tail pointers; a second head marks the redo frontier after an
 * undo.
 *
 * Simple grouping: a "barrier" entry with op_kind=4 separates logical
 * actions (e.g. a multi-select duplicate places N objects but reads as one
 * undo step).
 */

#include "forge/forge_core.h"

#include <string.h>

#include "system.h"

static forge_undo_entry_t s_undo[FORGE_MAX_UNDO];
static s32 s_head;          /* next slot to write */
static s32 s_tail;          /* oldest valid slot */
static s32 s_redo_head;     /* high-water mark after undoing */
static s32 s_count;         /* number of valid entries between tail and head */

#define FORGE_UNDO_KIND_PLACE     0
#define FORGE_UNDO_KIND_DELETE    1
#define FORGE_UNDO_KIND_TRANSFORM 2
#define FORGE_UNDO_KIND_PROP      3
#define FORGE_UNDO_KIND_BARRIER   4

static s32 forgeUndoAdvance(s32 i) { return (i + 1) % FORGE_MAX_UNDO; }
static s32 forgeUndoRetreat(s32 i) { return (i + FORGE_MAX_UNDO - 1) % FORGE_MAX_UNDO; }

static void forgeUndoPush(forge_undo_entry_t *e)
{
	s_undo[s_head] = *e;
	s_undo[s_head].in_use = 1;
	s_head = forgeUndoAdvance(s_head);
	/* If we've hit tail, drop the oldest. */
	if (s_head == s_tail) {
		s_tail = forgeUndoAdvance(s_tail);
	} else {
		++s_count;
	}
	s_redo_head = s_head; /* any new entry clears redo */
}

s32 forgeUndoCanUndo(void)
{
	if (s_count == 0) return 0;
	s32 i = forgeUndoRetreat(s_head);
	return (i != s_tail || s_undo[i].in_use) ? 1 : 0;
}

s32 forgeUndoCanRedo(void)
{
	return (s_head != s_redo_head) ? 1 : 0;
}

void forgeUndoPushBarrier(const char *label)
{
	forge_undo_entry_t e;
	memset(&e, 0, sizeof(e));
	e.op_kind = FORGE_UNDO_KIND_BARRIER;
	e.uid = 0;
	(void)label;
	forgeUndoPush(&e);
}

void forgeUndoRecordPlace(u32 uid, const forge_object_t *snap)
{
	if (!snap) return;
	forge_undo_entry_t e;
	memset(&e, 0, sizeof(e));
	e.op_kind = FORGE_UNDO_KIND_PLACE;
	e.uid = uid;
	e.after = *snap;
	forgeUndoPush(&e);
}

void forgeUndoRecordDelete(u32 uid, const forge_object_t *snap)
{
	if (!snap) return;
	forge_undo_entry_t e;
	memset(&e, 0, sizeof(e));
	e.op_kind = FORGE_UNDO_KIND_DELETE;
	e.uid = uid;
	e.before = *snap;
	forgeUndoPush(&e);
}

void forgeUndoRecordTransform(u32 uid, const forge_object_t *before, const forge_object_t *after)
{
	if (!before || !after) return;
	forge_undo_entry_t e;
	memset(&e, 0, sizeof(e));
	e.op_kind = FORGE_UNDO_KIND_TRANSFORM;
	e.uid = uid;
	e.before = *before;
	e.after = *after;
	forgeUndoPush(&e);
}

void forgeUndoRecordPropChange(u32 uid, const forge_object_t *before, const forge_object_t *after)
{
	if (!before || !after) return;
	forge_undo_entry_t e;
	memset(&e, 0, sizeof(e));
	e.op_kind = FORGE_UNDO_KIND_PROP;
	e.uid = uid;
	e.before = *before;
	e.after = *after;
	forgeUndoPush(&e);
}

static void forgeUndoRestore(const forge_undo_entry_t *e, s32 going_backward)
{
	switch (e->op_kind) {
	case FORGE_UNDO_KIND_PLACE: {
		/* undo = remove the placed object; redo = re-place it */
		forge_object_t *o = forgeObjectFindByUid(e->uid);
		if (going_backward) {
			if (o) o->in_use = 0;
		} else {
			if (!o) {
				/* find a slot and restore */
				o = forgeObjectAllocate((forge_category_t)e->after.category, e->after.catalog_id);
			}
			if (o) {
				u32 uid = o->uid;
				*o = e->after;
				o->uid = uid;
				o->in_use = 1;
			}
		}
		break;
	}
	case FORGE_UNDO_KIND_DELETE: {
		forge_object_t *o = forgeObjectFindByUid(e->uid);
		if (going_backward) {
			if (!o) o = forgeObjectAllocate((forge_category_t)e->before.category, e->before.catalog_id);
			if (o) {
				u32 uid = o->uid;
				*o = e->before;
				o->uid = uid;
				o->in_use = 1;
			}
		} else {
			if (o) o->in_use = 0;
		}
		break;
	}
	case FORGE_UNDO_KIND_TRANSFORM:
	case FORGE_UNDO_KIND_PROP: {
		forge_object_t *o = forgeObjectFindByUid(e->uid);
		if (o) {
			const forge_object_t *src = going_backward ? &e->before : &e->after;
			u32 uid = o->uid;
			*o = *src;
			o->uid = uid;
		}
		break;
	}
	case FORGE_UNDO_KIND_BARRIER:
	default:
		break;
	}
}

void forgeUndoApplyUndo(void)
{
	if (!forgeUndoCanUndo()) return;
	/* walk backward, applying entries until we hit a barrier or the tail */
	s32 i = forgeUndoRetreat(s_head);
	s32 steps = 0;
	for (;;) {
		const forge_undo_entry_t *e = &s_undo[i];
		if (!e->in_use) break;
		if (e->op_kind == FORGE_UNDO_KIND_BARRIER && steps > 0) break;
		if (e->op_kind != FORGE_UNDO_KIND_BARRIER) {
			forgeUndoRestore(e, 1);
			++steps;
		}
		if (i == s_tail) {
			i = forgeUndoRetreat(i);
			break;
		}
		i = forgeUndoRetreat(i);
		if (i == s_tail && !s_undo[s_tail].in_use) break;
	}
	s_head = forgeUndoAdvance(i);
	sysLogPrintf(LOG_NOTE, "GRID.UNDO: undo %d step(s)", steps);
}

void forgeUndoApplyRedo(void)
{
	if (!forgeUndoCanRedo()) return;
	s32 i = s_head;
	s32 steps = 0;
	while (i != s_redo_head) {
		const forge_undo_entry_t *e = &s_undo[i];
		if (!e->in_use) break;
		if (e->op_kind == FORGE_UNDO_KIND_BARRIER && steps > 0) {
			i = forgeUndoAdvance(i);
			break;
		}
		if (e->op_kind != FORGE_UNDO_KIND_BARRIER) {
			forgeUndoRestore(e, 0);
			++steps;
		}
		i = forgeUndoAdvance(i);
	}
	s_head = i;
	sysLogPrintf(LOG_NOTE, "GRID.UNDO: redo %d step(s)", steps);
}
