/**
 * mod_author_runtime.c -- the game-side half of the mod author.
 *
 * See mod_author_runtime.h for the contract.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "asset_archive_policy.h"
#include "assetcatalog.h"
#include "fs.h"
#include "mod_author.h"
#include "mod_author_recipe.h"
#include "mod_author_runtime.h"
#include "modmgr.h"
#include "modmgr_apply.h"
#include "system.h"

/* Where each family's base archives live under the extracted data folder. */
typedef struct runtime_family {
	asset_type_e type;
	const char *dir;
	const char *ext;
} runtime_family_t;

static const runtime_family_t s_Families[] = {
	{ ASSET_WEAPON,    "weapons",    ".pdweapon" },
	{ ASSET_CHARACTER, "characters", ".pdcharacter" },
	{ ASSET_BODY,      "bodies",     ".pdbody" },
	{ ASSET_HEAD,      "heads",      ".pdhead" },
	{ ASSET_VEHICLE,   "vehicles",   ".pdvehicle" },
	{ ASSET_ARENA,     "arenas",     ".pdarena" },
};

static s32 runtimeFail(char *err, size_t err_cap, const char *fmt, ...)
{
	if (err && err_cap) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(err, err_cap, fmt, ap);
		va_end(ap);
		err[err_cap - 1] = '\0';
	}
	return 0;
}

static s32 runtimeFileExists(const char *path)
{
	FILE *f = (path && path[0]) ? fopen(path, "rb") : NULL;
	if (!f) return 0;
	fclose(f);
	return 1;
}

static s32 runtimeIsAbsolute(const char *path)
{
	return path[0] == '/' || path[0] == '\\' ||
		(path[0] && path[1] == ':' && (path[2] == '/' || path[2] == '\\'));
}

/* `candidate` as an existing typed archive file, absolute or game-relative. */
static s32 runtimeTypedFile(const char *candidate, size_t len, char *out, size_t out_cap)
{
	char path[FS_MAXPATH + 1];
	char full[FS_MAXPATH + 1];
	const char *resolved;

	if (len == 0 || len >= sizeof(path)) return 0;
	memcpy(path, candidate, len);
	path[len] = '\0';
	if (!assetArchivePathIsTyped(path)) return 0;

	resolved = path;
	if (!runtimeFileExists(resolved)) {
		if (runtimeIsAbsolute(path)) return 0;
		resolved = fsFullPath(path, full, sizeof(full));
		if (!resolved || !runtimeFileExists(resolved)) return 0;
	}
	if (strlen(resolved) >= out_cap) return 0;
	snprintf(out, out_cap, "%s", resolved);
	return 1;
}

s32 modAuthorRuntimeTemplatePath(const char *catalog_id, char *out, size_t out_cap,
		char *err, size_t err_cap)
{
	const asset_entry_t *entry;
	const char *member;

	if (err && err_cap) err[0] = '\0';
	if (!out || out_cap == 0) return runtimeFail(err, err_cap, "no room for the path");
	out[0] = '\0';
	if (!catalog_id || !catalog_id[0]) return runtimeFail(err, err_cap, "no template id");

	entry = assetCatalogResolve(catalog_id);
	if (!entry) {
		return runtimeFail(err, err_cap, "%s is not in the catalog", catalog_id);
	}

	/* A scanned archive records "<archive>::<descriptor>". */
	member = strstr(entry->descriptor_path, "::");
	if (member && runtimeTypedFile(entry->descriptor_path,
			(size_t)(member - entry->descriptor_path), out, out_cap)) {
		return 1;
	}
	if (runtimeTypedFile(entry->dirpath, strlen(entry->dirpath), out, out_cap)) {
		return 1;
	}

	/* A base asset: "<data>/<family>/<id with ':' as '_'>.<ext>". */
	for (size_t i = 0; i < sizeof(s_Families) / sizeof(s_Families[0]); i++) {
		char leaf[CATALOG_ID_LEN];
		char rel[FS_MAXPATH];
		char data[FS_MAXPATH];
		size_t n;

		if (s_Families[i].type != entry->type) continue;
		n = strlen(entry->id);
		if (n >= sizeof(leaf)) break;
		for (size_t k = 0; k <= n; k++) {
			leaf[k] = (entry->id[k] == ':') ? '_' : entry->id[k];
		}
		snprintf(rel, sizeof(rel), "%s/%s%s", s_Families[i].dir, leaf, s_Families[i].ext);
		fsDataPathFor(rel, data, sizeof(data));
		if (runtimeTypedFile(data, strlen(data), out, out_cap)) return 1;
		return runtimeFail(err, err_cap, "%s has no archive at %s", catalog_id, data);
	}
	return runtimeFail(err, err_cap, "%s is not an asset a creator can start from", catalog_id);
}

s32 modAuthorRuntimeEnable(const char *mod_id, s32 *restart_count,
		char *err, size_t err_cap)
{
	modmgr_apply_result_t apply;
	s32 count;
	s32 index = -1;

	if (err && err_cap) err[0] = '\0';
	if (restart_count) *restart_count = 0;
	if (!mod_id || !mod_id[0]) return runtimeFail(err, err_cap, "no mod id");

	modmgrRescanDirectory();
	count = modmgrGetCount();
	for (s32 i = 0; i < count; i++) {
		const char *id = modmgrGetModId(i);
		if (id && strcmp(id, mod_id) == 0) {
			index = i;
			break;
		}
	}
	if (index < 0) {
		return runtimeFail(err, err_cap, "the mod manager did not find %s", mod_id);
	}
	if (!modmgrGetModValid(index)) {
		const char *why = modmgrGetModValidationError(index);
		return runtimeFail(err, err_cap, "the mod manager rejected %s: %s", mod_id,
			(why && why[0]) ? why : "invalid mod.json");
	}
	modmgrSetEnabled(index, 1);
	if (!modmgrGetModEnabled(index)) {
		return runtimeFail(err, err_cap, "the mod manager could not enable %s", mod_id);
	}

	memset(&apply, 0, sizeof(apply));
	if (!modmgrApplyChangesChecked(1, &apply)) {
		return runtimeFail(err, err_cap, "enabled, but applying it failed: %s",
			apply.error[0] ? apply.error : "no detail");
	}
	if (restart_count) *restart_count = apply.restart_count;
	return 1;
}

s32 modAuthorRuntimeSave(const mod_author_request_t *request, mod_author_result_t *result,
		s32 *restart_count, char *err, size_t err_cap)
{
	mod_author_request_t local;
	mod_author_result_t *saved;
	const char *mods_root = modmgrGetModsDir();
	char why[256];
	s32 code;

	if (err && err_cap) err[0] = '\0';
	if (result) memset(result, 0, sizeof(*result));
	if (restart_count) *restart_count = 0;
	if (!request) {
		runtimeFail(err, err_cap, "no request");
		return MOD_AUTHOR_ERR_ARGS;
	}
	if (!mods_root || !mods_root[0]) {
		runtimeFail(err, err_cap, "the game has no mods folder");
		sysLogPrintf(LOG_WARNING, "MODAUTHOR.SAVE.FAIL code=%d reason='no mods folder'",
			MOD_AUTHOR_ERR_WRITE);
		return MOD_AUTHOR_ERR_WRITE;
	}

	saved = (mod_author_result_t *)calloc(1, sizeof(*saved));
	if (!saved) {
		runtimeFail(err, err_cap, "out of memory");
		return MOD_AUTHOR_ERR_WRITE;
	}
	local = *request;
	local.mods_root = mods_root;

	why[0] = '\0';
	code = modAuthorSave(&local, saved, why, sizeof(why));
	if (code != MOD_AUTHOR_OK) {
		sysLogPrintf(LOG_WARNING, "MODAUTHOR.SAVE.FAIL code=%d reason='%s'", code, why);
		runtimeFail(err, err_cap, "%s", why);
		free(saved);
		return code;
	}
	sysLogPrintf(LOG_NOTE,
		"MODAUTHOR.SAVE.OK kind=%s catalog=%s mod_id=%s updated=%d archives=%u ids=%u archive='%s'",
		saved->kind, saved->catalog_id, saved->mod_id, saved->updated,
		(unsigned)saved->ids.archive_count, (unsigned)saved->ids.id_count,
		saved->archive_path);
	if (result) *result = *saved;

	why[0] = '\0';
	{
		s32 restarts = 0;
		if (!modAuthorRuntimeEnable(saved->mod_id, &restarts, why, sizeof(why))) {
			sysLogPrintf(LOG_WARNING, "MODAUTHOR.ENABLE.FAIL mod_id=%s reason='%s'",
				saved->mod_id, why);
			runtimeFail(err, err_cap, "saved, but not active: %s", why);
			free(saved);
			return MOD_AUTHOR_ERR_WRITE;
		}
		if (restart_count) *restart_count = restarts;
		sysLogPrintf(LOG_NOTE, "MODAUTHOR.ENABLE.OK mod_id=%s restart_count=%d",
			saved->mod_id, restarts);
	}
	free(saved);
	return MOD_AUTHOR_OK;
}

s32 modAuthorRuntimeRunRecipe(const char *recipe_path)
{
	mod_author_recipe_t *recipe;
	mod_author_request_t request;
	mod_author_result_t *result;
	const asset_entry_t *entry;
	char template_path[FS_MAXPATH + 1];
	char err[256];
	const char *template_id;
	s32 restarts = 0;
	s32 code;
	s32 ok = 0;

	/* A relative recipe path is tried as given, then against the game's
	 * folders, the way every other file argument is. */
	if (recipe_path && recipe_path[0] && !runtimeFileExists(recipe_path) &&
			!runtimeIsAbsolute(recipe_path)) {
		static char resolved[FS_MAXPATH + 1];
		const char *full = fsFullPath(recipe_path, resolved, sizeof(resolved));
		if (full && runtimeFileExists(full)) recipe_path = full;
	}
	sysLogPrintf(LOG_NOTE, "MODAUTHOR.RECIPE.BEGIN path='%s'", recipe_path ? recipe_path : "");
	err[0] = '\0';
	recipe = modAuthorRecipeLoadFile(recipe_path, err, sizeof(err));
	if (!recipe) {
		sysLogPrintf(LOG_WARNING, "MODAUTHOR.RECIPE.RESULT result=FAIL stage=read reason='%s'", err);
		return 0;
	}
	result = (mod_author_result_t *)calloc(1, sizeof(*result));
	if (!result) {
		sysLogPrintf(LOG_WARNING, "MODAUTHOR.RECIPE.RESULT result=FAIL stage=memory reason=''");
		modAuthorRecipeFree(recipe);
		return 0;
	}

	template_path[0] = '\0';
	template_id = modAuthorRecipeTemplateId(recipe);
	if (template_id[0]) {
		if (!modAuthorRuntimeTemplatePath(template_id, template_path, sizeof(template_path),
				err, sizeof(err))) {
			sysLogPrintf(LOG_WARNING,
				"MODAUTHOR.RECIPE.RESULT result=FAIL stage=template reason='%s'", err);
			goto done;
		}
		sysLogPrintf(LOG_NOTE, "MODAUTHOR.TEMPLATE id=%s archive='%s'", template_id, template_path);
	} else {
		sysLogPrintf(LOG_NOTE, "MODAUTHOR.TEMPLATE file='%s'", modAuthorRecipeTemplateFile(recipe));
	}

	modAuthorRecipeToRequest(recipe, template_path[0] ? template_path : NULL, NULL, &request);
	code = modAuthorRuntimeSave(&request, result, &restarts, err, sizeof(err));
	if (code != MOD_AUTHOR_OK) {
		sysLogPrintf(LOG_WARNING,
			"MODAUTHOR.RECIPE.RESULT result=FAIL stage=save code=%d reason='%s'", code, err);
		goto done;
	}

	entry = assetCatalogResolve(result->catalog_id);
	if (!entry) {
		sysLogPrintf(LOG_WARNING,
			"MODAUTHOR.RECIPE.RESULT result=FAIL stage=catalog catalog=%s restart_count=%d "
			"reason='saved and enabled, but not in the catalog'",
			result->catalog_id, restarts);
		goto done;
	}
	sysLogPrintf(LOG_NOTE,
		"MODAUTHOR.CATALOG.OK catalog=%s type=%d enabled=%d bundled=%d category='%s' source='%s'",
		entry->id, (s32)entry->type, entry->enabled, entry->bundled, entry->category,
		entry->descriptor_path);
	sysLogPrintf(LOG_NOTE,
		"MODAUTHOR.RECIPE.RESULT result=OK catalog=%s mod_id=%s updated=%d restart_count=%d",
		result->catalog_id, result->mod_id, result->updated, restarts);
	ok = 1;

done:
	free(result);
	modAuthorRecipeFree(recipe);
	return ok;
}
