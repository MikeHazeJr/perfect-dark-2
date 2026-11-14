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
