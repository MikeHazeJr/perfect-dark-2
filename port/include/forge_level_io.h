/**
 * forge_level_io.h -- a level's placements in and out of the running game.
 *
 * forge_level_doc.h edits six texts and knows nothing about the game. This
 * is the part that does. It finds the archive a level in the catalog came
 * from, reads the six members out of the scenario inside it, and saves a
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
 * What is written depends on where the save lands. Under a new name, the
 * new level is the template's archive plus the members the document changed.
 * Under the name that gives the level's own id (a mod level opened and saved
 * again), the archive being rebuilt is the template itself, which after an
 * earlier save no longer is what the document was opened from; then every
 * member is written, so an edit taken back since is taken back in the file
 * too. The log line's `own_template` says which of the two it was.
 *
 * Returns MOD_AUTHOR_OK, or the mod author's error code with the reason in
 * `err`.
 */
s32 forgeLevelSaveAs(forge_level_doc_t *doc, const forge_level_source_t *source,
		const char *display_name, mod_author_result_t *result, char *err, size_t err_cap);

/*
 * The level Forge has open. One at a time, owned here so the editor's panel,
 * the debug flags and later the Modding Hub's Levels tool all work on the
 * same document.
 */

/**
 * A Forge session started on this arena ("" or NULL when the session is not
 * on a level from the catalog). Any document still open is closed: unsaved
 * placements do not follow the player into another session.
 */
void forgeLevelSessionBegin(const char *arena_id);

/* The arena the current Forge session was started on, or "". */
const char *forgeLevelSessionArena(void);

/**
 * Opens the placements of `arena_id` as the session's document, replacing
 * one that is open. 1 on success; 0 with the reason, and no document open.
 */
s32 forgeLevelSessionOpen(const char *arena_id, char *err, size_t err_cap);

/* The open document and where it came from, or NULL when none is open. */
forge_level_doc_t *forgeLevelSessionDoc(void);
const forge_level_source_t *forgeLevelSessionSource(void);

/* 1 when the open document differs from the level it was opened from. */
s32 forgeLevelSessionChanged(void);

/**
 * Saves the open document as the level mod `display_name` (see
 * forgeLevelSaveAs). The document stays open, still as its template plus the
 * edits, so saving again under the same name updates that level.
 */
s32 forgeLevelSessionSaveAs(const char *display_name, mod_author_result_t *result,
		char *err, size_t err_cap);

/* Closes the open document and drops its edits. */
void forgeLevelSessionClose(void);

/**
 * Starts a Forge session on the arena `arena_id`, the way choosing it in The
 * Grid does: a clean match setup on that level in the standard game mode,
 * then the stage change. Returns 1 when the session was requested.
 */
s32 forgeLevelStartForge(const char *arena_id);

/**
 * --debug-forge-level <arena id> with --debug-forge-level-name <name>: opens
 * that level, adds a player start a short step in front of its first one and
 * another spot for its first weapon pickup a short step in front of that
 * pickup, leaves its first ammo crate out, and saves the result as the level
 * `display_name`. With --debug-forge-level-weapon <weapon id> the copied
 * pickup is given that catalog weapon (`pickup_weapon_id`; NULL or "" leaves
 * it a weapon place like the one it was copied from). It is what a
 * smoke uses to prove an edited level loads; the Forge editor makes the
 * same open and save calls. Returns 1 when the level was saved and is in the
 * catalog.
 */
s32 forgeLevelDebugEdit(const char *arena_id, const char *display_name, const char *pickup_weapon_id);

/**
 * The model and the size a pickup of the weapon `weapon_catalog_id` is shown
 * with. For a weapon the match's weapon sets offer, its row in the weapon set
 * table (g_MpWeapons): what one of a level's weapon places becomes when a
 * match gives it that weapon. For any other weapon, the model it is held
 * with, at life size. 1 with the model's catalog id and the size (256 is life
 * size); 0 with the reason when the catalog has no such weapon or the game
 * has no model for it.
 */
s32 forgeLevelPickupLook(const char *weapon_catalog_id, char *out_model_id, size_t model_cap,
		s32 *out_extra_scale, char *err, size_t err_cap);

/**
 * Makes the pickup `record_id` of the open document give the catalog weapon
 * `weapon_catalog_id`, shown with that weapon's model and size
 * (forgeLevelPickupLook, then forgeLevelDocSetPickupWeapon). 1 on success; 0
 * with the reason, and the document unchanged.
 */
s32 forgeLevelSessionGivePickup(const char *record_id, const char *weapon_catalog_id,
		char *err, size_t err_cap);

/**
 * --debug-forge-session-save <name>, inside a Forge session started on a
 * level (--launch-forge-level <arena id>): opens that level's placements,
 * adds a player start at `position` facing `look` (the editor camera's), and
 * saves the result as the level `display_name` while the level is loaded and
 * the editor is up. It is what the editor's Add Start Here and Save As Level
 * buttons do, and it leaves the document open as they do. Returns 1 when the
 * level was saved and is in the catalog.
 */
s32 forgeLevelDebugSessionEdit(const char *display_name, const f32 position[3], const f32 look[3]);

#ifdef __cplusplus
}
#endif

#endif
