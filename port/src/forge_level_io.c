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
#include "constants.h"
#include "data.h"
#include "forge_level_doc.h"
#include "forge_level_io.h"
#include "forge_level_live.h"
#include "fs.h"
#include "game/playermgr.h"
#include "mod_author.h"
#include "mod_author_runtime.h"
#include "modarchive.h"
#include "modmgr.h"
#include "net/matchsetup.h"
#include "pdgui_forge.h"
#include "system.h"
#include "types.h"

/* The level Forge has open, and the arena its session was started on. `opened`
 * is a copy of the document as it was opened, which is what the stage shows
 * when the session is on that level; `generation` changes whenever the open
 * document does (forge_level_live.h follows both). */
static struct {
	char arena_id[CATALOG_ID_LEN];
	forge_level_doc_t *doc;
	forge_level_doc_t *opened;
	forge_level_source_t source;
	u32 generation;
} s_Session;

/* Undo and redo for the open document: copies of it from before each edit
 * (oldest first), the documents undone since the last edit (latest last), and
 * the copy an edit in progress started from. */
static struct {
	forge_level_doc_t *undo[FORGE_LEVEL_UNDO_DEPTH];
	s32 undo_count;
	forge_level_doc_t *redo[FORGE_LEVEL_UNDO_DEPTH];
	s32 redo_count;
	forge_level_doc_t *pending;
} s_Undo;

static void undoForgetRedo(void)
{
	for (s32 i = 0; i < s_Undo.redo_count; i++) forgeLevelDocFree(s_Undo.redo[i]);
	s_Undo.redo_count = 0;
}

static void undoForgetAll(void)
{
	for (s32 i = 0; i < s_Undo.undo_count; i++) forgeLevelDocFree(s_Undo.undo[i]);
	s_Undo.undo_count = 0;
	undoForgetRedo();
	forgeLevelDocFree(s_Undo.pending);
	s_Undo.pending = NULL;
}

/* Pushes onto a full list by forgetting its oldest entry. */
static void undoPush(forge_level_doc_t **list, s32 *count, forge_level_doc_t *doc)
{
	if (*count == FORGE_LEVEL_UNDO_DEPTH) {
		forgeLevelDocFree(list[0]);
		memmove(&list[0], &list[1], sizeof(list[0]) * (FORGE_LEVEL_UNDO_DEPTH - 1));
		(*count)--;
	}
	list[(*count)++] = doc;
}

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
	mod_author_result_t *names;
	mod_author_request_t request;
	s32 restarts = 0;
	s32 own = 0;
	s32 code;

	if (err && err_cap) err[0] = '\0';
	if (result) memset(result, 0, sizeof(*result));
	if (!doc || !source || !source->archive_path[0] || !display_name || !display_name[0]) {
		levelFail(err, err_cap, "a save needs an open level and a name", "");
		return MOD_AUTHOR_ERR_ARGS;
	}

	memset(&request, 0, sizeof(request));
	request.template_path = source->archive_path;
	request.display_name = display_name;
	request.tool = "forge";

	/* Is this a save over the level the document was opened from (a level of
	 * the player's own, kept under its name)? Then the archive it replaces is
	 * the one "changed" is measured against, and every member is written, so
	 * that an edit taken back since the last save is taken back in the file
	 * too. Asked of the mod author, which knows the id a name gives. A name
	 * the author refuses is refused again, with its reason, by the save. */
	names = (mod_author_result_t *)calloc(1, sizeof(*names));
	request.mods_root = modmgrGetModsDir();
	if (names && request.mods_root && request.mods_root[0] &&
			modAuthorNames(&request, names, NULL, 0) == MOD_AUTHOR_OK) {
		own = strcmp(names->catalog_id, source->arena_id) == 0;
	}
	free(names);
	request.mods_root = NULL;   /* modAuthorRuntimeSave supplies it */

	/* The override names are long; keep them off the stack. */
	overrides = (forge_level_overrides_t *)calloc(1, sizeof(*overrides));
	if (!overrides) {
		levelFail(err, err_cap, "out of memory", "");
		return MOD_AUTHOR_ERR_WRITE;
	}
	if (!(own ? forgeLevelDocOverridesAll(doc, source->scenario_member, overrides, err, err_cap)
			: forgeLevelDocOverrides(doc, source->scenario_member, overrides, err, err_cap))) {
		free(overrides);
		return MOD_AUTHOR_ERR_ARGS;
	}

	request.overrides = overrides->entries;
	request.override_count = overrides->count;
	code = modAuthorRuntimeSave(&request, result, &restarts, err, err_cap);
	if (code == MOD_AUTHOR_OK) {
		sysLogPrintf(LOG_NOTE,
			"FORGE.LEVEL.SAVE.OK: template=%s catalog=%s changed_members=%u restart_count=%d archive='%s' own_template=%d",
			source->arena_id, result ? result->catalog_id : "", (unsigned)overrides->count, restarts,
			result ? result->archive_path : "", own);
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

void forgeLevelSessionEditBegin(void)
{
	forgeLevelDocFree(s_Undo.pending);
	s_Undo.pending = s_Session.doc ? forgeLevelDocClone(s_Session.doc) : NULL;
}

void forgeLevelSessionEditEnd(s32 changed)
{
	forge_level_doc_t *before = s_Undo.pending;

	s_Undo.pending = NULL;
	if (!before) return;
	if (!changed || !s_Session.doc) {
		forgeLevelDocFree(before);
		return;
	}
	undoPush(s_Undo.undo, &s_Undo.undo_count, before);
	undoForgetRedo();
	s_Session.generation++;
}

s32 forgeLevelSessionUndo(void)
{
	forge_level_doc_t *before;

	if (!s_Session.doc || s_Undo.undo_count == 0) return 0;
	before = s_Undo.undo[--s_Undo.undo_count];
	undoPush(s_Undo.redo, &s_Undo.redo_count, s_Session.doc);
	s_Session.doc = before;
	s_Session.generation++;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.UNDO: arena=%s pads=%d spawns=%d objects=%d undo_left=%d redo=%d",
		s_Session.source.arena_id, forgeLevelDocPadCount(before), forgeLevelDocSpawnCount(before),
		forgeLevelDocObjectCount(before), s_Undo.undo_count, s_Undo.redo_count);
	return 1;
}

s32 forgeLevelSessionRedo(void)
{
	forge_level_doc_t *after;

	if (!s_Session.doc || s_Undo.redo_count == 0) return 0;
	after = s_Undo.redo[--s_Undo.redo_count];
	undoPush(s_Undo.undo, &s_Undo.undo_count, s_Session.doc);
	s_Session.doc = after;
	s_Session.generation++;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.REDO: arena=%s pads=%d spawns=%d objects=%d undo=%d redo_left=%d",
		s_Session.source.arena_id, forgeLevelDocPadCount(after), forgeLevelDocSpawnCount(after),
		forgeLevelDocObjectCount(after), s_Undo.undo_count, s_Undo.redo_count);
	return 1;
}

s32 forgeLevelSessionUndoCount(void)
{
	return s_Session.doc ? s_Undo.undo_count : 0;
}

s32 forgeLevelSessionRedoCount(void)
{
	return s_Session.doc ? s_Undo.redo_count : 0;
}

void forgeLevelSessionClose(void)
{
	undoForgetAll();
	if (!s_Session.doc) return;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.CLOSE: arena=%s edited=%d", s_Session.source.arena_id,
		forgeLevelSessionChanged());
	forgeLevelDocFree(s_Session.doc);
	s_Session.doc = NULL;
	forgeLevelDocFree(s_Session.opened);
	s_Session.opened = NULL;
	memset(&s_Session.source, 0, sizeof(s_Session.source));
	s_Session.generation++;
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
	/* The world shows the level as it was opened until the live view
	 * (forge_level_live.h) learns what changed; without the copy it shows
	 * nothing of the document, and the document works as before. */
	s_Session.opened = forgeLevelDocClone(doc);
	s_Session.source = *source;
	s_Session.generation++;
	free(source);
	return 1;
}

forge_level_doc_t *forgeLevelSessionDoc(void)
{
	return s_Session.doc;
}

const forge_level_doc_t *forgeLevelSessionOpenedDoc(void)
{
	return s_Session.doc ? s_Session.opened : NULL;
}

u32 forgeLevelSessionGeneration(void)
{
	return s_Session.generation;
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

s32 forgeLevelPickupLook(const char *weapon_catalog_id, char *out_model_id, size_t model_cap,
		s32 *out_extra_scale, char *err, size_t err_cap)
{
	catalog_weapon_result_t weapon;
	const char *model_id;
	s32 modelnum = -1;
	s32 scale = 256;

	if (err && err_cap) err[0] = '\0';
	if (out_model_id && model_cap) out_model_id[0] = '\0';
	if (!weapon_catalog_id || !weapon_catalog_id[0] || !out_model_id || model_cap == 0 || !out_extra_scale) {
		levelFail(err, err_cap, "a weapon, and somewhere to put its model and size, are needed", "");
		return 0;
	}
	memset(&weapon, 0, sizeof(weapon));
	if (!catalogResolveWeapon(weapon_catalog_id, &weapon)) {
		levelFail(err, err_cap, "the catalog has no weapon", weapon_catalog_id);
		return 0;
	}

	/* A weapon the match's weapon sets offer is shown as its row there says:
	 * that is what a weapon place of the level becomes when a match gives it
	 * this weapon (setup.c, setupPlaceWeapon). Row 0 is "no weapon". */
	if (weapon.mp_weapon_id > 0 && weapon.mp_weapon_id < NUM_MPWEAPONS) {
		const struct mpweapon *row = &g_MpWeapons[weapon.mp_weapon_id];

		if (row->weaponnum == (u8)weapon.weapon_num && row->model > 0) {
			modelnum = row->model;
			if (row->extrascale > 0) scale = row->extrascale;
		}
	}
	/* Any other weapon: the model it is held with, at life size. */
	if (modelnum < 0) {
		modelnum = playermgrGetModelOfWeapon(weapon.weapon_num);
	}
	model_id = modelnum >= 0 ? catalogModelIdByModelnum(modelnum) : NULL;
	if (!model_id || !model_id[0]) {
		levelFail(err, err_cap, "the game has no model to show a pickup of", weapon_catalog_id);
		return 0;
	}
	snprintf(out_model_id, model_cap, "%s", model_id);
	*out_extra_scale = scale;
	return 1;
}

s32 forgeLevelSessionGivePickup(const char *record_id, const char *weapon_catalog_id,
		char *err, size_t err_cap)
{
	char model_id[CATALOG_ID_LEN];
	s32 scale = 256;

	if (err && err_cap) err[0] = '\0';
	if (!s_Session.doc) {
		levelFail(err, err_cap, "no level file is open", "");
		return 0;
	}
	if (!forgeLevelPickupLook(weapon_catalog_id, model_id, sizeof(model_id), &scale, err, err_cap)) {
		return 0;
	}
	if (!forgeLevelDocSetPickupWeapon(s_Session.doc, record_id, weapon_catalog_id, model_id, scale,
			err, err_cap)) {
		return 0;
	}
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: %s now gives %s (model %s, size %d)",
		record_id, weapon_catalog_id, model_id, scale);
	return 1;
}

/* `distance` units from a pad along the way it faces. */
static void levelStepFrom(const forge_level_pad_t *pad, f32 distance, f32 out[3])
{
	for (s32 i = 0; i < 3; i++) out[i] = pad->position[i] + pad->look[i] * distance;
}

/* --debug-forge-session-live: edits the world must show (forge_level_live.h),
 * made after the save so the saved level is what the session smokes have
 * always checked. Along `look` from `position`: the first crate or prop of the
 * level is copied 250 units ahead and then moved 150 units to the right of
 * that, so the stage's own object must be hidden and two stand-ins drawn; a
 * level with none gets an ammo crate made from nothing 250 units ahead. */
static s32 debugLiveEdits(const f32 position[3], const f32 look[3], char *err, size_t err_cap)
{
	forge_level_doc_t *doc = forgeLevelSessionDoc();
	forge_level_object_t object;
	char made[32];
	f32 ahead[3];
	f32 aside[3];
	s32 found = -1;
	s32 done;

	made[0] = '\0';
	ahead[0] = position[0] + look[0] * 250.0f;
	ahead[1] = position[1];
	ahead[2] = position[2] + look[2] * 250.0f;
	/* The camera's right, for a flat look (lx, 0, lz), is (-lz, 0, lx). */
	aside[0] = ahead[0] - look[2] * 150.0f;
	aside[1] = ahead[1];
	aside[2] = ahead[2] + look[0] * 150.0f;
	for (s32 i = 0; doc && i < forgeLevelDocObjectCount(doc) && found < 0; i++) {
		if (!forgeLevelDocGetObject(doc, i, &object)) continue;
		if (object.movable && !object.removed && object.model_catalog_id[0] &&
				(strcmp(object.kind, "multi_ammo_crate") == 0 || strcmp(object.kind, "ammo_crate") == 0 ||
				strcmp(object.kind, "prop") == 0)) {
			found = i;
		}
	}
	if (found < 0) {
		forgeLevelSessionEditBegin();
		done = forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0, ahead, look, made, sizeof(made),
			err, err_cap);
		forgeLevelSessionEditEnd(done);
		if (!done) return 0;
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: live: made ammo crate %s at (%.0f, %.0f, %.0f)", made,
			ahead[0], ahead[1], ahead[2]);
		return 1;
	}
	forgeLevelSessionEditBegin();
	done = forgeLevelDocDuplicateObject(doc, object.record_id, ahead, look, made, sizeof(made), err, err_cap);
	forgeLevelSessionEditEnd(done);
	if (!done) return 0;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: live: copied %s (%s) as %s at (%.0f, %.0f, %.0f)", object.record_id,
		object.kind, made, ahead[0], ahead[1], ahead[2]);
	forgeLevelSessionEditBegin();
	done = forgeLevelDocMoveObject(forgeLevelSessionDoc(), object.record_id, aside, look, err, err_cap);
	forgeLevelSessionEditEnd(done);
	if (!done) return 0;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: live: moved %s to (%.0f, %.0f, %.0f)", object.record_id, aside[0],
		aside[1], aside[2]);
	return 1;
}

s32 forgeLevelDebugSessionEdit(const char *display_name, const f32 position[3], const f32 look[3],
		s32 undo_redo, const f32 *live_look)
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
	/* Bracketed for undo, as the editor's Add Start Here is. */
	forgeLevelSessionEditBegin();
	ok = forgeLevelDocAddSpawn(doc, position, look, NULL, spawn_id, sizeof(spawn_id), err, sizeof(err));
	forgeLevelSessionEditEnd(ok);
	if (!ok) goto done;
	ok = 0;
	sysLogPrintf(LOG_NOTE,
		"FORGE.LEVEL.EDIT: added spawn %s at the camera (%.0f, %.0f, %.0f); the level now has pads=%d spawns=%d objects=%d",
		spawn_id, position[0], position[1], position[2], forgeLevelDocPadCount(doc),
		forgeLevelDocSpawnCount(doc), forgeLevelDocObjectCount(doc));
	/* --debug-forge-session-undo: take the start back and put it back again,
	 * as the editor's Undo and Redo do, before saving. */
	if (undo_redo) {
		if (!forgeLevelSessionUndo()) {
			snprintf(err, sizeof(err), "the start could not be undone");
			goto done;
		}
		if (!forgeLevelSessionRedo()) {
			snprintf(err, sizeof(err), "the start could not be redone");
			goto done;
		}
		/* Undo and Redo replace the open document. */
		doc = forgeLevelSessionDoc();
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: after undo and redo the level has pads=%d spawns=%d objects=%d",
			forgeLevelDocPadCount(doc), forgeLevelDocSpawnCount(doc), forgeLevelDocObjectCount(doc));
	}
	if (forgeLevelSessionSaveAs(display_name, result, err, sizeof(err)) != MOD_AUTHOR_OK) goto done;
	if (!assetCatalogResolve(result->catalog_id)) {
		snprintf(err, sizeof(err), "%s was saved but is not in the catalog", result->catalog_id);
		goto done;
	}
	if (live_look && !debugLiveEdits(position, live_look, err, sizeof(err))) goto done;
	ok = 1;

done:
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
		"FORGE.LEVEL.DEBUG.SESSION.RESULT: result=%s catalog=%s reason='%s'",
		ok ? "OK" : "FAIL", (ok && result) ? result->catalog_id : "", err);
	free(result);
	return ok;
}

s32 forgeLevelDebugEdit(const char *arena_id, const char *display_name, const char *pickup_weapon_id,
		s32 make_vehicle, const char *prop_model_id)
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
	 * front of it. A level with no weapon pickup to copy (an empty level,
	 * such as the Blank Room) gets a weapon place made from nothing instead,
	 * a longer step in front of its first start. */
	for (s32 i = 0; i < forgeLevelDocObjectCount(doc) && picked < 0; i++) {
		if (forgeLevelDocGetObject(doc, i, &object) && object.movable &&
				strcmp(object.kind, "weapon_pickup") == 0) {
			picked = i;
		}
	}
	if (picked >= 0) {
		if (!forgeLevelDocGetObject(doc, picked, &object) || !forgeLevelDocFindPad(doc, object.pad_ref, &pad)) {
			snprintf(err, sizeof(err), "the level's first weapon pickup stands on no pad");
			goto done;
		}
		levelStepFrom(&pad, 60.0f, place);
		if (!forgeLevelDocDuplicateObject(doc, object.record_id, place, pad.look, copy_id, sizeof(copy_id),
				err, sizeof(err))) {
			goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: copied %s %s to %s at (%.0f, %.0f, %.0f)", object.kind,
			object.record_id, copy_id, place[0], place[1], place[2]);
	} else {
		levelStepFrom(&pad, 160.0f, place);
		if (!forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 0, place, pad.look, copy_id,
				sizeof(copy_id), err, sizeof(err))) {
			goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: made weapon place %s at (%.0f, %.0f, %.0f): the level had no weapon pickup to copy",
			copy_id, place[0], place[1], place[2]);
	}

	/* The copy is given a weapon of the catalog, when one was asked for: the
	 * original stays one of the match's weapon places. */
	if (pickup_weapon_id && pickup_weapon_id[0] &&
			!forgeLevelSessionGivePickup(copy_id, pickup_weapon_id, err, sizeof(err))) {
		goto done;
	}

	/* The level's first ammo crate is left out. A level with none gets one
	 * made from nothing instead, further in front of its first start. */
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
		char crate_id[32];

		crate_id[0] = '\0';
		if (!forgeLevelDocGetSpawn(doc, 0, &spawn) || !forgeLevelDocFindPad(doc, spawn.pad_ref, &pad)) {
			snprintf(err, sizeof(err), "the level has no player start to step from");
			goto done;
		}
		levelStepFrom(&pad, 280.0f, place);
		if (!forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0, place, pad.look, crate_id,
				sizeof(crate_id), err, sizeof(err))) {
			goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: made ammo crate %s at (%.0f, %.0f, %.0f): the level had none to leave out",
			crate_id, place[0], place[1], place[2]);
	}

	/* A hoverbike well in front of the first start, past the start, the
	 * weapon place and the crate made above (40, 160 and 280 units), facing
	 * the way the start faces, so a rider drives away from them. */
	if (make_vehicle) {
		char bike_id[32];

		bike_id[0] = '\0';
		if (!forgeLevelDocGetSpawn(doc, 0, &spawn) || !forgeLevelDocFindPad(doc, spawn.pad_ref, &pad)) {
			snprintf(err, sizeof(err), "the level has no player start to step from");
			goto done;
		}
		levelStepFrom(&pad, 600.0f, place);
		if (!forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_HOVERBIKE, 0, place, pad.look, bike_id,
				sizeof(bike_id), err, sizeof(err))) {
			goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: made hoverbike %s at (%.0f, %.0f, %.0f) facing (%.2f, %.2f, %.2f)",
			bike_id, place[0], place[1], place[2], pad.look[0], pad.look[1], pad.look[2]);
	}

	/* A plain prop drawn with a model of the catalog, between the crate and
	 * the hoverbike's place. The catalog must have the model: a level that
	 * names a model the game cannot find is not what this proves. */
	if (prop_model_id && prop_model_id[0]) {
		char prop_id[32];
		const asset_entry_t *model = assetCatalogResolve(prop_model_id);

		prop_id[0] = '\0';
		if (!model || model->type != ASSET_MODEL) {
			snprintf(err, sizeof(err), "\"%s\" is not a model in the catalog", prop_model_id);
			goto done;
		}
		if (!forgeLevelDocGetSpawn(doc, 0, &spawn) || !forgeLevelDocFindPad(doc, spawn.pad_ref, &pad)) {
			snprintf(err, sizeof(err), "the level has no player start to step from");
			goto done;
		}
		levelStepFrom(&pad, 420.0f, place);
		if (!forgeLevelDocAddProp(doc, prop_model_id, place, pad.look, prop_id, sizeof(prop_id),
				err, sizeof(err))) {
			goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: made prop %s drawn with %s at (%.0f, %.0f, %.0f)",
			prop_id, prop_model_id, place[0], place[1], place[2]);
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
