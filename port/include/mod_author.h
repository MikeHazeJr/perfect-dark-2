/**
 * mod_author.h -- turn a template archive plus edits into a complete mod.
 *
 * This is what the in-game creators call when the player presses Save. A
 * request names a template typed archive (a base weapon, a body, a vehicle),
 * a display name and optional member overrides. The result is a mod folder
 * the mod manager can discover, enable and pack:
 *
 *   <mods_root>/<Category>/<kind>_<slug>/
 *       mod.json                 id "mod.<kind>_<slug>", validated
 *       <kind>_<slug>.pd<kind>   catalog id "mod:<kind>_<slug>", rebuilt from
 *                                the template through assetArchiveRebuildFile
 *
 * `<slug>` is the display name in lower-case ASCII letters and digits with
 * every other run of characters turned into one underscore. For names made of
 * letters, digits and single spaces this is the naming the Weapons tool has
 * always used ("Rivet Carbine" is "mod:weapon_rivet_carbine"). A name too
 * long for an id is shortened and told apart by a hash, so two different
 * names never share a folder.
 *
 * Saving again under the same display name updates that mod in place. The new
 * archive is staged beside the old one, under a name the catalog scanner does
 * not pick up, and only replaces it once it has passed release validation, so
 * a failed or interrupted save never damages a working mod. A folder that
 * already holds a different mod, or an archive nobody claims, is never
 * touched.
 *
 * Text limits follow what the loaders keep, so nothing is cut on load:
 * display name 1 to 63 bytes, author up to 63, version up to 31, description
 * up to 255. Each is one line of valid UTF-8 with no control characters.
 *
 * Pure with respect to game state: no catalog, no mod registry, no logging.
 * The caller rescans and enables the mod afterwards.
 */
#ifndef _IN_MOD_AUTHOR_H
#define _IN_MOD_AUTHOR_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "asset_archive_clone.h"
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_AUTHOR_OK             0
#define MOD_AUTHOR_ERR_ARGS      -1   /* bad request */
#define MOD_AUTHOR_ERR_TEMPLATE  -2   /* template unreadable or not a supported family */
#define MOD_AUTHOR_ERR_EXISTS    -3   /* the mod folder belongs to something else */
#define MOD_AUTHOR_ERR_ARCHIVE   -4   /* the archive could not be rebuilt or failed validation */
#define MOD_AUTHOR_ERR_WRITE     -5   /* folder or file could not be written */

#define MOD_AUTHOR_PATH_MAX      1024
#define MOD_AUTHOR_NAME_MAX      64   /* display name bytes, terminator included */
#define MOD_AUTHOR_ID_MAX        64

typedef struct mod_author_request {
	const char *template_path;   /* typed archive to start from */
	const char *mods_root;       /* existing directory that holds mods, e.g. "mods" */
	const char *display_name;    /* shown to players; also determines the ids */
	const char *author;          /* optional, default "Player" */
	const char *version;         /* optional, default "1.0.0" */
	const char *description;     /* optional */
	const char *tool;            /* optional provenance tool name */
	const asset_archive_clone_override_t *overrides;   /* optional edits */
	u32 override_count;
} mod_author_request_t;

typedef struct mod_author_result {
	char kind[32];                          /* "weapon", "character", "vehicle", ... */
	char slug[MOD_AUTHOR_ID_MAX];           /* "weapon_rivet_carbine" */
	char mod_id[MOD_AUTHOR_ID_MAX];         /* "mod.weapon_rivet_carbine" */
	char catalog_id[CATALOG_ID_LEN];        /* "mod:weapon_rivet_carbine" */
	char mod_dir[MOD_AUTHOR_PATH_MAX];      /* <mods_root>/<Category>/<slug> */
	char archive_path[MOD_AUTHOR_PATH_MAX];
	char manifest_path[MOD_AUTHOR_PATH_MAX];
	s32 updated;                            /* 1 when this mod already existed */
	asset_archive_clone_report_t ids;       /* template id to mod id map (Save only) */
} mod_author_result_t;

/**
 * What a request would produce, without writing anything. Lets a creator show
 * the id and say "new mod" or "replaces your existing mod" before the player
 * confirms.
 *
 * `result` receives kind, slug, mod_id, catalog_id, the three paths and
 * `updated`. Returns MOD_AUTHOR_ERR_EXISTS when the folder belongs to
 * something else, exactly as Save would.
 */
s32 modAuthorPlan(const mod_author_request_t *request, mod_author_result_t *result,
		char *err, size_t err_cap);

/**
 * Create or update the mod described by `request`.
 *
 * The display name is written into the archive's descriptor unless an
 * override replaces the descriptor itself. Overrides follow the rules of
 * assetArchiveRebuildFile.
 *
 * Returns MOD_AUTHOR_OK or a MOD_AUTHOR_ERR_* code. `result` (optional) is
 * filled on success and zeroed on failure. `err` (optional) receives a short
 * reason on failure. On failure nothing this call created is left behind.
 */
s32 modAuthorSave(const mod_author_request_t *request, mod_author_result_t *result,
		char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MOD_AUTHOR_H */
