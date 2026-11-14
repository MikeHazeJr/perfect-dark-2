/**
 * mod_author_runtime.h -- the game-side half of the mod author.
 *
 * mod_author.h writes a mod folder and knows nothing about the running game.
 * This module is the part that does: it turns a catalog id into the archive
 * that asset was loaded from, saves into the game's own mods folder, tells
 * the mod manager about the saved mod, enables it and applies the change, so
 * the new asset is in the catalog without a restart.
 *
 * The creator screens and the --mod-author boot flag both go through
 * modAuthorRuntimeSave, so what a scripted run proves is what a player gets.
 * Every step is logged as a MODAUTHOR.* line.
 */
#ifndef _IN_MOD_AUTHOR_RUNTIME_H
#define _IN_MOD_AUTHOR_RUNTIME_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "mod_author.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Path of the typed archive the catalog asset `catalog_id` comes from: a base
 * archive under the extracted data folder, or a mod's archive.
 *
 * Returns 1 and the path, or 0 and a short reason in `err` (optional) when
 * the id is unknown or its archive is not a file on disk.
 */
s32 modAuthorRuntimeTemplatePath(const char *catalog_id, char *out, size_t out_cap,
		char *err, size_t err_cap);

/**
 * Make a saved mod active: rescan the mods folder, enable `mod_id` and apply.
 * `restart_count` (optional) receives how many package changes still need a
 * restart. Returns 1 on success, 0 with a reason in `err` (optional).
 */
s32 modAuthorRuntimeEnable(const char *mod_id, s32 *restart_count,
		char *err, size_t err_cap);

/**
 * Save `request` into the game's mods folder and make it active.
 * `request->mods_root` is ignored: the mod manager's folder is used.
 *
 * Returns MOD_AUTHOR_OK when the mod was saved and enabled. A save that
 * worked but could not be enabled returns MOD_AUTHOR_ERR_WRITE with `result`
 * still describing what was written.
 */
s32 modAuthorRuntimeSave(const mod_author_request_t *request, mod_author_result_t *result,
		s32 *restart_count, char *err, size_t err_cap);

/**
 * Run one recipe file (mod_author_recipe.h) through modAuthorRuntimeSave.
 * Returns 1 when the mod was saved, enabled and is in the catalog.
 */
s32 modAuthorRuntimeRunRecipe(const char *recipe_path);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MOD_AUTHOR_RUNTIME_H */
