/**
 * forge_level_live_world.c -- the client's half of forge_level_live.h.
 *
 * Hides the stage's objects the open document moved, redrew or left out, and
 * draws stand-ins where the document puts objects, while the editor camera
 * flies. A stand-in is a plain object in the stage's memory, made and set
 * down the way setup makes and sets down an object on a pad
 * (setupCreateObject: the pad's look and up, the object's size, then
 * func0f06a730, which rests it on the floor under the pad). Stand-ins are
 * never freed while the stage lasts: a stand-in no longer wanted is hidden
 * and kept for the next one drawn with its model.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "forge_level_doc.h"
#include "forge_level_io.h"
#include "forge_level_live.h"
#include "system.h"
#include "game/bg.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/setuputils.h"
#include "lib/collision.h"
#include "lib/memp.h"
#include "lib/meshcollision.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "constants.h"

/* The most stand-ins one stage holds. */
#define LIVE_STANDINS 64
/* Per change of the document, the first so many hides and draws get a line each. */
#define LIVE_LOG_LINES 24

typedef struct live_standin {
	char record_id[32];       /* the object it stands in for, "" when not wanted */
	s32 modelnum;
	s32 extra_scale;
	struct defaultobj *obj;   /* in the stage's memory (s_Live.pool) */
} live_standin_t;

typedef struct live_hidden {
	char record_id[32];
	s32 command;
	struct prop *prop;        /* the stage's own, disabled here */
} live_hidden_t;

static struct {
	/* The stage everything below points into, once anything does. */
	s32 bound;
	void *stage_props;
	s32 stagenum;
	s32 last_frame;

	s32 applied;              /* the world follows the document */
	s32 refused;              /* the document of `generation` could not be followed */
	u32 generation;           /* of the document as last applied or refused */

	live_hidden_t hidden[FORGE_LEVEL_LIVE_MAX];
	s32 hidden_count;

	struct defaultobj *pool;
	live_standin_t standins[LIVE_STANDINS];
	s32 standin_count;

	s32 log_lines;
	char off_reason[160];     /* why the world last stopped following, logged once */
} s_Live;

static void liveLog(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

static void liveLog(const char *fmt, ...)
{
	char line[320];
	va_list args;

	if (s_Live.log_lines >= LIVE_LOG_LINES) return;
	s_Live.log_lines++;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.LIVE: %s", line);
}

/* The object type the stage gives each kind the plan shows. A weapon place
 * of a match can be made a shield there (setupPlaceWeapon). */
static s32 liveKindMatches(const char *kind, u8 type)
{
	static const struct { const char *kind; u8 type; } kinds[] = {
		{ "prop", OBJTYPE_BASIC },
		{ "weapon_pickup", OBJTYPE_WEAPON },
		{ "weapon_pickup", OBJTYPE_SHIELD },
		{ "ammo_crate", OBJTYPE_AMMOCRATE },
		{ "multi_ammo_crate", OBJTYPE_MULTIAMMOCRATE },
		{ "hoverbike", OBJTYPE_HOVERBIKE },
		{ "hover_prop", OBJTYPE_HOVERPROP },
		{ "key", OBJTYPE_KEY },
		{ "hat", OBJTYPE_HAT },
		{ "shield", OBJTYPE_SHIELD },
		{ "gas_bottle", OBJTYPE_GASBOTTLE },
		{ "single_monitor", OBJTYPE_SINGLEMONITOR },
		{ "multi_monitor", OBJTYPE_MULTIMONITOR },
		{ "hanging_monitors", OBJTYPE_HANGINGMONITORS },
		{ "alarm", OBJTYPE_ALARM },
		{ "cctv", OBJTYPE_CCTV },
		{ "autogun", OBJTYPE_AUTOGUN },
		{ "safe", OBJTYPE_SAFE },
		{ "glass", OBJTYPE_GLASS },
		{ "tinted_glass", OBJTYPE_TINTEDGLASS },
	};

	for (u32 i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
		if (kinds[i].type == type && strcmp(kinds[i].kind, kind) == 0) return 1;
	}
	return 0;
}

/* Everything here belongs to one stage. Called first every frame: when the
 * stage is not the one last seen (another setup block, another stage number,
 * or a frame count that went back, as it does when a stage loads), forget it
 * all without touching it: what pointed into the old stage's memory is gone. */
static void liveCheckStage(void)
{
	const s32 frame = g_Vars.lvframenum;

	if ((void *)g_StageSetup.props != s_Live.stage_props || g_StageNum != s_Live.stagenum ||
			frame < s_Live.last_frame) {
		if (s_Live.bound) {
			sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.LIVE: the stage changed: forgot %d hidden objects and %d stand-ins",
				s_Live.hidden_count, s_Live.standin_count);
		}
		s_Live.bound = 0;
		s_Live.applied = 0;
		s_Live.refused = 0;
		s_Live.hidden_count = 0;
		s_Live.pool = NULL;
		s_Live.standin_count = 0;
		s_Live.stage_props = (void *)g_StageSetup.props;
		s_Live.stagenum = g_StageNum;
	}
	s_Live.last_frame = frame;
}

/* Something now points into this stage's memory. */
static void liveBind(void)
{
	s_Live.bound = 1;
}

static void liveOff(const char *reason)
{
	if (strcmp(s_Live.off_reason, reason) == 0) return;
	snprintf(s_Live.off_reason, sizeof(s_Live.off_reason), "%s", reason);
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.LIVE: off: %s", reason);
}

/* The stage's own object `h` is shown again, if it is still the one hidden. */
static s32 liveUnhide(const live_hidden_t *h)
{
	struct defaultobj *obj = setupGetObjByCmdIndex((u32)h->command);

	if (!obj || !h->prop || obj->prop != h->prop) return 0;
	propEnable(h->prop);
	return 1;
}

/* Shows the level as loaded: the stage's objects back, the stand-ins hidden. */
static void liveShowStage(const char *why)
{
	s32 shown = 0;
	s32 hidden = 0;

	if (!s_Live.applied && s_Live.hidden_count == 0) return;
	for (s32 i = 0; i < s_Live.hidden_count; i++) {
		shown += liveUnhide(&s_Live.hidden[i]);
	}
	s_Live.hidden_count = 0;
	for (s32 i = 0; i < s_Live.standin_count; i++) {
		live_standin_t *slot = &s_Live.standins[i];
		slot->record_id[0] = '\0';
		if (slot->obj && slot->obj->prop && (slot->obj->prop->flags & PROPFLAG_ENABLED)) {
			propDisable(slot->obj->prop);
			hidden++;
		}
	}
	s_Live.applied = 0;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.LIVE: showing the level as it was loaded (%s): %d objects back, %d stand-ins hidden",
		why, shown, hidden);
}

/* Sets an object down where `d` says, as setup sets down an object on a pad. */
static s32 livePlace(struct defaultobj *obj, const forge_level_live_draw_t *d)
{
	struct coord pos;
	RoomNum inrooms[24];
	RoomNum aboverooms[22];
	RoomNum rooms[2];
	RoomNum *found = NULL;
	Mtxf mtx;
	f32 look[3];
	f32 up[3];
	s32 room;

	pos.x = d->position[0];
	pos.y = d->position[1];
	pos.z = d->position[2];
	/* The room under the pad, found the way setup finds one for a pad that
	 * names none (setupPreparePads). */
	bgFindRoomsByPos(&pos, inrooms, aboverooms, 20, NULL);
	if (inrooms[0] != -1) {
		found = inrooms;
	} else if (aboverooms[0] != -1) {
		found = aboverooms;
	}
	if (!found) return 0;
	room = cdFindFloorRoomAtPos(&pos, found);
	rooms[0] = room > 0 ? (RoomNum)room : found[0];
	rooms[1] = -1;

	for (s32 k = 0; k < 3; k++) {
		look[k] = d->look[k];
		up[k] = d->up[k];
	}
	if (look[0] * look[0] + look[1] * look[1] + look[2] * look[2] < 0.000001f) {
		look[0] = 0.0f; look[1] = 0.0f; look[2] = 1.0f;
	}
	if (up[0] * up[0] + up[1] * up[1] + up[2] * up[2] < 0.000001f) {
		up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
	}
	mtx00016d58(&mtx, 0, 0, 0, -look[0], -look[1], -look[2], up[0], up[1], up[2]);
	mtx00015f04(obj->model->scale, &mtx);
	func0f06a730(obj, &pos, &mtx, rooms, &pos);
	return 1;
}

/* A new stand-in drawn with `modelnum`, in slot `index`. NULL when the stage
 * cannot make one. */
static struct defaultobj *liveMakeStandin(s32 index, s32 modelnum, s32 extra_scale)
{
	struct defaultobj *obj;
	struct prop *prop;

	if (!s_Live.pool) {
		s_Live.pool = (struct defaultobj *)mempAlloc(LIVE_STANDINS * sizeof(struct defaultobj), MEMPOOL_STAGE);
		if (!s_Live.pool) return NULL;
		liveBind();
	}
	if (modelnum < 0 || modelnum >= MODEL_CUSTOM_END) return NULL;
	if (!setupLoadModeldef(modelnum) || !g_ModelStates[modelnum].modeldef) return NULL;

	obj = &s_Live.pool[index];
	memset(obj, 0, sizeof(*obj));
	obj->type = OBJTYPE_BASIC;
	obj->modelnum = (s16)modelnum;
	obj->pad = -1;
	obj->extrascale = (u16)extra_scale;
	obj->flags3 = OBJFLAG3_WALKTHROUGH;
	obj->maxdamage = 1000;
	obj->floorcol = 0x0fff;
	obj->realrot[0][0] = 1.0f;
	obj->realrot[1][1] = 1.0f;
	obj->realrot[2][2] = 1.0f;

	prop = objInitWithModelDef(obj, g_ModelStates[modelnum].modeldef);
	if (!prop || !obj->model) return NULL;
	modelSetScale(obj->model, obj->model->scale * (extra_scale * (1.0f / 256.0f)));
	/* A picture, not a thing: nothing stands on it or bumps into it. */
	meshDetachFromProp(prop);
	propActivate(prop);
	return obj;
}

/* The model and size the stand-in for `d` is drawn with; `from_stage` says
 * whether they are the stage's own object's. */
static s32 liveLook(const forge_level_live_draw_t *d, s32 *modelnum, s32 *extra_scale, s32 *from_stage)
{
	catalog_model_result_t model;

	*from_stage = 0;
	if (d->looks_like_command >= 0) {
		struct defaultobj *obj = setupGetObjByCmdIndex((u32)d->looks_like_command);
		if (obj && obj->model && obj->modelnum >= 0) {
			*modelnum = obj->modelnum;
			*extra_scale = obj->extrascale > 0 ? obj->extrascale : 256;
			*from_stage = 1;
			return 1;
		}
	}
	memset(&model, 0, sizeof(model));
	if (!catalogResolveModel(d->model_catalog_id, &model) || model.modelnum < 0) return 0;
	*modelnum = model.modelnum;
	*extra_scale = d->extra_scale > 0 ? d->extra_scale : 256;
	return 1;
}

/* Draws the stand-ins of `plan`. `drawn[j]` is set for each draw that made
 * it into the world. */
static void liveDrawStandins(const forge_level_live_plan_t *plan, s32 *drawn, s32 *drew, s32 *failed)
{
	s32 taken[LIVE_STANDINS];

	memset(taken, 0, sizeof(taken));
	for (s32 j = 0; j < plan->draw_count; j++) {
		const forge_level_live_draw_t *d = &plan->draw[j];
		s32 modelnum = -1;
		s32 scale = 256;
		s32 from_stage = 0;
		s32 use = -1;
		struct defaultobj *obj;

		drawn[j] = 0;
		if (!liveLook(d, &modelnum, &scale, &from_stage)) {
			liveLog("%s: no model to draw it with (\"%s\")", d->record_id, d->model_catalog_id);
			(*failed)++;
			continue;
		}
		/* The slot that already stands in for it with that look, else a slot
		 * with that look that nothing in the plan wants, else a new one. */
		for (s32 i = 0; i < s_Live.standin_count && use < 0; i++) {
			const live_standin_t *slot = &s_Live.standins[i];
			if (!taken[i] && slot->obj && slot->modelnum == modelnum && slot->extra_scale == scale &&
					strcmp(slot->record_id, d->record_id) == 0) {
				use = i;
			}
		}
		for (s32 i = 0; i < s_Live.standin_count && use < 0; i++) {
			const live_standin_t *slot = &s_Live.standins[i];
			s32 wanted = 0;
			if (taken[i] || !slot->obj || slot->modelnum != modelnum || slot->extra_scale != scale) continue;
			for (s32 k = 0; k < plan->draw_count && !wanted; k++) {
				wanted = slot->record_id[0] && strcmp(plan->draw[k].record_id, slot->record_id) == 0;
			}
			if (!wanted) use = i;
		}
		if (use < 0) {
			if (s_Live.standin_count >= LIVE_STANDINS) {
				liveLog("%s: no stand-in left on this stage (%d made)", d->record_id, LIVE_STANDINS);
				(*failed)++;
				continue;
			}
			obj = liveMakeStandin(s_Live.standin_count, modelnum, scale);
			if (!obj) {
				liveLog("%s: the stage could not make a stand-in with model %d", d->record_id, modelnum);
				(*failed)++;
				continue;
			}
			use = s_Live.standin_count++;
			s_Live.standins[use].obj = obj;
			s_Live.standins[use].modelnum = modelnum;
			s_Live.standins[use].extra_scale = scale;
		}
		taken[use] = 1;
		obj = s_Live.standins[use].obj;
		snprintf(s_Live.standins[use].record_id, sizeof(s_Live.standins[use].record_id), "%s", d->record_id);
		if (!livePlace(obj, d)) {
			if (obj->prop->flags & PROPFLAG_ENABLED) propDisable(obj->prop);
			s_Live.standins[use].record_id[0] = '\0';
			liveLog("%s: no room at (%.0f, %.0f, %.0f) to put it in", d->record_id, d->position[0],
				d->position[1], d->position[2]);
			(*failed)++;
			continue;
		}
		propEnable(obj->prop);
		drawn[j] = 1;
		(*drew)++;
		liveLog("drew %s with %s (model %d) at (%.0f, %.0f, %.0f)", d->record_id,
			from_stage ? "its look on the stage" : d->model_catalog_id, modelnum,
			obj->prop->pos.x, obj->prop->pos.y, obj->prop->pos.z);
	}

	/* Slots nothing took are hidden and kept for later. */
	for (s32 i = 0; i < s_Live.standin_count; i++) {
		live_standin_t *slot = &s_Live.standins[i];
		if (taken[i]) continue;
		slot->record_id[0] = '\0';
		if (slot->obj && slot->obj->prop && (slot->obj->prop->flags & PROPFLAG_ENABLED)) {
			propDisable(slot->obj->prop);
		}
	}
}

static void liveApply(const forge_level_live_plan_t *plan)
{
	s32 drawn[FORGE_LEVEL_LIVE_MAX];
	s32 want[FORGE_LEVEL_LIVE_MAX];
	s32 hid = 0;
	s32 drew = 0;
	s32 failed = 0;
	s32 shown = 0;
	s32 kept = 0;

	s_Live.log_lines = 0;
	liveDrawStandins(plan, drawn, &drew, &failed);

	/* An object to be hidden whose stand-in could not be drawn stays where
	 * the stage has it: the world never loses an object the level still has. */
	for (s32 j = 0; j < plan->hide_count; j++) {
		want[j] = 1;
		for (s32 k = 0; k < plan->draw_count; k++) {
			if (strcmp(plan->draw[k].record_id, plan->hide[j].record_id) == 0) {
				want[j] = drawn[k];
				break;
			}
		}
	}

	/* The stage's objects no longer to be hidden come back. */
	for (s32 i = 0; i < s_Live.hidden_count;) {
		s32 still = 0;
		for (s32 j = 0; j < plan->hide_count && !still; j++) {
			still = want[j] && plan->hide[j].command == s_Live.hidden[i].command;
		}
		if (still) {
			i++;
			continue;
		}
		if (liveUnhide(&s_Live.hidden[i])) {
			shown++;
			liveLog("showed %s again", s_Live.hidden[i].record_id);
		}
		s_Live.hidden[i] = s_Live.hidden[--s_Live.hidden_count];
	}

	/* The ones newly to be hidden go. */
	for (s32 j = 0; j < plan->hide_count; j++) {
		const forge_level_live_hide_t *e = &plan->hide[j];
		struct defaultobj *obj;
		s32 already = 0;

		if (!want[j]) {
			liveLog("%s: left where the stage has it, as its stand-in could not be drawn", e->record_id);
			continue;
		}
		for (s32 i = 0; i < s_Live.hidden_count && !already; i++) {
			already = s_Live.hidden[i].command == e->command;
		}
		if (already) {
			kept++;
			continue;
		}
		obj = setupGetObjByCmdIndex((u32)e->command);
		if (!obj) {
			liveLog("%s: the stage has no object at command %d", e->record_id, e->command);
			failed++;
			continue;
		}
		if (!liveKindMatches(e->kind, obj->type)) {
			liveLog("%s: command %d of the stage is object type %d, not a %s; left as it is", e->record_id,
				e->command, (s32)obj->type, e->kind);
			failed++;
			continue;
		}
		/* An object the stage does not show (left out on this difficulty, a
		 * weapon place the match left empty) has nothing to hide. */
		if (!obj->prop || (obj->prop->flags & PROPFLAG_ENABLED) == 0) continue;
		if (s_Live.hidden_count >= FORGE_LEVEL_LIVE_MAX) {
			failed++;
			continue;
		}
		liveBind();
		propDisable(obj->prop);
		snprintf(s_Live.hidden[s_Live.hidden_count].record_id, sizeof(s_Live.hidden[0].record_id), "%s",
			e->record_id);
		s_Live.hidden[s_Live.hidden_count].command = e->command;
		s_Live.hidden[s_Live.hidden_count].prop = obj->prop;
		s_Live.hidden_count++;
		hid++;
		liveLog("hid %s (command %d)", e->record_id, e->command);
	}

	s_Live.applied = 1;
	s_Live.off_reason[0] = '\0';
	sysLogPrintf(LOG_NOTE,
		"FORGE.LEVEL.LIVE: arena=%s hidden=%d drawn=%d shown_again=%d newly_hidden=%d not_shown=%d failed=%d stand_ins=%d",
		forgeLevelSessionArena(), s_Live.hidden_count, drew, shown, hid, plan->not_shown, failed,
		s_Live.standin_count);
	(void)kept;
}

void forgeLevelLiveTick(s32 editing)
{
	const forge_level_doc_t *doc;
	const forge_level_doc_t *opened;
	const forge_level_source_t *source;
	forge_level_live_plan_t *plan;
	char err[160];
	u32 generation;

	liveCheckStage();

	doc = forgeLevelSessionDoc();
	opened = forgeLevelSessionOpenedDoc();
	source = forgeLevelSessionSource();
	if (!editing || !doc) {
		liveShowStage(editing ? "no level file is open" : "the camera is not flying");
		return;
	}
	if (!opened || !source) {
		liveShowStage("no copy of the level as opened");
		liveOff("no copy of the level as it was opened");
		return;
	}
	/* The stage shows the level the session started on; the document must be
	 * that level for its rows to be the stage's objects. */
	if (strcmp(forgeLevelSessionArena(), source->arena_id) != 0) {
		liveShowStage("another level's file is open");
		liveOff("the level file open is not the level loaded");
		return;
	}
	generation = forgeLevelSessionGeneration();
	if (generation == s_Live.generation && (s_Live.applied || s_Live.refused)) return;
	s_Live.generation = generation;
	s_Live.refused = 0;

	plan = (forge_level_live_plan_t *)calloc(1, sizeof(*plan));
	if (!plan) {
		liveOff("out of memory");
		return;
	}
	err[0] = '\0';
	if (!forgeLevelLivePlan(opened, doc, plan, err, sizeof(err))) {
		liveShowStage("the level file and the stage differ");
		liveOff(err);
		s_Live.refused = 1;
		free(plan);
		return;
	}
	liveApply(plan);
	free(plan);
}
