/**
 * level_tool.h -- the rules of the Modding Hub's Levels tool.
 *
 * The tool lists the catalog's levels, says what each holds, makes a new level
 * from one of them under a name of its own, and opens a level in Forge
 * (context/designs/modding/forge-mode-scope.md, step F4). The window is
 * port/fast3d/pdgui_menu_leveltool.cpp; this module is what it decides, kept
 * apart from the drawing so pd-tests can prove it: what a level's placements
 * add up to, how that is said in one line, and whether a name may be used for
 * a new level.
 *
 * A new level never replaces anything. The mod author's save updates a mod in
 * place when the name gives the id of one the player already has; that is
 * right for Forge's own Save, and wrong for "make a new level from this
 * template", which would put a fresh copy of the template over a level the
 * player has worked on. So the tool refuses such a name and says which level
 * has it.
 *
 * Pure: no catalog, no files, no logging.
 */
#ifndef _IN_LEVEL_TOOL_H
#define _IN_LEVEL_TOOL_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "forge_level_doc.h"
#include "mod_author.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What a level's placements add up to. */
typedef struct level_tool_summary {
	s32 pads;
	s32 starts;          /* player starts (spawns.json rows) */
	s32 objects;         /* every object record, the ones left out included */
	s32 weapon_places;   /* weapon pickups a match's weapon set fills (places 0 to 15) */
	s32 fixed_weapons;   /* weapon pickups that always give one weapon */
	s32 ammo_crates;
	s32 props;
	s32 other;           /* every other kind of record (doors, glass, tags, ...) */
	s32 left_out;        /* records the level leaves out on every difficulty */
} level_tool_summary_t;

/* Counts a level's placements. A record that is left out counts in `left_out`
 * and in `objects` only, not under its kind: it is not in the level a player
 * sees. A NULL doc gives all zeros. */
void levelToolSummarise(const forge_level_doc_t *doc, level_tool_summary_t *out);

/*
 * One line for the tool's detail pane, e.g.
 * "8 starts, 12 weapon places, 1 fixed weapon, 3 ammo crates, 40 props,
 * 25 other objects, 2 left out". Kinds with a count of zero are left out of
 * the line, except the starts, whose absence is what a player needs to know:
 * "no starts". Always terminated; cut at cap.
 */
void levelToolDescribe(const level_tool_summary_t *summary, char *out, size_t cap);

/* Whether a name may be used for a new level made from a template. */
#define LEVEL_TOOL_NAME_OK        0
#define LEVEL_TOOL_NAME_EMPTY     1 /* no name yet (nothing to say) */
#define LEVEL_TOOL_NAME_REFUSED   2 /* the mod author refuses it; why is its reason */
#define LEVEL_TOOL_NAME_TEMPLATE  3 /* it is the template's own name */
#define LEVEL_TOOL_NAME_TAKEN     4 /* a level of the player's already has it */

/*
 * `plan_code` and `plan` are the mod author's answer for the name
 * (modAuthorPlan on the template's archive and the name): its code, and on
 * MOD_AUTHOR_OK the id the name gives and whether a mod with that id is
 * already there (`updated`). `plan_reason` is the author's reason when it
 * refused. `template_id` is the catalog id of the level the copy starts from.
 * Returns a LEVEL_TOOL_NAME_* and, for every answer but OK and EMPTY, a
 * sentence for the player in `why`.
 */
s32 levelToolNewLevelName(const char *display_name, const char *template_id, s32 plan_code,
		const mod_author_result_t *plan, const char *plan_reason, char *why, size_t why_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_LEVEL_TOOL_H */
