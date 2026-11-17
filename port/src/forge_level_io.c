/**
 * forge_level_io.c -- a level's placements in and out of the running game.
 *
 * See forge_level_io.h for the contract.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "forge_level_doc.h"
#include "forge_level_io.h"
#include "fs.h"
#include "mod_author.h"
#include "mod_author_runtime.h"
#include "modarchive.h"
#include "net/matchsetup.h"
#include "pdgui_forge.h"
#include "system.h"

/* The level Forge has open, and the arena its session was started on. */
static struct {
	char arena_id[CATALOG_ID_LEN];
	forge_level_doc_t *doc;
	forge_level_source_t source;
} s_Session;

static void levelFail(char *err, size_t err_cap, const char *reason, const char *detail)
{
	if (!err || err_cap == 0) return;
	snprintf(err, err_cap, "%s%s%s", reason, (detail && detail[0]) ? ": " : "",
		(detail && detail[0]) ? detail : "");
	err[err_cap - 1] = '\0';
}

forge_level_doc_t *forgeLevelOpen(const char *arena_id, forge_level_source_t *source,
		char *err, size_t err_cap)
{
	const asset_entry_t *arena;
	const char *member;
	const char *cut;
	forge_level_texts_t texts;
	forge_level_doc_t *doc = NULL;
	void *archive = NULL;
	void *scenario = NULL;
	void *members[FORGE_LEVEL_MEMBER_COUNT];
	u32 archive_size = 0;
	u32 scenario_size = 0;
	char reason[256];

	if (err && err_cap) err[0] = '\0';
	memset(members, 0, sizeof(members));
	memset(&texts, 0, sizeof(texts));
	if (!source) {
		levelFail(err, err_cap, "no place to say where the level came from", "");
		return NULL;
	}
	memset(source, 0, sizeof(*source));

	arena = (arena_id && arena_id[0]) ? assetCatalogResolve(arena_id) : NULL;
	if (!arena || arena->type != ASSET_ARENA) {
		levelFail(err, err_cap, "not a level in the catalog", arena_id ? arena_id : "");
		return NULL;
	}
	/* The catalog names the scenario "<archive>::<member>". A level that
	 * keeps its scenario anywhere else has nothing here to edit. */
	member = arena->ext.arena.scenario_archive;
	cut = NULL;
	for (const char *p = strstr(member, "::"); p; p = strstr(p + 2, "::")) cut = p;
	if (!cut || !cut[2]) {
		levelFail(err, err_cap, "the level does not keep its scenario inside its own archive", arena_id);
		return NULL;
	}
	reason[0] = '\0';
	if (strlen(arena_id) >= sizeof(source->arena_id) || strlen(cut + 2) >= sizeof(source->scenario_member) ||
			!modAuthorRuntimeTemplatePath(arena_id, source->archive_path, sizeof(source->archive_path),
				reason, sizeof(reason))) {
		levelFail(err, err_cap, "the level's archive was not found", reason);
		return NULL;
	}
	snprintf(source->arena_id, sizeof(source->arena_id), "%s", arena_id);
	snprintf(source->scenario_member, sizeof(source->scenario_member), "%s", cut + 2);

	archive = fsFileLoad(source->archive_path, &archive_size);
	if (!archive || archive_size == 0) {
		levelFail(err, err_cap, "the level's archive could not be read", source->archive_path);
		goto done;
	}
	scenario = modArchiveExtractMemAlloc(archive, archive_size, source->scenario_member, &scenario_size);
	if (!scenario || scenario_size == 0) {
		levelFail(err, err_cap, "the level's archive has no scenario at", source->scenario_member);
		goto done;
	}
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
		u32 size = 0;

		members[i] = modArchiveExtractMemAlloc(scenario, scenario_size, forgeLevelDocMemberName(i), &size);
		if (!members[i]) {
			levelFail(err, err_cap, "the level's scenario has no", forgeLevelDocMemberName(i));
			goto done;
		}
		texts.text[i] = (const char *)members[i];
		texts.size[i] = size;
	}

	reason[0] = '\0';
	doc = forgeLevelDocRead(&texts, reason, sizeof(reason));
	if (!doc) {
		levelFail(err, err_cap, "the level's files were refused", reason);
		goto done;
	}
	sysLogPrintf(LOG_NOTE,
		"FORGE.LEVEL.OPEN: arena=%s pads=%d spawns=%d objects=%d field_rows=%d archive='%s' scenario='%s'",
		source->arena_id, forgeLevelDocPadCount(doc), forgeLevelDocSpawnCount(doc),
		forgeLevelDocObjectCount(doc), forgeLevelDocFieldRowCount(doc), source->archive_path,
		source->scenario_member);

done:
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) free(members[i]);
	free(scenario);
	if (archive) sysMemFree(archive);
	return doc;
}

s32 forgeLevelSaveAs(forge_level_doc_t *doc, const forge_level_source_t *source,
		const char *display_name, mod_author_result_t *result, char *err, size_t err_cap)
{
	forge_level_overrides_t *overrides;
	mod_author_request_t request;
	s32 restarts = 0;
	s32 code;

	if (err && err_cap) err[0] = '\0';
	if (result) memset(result, 0, sizeof(*result));
	if (!doc || !source || !source->archive_path[0] || !display_name || !display_name[0]) {
		levelFail(err, err_cap, "a save needs an open level and a name", "");
		return MOD_AUTHOR_ERR_ARGS;
	}
	/* The override names are long; keep them off the stack. */
	overrides = (forge_level_overrides_t *)calloc(1, sizeof(*overrides));
	if (!overrides) {
		levelFail(err, err_cap, "out of memory", "");
		return MOD_AUTHOR_ERR_WRITE;
	}
	if (!forgeLevelDocOverrides(doc, source->scenario_member, overrides, err, err_cap)) {
		free(overrides);
		return MOD_AUTHOR_ERR_ARGS;
	}

	memset(&request, 0, sizeof(request));
	request.template_path = source->archive_path;
	request.display_name = display_name;
	request.tool = "forge";
	request.overrides = overrides->entries;
	request.override_count = overrides->count;
	code = modAuthorRuntimeSave(&request, result, &restarts, err, err_cap);
	if (code == MOD_AUTHOR_OK) {
		sysLogPrintf(LOG_NOTE,
			"FORGE.LEVEL.SAVE.OK: template=%s catalog=%s changed_members=%u restart_count=%d archive='%s'",
			source->arena_id, result ? result->catalog_id : "", (unsigned)overrides->count, restarts,
			result ? result->archive_path : "");
	} else {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.SAVE.FAIL: template=%s name='%s' code=%d reason='%s'",
			source->arena_id, display_name, code, err ? err : "");
	}
	free(overrides);
	return code;
}

s32 forgeLevelSessionChanged(void)
{
	if (!s_Session.doc) return 0;
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
		if (forgeLevelDocMemberChanged(s_Session.doc, i)) return 1;
	}
	return 0;
}

void forgeLevelSessionClose(void)
{
	if (!s_Session.doc) return;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.CLOSE: arena=%s edited=%d", s_Session.source.arena_id,
		forgeLevelSessionChanged());
	forgeLevelDocFree(s_Session.doc);
	s_Session.doc = NULL;
	memset(&s_Session.source, 0, sizeof(s_Session.source));
}

void forgeLevelSessionBegin(const char *arena_id)
{
	forgeLevelSessionClose();
	snprintf(s_Session.arena_id, sizeof(s_Session.arena_id), "%s",
		(arena_id && strlen(arena_id) < sizeof(s_Session.arena_id)) ? arena_id : "");
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.SESSION: arena=%s", s_Session.arena_id[0] ? s_Session.arena_id : "(none)");
}

const char *forgeLevelSessionArena(void)
{
	return s_Session.arena_id;
}

s32 forgeLevelSessionOpen(const char *arena_id, char *err, size_t err_cap)
{
	forge_level_source_t *source = (forge_level_source_t *)calloc(1, sizeof(*source));
	forge_level_doc_t *doc;

	if (err && err_cap) err[0] = '\0';
	if (!source) {
		levelFail(err, err_cap, "out of memory", "");
		return 0;
	}
	/* Open first: a level that cannot be opened leaves the one already open
	 * as it was. */
	doc = forgeLevelOpen(arena_id, source, err, err_cap);
	if (!doc) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.OPEN.FAIL: arena=%s reason='%s'", arena_id ? arena_id : "",
			err ? err : "");
		free(source);
		return 0;
	}
	forgeLevelSessionClose();
	s_Session.doc = doc;
	s_Session.source = *source;
	free(source);
	return 1;
}

forge_level_doc_t *forgeLevelSessionDoc(void)
{
	return s_Session.doc;
}

const forge_level_source_t *forgeLevelSessionSource(void)
{
	return s_Session.doc ? &s_Session.source : NULL;
}

s32 forgeLevelSessionSaveAs(const char *display_name, mod_author_result_t *result,
		char *err, size_t err_cap)
{
	if (!s_Session.doc) {
		if (result) memset(result, 0, sizeof(*result));
		levelFail(err, err_cap, "no level is open", "");
		return MOD_AUTHOR_ERR_ARGS;
	}
	return forgeLevelSaveAs(s_Session.doc, &s_Session.source, display_name, result, err, err_cap);
}

s32 forgeLevelStartForge(const char *arena_id)
{
	const asset_entry_t *arena = (arena_id && arena_id[0]) ? assetCatalogResolve(arena_id) : NULL;
	char id[CATALOG_ID_LEN];
	s32 stagenum;
	s32 canvas;
	s32 started;

	if (!arena || arena->type != ASSET_ARENA || strlen(arena->id) >= sizeof(g_MatchConfig.stage_id)) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.START.FAIL: '%s' is not a level in the catalog",
			arena_id ? arena_id : "");
		return 0;
	}
	/* The catalog owns the row; keep what is needed past the calls below. */
	snprintf(id, sizeof(id), "%s", arena->id);
	stagenum = arena->ext.arena.stagenum;
	canvas = arena->ext.arena.load_mode == ARENA_LOADMODE_CANVAS;

	/* A clean match setup on this level, as The Grid's Enter makes one. */
	matchConfigInit();
	snprintf(g_MatchConfig.stage_id, sizeof(g_MatchConfig.stage_id), "%s", id);
	g_MatchConfig.stagenum = (u8)stagenum;
	snprintf(g_MatchConfig.scenario_id, sizeof(g_MatchConfig.scenario_id), "%s", "base:combat");
	g_MatchConfig.timelimit = 0;
	g_MatchConfig.scorelimit = 0;

	started = canvas ? pdguiForgeStartSessionOnCanvas(stagenum) : pdguiForgeStartSessionOn(stagenum);
	sysLogPrintf(started ? LOG_NOTE : LOG_WARNING, "FORGE.LEVEL.START%s: arena=%s stagenum=0x%02x canvas=%d",
		started ? "" : ".FAIL", id, (u32)stagenum, canvas);
	if (started) forgeLevelSessionBegin(id);
	return started ? 1 : 0;
}

/* `distance` units from a pad along the way it faces. */
static void levelStepFrom(const forge_level_pad_t *pad, f32 distance, f32 out[3])
{
	for (s32 i = 0; i < 3; i++) out[i] = pad->position[i] + pad->look[i] * distance;
}

s32 forgeLevelDebugSessionEdit(const char *display_name, const f32 position[3], const f32 look[3])
{
	mod_author_result_t *result = (mod_author_result_t *)calloc(1, sizeof(*result));
	forge_level_doc_t *doc = NULL;
	char arena_id[CATALOG_ID_LEN];
	char err[256];
	char spawn_id[32];
	s32 ok = 0;

	err[0] = '\0';
	spawn_id[0] = '\0';
	snprintf(arena_id, sizeof(arena_id), "%s", forgeLevelSessionArena());
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.DEBUG.SESSION.BEGIN: arena=%s name='%s'", arena_id,
		display_name ? display_name : "");
	if (!result) {
		snprintf(err, sizeof(err), "out of memory");
		goto done;
	}
	if (!arena_id[0]) {
		snprintf(err, sizeof(err), "the Forge session is not on a level from the catalog");
		goto done;
	}
	if (!forgeLevelSessionOpen(arena_id, err, sizeof(err))) goto done;
	doc = forgeLevelSessionDoc();
	if (!forgeLevelDocAddSpawn(doc, position, look, NULL, spawn_id, sizeof(spawn_id), err, sizeof(err))) {
		goto done;
	}
	sysLogPrintf(LOG_NOTE,
		"FORGE.LEVEL.EDIT: added spawn %s at the camera (%.0f, %.0f, %.0f); the level now has pads=%d spawns=%d objects=%d",
		spawn_id, position[0], position[1], position[2], forgeLevelDocPadCount(doc),
		forgeLevelDocSpawnCount(doc), forgeLevelDocObjectCount(doc));
	if (forgeLevelSessionSaveAs(display_name, result, err, sizeof(err)) != MOD_AUTHOR_OK) goto done;
	if (!assetCatalogResolve(result->catalog_id)) {
		snprintf(err, sizeof(err), "%s was saved but is not in the catalog", result->catalog_id);
		goto done;
	}
	ok = 1;

done:
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
		"FORGE.LEVEL.DEBUG.SESSION.RESULT: result=%s catalog=%s reason='%s'",
		ok ? "OK" : "FAIL", (ok && result) ? result->catalog_id : "", err);
	free(result);
	return ok;
}

s32 forgeLevelDebugEdit(const char *arena_id, const char *display_name)
{
	mod_author_result_t *result = (mod_author_result_t *)calloc(1, sizeof(*result));
	forge_level_doc_t *doc = NULL;
	forge_level_spawn_t spawn;
	forge_level_object_t object;
	forge_level_pad_t pad;
	char err[256];
	char spawn_id[32];
	char copy_id[32];
	f32 place[3];
	s32 picked = -1;
	s32 ok = 0;

	err[0] = '\0';
	spawn_id[0] = '\0';
	copy_id[0] = '\0';
	memset(&spawn, 0, sizeof(spawn));
	memset(&object, 0, sizeof(object));
	memset(&pad, 0, sizeof(pad));
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.DEBUG.BEGIN: arena=%s name='%s'", arena_id ? arena_id : "",
		display_name ? display_name : "");
	if (!result) {
		snprintf(err, sizeof(err), "out of memory");
		goto done;
	}
	if (!forgeLevelSessionOpen(arena_id, err, sizeof(err))) goto done;
	doc = forgeLevelSessionDoc();

	/* Another player start, a short step in front of the level's first one
	 * and facing the same way. */
	if (!forgeLevelDocGetSpawn(doc, 0, &spawn) || !forgeLevelDocFindPad(doc, spawn.pad_ref, &pad)) {
		snprintf(err, sizeof(err), "the level has no player start to step from");
		goto done;
	}
	levelStepFrom(&pad, 40.0f, place);
	if (!forgeLevelDocAddSpawn(doc, place, pad.look, spawn.team, spawn_id, sizeof(spawn_id),
			err, sizeof(err))) {
		goto done;
	}
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: added spawn %s at (%.0f, %.0f, %.0f), in front of %s on %s",
		spawn_id, place[0], place[1], place[2], spawn.spawn_id, spawn.pad_ref);

	/* Another spot for the level's first weapon pickup, a short step in
	 * front of it. */
	for (s32 i = 0; i < forgeLevelDocObjectCount(doc) && picked < 0; i++) {
		if (forgeLevelDocGetObject(doc, i, &object) && object.movable &&
				strcmp(object.kind, "weapon_pickup") == 0) {
			picked = i;
		}
	}
	if (picked < 0 || !forgeLevelDocGetObject(doc, picked, &object) ||
			!forgeLevelDocFindPad(doc, object.pad_ref, &pad)) {
		snprintf(err, sizeof(err), "the level has no weapon pickup to copy");
		goto done;
	}
	levelStepFrom(&pad, 60.0f, place);
	if (!forgeLevelDocDuplicateObject(doc, object.record_id, place, pad.look, copy_id, sizeof(copy_id),
			err, sizeof(err))) {
		goto done;
	}
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: copied %s %s to %s at (%.0f, %.0f, %.0f)", object.kind,
		object.record_id, copy_id, place[0], place[1], place[2]);

	/* The level's first ammo crate is left out. A level with none keeps
	 * everything; that is not a failure of this run. */
	picked = -1;
	for (s32 i = 0; i < forgeLevelDocObjectCount(doc) && picked < 0; i++) {
		if (forgeLevelDocGetObject(doc, i, &object) && object.removable && !object.removed &&
				strcmp(object.kind, "multi_ammo_crate") == 0) {
			picked = i;
		}
	}
	if (picked >= 0 && forgeLevelDocGetObject(doc, picked, &object)) {
		if (!forgeLevelDocSetObjectRemoved(doc, object.record_id, 1, err, sizeof(err))) {
			goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: left out %s %s on %s", object.kind, object.record_id,
			object.pad_ref[0] ? object.pad_ref : "no pad");
	} else {
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: the level has no ammo crate to leave out");
	}
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: the level now has pads=%d spawns=%d objects=%d",
		forgeLevelDocPadCount(doc), forgeLevelDocSpawnCount(doc), forgeLevelDocObjectCount(doc));

	if (forgeLevelSessionSaveAs(display_name, result, err, sizeof(err)) != MOD_AUTHOR_OK) {
		goto done;
	}
	if (!assetCatalogResolve(result->catalog_id)) {
		snprintf(err, sizeof(err), "%s was saved but is not in the catalog", result->catalog_id);
		goto done;
	}
	ok = 1;

done:
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING, "FORGE.LEVEL.DEBUG.RESULT: result=%s catalog=%s reason='%s'",
		ok ? "OK" : "FAIL", (ok && result) ? result->catalog_id : "", err);
	/* Nothing is left open: this ran before any Forge session. */
	forgeLevelSessionClose();
	free(result);
	return ok;
}
