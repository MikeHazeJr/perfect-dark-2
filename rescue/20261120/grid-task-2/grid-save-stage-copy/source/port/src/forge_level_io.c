/**
 * forge_level_io.c -- a level's placements in and out of the running game.
 *
 * See forge_level_io.h for the contract.
 */

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <windows.h>

#include <PR/ultratypes.h>

#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "asset_runtime.h"
#include "assetcatalog.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_resolve.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_stage_slots.h"
#include "assetprovider.h"
#include "bss.h"
#include "catalog_dep_activation_plan.h"
#include "constants.h"
#include "data.h"
#include "forge_level_doc.h"
#include "forge_level_io.h"
#include "forge_level_live.h"
#include "forge_room_builder.h"
#include "fs.h"
#include "game/bg.h"
#include "game/camera.h"
#include "game/forgemode.h"
#include "grid_archive_policy.h"
#include "grid_match_runtime.h"
#include "grid_match_package_io.h"
#include "game/playermgr.h"
#include "game/stagetable.h"
#include "lib/collision.h"
#include "lib/main.h"
#include "lib/meshcollision.h"
#include "lib/mtx.h"
#include "mod.h"
#include "mod_asset_validate.h"
#include "mod_author.h"
#include "mod_author_runtime.h"
#include "modarchive.h"
#include "modasset_material_order.h"
#include "modmgr.h"
#include "mods_roots.h"
#include "mission_graph_modules.h"
#include "net/grid_match_wire.h"
#include "net/matchsetup.h"
#include "net/net.h"
#include "net/netmanifest.h"
#include "net/netmsg.h"
#include "pdgui_forge.h"
#include "save_atomic.h"
#include "scenario_intro.h"
#include "scenario_source_runtime.h"
#include "scenario_stage_rules.h"
#include "smoke_harness.h"
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
	s32 opened_original;
	forge_revision_t saved_archive;
	forge_revision_t parent;
	forge_revision_parent_e parent_state;
	s32 parent_checked;
	char parent_error[160];
	u32 generation;
	u64 revision;
	u64 saved_revision;
	u64 next_revision;
} s_Session;

static struct {
	forge_revision_t revision;
	u8 digest[SHA256_DIGEST_SIZE];
	u32 size;
} s_MatchShare;

static grid_match_source_package_t s_WeaponMatchShare;

static struct {
	grid_match_session_sources_t sources;
	grid_match_sources_t plan;
	char arena_id[CATALOG_ID_LEN];
	u8 arena_sha256[SHA256_DIGEST_SIZE];
	s32 stage;
	u32 catalog_generation;
} s_VariantDependencyMount;

enum forge_draft_phase {
	FORGE_DRAFT_NONE,
	FORGE_DRAFT_QUEUED,
	FORGE_DRAFT_LOADING,
	FORGE_DRAFT_PLAYING,
	FORGE_DRAFT_RETURN_QUEUED,
	FORGE_DRAFT_RETURNING
};

static struct {
	enum forge_draft_phase phase;
	s32 stage;
	char scenario_root[FS_MAXPATH + 1];
	void *bytes;
	u32 size;
} s_Draft;

typedef struct forge_level_snapshot {
	forge_level_doc_t *doc;
	u64 revision;
} forge_level_snapshot_t;

/* Undo and redo for the open document: copies of it from before each edit
 * (oldest first), the documents undone since the last edit (latest last), and
 * the copy an edit in progress started from. */
static struct {
	forge_level_snapshot_t undo[FORGE_LEVEL_UNDO_DEPTH];
	s32 undo_count;
	forge_level_snapshot_t redo[FORGE_LEVEL_UNDO_DEPTH];
	s32 redo_count;
	forge_level_doc_t *pending;
} s_Undo;

static void undoForgetRedo(void)
{
	for (s32 i = 0; i < s_Undo.redo_count; i++) forgeLevelDocFree(s_Undo.redo[i].doc);
	s_Undo.redo_count = 0;
}

static void undoForgetAll(void)
{
	for (s32 i = 0; i < s_Undo.undo_count; i++) forgeLevelDocFree(s_Undo.undo[i].doc);
	s_Undo.undo_count = 0;
	undoForgetRedo();
	forgeLevelDocFree(s_Undo.pending);
	s_Undo.pending = NULL;
}

/* Pushes onto a full list by forgetting its oldest entry. */
static void undoPush(forge_level_snapshot_t *list, s32 *count, forge_level_snapshot_t snapshot)
{
	if (*count == FORGE_LEVEL_UNDO_DEPTH) {
		forgeLevelDocFree(list[0].doc);
		memmove(&list[0], &list[1], sizeof(list[0]) * (FORGE_LEVEL_UNDO_DEPTH - 1));
		(*count)--;
	}
	list[(*count)++] = snapshot;
}

static void levelFail(char *err, size_t err_cap, const char *reason, const char *detail)
{
	if (!err || err_cap == 0) return;
	snprintf(err, err_cap, "%s%s%s", reason, (detail && detail[0]) ? ": " : "",
		(detail && detail[0]) ? detail : "");
	err[err_cap - 1] = '\0';
}

static void levelStepFrom(const forge_level_pad_t *pad, f32 distance, f32 out[3])
{
	for (s32 i = 0; i < 3; i++) out[i] = pad->position[i] + pad->look[i] * distance;
}

#define LEVEL_ARCHIVE_MAX_BYTES (128u * 1024u * 1024u)

/* Read only the catalog's single native arena member. A generic nested
 * filesystem read would inflate the outer entry before checking its size. */
static void *levelLoadPackedArena(const char *locator, u32 *size)
{
	*size = 0;
	const char *cut = strstr(locator, "::");
	if (!cut || cut == locator || (size_t)(cut - locator) >= FS_MAXPATH) return NULL;
	const char *member = cut + 2;
	if (strchr(member, ':') || strchr(member, '\\') || assetPathIsAbsolute(member)
			|| assetPathHasParentTraversal(member)
			|| assetArchiveTypeForPath(member) != ASSET_ARENA) return NULL;

	char outer[FS_MAXPATH + 1] = {0};
	memcpy(outer, locator, (size_t)(cut - locator));
	const char *suffix = strrchr(outer, '.');
	if (!suffix || strcasecmp(suffix, ".pdmod") != 0) return NULL;
	char path[FS_MAXPATH + 1];
	fsFullPath(outer, path, sizeof(path));
	if (!fsPathIsAbsolute(path)) return NULL;

	mod_archive_t *wrapper = modArchiveOpen(path);
	if (!wrapper) return NULL;
	const s32 index = modArchiveFindEntry(wrapper, member);
	const char *name = index >= 0 ? modArchiveGetEntryName(wrapper, index) : NULL;
	const u32 expected = name && strcmp(name, member) == 0
		? modArchiveGetEntrySize(wrapper, index) : 0;
	void *bytes = expected && expected <= LEVEL_ARCHIVE_MAX_BYTES
		? modArchiveExtractAlloc(wrapper, index, size) : NULL;
	modArchiveClose(wrapper);
	if (!bytes || *size != expected) {
		free(bytes);
		*size = 0;
		return NULL;
	}
	return bytes;
}

static s32 levelCheckArchiveDigest(const u8 digest[SHA256_DIGEST_SIZE], const char *expected,
		char *err, size_t err_cap)
{
	char hex[SHA256_HEX_SIZE];
	sha256ToHex(digest, hex);
	if (strcmp(hex, expected) == 0) return 1;
	levelFail(err, err_cap, "the source has changed; reopen the level", "");
	return 0;
}

static s32 levelSourceStillMatches(const forge_level_source_t *source, char *err, size_t err_cap)
{
	u8 digest[SHA256_DIGEST_SIZE];
	s32 read = 0;
	if (source->archive_sha256[0] && strstr(source->archive_path, "::")) {
		u32 size;
		void *bytes = levelLoadPackedArena(source->archive_path, &size);
		if (bytes) {
			sha256Hash(bytes, size, digest);
			free(bytes);
			read = 1;
		}
	} else if (source->archive_sha256[0]) {
		read = modArchiveSha256(source->archive_path, digest) == 0;
	}
	if (!read) {
		levelFail(err, err_cap, "the source revision could not be read", "");
		return 0;
	}
	return levelCheckArchiveDigest(digest, source->archive_sha256, err, err_cap);
}

/* The author, revision store and archive rewriter take a physical .pdarena.
 * Scope a byte-for-byte copy to their synchronous operation, outside Mods. */
static const char *levelArchiveInput(const char *locator, const char *expected_digest,
		char scratch[FS_MAXPATH + 1], char *err, size_t err_cap)
{
	scratch[0] = '\0';
	if (!strstr(locator, "::")) return locator;
	u32 size;
	void *bytes = levelLoadPackedArena(locator, &size);
	if (!bytes) {
		levelFail(err, err_cap, "the packed arena source could not be read", "");
		return NULL;
	}
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(bytes, size, digest);
	if (!levelCheckArchiveDigest(digest, expected_digest, err, err_cap)) {
		free(bytes);
		return NULL;
	}

	static u32 serial;
	char relative[96];
	snprintf(relative, sizeof(relative), "$S/forge-source-%llx-%u.pdarena",
		(unsigned long long)sysGetMicroseconds(), (unsigned)++serial);
	fsFullPath(relative, scratch, FS_MAXPATH + 1);
	const int fd = fsPathIsAbsolute(scratch) && strlen(scratch) < FS_MAXPATH - 32
		? _open(scratch, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE) : -1;
	s32 ok = 0;
	if (fd >= 0) {
		FILE *stream = _fdopen(fd, "wb");
		if (stream) {
			const s32 complete = fwrite(bytes, 1, size, stream) == size;
			ok = fclose(stream) == 0 && complete;
		} else {
			_close(fd);
		}
		if (!ok && remove(scratch) != 0) {
			sysLogPrintf(LOG_WARNING, "FORGE.LEVEL: could not remove incomplete source '%s'", scratch);
		}
	}
	free(bytes);
	if (ok) return scratch;
	scratch[0] = '\0';
	levelFail(err, err_cap, "the private packed arena input could not be staged", "");
	return NULL;
}

static void levelReleaseArchiveInput(const char *scratch)
{
	if (scratch[0] && remove(scratch) != 0) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL: could not remove private source '%s'", scratch);
	}
}

static s32 levelReadDescriptor(const void *bytes, u32 size, const char *archive_name,
		ini_section_t *ini, char *err, size_t err_cap);
static s32 gridValidateDraftSources(const void *scenario, u32 scenario_size,
	const char *scenario_member, const char *scenario_id, char *err, size_t err_cap);

static s32 levelAllOverrides(forge_level_doc_t *doc, const forge_level_source_t *source,
		const char *archive_input, forge_level_overrides_t *overrides, char *err, size_t err_cap)
{
	if (!forgeLevelDocOverridesAll(doc, source->scenario_member, overrides, err, err_cap)) {
		return 0;
	}
	/* Undo can return to a document with no optional member after an
	 * in-place save has added that member to the template. */
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
		/* Offline source absent from a shared snapshot belongs to the pinned template. */
		if (i == FORGE_LEVEL_MEMBER_COVERS || i == FORGE_LEVEL_MEMBER_PATHS
				|| i == FORGE_LEVEL_MEMBER_ROOMS
				|| !forgeLevelDocMemberOptional(i)) continue;
		char name[FORGE_LEVEL_OVERRIDE_NAME_MAX];
		int len = snprintf(name, sizeof(name), "%s::%s", source->scenario_member, forgeLevelDocMemberName(i));
		if (len < 0 || (size_t)len >= sizeof(name)) {
			levelFail(err, err_cap, "the optional scenario member path is too long", forgeLevelDocMemberName(i));
			return 0;
		}
		s32 present = 0;
		for (u32 j = 0; j < overrides->count; j++) {
			if (strcmp(overrides->entries[j].entry, name) == 0) present = 1;
		}
		if (present) continue;
		char chain[FS_MAXPATH + 1];
		len = snprintf(chain, sizeof(chain), "%s::%s", archive_input, name);
		if (len < 0 || (size_t)len >= sizeof(chain)) {
			levelFail(err, err_cap, "the source archive member path is too long", name);
			return 0;
		}
		if (fsFileSize(chain) >= 0) {
			u32 slot = overrides->count++;
			snprintf(overrides->names[slot], sizeof(overrides->names[slot]), "%s", name);
			overrides->entries[slot].entry = overrides->names[slot];
			overrides->entries[slot].remove = 1;
		}
	}
	return 1;
}

s32 forgeLevelSessionCanPlayDraft(void)
{
	return s_Session.doc && s_Session.arena_id[0]
		&& strcmp(s_Session.arena_id, s_Session.source.arena_id) == 0;
}

s32 forgeLevelSessionAimProp(const f32 screen[2], forge_level_prop_aim_t *aim)
{
	if (!aim) return 0;
	*aim = (forge_level_prop_aim_t){0};
	if (g_NetMode != NETMODE_NONE || s_Draft.phase != FORGE_DRAFT_NONE
			|| !forgeLevelSessionCanPlayDraft() || mainStageChangePending()) return 0;
	const asset_entry_t *arena = assetCatalogResolve(s_Session.arena_id);
	if (!arena || arena->type != ASSET_ARENA || arena->ext.arena.stagenum != g_StageNum
			|| strcmp(arena->id, s_Session.arena_id) != 0) return 0;
	catalog_stage_result_t stage;
	if (!catalogGetStageResultByIndexChecked(g_StageIndex, &stage)) return 0;
	const asset_entry_t *scenario = scenarioSourceFindEntryForStage(&stage, g_Vars.normmplayerisrunning);
	if (!scenario || scenario->type != ASSET_SCENARIO
			|| strcmp(scenario->id, arena->ext.arena.scenario_id) != 0) return 0;

	struct coord camera, up, look;
	if (!forgeCameraOverride(&camera, &up, &look)) return 0;
	if (screen) {
		Mtxf *camera_to_world = camGetProjectionMtxF();
		if (!camera_to_world) return 0;
		const f32 left = camGetScreenLeft();
		const f32 top = camGetScreenTop();
		if (!__builtin_isfinite(screen[0]) || !__builtin_isfinite(screen[1]) || screen[0] < left
				|| screen[0] >= left + camGetScreenWidth() || screen[1] < top
				|| screen[1] >= top + camGetScreenHeight()) return 1;

		/* Use the rendered camera, also used to project the floor preview. */
		f32 pixel[2] = { screen[0], screen[1] };
		struct coord view_ray;
		cam0f0b4c3c(pixel, &view_ray, 1.0f);
		mtx4RotateVec(camera_to_world, &view_ray, &look);
		camera = g_Vars.currentplayer->cam_pos;
	}

	const f32 reach = 4000.0f;
	const struct coord target = { .x = camera.x + reach * look.x, .y = camera.y + reach * look.y,
		.z = camera.z + reach * look.z };
	struct coord normal = {0};
	s32 triangle;
	const f32 fraction = meshRayCastWorldMasked(&camera, &target, 0, &normal, &triangle);
	const f32 distance = reach * fraction;
	aim->position[0] = camera.x + distance * look.x;
	aim->position[1] = camera.y + distance * look.y;
	aim->position[2] = camera.z + distance * look.z;

	const f32 yaw = forgeGetCameraYawDeg() * 0.017453292519943f;
	aim->look[0] = -sinf(yaw);
	aim->look[2] = cosf(yaw);
	if (triangle < 0 || look.y >= 0.0f || fabsf(normal.y) < 0.35f) return 1;

	/* Snap the horizontal axes only. Ramps and other non-level surfaces
	 * retain their native height through the second floor query. */
	const f32 snap = FORGE_LEVEL_PROP_SNAP_UNITS;
	const f32 floor_range = 60.0f;
	const struct coord probe = {
		.x = roundf(aim->position[0] / snap) * snap,
		.y = aim->position[1] + floor_range,
		.z = roundf(aim->position[2] / snap) * snap,
	};
	struct coord below = probe;
	below.y -= 2.0f * floor_range;
	aim->position[0] = probe.x;
	aim->position[2] = probe.z;
	const f32 floor = meshRayCastWorldMasked(&probe, &below, GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2, NULL, &triangle);
	if (triangle < 0) return 1;

	/* Position the pad above the floor so the native prop setup can
	 * settle it and the room resolver can assign the same floor. */
	struct coord place = probe;
	place.y -= floor * 2.0f * floor_range - 8.0f;
	aim->position[1] = place.y;
	aim->has_floor = 1;
	aim->valid = forgeLevelLivePositionInRoom(aim->position)
		&& meshRayCastWorldMasked(&camera, &place, 0, NULL, NULL) >= 1.0f;
	return 1;
}

s32 forgeLevelSessionCanEditCover(void)
{
	if (g_NetMode != NETMODE_NONE || !forgeLevelSessionCanPlayDraft()) return 0;
	const asset_entry_t *arena = assetCatalogResolve(s_Session.arena_id);
	if (!arena || arena->type != ASSET_ARENA || arena->ext.arena.stagenum != g_StageNum) return 0;
	size_t size = 0;
	forgeLevelDocMemberText(s_Session.doc, FORGE_LEVEL_MEMBER_COVERS, &size);
	return size && scenarioSourceUsesGridCoverMember(arena->ext.arena.scenario_id, arena->ext.arena.scenario_archive);
}

static s32 levelScenarioUsesRoomTable(const asset_entry_t *scenario, const char *scenario_root)
{
	char actual[FS_MAXPATH + 1], expected[FS_MAXPATH + 1];
	return scenario_root[0] && scenarioSourceRoomTablePath(scenario, actual, sizeof(actual))
		&& assetPathJoinChecked(expected, sizeof(expected), scenario_root, "::",
			forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_ROOMS))
		&& strcmp(actual, expected) == 0;
}

s32 forgeLevelSessionCanEditRoomBrightness(void)
{
	if (g_NetMode != NETMODE_NONE || s_Draft.phase != FORGE_DRAFT_NONE
			|| !forgeLevelSessionCanPlayDraft() || mainStageChangePending() || !bgUsesScenarioSource()
			|| !forgeLevelDocRoomBrightnessCount(s_Session.doc)) return 0;
	const asset_entry_t *arena = assetCatalogResolve(s_Session.arena_id);
	if (!arena || arena->type != ASSET_ARENA || arena->ext.arena.stagenum != g_StageNum) return 0;
	catalog_stage_result_t stage;
	if (!catalogGetStageResultByIndexChecked(g_StageIndex, &stage)) return 0;
	const asset_entry_t *scenario = scenarioSourceFindEntryForStage(&stage, g_Vars.normmplayerisrunning);
	return scenario && scenario->type == ASSET_SCENARIO && scenario->ext.scenario.stagenum == g_StageNum
		&& strcmp(scenario->id, arena->ext.arena.scenario_id) == 0
		&& levelScenarioUsesRoomTable(scenario, arena->ext.arena.scenario_archive);
}

s32 forgeLevelSessionCanConnectPads(void)
{
	if (g_NetMode != NETMODE_NONE || s_Draft.phase != FORGE_DRAFT_NONE
			|| !forgeLevelSessionCanPlayDraft() || mainStageChangePending()) return 0;
	const asset_entry_t *arena = assetCatalogResolve(s_Session.arena_id);
	if (!arena || arena->type != ASSET_ARENA || arena->ext.arena.stagenum != g_StageNum) return 0;
	size_t size = 0;
	forgeLevelDocMemberText(s_Session.doc, FORGE_LEVEL_MEMBER_PATHS, &size);
	return size && scenarioSourceUsesGridPathMember(arena->ext.arena.scenario_id, arena->ext.arena.scenario_archive);
}

/* New placements leave their pad room unset. Use the resident floor the same
 * way native setup resolves such pads, requiring actual collision here. */
static RoomNum levelRouteFloorRoom(struct coord *position, const char *room_ref)
{
	RoomNum inrooms[24], above[22];
	if (!__builtin_isfinite(position->x) || !__builtin_isfinite(position->y)
			|| !__builtin_isfinite(position->z)) return -1;
	bgFindRoomsByPos(position, inrooms, above, 20, NULL);
	RoomNum *rooms = inrooms[0] != -1 ? inrooms : above;
	const s32 room = rooms[0] != -1 ? cdFindFloorRoomAtPos(position, rooms) : -1;
	if (room <= 0 || !room_ref[0]) return room;
	if (strncmp(room_ref, "room_", 5) != 0) return -1;
	char *end;
	const long declared = strtol(room_ref + 5, &end, 10);
	return !*end && declared == room ? room : -1;
}

s32 forgeLevelSessionMoveCoverToPad(const char *cover_ref, const char *pad_ref,
	char *err, size_t err_cap)
{
	if (s_Draft.phase != FORGE_DRAFT_NONE || mainStageChangePending() || !forgeLevelSessionCanEditCover()) {
		levelFail(err, err_cap, "open this level offline with its native cover table", "");
		return 0;
	}
	forge_level_pad_t pad;
	if (!forgeLevelDocFindPad(s_Session.doc, pad_ref, &pad)) {
		levelFail(err, err_cap, "select a valid destination pad", "");
		return 0;
	}
	struct coord position = { .x = pad.position[0], .y = pad.position[1], .z = pad.position[2] };
	if (levelRouteFloorRoom(&position, pad.room_ref) <= 0) {
		const char *reason = pad.room_ref[0]
			? "the destination pad needs a native floor in its authored room"
			: "the destination pad needs a native floor";
		levelFail(err, err_cap, reason, pad.pad_ref);
		return 0;
	}
	forgeLevelSessionEditBegin();
	if (!s_Undo.pending) {
		levelFail(err, err_cap, "out of memory before moving the cover", "");
		return 0;
	}
	const s32 changed = forgeLevelDocMoveCoverToPad(s_Session.doc, cover_ref, pad_ref, err, err_cap);
	forgeLevelSessionEditEnd(changed);
	return changed;
}

s32 forgeLevelSessionConnectPads(const char *first, const char *second,
	char *out_path_id, size_t out_cap, char *err, size_t err_cap)
{
	if (out_path_id && out_cap) out_path_id[0] = '\0';
	if (!forgeLevelSessionCanConnectPads()) {
		levelFail(err, err_cap, "open and run this level with its generated native bot routes", "");
		return 0;
	}
	forge_level_pad_t a, b;
	if (!forgeLevelDocFindPad(s_Session.doc, first, &a) || !forgeLevelDocFindPad(s_Session.doc, second, &b)
			|| (a.position[0] == b.position[0] && a.position[2] == b.position[2])) {
		levelFail(err, err_cap, "select two pads at different positions on the floor", "");
		return 0;
	}
	struct coord from = { .x = a.position[0], .y = a.position[1], .z = a.position[2] };
	struct coord to = { .x = b.position[0], .y = b.position[1], .z = b.position[2] };
	const RoomNum room = levelRouteFloorRoom(&from, a.room_ref);
	if (room <= 0 || room != levelRouteFloorRoom(&to, b.room_ref)) {
		levelFail(err, err_cap, "both pads need a native floor in the same room", "");
		return 0;
	}
	RoomNum rooms[] = { room, -1 };
	if (!cdTestLos05(&from, rooms, &to, rooms, CDTYPE_BG,
			GEOFLAG_WALL | GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2 | GEOFLAG_BLOCK_SIGHT | GEOFLAG_BLOCK_SHOOT)) {
		levelFail(err, err_cap, "a room surface blocks this route between the pads", "");
		return 0;
	}
	forgeLevelSessionEditBegin();
	if (!s_Undo.pending) {
		levelFail(err, err_cap, "out of memory before connecting the pads", "");
		return 0;
	}
	const s32 changed = forgeLevelDocConnectPads(s_Session.doc, first, second, out_path_id, out_cap, err, err_cap);
	forgeLevelSessionEditEnd(changed);
	return changed;
}

static void *levelBuildPrivateDraftArchive(forge_level_doc_t *doc, const forge_level_source_t *source,
	char path[FS_MAXPATH + 1], u32 *out_size, const char *tool, asset_archive_clone_report_t *report,
	char *err, size_t err_cap)
{
	static u32 serial;
	char relative[96];
	u32 archive_size = 0;
	*out_size = 0;
	if (!strstr(source->archive_path, "::") && !levelSourceStillMatches(source, err, err_cap)) return NULL;

	snprintf(relative, sizeof(relative), "$S/forge-draft-%llx-%u.pdarena",
		(unsigned long long)sysGetMicroseconds(), (unsigned)++serial);
	fsFullPath(relative, path, FS_MAXPATH + 1);
	if (!fsPathIsAbsolute(path) || strlen(path) >= FS_MAXPATH - 32) {
		levelFail(err, err_cap, "the profile draft path is unavailable", "");
		return NULL;
	}
	forge_level_overrides_t *overrides = calloc(1, sizeof(*overrides));
	if (!overrides) {
		levelFail(err, err_cap, "out of memory preparing the draft", "");
		return NULL;
	}
	char scratch[FS_MAXPATH + 1];
	const char *input = levelArchiveInput(source->archive_path, source->archive_sha256, scratch, err, err_cap);
	const s32 built = input && levelAllOverrides(doc, source, input, overrides, err, err_cap)
		&& assetArchiveRebuildFile(input, path, source->arena_id, tool,
			overrides->entries, overrides->count, report, err, err_cap) == ASSET_ARCHIVE_CLONE_OK;
	levelReleaseArchiveInput(scratch);
	free(overrides);
	if (!built) {
		if (fsFileSize(path) >= 0 && remove(path) != 0) {
			levelFail(err, err_cap, "the private draft archive could not be removed", "");
		}
		return NULL;
	}

	const s32 archive_length = fsFileSize(path);
	void *archive = NULL;
	if (archive_length > 0 && (u32)archive_length <= LEVEL_ARCHIVE_MAX_BYTES
			&& levelSourceStillMatches(source, err, err_cap)) {
		archive = fsFileLoad(path, &archive_size);
	}
	const s32 removed = remove(path) == 0;
	if (!archive || archive_size != (u32)archive_length || !removed) {
		if (archive) sysMemFree(archive);
		if (!err || !err_cap || !err[0]) {
			levelFail(err, err_cap, "the private draft archive could not be staged", "");
		}
		return NULL;
	}
	if (modAssetValidateArchiveBytes(archive, archive_size, path, err, err_cap) != MOD_ASSET_VALIDATE_OK) {
		sysMemFree(archive);
		return NULL;
	}
	*out_size = archive_size;
	return archive;
}

static void *levelBuildValidatedOriginalDraft(forge_level_doc_t *doc, const forge_level_source_t *source,
	u32 *out_size, char out_sha256[SHA256_HEX_SIZE], const char *tool, asset_archive_clone_report_t *report,
	char *err, size_t err_cap)
{
	*out_size = 0;
	if (err && err_cap) err[0] = '\0';
	if (out_sha256) out_sha256[0] = '\0';
	if (!doc || !source || !source->arena_id[0] || !source->archive_path[0] || !source->scenario_member[0]) {
		levelFail(err, err_cap, "open an original Grid arena to validate its draft", "");
		return NULL;
	}
	const asset_entry_t *arena = assetCatalogResolve(source->arena_id);
	if (!arena || arena->type != ASSET_ARENA || !arena->enabled || !arena->ext.arena.scenario_id[0]) {
		levelFail(err, err_cap, "the original draft has no enabled arena and scenario binding", source->arena_id);
		return NULL;
	}
	char path[FS_MAXPATH + 1];
	u32 archive_size = 0;
	void *archive = levelBuildPrivateDraftArchive(doc, source, path, &archive_size, tool, report, err, err_cap);
	if (!archive) return NULL;
	if (gridArchiveOrigin(path, archive, archive_size) != GRID_ARCHIVE_ORIGINAL) {
		levelFail(err, err_cap, "Validate Draft is available for original Grid arenas", "");
		sysMemFree(archive);
		return NULL;
	}
	u8 digest[SHA256_DIGEST_SIZE];
	char sha[SHA256_HEX_SIZE];
	sha256Hash(archive, archive_size, digest);
	sha256ToHex(digest, sha);
	if (!gridMatchRuntimePreflightLocalArena(path, archive, archive_size, source->arena_id, sha, err, err_cap)) {
		sysMemFree(archive);
		return NULL;
	}
	u32 scenario_size = 0;
	void *scenario = gridMatchPackageReadMember(archive, archive_size, source->scenario_member,
		LEVEL_ARCHIVE_MAX_BYTES, &scenario_size, NULL);
	if (!scenario || !scenario_size) {
		free(scenario);
		sysMemFree(archive);
		levelFail(err, err_cap, "the draft scenario is missing or too large", "");
		return NULL;
	}
	const s32 valid = gridValidateDraftSources(scenario, scenario_size, source->scenario_member,
		arena->ext.arena.scenario_id, err, err_cap);
	free(scenario);
	if (!valid) {
		sysMemFree(archive);
		return NULL;
	}
	*out_size = archive_size;
	if (out_sha256) memcpy(out_sha256, sha, sizeof(sha));
	return archive;
}

s32 forgeLevelValidateOriginalDraft(forge_level_doc_t *doc, const forge_level_source_t *source,
	char out_sha256[SHA256_HEX_SIZE], char *err, size_t err_cap)
{
	u32 size;
	void *validated = levelBuildValidatedOriginalDraft(doc, source, &size, out_sha256,
		"forge-draft", NULL, err, err_cap);
	if (!validated) return 0;
	sysMemFree(validated);
	return 1;
}

s32 forgeLevelSessionPlayDraft(char *err, size_t err_cap)
{
	const forge_level_source_t *source = &s_Session.source;
	catalog_stage_result_t stage;
	char path[FS_MAXPATH + 1];
	u32 archive_size = 0;
	u32 scenario_size = 0;

	if (err && err_cap) err[0] = '\0';
	if (!forgeLevelSessionCanPlayDraft() || !forgeIsFreefly() || forgeIsCanvasMode()
			|| g_NetMode != NETMODE_NONE || s_Draft.phase != FORGE_DRAFT_NONE
			|| mainStageChangePending()) {
		levelFail(err, err_cap, "open this arena in an offline Grid session to play its draft", "");
		return 0;
	}
	if (!catalogResolveStageForType(source->arena_id, ASSET_ARENA, &stage)
			|| !stage.entry || stage.stagenum != g_StageNum) {
		levelFail(err, err_cap, "the draft is not for the running arena", source->arena_id);
		return 0;
	}
	catalog_stage_result_t setup_stage, world_stage;
	const asset_entry_t *mod_map = assetCatalogFindModMapByStagenum(g_StageNum);
	const char *world_id = mod_map ? mod_map->id : catalogStageIdByStagenum(g_StageNum);
	const asset_entry_t *scenario = catalogGetStageResultByIndexChecked(g_StageIndex, &setup_stage)
		? scenarioSourceFindEntryForStage(&setup_stage, g_Vars.normmplayerisrunning) : NULL;
	const asset_entry_t *world_scenario = world_id && catalogResolveStage(world_id, &world_stage)
		? scenarioSourceFindEntryForStage(&world_stage, g_Vars.normmplayerisrunning) : NULL;
	if (!scenario || !world_scenario || scenario != world_scenario
			|| strcmp(stage.entry->ext.arena.scenario_id, scenario->id) != 0) {
		levelFail(err, err_cap, "the arena's embedded scenario is not the running source", source->arena_id);
		return 0;
	}
	const char *cut = NULL;
	const char *scene = scenario->ext.scenario.scene_file;
	for (const char *p = strstr(scene, "::"); p; p = strstr(p + 2, "::")) cut = p;
	if (!cut || cut == scene || (size_t)(cut - scene) + 2 >= sizeof(s_Draft.scenario_root)) {
		levelFail(err, err_cap, "the running scenario has no usable archive root", scenario->id);
		return 0;
	}
	size_t root_len = (size_t)(cut - scene);
	memcpy(s_Draft.scenario_root, scene, root_len);
	s_Draft.scenario_root[root_len] = '\0';
	if (!scenarioSourceUsesLevelFileMembers(scenario->id, s_Draft.scenario_root)) {
		levelFail(err, err_cap, "this stage uses custom graph or placement source paths", scenario->id);
		return 0;
	}
	if (forgeLevelDocMemberChanged(s_Session.doc, FORGE_LEVEL_MEMBER_COVERS)
			&& !scenarioSourceUsesGridCoverMember(scenario->id, s_Draft.scenario_root)) {
		levelFail(err, err_cap, "this stage reads cover from a different archive member", scenario->id);
		return 0;
	}
	if (forgeLevelDocMemberChanged(s_Session.doc, FORGE_LEVEL_MEMBER_PATHS)
			&& !scenarioSourceUsesGridPathMember(scenario->id, s_Draft.scenario_root)) {
		levelFail(err, err_cap, "this stage does not generate routes from the edited path member", scenario->id);
		return 0;
	}
	if (forgeLevelDocMemberChanged(s_Session.doc, FORGE_LEVEL_MEMBER_ROOMS)
			&& !levelScenarioUsesRoomTable(scenario, s_Draft.scenario_root)) {
		levelFail(err, err_cap, "this stage reads room brightness from a different archive member", scenario->id);
		return 0;
	}

	void *archive = levelBuildPrivateDraftArchive(s_Session.doc, source, path, &archive_size,
		"forge-draft", NULL, err, err_cap);
	if (!archive) return 0;
	void *bytes = gridMatchPackageReadMember(archive, archive_size, source->scenario_member,
		LEVEL_ARCHIVE_MAX_BYTES, &scenario_size, NULL);
	sysMemFree(archive);
	if (!bytes || !scenario_size) {
		free(bytes);
		levelFail(err, err_cap, "the draft scenario is missing or too large", "");
		return 0;
	}
	s_Draft.bytes = bytes;
	s_Draft.size = scenario_size;
	s_Draft.stage = stage.stagenum;
	s_Draft.phase = FORGE_DRAFT_QUEUED;
	u8 digest[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	sha256Hash(bytes, scenario_size, digest);
	sha256ToHex(digest, hex);
	sysLogPrintf(LOG_NOTE, "FORGE.DRAFT.PREPARED: arena=%s revision=%s bytes=%u unsaved=%d",
		source->arena_id, hex, scenario_size, forgeLevelSessionHasUnsavedChanges());
	mainChangeToStage(s_Draft.stage);
	return 1;
}

s32 forgeLevelDraftReturn(void)
{
	if (s_Draft.phase == FORGE_DRAFT_RETURN_QUEUED) return 1;
	if (s_Draft.phase != FORGE_DRAFT_PLAYING || mainStageChangePending()) return 0;
	s_Draft.phase = FORGE_DRAFT_RETURN_QUEUED;
	mainChangeToStage(s_Draft.stage);
	return 1;
}

s32 forgeLevelDraftIsPlaying(void)
{
	return s_Draft.phase == FORGE_DRAFT_PLAYING || s_Draft.phase == FORGE_DRAFT_RETURN_QUEUED;
}

s32 forgeLevelDraftOwnsStageTransition(void)
{
	return s_Draft.phase != FORGE_DRAFT_NONE;
}

void forgeLevelDraftStageLeaving(s32 next_stage)
{
	if (s_Draft.phase == FORGE_DRAFT_NONE) return;
	if (s_Draft.phase == FORGE_DRAFT_QUEUED && next_stage == s_Draft.stage
			&& g_NetMode == NETMODE_NONE
			&& fsBindScenarioSource(s_Draft.scenario_root, s_Draft.bytes, s_Draft.size)) {
		s_Draft.phase = FORGE_DRAFT_LOADING;
		sysLogPrintf(LOG_NOTE, "FORGE.DRAFT.BIND: stage=0x%02x", next_stage);
		return;
	}
	s32 returning = s_Draft.phase == FORGE_DRAFT_RETURN_QUEUED && next_stage == s_Draft.stage;
	fsBindScenarioSource(NULL, NULL, 0);
	free(s_Draft.bytes);
	s_Draft.bytes = NULL;
	s_Draft.size = 0;
	s_Draft.phase = returning ? FORGE_DRAFT_RETURNING : FORGE_DRAFT_NONE;
}

s32 forgeLevelDraftStageReady(void)
{
	if (s_Draft.phase == FORGE_DRAFT_LOADING) {
		s_Draft.phase = FORGE_DRAFT_PLAYING;
		sysLogPrintf(LOG_NOTE, "FORGE.DRAFT.PLAYING: stage=0x%02x", s_Draft.stage);
		return 1;
	}
	if (s_Draft.phase != FORGE_DRAFT_RETURNING) return 0;
	s_Draft.phase = FORGE_DRAFT_NONE;
	/* A save over the same ID can replace the stage on disk while the
	 * editor still shows its previous preview baseline. */
	forge_level_source_t *source = calloc(1, sizeof(*source));
	if (source) {
		forge_level_doc_t *opened = forgeLevelOpen(s_Session.arena_id, source, NULL, 0);
		if (opened) {
			forgeLevelDocFree(s_Session.opened);
			s_Session.opened = opened;
			s_Session.generation++;
		}
		free(source);
	}
	sysLogPrintf(LOG_NOTE, "FORGE.DRAFT.RETURNED: stage=0x%02x unsaved=%d undo=%d",
		s_Draft.stage, forgeLevelSessionHasUnsavedChanges(), s_Undo.undo_count);
	return -1;
}

s32 forgeLevelDraftStageFailed(void)
{
	s32 recover = s_Draft.phase == FORGE_DRAFT_LOADING;
	if (recover) s_Draft.phase = FORGE_DRAFT_RETURN_QUEUED;
	forgeLevelDraftStageLeaving(recover ? s_Draft.stage : -1);
	return recover;
}

s32 forgeLevelDebugEditDraft(char *err, size_t err_cap)
{
	forge_level_spawn_t spawn;
	forge_level_pad_t pad;
	char id[32];
	if (!forgeLevelSessionOpen(s_Session.arena_id, err, err_cap)) return 0;
	if (sysArgCheck("--debug-grid-match-sources")) {
		forge_revision_t pinned;
		grid_match_sources_t sources = {0};
		s32 reviewed = forgeLevelSessionPinSavedRevision(&pinned, err, err_cap)
			&& forgeLevelSessionReviewMatchSources(&sources, err, err_cap);
		gridMatchSourcesFree(&sources);
		if (!reviewed) return 0;
	}
	forge_level_doc_t *draft = forgeLevelDocClone(s_Session.doc);
	if (!draft || !forgeLevelDocGetSpawn(draft, 0, &spawn)
			|| !forgeLevelDocFindPad(draft, spawn.pad_ref, &pad)) {
		forgeLevelDocFree(draft);
		levelFail(err, err_cap, "could not find a draft test start", "");
		return 0;
	}
	f32 start[3], crate[3];
	levelStepFrom(&pad, 160.0f, start);
	levelStepFrom(&pad, 280.0f, crate);
	const scenario_stage_atmosphere_t look = {
		.present = 1, .has_sky = 1, .sky_rgb = { 30, 61, 104 }
	};
	if (!forgeLevelDocAddSpawn(draft, start, pad.look, NULL, id, sizeof(id), err, err_cap)
			|| !forgeLevelDocAddObject(draft, FORGE_LEVEL_NEW_AMMO_CRATE, 0, crate,
				pad.look, id, sizeof(id), err, err_cap)
			|| !forgeLevelDocSetHill(draft, spawn.pad_ref, 1, err, err_cap)
			|| !forgeLevelDocSetAtmosphere(draft, &look, err, err_cap)) {
		forgeLevelDocFree(draft);
		return 0;
	}
	s_Undo.pending = s_Session.doc;
	s_Session.doc = draft;
	forgeLevelSessionEditEnd(1);
	return 1;
}

forge_level_doc_t *forgeLevelReadScenarioDocument(const void *scenario, u32 scenario_size,
		char *err, size_t err_cap)
{
	forge_level_texts_t texts = {0};
	void *members[FORGE_LEVEL_MEMBER_COUNT] = {0};
	forge_level_doc_t *doc = NULL;
	u32 remaining = LEVEL_ARCHIVE_MAX_BYTES;
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
		u32 size = 0;
		s32 absent = 0;
		const char *member = forgeLevelDocMemberName(i);
		members[i] = gridMatchPackageReadMember(scenario, scenario_size, member,
			remaining ? remaining : 1, &size, &absent);
		if ((!members[i] && (!forgeLevelDocMemberOptional(i) || !absent)) || size > remaining) {
			levelFail(err, err_cap, "the level's scenario member is missing or outside its source budget", member);
			goto done;
		}
		remaining -= size;
		texts.text[i] = members[i];
		texts.size[i] = size;
	}
	doc = forgeLevelDocRead(&texts, err, err_cap);
done:
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) free(members[i]);
	return doc;
}

static s32 levelDescriptorValueMatches(const ini_section_t *ini, const char *key, const char *expected)
{
	s32 found = 0;
	if (!expected[0]) return 0;
	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) continue;
		if (found || strcmp(ini->pairs[i].value, expected) != 0) return 0;
		found = 1;
	}
	return found;
}

static s32 levelCheckPackedIdentity(const forge_level_source_t *source, const char *scenario_id,
		const void *archive, u32 archive_size, const void *scenario, u32 scenario_size,
		char *err, size_t err_cap)
{
	ini_section_t *ini = malloc(sizeof(*ini));
	const s32 ok = ini
		&& levelReadDescriptor(archive, archive_size, source->archive_path, ini, err, err_cap)
		&& levelDescriptorValueMatches(ini, "catalog_id", source->arena_id)
		&& levelDescriptorValueMatches(ini, "scenario_archive", source->scenario_member)
		&& levelDescriptorValueMatches(ini, "scenario", scenario_id)
		&& levelReadDescriptor(scenario, scenario_size, source->scenario_member, ini, err, err_cap)
		&& levelDescriptorValueMatches(ini, "catalog_id", scenario_id);
	free(ini);
	if (!ok && (!err || !err_cap || !err[0])) {
		levelFail(err, err_cap, "the packed arena or scenario identity does not match the catalog", source->arena_id);
	}
	return ok;
}

static forge_level_doc_t *levelOpenSource(const char *arena_id, forge_level_source_t *source,
		s32 *out_original, char *err, size_t err_cap)
{
	const asset_entry_t *arena;
	const char *member;
	const char *cut;
	forge_level_doc_t *doc = NULL;
	void *archive = NULL;
	void *scenario = NULL;
	u32 archive_size = 0;
	u32 scenario_size = 0;
	char reason[256];

	if (err && err_cap) err[0] = '\0';
	if (out_original) *out_original = 0;
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
	const char *first = strstr(member, "::");
	cut = first;
	for (const char *p = first; p; p = strstr(p + 2, "::")) cut = p;
	if (!cut || !cut[2]) {
		levelFail(err, err_cap, "the level does not keep its scenario inside its own archive", arena_id);
		return NULL;
	}
	const s32 packed = first != cut;
	reason[0] = '\0';
	if (strlen(arena_id) >= sizeof(source->arena_id) || strlen(cut + 2) >= sizeof(source->scenario_member)) {
		levelFail(err, err_cap, "the level archive identity is too long", arena_id);
		return NULL;
	}
	if (packed) {
		/* The scanner records <wrapper>::<arena>::arena.ini beside
		 * <wrapper>::<arena>::<scenario>. Both must name the same owner. */
		const size_t root_length = (size_t)(cut - member);
		if (strstr(first + 2, "::") != cut || root_length >= sizeof(source->archive_path)
				|| strncmp(arena->descriptor_path, member, root_length) != 0
				|| strcmp(arena->descriptor_path + root_length, "::arena.ini") != 0
				|| assetArchiveTypeForPath(cut + 2) != ASSET_SCENARIO
				|| assetPathIsAbsolute(cut + 2) || assetPathHasParentTraversal(cut + 2)
				|| strchr(cut + 2, ':') || strchr(cut + 2, '\\')) {
			levelFail(err, err_cap, "the packed scenario must belong to the catalog arena", arena_id);
			return NULL;
		}
		memcpy(source->archive_path, member, root_length);
	} else if (!modAuthorRuntimeTemplatePath(arena_id, source->archive_path, sizeof(source->archive_path),
			reason, sizeof(reason))) {
		levelFail(err, err_cap, "the level's archive was not found", reason);
		return NULL;
	}
	snprintf(source->arena_id, sizeof(source->arena_id), "%s", arena_id);
	snprintf(source->scenario_member, sizeof(source->scenario_member), "%s", cut + 2);

	archive = packed ? levelLoadPackedArena(source->archive_path, &archive_size)
		: fsFileLoad(source->archive_path, &archive_size);
	if (!archive || archive_size == 0) {
		levelFail(err, err_cap, "the level's archive could not be read", source->archive_path);
		goto done;
	}
	if (packed && assetArchiveValidateBytes(archive, archive_size, source->archive_path,
			ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) != 0) goto done;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(archive, archive_size, digest);
	sha256ToHex(digest, source->archive_sha256);
	scenario = modArchiveExtractMemAlloc(archive, archive_size, source->scenario_member, &scenario_size);
	if (!scenario || scenario_size == 0) {
		levelFail(err, err_cap, "the level's archive has no scenario at", source->scenario_member);
		goto done;
	}
	if (packed && !levelCheckPackedIdentity(source, arena->ext.arena.scenario_id, archive,
			archive_size, scenario, scenario_size, err, err_cap)) goto done;
	reason[0] = '\0';
	doc = forgeLevelReadScenarioDocument(scenario, scenario_size, reason, sizeof(reason));
	if (!doc) {
		levelFail(err, err_cap, "the level's files were refused", reason);
		goto done;
	}
	if (out_original && !packed && arena->grid_offer_only && strncmp(arena_id, "base:", 5) != 0) {
		*out_original = gridArchiveOrigin(source->archive_path, archive, archive_size) == GRID_ARCHIVE_ORIGINAL;
	}
	sysLogPrintf(LOG_NOTE,
		"FORGE.LEVEL.OPEN: arena=%s pads=%d spawns=%d objects=%d field_rows=%d archive='%s' scenario='%s'",
		source->arena_id, forgeLevelDocPadCount(doc), forgeLevelDocSpawnCount(doc),
		forgeLevelDocObjectCount(doc), forgeLevelDocFieldRowCount(doc), source->archive_path,
		source->scenario_member);

done:
	free(scenario);
	if (archive) sysMemFree(archive);
	return doc;
}

forge_level_doc_t *forgeLevelOpen(const char *arena_id, forge_level_source_t *source,
		char *err, size_t err_cap)
{
	return levelOpenSource(arena_id, source, NULL, err, err_cap);
}

const char *forgeLevelCloneProvenance(const char *arena_id, const char *archive)
{
	if (arena_id && strncmp(arena_id, "base:", 5) == 0) return GRID_ARENA_PROVENANCE_BYOR;
	u32 size = 0;
	void *bytes = archive ? fsFileLoad(archive, &size) : NULL;
	grid_archive_origin_e origin = bytes ? gridArchiveOrigin(archive, bytes, size) : GRID_ARCHIVE_INVALID;
	if (bytes) sysMemFree(bytes);
	if (origin == GRID_ARCHIVE_BYOR) return GRID_ARENA_PROVENANCE_BYOR;
	if (origin == GRID_ARCHIVE_ORIGINAL) return GRID_ARENA_PROVENANCE_ORIGINAL;
	return GRID_ARENA_PROVENANCE_UNVERIFIED;
}

static s32 levelRememberPreviousSave(const mod_author_result_t *target,
	const forge_level_source_t *source, char *err, size_t err_cap)
{
	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!fsPathIsAbsolute(directory) || !fsCreateDir(directory)) {
		levelFail(err, err_cap, "could not open the profile's revision folder", "");
		return MOD_AUTHOR_ERR_WRITE;
	}
	forge_revision_t previous;
	if (!forgeRevisionRememberPrevious(directory, source->arena_id, target->archive_path,
			source->archive_sha256, &previous, err, err_cap)) return MOD_AUTHOR_ERR_WRITE;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.PREVIOUS: retained arena=%s sha256=%s", previous.arena_id, previous.digest);
	return MOD_AUTHOR_OK;
}

/* The folder manifest and both live provider rows must still describe the
 * exact same owner. The catalog category is an authored display group. */
static s32 levelInPlaceOwner(const mod_author_result_t *target, const forge_level_source_t *source,
	const char *name, char scenario_id[CATALOG_ID_LEN], s32 check_files, s32 require_resident)
{
	if (g_NetMode != NETMODE_NONE || s_Draft.phase != FORGE_DRAFT_NONE || mainStageChangePending()
			|| !gridMatchRuntimeIsIdle() || strstr(source->archive_path, "::")) return 0;
	const modinfo_t *mod = modmgrFindMod(target->mod_id);
	const asset_entry_t *arena = assetCatalogResolve(source->arena_id);
	if (!mod || !mod->valid || !mod->enabled || !mod->loaded || !mod->has_modjson
			|| mod->is_archive || mod->session_only || mod->is_template || mod->pending_restart
			|| strcmp(mod->name, name) || !modsRootsSameDir(mod->dirpath, target->mod_dir)
			|| !arena || arena->type != ASSET_ARENA || arena->bundled || arena->temporary || !arena->enabled
		|| (require_resident && arena->ext.arena.stagenum != g_StageNum)
		|| strcmp(arena->ext.arena.display_name, name)) return 0;
	const asset_entry_t *scenario = assetCatalogResolve(arena->ext.arena.scenario_id);
	if (!scenario || scenario->type != ASSET_SCENARIO || scenario->bundled || scenario->temporary
			|| !scenario->enabled
		|| scenario->ext.scenario.stagenum != arena->ext.arena.stagenum) return 0;
	char opened[FS_MAXPATH + 1], chain[FS_MAXPATH + 1], expected[FS_MAXPATH + 1];
	fsFullPath(source->archive_path, opened, sizeof(opened));
	if (!fsPathIsAbsolute(opened) || !modsRootsSameDir(opened, target->archive_path)
			|| !assetPathJoinChecked(expected, sizeof(expected), source->archive_path, "::", "arena.ini")
			|| strcmp(expected, arena->descriptor_path)
			|| !assetPathJoinChecked(chain, sizeof(chain), source->archive_path, "::", source->scenario_member)
			|| strcmp(chain, arena->ext.arena.scenario_archive)
			|| !assetPathJoinChecked(expected, sizeof(expected), chain, "::", "scenario.ini")
			|| strcmp(expected, scenario->descriptor_path)) return 0;
	const char *arena_file = fileProviderPath(catalogEffectiveHandle(arena));
	const char *scene_file = fileProviderPath(catalogEffectiveHandle(scenario));
	const size_t chain_len = strlen(chain);
	if (!arena_file || strcmp(arena_file, chain) || !scene_file
			|| strncmp(scene_file, chain, chain_len) || strncmp(scene_file + chain_len, "::", 2)) return 0;
	if (!check_files) return 1;
	const DWORD file_flags = GetFileAttributesA(target->archive_path);
	const DWORD owner_flags = GetFileAttributesA(target->mod_dir);
	return file_flags != INVALID_FILE_ATTRIBUTES && !(file_flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
		&& owner_flags != INVALID_FILE_ATTRIBUTES && (owner_flags & FILE_ATTRIBUTE_DIRECTORY)
		&& !(owner_flags & FILE_ATTRIBUTE_REPARSE_POINT)
		&& assetPathCopyChecked(scenario_id, CATALOG_ID_LEN, scenario->id);
}

static s32 levelArchiveDescriptorDigests(const void *archive, u32 size, const char *scenario_member,
	u8 digests[2][SHA256_DIGEST_SIZE])
{
	const u32 max_descriptor = 1024u * 1024u;
	u32 length = 0, scenario_size = 0;
	void *descriptor = gridMatchPackageReadMember(archive, size, "arena.ini", max_descriptor, &length, NULL);
	if (!descriptor || !length) {
		free(descriptor);
		return 0;
	}
	sha256Hash(descriptor, length, digests[0]);
	free(descriptor);
	void *scenario = gridMatchPackageReadMember(archive, size, scenario_member,
		LEVEL_ARCHIVE_MAX_BYTES, &scenario_size, NULL);
	descriptor = scenario ? gridMatchPackageReadMember(scenario, scenario_size, "scenario.ini", max_descriptor, &length, NULL) : NULL;
	free(scenario);
	const s32 valid = descriptor && length;
	if (valid) sha256Hash(descriptor, length, digests[1]);
	free(descriptor);
	return valid;
}

static s32 levelValidateBoundSaveCandidate(const char *locator, const char *arena_id,
	const void *archive, u32 size, char *err, size_t err_cap);

static s32 levelSaveInPlaceArchive(const mod_author_result_t *target, const forge_level_source_t *source,
	const char *name, const char *scenario_id, const void *archive, u32 size,
	u32 catalog_generation, s32 stage_index, s32 require_resident, char *err, size_t err_cap)
{
	u8 before[2][SHA256_DIGEST_SIZE], after[2][SHA256_DIGEST_SIZE];
	const s32 length = fsFileSize(target->archive_path);
	u32 prior_size = 0;
	void *prior = length > 0 && (u32)length <= LEVEL_ARCHIVE_MAX_BYTES ? fsFileLoad(target->archive_path, &prior_size) : NULL;
	if (!prior || prior_size != (u32)length) {
		if (prior) sysMemFree(prior);
		levelFail(err, err_cap, "the saved source could not be read", source->arena_id);
		return MOD_AUTHOR_ERR_TEMPLATE;
	}
	u8 prior_digest[SHA256_DIGEST_SIZE];
	sha256Hash(prior, prior_size, prior_digest);
	if (!levelCheckArchiveDigest(prior_digest, source->archive_sha256, err, err_cap)) {
		sysMemFree(prior);
		return MOD_AUTHOR_ERR_TEMPLATE;
	}
	const s32 descriptors_match = levelArchiveDescriptorDigests(prior, prior_size, source->scenario_member, before)
		&& levelArchiveDescriptorDigests(archive, size, source->scenario_member, after)
		&& memcmp(before, after, sizeof(before)) == 0;
	sysMemFree(prior);
	if (!descriptors_match) {
		levelFail(err, err_cap, "the in-place save would change its archive descriptors", source->arena_id);
		return MOD_AUTHOR_ERR_ARCHIVE;
	}
	if (!levelValidateBoundSaveCandidate(target->archive_path, source->arena_id, archive, size, err, err_cap)) {
		return MOD_AUTHOR_ERR_ARCHIVE;
	}
	save_atomic_file_t write;
	if (saveAtomicBegin(&write, target->archive_path) != 0) {
		levelFail(err, err_cap, "could not stage the arena for an in-place save", source->arena_id);
		return MOD_AUTHOR_ERR_WRITE;
	}
	s32 code = MOD_AUTHOR_ERR_WRITE;
	if (fwrite(archive, 1, size, saveAtomicStream(&write)) != size) {
		levelFail(err, err_cap, "could not write the staged arena", source->arena_id);
	} else {
		char bound_scenario[CATALOG_ID_LEN];
		const modinfo_t *mod = modmgrFindMod(target->mod_id);
		const DWORD manifest_flags = GetFileAttributesA(target->manifest_path);
		u8 manifest_digest[SHA256_DIGEST_SIZE];
		if (assetCatalogGetGeneration() != catalog_generation || g_StageIndex != stage_index
			|| !levelInPlaceOwner(target, source, name, bound_scenario, 1, require_resident)
			|| strcmp(bound_scenario, scenario_id)
				|| !mod || manifest_flags == INVALID_FILE_ATTRIBUTES
				|| (manifest_flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
				|| sha256HashFile(target->manifest_path, manifest_digest) != 0
				|| memcmp(manifest_digest, mod->sha256, sizeof(manifest_digest)) != 0) {
			levelFail(err, err_cap, "the in-place stage or catalog binding changed during save", source->arena_id);
			code = MOD_AUTHOR_ERR_TEMPLATE;
		} else if ((code = levelRememberPreviousSave(target, source, err, err_cap)) == MOD_AUTHOR_OK) {
			if (!levelSourceStillMatches(source, err, err_cap)) {
				code = MOD_AUTHOR_ERR_TEMPLATE;
			} else if (saveAtomicCommit(&write) == 0) {
				/* An unchanged dependency plan can follow this exact local save without
				 * releasing the mounts the resident stage is still using. */
				if (s_VariantDependencyMount.plan.packages
						&& strcmp(s_VariantDependencyMount.arena_id, source->arena_id) == 0
						&& memcmp(s_VariantDependencyMount.arena_sha256, prior_digest, sizeof(prior_digest)) == 0) {
					sha256Hash(archive, size, s_VariantDependencyMount.arena_sha256);
				}
				sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.SAVE.INPLACE: arena=%s archive='%s'", source->arena_id, target->archive_path);
				return MOD_AUTHOR_OK;
			} else {
				levelFail(err, err_cap, "could not replace the saved arena", source->arena_id);
				return MOD_AUTHOR_ERR_WRITE;
			}
		}
	}
	saveAtomicAbort(&write);
	return code;
}

static s32 levelSaveExisting(forge_level_doc_t *doc, const forge_level_source_t *source,
	const char *name, s32 original, mod_author_result_t *target, s32 require_resident, char *err, size_t err_cap)
{
	const u32 catalog_generation = assetCatalogGetGeneration();
	const s32 stage_index = g_StageIndex;
	char scenario_id[CATALOG_ID_LEN];
	if (!target->updated || strcmp(target->catalog_id, source->arena_id)
			|| !levelInPlaceOwner(target, source, name, scenario_id, 1, require_resident)) {
		levelFail(err, err_cap, "the opened level no longer has its exact loaded owner. Use a new name", source->arena_id);
		return MOD_AUTHOR_ERR_TEMPLATE;
	}
	char path[FS_MAXPATH + 1];
	u32 size;
	void *archive = original
		? levelBuildValidatedOriginalDraft(doc, source, &size, NULL, "forge", &target->ids, err, err_cap)
		: levelBuildPrivateDraftArchive(doc, source, path, &size, "forge", &target->ids, err, err_cap);
	if (!archive) return MOD_AUTHOR_ERR_ARCHIVE;

	s32 code = MOD_AUTHOR_ERR_TEMPLATE;
	if (target->ids.archive_count == 2 && target->ids.id_count == 2
			&& !strcmp(target->ids.ids[0].old_id, source->arena_id)
			&& !strcmp(target->ids.ids[0].new_id, source->arena_id)
			&& !strcmp(target->ids.ids[1].old_id, scenario_id)
			&& !strcmp(target->ids.ids[1].new_id, scenario_id)) {
		code = levelSaveInPlaceArchive(target, source, name, scenario_id,
			archive, size, catalog_generation, stage_index, require_resident, err, err_cap);
	} else {
		levelFail(err, err_cap, "the opened level cannot keep its exact arena and scenario bindings", source->arena_id);
	}
	sysMemFree(archive);
	return code;
}

static s32 levelForkSource(const forge_revision_t *revision, forge_level_source_t *source, char *err, size_t err_cap);
static s32 levelForkCategory(const char *mod_dir, char *category, size_t capacity);
typedef struct level_save_parent {
	char map_sha256[SHA256_HEX_SIZE];
	char revision_sha256[SHA256_HEX_SIZE];
} level_save_parent_t;

static s32 levelSaveDigestValid(const char *value)
{
	return value && strlen(value) == SHA256_HEX_SIZE - 1
		&& strspn(value, "0123456789abcdef") == SHA256_HEX_SIZE - 1;
}

/* Name the immediate source for a distinct copy. This neither moves a
 * current pin nor makes the saved level eligible for distribution. */
static s32 levelSaveParentIdentity(const forge_level_source_t *source, const char *input,
		level_save_parent_t *parent, char *err, size_t err_cap)
{
	u8 digest[SHA256_DIGEST_SIZE];
	if (!source->arena_id[0] || modArchiveSha256(input, digest) != 0) {
		levelFail(err, err_cap, "could not identify the exact parent revision", source->arena_id);
		return 0;
	}
	sha256ToHex(digest, parent->revision_sha256);
	if (source->archive_sha256[0] && strcmp(source->archive_sha256, parent->revision_sha256) != 0) {
		levelFail(err, err_cap, "the parent archive changed before saving", source->arena_id);
		return 0;
	}
	sha256Hash(source->arena_id, strlen(source->arena_id), digest);
	sha256ToHex(digest, parent->map_sha256);
	return 1;
}

static s32 levelReadCopyBinding(const char *path, s32 *bound, char digest[SHA256_HEX_SIZE],
		level_save_parent_t *parent, char *err, size_t err_cap);

/* Use the same author as the first Save As, retaining its generated IDs.
 * The new folder is private scratch and is never passed to the Mod manager. */
static s32 levelSaveLastDestination(const mod_author_request_t *request, const forge_level_source_t *source,
	const forge_revision_t *saved, mod_author_result_t *target, char *err, size_t err_cap)
{
	const u32 catalog_generation = assetCatalogGetGeneration();
	const s32 stage_index = g_StageIndex;
	forge_revision_t bound = *saved;
	forge_level_source_t destination = {0};
	char scenario_id[CATALOG_ID_LEN];
	if (!modAuthorRuntimeTemplatePath(saved->arena_id, bound.path, sizeof(bound.path), NULL, 0)
			|| !modsRootsSameDir(saved->path, bound.path)
			|| !levelForkSource(&bound, &destination, err, err_cap)
			|| !levelInPlaceOwner(target, &destination, target->display_name, scenario_id, 1, 0)) {
		if (!err || !err_cap || !err[0]) {
			levelFail(err, err_cap, "the last saved level no longer matches its exact owned destination", target->catalog_id);
		}
		return MOD_AUTHOR_ERR_TEMPLATE;
	}
	char dependency_plan[SHA256_HEX_SIZE] = "";
	s32 bound_destination = 0;
	level_save_parent_t parent = {0};
	if (!levelReadCopyBinding(destination.archive_path, &bound_destination, dependency_plan, &parent, err, err_cap)) {
		return MOD_AUTHOR_ERR_ARCHIVE;
	}

	mod_author_result_t *candidate = calloc(1, sizeof(*candidate));
	if (!candidate) {
		levelFail(err, err_cap, "out of memory preparing the saved level", target->catalog_id);
		return MOD_AUTHOR_ERR_WRITE;
	}
	static u32 serial;
	char staging[MOD_AUTHOR_PATH_MAX];
	const s32 length = snprintf(staging, sizeof(staging), "%s/.grid-save-%llx-%u", request->mods_root,
		(unsigned long long)sysGetMicroseconds(), (unsigned)++serial);
	if (length <= 0 || (size_t)length >= sizeof(staging) || _mkdir(staging) != 0) {
		free(candidate);
		levelFail(err, err_cap, "could not create the private save folder", target->catalog_id);
		return MOD_AUTHOR_ERR_WRITE;
	}

	mod_author_request_t draft = *request;
	draft.mods_root = staging;
	draft.require_new_folder = 1;
	const mod_author_descriptor_value_t fields[] = {
		request->descriptor_values[0],
		{ FORGE_REVISION_PARENT_MAP_KEY, parent.map_sha256[0] ? parent.map_sha256 : NULL },
		{ FORGE_REVISION_PARENT_DIGEST_KEY, parent.revision_sha256[0] ? parent.revision_sha256 : NULL },
		{ FORGE_REVISION_DEPENDENCY_PLAN_KEY, bound_destination ? dependency_plan : NULL },
	};
	draft.descriptor_values = fields;
	draft.descriptor_value_count = ARRAYCOUNT(fields);
	s32 code = modAuthorSave(&draft, candidate, err, err_cap);
	if (code == MOD_AUTHOR_OK) {
		u32 size = 0;
		const s32 bytes = fsFileSize(candidate->archive_path);
		void *archive = bytes > 0 && (u32)bytes <= LEVEL_ARCHIVE_MAX_BYTES
			? fsFileLoad(candidate->archive_path, &size) : NULL;
		char category[MOD_AUTHOR_PATH_MAX];
		const s32 archive_removed = remove(candidate->archive_path) == 0;
		const s32 manifest_removed = remove(candidate->manifest_path) == 0;
		const s32 folder_removed = _rmdir(candidate->mod_dir) == 0;
		const s32 category_removed = levelForkCategory(candidate->mod_dir, category, sizeof(category))
			&& _rmdir(category) == 0;
		if (!archive || size != (u32)bytes || !archive_removed || !manifest_removed
				|| !folder_removed || !category_removed) {
			levelFail(err, err_cap, "could not read and retire the private save candidate", target->catalog_id);
			code = MOD_AUTHOR_ERR_WRITE;
		} else if (!levelSourceStillMatches(source, err, err_cap)) {
			code = MOD_AUTHOR_ERR_TEMPLATE;
		} else if (candidate->ids.archive_count != 2 || candidate->ids.id_count != 2
				|| strcmp(candidate->ids.ids[0].old_id, source->arena_id)
				|| strcmp(candidate->ids.ids[0].new_id, target->catalog_id)
				|| strcmp(candidate->ids.ids[1].new_id, scenario_id)) {
			levelFail(err, err_cap, "the save cannot retain its exact arena and scenario bindings", target->catalog_id);
			code = MOD_AUTHOR_ERR_TEMPLATE;
		} else {
			code = levelSaveInPlaceArchive(target, &destination, target->display_name, scenario_id,
				archive, size, catalog_generation, stage_index, 0, err, err_cap);
			if (code == MOD_AUTHOR_OK) target->ids = candidate->ids;
		}
		if (archive) sysMemFree(archive);
	}
	if (_rmdir(staging) != 0) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL: could not remove private save folder '%s'", staging);
	}
	free(candidate);
	return code;
}

static s32 levelSaveVariantAwareCopy(forge_level_doc_t *doc, const forge_level_source_t *source,
	const mod_author_request_t *request, const grid_match_sources_t *checked, const char *dependency_plan,
	const level_save_parent_t *parent, mod_author_result_t *target, s32 *handled, char *err, size_t err_cap);

static s32 levelSaveLocalCopy(const mod_author_request_t *request, mod_author_result_t *target,
		char *err, size_t err_cap)
{
	const s32 code = modAuthorSave(request, target, err, err_cap);
	if (code != MOD_AUTHOR_OK) return code;

	/* Keep the opened arena's resident catalog slot. The generic Mods
	 * apply path rebuilds the whole catalog and can renumber that arena. */
	u8 digest[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE], reason[192] = "";
	if (modArchiveSha256(target->archive_path, digest) != 0) {
		snprintf(reason, sizeof(reason), "The new arena could not be verified.");
	} else {
		sha256ToHex(digest, hex);
		if (modmgrEnableAuthoredGridVariant(target->mod_dir, target->mod_id,
				target->catalog_id, hex, reason, sizeof(reason))) {
			catalog_stage_result_t stage;
			if (catalogResolveStageForType(target->catalog_id, ASSET_ARENA, &stage)
					&& stage.entry && strcmp(stage.entry->id, target->catalog_id) == 0
					&& stage.stagenum >= STAGENUM_CUSTOM_START && stage.stagenum < STAGENUM_CUSTOM_END
					&& stageTableOwnsCustom(stageGetIndex(stage.stagenum), stage.stagenum)) {
				sysLogPrintf(LOG_NOTE, "MODAUTHOR.ENABLE.OK mod_id=%s restart_count=0", target->mod_id);
				return MOD_AUTHOR_OK;
			}
			snprintf(reason, sizeof(reason), "No playable stage slot is available for the new arena.");
		}
	}

	/* The archive is saved even when its local library admission fails. */
	levelFail(err, err_cap, "The level was saved on disk, but the Grid library could not refresh", reason);
	sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.SAVE.LIBRARY: arena=%s %s", target->catalog_id, reason);
	return MOD_AUTHOR_OK;
}

static s32 levelSaveAs(forge_level_doc_t *doc, const forge_level_source_t *source,
		const char *display_name, s32 require_direct, const forge_revision_t *last_saved,
		const char *dependency_plan, const grid_match_sources_t *variant_sources,
		mod_author_result_t *result, char *err, size_t err_cap)
{
	forge_level_overrides_t *overrides = NULL;
	mod_author_request_t request = {0};
	s32 restarts = 0;
	s32 own = 0;
	s32 code;

	if (err && err_cap) err[0] = '\0';
	if (result) memset(result, 0, sizeof(*result));
	if (!doc || !source || !source->archive_path[0] || !display_name || !display_name[0]) {
		levelFail(err, err_cap, "a save needs an open level and a name", "");
		return MOD_AUTHOR_ERR_ARGS;
	}
	const s32 packed = strstr(source->archive_path, "::") != NULL;
	if (!packed && !levelSourceStillMatches(source, err, err_cap)) return MOD_AUTHOR_ERR_TEMPLATE;
	char scratch[FS_MAXPATH + 1];
	const char *input = levelArchiveInput(source->archive_path, source->archive_sha256, scratch, err, err_cap);
	if (!input) return MOD_AUTHOR_ERR_TEMPLATE;

	request.template_path = input;
	request.mods_root = modmgrGetModsDir();
	request.display_name = display_name;
	request.tool = "forge";
	const mod_author_descriptor_value_t descriptor_values[] = {
		{ GRID_ARENA_PROVENANCE_KEY, forgeLevelCloneProvenance(source->arena_id, input) },
		{ FORGE_REVISION_DEPENDENCY_PLAN_KEY, dependency_plan && dependency_plan[0] ? dependency_plan : NULL },
	};
	request.descriptor_values = descriptor_values;
	request.descriptor_value_count = dependency_plan ? 2 : 1;

	mod_author_result_t *target = calloc(1, sizeof(*target));
	if (!target || !request.mods_root || !request.mods_root[0]) {
		levelFail(err, err_cap, target ? "the game has no mods folder" : "out of memory", "");
		code = MOD_AUTHOR_ERR_WRITE;
		goto done;
	}
	code = modAuthorPlan(&request, target, err, err_cap);
	if (code != MOD_AUTHOR_OK) goto done;
	own = strcmp(target->catalog_id, source->arena_id) == 0;
	const s32 original = strcmp(descriptor_values[0].value, GRID_ARENA_PROVENANCE_ORIGINAL) == 0;
	if (require_direct && (!original || !own || packed)) {
		levelFail(err, err_cap, "the opened level is no longer an original at its exact save destination", source->arena_id);
		code = MOD_AUTHOR_ERR_TEMPLATE;
		goto done;
	}
	if (packed && own) {
		levelFail(err, err_cap, "save the packed level under a new name", "");
		code = MOD_AUTHOR_ERR_ARGS;
		goto done;
	}
	const s32 repeat = !own && target->updated && last_saved && last_saved->digest[0]
		&& !strcmp(last_saved->arena_id, target->catalog_id)
		&& modsRootsSameDir(last_saved->path, target->archive_path);
	if (!own && !repeat && (target->updated || assetCatalogResolveAny(target->catalog_id) || modmgrFindMod(target->mod_id))) {
		levelFail(err, err_cap, "choose a new name because that level already exists", target->catalog_id);
		code = MOD_AUTHOR_ERR_EXISTS;
		goto done;
	}

	/* A prior in-place save may have changed the template. Capture the
	 * current document even when the next save gives it a new identity. */
	overrides = calloc(1, sizeof(*overrides));
	if (!overrides) {
		levelFail(err, err_cap, "out of memory", "");
		code = MOD_AUTHOR_ERR_WRITE;
		goto done;
	}
	if (!levelAllOverrides(doc, source, input, overrides, err, err_cap)) {
		code = MOD_AUTHOR_ERR_ARGS;
		goto done;
	}

	if (own) {
		code = levelSaveExisting(doc, source, target->display_name, original, target, require_direct, err, err_cap);
	} else {
		/* Validate under the opened identity before the author assigns new IDs. */
		if (original && !forgeLevelValidateOriginalDraft(doc, source, NULL, err, err_cap)) {
			code = MOD_AUTHOR_ERR_ARCHIVE;
			goto done;
		}
		if (original && !levelSourceStillMatches(source, err, err_cap)) {
			code = MOD_AUTHOR_ERR_TEMPLATE;
			goto done;
		}
		level_save_parent_t parent = {0};
		if (!repeat && !levelSaveParentIdentity(source, input, &parent, err, err_cap)) {
			code = MOD_AUTHOR_ERR_TEMPLATE;
			goto done;
		}
		const mod_author_descriptor_value_t copy_fields[] = {
			descriptor_values[0],
			{ FORGE_REVISION_PARENT_MAP_KEY, parent.map_sha256 },
			{ FORGE_REVISION_PARENT_DIGEST_KEY, parent.revision_sha256 },
			descriptor_values[1],
		};
		if (!repeat) {
			request.descriptor_values = copy_fields;
			request.descriptor_value_count = dependency_plan ? ARRAYCOUNT(copy_fields) : ARRAYCOUNT(copy_fields) - 1;
		}
		request.overrides = overrides->entries;
		request.override_count = overrides->count;
		request.require_new_folder = 1;
		if (original && !repeat) {
			s32 handled = 0;
			code = levelSaveVariantAwareCopy(doc, source, &request, variant_sources, dependency_plan,
				&parent, target, &handled, err, err_cap);
			if (handled || code != MOD_AUTHOR_OK) goto done;
		}
		if (repeat) {
			code = levelSaveLastDestination(&request, source, last_saved, target, err, err_cap);
		} else if (doc == s_Session.doc && forgeSessionIsActive() && g_NetMode == NETMODE_NONE) {
			code = levelSaveLocalCopy(&request, target, err, err_cap);
		} else {
			code = modAuthorRuntimeSave(&request, target, &restarts, err, err_cap);
		}
	}

done:
	levelReleaseArchiveInput(scratch);
	if (code == MOD_AUTHOR_OK) {
		if (result) *result = *target;
		sysLogPrintf(LOG_NOTE,
			"FORGE.LEVEL.SAVE.OK: template=%s catalog=%s changed_members=%u restart_count=%d archive='%s' own_template=%d",
			source->arena_id, target->catalog_id, (unsigned)overrides->count, restarts, target->archive_path, own);
	} else {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.SAVE.FAIL: template=%s name='%s' code=%d reason='%s'",
			source->arena_id, display_name, code, err ? err : "");
	}
	free(overrides);
	free(target);
	return code;
}

s32 forgeLevelSaveAs(forge_level_doc_t *doc, const forge_level_source_t *source,
		const char *display_name, mod_author_result_t *result, char *err, size_t err_cap)
{
	return levelSaveAs(doc, source, display_name, 0, NULL, NULL, NULL, result, err, err_cap);
}

s32 forgeLevelSessionHasUnsavedChanges(void)
{
	return s_Session.doc && s_Session.revision != s_Session.saved_revision;
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
	if (!changed || !s_Session.doc) {
		forgeLevelDocFree(before);
		return;
	}
	if (before) {
		forge_level_snapshot_t snapshot = { before, s_Session.revision };
		undoPush(s_Undo.undo, &s_Undo.undo_count, snapshot);
		undoForgetRedo();
	} else {
		/* An edit without a snapshot breaks the undo chain. */
		undoForgetAll();
	}
	s_Session.revision = ++s_Session.next_revision;
	s_Session.generation++;
}

s32 forgeLevelSessionCommitAimedProp(const char *model_catalog_id, u32 expected_generation,
		const f32 screen[2], char *record_id, size_t record_cap, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (record_id && record_cap) record_id[0] = '\0';
	forge_level_prop_aim_t aim;
	if (expected_generation != s_Session.generation || !forgeLevelSessionAimProp(screen, &aim) || !aim.valid) {
		levelFail(err, err_cap, "aim at a placeable floor in the open Level File", "");
		return 0;
	}

	const asset_entry_t *model = model_catalog_id && model_catalog_id[0]
		? assetCatalogResolve(model_catalog_id) : NULL;
	if (!model || model->type != ASSET_MODEL || !model->enabled || model->temporary
			|| model->grid_offer_only || strcmp(model->id, model_catalog_id) != 0) {
		levelFail(err, err_cap, "the selected catalog model is unavailable", "");
		return 0;
	}
	if (s_Undo.pending) {
		levelFail(err, err_cap, "another level edit is pending", "");
		return 0;
	}
	forgeLevelSessionEditBegin();
	if (!s_Undo.pending) {
		levelFail(err, err_cap, "out of memory before placing the prop", "");
		return 0;
	}
	const s32 changed = forgeLevelDocAddProp(s_Session.doc, model_catalog_id, aim.position, aim.look,
		record_id, record_cap, err, err_cap);
	forgeLevelSessionEditEnd(changed);
	return changed;
}

s32 forgeLevelSessionUndo(void)
{
	if (!s_Session.doc || s_Undo.undo_count == 0) return 0;
	forge_level_snapshot_t before = s_Undo.undo[--s_Undo.undo_count];
	forge_level_snapshot_t current = { s_Session.doc, s_Session.revision };
	undoPush(s_Undo.redo, &s_Undo.redo_count, current);
	s_Session.doc = before.doc;
	s_Session.revision = before.revision;
	s_Session.generation++;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.UNDO: arena=%s pads=%d spawns=%d objects=%d undo_left=%d redo=%d",
		s_Session.source.arena_id, forgeLevelDocPadCount(before.doc), forgeLevelDocSpawnCount(before.doc),
		forgeLevelDocObjectCount(before.doc), s_Undo.undo_count, s_Undo.redo_count);
	return 1;
}

s32 forgeLevelSessionRedo(void)
{
	if (!s_Session.doc || s_Undo.redo_count == 0) return 0;
	forge_level_snapshot_t after = s_Undo.redo[--s_Undo.redo_count];
	forge_level_snapshot_t current = { s_Session.doc, s_Session.revision };
	undoPush(s_Undo.undo, &s_Undo.undo_count, current);
	s_Session.doc = after.doc;
	s_Session.revision = after.revision;
	s_Session.generation++;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.REDO: arena=%s pads=%d spawns=%d objects=%d undo=%d redo_left=%d",
		s_Session.source.arena_id, forgeLevelDocPadCount(after.doc), forgeLevelDocSpawnCount(after.doc),
		forgeLevelDocObjectCount(after.doc), s_Undo.undo_count, s_Undo.redo_count);
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
		forgeLevelSessionHasUnsavedChanges());
	forgeLevelDocFree(s_Session.doc);
	s_Session.doc = NULL;
	forgeLevelDocFree(s_Session.opened);
	s_Session.opened = NULL;
	memset(&s_Session.source, 0, sizeof(s_Session.source));
	s_Session.opened_original = 0;
	memset(&s_Session.saved_archive, 0, sizeof(s_Session.saved_archive));
	s_Session.parent_checked = 0;
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
	s32 original = 0;

	if (err && err_cap) err[0] = '\0';
	if (!source) {
		levelFail(err, err_cap, "out of memory", "");
		return 0;
	}
	/* Open first: a level that cannot be opened leaves the one already open
	 * as it was. */
	doc = levelOpenSource(arena_id, source, &original, err, err_cap);
	if (!doc) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.OPEN.FAIL: arena=%s reason='%s'", arena_id ? arena_id : "",
			err ? err : "");
		free(source);
		return 0;
	}
	forgeLevelSessionClose();
	s_Session.doc = doc;
	s_Session.revision = 0;
	s_Session.saved_revision = 0;
	s_Session.next_revision = 0;
	/* The world shows the level as it was opened until the live view
	 * (forge_level_live.h) learns what changed; without the copy it shows
	 * nothing of the document, and the document works as before. */
	s_Session.opened = forgeLevelDocClone(doc);
	s_Session.source = *source;
	s_Session.opened_original = original;
	snprintf(s_Session.saved_archive.arena_id, sizeof(s_Session.saved_archive.arena_id),
		"%s", source->arena_id);
	snprintf(s_Session.saved_archive.path, sizeof(s_Session.saved_archive.path),
		"%s", source->archive_path);
	memcpy(s_Session.saved_archive.digest, source->archive_sha256, SHA256_HEX_SIZE);
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

static s32 levelSessionSaveAs(const char *display_name, mod_author_result_t *result,
		s32 require_direct, char *err, size_t err_cap)
{
	if (!s_Session.doc) {
		if (result) memset(result, 0, sizeof(*result));
		levelFail(err, err_cap, "no level is open", "");
		return MOD_AUTHOR_ERR_ARGS;
	}
	mod_author_result_t *saved = result ? result : calloc(1, sizeof(*saved));
	if (!saved) {
		levelFail(err, err_cap, "out of memory", "");
		return MOD_AUTHOR_ERR_WRITE;
	}
	s32 status = levelSaveAs(s_Session.doc, &s_Session.source, display_name, require_direct,
		&s_Session.saved_archive, NULL, NULL, saved, err, err_cap);
	if (status == MOD_AUTHOR_OK) {
		s_Session.saved_revision = s_Session.revision;
		s_Session.saved_archive.digest[0] = '\0';
		snprintf(s_Session.saved_archive.arena_id, sizeof(s_Session.saved_archive.arena_id),
			"%s", saved->catalog_id);
		snprintf(s_Session.saved_archive.path, sizeof(s_Session.saved_archive.path),
			"%s", saved->archive_path);
		u8 digest[SHA256_DIGEST_SIZE];
		if (modArchiveSha256(saved->archive_path, digest) == 0) {
			sha256ToHex(digest, s_Session.saved_archive.digest);
		}
		if (strcmp(saved->catalog_id, s_Session.source.arena_id) == 0) {
			memcpy(s_Session.source.archive_sha256, s_Session.saved_archive.digest, SHA256_HEX_SIZE);
			s_Session.parent_checked = 0;
		}
	}
	if (!result) free(saved);
	return status;
}

s32 forgeLevelSessionSaveAs(const char *display_name, mod_author_result_t *result,
		char *err, size_t err_cap)
{
	return levelSessionSaveAs(display_name, result, 0, err, err_cap);
}

const char *forgeLevelSessionSaveTarget(void)
{
	const forge_level_source_t *source = &s_Session.source;
	const forge_revision_t *saved = &s_Session.saved_archive;
	if (!s_Session.doc || !s_Session.opened_original || !fsPathIsAbsolute(source->archive_path)
			|| !saved->digest[0] || strcmp(saved->arena_id, source->arena_id)
			|| strcmp(saved->digest, source->archive_sha256)
			|| !modsRootsSameDir(saved->path, source->archive_path)) return NULL;
	const asset_entry_t *arena = assetCatalogResolve(source->arena_id);
	if (!arena || arena->type != ASSET_ARENA || !arena->ext.arena.display_name[0]) return NULL;
	const char *name = arena->ext.arena.display_name;
	const mod_author_request_t request = {
		.template_path = source->archive_path,
		.mods_root = modmgrGetModsDir(),
		.display_name = name,
	};
	mod_author_result_t target;
	if (modAuthorNames(&request, &target, NULL, 0) != MOD_AUTHOR_OK || strcmp(target.catalog_id, source->arena_id)
		|| !levelInPlaceOwner(&target, source, name, NULL, 0, 1)) return NULL;
	return name;
}

s32 forgeLevelSessionSaveOpened(char *err, size_t err_cap)
{
	const char *target = forgeLevelSessionSaveTarget();
	char name[MOD_AUTHOR_NAME_MAX];
	if (!target || !assetPathCopyChecked(name, sizeof(name), target)) {
		levelFail(err, err_cap, "reopen the original Level File before saving to its opened archive", "");
		return MOD_AUTHOR_ERR_TEMPLATE;
	}
	return levelSessionSaveAs(name, NULL, 1, err, err_cap);
}

s32 forgeLevelSessionPinSavedRevision(forge_revision_t *revision, char *err, size_t err_cap)
{
	if (!s_Session.doc || forgeLevelSessionHasUnsavedChanges() || !s_Session.saved_archive.digest[0]) {
		levelFail(err, err_cap, "save the current Level File before pinning a revision", "");
		return 0;
	}
	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!fsPathIsAbsolute(directory) || strlen(directory) >= FS_MAXPATH - 96 || !fsCreateDir(directory)) {
		levelFail(err, err_cap, "the profile revision folder is unavailable", "");
		return 0;
	}
	const forge_revision_t *saved = &s_Session.saved_archive;
	char scratch[FS_MAXPATH + 1];
	const char *input = levelArchiveInput(saved->path, saved->digest, scratch, err, err_cap);
	const s32 published = input && forgeRevisionPublishLocal(directory, saved->arena_id,
		input, saved->digest, revision, err, err_cap);
	levelReleaseArchiveInput(scratch);
	if (!published) return 0;
	if (s_MatchShare.size && strcmp(s_MatchShare.revision.arena_id, revision->arena_id) == 0
			&& strcmp(s_MatchShare.revision.digest, revision->digest) != 0) {
		forgeLevelRevokeMatchSharing();
	}
	sysLogPrintf(LOG_NOTE, "FORGE.REVISION.PIN: arena=%s sha256=%s", revision->arena_id, revision->digest);
	return 1;
}

s32 forgeLevelSessionAllowMatchSharing(char *err, size_t err_cap)
{
	const forge_revision_t *saved = &s_Session.saved_archive;
	forge_revision_t revision;
	char directory[FS_MAXPATH + 1];
	if (err && err_cap) err[0] = '\0';
	if (!s_Session.doc || forgeLevelSessionHasUnsavedChanges() || !saved->digest[0]) {
		levelFail(err, err_cap, "save and pin the level before allowing sharing", "");
		return 0;
	}
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!forgeRevisionReadCurrent(directory, saved->arena_id, &revision, err, err_cap)) return 0;
	if (strcmp(revision.digest, saved->digest) != 0) {
		levelFail(err, err_cap, "pin the exact saved revision before allowing sharing", "");
		return 0;
	}
	const s32 length = fsFileSize(revision.path);
	if (length <= 0 || length > NET_DISTRIB_MAX_COMP
			|| strncmp(revision.arena_id, "base:", 5) == 0) {
		levelFail(err, err_cap, "sharing requires an original arena within the package size limit", "");
		return 0;
	}

	u32 size = 0;
	void *bytes = fsFileLoad(revision.path, &size);
	if (!bytes || size != (u32)length) {
		if (bytes) sysMemFree(bytes);
		levelFail(err, err_cap, "the pinned arena could not be read", "");
		return 0;
	}
	u8 digest[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, hex);
	const s32 original = strcmp(hex, revision.digest) == 0
		&& gridArchiveOrigin(revision.path, bytes, size) == GRID_ARCHIVE_ORIGINAL;
	sysMemFree(bytes);
	if (!original) {
		levelFail(err, err_cap, "only the exact pinned original arena can be shared", "");
		return 0;
	}
	s_MatchShare.revision = revision;
	memcpy(s_MatchShare.digest, digest, sizeof(s_MatchShare.digest));
	s_MatchShare.size = size;
	sysLogPrintf(LOG_NOTE, "FORGE.REVISION.SHARE: granted arena=%s sha256=%s bytes=%u",
		revision.arena_id, revision.digest, (unsigned)size);
	return 1;
}

const forge_revision_t *forgeLevelMatchSharePermission(void)
{
	return s_MatchShare.size ? &s_MatchShare.revision : NULL;
}

void forgeLevelRevokeMatchSharing(void)
{
	if (!s_MatchShare.size) return;
	sysLogPrintf(LOG_NOTE, "FORGE.REVISION.SHARE: revoked arena=%s sha256=%s",
		s_MatchShare.revision.arena_id, s_MatchShare.revision.digest);
	memset(&s_MatchShare, 0, sizeof(s_MatchShare));
}

static s32 levelCheckWeaponMatchSharing(const char *weapon_id,
		const grid_match_source_package_t *expected, s32 grant, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const asset_entry_t *entry = weapon_id && weapon_id[0] ? assetCatalogResolve(weapon_id) : NULL;
	grid_match_source_package_t source = {0};
	char chain[FS_MAXPATH + 1];
	if (!entry || !entry->enabled || entry->type != ASSET_WEAPON || entry->bundled || entry->temporary
			|| strcmp(entry->id, weapon_id) != 0 || strncmp(entry->id, "base:", 5) == 0
			|| !gridMatchPackageLocation(entry->descriptor_path, source.path, sizeof(source.path),
				chain, sizeof(chain), NULL) || chain[0] || assetArchiveTypeForPath(source.path) != ASSET_WEAPON) {
		levelFail(err, err_cap, "select a locally owned native weapon archive", "");
		return 0;
	}
	void *bytes = gridMatchPackageLoad(source.path, &source.size);
	if (!bytes || assetArchiveValidateBytes(bytes, source.size, source.path, ASSET_ARCHIVE_VALIDATE_RELEASE, NULL, 0) != 0
			|| gridArchiveOrigin(source.path, bytes, source.size) != GRID_ARCHIVE_ORIGINAL) {
		free(bytes);
		levelFail(err, err_cap, "only an original weapon marked by its author can be shared", "");
		return 0;
	}
	sha256Hash(bytes, source.size, source.digest);
	free(bytes);
	source.owner.type = ASSET_WEAPON;
	memcpy(source.owner.id, entry->id, sizeof(source.owner.id));
	memcpy(source.suffix, ".pdweapon", sizeof(".pdweapon"));
	if (expected && (expected->owner.type != source.owner.type
			|| strcmp(expected->owner.id, source.owner.id) != 0
			|| expected->size != source.size
			|| memcmp(expected->digest, source.digest, sizeof(source.digest)) != 0)) {
		levelFail(err, err_cap, "The weapon revision changed. Reopen Temporary Weapon Sharing.", "");
		return 0;
	}
	if (!grant) return 1;
	s_WeaponMatchShare = source;
	char digest[SHA256_HEX_SIZE];
	sha256ToHex(source.digest, digest);
	sysLogPrintf(LOG_NOTE, "FORGE.WEAPON.SHARE: granted weapon=%s sha256=%s bytes=%u",
		source.owner.id, digest, source.size);
	return 1;
}

s32 forgeLevelAllowWeaponMatchSharing(const char *weapon_id, char *err, size_t err_cap)
{
	return levelCheckWeaponMatchSharing(weapon_id, NULL, 1, err, err_cap);
}

static s32 levelCheckExactWeaponMatchSharing(const grid_match_source_package_t *candidate,
		s32 grant, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!candidate || candidate->owner.type != ASSET_WEAPON || candidate->bundled || !candidate->size
			|| !candidate->owner.id[0] || !memchr(candidate->owner.id, '\0', sizeof(candidate->owner.id))) {
		levelFail(err, err_cap, "select a verified original weapon package", "");
		return 0;
	}
	return levelCheckWeaponMatchSharing(candidate->owner.id, candidate, grant, err, err_cap);
}

s32 forgeLevelCanOfferExactWeaponMatchSharing(const grid_match_source_package_t *candidate)
{
	return levelCheckExactWeaponMatchSharing(candidate, 0, NULL, 0);
}

s32 forgeLevelAllowExactWeaponMatchSharing(const grid_match_source_package_t *candidate,
		char *err, size_t err_cap)
{
	return levelCheckExactWeaponMatchSharing(candidate, 1, err, err_cap);
}

const grid_match_source_package_t *forgeLevelWeaponMatchSharePermission(void)
{
	return s_WeaponMatchShare.size ? &s_WeaponMatchShare : NULL;
}

void forgeLevelRevokeWeaponMatchSharing(void)
{
	if (!s_WeaponMatchShare.size) return;
	sysLogPrintf(LOG_NOTE, "FORGE.WEAPON.SHARE: revoked weapon=%s", s_WeaponMatchShare.owner.id);
	memset(&s_WeaponMatchShare, 0, sizeof(s_WeaponMatchShare));
}

s32 forgeLevelMatchPackageShareAllowed(const grid_match_source_package_t *package,
		const void *exact_bytes, u32 size)
{
	if (!package || !exact_bytes || !size || size != package->size || size > NET_DISTRIB_MAX_COMP
			|| package->bundled || strncmp(package->owner.id, "base:", 5) == 0
			|| !gridMatchPackageTypeShareable(package->owner.type, package->suffix)) return 0;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(exact_bytes, size, digest);
	return memcmp(digest, package->digest, sizeof(digest)) == 0
		&& gridMatchPackageOrigin(package->suffix, exact_bytes, size) == GRID_ARCHIVE_ORIGINAL
		&& (strcmp(package->suffix, ".pdsong") != 0 || modSequenceGridSourceShareable(package->suffix, exact_bytes, size))
		&& (package->owner.type != ASSET_VEHICLE
			|| assetCatalogGridVehicleSourceShareable(package->suffix, exact_bytes, size))
		&& ((package->owner.type != ASSET_BODY && package->owner.type != ASSET_HEAD)
			|| assetCatalogGridBodyHeadSourceShareable(package->suffix, exact_bytes, size));
}

static s32 levelForkSource(const forge_revision_t *revision, forge_level_source_t *source,
		char *err, size_t err_cap)
{
	u32 size = 0;
	void *bytes = fsFileLoad(revision->path, &size);
	ini_section_t *ini = malloc(sizeof(*ini));
	const s32 ok = bytes && ini && levelReadDescriptor(bytes, size, revision->path, ini, err, err_cap)
		&& strcmp(iniGet(ini, "catalog_id", ""), revision->arena_id) == 0
		&& assetPathCopyChecked(source->scenario_member, sizeof(source->scenario_member),
			iniGet(ini, "scenario_archive", ""))
		&& assetArchiveTypeForPath(source->scenario_member) == ASSET_SCENARIO
		&& assetPathCopyChecked(source->arena_id, sizeof(source->arena_id), revision->arena_id)
		&& assetPathCopyChecked(source->archive_path, sizeof(source->archive_path), revision->path)
		&& assetPathCopyChecked(source->archive_sha256, sizeof(source->archive_sha256), revision->digest)
		&& levelSourceStillMatches(source, err, err_cap);
	free(ini);
	if (bytes) sysMemFree(bytes);
	if (!ok && err && err_cap && !err[0]) {
		levelFail(err, err_cap, "could not read the shared draft's pinned arena source", revision->arena_id);
	}
	return ok;
}

static forge_room_extension_t *levelBuildRoomExtension(const forge_level_source_t *source,
		s32 room, s32 extra_length, forge_level_doc_t *doc, char *err, size_t err_cap)
{
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(source->archive_path, &size);
	if (!bytes) {
		levelFail(err, err_cap, "could not read the pinned original arena", source->arena_id);
		return NULL;
	}
	u8 digest[SHA256_DIGEST_SIZE];
	char sha[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, sha);
	forge_room_extension_t *extension = NULL;
	if (strcmp(sha, source->archive_sha256) == 0
			&& gridArchiveOrigin(source->archive_path, bytes, size) == GRID_ARCHIVE_ORIGINAL) {
		size_t draft_rooms_size = 0;
		const char *draft_rooms = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_ROOMS, &draft_rooms_size);
		extension = forgeRoomCreateExtensionWithRoomBrightness(bytes, size, source->scenario_member, room,
			extra_length, draft_rooms, draft_rooms_size, err, err_cap);
	} else {
		levelFail(err, err_cap, "room extension needs exact pinned original geometry", source->arena_id);
	}
	free(bytes);
	return extension;
}

static s32 levelPreflightRoomFork(const mod_author_result_t *fork, char *err, size_t err_cap)
{
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(fork->archive_path, &size);
	if (!bytes) {
		levelFail(err, err_cap, "the extended variant could not be read for validation", fork->catalog_id);
		return 0;
	}
	u8 digest[SHA256_DIGEST_SIZE];
	char sha[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, sha);
	const s32 ok = gridMatchRuntimePreflightRoomExtension(fork->archive_path, bytes, size, fork->catalog_id, sha, err, err_cap);
	free(bytes);
	return ok;
}

static s32 levelForkCategory(const char *mod_dir, char *category, size_t capacity)
{
	const char *leaf = strrchr(mod_dir, '/');
	if (!leaf || leaf == mod_dir || (size_t)(leaf - mod_dir) >= capacity) return 0;
	memcpy(category, mod_dir, leaf - mod_dir);
	category[leaf - mod_dir] = '\0';
	return 1;
}

/* The normal Mods scan ignores dot directories. Keep the archive and its
 * manifest together while validating, then publish the directory without replacement. */
static s32 levelSaveCheckedRoomFork(const mod_author_request_t *request, mod_author_result_t *fork,
		char *err, size_t err_cap)
{
	char category[MOD_AUTHOR_PATH_MAX];
	if (!levelForkCategory(fork->mod_dir, category, sizeof(category))) {
		levelFail(err, err_cap, "the variant mod folder is invalid", "");
		return 0;
	}
	mod_author_result_t *candidate = calloc(1, sizeof(*candidate));
	if (!candidate) {
		levelFail(err, err_cap, "out of memory preparing the variant", "");
		return 0;
	}
	static u32 serial;
	char staging[MOD_AUTHOR_PATH_MAX];
	/* Keep the private directory short enough for nested archive scratch files. */
	const s32 length = snprintf(staging, sizeof(staging), "%s/.ge-%llx-%u", request->mods_root,
		(unsigned long long)sysGetMicroseconds(), (unsigned)++serial);
	if (length <= 0 || (size_t)length >= sizeof(staging) || _mkdir(staging) != 0) {
		levelFail(err, err_cap, "could not create the private variant folder", "");
		free(candidate);
		return 0;
	}
	mod_author_request_t draft = *request;
	draft.mods_root = staging;
	const s32 written = modAuthorSave(&draft, candidate, err, err_cap) == MOD_AUTHOR_OK;
	s32 published = 0;
	if (written && levelPreflightRoomFork(candidate, err, err_cap)) {
		if (fsCreateDir(category) && MoveFileExA(candidate->mod_dir, fork->mod_dir, MOVEFILE_WRITE_THROUGH)) {
			fork->ids = candidate->ids;
			published = 1;
		} else {
			levelFail(err, err_cap, "could not publish the new variant folder", fork->mod_dir);
		}
	}
	if (written) {
		if (!published) {
			remove(candidate->archive_path);
			remove(candidate->manifest_path);
			_rmdir(candidate->mod_dir);
		}
		if (levelForkCategory(candidate->mod_dir, category, sizeof(category))) _rmdir(category);
	}
	if (_rmdir(staging) != 0) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL: could not remove private variant folder '%s'", staging);
	}
	free(candidate);
	return published;
}

static s32 levelForkRevisionWithChecks(const forge_revision_t *revision, const char *name,
		forge_level_doc_t *doc, s32 room, s32 extra_length, s32 offline, s32 local_byor,
		mod_author_result_t *fork, char *err, size_t err_cap)
{
	const char *provenance = forgeLevelCloneProvenance(revision->arena_id, revision->path);
	if (local_byor && (strncmp(revision->arena_id, "base:", 5) == 0
			|| strcmp(provenance, GRID_ARENA_PROVENANCE_BYOR) != 0)) {
		levelFail(err, err_cap, "the shared draft needs a locally saved BYOR parent", revision->arena_id);
		return 0;
	}
	u8 digest[SHA256_DIGEST_SIZE];
	char parent_map[SHA256_HEX_SIZE];
	sha256Hash(revision->arena_id, strlen(revision->arena_id), digest);
	sha256ToHex(digest, parent_map);
	const mod_author_descriptor_value_t fields[] = {
		{ GRID_ARENA_PROVENANCE_KEY, provenance },
		{ FORGE_REVISION_PARENT_MAP_KEY, parent_map },
		{ FORGE_REVISION_PARENT_DIGEST_KEY, revision->digest }
	};
	mod_author_request_t request = {
		.template_path = revision->path,
		.mods_root = modmgrGetModsDir(),
		.display_name = name,
		.tool = "forge-fork",
		.descriptor_values = fields,
		.descriptor_value_count = ARRAYCOUNT(fields),
		.require_new_folder = 1
	};
	mod_author_result_t *target = calloc(1, sizeof(*target));
	if (!target) {
		levelFail(err, err_cap, "out of memory planning the fork", "");
		return 0;
	}
	forge_level_overrides_t *overrides = NULL;
	forge_room_extension_t *extension = NULL;
	asset_archive_clone_override_t combined[FORGE_LEVEL_MEMBER_COUNT + 4];
	s32 saved = 0;
	if (modAuthorPlan(&request, target, err, err_cap) != MOD_AUTHOR_OK) goto done;
	if (strcmp(revision->arena_id, target->catalog_id) == 0
			|| assetCatalogResolveAny(target->catalog_id) || (offline && modmgrFindMod(target->mod_id))) {
		levelFail(err, err_cap, "the fork needs a name that is available for a new level", "");
		goto done;
	}
	if (doc) {
		forge_level_source_t source = {0};
		if (!levelForkSource(revision, &source, err, err_cap)) goto done;
		overrides = calloc(1, sizeof(*overrides));
		if (!overrides) {
			levelFail(err, err_cap, "out of memory preparing the shared draft", "");
			goto done;
		}
		if (!levelAllOverrides(doc, &source, source.archive_path, overrides, err, err_cap)) goto done;
		request.overrides = overrides->entries;
		request.override_count = overrides->count;
		if (room) {
			extension = levelBuildRoomExtension(&source, room, extra_length, doc, err, err_cap);
			if (!extension) goto done;
			u32 count = 0;
			const asset_archive_clone_override_t *structural = forgeRoomExtensionEntries(extension, &count);
			char rooms_entry[FORGE_LEVEL_OVERRIDE_NAME_MAX];
			if (!structural || (count != 3 && count != 4) || request.override_count + count > ARRAYCOUNT(combined)
					|| !assetPathJoinChecked(rooms_entry, sizeof(rooms_entry), source.scenario_member, "::",
						forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_ROOMS))) {
				levelFail(err, err_cap, "incomplete native room extension", source.arena_id);
				goto done;
			}
			u32 structural_rooms = 0;
			for (u32 i = 0; i < count; i++) {
				if (strcmp(structural[i].entry, rooms_entry) == 0) structural_rooms++;
			}
			if (structural_rooms != 1) {
				levelFail(err, err_cap, "the extension did not produce one native room table", source.arena_id);
				goto done;
			}
			/* The builder edits geometry in the exact pinned room table. */
			u32 authored = 0;
			for (u32 i = 0; i < request.override_count; i++) {
				if (strcmp(request.overrides[i].entry, rooms_entry) != 0) combined[authored++] = request.overrides[i];
			}
			memcpy(combined + authored, structural, count * sizeof(*combined));
			request.overrides = combined;
			request.override_count = authored + count;
		}
	}
	/* A structural candidate can be written while a playtest is resident.
	 * Publishing its source must not refresh the active user's catalog. */
	if (room) saved = levelSaveCheckedRoomFork(&request, target, err, err_cap);
	else if (local_byor) saved = modAuthorSave(&request, target, err, err_cap) == MOD_AUTHOR_OK;
	else saved = modAuthorRuntimeSave(&request, target, NULL, err, err_cap) == MOD_AUTHOR_OK;
done:
	forgeRoomFreeExtension(extension);
	free(overrides);
	if (saved) *fork = *target;
	free(target);
	if (!saved) return 0;
	sysLogPrintf(LOG_NOTE, "FORGE.REVISION.FORK: original=%s parent=%s fork=%s",
			revision->arena_id, revision->digest, fork->catalog_id);
	return 1;
}

static s32 levelForkRevision(const forge_revision_t *revision, const char *name,
		forge_level_doc_t *doc, s32 room, s32 extra_length, mod_author_result_t *fork,
		char *err, size_t err_cap)
{
	return levelForkRevisionWithChecks(revision, name, doc, room, extra_length, 0, 0, fork, err, err_cap);
}

static s32 levelForkEditedRevision(const char *arena_id, const char *parent_sha,
		forge_level_doc_t *document, const char *name, s32 room, s32 extra_length, s32 local_byor,
		mod_author_result_t *fork, char *err, size_t err_cap)
{
	if (!arena_id || !parent_sha || !document || !name || !name[0] || !fork) {
		levelFail(err, err_cap, "the shared draft needs an exact parent and a new name", "");
		return 0;
	}
	char directory[FS_MAXPATH + 1];
	forge_revision_t revision;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	return forgeRevisionRead(directory, arena_id, parent_sha, &revision, err, err_cap)
		&& levelForkRevisionWithChecks(&revision, name, document, room, extra_length, 0, local_byor,
			fork, err, err_cap);
}

s32 forgeLevelForkEditedRevision(const char *arena_id, const char *parent_sha,
		forge_level_doc_t *document, const char *name, mod_author_result_t *fork,
		char *err, size_t err_cap)
{
	return levelForkEditedRevision(arena_id, parent_sha, document, name, 0, 0, 0, fork, err, err_cap);
}

s32 forgeLevelForkLocalByorRevision(const char *arena_id, const char *parent_sha,
		forge_level_doc_t *document, const char *name, mod_author_result_t *fork,
		char *err, size_t err_cap)
{
	return levelForkEditedRevision(arena_id, parent_sha, document, name, 0, 0, 1, fork, err, err_cap);
}

s32 forgeLevelForkExtendedRevision(const char *arena_id, const char *parent_sha,
		forge_level_doc_t *document, const char *name, s32 room, s32 extra_length,
		mod_author_result_t *fork, char *err, size_t err_cap)
{
	if ((room != 1 && room != 2) || extra_length <= 0) {
		levelFail(err, err_cap, "choose a room and an extension length", "");
		return 0;
	}
	return levelForkEditedRevision(arena_id, parent_sha, document, name, room, extra_length, 0, fork, err, err_cap);
}

s32 forgeLevelSessionCanSaveExtendedVariant(void)
{
	return g_NetMode == NETMODE_NONE && s_Draft.phase == FORGE_DRAFT_NONE
		&& forgeLevelSessionCanPlayDraft() && !mainStageChangePending();
}

s32 forgeLevelSessionSaveExtendedVariant(const char *name, s32 room, s32 extra_length,
		mod_author_result_t *fork, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (fork) memset(fork, 0, sizeof(*fork));
	if (!forgeLevelSessionCanSaveExtendedVariant() || !fork || !name || !name[0]
			|| (room != 1 && room != 2) || extra_length <= 0) {
		levelFail(err, err_cap, "open an offline level and choose a new name, room and length", "");
		return 0;
	}
	const forge_level_source_t *source = &s_Session.source;
	if (!levelSourceStillMatches(source, err, err_cap)) return 0;
	char directory[FS_MAXPATH + 1];
	forge_revision_t revision;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!forgeRevisionRead(directory, source->arena_id, source->archive_sha256, &revision, err, err_cap)) {
		levelFail(err, err_cap, "pin the opened level's saved revision before extending a room", source->arena_id);
		return 0;
	}
	/* Resolve the draft's setup and executable graph before cloning changes
	 * the embedded asset IDs. The new archive receives its own geometry proof. */
	if (!forgeLevelValidateOriginalDraft(s_Session.doc, source, NULL, err, err_cap)) return 0;
	const s32 saved = levelForkRevisionWithChecks(&revision, name, s_Session.doc, room, extra_length, 1, 0, fork, err, err_cap);
	return saved;
}

s32 forgeLevelSessionForkPinnedRevision(const char *name, mod_author_result_t *fork,
		char *err, size_t err_cap)
{
	char directory[FS_MAXPATH + 1];
	forge_revision_t revision;
	if (!name || !name[0] || !fork || !s_Session.doc || !s_Session.saved_archive.arena_id[0]) {
		levelFail(err, err_cap, "open a level and give the fork a new name", "");
		return 0;
	}
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	return forgeRevisionReadCurrent(directory, s_Session.saved_archive.arena_id,
		&revision, err, err_cap) && levelForkRevision(&revision, name, NULL, 0, 0, fork, err, err_cap);
}

s32 forgeLevelSessionForkPreviousRevision(const char *name, mod_author_result_t *fork,
	char *err, size_t err_cap)
{
	if (fork) memset(fork, 0, sizeof(*fork));
	if (!name || !name[0] || !fork || g_NetMode != NETMODE_NONE || !forgeLevelSessionCanPlayDraft()
			|| s_Draft.phase != FORGE_DRAFT_NONE || mainStageChangePending()) {
		levelFail(err, err_cap, "open the level offline and give the recovered level a new name", "");
		return 0;
	}
	char directory[FS_MAXPATH + 1];
	forge_revision_t previous;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	return forgeRevisionReadPrevious(directory, s_Session.source.arena_id, &previous, err, err_cap)
		&& levelForkRevision(&previous, name, NULL, 0, 0, fork, err, err_cap);
}

static forge_revision_parent_e levelReadParentRevision(forge_revision_t *revision,
		char *err, size_t err_cap)
{
	char directory[FS_MAXPATH + 1];
	forge_revision_t variant;
	const forge_level_source_t *source = &s_Session.source;
	char scratch[FS_MAXPATH + 1];
	const char *input = levelArchiveInput(source->archive_path, source->archive_sha256, scratch, err, err_cap);
	if (!input) return FORGE_REVISION_PARENT_UNAVAILABLE;
	snprintf(variant.arena_id, sizeof(variant.arena_id), "%s", source->arena_id);
	snprintf(variant.path, sizeof(variant.path), "%s", input);
	memcpy(variant.digest, source->archive_sha256, sizeof(variant.digest));
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	const forge_revision_parent_e result = forgeRevisionReadParent(directory, &variant, revision, err, err_cap);
	levelReleaseArchiveInput(scratch);
	return result;
}

forge_revision_parent_e forgeLevelSessionParentRevision(forge_revision_t *revision,
		char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!s_Session.doc) return FORGE_REVISION_PARENT_NONE;
	if (!s_Session.parent_checked) {
		s_Session.parent_state = levelReadParentRevision(&s_Session.parent,
			s_Session.parent_error, sizeof(s_Session.parent_error));
		s_Session.parent_checked = 1;
	}
	if (s_Session.parent_state == FORGE_REVISION_PARENT_AVAILABLE && revision) {
		*revision = s_Session.parent;
	}
	if (s_Session.parent_state == FORGE_REVISION_PARENT_UNAVAILABLE && err && err_cap) {
		snprintf(err, err_cap, "%s", s_Session.parent_error);
	}
	return s_Session.parent_state;
}

s32 forgeLevelSessionForkParentRevision(const char *name, mod_author_result_t *fork,
		char *err, size_t err_cap)
{
	if (!name || !name[0] || !fork || !s_Session.doc) {
		levelFail(err, err_cap, "open a Variant and give the fork a new name", "");
		return 0;
	}
	forge_revision_t parent;
	const forge_revision_parent_e state = levelReadParentRevision(&parent, err, err_cap);
	s_Session.parent_checked = 0;
	if (state == FORGE_REVISION_PARENT_NONE) {
		levelFail(err, err_cap, "the level has no recorded parent revision", "");
	}
	return state == FORGE_REVISION_PARENT_AVAILABLE
		&& levelForkRevision(&parent, name, NULL, 0, 0, fork, err, err_cap);
}

s32 forgeLevelDebugForkPinned(const char *name, char *err, size_t err_cap)
{
	if (!forgeLevelSessionOpen(s_Session.arena_id, err, err_cap)) return 0;
	const s32 original_starts = forgeLevelDocSpawnCount(s_Session.doc);
	forge_revision_t original;
	mod_author_result_t *fork = calloc(1, sizeof(*fork));
	mod_author_result_t *parent_fork = calloc(1, sizeof(*parent_fork));
	if (!fork || !parent_fork) {
		free(fork);
		free(parent_fork);
		levelFail(err, err_cap, "out of memory preparing the fork", "");
		return 0;
	}
	s32 ok = forgeLevelSessionPinSavedRevision(&original, err, err_cap)
		&& forgeLevelSessionForkPinnedRevision(name, fork, err, err_cap)
		&& sha256VerifyFile(original.path, original.digest) == 1
		&& strcmp(s_Session.source.archive_sha256, original.digest) == 0
		&& levelSourceStillMatches(&s_Session.source, err, err_cap)
		&& forgeLevelSessionOpen(fork->catalog_id, err, err_cap)
		&& forgeLevelDocSpawnCount(s_Session.doc) == original_starts;
	if (ok) {
		forge_revision_t parent;
		char parent_name[MOD_AUTHOR_NAME_MAX];
		const int len = snprintf(parent_name, sizeof(parent_name), "%s Parent", name);
		ok = len > 0 && (size_t)len < sizeof(parent_name)
			&& forgeLevelSessionParentRevision(&parent, err, err_cap) == FORGE_REVISION_PARENT_AVAILABLE
			&& strcmp(parent.arena_id, original.arena_id) == 0
			&& strcmp(parent.digest, original.digest) == 0
			&& forgeLevelSessionForkParentRevision(parent_name, parent_fork, err, err_cap)
			&& forgeLevelSessionOpen(parent_fork->catalog_id, err, err_cap)
			&& forgeLevelDocSpawnCount(s_Session.doc) == original_starts
			&& forgeLevelSessionParentRevision(&parent, err, err_cap) == FORGE_REVISION_PARENT_AVAILABLE
			&& strcmp(parent.arena_id, original.arena_id) == 0
			&& strcmp(parent.digest, original.digest) == 0;
		if (ok) {
			sysLogPrintf(LOG_NOTE, "FORGE.REVISION.PARENT.FORK: variant=%s parent=%s sha256=%s fork=%s",
				fork->catalog_id, parent.arena_id, parent.digest, parent_fork->catalog_id);
		}
	}
	if (ok) {
		sysLogPrintf(LOG_NOTE, "FORGE.REVISION.PRESERVED: original=%s sha256=%s reopened_fork=%s",
			original.arena_id, original.digest, fork->catalog_id);
		ok = forgeLevelStartForge(fork->catalog_id);
	}
	if (!ok && err && err_cap && !err[0]) {
		levelFail(err, err_cap, "could not prove the original and the fork", "");
	}
	free(fork);
	free(parent_fork);
	return ok;
}

typedef struct grid_source_review {
	forge_revision_t revision;
	grid_match_source_archive_t pinned;
	char scenario_id[CATALOG_ID_LEN];
	char scenario_member[FS_MAXPATH + 1];
	char arena_deps[ASSET_CATALOG_DECLARED_DEP_BYTES];
	char scenario_deps[ASSET_CATALOG_DECLARED_DEP_BYTES];
} grid_source_review_t;

static s32 levelReadDescriptor(const void *bytes, u32 size, const char *archive_name,
		ini_section_t *ini, char *err, size_t err_cap)
{
	u32 len = 0;
	char *text = assetArchiveExtractDescriptorMemAlloc(bytes, size, archive_name,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &len, NULL);
	s32 ok = text && iniParseText(text, len, ini, err, err_cap);
	free(text);
	return ok;
}

static s32 gridResolveSourceType(const char *id, asset_type_e *type, void *userdata)
{
	const grid_source_review_t *review = userdata;
	if (review && strcmp(id, review->revision.arena_id) == 0) {
		*type = ASSET_ARENA;
		return 1;
	}
	if (review && strcmp(id, review->scenario_id) == 0) {
		*type = ASSET_SCENARIO;
		return 1;
	}
	const asset_entry_t *entry = assetCatalogResolve(id);
	if (!entry || !entry->occupied || !entry->enabled || strcmp(id, entry->id) != 0) return 0;
	*type = entry->type;
	return 1;
}

static s32 gridReadExactDescriptor(const void *bytes, u32 size, const char *archive_name,
		ini_section_t *ini, char *err, size_t err_cap)
{
	u32 length = 0;
	const char *leaf = assetArchiveDescriptorForPath(archive_name);
	char *text = gridMatchPackageReadMember(bytes, size, leaf, 1024u * 1024u, &length, NULL);
	const s32 valid = text && !memchr(text, '\0', length) && iniParseText(text, length, ini, err, err_cap);
	free(text);
	return valid;
}

static s32 gridCopyDeclaredDependencies(const ini_section_t *ini, const char *owner,
		char dst[ASSET_CATALOG_DECLARED_DEP_BYTES], char *err, size_t err_cap)
{
	const char *deps = NULL;
	for (s32 i = 0; i < ini->count; i++) {
		const ini_pair_t *pair = &ini->pairs[i];
		if (strcmp(pair->key, "deps") != 0) continue;
		if (deps || strlen(pair->value) >= ASSET_CATALOG_DECLARED_DEP_BYTES) {
			levelFail(err, err_cap, "duplicate or oversized native dependency declaration", owner);
			return 0;
		}
		deps = pair->value;
	}
	strcpy(dst, deps ? deps : "");
	return 1;
}

static s32 gridVisitModelTextures(const asset_entry_t *entry, const grid_match_source_archive_t *archive,
		const void *mesh, u32 size, grid_match_source_texture_visit_fn visit, void *visit_data,
		char *err, size_t err_cap)
{
	const char *source = fileProviderPath(catalogEffectiveHandle(entry));
	const char *extension = source ? strrchr(source, '.') : NULL;
	if (!extension || strcasecmp(extension, ".obj") != 0 || !strstr(source, "::")) return 1;

	/* The model compiler reads model.mtl beside the selected geometry inside
	 * the innermost archive. Bind that owner to these exact captured bytes. */
	char descriptor[FS_MAXPATH + 1], root[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	char absolute[FS_MAXPATH + 1];
	if (!assetPathSiblingMemberChecked(descriptor, sizeof(descriptor), source, "mesh.ini")
			|| !gridMatchPackageLocation(descriptor, root, sizeof(root), chain, sizeof(chain), NULL)) {
		levelFail(err, err_cap, "the OBJ texture source has no exact typed mesh owner", entry->id);
		return 0;
	}
	fsFullPath(root, absolute, sizeof(absolute));
	if (strcmp(absolute, archive->path) != 0 || strcmp(chain, archive->member_chain) != 0) {
		levelFail(err, err_cap, "the OBJ texture source points outside its exact typed mesh", entry->id);
		return 0;
	}

	u32 length = 0;
	s32 absent = 0;
	char *mtl = gridMatchPackageReadMember(mesh, size, "model.mtl", NET_DISTRIB_MAX_COMP, &length, &absent);
	if (!mtl) {
		if (!absent) levelFail(err, err_cap, "the exact OBJ material member is not readable", entry->id);
		return absent;
	}
	const s32 valid = modAssetMaterialVisitCatalogTextures(mtl, length, visit, visit_data, err, err_cap);
	free(mtl);
	return valid;
}

static s32 gridInspectSourceWithTextures(const grid_match_source_root_t *asset,
		grid_match_source_archive_t *archive, grid_match_source_texture_visit_fn visit, void *visit_data,
		void *userdata, char *err, size_t err_cap)
{
	const grid_source_review_t *review = userdata;
	if (review && (strcmp(asset->id, review->revision.arena_id) == 0
			|| strcmp(asset->id, review->scenario_id) == 0)) {
		*archive = review->pinned;
		if (asset->type == ASSET_SCENARIO) {
			strcpy(archive->member_chain, review->scenario_member);
		}
		return 1;
	}

	const asset_entry_t *entry = assetCatalogResolve(asset->id);
	char descriptor_path[FS_MAXPATH];
	if (!entry || entry->type != asset->type
			|| !gridMatchCatalogArchiveDescriptor(entry, descriptor_path, sizeof(descriptor_path))) {
		levelFail(err, err_cap, "the asset has no typed owning archive", asset->id);
		return 0;
	}
	char root[FS_MAXPATH + 1];
	const char *descriptor_leaf;
	if (!gridMatchPackageLocation(descriptor_path, root, sizeof(root),
			archive->member_chain, sizeof(archive->member_chain), &descriptor_leaf)) {
		levelFail(err, err_cap, "the asset has no typed owning archive", asset->id);
		return 0;
	}
	fsFullPath(root, archive->path, sizeof(archive->path));
	u32 size = 0;
	void *bytes = fsPathIsAbsolute(archive->path) ? gridMatchPackageLoad(archive->path, &size) : NULL;
	if (!bytes) {
		levelFail(err, err_cap, "the owning archive failed validation", asset->id);
		return 0;
	}
	char path[FS_MAXPATH + 1];
	sha256Hash(bytes, size, archive->digest);
	archive->size = size;
	archive->bundled = entry->bundled != 0;
	ini_section_t *ini = malloc(sizeof(*ini));
	const char *suffix = strrchr(archive->path, '.');
	archive->owner.type = assetArchiveTypeForPath(archive->path);
	s32 ok = ini && suffix && assetPathCopyChecked(archive->suffix, sizeof(archive->suffix), suffix)
		&& gridReadExactDescriptor(bytes, size, archive->path, ini, err, err_cap)
		&& assetPathCopyChecked(archive->owner.id, sizeof(archive->owner.id), iniGet(ini, "catalog_id", ""));

	const void *leaf_bytes = bytes;
	u32 leaf_size = size;
	const char *archive_name = archive->path;
	void *member_bytes = NULL;
	const char *member = archive->member_chain;
	s32 depth = 0;
	while (member[0] && ok && depth++ < MOD_ASSET_VALIDATE_MAX_DEPTH) {
		const char *cut = strstr(member, "::");
		size_t len = cut ? (size_t)(cut - member) : strlen(member);
		memcpy(path, member, len);
		path[len] = '\0';
		u32 next_size = 0;
		void *next = assetArchivePathIsTyped(path)
			? gridMatchPackageReadMember(leaf_bytes, leaf_size, path, NET_DISTRIB_MAX_COMP, &next_size, NULL) : NULL;
		free(member_bytes);
		member_bytes = next;
		leaf_bytes = next;
		leaf_size = next_size;
		archive_name = path;
		member = cut ? cut + 2 : "";
		ok = next && next_size;
	}
	const char *leaf_descriptor = assetArchiveDescriptorForPath(archive_name);
	asset_type_e leaf_type = assetArchiveTypeForPath(archive_name);
	s32 matching_type = leaf_type == asset->type
		|| (leaf_type == ASSET_AUDIO && (asset->type == ASSET_MUSIC || asset->type == ASSET_SFX));
	ok = ok && !member[0] && leaf_descriptor && strcmp(leaf_descriptor, descriptor_leaf) == 0
		&& matching_type
		&& gridReadExactDescriptor(leaf_bytes, leaf_size, archive_name, ini, err, err_cap)
		&& strcmp(iniGet(ini, "catalog_id", ""), asset->id) == 0;
	if (ok && visit && asset->type == ASSET_MODEL) {
		ok = gridVisitModelTextures(entry, archive, leaf_bytes, leaf_size, visit, visit_data, err, err_cap);
	}
	if (ok) {
		ok = assetArchiveValidateBytes(bytes, size, archive->path, ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) == 0;
	}
	if (ok && archive->member_chain[0] && !archive->bundled && archive->owner.type != ASSET_ARENA
			&& gridMatchPackageTypeShareable(archive->owner.type, archive->suffix)) {
		/* A native compound package must register its own root when a
		 * placed object only names one of its embedded sources. Leave
		 * installed-only archives on their existing logical closure. */
		grid_match_source_package_t owner = { .owner = archive->owner, .size = size };
		strcpy(owner.suffix, archive->suffix);
		memcpy(owner.digest, archive->digest, sizeof(owner.digest));
		archive->complete_owner = forgeLevelMatchPackageShareAllowed(&owner, bytes, size);
	}
	free(ini);
	free(member_bytes);
	free(bytes);
	if (!ok && (!err || !err_cap || !err[0])) {
		levelFail(err, err_cap, "the owning archive does not contain the exact asset", asset->id);
	}
	return ok;
}

static s32 gridInspectSource(const grid_match_source_root_t *asset,
		grid_match_source_archive_t *archive, void *userdata, char *err, size_t err_cap)
{
	return gridInspectSourceWithTextures(asset, archive, NULL, NULL, userdata, err, err_cap);
}

static s32 gridAddSourceRoot(grid_match_source_root_t *roots, size_t *count,
		const char *id, asset_type_e type, char *err, size_t err_cap)
{
	if (!id[0]) return 1;
	for (size_t i = 0; i < *count; i++) {
		if (roots[i].type == type && strcmp(roots[i].id, id) == 0) return 1;
	}
	if (*count == MANIFEST_MAX_ENTRIES || !assetPathCopyChecked(roots[*count].id, CATALOG_ID_LEN, id)) {
		levelFail(err, err_cap, "the level has too many or invalid asset roots", id);
		return 0;
	}
	roots[(*count)++].type = type;
	return 1;
}

typedef struct grid_asset_root_context {
	grid_match_source_root_t *roots;
	size_t *count;
	char *error;
	size_t error_cap;
} grid_asset_root_context_t;

static s32 gridResolveIntroWeapon(const char *id, s32 *number, void *userdata)
{
	grid_asset_root_context_t *context = userdata;
	catalog_weapon_result_t weapon;
	if (!catalogResolveWeapon(id, &weapon) || weapon.weapon_num < 0) return 0;
	*number = weapon.weapon_num;
	return gridAddSourceRoot(context->roots, context->count, id, ASSET_WEAPON,
		context->error, context->error_cap);
}

static s32 gridVisitTypedSource(const char *id, asset_type_e type, void *userdata)
{
	grid_asset_root_context_t *context = userdata;
	return gridAddSourceRoot(context->roots, context->count, id, type,
		context->error, context->error_cap);
}

static s32 gridVisitGraphModule(const char *kind, const mission_graph_params_t *params, void *userdata)
{
	grid_asset_root_context_t *context = userdata;
	const char *id;
	asset_type_e type;
	s32 reference = missionGraphModuleAssetRef(kind, params, &id, &type);
	if (reference < 0) {
		levelFail(context->error, context->error_cap, "a graph module is missing its typed asset operand", kind);
		return 0;
	}
	return reference == 0 || gridVisitTypedSource(id, type, userdata);
}

static s32 gridPlacementSources(forge_level_doc_t *doc, grid_match_source_root_t *roots,
		size_t *count, char *err, size_t err_cap)
{
	grid_asset_root_context_t context = {roots, count, err, err_cap};
	size_t setup_size = 0, objects_size = 0;
	const char *setup = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_SETUP_FIELDS, &setup_size);
	const char *objects = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_OBJECTS, &objects_size);
	if (!scenarioSourceVisitSetupAssets(setup, setup_size, objects, objects_size,
			gridVisitTypedSource, &context, err, err_cap)) return 0;

	scenario_stage_rules_t rules;
	size_t size = 0;
	const char *text = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_STAGE, &size);
	if (!scenarioStageRulesParse(text, size, &rules, err, err_cap)) return 0;
	for (s32 i = 0; i < rules.player.unlimited_ammo_exclude_count; i++) {
		if (!gridAddSourceRoot(roots, count, rules.player.unlimited_ammo_excludes[i],
				ASSET_WEAPON, err, err_cap)) return 0;
	}
	const char *music = rules.atmosphere.primary_music;
	if (music[0]) {
		const asset_entry_t *audio = assetCatalogResolve(music);
		if (!audio || (audio->type != ASSET_AUDIO && audio->type != ASSET_MUSIC)) {
			levelFail(err, err_cap, "the stage music is not in the catalog", music);
			return 0;
		}
		if (!gridAddSourceRoot(roots, count, music, audio->type, err, err_cap)) return 0;
	}
	scenario_intro_t intro = {0};
	text = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_INTRO, &size);
	s32 ok = scenarioIntroParseWithContext(text, size, gridResolveIntroWeapon, &context, &intro, err, err_cap);
	scenarioIntroFree(&intro);
	if (!ok) return 0;
	text = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH, &size);
	mission_graph_program_t *graph = missionGraphProgramCompileLevel(text, size, missionGraphModuleKnown, err, err_cap);
	ok = graph && missionGraphProgramVisitModuleParams(graph, gridVisitGraphModule, &context);
	missionGraphProgramFree(graph);
	return ok;
}

static s32 gridCollectArenaSourcesWithDraft(const void *scenario, u32 scenario_size,
		const char *scenario_member, const char *scenario_id, forge_level_doc_t *draft,
		grid_match_source_root_t *roots, size_t *count, char *declared_deps, char *err, size_t err_cap)
{
	static const char ai_member[] = "ai/ailists.json";
	ini_section_t *ini = malloc(sizeof(*ini));
	forge_level_doc_t *read_doc = NULL;
	char *ai = NULL;
	s32 ok = 0;
	if (!ini || !gridReadExactDescriptor(scenario, scenario_size, scenario_member, ini, err, err_cap)
			|| strcmp(iniGet(ini, "catalog_id", ""), scenario_id) != 0) {
		levelFail(err, err_cap, "the embedded scenario identity does not match", scenario_id);
		goto done;
	}
	if (declared_deps && !gridCopyDeclaredDependencies(ini, scenario_id, declared_deps, err, err_cap)) goto done;
	const char *graph_member = forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
	const char *selected_graph = iniGet(ini, "level_graph_file", "");
	if (iniGet(ini, "level_graph", "")[0]) {
		levelFail(err, err_cap, "use the native level_graph_file binding", scenario_id);
		goto done;
	}
	if (selected_graph[0] && strcmp(selected_graph, graph_member) != 0) {
		levelFail(err, err_cap, "the scenario selects a different level graph", selected_graph);
		goto done;
	}
	if (!draft) read_doc = forgeLevelReadScenarioDocument(scenario, scenario_size, err, err_cap);
	forge_level_doc_t *doc = draft ? draft : read_doc;
	if (!doc) goto done;
	size_t graph_size = 0;
	const char *graph = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH, &graph_size);
	if (!scenarioSourceSetupAssetMembersMatch(graph, graph_size,
			forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_SETUP_FIELDS),
			forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_OBJECTS), ai_member, err, err_cap)) goto done;

	u32 ai_size = 0;
	ai = gridMatchPackageReadMember(scenario, scenario_size, ai_member, LEVEL_ARCHIVE_MAX_BYTES, &ai_size, NULL);
	grid_asset_root_context_t context = {roots, count, err, err_cap};
	if (!ai || !scenarioSourceVisitAiAssets(ai, ai_size, gridVisitTypedSource, &context, err, err_cap)) {
		if (err && err_cap && !err[0]) levelFail(err, err_cap, "the scenario's AI asset source could not be read", ai_member);
		goto done;
	}
	ok = gridPlacementSources(doc, roots, count, err, err_cap);

done:
	free(ai);
	forgeLevelDocFree(read_doc);
	free(ini);
	return ok;
}

static s32 gridCollectArenaSources(const void *scenario, u32 scenario_size,
		const char *scenario_member, const char *scenario_id, grid_match_source_root_t *roots,
		size_t *count, char *declared_deps, char *err, size_t err_cap)
{
	return gridCollectArenaSourcesWithDraft(scenario, scenario_size, scenario_member, scenario_id,
		NULL, roots, count, declared_deps, err, err_cap);
}

static s32 gridValidateDraftSources(const void *scenario, u32 scenario_size,
		const char *scenario_member, const char *scenario_id, char *err, size_t err_cap)
{
	grid_match_source_root_t *roots = calloc(MANIFEST_MAX_ENTRIES, sizeof(*roots));
	if (!roots) {
		levelFail(err, err_cap, "could not allocate draft asset roots", "");
		return 0;
	}
	size_t count = 0;
	s32 ok = gridCollectArenaSources(scenario, scenario_size, scenario_member, scenario_id,
		roots, &count, NULL, err, err_cap);
	for (size_t i = 0; i < count && ok; i++) {
		catalog_dep_activation_plan_t closure = {0};
		ok = catalogDepActivationPlanBuild(&closure, roots[i].id, roots[i].type,
			gridResolveSourceType, NULL, err, err_cap);
		catalogDepActivationPlanFree(&closure);
	}
	free(roots);
	return ok;
}

s32 forgeLevelBuildPinnedMatchSources(const char *arena_id,
		const grid_match_source_root_t *match_roots, size_t match_root_count,
		grid_match_sources_t *plan, char *err, size_t err_cap)
{
	grid_source_review_t review = {0};
	char directory[FS_MAXPATH + 1];
	if (!arena_id || !arena_id[0] || !plan || match_root_count > MANIFEST_MAX_ENTRIES
			|| (match_root_count && !match_roots)) {
		levelFail(err, err_cap, "an arena and valid match roots are required", "");
		return 0;
	}
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!forgeRevisionReadCurrent(directory, arena_id, &review.revision, err, err_cap)) return 0;
	const asset_entry_t *arena = assetCatalogResolve(review.revision.arena_id);
	grid_match_source_root_t arena_root = { .type = ASSET_ARENA };
	grid_match_source_archive_t installed = {0};
	char installed_digest[SHA256_HEX_SIZE];
	if (!arena || arena->type != ASSET_ARENA
			|| !assetPathCopyChecked(arena_root.id, sizeof(arena_root.id), arena_id)
			|| !gridInspectSource(&arena_root, &installed, NULL, err, err_cap)) {
		if (err && err_cap && !err[0]) levelFail(err, err_cap, "the saved arena has no verified native source", arena_id);
		return 0;
	}
	sha256ToHex(installed.digest, installed_digest);
	if (strcmp(installed_digest, review.revision.digest) != 0) {
		levelFail(err, err_cap, "pin the currently saved arena before reviewing its registered sources", "");
		return 0;
	}
	void *bytes = gridMatchPackageLoad(review.revision.path, &review.pinned.size);
	void *scenario = NULL;
	grid_match_source_root_t *roots = calloc(MANIFEST_MAX_ENTRIES, sizeof(*roots));
	ini_section_t *ini = malloc(sizeof(*ini));
	s32 ok = 0;
	if (!bytes || !ini || !roots) {
		levelFail(err, err_cap, "the pinned arena could not be read", "");
		goto done;
	}
	u32 scenario_size = 0;
	size_t count = 0;
	for (size_t i = 0; i < match_root_count; i++) {
		const grid_match_source_root_t *root = &match_roots[i];
		if (!memchr(root->id, '\0', sizeof(root->id)) || !root->id[0]
				|| !gridAddSourceRoot(roots, &count, root->id, root->type, err, err_cap)) {
			levelFail(err, err_cap, "the match contains an invalid asset root", "");
			goto done;
		}
	}
	char digest[SHA256_HEX_SIZE];
	sha256Hash(bytes, review.pinned.size, review.pinned.digest);
	sha256ToHex(review.pinned.digest, digest);
	if (strcmp(digest, review.revision.digest) != 0
			|| !gridReadExactDescriptor(bytes, review.pinned.size, review.revision.path, ini, err, err_cap)
			|| !assetPathCopyChecked(review.scenario_member, sizeof(review.scenario_member), iniGet(ini, "scenario_archive", ""))
			|| !assetPathCopyChecked(review.scenario_id, sizeof(review.scenario_id), iniGet(ini, "scenario", ""))
			|| !review.scenario_id[0] || strcmp(arena->ext.arena.scenario_id, review.scenario_id) != 0
			|| assetArchiveTypeForPath(review.scenario_member) != ASSET_SCENARIO) {
		levelFail(err, err_cap, "the pinned arena has no matching embedded scenario", "");
		goto done;
	}
	scenario = gridMatchPackageReadMember(bytes, review.pinned.size, review.scenario_member,
		NET_DISTRIB_MAX_COMP, &scenario_size, NULL);
	if (!scenario) {
		levelFail(err, err_cap, "the pinned arena has no readable embedded scenario", review.scenario_member);
		goto done;
	}
	snprintf(review.pinned.path, sizeof(review.pinned.path), "%s", review.revision.path);
	snprintf(review.pinned.suffix, sizeof(review.pinned.suffix), "%s", ".pdarena");
	snprintf(review.pinned.owner.id, sizeof(review.pinned.owner.id), "%s", review.revision.arena_id);
	review.pinned.owner.type = ASSET_ARENA;
	review.pinned.bundled = arena->bundled != 0;
	ok = gridAddSourceRoot(roots, &count, review.revision.arena_id, ASSET_ARENA, err, err_cap)
		&& gridAddSourceRoot(roots, &count, review.scenario_id, ASSET_SCENARIO, err, err_cap)
		&& gridCollectArenaSources(scenario, scenario_size, review.scenario_member, review.scenario_id,
			roots, &count, NULL, err, err_cap)
		&& gridMatchSourcesBuildWithModelTextures(plan, roots, count, gridResolveSourceType, gridInspectSource,
			gridInspectSourceWithTextures, &review, err, err_cap);
	if (ok) {
		sysLogPrintf(LOG_NOTE, "GRID.MATCH.SOURCES: arena=%s revision=%s entries=%u packages=%u digest=%s",
			review.revision.arena_id, review.revision.digest, (u32)plan->entry_count,
			(u32)plan->package_count, plan->digest);
	}
done:
	free(roots);
	free(ini);
	free(scenario);
	free(bytes);
	return ok;
}

s32 forgeLevelBuildPrivateGameplaySources(const grid_match_source_root_t *roots, size_t count,
		grid_match_sources_t *plan, char *err, size_t err_cap)
{
	return gridMatchSourcesBuildWithModelTextures(plan, roots, count, gridResolveSourceType,
		gridInspectSource, gridInspectSourceWithTextures, NULL, err, err_cap);
}

static s32 gridStagePackage(grid_match_cache_t *cache, const grid_match_offer_t *offer,
		size_t index, const grid_match_source_package_t *archive, char *err, size_t err_cap)
{
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(archive->path, &size);
	const s32 accepted = bytes && gridMatchCacheAccept(cache, offer, index, bytes, size, err, err_cap);
	free(bytes);
	if (!accepted && err && err_cap && !err[0]) {
		levelFail(err, err_cap, "could not read the staged archive", archive->path);
	}
	return accepted;
}

static s32 gridStageMatchSources(const grid_match_sources_t *plan,
		const grid_match_offer_t *offer, grid_match_cache_t **cache, size_t captured_index,
		const void *captured_bytes, u32 captured_size, char *err, size_t err_cap)
{
	if (!cache || *cache) {
		levelFail(err, err_cap, "another stage source set is already prepared", "");
		return 0;
	}
	grid_match_cache_t *candidate = NULL;
	if (!gridMatchCacheBegin(&candidate, offer, plan, err, err_cap)) return 0;
	for (size_t i = 0; i < plan->package_count; i++) {
		const s32 accepted = i == captured_index
			? gridMatchCacheAccept(candidate, offer, i, captured_bytes, captured_size, err, err_cap)
			: gridStagePackage(candidate, offer, i, &plan->packages[i], err, err_cap);
		if (!accepted) {
			gridMatchCacheReset(&candidate);
			return 0;
		}
	}
	if (!gridMatchCacheReady(candidate, offer)) {
		levelFail(err, err_cap, "the exact set of match archives is incomplete", "");
		gridMatchCacheReset(&candidate);
		return 0;
	}
	*cache = candidate;
	return 1;
}

s32 forgeLevelStageLocalMatchSources(const grid_match_sources_t *plan,
		const grid_match_offer_t *offer, grid_match_cache_t **cache, char *err, size_t err_cap)
{
	return gridStageMatchSources(plan, offer, cache, SIZE_MAX, NULL, 0, err, err_cap);
}

typedef struct grid_declared_edge_context {
	grid_source_review_t *review;
	catalog_dep_activation_add_fn add;
	void *children;
	char unresolved[CATALOG_ID_LEN];
} grid_declared_edge_context_t;

static s32 gridAddDeclaredEdge(const char *id, void *userdata)
{
	grid_declared_edge_context_t *context = userdata;
	asset_type_e type = ASSET_NONE;
	if (gridResolveSourceType(id, &type, context->review)
			&& context->add(id, type, context->children)) return 1;
	snprintf(context->unresolved, sizeof(context->unresolved), "%s", id);
	return 0;
}

static s32 gridVisitStagedDependencies(const char *id, asset_type_e type,
		catalog_dep_activation_add_fn add, void *children, void *userdata, char *err, size_t err_cap)
{
	grid_source_review_t *review = userdata;
	const char *deps = NULL;
	if (type == ASSET_ARENA && strcmp(id, review->revision.arena_id) == 0) deps = review->arena_deps;
	if (type == ASSET_SCENARIO && strcmp(id, review->scenario_id) == 0) deps = review->scenario_deps;
	grid_declared_edge_context_t context = { .review = review, .add = add, .children = children };
	if (assetCatalogVisitDeclaredDependencies(deps, gridAddDeclaredEdge, &context)) return 1;
	levelFail(err, err_cap, "missing or invalid declared arena dependency", context.unresolved);
	return 0;
}

s32 forgeLevelSessionCanReviewVariantSources(void)
{
	return s_Session.opened_original && g_NetMode == NETMODE_NONE && forgeLevelSessionCanPlayDraft()
		&& s_Draft.phase == FORGE_DRAFT_NONE && !mainStageChangePending();
}

static s32 levelCollectVariantSources(grid_source_review_t *review, const void *scenario, u32 scenario_size,
		forge_level_doc_t *doc, grid_match_sources_t *out, char *err, size_t err_cap)
{
	grid_match_source_root_t *roots = calloc(MANIFEST_MAX_ENTRIES, sizeof(*roots));
	if (!roots) {
		levelFail(err, err_cap, "out of memory reviewing the variant sources", "");
		return 0;
	}
	size_t count = 0;
	const s32 ok = gridAddSourceRoot(roots, &count, review->revision.arena_id, ASSET_ARENA, err, err_cap)
		&& gridAddSourceRoot(roots, &count, review->scenario_id, ASSET_SCENARIO, err, err_cap)
		&& gridCollectArenaSourcesWithDraft(scenario, scenario_size, review->scenario_member, review->scenario_id,
			doc, roots, &count, review->scenario_deps, err, err_cap)
		&& gridMatchSourcesBuildWithModelTexturesAndEdges(out, roots, count, gridResolveSourceType,
			gridInspectSource, gridInspectSourceWithTextures, review, gridVisitStagedDependencies, review, err, err_cap);
	free(roots);
	return ok;
}

static s32 levelBuildVariantSources(forge_level_doc_t *doc, const forge_level_source_t *source,
		grid_match_sources_t *out, char *err, size_t err_cap)
{
	const u32 generation = assetCatalogGetGeneration();
	const asset_entry_t *arena = assetCatalogResolve(source->arena_id);
	grid_source_review_t review = {0};
	if (!arena || arena->type != ASSET_ARENA || !arena->enabled || arena->bundled || arena->temporary
			|| !arena->grid_offer_only || !arena->ext.arena.scenario_id[0]
			|| !assetPathCopyChecked(review.revision.arena_id, sizeof(review.revision.arena_id), source->arena_id)
			|| !assetPathCopyChecked(review.pinned.path, sizeof(review.pinned.path), source->archive_path)
			|| !assetPathCopyChecked(review.scenario_member, sizeof(review.scenario_member), source->scenario_member)
			|| !assetPathCopyChecked(review.scenario_id, sizeof(review.scenario_id), arena->ext.arena.scenario_id)) {
		levelFail(err, err_cap, "the draft has no exact installed original arena and scenario", source->arena_id);
		return 0;
	}
	review.pinned.owner.type = ASSET_ARENA;
	strcpy(review.pinned.owner.id, review.revision.arena_id);
	strcpy(review.pinned.suffix, ".pdarena");
	void *bytes = gridMatchPackageLoad(review.pinned.path, &review.pinned.size);
	void *scenario = NULL;
	ini_section_t *ini = malloc(sizeof(*ini));
	grid_match_sources_t plan = {0};
	s32 ok = 0;
	if (!bytes || !ini) {
		levelFail(err, err_cap, "the opened arena could not be read for source review", source->arena_id);
		goto done;
	}
	sha256Hash(bytes, review.pinned.size, review.pinned.digest);
	if (!levelCheckArchiveDigest(review.pinned.digest, source->archive_sha256, err, err_cap)
			|| gridArchiveOrigin(review.pinned.path, bytes, review.pinned.size) != GRID_ARCHIVE_ORIGINAL
			|| !gridReadExactDescriptor(bytes, review.pinned.size, review.pinned.path, ini, err, err_cap)
			|| !levelDescriptorValueMatches(ini, "catalog_id", source->arena_id)
			|| !levelDescriptorValueMatches(ini, "scenario", review.scenario_id)
			|| !levelDescriptorValueMatches(ini, "scenario_archive", review.scenario_member)
			|| !gridCopyDeclaredDependencies(ini, source->arena_id, review.arena_deps, err, err_cap)) {
		if (err && err_cap && !err[0]) levelFail(err, err_cap, "the opened arena source binding changed", source->arena_id);
		goto done;
	}
	u32 scenario_size = 0;
	scenario = gridMatchPackageReadMember(bytes, review.pinned.size, review.scenario_member,
		NET_DISTRIB_MAX_COMP, &scenario_size, NULL);
	if (!scenario) {
		levelFail(err, err_cap, "the opened arena's exact scenario could not be read", review.scenario_member);
		goto done;
	}
	if (!levelCollectVariantSources(&review, scenario, scenario_size, doc, &plan, err, err_cap)) goto done;
	if (generation != assetCatalogGetGeneration()) {
		levelFail(err, err_cap, "the catalog changed during the source review", "review the draft again");
		goto done;
	}
	gridMatchSourcesFree(out);
	*out = plan;
	memset(&plan, 0, sizeof(plan));
	ok = 1;

done:
	gridMatchSourcesFree(&plan);
	free(ini);
	free(scenario);
	free(bytes);
	return ok;
}

s32 forgeLevelSessionReviewVariantSources(grid_match_sources_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out || !forgeLevelSessionCanReviewVariantSources()) {
		levelFail(err, err_cap, "open an original Grid Level File offline to review its variant sources", "");
		return 0;
	}
	return levelBuildVariantSources(s_Session.doc, &s_Session.source, out, err, err_cap);
}

static int levelCompareVariantAssetId(const void *id, const void *item)
{
	const grid_match_source_entry_t *entry = item;
	return strcmp(id, entry->asset.id);
}

static s32 levelVariantCheckOmissions(const grid_match_sources_t *before, const u16 *indices, size_t count,
		const forge_level_placement_fallback_t *fallbacks, size_t fallback_count,
		const char *arena_id, u8 *omitted, char *err, size_t err_cap)
{
	for (size_t i = 0; i < count; i++) {
		const size_t index = indices[i];
		if (index >= before->package_count || (i && indices[i - 1] >= index)) {
			levelFail(err, err_cap, "the omitted package selection is invalid", "");
			return 0;
		}
		const grid_match_source_package_t *package = &before->packages[index];
		if (package->bundled || strncmp(package->owner.id, "base:", 5) == 0
				|| strcmp(package->owner.id, arena_id) == 0) {
			levelFail(err, err_cap, "the variant must keep this package", package->owner.id);
			return 0;
		}
		omitted[index] = 1;
	}
	for (size_t i = 0; i < fallback_count; i++) {
		const forge_level_placement_fallback_t *edit = &fallbacks[i];
		const asset_type_e type = edit->kind == FORGE_LEVEL_FALLBACK_PICKUP ? ASSET_WEAPON : ASSET_MODEL;
		const grid_match_source_entry_t *entry = edit->expected_catalog_id
			? bsearch(edit->expected_catalog_id, before->entries, before->entry_count,
				sizeof(before->entries[0]), levelCompareVariantAssetId) : NULL;
		if (!entry || entry->asset.type != type || !omitted[entry->package_index]) {
			levelFail(err, err_cap, "the placement fallback does not belong to an omitted source",
				edit->record_id ? edit->record_id : "");
			return 0;
		}
	}
	return 1;
}

static s32 levelVariantOwnsTemporaryPackage(const char *arena_id, const u8 *arena_sha256,
	const grid_match_source_package_t *package)
{
	return (arena_sha256 && forgeLevelVariantDependencyOwnsPackage(arena_id, arena_sha256, package))
		|| (g_NetMode == NETMODE_SERVER && netmsgGridRoomOwnsProposedPackage(package));
}

static s32 levelVariantCheckRemainingSources(const grid_match_sources_t *before,
		const grid_match_sources_t *after, const u8 *omitted, const char *detached_arena,
		const u8 *bound_arena_sha, const grid_match_session_sources_t *borrowed, char *err, size_t err_cap)
{
	for (size_t i = 0; i < after->package_count; i++) {
		const grid_match_source_package_t *package = &after->packages[i];
		size_t prior = 0;
		while (prior < before->package_count && strcmp(before->packages[prior].owner.id, package->owner.id) != 0) prior++;
		if (prior < before->package_count) {
			if (omitted[prior]) {
				levelFail(err, err_cap, "the variant still requires this omitted package", package->owner.id);
				return 0;
			}
			const grid_match_source_package_t *previous = &before->packages[prior];
			if (previous->owner.type != package->owner.type || previous->bundled != package->bundled
					|| previous->size != package->size || strcmp(previous->suffix, package->suffix) != 0
					|| memcmp(previous->digest, package->digest, sizeof(package->digest)) != 0) {
				levelFail(err, err_cap, "a source package changed during the save", package->owner.id);
				return 0;
			}
		} else if (!package->bundled || strncmp(package->owner.id, "base:", 5) != 0) {
			levelFail(err, err_cap, "a fallback introduced a package that was not reviewed", package->owner.id);
			return 0;
		}
	}
	for (size_t i = 0; i < after->entry_count; i++) {
		const grid_match_source_entry_t *source = &after->entries[i];
		const grid_match_source_package_t *package = &after->packages[source->package_index];
		if (detached_arena && package->owner.type == ASSET_ARENA && strcmp(package->owner.id, detached_arena) == 0) continue;
		const char *id = source->asset.id;
		const asset_entry_t *asset = assetCatalogResolve(id);
		s32 available = asset && (!asset->temporary || (bound_arena_sha
			&& levelVariantOwnsTemporaryPackage(detached_arena, bound_arena_sha, package)));
		for (size_t j = 0; asset && asset->temporary && borrowed && j < borrowed->count && !available; j++) {
			const grid_match_session_file_t *file = &borrowed->files[j];
			available = file->owner.type == package->owner.type && strcmp(file->owner.id, package->owner.id) == 0
				&& strcmp(file->path, package->path) == 0;
		}
		if (!available) {
			levelFail(err, err_cap, "the variant requires a source that is not installed", id);
			return 0;
		}
	}
	return 1;
}

static s32 levelProjectVariantDependencies(const grid_match_sources_t *reviewed,
	grid_match_sources_t *out, char *err, size_t err_cap)
{
	grid_match_sources_t plan = {0};
	plan.packages = calloc(reviewed->package_count, sizeof(*plan.packages));
	plan.entries = calloc(reviewed->entry_count, sizeof(*plan.entries));
	if (!plan.packages || !plan.entries) {
		gridMatchSourcesFree(&plan);
		levelFail(err, err_cap, "could not allocate the variant dependency plan", "");
		return 0;
	}
	u16 index[MANIFEST_MAX_ENTRIES];
	memset(index, 0xff, sizeof(index));
	for (size_t i = 0; i < reviewed->entry_count; i++) {
		const grid_match_source_entry_t *source = &reviewed->entries[i];
		const size_t package_index = source->package_index;
		const grid_match_source_package_t *package = &reviewed->packages[package_index];
		if (package->owner.type == ASSET_ARENA) continue;
		if (index[package_index] == 0xffffu) {
			index[package_index] = (u16)plan.package_count;
			plan.packages[plan.package_count++] = *package;
		}
		plan.entries[plan.entry_count] = *source;
		plan.entries[plan.entry_count].package_index = index[package_index];
		plan.entries[plan.entry_count++].fiesta_pool = 0;
	}
	if (!gridMatchSourcesSeal(&plan, err, err_cap)) {
		gridMatchSourcesFree(&plan);
		return 0;
	}
	*out = plan;
	return 1;
}

static s32 levelStoreVariantDependencies(const grid_match_sources_t *reviewed, char digest[SHA256_HEX_SIZE],
	char *err, size_t err_cap)
{
	digest[0] = '\0';
	s32 has_mods = 0;
	for (size_t i = 0; i < reviewed->package_count; i++)
		has_mods |= !reviewed->packages[i].bundled && reviewed->packages[i].owner.type != ASSET_ARENA;
	if (!has_mods) return 1;

	grid_match_sources_t plan = {0};
	if (!levelProjectVariantDependencies(reviewed, &plan, err, err_cap)) return 0;
	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	s32 ok = 1;
	if (!assetPathIsAbsolute(directory) || !fsCreateDir(directory)) {
		levelFail(err, err_cap, "the profile revision folder is unavailable", "");
		ok = 0;
	}
	for (size_t i = 0; ok && i < plan.package_count; i++) {
		const grid_match_source_package_t *package = &plan.packages[i];
		if (package->bundled || !gridMatchPackageTypeShareable(package->owner.type, package->suffix)) continue;
		u32 size = 0;
		void *bytes = gridMatchPackageLoad(package->path, &size);
		u8 hash[SHA256_DIGEST_SIZE] = {0};
		if (bytes && size == package->size) sha256Hash(bytes, size, hash);
		if (!bytes || size != package->size || memcmp(hash, package->digest, sizeof(hash)) != 0) {
			levelFail(err, err_cap, "a selected source changed during the save", package->owner.id);
			ok = 0;
		} else if (forgeLevelMatchPackageShareAllowed(package, bytes, size)) {
			ok = forgeRevisionStoreDependency(directory, package, bytes, size, err, err_cap);
		}
		free(bytes);
	}
	if (ok) ok = forgeRevisionStoreDependencyPlan(directory, &plan, err, err_cap);
	if (ok) memcpy(digest, plan.digest, SHA256_HEX_SIZE);
	gridMatchSourcesFree(&plan);
	return ok;
}

s32 forgeLevelSessionSaveSelectedVariant(const char *display_name,
		const grid_match_sources_t *reviewed, const u16 *omitted_packages, size_t omitted_count,
		const forge_level_placement_fallback_t *fallbacks, size_t fallback_count,
		mod_author_result_t *result, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!display_name || !display_name[0] || !reviewed || (omitted_count && !omitted_packages)
			|| omitted_count > MANIFEST_MAX_ENTRIES || (fallback_count && !fallbacks)
			|| !forgeLevelSessionCanReviewVariantSources()
			|| fallback_count > (size_t)forgeLevelDocObjectCount(s_Session.doc)) {
		levelFail(err, err_cap, "review the original Grid Level File and choose a new variant name", "");
		return 0;
	}
	const u32 generation = assetCatalogGetGeneration();
	grid_match_sources_t before = {0}, after = {0};
	forge_level_doc_t *draft = NULL;
	mod_author_result_t *saved = calloc(1, sizeof(*saved));
	u8 omitted[MANIFEST_MAX_ENTRIES] = {0};
	s32 ok = 0;
	if (!saved) {
		levelFail(err, err_cap, "out of memory preparing the variant", "");
		goto done;
	}
	const forge_level_source_t *source = &s_Session.source;
	const mod_author_request_t name_request = {
		.template_path = source->archive_path, .mods_root = modmgrGetModsDir(),
		.display_name = display_name, .require_new_folder = 1,
	};
	if (modAuthorPlan(&name_request, saved, err, err_cap) != MOD_AUTHOR_OK) goto done;
	if (strcmp(saved->catalog_id, source->arena_id) == 0 || assetCatalogResolveAny(saved->catalog_id)
			|| modmgrFindMod(saved->mod_id)) {
		levelFail(err, err_cap, "choose a new name for the variant", saved->catalog_id);
		goto done;
	}
	if (!levelBuildVariantSources(s_Session.doc, source, &before, err, err_cap)) goto done;
	if (memcmp(before.digest, reviewed->digest, sizeof(before.digest)) != 0) {
		levelFail(err, err_cap, "the draft's source packages changed", "review them again before saving");
		goto done;
	}
	if (!levelVariantCheckOmissions(&before, omitted_packages, omitted_count, fallbacks, fallback_count,
			source->arena_id, omitted, err, err_cap)) goto done;
	u8 arena_sha[SHA256_DIGEST_SIZE] = {0};
	for (size_t i = 0; i < before.package_count; i++) {
		const grid_match_source_package_t *package = &before.packages[i];
		if (package->owner.type == ASSET_ARENA && strcmp(package->owner.id, source->arena_id) == 0) {
			memcpy(arena_sha, package->digest, sizeof(arena_sha));
			break;
		}
	}
	draft = forgeLevelDocCloneWithPlacementFallbacks(s_Session.doc, fallbacks, fallback_count, err, err_cap);
	if (!draft || !levelBuildVariantSources(draft, source, &after, err, err_cap)
			|| !levelVariantCheckRemainingSources(&before, &after, omitted, source->arena_id, arena_sha, NULL, err, err_cap)) goto done;
	if (generation != assetCatalogGetGeneration()) {
		levelFail(err, err_cap, "the catalog changed during the save", "review the draft again");
		goto done;
	}
	char dependency_plan[SHA256_HEX_SIZE];
	if (!levelStoreVariantDependencies(&after, dependency_plan, err, err_cap)
			|| levelSaveAs(draft, source, display_name, 0, NULL, dependency_plan, &after, saved, err, err_cap)
				!= MOD_AUTHOR_OK) goto done;
	if (result) *result = *saved;
	ok = 1;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.VARIANT.SOURCES: template=%s catalog=%s kept=%u omitted=%u fallbacks=%u",
		source->arena_id, saved->catalog_id, (unsigned)after.package_count, (unsigned)omitted_count,
		(unsigned)fallback_count);

done:
	free(saved);
	forgeLevelDocFree(draft);
	gridMatchSourcesFree(&after);
	gridMatchSourcesFree(&before);
	return ok;
}

typedef struct grid_registered_edges {
	const char *declared;
	s32 exact;
} grid_registered_edges_t;

static s32 gridOtherDeclaredEdge(const char *id, void *userdata)
{
	return strcmp(id, (const char *)userdata) != 0;
}

static void gridCheckRegisteredEdge(const char *id, asset_type_e type, void *userdata)
{
	(void)type;
	grid_registered_edges_t *edges = userdata;
	/* The visitor stops on the first exact declaration of this edge. */
	if (assetCatalogVisitDeclaredDependencies(edges->declared, gridOtherDeclaredEdge, (void *)id)) {
		edges->exact = 0;
	}
}

static s32 gridRequireRegisteredEdge(const char *id, void *userdata)
{
	return catalogDepContains((const char *)userdata, id);
}

static s32 gridVerifyStagedOwner(const grid_source_review_t *review, const char *id, asset_type_e type,
		const char *member_chain, const char *declared, s32 installed, char *err, size_t err_cap)
{
	const asset_entry_t *row = assetCatalogResolveAny(id);
	grid_registered_edges_t edges = { .declared = installed ? declared : NULL, .exact = 1 };
	catalogDepForEachTyped(id, gridCheckRegisteredEdge, &edges);
	if (installed && row && row->enabled && !row->temporary && !row->bundled && row->type == type) {
		grid_match_source_root_t asset = { .type = type };
		grid_match_source_archive_t source = {0};
		if (edges.exact && assetCatalogVisitDeclaredDependencies(declared, gridRequireRegisteredEdge, (void *)id)
				&& assetPathCopyChecked(asset.id, sizeof(asset.id), id)
				&& gridInspectSource(&asset, &source, NULL, err, err_cap)
				&& source.size == review->pinned.size
				&& memcmp(source.digest, review->pinned.digest, sizeof(source.digest)) == 0
				&& strcmp(source.owner.id, review->revision.arena_id) == 0
				&& strcmp(source.member_chain, member_chain) == 0) return 1;
	} else if (!installed && !row && edges.exact
			&& (type != ASSET_SCENARIO || assetCatalogFindStagenumPrivateSlot(id) < 0)) {
		return 1;
	}
	if (!err || !err_cap || !err[0]) levelFail(err, err_cap, "the staged source conflicts with local catalog ownership", id);
	return 0;
}

typedef struct grid_native_members {
	const char *scenario;
	s32 unexpected;
} grid_native_members_t;

static s32 gridCheckStagedMember(const char *name, u32 size, void *userdata)
{
	(void)size;
	grid_native_members_t *members = userdata;
	if (assetArchivePathIsTyped(name) && (!members->scenario || strcmp(name, members->scenario) != 0)) {
		members->unexpected = 1;
	}
	return members->unexpected;
}

static s32 gridPrepareStagedArenaSources(const char *locator, const void *bytes, u32 size,
		const char *arena_id, const char *expected_sha, const char *parent_id, const char *parent_sha,
		u8 room_id, u64 review_serial,
		grid_match_offer_t *out_offer, grid_match_cache_t **out_cache, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!locator || !arena_id || !expected_sha || room_id == 0xff || !review_serial
			|| !out_offer || !out_cache || *out_cache) {
		levelFail(err, err_cap, "an exact original arena and an empty review cache are required", "");
		return 0;
	}
	const u32 generation = assetCatalogGetGeneration();
	grid_source_review_t review = {0};
	if (!assetPathCopyChecked(review.revision.arena_id, sizeof(review.revision.arena_id), arena_id)
			|| !assetPathCopyChecked(review.revision.digest, sizeof(review.revision.digest), expected_sha)) {
		levelFail(err, err_cap, "the staged arena identity is too long", "");
		return 0;
	}
	if (!assetPathCopyChecked(review.pinned.path, sizeof(review.pinned.path), locator)) {
		levelFail(err, err_cap, "the staged arena source locator is too long", "");
		return 0;
	}
	review.pinned.owner.type = ASSET_ARENA;
	review.pinned.size = size;
	strcpy(review.pinned.owner.id, review.revision.arena_id);
	strcpy(review.pinned.suffix, ".pdarena");
	void *scenario = NULL;
	grid_match_source_root_t *roots = NULL;
	ini_section_t *ini = NULL;
	grid_match_sources_t plan = {0};
	grid_match_cache_t *cache = NULL;
	s32 ok = 0;
	if (!bytes || !size || size > NET_DISTRIB_MAX_COMP) {
		levelFail(err, err_cap, "the staged arena archive could not be read within its source budget", arena_id);
		goto done;
	}
	sha256Hash(bytes, review.pinned.size, review.pinned.digest);
	ini = malloc(sizeof(*ini));
	roots = calloc(MANIFEST_MAX_ENTRIES, sizeof(*roots));
	if (!ini || !roots) {
		levelFail(err, err_cap, "out of memory preparing staged arena dependencies", "");
		goto done;
	}
	if (!gridReadExactDescriptor(bytes, review.pinned.size, review.pinned.path, ini, err, err_cap)
			|| !assetPathCopyChecked(review.scenario_member, sizeof(review.scenario_member), iniGet(ini, "scenario_archive", ""))
			|| !assetPathCopyChecked(review.scenario_id, sizeof(review.scenario_id), iniGet(ini, "scenario", ""))
			|| !gridCopyDeclaredDependencies(ini, arena_id, review.arena_deps, err, err_cap)) goto done;
	if (parent_id) {
		u8 parent_hash[SHA256_DIGEST_SIZE];
		char parent_map[SHA256_HEX_SIZE];
		sha256Hash(parent_id, strlen(parent_id), parent_hash);
		sha256ToHex(parent_hash, parent_map);
		if (!levelDescriptorValueMatches(ini, FORGE_REVISION_PARENT_MAP_KEY, parent_map)
				|| !levelDescriptorValueMatches(ini, FORGE_REVISION_PARENT_DIGEST_KEY, parent_sha)) {
			levelFail(err, err_cap, "the received arena does not descend from the reviewed parent", arena_id);
			goto done;
		}
	}
	if (!gridMatchRuntimePreflightRoomExtension(review.pinned.path, bytes, review.pinned.size,
			arena_id, expected_sha, err, err_cap)) goto done;

	u32 scenario_size = 0;
	scenario = gridMatchPackageReadMember(bytes, review.pinned.size, review.scenario_member,
		NET_DISTRIB_MAX_COMP, &scenario_size, NULL);
	grid_native_members_t arena_members = { .scenario = review.scenario_member };
	grid_native_members_t scenario_members = {0};
	if (!scenario
			|| modArchiveMemForEachEntry(bytes, review.pinned.size, gridCheckStagedMember, &arena_members) != MODARCHIVE_OK
			|| modArchiveMemForEachEntry(scenario, scenario_size, gridCheckStagedMember, &scenario_members) != MODARCHIVE_OK
			|| arena_members.unexpected || scenario_members.unexpected) {
		levelFail(err, err_cap, "the staged arena must bind exactly one native scenario owner", arena_id);
		goto done;
	}

	const s32 installed = assetCatalogResolveAny(arena_id) != NULL;
	size_t count = 0;
	if (!gridAddSourceRoot(roots, &count, arena_id, ASSET_ARENA, err, err_cap)
			|| !gridAddSourceRoot(roots, &count, review.scenario_id, ASSET_SCENARIO, err, err_cap)
			|| !gridCollectArenaSources(scenario, scenario_size, review.scenario_member, review.scenario_id,
				roots, &count, review.scenario_deps, err, err_cap)
			|| !gridVerifyStagedOwner(&review, arena_id, ASSET_ARENA, "", review.arena_deps, installed, err, err_cap)
			|| !gridVerifyStagedOwner(&review, review.scenario_id, ASSET_SCENARIO, review.scenario_member,
				review.scenario_deps, installed, err, err_cap)
			|| !gridMatchSourcesBuildWithModelTexturesAndEdges(&plan, roots, count, gridResolveSourceType,
				gridInspectSource, gridInspectSourceWithTextures, &review, gridVisitStagedDependencies,
				&review, err, err_cap)) goto done;
	free(scenario);
	scenario = NULL;
	grid_match_offer_t offer = { .room_id = room_id, .generation = review_serial };
	memcpy(offer.manifest_digest, plan.digest, sizeof(offer.manifest_digest));
	size_t arena_index = SIZE_MAX;
	for (size_t i = 0; i < plan.package_count; i++) {
		grid_match_source_package_t *package = &plan.packages[i];
		if (package->owner.type != ASSET_ARENA || strcmp(package->owner.id, arena_id) != 0
				|| memcmp(package->digest, review.pinned.digest, sizeof(package->digest)) != 0) continue;
		arena_index = i;
		/* The native probe's label locates the received archive in memory only. */
		if (parent_id) package->path[0] = '\0';
		break;
	}
	if (arena_index == SIZE_MAX) {
		levelFail(err, err_cap, "the prepared plan lacks the exact staged arena", arena_id);
		goto done;
	}
	if (!gridStageMatchSources(&plan, &offer, &cache, arena_index, bytes, size, err, err_cap)) goto done;
	if (assetCatalogGetGeneration() != generation) {
		levelFail(err, err_cap, "the local catalog changed while checking the staged arena", "");
		goto done;
	}
	*out_offer = offer;
	*out_cache = cache;
	cache = NULL;
	sysLogPrintf(LOG_NOTE, "GRID.STAGED.SOURCES: verified arena=%s revision=%s entries=%u packages=%u digest=%s",
		arena_id, expected_sha, (u32)plan.entry_count, (u32)plan.package_count, offer.manifest_digest);
	ok = 1;

done:
	gridMatchCacheReset(&cache);
	gridMatchSourcesFree(&plan);
	free(ini);
	free(roots);
	free(scenario);
	return ok;
}

s32 forgeLevelPrepareStagedArenaSources(const char *locator, const char *arena_id,
		const char *expected_sha, u8 room_id, u64 review_serial,
		grid_match_offer_t *out_offer, grid_match_cache_t **out_cache, char *err, size_t err_cap)
{
	char path[FS_MAXPATH + 1] = "";
	if (locator && locator[0]) fsFullPath(locator, path, sizeof(path));
	u32 size = 0;
	void *bytes = path[0] ? gridMatchPackageLoad(path, &size) : NULL;
	const s32 ok = gridPrepareStagedArenaSources(path, bytes, size, arena_id, expected_sha,
		NULL, NULL, room_id, review_serial, out_offer, out_cache, err, err_cap);
	free(bytes);
	return ok;
}

s32 forgeLevelPrepareReceivedStagedArenaSources(const void *bytes, u32 size,
		const char *arena_id, const char *expected_sha, const char *parent_id, const char *parent_sha,
		u8 room_id, u64 review_serial, grid_match_offer_t *out_offer, grid_match_cache_t **out_cache,
		char *err, size_t err_cap)
{
	static const char label[] = "grid-review.pdarena";
	if (err && err_cap) err[0] = '\0';
	if (!bytes || !size || size > NET_DISTRIB_MAX_COMP || !arena_id || !expected_sha
			|| !parent_id || !parent_id[0] || !parent_sha || !parent_sha[0]
			|| strcmp(arena_id, parent_id) == 0 || strcmp(expected_sha, parent_sha) == 0
			|| assetCatalogResolveAny(arena_id)) {
		levelFail(err, err_cap, "the received arena is invalid or conflicts with a local source", arena_id);
		return 0;
	}
	u8 digest[SHA256_DIGEST_SIZE];
	char sha[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, sha);
	if (strcmp(sha, expected_sha) != 0) {
		levelFail(err, err_cap, "the received arena differs from its exact offer", arena_id);
		return 0;
	}
	if (!gridMatchCacheCheckArchiveBudget(bytes, size, err, err_cap)
			|| modAssetValidateArchiveBytes(bytes, size, label, err, err_cap) != MOD_ASSET_VALIDATE_OK) return 0;
	return gridPrepareStagedArenaSources(label, bytes, size, arena_id, expected_sha, parent_id,
		parent_sha, room_id, review_serial, out_offer, out_cache, err, err_cap);
}

s32 forgeLevelStageMatchSourcesExcept(const grid_match_sources_t *plan,
		const grid_match_offer_t *offer, const u16 *absent_packages, size_t absent_count,
		grid_match_cache_t **cache, char *err, size_t err_cap)
{
	if (!plan || !offer || !cache || *cache || absent_count > GRID_MATCH_MAX_SHARED_PACKAGES
			|| (absent_count && !absent_packages)) {
		levelFail(err, err_cap, "invalid installed match source check", "");
		return 0;
	}
	grid_match_cache_t *candidate = NULL;
	if (!gridMatchCacheBegin(&candidate, offer, plan, err, err_cap)) return 0;
	grid_match_source_package_t *installed = NULL;
	u8 omitted[MANIFEST_MAX_ENTRIES] = {0};
	s32 absent_arena = 0;
	for (size_t i = 0; i < absent_count; i++) {
		const size_t index = absent_packages[i];
		if (index >= plan->package_count || (i && absent_packages[i - 1] >= index)) {
			levelFail(err, err_cap, "invalid absent package indices", "");
			goto fail;
		}
		const grid_match_source_package_t *package = &plan->packages[index];
		const asset_type_e type = package->owner.type;
		if (package->bundled || !gridMatchPackageTypeShareable(type, package->suffix)
				|| (type == ASSET_ARENA && absent_arena)) {
			levelFail(err, err_cap, "the missing source is outside the temporary Grid package contract", package->owner.id);
			goto fail;
		}
		if (assetCatalogResolveAny(package->owner.id)) {
			levelFail(err, err_cap, "the shared package would overlap an installed asset", package->owner.id);
			goto fail;
		}
		absent_arena |= type == ASSET_ARENA;
		omitted[index] = 1;
	}
	installed = calloc(plan->package_count, sizeof(*installed));
	if (!installed) {
		levelFail(err, err_cap, "out of memory checking installed match sources", "");
		goto fail;
	}
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (omitted[entry->package_index]) {
			if (!assetCatalogResolveAny(entry->asset.id)) continue;
			levelFail(err, err_cap, "the shared package would overlap an installed asset", entry->asset.id);
			goto fail;
		}
		const grid_match_source_package_t *expected = &plan->packages[entry->package_index];
		grid_match_source_package_t *local = &installed[entry->package_index];
		grid_match_source_archive_t actual = {0};
		if (!gridInspectSource(&entry->asset, &actual, NULL, err, err_cap)) {
			goto fail;
		}
		if (expected->size != actual.size || expected->owner.type != actual.owner.type
				|| expected->bundled != actual.bundled || strcmp(expected->owner.id, actual.owner.id) != 0
				|| strcmp(expected->suffix, actual.suffix) != 0
				|| strcmp(entry->member_chain, actual.member_chain) != 0
				|| memcmp(expected->digest, actual.digest, sizeof(actual.digest)) != 0) {
			levelFail(err, err_cap, "the installed asset is a different revision", entry->asset.id);
			goto fail;
		}
		if (!local->path[0]) {
			*local = *expected;
			snprintf(local->path, sizeof(local->path), "%s", actual.path);
		}
	}
	for (size_t i = 0; i < plan->package_count; i++) {
		if (omitted[i]) continue;
		if (!gridStagePackage(candidate, offer, i, &installed[i], err, err_cap)) goto fail;
	}
	free(installed);
	*cache = candidate;
	return 1;

fail:
	free(installed);
	gridMatchCacheReset(&candidate);
	return 0;
}

s32 forgeLevelStageInstalledMatchSources(const grid_match_sources_t *plan,
		const grid_match_offer_t *offer, grid_match_cache_t **cache, char *err, size_t err_cap)
{
	return forgeLevelStageMatchSourcesExcept(plan, offer, NULL, 0, cache, err, err_cap);
}

static s32 levelReadVariantDependencyBinding(const asset_entry_t *arena,
		char plan_digest[SHA256_HEX_SIZE], u8 arena_sha[SHA256_DIGEST_SIZE], char *err, size_t err_cap)
{
	char descriptor[FS_MAXPATH], root[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	const char *leaf = NULL;
	if (!gridMatchCatalogArchiveDescriptor(arena, descriptor, sizeof(descriptor))
			|| !gridMatchPackageLocation(descriptor, root, sizeof(root), chain, sizeof(chain), &leaf)
			|| chain[0] || strcmp(leaf, "arena.ini") != 0 || assetArchiveTypeForPath(root) != ASSET_ARENA) {
		levelFail(err, err_cap, "could not locate the local Grid arena descriptor", arena->id);
		return 0;
	}
	const s32 packed = strstr(root, "::") != NULL;
	u32 size = 0;
	void *bytes = NULL;
	if (packed) {
		bytes = levelLoadPackedArena(root, &size);
	} else {
		const s32 measured = fsFileSize(root);
		if (measured > 0 && (u32)measured <= LEVEL_ARCHIVE_MAX_BYTES) bytes = fsFileLoad(root, &size);
	}
	ini_section_t *ini = malloc(sizeof(*ini));
	s32 ok = bytes && size && size <= LEVEL_ARCHIVE_MAX_BYTES && ini
		&& gridReadExactDescriptor(bytes, size, root, ini, err, err_cap);
	if (ok) {
		const char *value = iniGet(ini, FORGE_REVISION_DEPENDENCY_PLAN_KEY, NULL);
		if (value) {
			ok = levelDescriptorValueMatches(ini, "catalog_id", arena->id)
				&& levelDescriptorValueMatches(ini, GRID_ARENA_PROVENANCE_KEY, GRID_ARENA_PROVENANCE_ORIGINAL)
				&& levelDescriptorValueMatches(ini, FORGE_REVISION_DEPENDENCY_PLAN_KEY, value)
				&& strlen(value) == SHA256_HEX_SIZE - 1 && strspn(value, "0123456789abcdef") == SHA256_HEX_SIZE - 1;
			if (ok) {
				memcpy(plan_digest, value, SHA256_HEX_SIZE);
				sha256Hash(bytes, size, arena_sha);
			}
		}
	}
	free(ini);
	if (packed) free(bytes);
	else if (bytes) sysMemFree(bytes);
	if (!ok && err && err_cap && !err[0]) levelFail(err, err_cap, "the local arena's variant source binding is invalid", arena->id);
	return ok;
}

static s32 levelValidateBorrowedVariantRow(const grid_match_source_entry_t *entry,
		const grid_match_source_package_t *package, const char *arena_id, const u8 *arena_sha,
		char *err, size_t err_cap)
{
	grid_match_source_archive_t actual = {0};
	if (!gridInspectSource(&entry->asset, &actual, NULL, err, err_cap)) return 0;
	grid_match_source_package_t local = *package;
	return actual.owner.type == package->owner.type && actual.size == package->size
		&& actual.bundled == package->bundled && strcmp(actual.owner.id, package->owner.id) == 0
		&& strcmp(actual.suffix, package->suffix) == 0 && strcmp(actual.member_chain, entry->member_chain) == 0
		&& memcmp(actual.digest, package->digest, SHA256_DIGEST_SIZE) == 0
		&& assetPathCopyChecked(local.path, sizeof(local.path), actual.path)
		&& levelVariantOwnsTemporaryPackage(arena_id, arena_sha, &local);
}

static s32 levelFindMissingVariantPackages(const grid_match_sources_t *plan, const char *arena_id,
		const u8 *arena_sha, u16 *indices, size_t *missing_count, char *err, size_t err_cap)
{
	enum { PACKAGE_INSTALLED = 1, PACKAGE_MISSING = 2 };
	u8 status[MANIFEST_MAX_ENTRIES] = {0};
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		const asset_entry_t *row = assetCatalogResolveAny(entry->asset.id);
		if (row && (!row->enabled || row->type != entry->asset.type || strcmp(row->id, entry->asset.id) != 0
				|| (row->temporary && !levelValidateBorrowedVariantRow(entry, &plan->packages[entry->package_index],
					arena_id, arena_sha, err, err_cap)))) {
			levelFail(err, err_cap, "the locally installed row conflicts with the variant", entry->asset.id);
			return 0;
		}
		status[entry->package_index] |= row ? PACKAGE_INSTALLED : PACKAGE_MISSING;
	}
	for (size_t i = 0; i < plan->package_count; i++) {
		if (status[i] == PACKAGE_INSTALLED) continue;
		const grid_match_source_package_t *package = &plan->packages[i];
		if (status[i] != PACKAGE_MISSING || package->bundled
				|| !gridMatchPackageTypeShareable(package->owner.type, package->suffix)
				|| assetCatalogResolveAny(package->owner.id)) {
			levelFail(err, err_cap, "the variant needs this exact installed package", package->owner.id);
			return 0;
		}
		indices[(*missing_count)++] = (u16)i;
	}
	return 1;
}

static s32 levelCommitNewBytes(const char *path, const void *bytes, u32 size)
{
	save_atomic_file_t output;
	if (saveAtomicBegin(&output, path) != 0) return 0;
	if (fwrite(bytes, 1, size, saveAtomicStream(&output)) != size) {
		saveAtomicAbort(&output);
		return 0;
	}
	return saveAtomicCommitNew(&output) == 0;
}

/* Only a successful exclusive publish hands the scratch file to the caller. */
static s32 levelWriteVariantSource(const grid_match_source_package_t *package,
		const void *bytes, u32 size, char out_path[FS_MAXPATH + 1], char *err, size_t err_cap)
{
	char folder[FS_MAXPATH + 1], path[FS_MAXPATH + 1];
	if (!bytes || size != package->size || !gridMatchPackagePrivatePath(package->digest, package->suffix, folder, path)
			|| !fsCreateDir(folder) || !levelCommitNewBytes(path, bytes, size)) {
		levelFail(err, err_cap, "could not stage the saved variant dependency", package->owner.id);
		return 0;
	}
	strcpy(out_path, path);
	return 1;
}

static s32 levelValidateMountedVariantSources(const char *arena_id, const u8 arena_sha[SHA256_DIGEST_SIZE],
		const grid_match_sources_t *expected, char *err, size_t err_cap)
{
	forge_level_source_t source;
	forge_level_doc_t *doc = levelOpenSource(arena_id, &source, NULL, err, err_cap);
	if (!doc) return 0;
	char hex[SHA256_HEX_SIZE];
	sha256ToHex(arena_sha, hex);
	grid_match_sources_t actual = {0}, dependencies = {0};
	const s32 ok = strcmp(source.archive_sha256, hex) == 0
		&& levelBuildVariantSources(doc, &source, &actual, err, err_cap)
		&& levelProjectVariantDependencies(&actual, &dependencies, err, err_cap)
		&& strcmp(expected->digest, dependencies.digest) == 0;
	if (!ok && err && err_cap && !err[0]) levelFail(err, err_cap, "the exact variant source plan does not match its native level", arena_id);
	gridMatchSourcesFree(&dependencies);
	gridMatchSourcesFree(&actual);
	forgeLevelDocFree(doc);
	return ok;
}

static s32 levelRevalidateMountedVariant(const char *arena_id, const u8 arena_sha[SHA256_DIGEST_SIZE],
		u32 generation, char *err, size_t err_cap)
{
	const grid_match_sources_t *plan = &s_VariantDependencyMount.plan;
	const grid_match_session_sources_t *sources = &s_VariantDependencyMount.sources;
	u16 missing[MANIFEST_MAX_ENTRIES];
	size_t count = 0;
	if (!levelFindMissingVariantPackages(plan, arena_id, arena_sha, missing, &count, err, err_cap)) return 0;
	if (count) {
		levelFail(err, err_cap, "a mounted variant source is no longer available", arena_id);
		return 0;
	}
	u8 leased[MANIFEST_MAX_ENTRIES] = {0};
	for (size_t i = 0; i < sources->count; i++) leased[sources->files[i].package_index] = 1;
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (!leased[entry->package_index]) continue;
		const asset_entry_t *row = assetCatalogResolve(entry->asset.id);
		if (!row || !row->temporary) {
			levelFail(err, err_cap, "a mounted source no longer owns its exact catalog row", entry->asset.id);
			return 0;
		}
	}
	if (!levelValidateMountedVariantSources(arena_id, arena_sha, plan, err, err_cap)
			|| generation != assetCatalogGetGeneration()) return 0;
	s_VariantDependencyMount.catalog_generation = generation;
	return 1;
}

s32 forgeLevelVariantDependenciesStage(void)
{
	return s_VariantDependencyMount.plan.packages ? s_VariantDependencyMount.stage : -1;
}

const grid_match_sources_t *forgeLevelVariantDependencyPlanForStage(s32 stage)
{
	return stage >= 0 && stage == forgeLevelVariantDependenciesStage() ? &s_VariantDependencyMount.plan : NULL;
}

s32 forgeLevelVariantDependencyOwnsPackage(const char *arena_id,
		const u8 arena_sha256[SHA256_DIGEST_SIZE], const grid_match_source_package_t *package)
{
	const grid_match_session_sources_t *sources = &s_VariantDependencyMount.sources;
	if (!sources->lease || !s_VariantDependencyMount.plan.packages || !arena_id || !arena_sha256
			|| !package || package->bundled
			|| strcmp(arena_id, s_VariantDependencyMount.arena_id) != 0
			|| memcmp(arena_sha256, s_VariantDependencyMount.arena_sha256, SHA256_DIGEST_SIZE) != 0) return 0;
	for (size_t i = 0; i < sources->count; i++) {
		const grid_match_session_file_t *file = &sources->files[i];
		const grid_match_source_package_t *expected = &s_VariantDependencyMount.plan.packages[file->package_index];
		const char *path = assetCatalogGridSourceSetPath(sources->lease, file->package_index);
		if (expected->owner.type == package->owner.type && expected->size == package->size
				&& strcmp(expected->owner.id, package->owner.id) == 0 && strcmp(expected->suffix, package->suffix) == 0
				&& path && strcmp(path, file->path) == 0 && strcmp(path, package->path) == 0
				&& memcmp(expected->digest, package->digest, SHA256_DIGEST_SIZE) == 0) return 1;
	}
	return 0;
}

s32 forgeLevelReleaseVariantDependencyStageRefs(char *err, size_t err_cap)
{
	return assetCatalogReleaseGridSourceSetStageRefs(s_VariantDependencyMount.sources.lease, err, err_cap);
}

s32 forgeLevelRetireVariantDependencies(char *err, size_t err_cap)
{
	grid_match_session_sources_t *sources = &s_VariantDependencyMount.sources;
	if (err && err_cap) err[0] = '\0';
	s32 retired = 1;
	if (sources->lease || sources->files) {
		if (!gridMatchRuntimeRetrySourceSetRetirement(err, err_cap)) return 0;
		retired = gridMatchRuntimeRetireDetachedSourceSet(sources, err, err_cap);
	}
	if (!sources->files && !sources->lease) {
		if (retired && s_VariantDependencyMount.arena_id[0]) {
			sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.VARIANT.MOUNT: retired arena=%s", s_VariantDependencyMount.arena_id);
		}
		gridMatchSourcesFree(&s_VariantDependencyMount.plan);
		memset(&s_VariantDependencyMount, 0, sizeof(s_VariantDependencyMount));
	}
	return retired;
}

s32 forgeLevelMountVariantDependencies(const char *arena_id, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const asset_entry_t *arena = arena_id && arena_id[0] ? assetCatalogResolve(arena_id) : NULL;
	if (g_NetMode == NETMODE_CLIENT || !arena || arena->type != ASSET_ARENA || arena->bundled
			|| arena->temporary || !arena->grid_offer_only) return 1;
	const s32 stage = arena->ext.arena.stagenum;
	const u32 generation = assetCatalogGetGeneration();
	char digest[SHA256_HEX_SIZE] = "";
	u8 arena_sha[SHA256_DIGEST_SIZE] = {0};
	if (!levelReadVariantDependencyBinding(arena, digest, arena_sha, err, err_cap)) return 0;
	if (!digest[0]) return 1;
	if (stage < 0 || strcmp(arena->id, arena_id) != 0) {
		levelFail(err, err_cap, "the saved variant has no exact local stage binding", arena_id);
		return 0;
	}
	/* Published catalog rows can move the storage that owns the caller's ID. */
	char stable_id[CATALOG_ID_LEN];
	strcpy(stable_id, arena_id);
	arena_id = stable_id;
	if (s_VariantDependencyMount.plan.packages || s_VariantDependencyMount.sources.files) {
		if (s_VariantDependencyMount.plan.packages && stage == s_VariantDependencyMount.stage
				&& strcmp(arena_id, s_VariantDependencyMount.arena_id) == 0
				&& strcmp(digest, s_VariantDependencyMount.plan.digest) == 0
				&& memcmp(arena_sha, s_VariantDependencyMount.arena_sha256, sizeof(arena_sha)) == 0) {
			return generation == s_VariantDependencyMount.catalog_generation
				|| levelRevalidateMountedVariant(arena_id, arena_sha, generation, err, err_cap);
		}
		levelFail(err, err_cap, "a local variant source mount still belongs to another stage or revision", arena_id);
		return 0;
	}

	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	grid_match_sources_t plan = {0};
	grid_match_cache_t *cache = NULL;
	grid_match_session_sources_t pending = {0};
	const char **paths = NULL;
	u16 missing[MANIFEST_MAX_ENTRIES];
	size_t count = 0;
	s32 ok = 0;
	if (!forgeRevisionLoadDependencyPlan(directory, digest, &plan, err, err_cap)
			|| !levelFindMissingVariantPackages(&plan, arena_id, arena_sha, missing, &count, err, err_cap)) goto done;
	if (count && (gridMatchRuntimePending() || gridMatchRuntimeActive())) {
		levelFail(err, err_cap, "another exact Grid match already owns its stage sources", arena_id);
		goto done;
	}
	if (count && !gridMatchRuntimeRetrySourceSetRetirement(err, err_cap)) goto done;
	/* The cache uses a local key only to check the combined exact reader budget. */
	grid_match_offer_t offer = { .room_id = 0, .generation = 1 };
	memcpy(offer.manifest_digest, plan.digest, sizeof(offer.manifest_digest));
	if (!forgeLevelStageMatchSourcesExcept(&plan, &offer, missing, count, &cache, err, err_cap)) goto done;
	for (size_t i = 0; i < count; i++) {
		const u16 index = missing[i];
		u32 size = 0;
		void *bytes = forgeRevisionLoadDependency(directory, &plan.packages[index], &size, err, err_cap);
		const s32 valid = bytes && gridMatchCacheAccept(cache, &offer, index, bytes, size, err, err_cap);
		free(bytes);
		if (!valid) goto done;
	}
	if (!gridMatchCacheReady(cache, &offer) || generation != assetCatalogGetGeneration()) {
		levelFail(err, err_cap, "the exact local variant sources changed during preparation", arena_id);
		goto done;
	}
	if (count) {
		pending.files = calloc(count, sizeof(*pending.files));
		paths = calloc(count, sizeof(*paths));
		if (!pending.files || !paths) {
			levelFail(err, err_cap, "could not allocate the local variant source mount", arena_id);
			goto done;
		}
		pending.count = count;
		for (size_t i = 0; i < count; i++) {
			const u16 index = missing[i];
			grid_match_session_file_t *file = &pending.files[i];
			file->owner = plan.packages[index].owner;
			file->package_index = index;
			paths[i] = file->path;
			u32 size = 0;
			const void *bytes = gridMatchCacheRead(cache, &offer, index, &size);
			if (!levelWriteVariantSource(&plan.packages[index], bytes, size, file->path, err, err_cap)) goto done;
		}
		if (!assetCatalogAdmitGridSourceSet(&plan, count, missing, paths, arena_id, NULL,
				&pending.lease, err, err_cap)) goto done;
	}
	if (!levelValidateMountedVariantSources(arena_id, arena_sha, &plan, err, err_cap)) goto done;
	s_VariantDependencyMount.sources = pending;
	s_VariantDependencyMount.plan = plan;
	strcpy(s_VariantDependencyMount.arena_id, arena_id);
	memcpy(s_VariantDependencyMount.arena_sha256, arena_sha, sizeof(arena_sha));
	s_VariantDependencyMount.stage = stage;
	s_VariantDependencyMount.catalog_generation = assetCatalogGetGeneration();
	pending = (grid_match_session_sources_t){0};
	plan = (grid_match_sources_t){0};
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.VARIANT.MOUNT: arena=%s plan=%s restored=%u", arena_id, digest, (unsigned)count);
	ok = 1;

done:
	if (pending.files) {
		char cleanup[192];
		if (!gridMatchRuntimeRetireDetachedSourceSet(&pending, cleanup, sizeof(cleanup))) {
			sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.VARIANT.MOUNT: deferred scratch cleanup: %s", cleanup);
			if (pending.files) {
				s_VariantDependencyMount.sources = pending;
				s_VariantDependencyMount.stage = -1;
			}
		}
	}
	gridMatchCacheReset(&cache);
	gridMatchSourcesFree(&plan);
	free(paths);
	return ok;
}

typedef struct level_played_variant {
	grid_source_review_t review;
	char dependency_plan[SHA256_HEX_SIZE];
	const grid_match_sources_t *match_sources;
	const void *arena_bytes;
	u32 arena_size;
	void *scenario;
	u32 scenario_size;
	forge_level_doc_t *doc;
	grid_match_session_sources_t temporary;
	size_t arena_index;
} level_played_variant_t;

s32 forgeLevelCanReviewPlayedVariantSources(const grid_match_result_t *result)
{
	const grid_match_result_t *current = netGridLastMatchResult();
	const s32 listen_host = g_NetMode == NETMODE_SERVER && !g_NetDedicated;
	if (!result || !current || (g_NetMode != NETMODE_CLIENT && !listen_host) || !g_NetLocalClient
			|| g_NetLocalClient->state != CLSTATE_LOBBY || g_NetStageEpoch || mainStageChangePending()
			|| !gridMatchRuntimeIsIdle() || s_VariantDependencyMount.plan.packages
			|| s_VariantDependencyMount.sources.files) return 0;
	const grid_match_sources_t *sources = gridMatchRuntimeReturnedSources(result);
	if (!sources || sources != gridMatchRuntimeReturnedSources(current)) return 0;
	static u8 inspected_sha[SHA256_DIGEST_SIZE];
	static s32 inspected;
	static s32 original;
	if (!inspected || memcmp(inspected_sha, result->arena_sha256, sizeof(inspected_sha)) != 0) {
		u32 size = 0;
		const void *bytes = gridMatchRuntimeReadReturnedArena(result, &size);
		original = bytes && gridMatchPackageOrigin("played-grid.pdarena", bytes, size) == GRID_ARCHIVE_ORIGINAL;
		memcpy(inspected_sha, result->arena_sha256, sizeof(inspected_sha));
		inspected = 1;
	}
	return original;
}

static s32 levelReadPlayedArena(level_played_variant_t *played, const void *bytes, u32 size,
		const char *locator, const char *arena_id, char *err, size_t err_cap)
{
	grid_source_review_t *review = &played->review;
	ini_section_t *ini = malloc(sizeof(*ini));
	s32 ok = bytes && size && size <= NET_DISTRIB_MAX_COMP && ini
		&& assetPathCopyChecked(review->revision.arena_id, sizeof(review->revision.arena_id), arena_id)
		&& assetPathCopyChecked(review->pinned.path, sizeof(review->pinned.path), locator)
		&& gridReadExactDescriptor(bytes, size, locator, ini, err, err_cap)
		&& strcmp(ini->type, "arena") == 0 && levelDescriptorValueMatches(ini, "catalog_id", arena_id)
		&& levelDescriptorValueMatches(ini, GRID_ARENA_PROVENANCE_KEY, GRID_ARENA_PROVENANCE_ORIGINAL)
		&& assetPathCopyChecked(review->scenario_id, sizeof(review->scenario_id), iniGet(ini, "scenario", ""))
		&& assetPathCopyChecked(review->scenario_member, sizeof(review->scenario_member), iniGet(ini, "scenario_archive", ""))
		&& levelDescriptorValueMatches(ini, "scenario", review->scenario_id)
		&& levelDescriptorValueMatches(ini, "scenario_archive", review->scenario_member)
		&& assetArchiveTypeForPath(review->scenario_member) == ASSET_SCENARIO
		&& gridCopyDeclaredDependencies(ini, arena_id, review->arena_deps, err, err_cap);
	if (ok) {
		const char *binding = iniGet(ini, FORGE_REVISION_DEPENDENCY_PLAN_KEY, NULL);
		if (binding) {
			ok = levelDescriptorValueMatches(ini, FORGE_REVISION_DEPENDENCY_PLAN_KEY, binding)
				&& strlen(binding) == SHA256_HEX_SIZE - 1 && strspn(binding, "0123456789abcdef") == SHA256_HEX_SIZE - 1;
			if (ok) memcpy(played->dependency_plan, binding, SHA256_HEX_SIZE);
		}
	}
	free(ini);
	if (ok) {
		review->pinned.owner.type = ASSET_ARENA;
		strcpy(review->pinned.owner.id, arena_id);
		strcpy(review->pinned.suffix, ".pdarena");
		review->pinned.size = size;
		sha256Hash(bytes, size, review->pinned.digest);
		sha256ToHex(review->pinned.digest, review->revision.digest);
		played->scenario = gridMatchPackageReadMember(bytes, size, review->scenario_member,
			NET_DISTRIB_MAX_COMP, &played->scenario_size, NULL);
		ok = played->scenario != NULL;
	}
	if (!ok && err && err_cap && !err[0]) levelFail(err, err_cap, "the played archive has no exact original arena and scenario", arena_id);
	return ok;
}

static s32 levelStagePlayedDependencies(level_played_variant_t *played, const grid_match_result_t *result,
		char *err, size_t err_cap)
{
	const grid_match_sources_t *plan = played->match_sources;
	u8 occupied[MANIFEST_MAX_ENTRIES] = {0};
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		occupied[entry->package_index] |= assetCatalogResolveAny(entry->asset.id) != NULL;
	}
	grid_match_session_sources_t *temporary = &played->temporary;
	/* Extra capacity for the parent's exact raw archive during Save. The
	 * played arena itself is never admitted as a temporary catalog stage. */
	temporary->files = calloc(plan->package_count, sizeof(*temporary->files));
	const char **paths = calloc(plan->package_count, sizeof(*paths));
	u16 missing[MANIFEST_MAX_ENTRIES];
	s32 ok = temporary->files && paths;
	if (!ok) levelFail(err, err_cap, "could not allocate the played source review", "");
	for (size_t i = 0; ok && i < plan->package_count; i++) {
		const grid_match_source_package_t *package = &plan->packages[i];
		if (package->owner.type == ASSET_ARENA || occupied[i] || assetCatalogResolveAny(package->owner.id)) continue;
		u32 size = 0;
		const void *bytes = gridMatchRuntimeReadReturnedSource(result, i, &size);
		if (!bytes) continue;
		if (!forgeLevelMatchPackageShareAllowed(package, bytes, size)) {
			levelFail(err, err_cap, "a retained played archive is no longer eligible", package->owner.id);
			ok = 0;
			break;
		}
		grid_match_session_file_t *file = &temporary->files[temporary->count];
		ok = levelWriteVariantSource(package, bytes, size, file->path, err, err_cap);
		if (ok) {
			file->owner = package->owner;
			file->package_index = (u16)i;
			paths[temporary->count] = file->path;
			missing[temporary->count++] = (u16)i;
		}
	}
	if (ok && temporary->count) {
		ok = assetCatalogAdmitGridSourceSet(plan, temporary->count, missing, paths, result->arena_id,
			NULL, &temporary->lease, err, err_cap);
	}
	free(paths);
	return ok;
}

static s32 levelRetirePlayedScratch(level_played_variant_t *played, char *err, size_t err_cap)
{
	if (!played->temporary.files) return 1;
	if (!played->temporary.count) {
		free(played->temporary.files);
		played->temporary.files = NULL;
		return 1;
	}
	return gridMatchRuntimeRetireDetachedSourceSet(&played->temporary, err, err_cap);
}

static void levelClosePlayedVariant(level_played_variant_t *played)
{
	char cleanup[192];
	if (!levelRetirePlayedScratch(played, cleanup, sizeof(cleanup))) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.PLAYED: deferred review cleanup: %s", cleanup);
		if (played->temporary.files && !s_VariantDependencyMount.sources.files) {
			s_VariantDependencyMount.sources = played->temporary;
			s_VariantDependencyMount.stage = -1;
		}
	}
	forgeLevelDocFree(played->doc);
	free(played->scenario);
}

static s32 levelPreparePlayedVariant(const grid_match_result_t *result, level_played_variant_t *played,
		grid_match_sources_t *reviewed, char *err, size_t err_cap)
{
	if (!forgeLevelCanReviewPlayedVariantSources(result)) {
		levelFail(err, err_cap, "the exact played Grid sources are no longer available in this Room", "");
		return 0;
	}
	played->match_sources = gridMatchRuntimeReturnedSources(result);
	played->arena_bytes = gridMatchRuntimeReadReturnedArena(result, &played->arena_size);
	if (!levelReadPlayedArena(played, played->arena_bytes, played->arena_size, "played-grid.pdarena",
			result->arena_id, err, err_cap)) return 0;
	if (memcmp(played->review.pinned.digest, result->arena_sha256, SHA256_DIGEST_SIZE) != 0) {
		levelFail(err, err_cap, "the retained arena does not match the played revision", result->arena_id);
		return 0;
	}
	played->arena_index = SIZE_MAX;
	const grid_match_sources_t *sources = played->match_sources;
	for (size_t i = 0; i < sources->package_count; i++) {
		const grid_match_source_package_t *package = &sources->packages[i];
		if (package->owner.type == ASSET_ARENA && strcmp(package->owner.id, result->arena_id) == 0
				&& package->size == played->arena_size && memcmp(package->digest, result->arena_sha256, SHA256_DIGEST_SIZE) == 0) {
			played->arena_index = i;
			break;
		}
	}
	if (played->arena_index == SIZE_MAX) {
		levelFail(err, err_cap, "the verified match does not contain this played arena package", result->arena_id);
		return 0;
	}
	played->doc = forgeLevelReadScenarioDocument(played->scenario, played->scenario_size, err, err_cap);
	const s32 installed = assetCatalogResolveAny(result->arena_id) != NULL;
	return played->doc && levelStagePlayedDependencies(played, result, err, err_cap)
		&& levelCollectVariantSources(&played->review, played->scenario, played->scenario_size,
			played->doc, reviewed, err, err_cap)
		&& gridVerifyStagedOwner(&played->review, result->arena_id, ASSET_ARENA, "",
			played->review.arena_deps, installed, err, err_cap)
		&& gridVerifyStagedOwner(&played->review, played->review.scenario_id, ASSET_SCENARIO,
			played->review.scenario_member, played->review.scenario_deps, installed, err, err_cap)
		&& gridMatchSourcesExactSubset(reviewed, sources, err, err_cap)
		&& levelVariantCheckRemainingSources(reviewed, reviewed, (u8[MANIFEST_MAX_ENTRIES]){0},
			result->arena_id, result->arena_sha256, &played->temporary, err, err_cap);
}

s32 forgeLevelReviewPlayedVariantSources(const grid_match_result_t *result,
		forge_level_doc_t **out_doc, grid_match_sources_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out_doc || *out_doc || !out) {
		levelFail(err, err_cap, "the played review requires an empty document output", "");
		return 0;
	}
	level_played_variant_t played = {0};
	grid_match_sources_t review = {0};
	const s32 ok = levelPreparePlayedVariant(result, &played, &review, err, err_cap)
		&& levelRetirePlayedScratch(&played, err, err_cap);
	if (ok) {
		for (size_t i = 0; i < review.package_count; i++) review.packages[i].path[0] = '\0';
		gridMatchSourcesFree(out);
		*out = review;
		review = (grid_match_sources_t){0};
		*out_doc = played.doc;
		played.doc = NULL;
	}
	levelClosePlayedVariant(&played);
	gridMatchSourcesFree(&review);
	return ok;
}

static s32 levelPreservePlayedParent(const level_played_variant_t *played,
		const char *directory, char *err, size_t err_cap)
{
	const forge_revision_t *source = &played->review.revision;
	forge_revision_t pin;
	if (forgeRevisionRead(directory, source->arena_id, source->digest, &pin, NULL, 0)) return 1;
	char path[FS_MAXPATH + 1];
	const int length = snprintf(path, sizeof(path), "%s/%s.pdarena", directory, source->digest);
	if (!fsPathIsAbsolute(directory) || length <= 0 || (size_t)length >= sizeof(path)
			|| !fsCreateDir(directory)) {
		levelFail(err, err_cap, "the profile revision directory could not preserve the played parent", source->arena_id);
		return 0;
	}
	return levelCommitNewBytes(path, played->arena_bytes, played->arena_size)
		|| forgeRevisionRead(directory, source->arena_id, source->digest, &pin, err, err_cap);
}

static s32 levelValidateBoundSaveCandidate(const char *locator, const char *arena_id,
		const void *archive, u32 size, char *err, size_t err_cap)
{
	ini_section_t *ini = malloc(sizeof(*ini));
	s32 ok = ini && gridReadExactDescriptor(archive, size, locator, ini, err, err_cap);
	const s32 bound = ok && iniGet(ini, FORGE_REVISION_DEPENDENCY_PLAN_KEY, NULL) != NULL;
	free(ini);
	if (!bound) return ok;

	level_played_variant_t saved = {0};
	grid_match_sources_t actual = {0}, deps = {0}, stored = {0};
	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	ok = levelReadPlayedArena(&saved, archive, size, locator, arena_id, err, err_cap)
		&& forgeRevisionLoadDependencyPlan(directory, saved.dependency_plan, &stored, err, err_cap)
		&& levelCollectVariantSources(&saved.review, saved.scenario, saved.scenario_size, NULL, &actual, err, err_cap)
		&& actual.package_count > 1 && levelProjectVariantDependencies(&actual, &deps, err, err_cap)
		&& strcmp(stored.digest, deps.digest) == 0;
	if (!ok) levelFail(err, err_cap, "these edits change the saved variant's dependency binding", "save the draft as a new Variant");
	gridMatchSourcesFree(&stored);
	gridMatchSourcesFree(&deps);
	gridMatchSourcesFree(&actual);
	free(saved.scenario);
	return ok;
}

static s32 levelValidatePlayedVariantCandidate(const mod_author_result_t *candidate,
		const grid_match_sources_t *chosen, const char *dependency_plan, char *err, size_t err_cap)
{
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(candidate->archive_path, &size);
	level_played_variant_t saved = {0};
	grid_match_sources_t actual = {0}, actual_deps = {0}, chosen_deps = {0};
	s32 ok = levelReadPlayedArena(&saved, bytes, size, candidate->archive_path, candidate->catalog_id, err, err_cap)
		&& strcmp(saved.dependency_plan, dependency_plan) == 0
		&& gridMatchRuntimePreflightLocalArena(candidate->archive_path, bytes, size, candidate->catalog_id,
			saved.review.revision.digest, err, err_cap)
		&& levelCollectVariantSources(&saved.review, saved.scenario, saved.scenario_size, NULL, &actual, err, err_cap);
	if (ok && (actual.package_count > 1 || chosen->package_count > 1)) {
		ok = levelProjectVariantDependencies(&actual, &actual_deps, err, err_cap)
			&& levelProjectVariantDependencies(chosen, &chosen_deps, err, err_cap)
			&& strcmp(actual_deps.digest, chosen_deps.digest) == 0;
	}
	if (!ok && err && err_cap && !err[0]) levelFail(err, err_cap, "the new native archive changed the reviewed source closure", candidate->catalog_id);
	gridMatchSourcesFree(&chosen_deps);
	gridMatchSourcesFree(&actual_deps);
	gridMatchSourcesFree(&actual);
	free(saved.scenario);
	free(bytes);
	return ok;
}

static s32 levelPublishCheckedVariantCopy(const mod_author_request_t *request,
		mod_author_result_t *target, const grid_match_sources_t *chosen, const char *dependency_plan,
		char *err, size_t err_cap)
{
	static u32 serial;
	char staging[MOD_AUTHOR_PATH_MAX], category[MOD_AUTHOR_PATH_MAX];
	const int length = snprintf(staging, sizeof(staging), "%s/.gv-%llx-%u", request->mods_root,
		(unsigned long long)sysGetMicroseconds(), (unsigned)++serial);
	if (length <= 0 || (size_t)length >= sizeof(staging) || _mkdir(staging) != 0) {
		levelFail(err, err_cap, "could not create the private variant folder", "");
		return 0;
	}
	mod_author_result_t *candidate = calloc(1, sizeof(*candidate));
	mod_author_request_t draft = *request;
	draft.mods_root = staging;
	const s32 written = candidate && modAuthorSave(&draft, candidate, err, err_cap) == MOD_AUTHOR_OK;
	s32 ok = written && levelValidatePlayedVariantCandidate(candidate, chosen, dependency_plan, err, err_cap);
	if (ok) {
		ok = levelForkCategory(target->mod_dir, category, sizeof(category)) && fsCreateDir(category)
			&& MoveFileExA(candidate->mod_dir, target->mod_dir, MOVEFILE_WRITE_THROUGH);
		if (ok) target->ids = candidate->ids;
		else levelFail(err, err_cap, "could not publish the new variant folder", target->mod_dir);
	} else if (!candidate) {
		levelFail(err, err_cap, "could not allocate the variant archive candidate", "");
	}
	if (written) {
		if (!ok) {
			remove(candidate->archive_path);
			remove(candidate->manifest_path);
			_rmdir(candidate->mod_dir);
		}
		if (levelForkCategory(candidate->mod_dir, category, sizeof(category))) _rmdir(category);
	}
	if (_rmdir(staging) != 0) sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.VARIANT: could not remove staging folder '%s'", staging);
	free(candidate);
	return ok;
}

static s32 levelReadCopyBinding(const char *path, s32 *bound, char digest[SHA256_HEX_SIZE],
		level_save_parent_t *parent, char *err, size_t err_cap)
{
	mod_archive_t *archive = modArchiveOpen(path);
	const s32 index = archive ? modArchiveFindEntry(archive, "arena.ini") : -1;
	const u32 expected = index >= 0 ? modArchiveGetEntrySize(archive, index) : 0;
	u32 size = 0;
	char *text = expected && expected <= 1024u * 1024u ? modArchiveExtractAlloc(archive, index, &size) : NULL;
	ini_section_t *ini = malloc(sizeof(*ini));
	s32 ok = text && size == expected && !memchr(text, '\0', size) && ini
		&& iniParseText(text, size, ini, err, err_cap);
	if (ok) {
		const char *value = iniGet(ini, FORGE_REVISION_DEPENDENCY_PLAN_KEY, NULL);
		*bound = value != NULL;
		if (value && digest) {
			ok = levelSaveDigestValid(value)
				&& levelDescriptorValueMatches(ini, FORGE_REVISION_DEPENDENCY_PLAN_KEY, value);
			if (ok) memcpy(digest, value, SHA256_HEX_SIZE);
		}
	}
	if (ok && parent) {
		const char *map = iniGet(ini, FORGE_REVISION_PARENT_MAP_KEY, NULL);
		const char *revision = iniGet(ini, FORGE_REVISION_PARENT_DIGEST_KEY, NULL);
		if (map || revision) {
			ok = levelSaveDigestValid(map) && levelSaveDigestValid(revision)
				&& levelDescriptorValueMatches(ini, FORGE_REVISION_PARENT_MAP_KEY, map)
				&& levelDescriptorValueMatches(ini, FORGE_REVISION_PARENT_DIGEST_KEY, revision);
			if (ok) {
				memcpy(parent->map_sha256, map, SHA256_HEX_SIZE);
				memcpy(parent->revision_sha256, revision, SHA256_HEX_SIZE);
			}
		}
	}
	free(ini);
	free(text);
	if (archive) modArchiveClose(archive);
	if (!ok && err && err_cap && !err[0]) levelFail(err, err_cap, "could not read the original variant source binding", "");
	return ok;
}

static s32 levelSaveVariantAwareCopy(forge_level_doc_t *doc, const forge_level_source_t *source,
		const mod_author_request_t *request, const grid_match_sources_t *checked, const char *dependency_plan,
		const level_save_parent_t *parent, mod_author_result_t *target, s32 *handled, char *err, size_t err_cap)
{
	*handled = checked != NULL;
	if (!*handled && !levelReadCopyBinding(request->template_path, handled, NULL, NULL, err, err_cap)) return MOD_AUTHOR_ERR_ARCHIVE;
	if (!*handled) return MOD_AUTHOR_OK;

	grid_match_sources_t computed = {0};
	char computed_digest[SHA256_HEX_SIZE] = "";
	s32 code = MOD_AUTHOR_ERR_ARCHIVE;
	if (!checked) {
		if (!levelBuildVariantSources(doc, source, &computed, err, err_cap)) goto done;
		const u8 *arena_sha = NULL;
		for (size_t i = 0; i < computed.package_count; i++) {
			const grid_match_source_package_t *package = &computed.packages[i];
			if (package->owner.type == ASSET_ARENA && strcmp(package->owner.id, source->arena_id) == 0) {
				arena_sha = package->digest;
				break;
			}
		}
		if (!arena_sha || !levelVariantCheckRemainingSources(&computed, &computed, (u8[MANIFEST_MAX_ENTRIES]){0},
				source->arena_id, arena_sha, NULL, err, err_cap)
				|| !levelStoreVariantDependencies(&computed, computed_digest, err, err_cap)) goto done;
		checked = &computed;
		dependency_plan = computed_digest;
	}
	const mod_author_descriptor_value_t fields[] = {
		{ GRID_ARENA_PROVENANCE_KEY, GRID_ARENA_PROVENANCE_ORIGINAL },
		{ FORGE_REVISION_PARENT_MAP_KEY, parent->map_sha256 },
		{ FORGE_REVISION_PARENT_DIGEST_KEY, parent->revision_sha256 },
		{ FORGE_REVISION_DEPENDENCY_PLAN_KEY, dependency_plan[0] ? dependency_plan : NULL },
	};
	mod_author_request_t draft = *request;
	draft.descriptor_values = fields;
	draft.descriptor_value_count = ARRAYCOUNT(fields);
	if (!levelPublishCheckedVariantCopy(&draft, target, checked, dependency_plan, err, err_cap)) goto done;
	code = MOD_AUTHOR_OK;
	char directory[FS_MAXPATH + 1], library_error[192] = "";
	forge_revision_t pin;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!fsPathIsAbsolute(directory) || !fsCreateDir(directory)
			|| !forgeRevisionPublishLocal(directory, target->catalog_id, target->archive_path, NULL, &pin,
				library_error, sizeof(library_error))
			|| !modmgrEnableAuthoredGridVariant(target->mod_dir, target->mod_id, pin.arena_id, pin.digest,
				library_error, sizeof(library_error))) {
		levelFail(err, err_cap, "the variant was saved but the local library could not refresh", library_error);
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.VARIANT: saved without library refresh: %s", library_error);
	}

done:
	gridMatchSourcesFree(&computed);
	return code;
}

static s32 levelPrepareSharedVariant(const char *arena_id, const char *parent_sha,
		forge_level_doc_t *doc, level_played_variant_t *parent, grid_match_sources_t *before,
		s32 *new_mount, char *err, size_t err_cap)
{
	if (!arena_id || !parent_sha || !doc || g_NetMode != NETMODE_SERVER || !gridMatchRuntimeIsIdle()
			|| g_NetStageEpoch || mainStageChangePending()) {
		levelFail(err, err_cap, "save the committed draft from its host Room", "");
		return 0;
	}
	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	forge_revision_t pin;
	if (!forgeRevisionRead(directory, arena_id, parent_sha, &pin, err, err_cap)) return 0;
	parent->arena_bytes = gridMatchPackageLoad(pin.path, &parent->arena_size);
	if (!levelReadPlayedArena(parent, parent->arena_bytes, parent->arena_size, pin.path, pin.arena_id, err, err_cap)
			|| strcmp(parent->review.revision.digest, pin.digest) != 0) return 0;
	parent->review.revision = pin;

	const s32 installed = assetCatalogResolveAny(arena_id) != NULL;
	if (!gridVerifyStagedOwner(&parent->review, arena_id, ASSET_ARENA, "", parent->review.arena_deps,
			installed, err, err_cap)) return 0;
	if (parent->dependency_plan[0] && installed) {
		const s32 prior = forgeLevelVariantDependenciesStage();
		if (!forgeLevelMountVariantDependencies(arena_id, err, err_cap)) return 0;
		*new_mount = prior < 0 && forgeLevelVariantDependenciesStage() >= 0;
	}
	return levelCollectVariantSources(&parent->review, parent->scenario, parent->scenario_size, doc, before, err, err_cap)
		&& gridVerifyStagedOwner(&parent->review, parent->review.scenario_id, ASSET_SCENARIO,
			parent->review.scenario_member, parent->review.scenario_deps, installed, err, err_cap)
		&& levelVariantCheckRemainingSources(before, before, (u8[MANIFEST_MAX_ENTRIES]){0},
			arena_id, parent->review.pinned.digest, NULL, err, err_cap);
}

static s32 levelFinishSharedVariant(level_played_variant_t *parent, s32 new_mount, char *err, size_t err_cap)
{
	free((void *)parent->arena_bytes);
	free(parent->scenario);
	if (!new_mount) return 1;
	return forgeLevelRetireVariantDependencies(err, err_cap);
}

s32 forgeLevelReviewSharedVariantSources(const char *arena_id, const char *parent_sha,
		forge_level_doc_t *doc, grid_match_sources_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out) return 0;
	level_played_variant_t parent = {0};
	grid_match_sources_t before = {0};
	s32 new_mount = 0;
	s32 ok = levelPrepareSharedVariant(arena_id, parent_sha, doc, &parent, &before, &new_mount, err, err_cap);
	char cleanup[192];
	if (!levelFinishSharedVariant(&parent, new_mount, ok ? err : cleanup, ok ? err_cap : sizeof(cleanup))) {
		if (!ok) sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.SHARED: parent source cleanup: %s", cleanup);
		ok = 0;
	}
	if (ok) {
		for (size_t i = 0; i < before.package_count; i++) before.packages[i].path[0] = '\0';
		gridMatchSourcesFree(out);
		*out = before;
	} else gridMatchSourcesFree(&before);
	return ok;
}

s32 forgeLevelSaveSharedVariant(const char *arena_id, const char *parent_sha, forge_level_doc_t *doc,
		const char *display_name, const grid_match_sources_t *reviewed,
		const u16 *omitted_packages, size_t omitted_count, const forge_level_placement_fallback_t *fallbacks,
		size_t fallback_count, mod_author_result_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!display_name || !display_name[0] || !reviewed || !out || omitted_count > MANIFEST_MAX_ENTRIES
			|| (omitted_count && !omitted_packages) || (fallback_count && !fallbacks)
			|| fallback_count > (size_t)forgeLevelDocObjectCount(doc)) {
		levelFail(err, err_cap, "review the shared draft's sources and choose a new variant name", "");
		return 0;
	}
	level_played_variant_t parent = {0};
	grid_match_sources_t before = {0}, after = {0};
	forge_level_doc_t *draft = NULL;
	mod_author_result_t *target = calloc(1, sizeof(*target));
	forge_level_overrides_t *overrides = calloc(1, sizeof(*overrides));
	u8 omitted[MANIFEST_MAX_ENTRIES] = {0};
	char cleanup[192];
	s32 new_mount = 0, ok = 0;
	if (!target || !overrides) {
		levelFail(err, err_cap, "could not allocate the shared variant", "");
		goto done;
	}
	if (!levelPrepareSharedVariant(arena_id, parent_sha, doc, &parent, &before, &new_mount, err, err_cap)) goto done;
	if (strcmp(before.digest, reviewed->digest) != 0) {
		levelFail(err, err_cap, "the shared draft's source packages changed", "review the sources again");
		goto done;
	}
	if (!levelVariantCheckOmissions(&before, omitted_packages, omitted_count, fallbacks, fallback_count,
			arena_id, omitted, err, err_cap)) goto done;
	draft = forgeLevelDocCloneWithPlacementFallbacks(doc, fallbacks, fallback_count, err, err_cap);
	if (!draft || !levelCollectVariantSources(&parent.review, parent.scenario, parent.scenario_size, draft,
			&after, err, err_cap) || !levelVariantCheckRemainingSources(&before, &after, omitted, arena_id,
				parent.review.pinned.digest, NULL, err, err_cap)) goto done;

	forge_level_source_t source = {0};
	if (!levelForkSource(&parent.review.revision, &source, err, err_cap)
			|| !levelAllOverrides(draft, &source, source.archive_path, overrides, err, err_cap)) goto done;
	u8 parent_id_hash[SHA256_DIGEST_SIZE];
	char parent_map[SHA256_HEX_SIZE], dependency_plan[SHA256_HEX_SIZE] = "";
	sha256Hash(source.arena_id, strlen(source.arena_id), parent_id_hash);
	sha256ToHex(parent_id_hash, parent_map);
	mod_author_descriptor_value_t fields[] = {
		{ GRID_ARENA_PROVENANCE_KEY, GRID_ARENA_PROVENANCE_ORIGINAL },
		{ FORGE_REVISION_PARENT_MAP_KEY, parent_map },
		{ FORGE_REVISION_PARENT_DIGEST_KEY, source.archive_sha256 },
		{ FORGE_REVISION_DEPENDENCY_PLAN_KEY, NULL },
	};
	const mod_author_request_t request = {
		.template_path = source.archive_path, .mods_root = modmgrGetModsDir(),
		.display_name = display_name, .tool = "forge-shared-fork", .require_new_folder = 1,
		.descriptor_values = fields, .descriptor_value_count = ARRAYCOUNT(fields),
		.overrides = overrides->entries, .override_count = overrides->count,
	};
	if (modAuthorPlan(&request, target, err, err_cap) != MOD_AUTHOR_OK) goto done;
	if (strcmp(target->catalog_id, source.arena_id) == 0 || assetCatalogResolveAny(target->catalog_id)
			|| modmgrFindMod(target->mod_id)) {
		levelFail(err, err_cap, "choose a distinct available name for the shared variant", target->catalog_id);
		goto done;
	}
	if (!levelStoreVariantDependencies(&after, dependency_plan, err, err_cap)) goto done;
	fields[3].value = dependency_plan[0] ? dependency_plan : NULL;
	if (!levelPublishCheckedVariantCopy(&request, target, &after, dependency_plan, err, err_cap)) goto done;
	*out = *target;
	ok = 1;
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.SHARED: saved original=%s parent=%s variant=%s plan=%s omitted=%u fallbacks=%u",
		source.arena_id, source.archive_sha256, target->catalog_id, dependency_plan[0] ? dependency_plan : "none",
		(unsigned)omitted_count, (unsigned)fallback_count);

done:
	if (!levelFinishSharedVariant(&parent, new_mount, cleanup, sizeof(cleanup))) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.SHARED: parent source cleanup: %s", cleanup);
		if (ok) levelFail(err, err_cap, "the variant was saved but the parent source mount still needs to retire", cleanup);
	}
	gridMatchSourcesFree(&after);
	gridMatchSourcesFree(&before);
	forgeLevelDocFree(draft);
	free(overrides);
	free(target);
	return ok;
}

s32 forgeLevelSavePlayedVariant(const grid_match_result_t *result, const char *display_name,
		const grid_match_sources_t *reviewed, const u16 *omitted_packages, size_t omitted_count,
		const forge_level_placement_fallback_t *fallbacks, size_t fallback_count,
		mod_author_result_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!display_name || !display_name[0] || !reviewed || omitted_count > MANIFEST_MAX_ENTRIES
			|| (omitted_count && !omitted_packages) || (fallback_count && !fallbacks)) {
		levelFail(err, err_cap, "review the played Grid sources and choose a new variant name", "");
		return 0;
	}
	level_played_variant_t played = {0};
	grid_match_sources_t before = {0}, after = {0};
	forge_level_doc_t *draft = NULL;
	mod_author_result_t *target = calloc(1, sizeof(*target));
	mod_author_result_t *candidate = calloc(1, sizeof(*candidate));
	forge_level_overrides_t *overrides = calloc(1, sizeof(*overrides));
	u8 omitted[MANIFEST_MAX_ENTRIES] = {0};
	char staging[MOD_AUTHOR_PATH_MAX] = "";
	s32 written = 0, published = 0, owns_staging = 0;
	if (!target || !candidate || !overrides) {
		levelFail(err, err_cap, "could not allocate the played variant", "");
		goto done;
	}
	if (!levelPreparePlayedVariant(result, &played, &before, err, err_cap)) goto done;
	if (memcmp(before.digest, reviewed->digest, sizeof(before.digest)) != 0
			|| fallback_count > (size_t)forgeLevelDocObjectCount(played.doc)) {
		levelFail(err, err_cap, "the played draft's source review changed", "review it again before saving");
		goto done;
	}
	if (!levelVariantCheckOmissions(&before, omitted_packages, omitted_count, fallbacks, fallback_count,
			result->arena_id, omitted, err, err_cap)) goto done;
	draft = forgeLevelDocCloneWithPlacementFallbacks(played.doc, fallbacks, fallback_count, err, err_cap);
	if (!draft || !levelCollectVariantSources(&played.review, played.scenario, played.scenario_size, draft,
			&after, err, err_cap)
			|| !levelVariantCheckRemainingSources(&before, &after, omitted, result->arena_id,
				result->arena_sha256, &played.temporary, err, err_cap)) goto done;

	const grid_match_source_package_t *parent_package = &played.match_sources->packages[played.arena_index];
	grid_match_session_file_t *parent_file = &played.temporary.files[played.temporary.count];
	if (!levelWriteVariantSource(parent_package, played.arena_bytes, played.arena_size,
			parent_file->path, err, err_cap)) goto done;
	parent_file->owner = parent_package->owner;
	parent_file->package_index = (u16)played.arena_index;
	played.temporary.count++;
	forge_level_source_t source = {0};
	if (!assetPathCopyChecked(source.archive_path, sizeof(source.archive_path), parent_file->path)
			|| !assetPathCopyChecked(source.scenario_member, sizeof(source.scenario_member), played.review.scenario_member)
			|| !assetPathCopyChecked(source.arena_id, sizeof(source.arena_id), result->arena_id)) {
		levelFail(err, err_cap, "the played source path is too long", result->arena_id);
		goto done;
	}
	memcpy(source.archive_sha256, played.review.revision.digest, sizeof(source.archive_sha256));
	if (!levelAllOverrides(draft, &source, source.archive_path, overrides, err, err_cap)) goto done;

	u8 parent_id_hash[SHA256_DIGEST_SIZE];
	char parent_map[SHA256_HEX_SIZE], dependency_plan[SHA256_HEX_SIZE] = "";
	sha256Hash(source.arena_id, strlen(source.arena_id), parent_id_hash);
	sha256ToHex(parent_id_hash, parent_map);
	mod_author_descriptor_value_t fields[] = {
		{ GRID_ARENA_PROVENANCE_KEY, GRID_ARENA_PROVENANCE_ORIGINAL },
		{ FORGE_REVISION_PARENT_MAP_KEY, parent_map },
		{ FORGE_REVISION_PARENT_DIGEST_KEY, source.archive_sha256 },
		{ FORGE_REVISION_DEPENDENCY_PLAN_KEY, NULL },
	};
	mod_author_request_t request = {
		.template_path = source.archive_path, .mods_root = modmgrGetModsDir(),
		.display_name = display_name, .tool = "forge-played-fork", .require_new_folder = 1,
		.descriptor_values = fields, .descriptor_value_count = ARRAYCOUNT(fields),
		.overrides = overrides->entries, .override_count = overrides->count,
	};
	if (modAuthorPlan(&request, target, err, err_cap) != MOD_AUTHOR_OK) goto done;
	if (strcmp(target->catalog_id, source.arena_id) == 0 || assetCatalogResolveAny(target->catalog_id)
			|| modmgrFindMod(target->mod_id)) {
		levelFail(err, err_cap, "choose a distinct available name for the played variant", target->catalog_id);
		goto done;
	}
	if (!levelStoreVariantDependencies(&after, dependency_plan, err, err_cap)) goto done;
	fields[3].value = dependency_plan[0] ? dependency_plan : NULL;

	static u32 serial;
	const int length = snprintf(staging, sizeof(staging), "%s/.gp-%llx-%u", request.mods_root,
		(unsigned long long)sysGetMicroseconds(), (unsigned)++serial);
	if (length <= 0 || (size_t)length >= sizeof(staging) || _mkdir(staging) != 0) {
		levelFail(err, err_cap, "could not create the played variant's private staging directory", "");
		goto done;
	}
	owns_staging = 1;
	request.mods_root = staging;
	written = modAuthorSave(&request, candidate, err, err_cap) == MOD_AUTHOR_OK;
	if (!written || !levelValidatePlayedVariantCandidate(candidate, &after, dependency_plan, err, err_cap)) goto done;
	char directory[FS_MAXPATH + 1], category[MOD_AUTHOR_PATH_MAX];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!levelPreservePlayedParent(&played, directory, err, err_cap)
			|| !levelRetirePlayedScratch(&played, err, err_cap)
			|| !forgeLevelCanReviewPlayedVariantSources(result)) goto done;
	if (!levelForkCategory(target->mod_dir, category, sizeof(category)) || !fsCreateDir(category)
			|| !MoveFileExA(candidate->mod_dir, target->mod_dir, MOVEFILE_WRITE_THROUGH)) {
		levelFail(err, err_cap, "could not publish the new played variant", target->mod_dir);
		goto done;
	}
	published = 1;
	target->ids = candidate->ids;
	if (out) *out = *target;
	forge_revision_t pin;
	char library_error[256] = "";
	if (!forgeRevisionPublishLocal(directory, target->catalog_id, target->archive_path, NULL, &pin,
			library_error, sizeof(library_error))
			|| !modmgrEnableAuthoredGridVariant(target->mod_dir, target->mod_id, pin.arena_id, pin.digest,
			library_error, sizeof(library_error))) {
		levelFail(err, err_cap, "the variant was saved but the local library could not refresh", library_error);
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.PLAYED: saved without library refresh: %s", library_error);
	}
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.PLAYED: saved original=%s parent=%s variant=%s plan=%s omitted=%u fallbacks=%u",
		source.arena_id, source.archive_sha256, target->catalog_id, dependency_plan[0] ? dependency_plan : "none",
		(unsigned)omitted_count, (unsigned)fallback_count);

done:
	if (written && !published) {
		remove(candidate->archive_path);
		remove(candidate->manifest_path);
		_rmdir(candidate->mod_dir);
	}
	if (owns_staging) {
		char category[MOD_AUTHOR_PATH_MAX];
		if (written && levelForkCategory(candidate->mod_dir, category, sizeof(category))) _rmdir(category);
		if (_rmdir(staging) != 0) sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.PLAYED: could not remove staging folder '%s'", staging);
	}
	levelClosePlayedVariant(&played);
	gridMatchSourcesFree(&after);
	gridMatchSourcesFree(&before);
	forgeLevelDocFree(draft);
	free(overrides);
	free(candidate);
	free(target);
	return published;
}

s32 forgeLevelSessionReviewMatchSources(grid_match_sources_t *plan, char *err, size_t err_cap)
{
	if (!s_Session.doc || !s_Session.saved_archive.arena_id[0] || !plan) {
		levelFail(err, err_cap, "save and pin the level before reviewing match sources", "");
		return 0;
	}
	grid_match_sources_t prepared = {0};
	if (!forgeLevelBuildPinnedMatchSources(s_Session.saved_archive.arena_id, NULL, 0,
			&prepared, err, err_cap)) return 0;

	static u64 generation;
	grid_match_offer_t offer = { .room_id = 0, .generation = ++generation };
	memcpy(offer.manifest_digest, prepared.digest, sizeof(offer.manifest_digest));
	grid_match_cache_t *cache = NULL;
	s32 ok = forgeLevelStageLocalMatchSources(&prepared, &offer, &cache, err, err_cap);
	gridMatchCacheReset(&cache);
	if (ok && sysArgCheck("--debug-grid-match-sources")) {
		ok = forgeLevelStageInstalledMatchSources(&prepared, &offer, &cache, err, err_cap);
		gridMatchCacheReset(&cache);
	}
	if (!ok) {
		gridMatchSourcesFree(&prepared);
		return 0;
	}
	sysLogPrintf(LOG_NOTE, "GRID.MATCH.CACHE: ready packages=%u digest=%s",
		(u32)prepared.package_count, prepared.digest);
	gridMatchSourcesFree(plan);
	*plan = prepared;
	return 1;
}

static s32 checkpointPath(char path[FS_MAXPATH + 1], char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!s_Session.doc || !s_Session.opened || !s_Session.source.archive_sha256[0]) {
		levelFail(err, err_cap, "open a level before using checkpoints", "");
		return 0;
	}
	if (!levelSourceStillMatches(&s_Session.source, err, err_cap)) return 0;
	u8 digest[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	char name[sizeof("$S/forge-recovery-.json") + SHA256_HEX_SIZE];
	sha256Hash(s_Session.source.arena_id, strlen(s_Session.source.arena_id), digest);
	sha256ToHex(digest, hex);
	snprintf(name, sizeof(name), "$S/forge-recovery-%s.json", hex);
	fsFullPath(name, path, FS_MAXPATH + 1);
	if (!fsPathIsAbsolute(path) || strlen(path) >= FS_MAXPATH) {
		levelFail(err, err_cap, "the profile checkpoint path is unavailable", "");
		return 0;
	}
	return 1;
}

s32 forgeLevelSessionWriteCheckpoint(char *err, size_t err_cap)
{
	char path[FS_MAXPATH + 1];
	save_atomic_file_t save;
	if (!checkpointPath(path, err, err_cap)) return 0;
	if (saveAtomicBegin(&save, path) != 0) {
		levelFail(err, err_cap, "the profile checkpoint could not be opened for writing", "");
		return 0;
	}
	if (!forgeLevelDocWriteCheckpoint(s_Session.doc, s_Session.source.archive_sha256,
			saveAtomicStream(&save), err, err_cap)) {
		saveAtomicAbort(&save);
		return 0;
	}
	if (saveAtomicCommit(&save) != 0) {
		levelFail(err, err_cap, "the checkpoint could not be committed", "");
		return 0;
	}
	return 1;
}

s32 forgeLevelSessionRestoreCheckpoint(char *err, size_t err_cap)
{
	char path[FS_MAXPATH + 1];
	if (!checkpointPath(path, err, err_cap)) return 0;
	FILE *stream = fopen(path, "rb");
	if (!stream) {
		levelFail(err, err_cap, "no checkpoint could be opened for this level", "");
		return 0;
	}
	long size = fseek(stream, 0, SEEK_END) == 0 ? ftell(stream) : -1;
	char *json = NULL;
	if (size > 0 && (size_t)size <= FORGE_LEVEL_CHECKPOINT_MAX_BYTES && fseek(stream, 0, SEEK_SET) == 0) {
		json = malloc((size_t)size);
	}
	s32 read = json && fread(json, 1, (size_t)size, stream) == (size_t)size
		&& fgetc(stream) == EOF && !ferror(stream);
	fclose(stream);
	if (!read) {
		free(json);
		levelFail(err, err_cap, "the checkpoint is invalid, too large or unreadable", "");
		return 0;
	}
	forge_level_doc_t *doc = forgeLevelDocReadCheckpoint(json, (size_t)size, s_Session.source.archive_sha256,
		s_Session.opened, err, err_cap);
	free(json);
	if (!doc) return 0;
	forgeLevelSessionEditBegin();
	if (!s_Undo.pending) {
		forgeLevelDocFree(doc);
		levelFail(err, err_cap, "out of memory before restoring the checkpoint", "");
		return 0;
	}
	forgeLevelDocFree(s_Session.doc);
	s_Session.doc = doc;
	forgeLevelSessionEditEnd(1);
	return 1;
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
	const s32 already_mounted = forgeLevelVariantDependenciesStage();
	char err[256];
	if (!forgeLevelMountVariantDependencies(id, err, sizeof(err))) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.START.FAIL: arena=%s dependency sources: %s", id, err);
		return 0;
	}

	/* A clean match setup on this level, as The Grid's Enter makes one. */
	matchConfigInit();
	snprintf(g_MatchConfig.stage_id, sizeof(g_MatchConfig.stage_id), "%s", id);
	g_MatchConfig.stagenum = (u8)stagenum;
	snprintf(g_MatchConfig.scenario_id, sizeof(g_MatchConfig.scenario_id), "%s", "base:combat");
	g_MatchConfig.timelimit = 0;
	g_MatchConfig.scorelimit = 0;

	started = canvas ? pdguiForgeStartSessionOnCanvas(stagenum) : pdguiForgeStartSessionOn(stagenum);
	if (!started && already_mounted < 0 && forgeLevelVariantDependenciesStage() == stagenum
			&& !forgeLevelRetireVariantDependencies(err, sizeof(err))) {
		sysLogPrintf(LOG_WARNING, "FORGE.LEVEL.START.FAIL: arena=%s dependency cleanup: %s", id, err);
	}
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

static s32 debugRepeatSessionSave(const char *name, const f32 position[3], const f32 look[3],
	mod_author_result_t *result, char *err, size_t err_cap)
{
	const forge_level_source_t original = s_Session.source;
	const forge_revision_t first = s_Session.saved_archive;
	const u32 catalog_generation = assetCatalogGetGeneration();
	const s32 stage = g_StageNum;
	if (!first.digest[0] || !strcmp(first.arena_id, original.arena_id)) {
		levelFail(err, err_cap, "the first save did not create a separate level", "");
		return 0;
	}
	const f32 next_position[3] = { position[0] + 16.0f, position[1], position[2] };
	forge_level_doc_t *doc = forgeLevelSessionDoc();
	const s32 spawns = forgeLevelDocSpawnCount(doc) + 1;
	forgeLevelSessionEditBegin();
	if (!s_Undo.pending) {
		levelFail(err, err_cap, "could not start the repeated save's edit", "");
		return 0;
	}
	const s32 changed = forgeLevelDocAddSpawn(doc, next_position, look, NULL, NULL, 0, err, err_cap);
	forgeLevelSessionEditEnd(changed);
	if (!changed || forgeLevelSessionSaveAs(name, result, err, err_cap) != MOD_AUTHOR_OK) return 0;

	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	forge_revision_t previous;
	const s32 preserved = forgeRevisionReadPrevious(directory, first.arena_id, &previous, NULL, 0)
		&& strcmp(previous.digest, first.digest) == 0;
	if (!preserved || strcmp(result->catalog_id, first.arena_id) != 0
			|| !modsRootsSameDir(result->archive_path, first.path)
			|| !s_Session.saved_archive.digest[0] || !strcmp(s_Session.saved_archive.digest, first.digest)
			|| strcmp(s_Session.source.arena_id, original.arena_id) != 0
			|| sha256VerifyFile(original.archive_path, original.archive_sha256) != 1
			|| stage != g_StageNum || catalog_generation != assetCatalogGetGeneration()
			|| forgeLevelSessionHasUnsavedChanges()) {
		levelFail(err, err_cap, "the repeated save changed its source, catalog, or predecessor", first.arena_id);
		return 0;
	}
	forge_level_source_t reopened_source;
	forge_level_doc_t *reopened = forgeLevelOpen(first.arena_id, &reopened_source, err, err_cap);
	const s32 matches = reopened && forgeLevelDocSpawnCount(reopened) == spawns
		&& strcmp(reopened_source.archive_sha256, s_Session.saved_archive.digest) == 0;
	if (reopened) forgeLevelDocFree(reopened);
	if (!matches) {
		levelFail(err, err_cap, "could not reopen the edited destination with both starts", first.arena_id);
		return 0;
	}
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.REPEAT_SAVE: source=%s target=%s spawns=%d predecessor=1 catalog_unchanged=1",
		original.arena_id, first.arena_id, spawns);
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
		if (!forgeLevelSessionUndo() || forgeLevelSessionHasUnsavedChanges()) {
			snprintf(err, sizeof(err), "undo did not restore the clean opened revision");
			goto done;
		}
		if (!forgeLevelSessionRedo() || !forgeLevelSessionHasUnsavedChanges()) {
			snprintf(err, sizeof(err), "redo did not restore the unsaved edit");
			goto done;
		}
		/* Undo and Redo replace the open document. */
		doc = forgeLevelSessionDoc();
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: after undo and redo the level has pads=%d spawns=%d objects=%d",
			forgeLevelDocPadCount(doc), forgeLevelDocSpawnCount(doc), forgeLevelDocObjectCount(doc));
	}
	if (forgeLevelSessionSaveAs(display_name, result, err, sizeof(err)) != MOD_AUTHOR_OK) goto done;
	if (forgeLevelSessionHasUnsavedChanges()
			|| (undo_redo && (!forgeLevelSessionUndo() || !forgeLevelSessionHasUnsavedChanges()
				|| !forgeLevelSessionRedo() || forgeLevelSessionHasUnsavedChanges()))) {
		snprintf(err, sizeof(err), "the save did not mark the exact undo/redo revision as clean");
		goto done;
	}
	if (!assetCatalogResolve(result->catalog_id)) {
		snprintf(err, sizeof(err), "%s was saved but is not in the catalog", result->catalog_id);
		goto done;
	}
	if (smokeHarnessIsActive() && sysArgCheck("--debug-grid-repeat-save")
			&& !debugRepeatSessionSave(display_name, position, look, result, err, sizeof(err))) goto done;
	if (undo_redo) {
		const s32 spawns = forgeLevelDocSpawnCount(forgeLevelSessionDoc());
		if (!forgeLevelSessionWriteCheckpoint(err, sizeof(err))
				|| !forgeLevelSessionOpen(arena_id, err, sizeof(err))
				|| !forgeLevelSessionRestoreCheckpoint(err, sizeof(err))) goto done;
		if (forgeLevelDocSpawnCount(forgeLevelSessionDoc()) != spawns
				|| !forgeLevelSessionHasUnsavedChanges()) {
			snprintf(err, sizeof(err), "checkpoint did not restore the unsaved placement");
			goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.CHECKPOINT: reopened and restored arena=%s spawns=%d",
			arena_id, spawns);
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

	if (smokeHarnessIsActive() && sysArgCheck("--debug-forge-level-logic-zone-crossing")) {
		const f32 half_size[3] = {96.0f, 200.0f, 400.0f};
		char volume[32], enter[32], leave[32];
		if (!forgeLevelDocFindPad(doc, "pad_0005", &pad) || strcmp(pad.room_ref, "room_2")) {
			snprintf(err, sizeof(err), "the zone crossing fixture needs the original room 2 pad");
			goto done;
		}
		/* Both pads remain available to the layout. Remove their starts so the
		 * player can only enter the box after beginning the walk. */
		if (!forgeLevelDocRemoveSpawn(doc, "spawn_0005", err, sizeof(err))
				|| !forgeLevelDocRemoveSpawn(doc, "spawn_0007", err, sizeof(err))
				|| !forgeLevelDocAddLogicZone(doc, pad.pad_ref, half_size, volume, sizeof(volume), err, sizeof(err))
				|| !forgeLevelDocAddLogicZoneEvent(doc, volume, FORGE_LEVEL_LOGIC_ZONE_ENTER, enter, sizeof(enter), err, sizeof(err))
				|| !forgeLevelDocAddLogicZoneEvent(doc, volume, FORGE_LEVEL_LOGIC_ZONE_LEAVE, leave, sizeof(leave), err, sizeof(err))) goto done;
		sysLogPrintf(LOG_NOTE,
			"SMOKE.GRID.ZONE: authored source=%s pad=%s room=%s volume=%s enter=%s leave=%s center=(%.1f,%.1f,%.1f)",
			arena_id, pad.pad_ref, pad.room_ref, volume, enter, leave, pad.position[0], pad.position[1], pad.position[2]);
		goto save;
	}

	if (smokeHarnessIsActive() && sysArgCheck("--debug-forge-level-room-brightness")) {
		if (!forgeLevelDocSetRoomBrightness(doc, 1, 48, 112, err, sizeof(err))) goto done;
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: authored room 1 brightness min=48 max=112");
		goto save;
	}

	if (sysArgCheck("--debug-forge-level-trigger")) {
		const f32 half_size[3] = { 2048.0f, 2048.0f, 2048.0f };
		if (!forgeLevelDocAddDoorTrigger(doc, "pad_0189", "setup_0030",
				half_size, err, sizeof(err))) goto done;
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: authored door trigger pad=pad_0189 door=setup_0030");
		goto save;
	}
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
		const char *vehicle_id = smokeHarnessIsActive() ? sysArgGetString("--debug-grid-level-vehicle") : NULL;
		const asset_entry_t *model = vehicle_id ? assetRuntimeVehiclePlacementModel(vehicle_id) : NULL;
		if (vehicle_id && !model) {
			snprintf(err, sizeof(err), "the hoverbike has no exact native vehicle source: %s", vehicle_id);
			goto done;
		}
		if (!forgeLevelDocAddHoverbike(doc, model ? model->id : "base:model_hovbike", place, pad.look,
				bike_id, sizeof(bike_id), err, sizeof(err))) goto done;
		if (model) sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: hoverbike source record=%s vehicle=%s model=%s",
			bike_id, vehicle_id, model->id);
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
		const s32 decoration = smokeHarnessIsActive() && sysArgCheck("--debug-forge-level-decoration");
		levelStepFrom(&pad, decoration ? 96.0f : 420.0f, place);
		const s32 added = decoration
			? forgeLevelDocAddDecoration(doc, prop_model_id, place, pad.look, prop_id, sizeof(prop_id), err, sizeof(err))
			: forgeLevelDocAddProp(doc, prop_model_id, place, pad.look, prop_id, sizeof(prop_id), err, sizeof(err));
		if (!added) goto done;
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: made %s %s drawn with %s at (%.0f, %.0f, %.0f)",
			decoration ? "decoration" : "prop", prop_id, prop_model_id, place[0], place[1], place[2]);
	}
	sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: the level now has pads=%d spawns=%d objects=%d",
		forgeLevelDocPadCount(doc), forgeLevelDocSpawnCount(doc), forgeLevelDocObjectCount(doc));

	if (sysArgCheck("--debug-forge-level-atmosphere")) {
		scenario_stage_atmosphere_t look = {
			.has_sky = 1, .sky_rgb = {15, 35, 75},
			.has_fog = 1, .fog_start = 120, .fog_end = 480,
			.has_ambient = 1, .ambient_light = 104,
			.primary_music = "base:song_sequence_b",
		};
		const char *music = smokeHarnessIsActive() ? sysArgGetString("--debug-grid-level-music") : NULL;
		if (music) {
			const asset_entry_t *source = assetCatalogResolve(music);
			if (!source || source->type != ASSET_AUDIO || source->ext.audio.category != AUDIO_CAT_MUSIC
					|| !assetPathCopyChecked(look.primary_music, sizeof(look.primary_music), source->id)) {
				snprintf(err, sizeof(err), "the Grid fixture music is not catalogued: %s", music);
				goto done;
			}
		}
		if (!forgeLevelDocSetAtmosphere(doc, &look, err, sizeof(err))) goto done;
		if (music) sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: authored primary music %s", look.primary_music);
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: authored stage atmosphere");
	}
	if (sysArgCheck("--debug-forge-level-hills")) {
		for (s32 i = 0; i < 2; i++) {
			forge_level_spawn_t spawn;
			if (!forgeLevelDocGetSpawn(doc, i, &spawn)) {
				snprintf(err, sizeof(err), "the fixture needs two different start pads");
				goto done;
			}
			if (!forgeLevelDocSetHill(doc, spawn.pad_ref, 1, err, sizeof(err))) goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: authored %d King of the Hill markers",
			forgeLevelDocHillCount(doc));
	}
	if (sysArgCheck("--debug-forge-level-cases")) {
		for (s32 base = 0; base < 4; base++) {
			forge_level_spawn_t home, respawn;
			if (!forgeLevelDocGetSpawn(doc, base, &home)
					|| !forgeLevelDocGetSpawn(doc, base + 4, &respawn)) {
				levelFail(err, sizeof(err), "the case fixture needs eight starts", "");
				goto done;
			}
			if (!forgeLevelDocSetCaseHome(doc, base, home.pad_ref, err, sizeof(err))
					|| !forgeLevelDocAddCaseRespawn(doc, base, respawn.pad_ref, err, sizeof(err))) goto done;
		}
		sysLogPrintf(LOG_NOTE, "FORGE.LEVEL.EDIT: authored 4 case homes and 4 team respawns");
	}
save:
	if (forgeLevelSessionSaveAs(display_name, result, err, sizeof(err)) != MOD_AUTHOR_OK) {
		goto done;
	}
	if (sysArgCheck("--debug-forge-level-pin")) {
		forge_revision_t revision;
		if (!forgeLevelSessionPinSavedRevision(&revision, err, sizeof(err))) goto done;
		if (sysArgCheck("--debug-forge-level-share")
				&& !forgeLevelSessionAllowMatchSharing(err, sizeof(err))) goto done;
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
