/**
 * mod_author_recipe.h -- a mod described in a file.
 *
 * A recipe is the request a creator screen builds when the player presses
 * Save, written down as JSON so the same mod can be made again from scratch:
 * by a test, by a smoke run, or by a modder who prefers a text editor.
 *
 *   {
 *     "schema": "pd2.mod_author.recipe.v1",
 *     "template": "base:falcon2",
 *     "name": "Rivet Carbine",
 *     "author": "Mike",
 *     "version": "1.0.0",
 *     "description": "Dock yard burst carbine.",
 *     "overrides": [
 *       { "entry": "behavior/settings.json", "file": "settings.json" },
 *       { "entry": "bindings/presentation.json", "text": "{ ... }" },
 *       { "entry": "dependencies/assets/ui/reticle.pdui", "remove": true }
 *     ]
 *   }
 *
 * `template` is the catalog id of the asset to start from. `template_file`
 * may be given instead: a typed archive beside the recipe. Exactly one of the
 * two is required, and `name`. Everything else is optional.
 *
 * An override replaces, adds or removes one archive member, with the rules
 * of assetArchiveRebuildFile ("outer.pdxxx::member" reaches into an embedded
 * archive). Its new content comes from `file` or from `text`.
 *
 * Reading is strict, so a typo is an error rather than a silent default: the
 * document goes through the shared strict JSON reader, unknown keys are
 * refused, and every value must have the right type. `template_file` and
 * `file` are paths relative to the folder the recipe is in and must stay
 * inside it (no drive, no leading slash, no ".." step).
 *
 * This module is pure: no catalog, no mod registry, no logging. Turning a
 * template id into an archive path and enabling the saved mod belong to the
 * caller (mod_author_runtime.h in the game).
 */
#ifndef _IN_MOD_AUTHOR_RECIPE_H
#define _IN_MOD_AUTHOR_RECIPE_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "mod_author.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_AUTHOR_RECIPE_SCHEMA "pd2.mod_author.recipe.v1"

/* Largest recipe document, and largest single override file, that is read. */
#define MOD_AUTHOR_RECIPE_MAX_BYTES          (1024u * 1024u)
#define MOD_AUTHOR_RECIPE_MAX_OVERRIDE_BYTES (64u * 1024u * 1024u)
/* The author adds the descriptor itself, so one slot stays free for it. */
#define MOD_AUTHOR_RECIPE_MAX_OVERRIDES      (ASSET_ARCHIVE_CLONE_MAX_OVERRIDES - 1)

/* Owns every string and every override byte it hands out. */
typedef struct mod_author_recipe mod_author_recipe_t;

/**
 * Read a recipe from memory. `recipe_dir` is the folder relative paths are
 * resolved against; override files are loaded now.
 *
 * Returns NULL and a short reason in `err` (optional) on any problem.
 */
mod_author_recipe_t *modAuthorRecipeParse(const void *json, size_t size,
		const char *recipe_dir, char *err, size_t err_cap);

/** Read a recipe file; relative paths resolve against the file's folder. */
mod_author_recipe_t *modAuthorRecipeLoadFile(const char *path, char *err, size_t err_cap);

void modAuthorRecipeFree(mod_author_recipe_t *recipe);

/** The template's catalog id, or "" when the recipe names a file instead. */
const char *modAuthorRecipeTemplateId(const mod_author_recipe_t *recipe);

/** The template archive's path, or "" when the recipe names a catalog id. */
const char *modAuthorRecipeTemplateFile(const mod_author_recipe_t *recipe);

/**
 * Fill `out` for modAuthorSave. `template_path` and `mods_root` depend on
 * where the game is installed, so the caller supplies them; pass NULL for
 * `template_path` to use the recipe's own `template_file`. Every pointer in
 * `out` stays valid until the recipe is freed.
 */
void modAuthorRecipeToRequest(const mod_author_recipe_t *recipe,
		const char *template_path, const char *mods_root,
		mod_author_request_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MOD_AUTHOR_RECIPE_H */
