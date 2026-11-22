/**
 * weapon_graph_editor_io.h -- between the Weapons creator's graph and the
 * clean `.pdweapon` behavior files.
 *
 * The node editor works on one graph document: every node carries the
 * `subgraph` it belongs to ("primary", "secondary" or "shared"), with one
 * list of edges, exports and shared contexts. A clean weapon archive stores
 * the same thing as three files:
 *
 *   behavior/primary.graph.json      primary and shared nodes, the "primary" export
 *   behavior/secondary.graph.json    secondary nodes, the "secondary" export
 *   behavior/shared-context.json     the contexts both modes share
 *
 * Split turns the editor document into those files; Compose turns the files
 * of a template back into an editor document. Both are pure text to text.
 *
 * What Split guarantees, because the loader requires it of each mode file: a
 * non-empty `nodes` list and a non-empty `exports` list.
 *
 *   - A mode with no export gets one: its first node the game can run as a
 *     function. When it has none, a `function.empty` node is added and
 *     exported, which is how the base game writes a mode that does nothing.
 *   - A mode with an export but no node of its own gets the same
 *     `function.empty` node, unexported.
 *   - Every node keeps its `subgraph`, so a shared node stays shared.
 *   - Every link is kept. The loader joins both files before it resolves
 *     links, so a link may name a node from the other file; it is stored
 *     with the secondary mode as soon as either end belongs there.
 *
 * Node positions are not stored in the clean files. Numbers are written in
 * the shortest form that reads back to the same value, and the keys of
 * `params` come out in alphabetical order.
 */
#ifndef _IN_WEAPON_GRAPH_EDITOR_IO_H
#define _IN_WEAPON_GRAPH_EDITOR_IO_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct weapon_graph_editor_split {
	char *primary;          /* behavior/primary.graph.json */
	char *secondary;        /* behavior/secondary.graph.json */
	char *shared_context;   /* behavior/shared-context.json, or NULL when the
	                         * editor document lists no contexts, which means
	                         * "keep the template's file" */
} weapon_graph_editor_split_t;

/**
 * Split an editor graph document. `asset_id` is written as each file's
 * `asset_id`. Returns 1 and malloc'd texts in `out`, or 0 with a reason in
 * `err` (optional) and `out` zeroed.
 */
s32 weaponGraphEditorSplit(const char *editor_json, const char *asset_id,
		weapon_graph_editor_split_t *out, char *err, size_t err_cap);

void weaponGraphEditorSplitFree(weapon_graph_editor_split_t *split);

/**
 * Compose an editor graph document from a template's clean files.
 * `shared_context_json` may be NULL. Returns malloc'd text, or NULL with a
 * reason in `err` (optional).
 */
char *weaponGraphEditorCompose(const char *primary_json, const char *secondary_json,
		const char *shared_context_json, const char *asset_id,
		char *err, size_t err_cap);

/*
 * held-weapon-events.md slice 8a: the creator's designer nodes. Every node a
 * function's program can hold (weapon_rules.h: the weapon events, the gates
 * and the modules), in palette order, with the name and the group a designer
 * sees it under. A record node (a function's own, `fire.hitscan`) is none of
 * these.
 */
typedef struct weapon_graph_designer_node {
	const char *kind;   /* "og.weapon.automatic" */
	const char *label;  /* "Automatic" */
	const char *group;  /* "Events", "Checks", "Trigger", "Ammo", "Shot", "Aim", "Feel" or "State" */
} weapon_graph_designer_node_t;

s32 weaponGraphDesignerNodeCount(void);
const weapon_graph_designer_node_t *weaponGraphDesignerNodeAt(s32 index);
/* slice 8b: the designer nodes of a projectile's program (projectile_rules.h:
 * its events, gates and modules), for the creator's projectile canvas. */
s32 weaponGraphProjectileDesignerNodeCount(void);
const weapon_graph_designer_node_t *weaponGraphProjectileDesignerNodeAt(s32 index);
/* A weapon's or a projectile's designer node; NULL when the kind is not a
 * program node. */
const weapon_graph_designer_node_t *weaponGraphDesignerNodeFor(const char *kind);

/*
 * A node's execution ports. A program node (a weapon's or a projectile's)
 * other than an event takes an execution input, and its outputs are "out", a
 * gate's "pass" and "fail". A record node keeps the one unnamed input and
 * output of its record links.
 */
s32 weaponGraphNodeIsProgram(const char *kind);
s32 weaponGraphNodeTakesExec(const char *kind);
s32 weaponGraphNodeOutputCount(const char *kind);
/* The output's port: "" for a record node's; NULL past the last. */
const char *weaponGraphNodeOutputName(const char *kind, s32 index);

/* held-weapon-events.md slice 8a: what a designer node's setting takes. */
#define WEAPON_GRAPH_INPUT_WHOLE  1 /* a whole number from lo to hi */
#define WEAPON_GRAPH_INPUT_NUMBER 2 /* a number up to hi: above lo, or from lo when lo_included */
#define WEAPON_GRAPH_INPUT_SWITCH 3 /* true or false */
#define WEAPON_GRAPH_INPUT_CHOICE 4 /* one of `choices` ('|' between; a choice of digits is a number) */
#define WEAPON_GRAPH_INPUT_ID     5 /* a catalog id (namespace:name), or one of `choices` */
#define WEAPON_GRAPH_INPUT_NAME   6 /* a name: a state's key */
#define WEAPON_GRAPH_INPUT_KINDS  7 /* some of `choices`, written with commas between */

typedef struct weapon_graph_designer_setting {
	const char *key;      /* "rate" */
	const char *label;    /* "Rate (rounds per minute)" */
	s32 input;            /* WEAPON_GRAPH_INPUT_* */
	f32 lo;
	f32 hi;
	s32 lo_included;      /* a NUMBER's: whether lo itself is allowed */
	const char *choices;  /* a CHOICE's, an ID's and KINDS' */
} weapon_graph_designer_setting_t;

/*
 * A designer node's settings, in the order the inspector lists them: the
 * rule table's for a module (weaponRulesSettingAt), has_ammo's rounds,
 * state_is's and set_state's key and value, and the lock's target filter;
 * slice 8b: a projectile module's (the thrust's, the trail's, the hit
 * sound's, the bounce smoke's, the explosion's, the home's and the
 * proximity arm's target filter).
 * Fills up to `cap` of `out` (which may be NULL) and returns how many there
 * are.
 */
s32 weaponGraphDesignerSettings(const char *kind, weapon_graph_designer_setting_t *out, s32 cap);

/* A target filter's "kinds" text (PR 49 review) as the inspector's
 * checkboxes take it: bit i set when it names `choices`' i-th kind, its groups
 * expanded as the filter reads them ("characters" names player, simulant and
 * guard; "all" every kind). 0 when the text is not a kinds list. */
u32 weaponGraphDesignerKindsMask(const char *text, const char *choices);

/*
 * held-weapon-events.md slice 8b: one node's settings in a graph's text (a
 * projectile's program, edited outside the canvas): the members it carries
 * besides id, kind, subgraph and params, written as the editor keeps them
 * ("\"kinds\": \"vehicle\", ..."; "" for none). Returns malloc'd text, or
 * NULL with a reason when the graph does not parse or has no such node.
 */
char *weaponGraphEditorNodeSettings(const char *graph_json, const char *node_id, char *err, size_t err_cap);
/* The graph's text with that node's settings replaced by `settings` (the
 * same form; "" for none) and everything else kept. Returns malloc'd text,
 * or NULL with a reason. */
char *weaponGraphEditorSetNodeSettings(const char *graph_json, const char *node_id, const char *settings,
		char *err, size_t err_cap);

#ifdef __cplusplus
}

#include <string>
namespace crude_json { struct value; }

/**
 * A value as the editor shows it. An object has one member per line with
 * everything inside a member on that line; any other value is one line.
 * Numbers are written in the shortest form that reads back to the same
 * value, the way Split writes them.
 */
std::string weaponGraphEditorValueText(const crude_json::value &value);
#endif

#endif /* _IN_WEAPON_GRAPH_EDITOR_IO_H */
