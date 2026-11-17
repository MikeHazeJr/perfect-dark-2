/**
 * forge_level_live.h -- the level Forge has open, shown in the world while
 * the editor camera flies (Forge F3: objects are drawn while editing).
 *
 * The stage shows the level as it was loaded; the open document
 * (forge_level_io.h) holds the edits, which the HUD marks but the world did
 * not show. This keeps the world in step with the document:
 *
 * - An object of the loaded level that the document moved, redrew (another
 *   model, size or weapon) or left out is hidden: not drawn, not ticked,
 *   nothing collides with it (propDisable).
 * - An object the document added, and one it moved or redrew, is drawn
 *   where the document puts it by a stand-in: a plain object with the same
 *   model at the same size, set on the floor under its pad the way setup sets
 *   down every object, that anything can pass through. A stand-in is a
 *   picture, not the object: a weapon place's gives no weapon and a
 *   hoverbike's cannot be ridden.
 *
 * Playing the edits means playing the saved level, so the stage's own
 * objects come back and the stand-ins go when the camera stops flying
 * (Playtest), when the document closes and when the session ends. Nothing
 * here changes the document.
 *
 * The world never hides an object without drawing its stand-in, when the
 * object is still in the level: an object it cannot draw is left where the
 * stage has it, and the HUD's marker shows where the file puts it.
 *
 * A row of the document is matched to the stage's object by its record id:
 * the id's number is the object's place in the level's command list, which
 * the document checks when it reads a level (forge_level_doc.h). The world
 * half checks each match against the object's type before hiding anything.
 *
 * forgeLevelLivePlan is pure (no game state) and unit-tested in pd-tests;
 * forgeLevelLiveTick is the client's half.
 */
#ifndef _IN_FORGE_LEVEL_LIVE_H
#define _IN_FORGE_LEVEL_LIVE_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "forge_level_doc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The most objects one plan hides, and the most it draws. Edits past either
 * are counted in `not_shown` and left to the markers. */
#define FORGE_LEVEL_LIVE_MAX 128

typedef struct forge_level_live_hide {
	char record_id[32];
	char kind[32];
	s32 command;              /* its place in the level's command list */
} forge_level_live_hide_t;

typedef struct forge_level_live_draw {
	char record_id[32];
	char kind[32];
	char model_catalog_id[64];  /* "" for an object only moved whose record names none */
	f32 position[3];          /* its pad's */
	f32 look[3];
	f32 up[3];
	s32 extra_scale;          /* 256 is life size */
	s32 looks_like_command;   /* -1, or for an object that was only moved, its place in
	                           * the command list: the stand-in takes that object's own
	                           * model and size from the stage, which for one of the
	                           * match's weapon places is the weapon the match put there */
} forge_level_live_draw_t;

typedef struct forge_level_live_plan {
	s32 hide_count;
	forge_level_live_hide_t hide[FORGE_LEVEL_LIVE_MAX];
	s32 draw_count;
	forge_level_live_draw_t draw[FORGE_LEVEL_LIVE_MAX];
	s32 not_shown;            /* changed objects this cannot show: a kind it does not
	                           * draw, no model, no pad, or past the limits */
} forge_level_live_plan_t;

/* 1 for a kind of object the world can hide and draw a stand-in for: the
 * kinds the stage makes as one model on a pad (props, weapon pickups, ammo
 * crates, hoverbikes and the like), not characters, doors, lifts or the
 * level's bookkeeping records. */
s32 forgeLevelLiveKindShown(const char *kind);

/*
 * What the world should show for the document `now`, given the document as
 * it was opened (`opened`), which is what the stage loaded. Rows are matched
 * by place: the document only ever adds objects at the end, so row i of
 * `opened` is row i of `now` with the same id. Fills `out` and returns 1;
 * returns 0 with the reason when the two do not have that shape (then the
 * world should show nothing of the document).
 */
s32 forgeLevelLivePlan(const forge_level_doc_t *opened, const forge_level_doc_t *now,
		forge_level_live_plan_t *out, char *err, size_t err_cap);

/*
 * The client's half: call once a frame. `editing` is 1 while a Forge session
 * is in the flying camera. While it is, and the open document is of the level
 * the session is on, the world follows the document; it is brought up to date
 * when the document changes (forgeLevelSessionGeneration). Otherwise the
 * stage's own objects are shown again and the stand-ins hidden. A stage
 * change forgets everything (the stage's memory is gone). Logs
 * FORGE.LEVEL.LIVE lines.
 */
void forgeLevelLiveTick(s32 editing);

#ifdef __cplusplus
}
#endif

#endif
