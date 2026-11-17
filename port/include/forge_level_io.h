/**
 * forge_level_io.h -- a level's placements in and out of the running game.
 *
 * forge_level_doc.h edits five texts and knows nothing about the game. This
 * is the part that does. It finds the archive a level in the catalog came
 * from, reads the five members out of the scenario inside it, and saves a
 * changed document as a level mod through the mod author, the same save the
 * other in-game creators use (context/designs/modding/forge-mode-scope.md,
 * step F2). Every step is logged as a FORGE.LEVEL.* line.
 */
#ifndef _IN_FORGE_LEVEL_IO_H
#define _IN_FORGE_LEVEL_IO_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "forge_level_doc.h"
#include "mod_author.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where an open level came from, which is what a save starts from. */
typedef struct forge_level_source {
	char arena_id[CATALOG_ID_LEN];
	char archive_path[MOD_AUTHOR_PATH_MAX];      /* the .pdarena on disk */
	char scenario_member[MOD_AUTHOR_PATH_MAX];   /* its scenario, as the arena names it */
} forge_level_source_t;

/**
 * Opens the placements of the level `arena_id`, an arena in the catalog: a
 * base arena or a mod level. Returns the document (free it with
 * forgeLevelDocFree) and fills `source`, or returns NULL with the reason in
 * `err` when the arena is unknown, keeps its scenario outside its own
 * archive, or holds files the document refuses.
 */
forge_level_doc_t *forgeLevelOpen(const char *arena_id, forge_level_source_t *source,
		char *err, size_t err_cap);

/**
 * Saves the document as the level mod `display_name`, made from the level it
 * was opened from plus what the document changed, and makes it active: it is
 * in the catalog under `result->catalog_id` when this returns. A level whose
 * template is a base arena holds data from the player's own game files and
 * is local only.
 *
 * Returns MOD_AUTHOR_OK, or the mod author's error code with the reason in
 * `err`.
 */
s32 forgeLevelSaveAs(forge_level_doc_t *doc, const forge_level_source_t *source,
		const char *display_name, mod_author_result_t *result, char *err, size_t err_cap);

/**
 * --debug-forge-level <arena id> with --debug-forge-level-name <name>: opens
 * that level, adds a player start a short step in front of its first one and
 * another spot for its first weapon pickup a short step in front of that
 * pickup, and saves the result as the level `display_name`. It is what a
 * smoke uses to prove an edited level loads; the Forge editor will make the
 * same open and save calls. Returns 1 when the level was saved and is in the
 * catalog.
 */
s32 forgeLevelDebugEdit(const char *arena_id, const char *display_name);

#ifdef __cplusplus
}
#endif

#endif
