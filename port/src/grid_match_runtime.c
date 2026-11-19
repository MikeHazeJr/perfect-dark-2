#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "asset_runtime.h"
#include "assetprovider.h"
#include "assetcatalog_resolve.h"
#include "catalog_mgr_weapons.h"
#include "bss.h"
#include "constants.h"
#include "forge_level_io.h"
#include "forge_room_builder.h"
#include "fs.h"
#include "game/bg.h"
#include "game/grid_layout_actors.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/setuputils.h"
#include "game/stagetable.h"
#include "grid_archive_policy.h"
#include "grid_match_runtime.h"
#include "grid_match_package_io.h"
#include "lib/collision.h"
#include "lib/meshcollision.h"
#include "lib/meshraygrid.h"
#include "mission_graph_runner.h"
#include "modasset_compiler.h"
#include "modarchive.h"
#include "mod_author_runtime.h"
#include "net/grid_edit_room_net.h"
#include "net/net.h"
#include "net/netmanifest.h"
#include "scenario_room_domain.h"
#include "scenario_scene_renderer.h"
#include "scenario_source_runtime.h"
#include "scenario_stage_rules.h"
#include "smoke_harness.h"
#include "system.h"

typedef struct grid_match_installed_asset {
	grid_match_source_root_t asset;
	char descriptor[FS_MAXPATH];
	s32 bundled;
} grid_match_installed_asset_t;

typedef struct grid_match_scenario_probe {
	scenario_scene_renderer_probe_t scene_probe;
	s32 collision_triangles;
	s32 collision_max_room;
	s32 collision_has_flags;
	s32 authored_room_bounds;
	scenario_source_navigation_probe_t navigation_probe;
} grid_match_scenario_probe_t;

typedef struct grid_match_runtime {
	grid_match_runtime_identity_t identity;
	grid_match_cache_t *cache;
	grid_match_sources_t private_gameplay_sources;
	fs_stage_archive_t *archives;
	fs_stage_archive_set_t *prepared_archives;
	grid_match_installed_asset_t *assets;
	size_t archive_count;
	size_t asset_count;
	size_t fiesta_asset_indexes[NUM_MPWEAPONS];
	u8 fiesta_slots[NUM_MPWEAPONS];
	u8 fiesta_weapons[NUM_MPWEAPONS];
	s32 fiesta_count;
	size_t arena_package_index;
	size_t weapon_package_index;
	u32 catalog_generation;
	u64 layout_generation;
	char arena_stage_root[FS_MAXPATH + 1];
	char scenario_link[FS_MAXPATH];
	grid_match_scenario_probe_t scenario_probe;
	asset_catalog_arena_lease_t arena_lease;
	asset_catalog_grid_weapon_lease_t *weapon_lease;
	grid_match_session_sources_t session_sources;
} grid_match_runtime_t;

typedef struct grid_layout_decoration {
	grid_edit_move_decoration_t pose;
	u64 prop_generation;
	RoomNum room;
	f32 native_y;
} grid_layout_decoration_t;

struct grid_match_private_layout {
	grid_match_runtime_identity_t round;
	u32 catalog_generation;
	u32 round_epoch;
	u64 base_generation;
	u8 arena_sha256[SHA256_DIGEST_SIZE];
	struct colmesh collision;
	mesh_world_prepared_t *mesh;
	scenario_grid_tiles_t *tiles;
	bg_grid_room_layout_t *rooms;
	scenario_scene_prepared_t *scene;
	grid_layout_decoration_t *decorations;
	s32 decoration_count;
	s32 committed;
};

static grid_match_runtime_t *s_PendingMatch;
static grid_match_runtime_t *s_ActiveMatch;

static s32 runtimeValidateStageBindings(const grid_match_runtime_t *match, char *err, size_t err_cap);

static struct {
	asset_catalog_arena_lease_t lease;
	asset_catalog_grid_arena_file_policy_e file_policy;
	char path[FS_MAXPATH + 1];
	char arena_id[CATALOG_ID_LEN];
} s_RetiringArena;

static struct {
	asset_catalog_grid_weapon_lease_t *lease;
	char path[FS_MAXPATH + 1];
} s_RetiringWeapon;

static grid_match_session_sources_t s_RetiringSources;

static struct {
	grid_match_result_t result;
	void *bytes;
	u32 size;
	grid_match_sources_t sources;
	grid_match_reconnect_sources_t retained;
} s_ReturnedArena;

static s32 runtimeError(char *err, size_t cap, const char *reason, const char *id)
{
	if (err && cap) snprintf(err, cap, "%s%s%s", reason, id ? ": " : "", id ? id : "");
	return 0;
}

s32 gridMatchRuntimeRetryArenaLeaseRetirement(char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!s_RetiringArena.path[0]) return 1;
	if (!assetCatalogRetireGridArenaLease(&s_RetiringArena.lease, err, err_cap)) return 0;
	if (s_RetiringArena.file_policy == GRID_ARENA_LEASE_TEMPORARY_FILE) {
		if (remove(s_RetiringArena.path) != 0 && errno != ENOENT) {
			return runtimeError(err, err_cap, "could not remove the retired Grid arena file", s_RetiringArena.path);
		}
		sysLogPrintf(LOG_NOTE, "GRID.MATCH.LEASE: retired temporary arena=%s", s_RetiringArena.arena_id);
	} else {
		sysLogPrintf(LOG_NOTE, "GRID.MATCH.LEASE: retired pinned arena lease=%s", s_RetiringArena.arena_id);
	}
	memset(&s_RetiringArena, 0, sizeof(s_RetiringArena));
	return 1;
}

s32 gridMatchRuntimeRetireDetachedArenaLease(asset_catalog_arena_lease_t *lease, char *err, size_t err_cap)
{
	if (!lease || !lease->arena_id[0] || !lease->archive_path[0] || s_RetiringArena.path[0]) {
		return runtimeError(err, err_cap, "Grid arena lease has no available retirement owner", NULL);
	}
	if (lease->file_policy != GRID_ARENA_LEASE_TEMPORARY_FILE && lease->file_policy != GRID_ARENA_LEASE_PINNED_FILE) {
		return runtimeError(err, err_cap, "unknown Grid arena lease file policy", lease->arena_id);
	}
	s_RetiringArena.lease = *lease;
	/* Catalog retirement clears its lease before scratch deletion can be retried. */
	s_RetiringArena.file_policy = lease->file_policy;
	memcpy(s_RetiringArena.path, lease->archive_path, sizeof(s_RetiringArena.path));
	memcpy(s_RetiringArena.arena_id, lease->arena_id, sizeof(s_RetiringArena.arena_id));
	memset(lease, 0, sizeof(*lease));
	return gridMatchRuntimeRetryArenaLeaseRetirement(err, err_cap);
}

s32 gridMatchRuntimeRetryWeaponLeaseRetirement(char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!s_RetiringWeapon.path[0]) return 1;
	if (s_RetiringWeapon.lease && !assetCatalogRetireGridWeaponLease(&s_RetiringWeapon.lease, err, err_cap)) return 0;
	if (remove(s_RetiringWeapon.path) != 0 && errno != ENOENT) {
		return runtimeError(err, err_cap, "could not remove the retired Grid weapon file", s_RetiringWeapon.path);
	}
	s_RetiringWeapon.path[0] = '\0';
	sysLogPrintf(LOG_NOTE, "GRID.MATCH.LEASE: retired temporary weapon");
	return 1;
}

s32 gridMatchRuntimeReleaseWeaponStageRefs(char *err, size_t err_cap)
{
	const asset_catalog_grid_weapon_lease_t *lease = s_ActiveMatch && s_ActiveMatch->weapon_lease
		? s_ActiveMatch->weapon_lease : s_RetiringWeapon.lease;
	const asset_catalog_grid_source_set_t *set = s_ActiveMatch && s_ActiveMatch->session_sources.lease
		? s_ActiveMatch->session_sources.lease : s_RetiringSources.lease;
	return assetCatalogReleaseGridWeaponLeaseStageRefs(lease, err, err_cap)
		&& (!set || assetCatalogReleaseGridSourceSetStageRefs(set, err, err_cap));
}

s32 gridMatchRuntimeRetireDetachedWeaponLease(asset_catalog_grid_weapon_lease_t **lease,
	char *err, size_t err_cap)
{
	const char *path = lease && *lease ? assetCatalogGridWeaponLeasePath(*lease) : NULL;
	if (!path || s_RetiringWeapon.path[0]
			|| !assetPathCopyChecked(s_RetiringWeapon.path, sizeof(s_RetiringWeapon.path), path)) {
		return runtimeError(err, err_cap, "Grid weapon lease has no available retirement owner", NULL);
	}
	s_RetiringWeapon.lease = *lease;
	*lease = NULL;
	return gridMatchRuntimeRetryWeaponLeaseRetirement(err, err_cap);
}

static void runtimeRetireArena(grid_match_runtime_t *match)
{
	if (!match || !match->arena_lease.arena_id[0]) return;
	/* Ownership transfer allows only one leased arena. This cleanup owner
	 * therefore remains available even after freeing the match cache. */
	char err[256];
	if (!gridMatchRuntimeRetireDetachedArenaLease(&match->arena_lease, err, sizeof(err))) {
		sysLogPrintf(LOG_WARNING, "GRID.MATCH.LEASE: deferred retirement: %s", err);
	}
}

static void runtimeRetireWeapon(grid_match_runtime_t *match)
{
	if (!match || !match->weapon_lease) return;
	char err[256];
	if (!gridMatchRuntimeRetireDetachedWeaponLease(&match->weapon_lease, err, sizeof(err))) {
		sysLogPrintf(LOG_WARNING, "GRID.MATCH.LEASE: deferred weapon retirement: %s", err);
	}
}

s32 gridMatchRuntimeRetrySourceSetRetirement(char *err, size_t err_cap)
{
	if (s_RetiringSources.lease && !assetCatalogRetireGridSourceSet(&s_RetiringSources.lease, err, err_cap)) return 0;
	s32 complete = 1;
	for (size_t i = 0; i < s_RetiringSources.count; i++) {
		grid_match_session_file_t *file = &s_RetiringSources.files[i];
		if (!file->path[0]) continue;
		if (remove(file->path) != 0 && errno != ENOENT) {
			complete = 0;
			continue;
		}
		file->path[0] = '\0';
		if (file->owner.type == ASSET_ARENA) {
			sysLogPrintf(LOG_NOTE, "GRID.MATCH.LEASE: retired temporary arena=%s", file->owner.id);
		} else if (file->owner.type == ASSET_WEAPON) {
			sysLogPrintf(LOG_NOTE, "GRID.MATCH.LEASE: retired temporary weapon=%s", file->owner.id);
		} else {
			sysLogPrintf(LOG_NOTE, "GRID.MATCH.LEASE: retired temporary source=%s type=%u",
				file->owner.id, (unsigned)file->owner.type);
		}
	}
	if (!complete) return runtimeError(err, err_cap, "could not remove temporary Grid source files", NULL);
	free(s_RetiringSources.files);
	s_RetiringSources = (grid_match_session_sources_t){0};
	return 1;
}

s32 gridMatchRuntimeRetireDetachedSourceSet(grid_match_session_sources_t *sources, char *err, size_t err_cap)
{
	if (!sources) return runtimeError(err, err_cap, "no Grid source set to retire", NULL);
	if (!sources->lease && !sources->files) return 1;
	if (s_RetiringSources.files || s_RetiringSources.lease) {
		return runtimeError(err, err_cap, "another Grid source set is being retired", NULL);
	}
	s_RetiringSources = *sources;
	*sources = (grid_match_session_sources_t){0};
	return gridMatchRuntimeRetrySourceSetRetirement(err, err_cap);
}

static void runtimeRetireSources(grid_match_runtime_t *match)
{
	char err[256];
	if (match && !gridMatchRuntimeRetireDetachedSourceSet(&match->session_sources, err, sizeof(err))) {
		sysLogPrintf(LOG_WARNING, "GRID.MATCH.LEASE: deferred source retirement: %s", err);
	}
}

s32 gridMatchRuntimeIsIdle(void)
{
	return !s_ActiveMatch && !s_PendingMatch && !s_RetiringArena.path[0] && !s_RetiringWeapon.path[0]
		&& !s_RetiringSources.files && !s_RetiringSources.lease;
}

void gridMatchRuntimePollLeaseRetirement(u32 now_ms)
{
	static u32 last_attempt;
	if (gridMatchRuntimeIsIdle() || s_ActiveMatch || s_PendingMatch || (u32)(now_ms - last_attempt) < 1000u) return;
	last_attempt = now_ms;
	if (gridMatchRuntimeRetryArenaLeaseRetirement(NULL, 0) && gridMatchRuntimeRetryWeaponLeaseRetirement(NULL, 0)) {
		gridMatchRuntimeRetrySourceSetRetirement(NULL, 0);
	}
}

static void runtimeFree(grid_match_runtime_t *match)
{
	if (!match) return;
	fsReleasePreparedStageArchives(&match->prepared_archives);
	for (size_t i = 0; i < match->archive_count; i++) {
		free((void *)match->archives[i].root);
	}
	free(match->archives);
	free(match->assets);
	gridMatchSourcesFree(&match->private_gameplay_sources);
	gridMatchCacheReset(&match->cache);
	free(match);
}

static const asset_entry_t *runtimeStageArena(const grid_match_runtime_identity_t *identity)
{
	if (identity->stage <= 0 || !STAGE_IS_GAMEPLAY(identity->stage)) return NULL;
	const s32 index = stageGetIndex(identity->stage);
	const asset_entry_t *arena = assetCatalogResolve(identity->arena_id);
	const asset_entry_t *scenario = assetCatalogResolve(identity->scenario_id);
	if (index < 0 || !arena || !arena->enabled || arena->type != ASSET_ARENA
			|| arena->ext.arena.stagenum != identity->stage
			|| arena->ext.arena.load_mode != ARENA_LOADMODE_PLAYABLE
			|| strcmp(arena->ext.arena.scenario_id, identity->scenario_id) != 0
			|| !scenario || !scenario->enabled || scenario->type != ASSET_SCENARIO
			|| scenario->ext.scenario.stagenum != identity->stage) {
		return NULL;
	}

	/* Setup/BG and world activation resolve the stage by different routes. */
	catalog_stage_result_t setup_stage, world_stage;
	const asset_entry_t *mod_map = assetCatalogFindModMapByStagenum(identity->stage);
	const char *world_id = mod_map ? mod_map->id : catalogStageIdByStagenum(identity->stage);
	if (!world_id || !catalogGetStageResultByIndexChecked(index, &setup_stage)
			|| !catalogResolveStage(world_id, &world_stage)
			|| scenarioSourceFindEntryForStage(&setup_stage, 1) != scenario
			|| scenarioSourceFindEntryForStage(&world_stage, 1) != scenario) {
		return NULL;
	}
	return arena;
}

static s32 runtimeAddArchive(grid_match_runtime_t *match, const char *path,
	const void *bytes, u32 size)
{
	for (size_t i = 0; i < match->archive_count; i++) {
		const fs_stage_archive_t *archive = &match->archives[i];
		if (strcmp(archive->root, path) == 0) {
			return archive->bytes == bytes && archive->size == size;
		}
	}
	size_t length = strlen(path) + 1;
	char *copy = malloc(length);
	if (!copy) return 0;
	memcpy(copy, path, length);
	fs_stage_archive_t *archive = &match->archives[match->archive_count++];
	archive->root = copy;
	archive->bytes = bytes;
	archive->size = size;
	return 1;
}

static s32 runtimeHasBoundArchive(const grid_match_runtime_t *match, const char *path,
	const void *bytes, u32 size)
{
	if (!bytes) return 0;
	for (size_t i = 0; i < match->archive_count; i++) {
		const fs_stage_archive_t *archive = &match->archives[i];
		if (strcmp(archive->root, path) == 0 && archive->bytes == bytes && archive->size == size) {
			return 1;
		}
	}
	return 0;
}

static s32 runtimeBindRoot(grid_match_runtime_t *match, const char *root,
	const grid_match_source_package_t *package, const void *bytes, u32 size)
{
	char absolute[FS_MAXPATH + 1];
	if (!root[0] || assetArchiveTypeForPath(root) != package->owner.type) return 0;
	fsFullPath(root, absolute, sizeof(absolute));
	if (!fsPathIsAbsolute(absolute) || strlen(absolute) >= FS_MAXPATH
			|| assetArchiveTypeForPath(absolute) != package->owner.type) return 0;

	/* Check the installed native owner once per path, bypassing any active
	 * stage mount. Its locator can name a typed member of a .pdmod. */
	for (size_t i = 0; i < match->archive_count; i++) {
		const fs_stage_archive_t *archive = &match->archives[i];
		if (strcmp(archive->root, absolute) == 0) {
			return archive->bytes == bytes && archive->size == size
				&& runtimeAddArchive(match, root, bytes, size);
		}
	}
	u32 installed_size = 0;
	void *installed = gridMatchPackageLoad(absolute, &installed_size);
	if (!installed) return 0;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(installed, installed_size, digest);
	free(installed);
	return installed_size == size && memcmp(digest, package->digest, sizeof(digest)) == 0
		&& runtimeAddArchive(match, absolute, bytes, size)
		&& runtimeAddArchive(match, root, bytes, size);
}

static s32 runtimeProviderDescriptor(const asset_entry_t *entry, const char *path,
		char *out, size_t capacity)
{
	char root[FS_MAXPATH];
	if (!path || !assetPathCopyChecked(root, sizeof(root), path)) return 0;
	for (;;) {
		const asset_type_e type = assetArchiveTypeForPath(root);
		const s32 matches = type == entry->type || (type == ASSET_AUDIO
			&& (entry->type == ASSET_MUSIC || entry->type == ASSET_SFX));
		const char *descriptor = matches ? assetArchiveDescriptorForPath(root) : NULL;
		if (descriptor) return assetPathJoinChecked(out, capacity, root, "::", descriptor);
		char *last = NULL;
		for (char *part = strstr(root, "::"); part; part = strstr(part + 2, "::")) last = part;
		if (!last) return 0;
		*last = '\0';
	}
}

s32 gridMatchCatalogArchiveDescriptor(const asset_entry_t *entry, char *out, size_t capacity)
{
	if (!entry || !out || !capacity) return 0;
	const char *provider_path = fileProviderPath(catalogEffectiveHandle(entry));
	/* A body, head or weapon can register a bundled mesh through an
	 * embedded copy while the model loader uses its standalone provider.
	 * Pin the mesh and its materials at the same typed source the loader
	 * reads. Source inspection still verifies the archive's catalog ID. */
	if (entry->type == ASSET_MODEL && runtimeProviderDescriptor(entry, provider_path, out, capacity)) {
		return 1;
	}
	if (strstr(entry->descriptor_path, "::")) {
		return assetPathCopyChecked(out, capacity, entry->descriptor_path);
	}
	/* Some bundled families bind a provider or hydrated member before the
	 * catalog's scanner descriptor is populated. Infer the descriptor only
	 * from a typed owner of the same family. The caller validates the ID. */
	if (runtimeProviderDescriptor(entry, provider_path, out, capacity)) return 1;
	const asset_runtime_binding_t *binding = assetRuntimeFindByTypeAndId(entry->type, entry->id);
	if (binding && binding->active && binding->source_hydrated
			&& runtimeProviderDescriptor(entry, binding->primary_path, out, capacity)) return 1;
	if (!entry->bundled) return 0;
	char root[FS_MAXPATH];
	return modAuthorRuntimeTemplatePath(entry->id, root, sizeof(root), NULL, 0)
		&& runtimeProviderDescriptor(entry, root, out, capacity);
}

static s32 runtimeBindAsset(grid_match_runtime_t *match, const grid_match_sources_t *plan,
	const grid_match_cache_t *cache, size_t index)
{
	const grid_match_source_entry_t *source = &plan->entries[index];
	const grid_match_source_package_t *package = &plan->packages[source->package_index];
	const asset_entry_t *entry = assetCatalogResolve(source->asset.id);
	if (!entry || !entry->enabled || entry->type != source->asset.type
			|| (entry->bundled != 0) != package->bundled) return 0;

	char path[FS_MAXPATH];
	if (!gridMatchCatalogArchiveDescriptor(entry, path, sizeof(path))) return 0;
	char root[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	const char *member;
	if (!gridMatchPackageLocation(path, root, sizeof(root), chain, sizeof(chain), &member)) return 0;
	const char *leaf = assetArchiveDescriptorForPath(chain[0] ? chain : package->suffix);
	if (!leaf || strcmp(member, leaf) != 0 || strcmp(chain, source->member_chain) != 0) return 0;
	u32 size = 0;
	const void *bytes = gridMatchCacheRead(cache, &match->identity.offer, source->package_index, &size);
	grid_match_installed_asset_t *installed = &match->assets[index];
	if (!bytes || size != package->size
			|| !runtimeBindRoot(match, root, package, bytes, size)
			|| !assetPathCopyChecked(installed->descriptor, sizeof(installed->descriptor), path)) {
		return 0;
	}
	installed->asset = source->asset;
	installed->bundled = package->bundled;
	return 1;
}

/* Bind a typed source to both local weapon domains. The MP slot supplies
 * ammo and setup metadata while the gameplay weapon number supplies the gun. */
s32 gridMatchResolveCatalogSpawnWeapon(const char *id, s32 *slot, s32 *weapon)
{
	const asset_entry_t *entry = assetCatalogResolve(id);
	if (!entry || !entry->occupied || !entry->enabled || entry->type != ASSET_WEAPON
			|| strcmp(entry->id, id) != 0) return 0;
	const s32 mp_slot = entry->mp_index;
	if (mp_slot <= MPWEAPON_NONE || mp_slot >= NUM_MPWEAPONS || mp_slot == MPWEAPON_SHIELD
			|| mp_slot == MPWEAPON_DISABLED || entry->ext.weapon.weapon_id != mp_slot) return 0;
	s32 weapon_num = 0;
	if (!catalogGetMpWeaponNumChecked(mp_slot, &weapon_num) || weapon_num != entry->runtime_index
			|| weapon_num <= WEAPON_UNARMED || weapon_num >= CATALOG_MGR_WEAPON_COUNT
			|| weapon_num == WEAPON_DISABLED) return 0;
	*slot = mp_slot;
	*weapon = weapon_num;
	return 1;
}

static s32 runtimeAddFiestaWeapon(grid_match_runtime_t *match, size_t asset_index)
{
	const grid_match_installed_asset_t *asset = &match->assets[asset_index];
	s32 slot, weapon;
	if (asset->asset.type != ASSET_WEAPON || match->fiesta_count >= NUM_MPWEAPONS
			|| !gridMatchResolveCatalogSpawnWeapon(asset->asset.id, &slot, &weapon)) return 0;
	for (s32 i = 0; i < match->fiesta_count; i++) {
		if (match->fiesta_slots[i] == slot || match->fiesta_weapons[i] == weapon) return 0;
	}
	const s32 index = match->fiesta_count++;
	match->fiesta_asset_indexes[index] = asset_index;
	match->fiesta_slots[index] = (u8)slot;
	match->fiesta_weapons[index] = (u8)weapon;
	return 1;
}

static s32 runtimeFiestaBindingMatches(const grid_match_runtime_t *match, s32 index)
{
	const grid_match_installed_asset_t *asset = &match->assets[match->fiesta_asset_indexes[index]];
	s32 slot, weapon;
	return gridMatchResolveCatalogSpawnWeapon(asset->asset.id, &slot, &weapon)
		&& slot == match->fiesta_slots[index] && weapon == match->fiesta_weapons[index];
}

typedef struct grid_match_member_bound {
	const char *name;
	u32 size;
	u32 matches;
} grid_match_member_bound_t;

static s32 runtimeMeasureMember(const char *name, u32 size, void *opaque)
{
	grid_match_member_bound_t *member = opaque;
	if (strcmp(name, member->name) == 0) {
		member->size = size;
		++member->matches;
	}
	return 0;
}

static void *runtimeReadMemberChain(const void *bytes, u32 size, const char *chain,
	u32 max_member_size, u32 *out_size, s32 *out_absent)
{
	void *owned = NULL;
	*out_size = 0;
	if (out_absent) *out_absent = 0;
	for (const char *part = chain; part; ) {
		char name[FS_MAXPATH + 1];
		const char *separator = strstr(part, "::");
		const size_t length = separator ? (size_t)(separator - part) : strlen(part);
		if (!length || length >= sizeof(name)) break;
		memcpy(name, part, length);
		name[length] = '\0';
		grid_match_member_bound_t bound = { .name = name };
		if (modArchiveMemForEachEntry(bytes, size, runtimeMeasureMember, &bound) != MODARCHIVE_OK) break;
		if (!bound.matches && !separator && out_absent) *out_absent = 1;
		if (bound.matches != 1 || !bound.size || bound.size > max_member_size) break;
		u32 extracted_size = 0;
		void *extracted = modArchiveExtractMemAlloc(bytes, size, name, &extracted_size);
		free(owned);
		owned = extracted;
		if (!owned || extracted_size != bound.size) break;
		if (!separator) {
			*out_size = extracted_size;
			return owned;
		}
		bytes = owned;
		size = extracted_size;
		part = separator + 2;
	}
	free(owned);
	return NULL;
}

typedef struct grid_match_scenario_source {
	const grid_match_runtime_t *match;
	const char *arena_root;
	const char *scenario_chain;
	const void *arena;
	u32 arena_size;
	const void *scenario;
	u32 scenario_size;
	u32 max_bytes;
} grid_match_scenario_source_t;

static const char *runtimeScenarioMemberLeaf(const grid_match_scenario_source_t *source,
	const char *path)
{
	char descriptor[FS_MAXPATH + 1];
	char root[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	if (!assetPathSiblingMemberChecked(descriptor, sizeof(descriptor), path, "scenario.ini")
			|| !gridMatchPackageLocation(descriptor, root, sizeof(root), chain, sizeof(chain), NULL)
			|| strcmp(chain, source->scenario_chain) != 0
			|| (source->match
				? !runtimeHasBoundArchive(source->match, root, source->arena, source->arena_size)
				: strcmp(root, source->arena_root) != 0)) {
		return NULL;
	}
	const char *leaf = path;
	for (const char *part = strstr(path, "::"); part; part = strstr(part + 2, "::")) leaf = part + 2;
	if (!leaf[0] || assetPathIsAbsolute(leaf) || assetPathHasParentTraversal(leaf)
			|| strchr(leaf, '\\') || strchr(leaf, ':')) {
		return NULL;
	}
	return leaf;
}

static void *runtimeReadScenarioMember(const grid_match_scenario_source_t *source, const char *path,
	u32 *out_size, s32 *out_absent)
{
	*out_size = 0;
	if (out_absent) *out_absent = 0;
	const char *leaf = runtimeScenarioMemberLeaf(source, path);
	return leaf ? runtimeReadMemberChain(source->scenario, source->scenario_size, leaf,
		source->max_bytes, out_size, out_absent) : NULL;
}

static char *runtimeReadNavigationMember(void *ctx, const char *path, u32 *out_size)
{
	u32 length = 0;
	*out_size = 0;
	void *raw = runtimeReadScenarioMember(ctx, path, &length, NULL);
	if (!raw) return NULL;
	char *text = realloc(raw, (size_t)length + 1);
	if (!text) {
		free(raw);
		return NULL;
	}
	text[length] = '\0';
	*out_size = length;
	return text;
}

static s32 runtimePreflightCollision(const grid_match_scenario_source_t *source,
	const asset_entry_t *scenario, grid_match_scenario_probe_t *probe, struct colmesh *collision,
	char *err, size_t err_cap)
{
	const char *collision_path = scenario->ext.scenario.collision_file[0]
		? scenario->ext.scenario.collision_file : scenario->ext.scenario.scene_file;
	char flags_path[FS_MAXPATH + 1];
	if (!assetPathSiblingMemberChecked(flags_path, sizeof(flags_path), collision_path, "collision.flags.json")) {
		return runtimeError(err, err_cap, "the native collision source is outside the exact Grid scenario", scenario->id);
	}
	u32 source_size = 0;
	void *bytes = runtimeReadScenarioMember(source, collision_path, &source_size, NULL);
	if (!bytes) return runtimeError(err, err_cap, "the exact Grid scenario has no readable collision source", scenario->id);
	u32 flags_size = 0;
	s32 flags_absent = 0;
	void *flags = runtimeReadScenarioMember(source, flags_path, &flags_size, &flags_absent);
	const s32 valid = (flags || flags_absent)
		&& modAssetCompilerBuildColmeshFromBytes(collision_path, bytes, source_size,
			flags, flags_size, source->max_bytes, collision) == 1;
	free(bytes);
	free(flags);
	if (!valid) return runtimeError(err, err_cap, "the exact Grid scenario collision or its authored flags are invalid", scenario->id);

	s32 max_room = 0;
	for (s32 i = 0; i < collision->numtris; ++i) {
		if (collision->tris[i].roomnum > max_room) max_room = collision->tris[i].roomnum;
	}
	probe->collision_triangles = collision->numtris;
	probe->collision_max_room = max_room;
	probe->collision_has_flags = !flags_absent;
	return 1;
}

static s32 runtimePreflightRoomTable(const grid_match_scenario_source_t *source,
	const asset_entry_t *scenario, s32 room_count, const struct colmesh *collision,
	grid_match_scenario_probe_t *probe, char *err, size_t err_cap)
{
	char path[FS_MAXPATH + 1];
	if (!scenarioSourceRoomTablePath(scenario, path, sizeof(path))) {
		return runtimeError(err, err_cap, "the native room table is outside the exact Grid scenario", scenario->id);
	}
	u32 size = 0;
	s32 absent = 0;
	char *text = runtimeReadScenarioMember(source, path, &size, &absent);
	const s32 valid = text || absent
		? scenarioSourcePreflightRoomTable(text, size, room_count, collision,
			&probe->authored_room_bounds, err, err_cap)
		: runtimeError(err, err_cap, "the exact Grid scenario has an unreadable room table", scenario->id);
	free(text);
	return valid;
}

static s32 runtimeProbeOriginalScenario(grid_match_scenario_source_t *source,
	const asset_entry_t *scenario, s32 room_extension, grid_match_scenario_probe_t *probe,
	char *err, size_t err_cap)
{
	struct colmesh collision = {0};
	if (!runtimePreflightCollision(source, scenario, probe, &collision, err, err_cap)) {
		meshFree(&collision);
		return 0;
	}
	char path[FS_MAXPATH + 1];
	u32 length = 0;
	char *text = scenarioSourcePortalPath(scenario, path, sizeof(path))
		? runtimeReadScenarioMember(source, path, &length, NULL) : NULL;
	s32 room_count = 0;
	s32 valid = text
		? scenarioSourcePreflightPortals(text, length, scenario->ext.scenario.source_room_count,
			probe->collision_max_room, &room_count, err, err_cap)
		: runtimeError(err, err_cap, "the exact Grid scenario has no readable portal table", scenario->id);
	free(text);
	if (valid) valid = runtimePreflightRoomTable(source, scenario, room_count, &collision, probe, err, err_cap);
	if (valid) {
		const char *scene_path = scenario->ext.scenario.scene_file;
		void *scene = runtimeReadScenarioMember(source, scene_path, &length, NULL);
		valid = scene && scenarioSceneRendererProbeGlbBytes(scenario->id, scene_path, scene, length, &probe->scene_probe);
		free(scene);
		if (!valid) runtimeError(err, err_cap, "the exact Grid scenario has no usable self-contained native scene GLB", scenario->id);
	}
	if (valid) {
		valid = scenarioSourcePreflightNavigation(scenario, runtimeReadNavigationMember, source,
			room_count, source->max_bytes, room_extension ? &collision : NULL,
			&probe->navigation_probe, err, err_cap);
	}
	meshFree(&collision);
	return valid;
}

static s32 runtimePreflightOriginalScenario(grid_match_runtime_t *match,
	const char *scenario_root, const char *scenario_chain, const void *arena, u32 size,
	char *err, size_t err_cap)
{
	const s32 original = gridArchiveOrigin(scenario_root, arena, size) == GRID_ARCHIVE_ORIGINAL;
	if (!original && !netGridEditPrivateLaunchMatches(match->identity.offer.room_id, match->identity.arena_id)) return 1;

	const asset_entry_t *scenario = assetCatalogResolve(match->identity.scenario_id);
	if (!scenario) {
		return !original || runtimeError(err, err_cap, "the exact Grid scenario is missing", match->identity.scenario_id);
	}
	u32 scenario_size = 0;
	void *native_scenario = runtimeReadMemberChain(arena, size, scenario_chain, NET_DISTRIB_MAX_COMP,
		&scenario_size, NULL);
	if (!native_scenario) {
		return !original || runtimeError(err, err_cap, "the exact Grid scenario could not be read", scenario->id);
	}
	grid_match_scenario_source_t source = {
		.match = match, .scenario_chain = scenario_chain, .arena = arena, .arena_size = size,
		.scenario = native_scenario, .scenario_size = scenario_size, .max_bytes = NET_DISTRIB_MAX_COMP,
	};
	s32 valid = 1;
	if (original) {
		valid = runtimeProbeOriginalScenario(&source, scenario, 0, &match->scenario_probe, err, err_cap);
	} else {
		/* For a local base-derived arena, only the decoration move needs this
		 * proof. Its native stage still owns room and geometry validation. */
		(void)scenarioSourcePreflightNavigation(scenario, runtimeReadNavigationMember, &source,
			SCENARIO_ROOM_DOMAIN_MAX_COUNT, source.max_bytes, NULL,
			&match->scenario_probe.navigation_probe, NULL, 0);
	}
	free(native_scenario);
	return valid;
}

static s32 runtimePreflightSavedArena(const char *locator, const void *bytes, u32 size,
	const char *arena_id, const char *expected_sha, s32 room_extension, u32 max_bytes, char *err, size_t err_cap)
{
	const u32 descriptor_max_bytes = 1024u * 1024u;
	if (err && err_cap) err[0] = '\0';
	if (!bytes || !size || size > max_bytes || !arena_id || !expected_sha
			|| assetArchiveTypeForPath(locator) != ASSET_ARENA) {
		return runtimeError(err, err_cap, "the saved variant has no exact native arena source", arena_id);
	}

	u8 digest[SHA256_DIGEST_SIZE];
	char sha[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, sha);
	if (strcmp(sha, expected_sha) != 0) {
		return runtimeError(err, err_cap, "the saved variant changed after staging", arena_id);
	}

	u32 length = 0;
	char *arena_descriptor = runtimeReadMemberChain(bytes, size, "arena.ini", descriptor_max_bytes,
		&length, NULL);
	asset_catalog_grid_arena_source_t arena_source;
	const s32 arena_valid = arena_descriptor && assetCatalogReadGridArenaSource(arena_descriptor, length,
		arena_id, &arena_source) && gridArchiveOrigin(locator, bytes, size) == GRID_ARCHIVE_ORIGINAL;
	free(arena_descriptor);
	if (!arena_valid) {
		return runtimeError(err, err_cap, "the saved variant has no valid original arena descriptor", arena_id);
	}

	char scenario_ref[FS_MAXPATH + 1], descriptor[FS_MAXPATH + 1];
	char root[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	if (!assetPathJoinChecked(scenario_ref, sizeof(scenario_ref), locator, "::", arena_source.scenario_member)
			|| !assetPathJoinChecked(descriptor, sizeof(descriptor), scenario_ref, "::", "scenario.ini")
			|| !gridMatchPackageLocation(descriptor, root, sizeof(root), chain, sizeof(chain), NULL)
			|| strcmp(root, locator) != 0 || strcmp(chain, arena_source.scenario_member) != 0) {
		return runtimeError(err, err_cap, "the saved variant does not bind its embedded Grid scenario", arena_id);
	}

	u32 scenario_size = 0;
	void *native_scenario = runtimeReadMemberChain(bytes, size, chain, max_bytes,
		&scenario_size, NULL);
	if (!native_scenario) {
		return runtimeError(err, err_cap, "the saved variant has no readable embedded Grid scenario", arena_id);
	}
	char *scenario_descriptor = runtimeReadMemberChain(native_scenario, scenario_size, "scenario.ini",
		descriptor_max_bytes, &length, NULL);
	asset_entry_t *scenario = malloc(sizeof(*scenario));
	grid_match_scenario_source_t source = {
		.arena_root = root, .scenario_chain = chain, .scenario = native_scenario, .scenario_size = scenario_size,
		.max_bytes = max_bytes,
	};
	grid_match_scenario_probe_t probe = {0};
	s32 valid;
	if (!scenario || !scenario_descriptor || !assetCatalogBindGridScenarioDescriptor(
			scenario_descriptor, length, scenario_ref, arena_source.scenario_id, scenario)) {
		valid = runtimeError(err, err_cap, "the saved variant has no valid embedded scenario descriptor", arena_id);
	} else {
		valid = runtimeProbeOriginalScenario(&source, scenario, room_extension, &probe, err, err_cap);
	}
	if (valid) {
		sysLogPrintf(LOG_NOTE, "GRID.MATCH.PREFLIGHT: detached arena=%s scenario=%s sha256=%s authored_room_bounds=%d",
			arena_id, scenario->id, sha, probe.authored_room_bounds);
		if (room_extension) {
			sysLogPrintf(LOG_NOTE, "GRID.MATCH.PREFLIGHT: extension walk arena=%s sha256=%s starts=%d links=%d",
				arena_id, sha, probe.navigation_probe.checked_starts, probe.navigation_probe.checked_links);
		}
	}
	free(scenario);
	free(scenario_descriptor);
	free(native_scenario);
	return valid;
}

s32 gridMatchRuntimePreflightSavedArena(const char *locator, const void *bytes, u32 size,
	const char *arena_id, const char *expected_sha, char *err, size_t err_cap)
{
	return runtimePreflightSavedArena(locator, bytes, size, arena_id, expected_sha, 0, NET_DISTRIB_MAX_COMP, err, err_cap);
}

s32 gridMatchRuntimePreflightLocalArena(const char *locator, const void *bytes, u32 size,
	const char *arena_id, const char *expected_sha, char *err, size_t err_cap)
{
	return runtimePreflightSavedArena(locator, bytes, size, arena_id, expected_sha, 0,
		128u * 1024u * 1024u, err, err_cap);
}

s32 gridMatchRuntimePreflightRoomExtension(const char *locator, const void *bytes, u32 size,
	const char *arena_id, const char *expected_sha, char *err, size_t err_cap)
{
	return runtimePreflightSavedArena(locator, bytes, size, arena_id, expected_sha, 1, NET_DISTRIB_MAX_COMP, err, err_cap);
}

s32 gridMatchRuntimePrepare(const grid_match_sources_t *plan, const grid_match_offer_t *offer,
	grid_match_cache_t **cache, const char *arena_id, s32 stage, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (s_PendingMatch || !plan || !offer || !cache || !arena_id || !arena_id[0]
			|| !gridMatchCacheReady(*cache, offer)) {
		return runtimeError(err, err_cap, "no complete Grid match cache or a stage is already pending", NULL);
	}
	grid_match_sources_t sealed = *plan;
	if (!gridMatchSourcesSeal(&sealed, err, err_cap)) return 0;
	if (sealed.total_bytes != plan->total_bytes
			|| memcmp(sealed.digest, plan->digest, SHA256_HEX_SIZE) != 0
			|| memcmp(sealed.digest, offer->manifest_digest, SHA256_HEX_SIZE) != 0) {
		return runtimeError(err, err_cap, "the staged cache does not match the plan", NULL);
	}

	const asset_entry_t *arena = assetCatalogResolve(arena_id);
	grid_match_runtime_identity_t identity = { .offer = *offer, .stage = stage };
	if (!arena || arena->type != ASSET_ARENA
			|| !assetPathCopyChecked(identity.arena_id, sizeof(identity.arena_id), arena_id)
			|| !assetPathCopyChecked(identity.scenario_id, sizeof(identity.scenario_id), arena->ext.arena.scenario_id)
			|| !runtimeStageArena(&identity)) {
		return runtimeError(err, err_cap, "the installed arena and scenario do not resolve to the offered stage", arena_id);
	}

	const grid_match_source_entry_t *arena_source = NULL, *scenario_source = NULL;
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *source = &plan->entries[i];
		if (source->asset.type == ASSET_ARENA && strcmp(source->asset.id, identity.arena_id) == 0) {
			arena_source = source;
		}
		if (source->asset.type == ASSET_SCENARIO && strcmp(source->asset.id, identity.scenario_id) == 0) {
			scenario_source = source;
		}
	}
	const char *scenario_link = arena->ext.arena.scenario_archive;
	char scenario_descriptor[FS_MAXPATH + 1], scenario_root[FS_MAXPATH + 1], scenario_chain[FS_MAXPATH + 1];
	if (!arena_source || arena_source->member_chain[0] || !scenario_source
			|| arena_source->package_index != scenario_source->package_index
			|| !scenario_source->member_chain[0]
			|| !assetPathJoinChecked(scenario_descriptor, sizeof(scenario_descriptor),
				scenario_link, "::", "scenario.ini")
			|| !gridMatchPackageLocation(scenario_descriptor, scenario_root, sizeof(scenario_root),
				scenario_chain, sizeof(scenario_chain), NULL)
			|| strcmp(scenario_chain, scenario_source->member_chain) != 0) {
		return runtimeError(err, err_cap, "the offer does not bind the arena's embedded scenario", arena_id);
	}
	memcpy(identity.arena_sha256, plan->packages[arena_source->package_index].digest,
		sizeof(identity.arena_sha256));

	grid_match_runtime_t *match = calloc(1, sizeof(*match));
	if (!match) return runtimeError(err, err_cap, "out of memory preparing Grid stage bindings", NULL);
	match->identity = identity;
	match->asset_count = plan->entry_count;
	match->arena_package_index = arena_source->package_index;
	match->catalog_generation = assetCatalogGetGeneration();
	match->archives = calloc(2 * (plan->entry_count + 1), sizeof(*match->archives));
	match->assets = calloc(match->asset_count, sizeof(*match->assets));
	if (!match->archives || !match->assets
			|| !assetPathCopyChecked(match->arena_stage_root, sizeof(match->arena_stage_root), scenario_root)
			|| !assetPathCopyChecked(match->scenario_link, sizeof(match->scenario_link), scenario_link)) {
		runtimeFree(match);
		return runtimeError(err, err_cap, "could not prepare Grid stage bindings", NULL);
	}
	for (size_t i = 0; i < match->asset_count; i++) {
		if (!runtimeBindAsset(match, plan, *cache, i)) {
			runtimeFree(match);
			return runtimeError(err, err_cap, "installed asset is absent, changed, or bound to another archive", plan->entries[i].asset.id);
		}
		if (plan->entries[i].fiesta_pool && !runtimeAddFiestaWeapon(match, i)) {
			runtimeFree(match);
			return runtimeError(err, err_cap, "the sealed Grid Fiesta weapon has no distinct local binding", plan->entries[i].asset.id);
		}
	}
	u32 size = 0;
	const size_t package_index = arena_source->package_index;
	const void *bytes = gridMatchCacheRead(*cache, offer, package_index, &size);
	if (!bytes || !runtimeBindRoot(match, scenario_root, &plan->packages[package_index], bytes, size)) {
		runtimeFree(match);
		return runtimeError(err, err_cap, "installed arena points at a different scenario archive", arena_id);
	}
	if (!runtimePreflightOriginalScenario(match, scenario_root, scenario_chain, bytes, size, err, err_cap)) {
		runtimeFree(match);
		return 0;
	}
	if (!fsPrepareStageArchives(&match->prepared_archives, match->archives, match->archive_count)) {
		runtimeFree(match);
		return runtimeError(err, err_cap, "could not prepare private Grid stage sources", arena_id);
	}
	if (match->catalog_generation != assetCatalogGetGeneration()) {
		runtimeFree(match);
		return runtimeError(err, err_cap, "the catalog changed while preparing Grid stage sources", NULL);
	}
	const grid_match_scenario_probe_t *probe = &match->scenario_probe;
	if (probe->scene_probe.geometry_sha256[0]) {
		sysLogPrintf(LOG_NOTE, "GRID.MATCH.PREFLIGHT: collision arena=%s triangles=%d max_room=%d flags=%s",
			arena_id, probe->collision_triangles, probe->collision_max_room,
			probe->collision_has_flags ? "authored" : "normal-fallback");
		sysLogPrintf(LOG_NOTE, "GRID.MATCH.PREFLIGHT: scene arena=%s geometry_sha256=%s vertices=%zu groups=%zu",
			arena_id, probe->scene_probe.geometry_sha256, probe->scene_probe.vertices, probe->scene_probe.groups);
		const scenario_source_navigation_probe_t *nav = &probe->navigation_probe;
		char digest[SHA256_HEX_SIZE];
		sha256ToHex(nav->digest, digest);
		sysLogPrintf(LOG_NOTE,
			"GRID.MATCH.PREFLIGHT: navigation arena=%s pads=%d waypoints=%d waygroups=%d covers=%d paths=%d bytes=%d sha256=%s spawns=%d",
			arena_id, nav->pads, nav->waypoints, nav->waygroups, nav->covers, nav->paths, nav->bytes, digest, nav->spawns);
	}
	match->cache = *cache;
	*cache = NULL;
	s_PendingMatch = match;
	return 1;
}

const grid_match_runtime_identity_t *gridMatchRuntimePending(void)
{
	return s_PendingMatch ? &s_PendingMatch->identity : NULL;
}

const grid_match_runtime_identity_t *gridMatchRuntimeActive(void)
{
	return s_ActiveMatch ? &s_ActiveMatch->identity : NULL;
}

s32 gridMatchRuntimeDecorationPadIndependent(s32 pad)
{
	const grid_match_runtime_t *match = s_ActiveMatch;
	if (!match || g_NetMode != NETMODE_SERVER || !g_NetStageEpoch
			|| match->identity.stage != g_Vars.stagenum) return 0;
	const scenario_source_navigation_probe_t *nav = &match->scenario_probe.navigation_probe;
	return nav->bytes > 0 && pad >= 0 && pad < nav->pads && pad < (s32)SCENARIO_SOURCE_NATIVE_PAD_COUNT
		&& !(nav->protected_pads[pad / 8] & (1u << (pad % 8)));
}

s32 gridMatchRuntimeCopyActiveSources(grid_match_sources_t *out, char *err, size_t err_cap)
{
	if (!s_ActiveMatch) return runtimeError(err, err_cap, "no active Grid match", NULL);
	return gridMatchCacheCopySources(s_ActiveMatch->cache, &s_ActiveMatch->identity.offer,
		out, err, err_cap);
}

s32 gridMatchRuntimeOwnPrivateGameplaySources(const grid_match_offer_t *pending_offer,
	grid_match_sources_t *sources, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	grid_match_runtime_t *match = s_PendingMatch;
	if (!match || match->private_gameplay_sources.entries || !sources
			|| match->catalog_generation != assetCatalogGetGeneration()
			|| !gridMatchCacheReady(match->cache, pending_offer)) {
		return runtimeError(err, err_cap, "no matching pending Grid start can own the gameplay sources", NULL);
	}

	grid_match_sources_t whole = {0};
	if (!gridMatchCacheCopySources(match->cache, pending_offer, &whole, err, err_cap)) return 0;
	const s32 exact = gridMatchSourcesExactSubset(sources, &whole, err, err_cap);
	gridMatchSourcesFree(&whole);
	if (!exact) return 0;
	if (match != s_PendingMatch || match->catalog_generation != assetCatalogGetGeneration()) {
		return runtimeError(err, err_cap, "the pending Grid start changed while checking gameplay sources", NULL);
	}

	match->private_gameplay_sources = *sources;
	memset(sources, 0, sizeof(*sources));
	return 1;
}

static s32 runtimeSameOffer(const grid_match_offer_t *first, const grid_match_offer_t *second)
{
	return first && second && first->room_id == second->room_id && first->generation == second->generation
		&& memcmp(first->manifest_digest, second->manifest_digest, SHA256_HEX_SIZE) == 0;
}

static const grid_match_runtime_t *runtimeMatchForOffer(const grid_match_offer_t *offer)
{
	const grid_match_runtime_t *match = s_PendingMatch;
	if (!match || !runtimeSameOffer(&match->identity.offer, offer)) match = s_ActiveMatch;
	return match && runtimeSameOffer(&match->identity.offer, offer) ? match : NULL;
}

s32 gridMatchRuntimeResolveSpawnWeapon(const grid_match_offer_t *offer, const char *id, s32 *weapon)
{
	const grid_match_runtime_t *match = runtimeMatchForOffer(offer);
	if (!match || !id || !id[0] || !weapon) return 0;
	for (size_t i = 0; i < match->asset_count; i++) {
		const grid_match_source_root_t *asset = &match->assets[i].asset;
		if (asset->type == ASSET_WEAPON && strcmp(asset->id, id) == 0) {
			s32 slot;
			return gridMatchResolveCatalogSpawnWeapon(id, &slot, weapon);
		}
	}
	return 0;
}

s32 gridMatchRuntimeValidateActiveSpawnWeapon(const char *id, s32 weapon, char *err, size_t err_cap)
{
	const grid_match_runtime_identity_t *active = gridMatchRuntimeActive();
	s32 resolved;
	if (err && err_cap) err[0] = '\0';
	if (!active || !gridMatchRuntimeResolveSpawnWeapon(&active->offer, id, &resolved)
			|| resolved != weapon || !catalogManagerWeaponIsUsable(resolved)) {
		return runtimeError(err, err_cap, "the exact Grid spawn weapon is no longer usable", id);
	}
	return 1;
}

s32 gridMatchRuntimeFiestaMatches(const grid_match_offer_t *offer, s32 fiesta)
{
	const grid_match_runtime_t *match = runtimeMatchForOffer(offer);
	return match && (match->fiesta_count > 0) == (fiesta != 0);
}

s32 gridMatchRuntimeValidateActiveFiesta(char *err, size_t err_cap)
{
	const grid_match_runtime_t *match = s_ActiveMatch;
	if (err && err_cap) err[0] = '\0';
	if (!match) return runtimeError(err, err_cap, "no active Grid match for Fiesta validation", NULL);
	for (s32 i = 0; i < match->fiesta_count; i++) {
		if (!runtimeFiestaBindingMatches(match, i)
				|| !catalogManagerWeaponIsUsable(match->fiesta_weapons[i])) {
			const grid_match_installed_asset_t *asset = &match->assets[match->fiesta_asset_indexes[i]];
			return runtimeError(err, err_cap, "an active Grid Fiesta weapon is no longer usable", asset->asset.id);
		}
	}
	return 1;
}

s32 gridMatchRuntimeFiestaSlots(const u8 **slots)
{
	if (slots) *slots = NULL;
	if (!gridMatchRuntimeValidateActiveFiesta(NULL, 0)) return 0;
	const grid_match_runtime_t *match = s_ActiveMatch;
	if (slots && match->fiesta_count > 0) *slots = match->fiesta_slots;
	return match->fiesta_count;
}

s32 gridMatchRuntimeFiestaActive(void)
{
	return s_ActiveMatch && s_ActiveMatch->fiesta_count > 0;
}

const char *gridMatchRuntimeFiestaWeaponId(s32 weapon, s32 *out_slot)
{
	const grid_match_runtime_t *match = s_ActiveMatch;
	if (!match || !catalogManagerWeaponIsUsable(weapon)) return NULL;
	for (s32 i = 0; i < match->fiesta_count; i++) {
		if (match->fiesta_weapons[i] == weapon && runtimeFiestaBindingMatches(match, i)) {
			if (out_slot) *out_slot = match->fiesta_slots[i];
			return match->assets[match->fiesta_asset_indexes[i]].asset.id;
		}
	}
	return NULL;
}

void gridMatchRuntimeTraceFiestaGrant(const char *actor, s32 mp_slot, s32 weapon)
{
	const grid_match_runtime_t *match = s_ActiveMatch;
	if (!smokeHarnessIsActive() || !sysArgCheck("--debug-grid-fiesta") || !match || !match->fiesta_count
			|| !actor || (strcmp(actor, "player") != 0 && strcmp(actor, "bot") != 0)) return;

	for (s32 i = 0; i < match->fiesta_count; i++) {
		if (match->fiesta_slots[i] == mp_slot && match->fiesta_weapons[i] == weapon
				&& runtimeFiestaBindingMatches(match, i) && catalogManagerWeaponIsUsable(weapon)) {
			const grid_match_installed_asset_t *asset = &match->assets[match->fiesta_asset_indexes[i]];
			sysLogPrintf(LOG_NOTE, "GRID.FIESTA.GRANT: actor=%s id=%s slot=%d weapon=%d digest=%s",
				actor, asset->asset.id, mp_slot, weapon, match->identity.offer.manifest_digest);
			return;
		}
	}
	sysLogPrintf(LOG_WARNING, "GRID.FIESTA.GRANT: outside sealed pool actor=%s slot=%d weapon=%d digest=%s",
		actor, mp_slot, weapon, match->identity.offer.manifest_digest);
}

static s32 runtimePrivateCandidateActive(const grid_match_runtime_t *match, const grid_match_offer_t *offer)
{
	if (!match || match != s_ActiveMatch || s_PendingMatch || !match->private_gameplay_sources.entries
			|| match->catalog_generation != assetCatalogGetGeneration()
			|| !runtimeSameOffer(&match->identity.offer, offer) || !gridMatchCacheReady(match->cache, offer)) return 0;
	const asset_entry_t *arena = runtimeStageArena(&match->identity);
	return arena && strcmp(arena->ext.arena.scenario_archive, match->scenario_link) == 0;
}

/* The temporary arena scanner admits exactly these two logical catalog rows.
 * Installed dependencies use their own packages and ordinary provider paths. */
static const grid_match_source_entry_t *runtimePrivateArenaScenario(const grid_match_sources_t *plan,
	const char *arena_id, const u8 arena_sha256[SHA256_DIGEST_SIZE])
{
	const grid_match_source_entry_t *arena = NULL;
	for (size_t i = 0; i < plan->entry_count; ++i) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (entry->asset.type == ASSET_ARENA && strcmp(entry->asset.id, arena_id) == 0) {
			arena = entry;
			break;
		}
	}
	if (!arena || arena->member_chain[0] || arena->package_index >= plan->package_count) return NULL;
	const grid_match_source_package_t *package = &plan->packages[arena->package_index];
	if (package->bundled || package->owner.type != ASSET_ARENA || strcmp(package->owner.id, arena_id) != 0
			|| strcmp(package->suffix, ".pdarena") != 0
			|| memcmp(package->digest, arena_sha256, SHA256_DIGEST_SIZE) != 0) return NULL;

	const grid_match_source_entry_t *scenario = NULL;
	for (size_t i = 0; i < plan->entry_count; ++i) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (entry == arena || entry->package_index != arena->package_index) continue;
		if (scenario || entry->asset.type != ASSET_SCENARIO || !entry->member_chain[0]
				|| strstr(entry->member_chain, "::") || assetArchiveTypeForPath(entry->member_chain) != ASSET_SCENARIO) {
			return NULL;
		}
		scenario = entry;
	}
	return scenario;
}

static s32 runtimePreparePrivateIncoming(grid_match_private_candidate_t *candidate,
	const grid_match_sources_t *plan, const char *revision_path, const char *arena_id,
	const u8 arena_sha256[SHA256_DIGEST_SIZE],
	char *err, size_t err_cap)
{
	const grid_match_source_entry_t *entry = runtimePrivateArenaScenario(plan, arena_id, arena_sha256);
	if (!entry) return runtimeError(err, err_cap, "the combined plan has no supported exact Grid arena pair", arena_id);
	const size_t package_index = entry->package_index;
	u32 size = 0;
	const void *bytes = gridMatchCacheRead(candidate->cache, &candidate->offer, package_index, &size);
	if (!bytes || size != plan->packages[package_index].size || size > NET_DISTRIB_MAX_COMP
			|| !fsPathIsAbsolute(revision_path) || strstr(revision_path, "::")
			|| assetArchiveTypeForPath(revision_path) != ASSET_ARENA
			|| !assetPathCopyChecked(candidate->arena_path, sizeof(candidate->arena_path), revision_path)) {
		return runtimeError(err, err_cap, "could not bind the exact private Grid arena bytes", arena_id);
	}
	u8 cached_sha[SHA256_DIGEST_SIZE];
	sha256Hash(bytes, size, cached_sha);
	if (memcmp(cached_sha, arena_sha256, sizeof(cached_sha)) != 0) {
		return runtimeError(err, err_cap, "the prepared Grid arena bytes no longer match the review", arena_id);
	}

	/* Read the bounded physical revision outside the outgoing FS mount and
	 * compare it to the bytes whose reviewed SHA was just verified. */
	u32 pinned_size = 0;
	void *pinned = gridMatchPackageLoad(revision_path, &pinned_size);
	const s32 pinned_exact = pinned && pinned_size == size && memcmp(pinned, bytes, size) == 0;
	free(pinned);
	if (!pinned_exact) return runtimeError(err, err_cap, "the pinned Grid arena no longer matches the review", arena_id);

	const u32 descriptor_max_bytes = 1024u * 1024u;
	u32 length = 0;
	asset_catalog_grid_arena_source_t arena = {0};
	if (!assetCatalogPreflightGridArenaLease(candidate->arena_path, arena_id, entry->asset.id,
			GRID_ARENA_LEASE_PINNED_FILE, bytes, size, &arena, err, err_cap)) return 0;
	char scenario_ref[FS_MAXPATH + 1];
	if (strcmp(arena.scenario_id, entry->asset.id) != 0 || strcmp(arena.scenario_member, entry->member_chain) != 0
			|| !assetPathJoinChecked(scenario_ref, sizeof(scenario_ref),
			candidate->arena_path, "::", arena.scenario_member)) {
		return runtimeError(err, err_cap, "the private arena descriptor does not bind its offered embedded scenario", arena_id);
	}
	u32 scenario_size = 0;
	void *native_scenario = runtimeReadMemberChain(bytes, size, arena.scenario_member,
		NET_DISTRIB_MAX_COMP, &scenario_size, NULL);
	char *scenario_descriptor = native_scenario ? runtimeReadMemberChain(native_scenario, scenario_size,
		"scenario.ini", descriptor_max_bytes, &length, NULL) : NULL;
	asset_entry_t *detached = scenario_descriptor ? malloc(sizeof(*detached)) : NULL;
	const s32 scenario_valid = detached && assetCatalogBindGridScenarioDescriptor(scenario_descriptor,
		length, scenario_ref, arena.scenario_id, detached);
	free(detached);
	free(scenario_descriptor);
	free(native_scenario);
	if (!scenario_valid || !assetPathCopyChecked(candidate->arena_id, sizeof(candidate->arena_id), arena_id)
			|| !assetPathCopyChecked(candidate->scenario_id, sizeof(candidate->scenario_id), arena.scenario_id)
			|| !assetPathCopyChecked(candidate->scenario_member, sizeof(candidate->scenario_member), arena.scenario_member)) {
		return runtimeError(err, err_cap, "could not bind the detached native Grid scenario", arena.scenario_id);
	}
	memcpy(candidate->arena_sha256, arena_sha256, sizeof(candidate->arena_sha256));
	candidate->arena_package_index = package_index;

	grid_match_runtime_t *incoming = calloc(1, sizeof(*incoming));
	if (!incoming) return runtimeError(err, err_cap, "out of memory preparing the incoming Grid mount", NULL);
	incoming->identity.offer = candidate->offer;
	incoming->asset_count = plan->entry_count;
	incoming->archives = calloc(2 * (incoming->asset_count + 1), sizeof(*incoming->archives));
	incoming->assets = calloc(incoming->asset_count, sizeof(*incoming->assets));
	char absolute[FS_MAXPATH + 1];
	fsFullPath(candidate->arena_path, absolute, sizeof(absolute));
	s32 valid = incoming->archives && incoming->assets && fsPathIsAbsolute(absolute)
		&& runtimeAddArchive(incoming, absolute, bytes, size)
		&& runtimeAddArchive(incoming, candidate->arena_path, bytes, size);
	if (!valid) runtimeError(err, err_cap, "could not prepare detached Grid arena aliases", arena_id);
	for (size_t i = 0; valid && i < incoming->asset_count; ++i) {
		const grid_match_source_entry_t *dependency = &plan->entries[i];
		if (dependency->package_index != package_index && !runtimeBindAsset(incoming, plan, candidate->cache, i)) {
			valid = runtimeError(err, err_cap, "an installed Grid dependency no longer matches its exact source", dependency->asset.id);
		}
	}
	if (valid && !fsPrepareStageArchives(&candidate->incoming_archives, incoming->archives, incoming->archive_count)) {
		valid = runtimeError(err, err_cap, "could not prepare the incoming Grid stage archives", arena_id);
	}
	if (valid) candidate->arena_file_policy = GRID_ARENA_LEASE_PINNED_FILE;
	runtimeFree(incoming);
	return valid;
}

typedef struct grid_layout_source_compare {
	const void *source;
	u32 source_size;
	const char *source_id;
	const void *other;
	u32 other_size;
	const char *other_id;
	s32 replaces_waypoints;
	s32 changed;
	s32 placements_changed;
} grid_layout_source_compare_t;

static s32 runtimeGridLayoutMetadata(const char *name, s32 replaces_waypoints)
{
	return strncmp(name, ASSET_ARCHIVE_META_DIR "/", sizeof(ASSET_ARCHIVE_META_DIR)) == 0
		|| strcmp(name, "scenario.ini") == 0 || strcmp(name, "stage.json") == 0 || strcmp(name, "scene.glb") == 0
		|| strcmp(name, "collision.obj") == 0 || strcmp(name, "rooms.json") == 0
		|| (replaces_waypoints && strcmp(name, "navigation/waypoints.json") == 0);
}

static s32 runtimeCompareGridLayoutMember(const char *name, u32 length, void *userdata)
{
	grid_layout_source_compare_t *pair = userdata;
	if (runtimeGridLayoutMetadata(name, pair->replaces_waypoints)) return 0;
	if (length > NET_DISTRIB_MAX_COMP) {
		pair->changed = 1;
		return 1;
	}
	u32 first_size = 0, second_size = 0;
	void *first = modArchiveExtractMemAlloc(pair->source, pair->source_size, name, &first_size);
	void *second = first ? gridMatchPackageReadMember(pair->other, pair->other_size,
		name, NET_DISTRIB_MAX_COMP, &second_size, NULL) : NULL;
	pair->changed = !first || !second || first_size != length;
	if (!pair->changed) {
		pair->changed = strcmp(name, "level.graph.json") == 0
			? !missionGraphLevelLayoutCompatible(first, first_size, pair->source_id,
				second, second_size, pair->other_id)
			: second_size != length || memcmp(first, second, length) != 0;
		if (pair->changed && (strcmp(name, forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_PADS)) == 0
				|| strcmp(name, forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_VOLUMES)) == 0)) {
			pair->placements_changed = 1;
			pair->changed = 0;
		}
	}
	free(first);
	free(second);
	return pair->changed;
}

static s32 runtimeGridLayoutSameSettings(const ini_section_t *from, const ini_section_t *to)
{
	if (strcmp(from->type, to->type) != 0) return 0;
	for (s32 i = 0; i < from->count; ++i) {
		const ini_pair_t *pair = &from->pairs[i];
		if (strcmp(pair->key, "id") == 0 || strcmp(pair->key, "catalog_id") == 0 || strcmp(pair->key, "name") == 0
				|| strcmp(pair->key, "description") == 0) continue;
		const char *other = iniGet(to, pair->key, NULL);
		if (!other || strcmp(pair->value, other) != 0) return 0;
	}
	return 1;
}

static s32 runtimeGridLayoutDescriptorsMatch(const void *old_source, u32 old_size,
	const void *new_source, u32 new_size)
{
	u32 old_length = 0, new_length = 0;
	const size_t limit = 1024u * 1024u;
	char *old_text = gridMatchPackageReadMember(old_source, old_size, "scenario.ini",
		limit, &old_length, NULL);
	char *new_text = gridMatchPackageReadMember(new_source, new_size, "scenario.ini",
		limit, &new_length, NULL);
	ini_section_t *old_ini = malloc(sizeof(*old_ini));
	ini_section_t *new_ini = malloc(sizeof(*new_ini));
	const s32 valid = old_text && new_text && old_ini && new_ini
		&& iniParseText(old_text, old_length, old_ini, NULL, 0)
		&& iniParseText(new_text, new_length, new_ini, NULL, 0)
		&& runtimeGridLayoutSameSettings(old_ini, new_ini)
		&& runtimeGridLayoutSameSettings(new_ini, old_ini);
	free(old_text);
	free(new_text);
	free(old_ini);
	free(new_ini);
	return valid;
}

static void runtimeGridLayoutStripAmbient(scenario_stage_atmosphere_t *atmosphere)
{
	atmosphere->has_ambient = 0;
	atmosphere->ambient_light = 0;
	atmosphere->present = atmosphere->has_sky || atmosphere->has_fog || atmosphere->primary_music[0];
}

static s32 runtimeGridLayoutStageRulesMatch(const void *old_source, u32 old_size,
	const void *new_source, u32 new_size)
{
	u32 old_length = 0, new_length = 0;
	s32 old_absent = 0, new_absent = 0;
	char *old_text = runtimeReadMemberChain(old_source, old_size, "stage.json",
		NET_DISTRIB_MAX_COMP, &old_length, &old_absent);
	char *new_text = runtimeReadMemberChain(new_source, new_size, "stage.json",
		NET_DISTRIB_MAX_COMP, &new_length, &new_absent);
	s32 valid = (old_absent && new_absent) || (old_text && new_text
		&& old_length == new_length && memcmp(old_text, new_text, old_length) == 0);
	if (!valid) {
		static const char defaults[] = "{\"schema\":\"" SCENARIO_STAGE_RULES_SCHEMA "\"}";
		scenario_stage_rules_t old_rules, new_rules;
		s32 live_enabled = 0;
		s32 live_ambient = 0;
		valid = scenarioStageRulesParse(old_absent ? defaults : old_text,
				old_absent ? sizeof(defaults) - 1 : old_length, &old_rules, NULL, 0)
			&& scenarioStageRulesParse(new_absent ? defaults : new_text,
				new_absent ? sizeof(defaults) - 1 : new_length, &new_rules, NULL, 0)
			&& netGridEditPrivatePlaytestAmbientState(&live_enabled, &live_ambient)
			&& new_rules.atmosphere.has_ambient == live_enabled
			&& (!live_enabled || new_rules.atmosphere.ambient_light == live_ambient);
		if (valid) {
			runtimeGridLayoutStripAmbient(&old_rules.atmosphere);
			runtimeGridLayoutStripAmbient(&new_rules.atmosphere);
			valid = memcmp(&old_rules, &new_rules, sizeof(old_rules)) == 0;
		}
	}
	free(old_text);
	free(new_text);
	return valid;
}

static s32 runtimeGridLayoutSourcesMatch(const void *old_source, u32 old_size, const char *old_id,
	const void *new_source, u32 new_size, const char *new_id, const forge_room_extension_t *extension,
	s32 *placements_changed)
{
	u32 count = 0;
	const asset_archive_clone_override_t *overrides = forgeRoomExtensionEntries(extension, &count);
	if (!overrides || (count != 3 && count != 4)) return 0;
	s32 replaces_waypoints = 0;
	for (u32 i = 0; i < count; ++i) {
		const char *member = strstr(overrides[i].entry, "::");
		if (!member) return 0;
		member += 2;
		u32 length = 0;
		void *actual = gridMatchPackageReadMember(new_source, new_size, member,
			NET_DISTRIB_MAX_COMP, &length, NULL);
		const s32 exact = actual && length == overrides[i].len
			&& memcmp(actual, overrides[i].data, length) == 0;
		free(actual);
		if (!exact) return 0;
		if (strcmp(member, "navigation/waypoints.json") == 0) replaces_waypoints = 1;
	}
	grid_layout_source_compare_t forward = { old_source, old_size, old_id, new_source, new_size, new_id, replaces_waypoints, 0, 0 };
	grid_layout_source_compare_t reverse = { new_source, new_size, new_id, old_source, old_size, old_id, replaces_waypoints, 0, 0 };
	const s32 compatible = runtimeGridLayoutDescriptorsMatch(old_source, old_size, new_source, new_size)
		&& runtimeGridLayoutStageRulesMatch(old_source, old_size, new_source, new_size)
		&& modArchiveMemForEachEntry(old_source, old_size, runtimeCompareGridLayoutMember, &forward) == MODARCHIVE_OK
		&& !forward.changed
		&& modArchiveMemForEachEntry(new_source, new_size, runtimeCompareGridLayoutMember, &reverse) == MODARCHIVE_OK
		&& !reverse.changed;
	*placements_changed = forward.placements_changed || reverse.placements_changed;
	return compatible;
}

static struct defaultobj *runtimeGridLayoutDecoration(const grid_match_runtime_t *match, const char *record_id)
{
	u32 index;
	if (!scenarioSourceGridDecorationCommand(match->identity.scenario_id, g_StageSetup.props, record_id, &index)) return NULL;
	struct defaultobj *obj = setupGetObjByCmdIndex(index);
	return obj && gridMatchRuntimeDecorationPadIndependent(obj->pad) ? obj : NULL;
}

static s32 runtimeGridLayoutPlacementMemberMatches(forge_level_doc_t *expected, s32 member,
	const void *scenario, u32 scenario_size)
{
	size_t expected_size = 0;
	const char *text = forgeLevelDocMemberText(expected, member, &expected_size);
	u32 actual_size = 0;
	void *actual = gridMatchPackageReadMember(scenario, scenario_size, forgeLevelDocMemberName(member),
		NET_DISTRIB_MAX_COMP, &actual_size, NULL);
	const s32 same = text && actual && actual_size == expected_size && memcmp(text, actual, actual_size) == 0;
	free(actual);
	return same;
}

static s32 runtimePrepareGridLayoutDecorations(grid_match_private_layout_t *layout, const grid_match_runtime_t *match,
	const void *old_source, u32 old_size, const void *new_source, u32 new_size,
	const struct grid_edit_room *draft_room, u64 draft_revision, char *err, size_t err_cap)
{
	u64 revision = 0;
	forge_level_doc_t *draft = gridEditRoomHostCloneDocument(draft_room, &revision);
	forge_level_doc_t *expected = NULL;
	s32 ok = 0;
	if (!draft || revision != draft_revision) goto done;
	expected = forgeLevelReadScenarioDocument(old_source, old_size, err, err_cap);
	const s32 count = forgeLevelDocObjectCount(expected);
	if (count <= 0 || count > FORGE_LEVEL_MAX_OBJECT_NUMBER + 1) goto done;
	layout->decorations = calloc((size_t)count, sizeof(*layout->decorations));
	if (!layout->decorations) goto done;

	/* Derive the exact allowed source changes from the resident static props.
	 * All other pad/volume bytes remain those of the approved round. */
	for (s32 i = 0; i < count; ++i) {
		forge_level_object_t object;
		forge_level_pad_t pinned, edited;
		if (!forgeLevelDocGetObject(expected, i, &object)
				|| !forgeLevelDocGetDecorationPose(expected, object.record_id, &pinned)
				|| !forgeLevelDocGetDecorationPose(draft, object.record_id, &edited)
				|| (pinned.position[0] == edited.position[0] && pinned.position[2] == edited.position[2])) continue;
		if (strcmp(pinned.pad_ref, edited.pad_ref) != 0 || strcmp(pinned.room_ref, edited.room_ref) != 0) goto done;
		struct defaultobj *obj = runtimeGridLayoutDecoration(match, object.record_id);
		const struct prop *prop = obj ? obj->prop : NULL;
		const u64 generation = propGetLifecycleGeneration(prop);
		if (!generation || prop->pos.x != edited.position[0] || prop->pos.z != edited.position[2]
				|| !objGridDecorationMove(obj, generation, prop->pos.x, prop->pos.z, 0)) goto done;
		const f32 position[] = { edited.position[0], pinned.position[1], edited.position[2] };
		if (!forgeLevelDocMoveDecoration(expected, object.record_id, position, pinned.look, err, err_cap)) goto done;
		grid_layout_decoration_t *proof = &layout->decorations[layout->decoration_count++];
		*proof = (grid_layout_decoration_t){
			.pose = { .x = prop->pos.x, .z = prop->pos.z },
			.prop_generation = generation, .room = prop->rooms[0], .native_y = prop->pos.y,
		};
		strcpy(proof->pose.record_id, object.record_id);
	}
	ok = layout->decoration_count > 0
		&& runtimeGridLayoutPlacementMemberMatches(expected, FORGE_LEVEL_MEMBER_PADS, new_source, new_size)
		&& runtimeGridLayoutPlacementMemberMatches(expected, FORGE_LEVEL_MEMBER_VOLUMES, new_source, new_size);

done:
	forgeLevelDocFree(expected);
	forgeLevelDocFree(draft);
	if (!ok && (!err || !err_cap || !err[0])) {
		return runtimeError(err, err_cap, "The reviewed placement changes do not match the live decorations", NULL);
	}
	return ok;
}

static s32 runtimeGridLayoutDecorationsFit(const grid_match_private_layout_t *layout,
	const grid_match_runtime_t *match, char *err, size_t err_cap)
{
	for (s32 i = 0; i < layout->decoration_count; ++i) {
		const grid_layout_decoration_t *proof = &layout->decorations[i];
		struct defaultobj *obj = runtimeGridLayoutDecoration(match, proof->pose.record_id);
		const struct prop *prop = obj ? obj->prop : NULL;
		if (!prop || prop->pos.x != proof->pose.x || prop->pos.z != proof->pose.z
				|| prop->pos.y != proof->native_y || prop->rooms[0] != proof->room
				|| !objGridDecorationMove(obj, proof->prop_generation, proof->pose.x, proof->pose.z, 0)) {
			return runtimeError(err, err_cap, "A reviewed decoration changed before Apply", proof->pose.record_id);
		}
		struct coord pos = prop->pos;
		RoomNum rooms[] = { proof->room, -1 };
		f32 height = 0.0f;
		struct coord normal;
		struct prop *floor_prop = NULL;
		if (cdFindFloorRoomYColourNormalPropAtPos(&pos, rooms, &height, NULL, &normal, &floor_prop) != proof->room
				|| floor_prop || !__builtin_isfinite(height)) {
			return runtimeError(err, err_cap, "A reviewed decoration lost its live floor", proof->pose.record_id);
		}
		/* Confirm the candidate retains this room and floor height, rather than
		 * allowing the decoration to remain suspended over a lower floor. */
		struct coord above = { .x = pos.x, .y = height + 0.25f, .z = pos.z };
		struct coord below = { .x = pos.x, .y = height - 0.25f, .z = pos.z };
		s32 floor = 0;
		for (s32 j = 0; j < layout->collision.numtris; ++j) {
			const struct meshtri *tri = &layout->collision.tris[j];
			f32 fraction;
			if (tri->roomnum == proof->room && (tri->flags & (GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2))
					&& !(tri->flags & GEOFLAG_DIE) && tri->normal.y > 0.0f
					&& meshRayVsTri(&above, &below, tri, &fraction)) {
				floor = 1;
				break;
			}
		}
		if (!floor || !meshCheckFlatWalk(&layout->collision, &above, proof->room,
				&above, proof->room, 0.25f, pos.y - height + 1.0f)) {
			return runtimeError(err, err_cap, "The reviewed layout removes a live decoration's floor", proof->pose.record_id);
		}
	}
	return 1;
}

void gridMatchRuntimeReleasePrivateCandidate(grid_match_private_candidate_t *candidate)
{
	if (!candidate) return;
	fsReleasePreparedStageArchives(&candidate->incoming_archives);
	fsReleasePreparedStageArchives(&candidate->outgoing_archives);
	gridMatchCacheReset(&candidate->cache);
	gridMatchCacheReset(&candidate->outgoing_cache);
	memset(candidate, 0, sizeof(*candidate));
}

s32 gridMatchRuntimePrivateCandidateCurrent(const grid_match_private_candidate_t *candidate,
	const grid_match_offer_t *arena_offer)
{
	const grid_match_runtime_t *match = s_ActiveMatch;
	if (!candidate || !runtimeSameOffer(&candidate->arena_offer, arena_offer)
			|| candidate->offer.room_id != arena_offer->room_id
			|| candidate->offer.generation != arena_offer->generation
			|| candidate->outgoing.offer.room_id != arena_offer->room_id
			|| !runtimePrivateCandidateActive(match, &candidate->outgoing.offer)) return 0;
	const grid_match_runtime_identity_t *active = &match->identity;
	return candidate->catalog_generation == match->catalog_generation
		&& candidate->incoming_archives && candidate->outgoing_archives
		&& !assetCatalogResolveAny(candidate->arena_id) && !assetCatalogResolveAny(candidate->scenario_id)
		&& assetCatalogPreviewGridArenaLeaseStage(candidate->scenario_id) >= 0
		&& candidate->outgoing_cache == match->cache
		&& candidate->outgoing.stage == active->stage
		&& strcmp(candidate->outgoing.arena_id, active->arena_id) == 0
		&& strcmp(candidate->outgoing.scenario_id, active->scenario_id) == 0
		&& memcmp(candidate->outgoing.arena_sha256, active->arena_sha256, SHA256_DIGEST_SIZE) == 0
		&& gridMatchCacheReady(candidate->cache, &candidate->offer);
}

s32 gridMatchRuntimeComposePrivateCandidate(const grid_match_offer_t *active_offer,
	const grid_match_cache_t *arena_cache, const grid_match_offer_t *arena_offer,
	const char *revision_path, const char *arena_id, const u8 arena_sha256[SHA256_DIGEST_SIZE],
	grid_match_private_candidate_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const grid_match_runtime_t *match = s_ActiveMatch;
	if (!out || !revision_path || !arena_id || !arena_id[0] || !arena_sha256
			|| !runtimePrivateCandidateActive(match, active_offer)
			|| !gridMatchCacheReady(arena_cache, arena_offer)
			|| arena_offer->room_id != active_offer->room_id) {
		return runtimeError(err, err_cap, "no matching active Grid playtest and prepared arena sources to compose", NULL);
	}
	if (!runtimeValidateStageBindings(match, err, err_cap)) return 0;

	grid_match_private_candidate_t candidate = {
		.offer = { .room_id = arena_offer->room_id, .generation = arena_offer->generation },
		.arena_offer = *arena_offer,
		.outgoing = match->identity,
		.catalog_generation = match->catalog_generation,
	};
	grid_match_sources_t arena = {0};
	if (!gridMatchCacheCopySources(arena_cache, &candidate.arena_offer, &arena, err, err_cap)) return 0;
	if (!runtimePrivateArenaScenario(&arena, arena_id, arena_sha256)) {
		gridMatchSourcesFree(&arena);
		return runtimeError(err, err_cap, "the reviewed cache has no supported exact Grid arena pair", arena_id);
	}
	grid_match_sources_t plan = {0};
	const s32 combined = gridMatchSourcesCombine(&plan, &match->private_gameplay_sources, &arena, err, err_cap);
	gridMatchSourcesFree(&arena);
	if (!combined) return 0;
	candidate.entry_count = plan.entry_count;
	candidate.package_count = plan.package_count;
	memcpy(candidate.offer.manifest_digest, plan.digest, sizeof(candidate.offer.manifest_digest));
	s32 ready = gridMatchCacheRetainReady(&candidate.outgoing_cache, match->cache,
			&candidate.outgoing.offer, err, err_cap)
		&& gridMatchCacheCompose(&candidate.cache, &candidate.offer, &plan,
			candidate.outgoing_cache, &candidate.outgoing.offer, arena_cache, &candidate.arena_offer, err, err_cap);
	if (ready) ready = runtimePreparePrivateIncoming(&candidate, &plan, revision_path, arena_id, arena_sha256, err, err_cap);
	if (ready && !fsPrepareStageArchives(&candidate.outgoing_archives, match->archives, match->archive_count)) {
		ready = runtimeError(err, err_cap, "could not retain the outgoing Grid stage archives", match->identity.arena_id);
	}
	gridMatchSourcesFree(&plan);
	if (!ready) {
		gridMatchRuntimeReleasePrivateCandidate(&candidate);
		return 0;
	}
	if (!runtimeSameOffer(&candidate.outgoing.offer, active_offer) || !gridMatchCacheReady(arena_cache, arena_offer)
			|| !gridMatchRuntimePrivateCandidateCurrent(&candidate, arena_offer)) {
		gridMatchRuntimeReleasePrivateCandidate(&candidate);
		return runtimeError(err, err_cap, "the active Grid playtest changed while composing candidate sources", NULL);
	}

	gridMatchRuntimeReleasePrivateCandidate(out);
	*out = candidate;
	return 1;
}

void gridMatchRuntimeReleasePrivateLayout(grid_match_private_layout_t *layout)
{
	if (!layout) return;
	scenarioSceneRendererReleaseGridGeometry(layout->scene);
	bgReleaseGridRoomLayout(layout->rooms);
	scenarioSourceReleaseGridTiles(layout->tiles);
	meshWorldFreePrepared(layout->mesh);
	meshFree(&layout->collision);
	free(layout->decorations);
	free(layout);
}

u64 gridMatchRuntimePrivateLayoutGeneration(void)
{
	return s_ActiveMatch ? s_ActiveMatch->layout_generation : 0;
}

static s32 runtimePrepareGridNativeBanks(grid_match_private_layout_t *layout,
	grid_match_scenario_source_t *source, const asset_entry_t *scenario,
	grid_match_scenario_source_t *round_source, const grid_match_runtime_t *match,
	char *err, size_t err_cap)
{
	const s32 room_count = 3; /* Reserved room 0 and the two authored rooms. */
	grid_match_scenario_probe_t checked = {0};
	struct colmesh *collision = &layout->collision;
	const asset_entry_t *round_scenario = assetCatalogResolve(match->identity.scenario_id);
	const grid_match_scenario_probe_t *active = &match->scenario_probe;
	if (!round_scenario) return runtimeError(err, err_cap, "The running Grid routes lost their source", NULL);
	/* The exact room builder can omit generated waypoints with no traversable
	 * links. Test its linked graph against the incoming collision and separately
	 * prove the running padfile is still the original. The exact extension
	 * comparison preserves the pads used by starts and traversable links. */
	scenario_source_navigation_probe_t reviewed_routes = {0};
	if (!runtimePreflightCollision(source, scenario, &checked, collision, err, err_cap)
			|| !runtimePreflightRoomTable(source, scenario, room_count, collision, &checked, err, err_cap)
			|| !scenarioSourcePreflightNavigation(scenario, runtimeReadNavigationMember, source,
				room_count, source->max_bytes, collision, &reviewed_routes, err, err_cap)
			|| !scenarioSourcePreflightNavigation(round_scenario, runtimeReadNavigationMember, round_source,
				room_count, round_source->max_bytes, NULL, &checked.navigation_probe, err, err_cap)) return 0;
	if (checked.authored_room_bounds != 2 || checked.collision_max_room != 2
			|| !g_WorldMesh.ready || g_WorldMesh.numtris != active->collision_triangles
			|| checked.collision_triangles != active->collision_triangles
			|| memcmp(checked.navigation_probe.digest, active->navigation_probe.digest, SHA256_DIGEST_SIZE) != 0) {
		return runtimeError(err, err_cap, "The reviewed layout changes the native routes or world ownership", NULL);
	}
	if (!meshWorldPrepare(collision, &layout->mesh)) {
		return runtimeError(err, err_cap, "Could not prepare the native collision grid", NULL);
	}
	if (!scenarioSourcePrepareGridTiles(collision, room_count, &layout->tiles, err, err_cap)
			|| !bgPrepareGridRoomLayout(collision, &layout->rooms, err, err_cap)) return 0;

	const char *scene_path = scenario->ext.scenario.scene_file;
	u32 scene_size = 0;
	void *scene = runtimeReadScenarioMember(source, scene_path, &scene_size, NULL);
	const s32 ready = scene && scenarioSceneRendererPrepareGridGeometry(scenario->id, scene_path,
		scene, scene_size, &layout->scene);
	free(scene);
	return ready || runtimeError(err, err_cap, "Could not prepare the native Grid render scene", NULL);
}

s32 gridMatchRuntimePreparePrivateLayout(const grid_match_private_candidate_t *candidate,
	const grid_match_offer_t *arena_offer, s32 room, s32 extension,
	const struct grid_edit_room *draft_room, u64 draft_revision, grid_match_private_layout_t **out,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const grid_match_runtime_t *match = s_ActiveMatch;
	if (!out || *out || !match || g_NetMode != NETMODE_SERVER || g_StageNum != match->identity.stage
			|| !g_NetStageEpoch || !gridMatchRuntimePrivateCandidateCurrent(candidate, arena_offer)) {
		return runtimeError(err, err_cap, "The exact private layout review is no longer current", NULL);
	}
	u32 old_arena_size = 0, new_arena_size = 0;
	const void *old_arena = gridMatchCacheRead(match->cache, &match->identity.offer,
		match->arena_package_index, &old_arena_size);
	const void *new_arena = gridMatchCacheRead(candidate->cache, &candidate->offer,
		candidate->arena_package_index, &new_arena_size);
	const char *old_member = strstr(match->scenario_link, "::");
	if (!old_arena || !new_arena || !old_member || strstr(old_member + 2, "::")) {
		return runtimeError(err, err_cap, "The exact two-room layout sources are unavailable", NULL);
	}
	old_member += 2;
	forge_room_extension_t *expected = forgeRoomCreateExtension(old_arena, old_arena_size, old_member,
		room, extension, err, err_cap);
	if (!expected) return 0;
	u32 old_size = 0, new_size = 0;
	void *old_source = runtimeReadMemberChain(old_arena, old_arena_size, old_member,
		NET_DISTRIB_MAX_COMP, &old_size, NULL);
	void *new_source = runtimeReadMemberChain(new_arena, new_arena_size, candidate->scenario_member,
		NET_DISTRIB_MAX_COMP, &new_size, NULL);
	s32 placements_changed = 0;
	const s32 compatible = old_source && new_source
		&& runtimeGridLayoutSourcesMatch(old_source, old_size, match->identity.scenario_id,
			new_source, new_size, candidate->scenario_id, expected, &placements_changed);
	forgeRoomFreeExtension(expected);
	if (!compatible) {
		free(old_source);
		free(new_source);
		return runtimeError(err, err_cap,
			"The reviewed variant changes more than the supported room layout", NULL);
	}

	char scenario_ref[FS_MAXPATH + 1];
	u32 ini_size = 0;
	char *ini = runtimeReadMemberChain(new_source, new_size, "scenario.ini",
		1024u * 1024u, &ini_size, NULL);
	asset_entry_t *scenario = malloc(sizeof(*scenario));
	grid_match_private_layout_t *layout = calloc(1, sizeof(*layout));
	const s32 bound = scenario && layout && ini
		&& assetPathJoinChecked(scenario_ref, sizeof(scenario_ref), candidate->arena_path, "::", candidate->scenario_member)
		&& assetCatalogBindGridScenarioDescriptor(ini, ini_size, scenario_ref, candidate->scenario_id, scenario);
	free(ini);
	grid_match_scenario_source_t source = {
		.arena_root = candidate->arena_path, .scenario_chain = candidate->scenario_member,
		.arena = new_arena, .arena_size = new_arena_size, .scenario = new_source,
		.scenario_size = new_size, .max_bytes = NET_DISTRIB_MAX_COMP,
	};
	grid_match_scenario_source_t round_source = {
		.match = match, .scenario_chain = old_member, .arena = old_arena, .arena_size = old_arena_size,
		.scenario = old_source, .scenario_size = old_size, .max_bytes = NET_DISTRIB_MAX_COMP,
	};
	const s32 ready = bound && runtimePrepareGridNativeBanks(layout, &source, scenario,
		&round_source, match, err, err_cap)
		&& (!placements_changed || (runtimePrepareGridLayoutDecorations(layout, match,
			old_source, old_size, new_source, new_size, draft_room, draft_revision, err, err_cap)
			&& runtimeGridLayoutDecorationsFit(layout, match, err, err_cap)));
	free(old_source);
	free(new_source);
	free(scenario);
	if (!ready || !gridMatchRuntimePrivateCandidateCurrent(candidate, arena_offer)) {
		gridMatchRuntimeReleasePrivateLayout(layout);
		if (!err || !err_cap || !err[0]) runtimeError(err, err_cap, "Could not bind the current Grid layout", NULL);
		return 0;
	}
	layout->round = match->identity;
	layout->round_epoch = g_NetStageEpoch;
	layout->catalog_generation = match->catalog_generation;
	layout->base_generation = match->layout_generation;
	memcpy(layout->arena_sha256, candidate->arena_sha256, sizeof(layout->arena_sha256));
	*out = layout;
	return 1;
}

s32 gridMatchRuntimeCommitPrivateLayout(grid_match_private_layout_t *layout, u64 previous_generation,
	u64 next_generation, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	grid_match_runtime_t *match = s_ActiveMatch;
	if (!layout || layout->committed || !match || g_NetMode != NETMODE_SERVER
			|| next_generation <= previous_generation
			|| layout->base_generation != previous_generation || match->layout_generation != previous_generation
			|| layout->round_epoch != g_NetStageEpoch || layout->catalog_generation != match->catalog_generation
			|| g_StageNum != layout->round.stage || !runtimePrivateCandidateActive(match, &layout->round.offer)) {
		return runtimeError(err, err_cap, "The running private round changed before Apply", NULL);
	}
	if (!runtimeGridLayoutDecorationsFit(layout, match, err, err_cap)) return 0;
	grid_layout_actors_t *actors = NULL;
	if (!gridLayoutPrepareActors(&layout->collision, &actors, err, err_cap)) return 0;

	/* All products and actor placements are ready. No consumer runs between
	 * these exchanges and no operation after this point can refuse Apply. */
	meshWorldSwapPrepared(layout->mesh);
	scenarioSourceSwapGridTiles(&layout->tiles);
	bgSwapGridRoomLayout(&layout->rooms);
	scenarioSceneRendererSwapGridGeometry(layout->scene);
	gridLayoutCommitActors(actors);
	gridLayoutReleaseActors(actors);
	match->layout_generation = next_generation;
	layout->committed = 1;

	char sha[SHA256_HEX_SIZE];
	sha256ToHex(layout->arena_sha256, sha);
	sysLogPrintf(LOG_NOTE, "GRID.LAYOUT: committed room=%u epoch=%u generation=%llu sha256=%s",
		(unsigned)match->identity.offer.room_id, (unsigned)g_NetStageEpoch,
		(unsigned long long)next_generation, sha);
	if (layout->decoration_count) {
		sysLogPrintf(LOG_NOTE, "GRID.LAYOUT.DECORATIONS: retained=%d epoch=%u generation=%llu",
			layout->decoration_count, (unsigned)g_NetStageEpoch, (unsigned long long)next_generation);
	}
	return 1;
}

void gridMatchRuntimeFreeReconnectSources(grid_match_reconnect_sources_t *sources)
{
	if (!sources) return;
	for (size_t i = 0; i < sources->count; i++) {
		free(sources->packages[i].bytes);
	}
	free(sources->packages);
	memset(sources, 0, sizeof(*sources));
}

static s32 runtimeRetainReconnectPackage(const grid_match_runtime_t *match, size_t index,
		asset_type_e type, const char *path, grid_match_reconnect_package_t *out)
{
	const grid_match_offer_t *offer = &match->identity.offer;
	const grid_match_source_package_t *package = gridMatchCachePackage(match->cache, offer, index);
	u32 size = 0;
	const void *bytes = gridMatchCacheRead(match->cache, offer, index, &size);
	if (index >= 0xffffu || !path || !package || package->bundled || package->owner.type != type
			|| !size || size > NET_DISTRIB_MAX_COMP || !runtimeHasBoundArchive(match, path, bytes, size)) {
		return 0;
	}

	void *copy = malloc(size);
	if (!copy) return 0;
	memcpy(copy, bytes, size);
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(copy, size, digest);
	if (memcmp(digest, package->digest, sizeof(digest)) != 0) {
		free(copy);
		return 0;
	}
	*out = (grid_match_reconnect_package_t){ .owner = package->owner, .size = size,
		.index = (u16)index, .bytes = copy };
	memcpy(out->digest, digest, sizeof(out->digest));
	return 1;
}

static s32 runtimeRetainTemporarySources(const grid_match_runtime_t *match, s32 include_arena,
		grid_match_reconnect_sources_t *out, char *err, size_t err_cap)
{
	grid_match_reconnect_sources_t copy = {0};
	const size_t count = match->session_sources.count
		+ (match->arena_lease.arena_id[0] != '\0') + (match->weapon_lease != NULL);
	if (count) {
		copy.packages = calloc(count, sizeof(*copy.packages));
		if (!copy.packages) return runtimeError(err, err_cap, "could not retain the Grid package list", NULL);
	}
	for (size_t i = 0; i < match->session_sources.count; i++) {
		const grid_match_session_file_t *file = &match->session_sources.files[i];
		if (!include_arena && file->owner.type == ASSET_ARENA) continue;
		if (!runtimeRetainReconnectPackage(match, file->package_index, file->owner.type, file->path,
				&copy.packages[copy.count])) goto fail;
		copy.count++;
	}
	if (include_arena && match->arena_lease.arena_id[0]) {
		if (!runtimeRetainReconnectPackage(match, match->arena_package_index,
				ASSET_ARENA, match->arena_lease.archive_path, &copy.packages[copy.count])) goto fail;
		copy.count++;
	}
	if (match->weapon_lease) {
		if (!runtimeRetainReconnectPackage(match, match->weapon_package_index, ASSET_WEAPON,
				assetCatalogGridWeaponLeasePath(match->weapon_lease), &copy.packages[copy.count])) goto fail;
		copy.count++;
	}
	if (copy.count == 2 && copy.packages[0].index > copy.packages[1].index) {
		grid_match_reconnect_package_t first = copy.packages[0];
		copy.packages[0] = copy.packages[1];
		copy.packages[1] = first;
	}
	gridMatchRuntimeFreeReconnectSources(out);
	*out = copy;
	return 1;

fail:
	gridMatchRuntimeFreeReconnectSources(&copy);
	return runtimeError(err, err_cap, "could not retain a verified temporary Grid source", NULL);
}

s32 gridMatchRuntimeRetainReconnectSources(grid_match_reconnect_sources_t *out,
		char *err, size_t err_cap)
{
	if (!s_ActiveMatch || !out) return runtimeError(err, err_cap, "no active Grid match to retain", NULL);
	return runtimeRetainTemporarySources(s_ActiveMatch, 1, out, err, err_cap);
}

const void *gridMatchRuntimeReadPendingPackage(const grid_match_offer_t *offer,
	size_t package_index, u32 *out_size)
{
	return gridMatchCacheRead(s_PendingMatch ? s_PendingMatch->cache : NULL,
		offer, package_index, out_size);
}

const grid_match_source_package_t *gridMatchRuntimePendingPackage(const grid_match_offer_t *offer,
	size_t package_index)
{
	return gridMatchCachePackage(s_PendingMatch ? s_PendingMatch->cache : NULL, offer, package_index);
}

s32 gridMatchRuntimeOwnSourceSet(grid_match_session_sources_t *sources, char *err, size_t err_cap)
{
	grid_match_runtime_t *match = s_PendingMatch;
	if (!match || !sources || !sources->lease || !sources->files || !sources->count
			|| match->session_sources.lease || match->arena_lease.arena_id[0] || match->weapon_lease
			|| (s_ActiveMatch && s_ActiveMatch->session_sources.lease)
			|| s_RetiringSources.files || s_RetiringSources.lease) {
		return runtimeError(err, err_cap, "another Grid source set is owned or no stage is pending", NULL);
	}
	for (size_t i = 0; i < sources->count; i++) {
		const grid_match_session_file_t *file = &sources->files[i];
		const grid_match_source_package_t *package = gridMatchCachePackage(match->cache, &match->identity.offer,
			file->package_index);
		u32 size = 0;
		const void *bytes = gridMatchCacheRead(match->cache, &match->identity.offer, file->package_index, &size);
		const char *lease_path = assetCatalogGridSourceSetPath(sources->lease, file->package_index);
		if (!package || package->bundled || package->owner.type != file->owner.type
				|| strcmp(package->owner.id, file->owner.id) != 0 || !size
				|| !lease_path || strcmp(lease_path, file->path) != 0
				|| (i && sources->files[i - 1].package_index >= file->package_index)
				|| !runtimeHasBoundArchive(match, file->path, bytes, size)) {
			return runtimeError(err, err_cap, "the temporary Grid source did not bind to the exact match", file->owner.id);
		}
	}
	match->session_sources = *sources;
	*sources = (grid_match_session_sources_t){0};
	return 1;
}

s32 gridMatchRuntimeOwnArenaLease(asset_catalog_arena_lease_t *lease,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	grid_match_runtime_t *match = s_PendingMatch;
	if (!match || !lease || !lease->arena_id[0] || match->arena_lease.arena_id[0]
			|| (s_ActiveMatch && s_ActiveMatch->arena_lease.arena_id[0]) || s_RetiringArena.path[0]) {
		return runtimeError(err, err_cap, "another Grid arena lease is owned or no stage is pending", NULL);
	}

	char chain[FS_MAXPATH];
	const asset_entry_t *arena = runtimeStageArena(&match->identity);
	const asset_entry_t *scenario = assetCatalogResolve(match->identity.scenario_id);
	if (!arena || !scenario || !arena->temporary || !scenario->temporary
			|| !arena->grid_offer_only || !scenario->grid_offer_only
			|| strcmp(match->identity.arena_id, lease->arena_id) != 0
			|| strcmp(match->identity.scenario_id, lease->scenario_id) != 0
			|| match->identity.stage != lease->stagenum || arena->runtime_index != lease->stage_index
			|| scenario->runtime_index != lease->stage_index || !fsPathIsAbsolute(lease->archive_path)
			|| !assetPathJoinChecked(chain, sizeof(chain), lease->archive_path, "::", lease->scenario_member)
			|| strcmp(chain, match->scenario_link) != 0 || strcmp(chain, arena->ext.arena.scenario_archive) != 0) {
		return runtimeError(err, err_cap, "the arena lease does not match the pending stage binding", NULL);
	}

	u32 size = 0;
	const void *bytes = gridMatchRuntimeReadPendingPackage(&match->identity.offer, match->arena_package_index, &size);
	u8 digest[SHA256_DIGEST_SIZE];
	if (!runtimeHasBoundArchive(match, lease->archive_path, bytes, size)
			|| modArchiveSha256(lease->archive_path, digest) != MODARCHIVE_OK
			|| memcmp(digest, match->identity.arena_sha256, sizeof(digest)) != 0) {
		return runtimeError(err, err_cap, "the leased archive does not match the staged bytes", NULL);
	}
	match->arena_lease = *lease;
	memset(lease, 0, sizeof(*lease));
	return 1;
}

s32 gridMatchRuntimeOwnWeaponLease(asset_catalog_grid_weapon_lease_t **lease,
	size_t package_index, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	grid_match_runtime_t *match = s_PendingMatch;
	const char *path = lease && *lease ? assetCatalogGridWeaponLeasePath(*lease) : NULL;
	if (!match || !path || match->weapon_lease || (s_ActiveMatch && s_ActiveMatch->weapon_lease)
			|| s_RetiringWeapon.path[0] || assetArchiveTypeForPath(path) != ASSET_WEAPON) {
		return runtimeError(err, err_cap, "another Grid weapon lease is owned or no stage is pending", NULL);
	}
	u32 size = 0;
	const void *bytes = gridMatchRuntimeReadPendingPackage(&match->identity.offer, package_index, &size);
	if (!runtimeHasBoundArchive(match, path, bytes, size)) {
		return runtimeError(err, err_cap, "the weapon lease does not match the staged bytes", NULL);
	}
	match->weapon_lease = *lease;
	match->weapon_package_index = package_index;
	*lease = NULL;
	return 1;
}

static s32 runtimeValidateStageBindings(const grid_match_runtime_t *match, char *err, size_t err_cap)
{
	if (match->catalog_generation != assetCatalogGetGeneration()) {
		return runtimeError(err, err_cap, "the catalog changed before Grid stage load", NULL);
	}
	const asset_entry_t *arena = runtimeStageArena(&match->identity);
	if (!arena || strcmp(arena->ext.arena.scenario_archive, match->scenario_link) != 0) {
		return runtimeError(err, err_cap, "the arena's catalog binding changed before stage load", match->identity.arena_id);
	}
	for (size_t i = 0; i < match->asset_count; i++) {
		const grid_match_installed_asset_t *asset = &match->assets[i];
		const asset_entry_t *entry = assetCatalogResolve(asset->asset.id);
		char descriptor[FS_MAXPATH];
		if (!entry || !entry->enabled || entry->type != asset->asset.type
				|| (entry->bundled != 0) != asset->bundled
				|| !gridMatchCatalogArchiveDescriptor(entry, descriptor, sizeof(descriptor))
				|| strcmp(descriptor, asset->descriptor) != 0) {
			return runtimeError(err, err_cap, "an asset's catalog binding changed before stage load", asset->asset.id);
		}
	}
	for (s32 i = 0; i < match->fiesta_count; i++) {
		if (!runtimeFiestaBindingMatches(match, i)) {
			const grid_match_installed_asset_t *asset = &match->assets[match->fiesta_asset_indexes[i]];
			return runtimeError(err, err_cap, "a Grid Fiesta weapon binding changed before stage load", asset->asset.id);
		}
	}
	return 1;
}

s32 gridMatchRuntimeValidatePendingStage(s32 stage, const grid_match_offer_t *offer,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const grid_match_runtime_t *match = s_PendingMatch;
	if (s_ActiveMatch || !match || !match->prepared_archives
			|| match->identity.stage != stage || !gridMatchCacheReady(match->cache, offer)) {
		return runtimeError(err, err_cap, "no matching pending Grid stage, or another stage is active", NULL);
	}
	return runtimeValidateStageBindings(match, err, err_cap);
}

s32 gridMatchRuntimePublishStage(s32 stage, const grid_match_offer_t *offer,
	char *err, size_t err_cap)
{
	if (!gridMatchRuntimeValidatePendingStage(stage, offer, err, err_cap)) return 0;
	grid_match_runtime_t *match = s_PendingMatch;
	if (!fsPublishPreparedStageArchives(&match->prepared_archives)) {
		return runtimeError(err, err_cap, "could not publish private Grid stage sources", NULL);
	}
	s_ActiveMatch = match;
	s_PendingMatch = NULL;
	return 1;
}

void gridMatchRuntimeCancelPending(void)
{
	grid_match_runtime_t *match = s_PendingMatch;
	s_PendingMatch = NULL;
	runtimeRetireArena(match);
	runtimeRetireWeapon(match);
	runtimeRetireSources(match);
	runtimeFree(match);
}

const void *gridMatchRuntimeReadReturnedArena(const grid_match_result_t *result, u32 *size)
{
	if (size) *size = 0;
	const grid_match_result_t *retained = &s_ReturnedArena.result;
	if (!s_ReturnedArena.bytes
			|| !gridMatchResultMatchesStage(result, &retained->offer, retained->stage_epoch,
				retained->arena_id, retained->arena_sha256)
			|| result->end_tick != retained->end_tick || result->reason != retained->reason
			|| memcmp(result->score_sha256, retained->score_sha256, sizeof(retained->score_sha256)) != 0) {
		return NULL;
	}
	if (size) *size = s_ReturnedArena.size;
	return s_ReturnedArena.bytes;
}

const grid_match_sources_t *gridMatchRuntimeReturnedSources(const grid_match_result_t *result)
{
	return gridMatchRuntimeReadReturnedArena(result, NULL) && s_ReturnedArena.sources.entry_count
		? &s_ReturnedArena.sources : NULL;
}

const void *gridMatchRuntimeReadReturnedSource(const grid_match_result_t *result,
	size_t package_index, u32 *size)
{
	if (size) *size = 0;
	if (!gridMatchRuntimeReturnedSources(result)) return NULL;
	for (size_t i = 0; i < s_ReturnedArena.retained.count; i++) {
		const grid_match_reconnect_package_t *source = &s_ReturnedArena.retained.packages[i];
		if (source->index != package_index) continue;
		if (size) *size = source->size;
		return source->bytes;
	}
	return NULL;
}

void gridMatchRuntimeForgetReturnedArena(void)
{
	free(s_ReturnedArena.bytes);
	gridMatchSourcesFree(&s_ReturnedArena.sources);
	gridMatchRuntimeFreeReconnectSources(&s_ReturnedArena.retained);
	memset(&s_ReturnedArena, 0, sizeof(s_ReturnedArena));
}

static s32 runtimeRetainReturnedVariantMount(const grid_match_runtime_t *match,
		const grid_match_sources_t *sources, grid_match_reconnect_sources_t *retained,
		char *err, size_t err_cap)
{
	const grid_match_runtime_identity_t *identity = &match->identity;
	if (!forgeLevelVariantDependencyPlanForStage(identity->stage)) return 1;

	/* A local saved-variant mount retires with the world. A proposal lease
	 * stays in the owning Room and does not require another raw copy. */
	u8 captured[MANIFEST_MAX_ENTRIES] = {0};
	for (size_t i = 0; i < retained->count; i++) {
		captured[retained->packages[i].index] = 1;
	}
	u16 indexes[MANIFEST_MAX_ENTRIES];
	size_t count = 0;
	for (size_t i = 0; i < sources->package_count; i++) {
		if (!captured[i] && forgeLevelVariantDependencyOwnsPackage(identity->arena_id,
				identity->arena_sha256, &sources->packages[i])) {
			indexes[count++] = (u16)i;
		}
	}
	if (!count) return 1;

	grid_match_reconnect_package_t *grown = realloc(retained->packages,
		(retained->count + count) * sizeof(*retained->packages));
	if (!grown) return runtimeError(err, err_cap, "could not retain the played variant source list", NULL);
	retained->packages = grown;
	for (size_t i = 0; i < count; i++) {
		const u16 index = indexes[i];
		const grid_match_source_package_t *package = &sources->packages[index];
		if (!runtimeRetainReconnectPackage(match, index, package->owner.type, package->path,
				&retained->packages[retained->count])) {
			return runtimeError(err, err_cap, "could not retain the played variant source", package->owner.id);
		}
		retained->count++;
	}
	return 1;
}

s32 gridMatchRuntimeRememberReturnedArena(const grid_match_result_t *result)
{
	const grid_match_runtime_t *match = s_ActiveMatch;
	const s32 listen_host = g_NetMode == NETMODE_SERVER && !g_NetDedicated;
	if (!match || (g_NetMode != NETMODE_CLIENT && !listen_host)) return 0;

	const grid_match_runtime_identity_t *identity = &match->identity;
	if (listen_host && (!g_NetLocalClient || g_NetLocalClient->room_id != identity->offer.room_id
			|| g_NetMatchRoomId != identity->offer.room_id)) return 0;
	if (!gridMatchResultMatchesStage(result, &identity->offer, g_NetStageEpoch,
				identity->arena_id, identity->arena_sha256)) {
		return 0;
	}
	const char *bound_archive = match->arena_stage_root;
	const asset_catalog_arena_lease_t *lease = &match->arena_lease;
	const char *set_path = match->session_sources.lease
		? assetCatalogGridSourceSetPath(match->session_sources.lease, match->arena_package_index) : NULL;
	if (lease->arena_id[0]) {
		if (assetArchiveTypeForPath(lease->archive_path) != ASSET_ARENA) return 0;
		const asset_entry_t *arena = runtimeStageArena(identity);
		if (!arena || !arena->temporary || !arena->grid_offer_only
				|| arena->runtime_index != lease->stage_index
				|| strcmp(arena->ext.arena.scenario_archive, match->scenario_link) != 0) return 0;
		bound_archive = lease->archive_path;
	} else if (set_path) {
		const asset_entry_t *arena = runtimeStageArena(identity);
		if (!arena || !arena->temporary || !arena->grid_offer_only
				|| strcmp(arena->ext.arena.scenario_archive, match->scenario_link) != 0) return 0;
		bound_archive = set_path;
	}
	if (gridMatchRuntimeReadReturnedArena(result, NULL)) return 1;

	const grid_match_source_package_t *package = gridMatchCachePackage(match->cache,
		&identity->offer, match->arena_package_index);
	if (!package || package->owner.type != ASSET_ARENA
			|| strcmp(package->owner.id, identity->arena_id) != 0
			|| strcmp(package->suffix, ".pdarena") != 0
			|| memcmp(package->digest, identity->arena_sha256, sizeof(package->digest)) != 0) return 0;

	u32 size = 0;
	const void *bytes = gridMatchCacheRead(match->cache, &identity->offer, match->arena_package_index, &size);
	if (!size || size != package->size || size > NET_DISTRIB_MAX_COMP
			|| !runtimeHasBoundArchive(match, bound_archive, bytes, size)) {
		return 0;
	}
	void *copy = malloc(size);
	if (!copy) return 0;
	memcpy(copy, bytes, size);
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(copy, size, digest);
	if (memcmp(digest, identity->arena_sha256, sizeof(digest)) != 0) {
		free(copy);
		return 0;
	}

	gridMatchRuntimeForgetReturnedArena();
	s_ReturnedArena.result = *result;
	s_ReturnedArena.bytes = copy;
	s_ReturnedArena.size = size;
	char retention_error[160] = {0};
	if (!gridMatchCacheCopySources(match->cache, &identity->offer, &s_ReturnedArena.sources,
				retention_error, sizeof(retention_error))
			|| !runtimeRetainTemporarySources(match, 0, &s_ReturnedArena.retained,
				retention_error, sizeof(retention_error))
			|| (listen_host && !runtimeRetainReturnedVariantMount(match,
				&s_ReturnedArena.sources, &s_ReturnedArena.retained,
				retention_error, sizeof(retention_error)))) {
		gridMatchSourcesFree(&s_ReturnedArena.sources);
		gridMatchRuntimeFreeReconnectSources(&s_ReturnedArena.retained);
		sysLogPrintf(LOG_WARNING, "GRID.MATCH.RETURNED_ARENA: variant sources unavailable: %s", retention_error);
	}
	sysLogPrintf(LOG_NOTE, "GRID.MATCH.RETURNED_ARENA: retained room=%u generation=%llu stage_epoch=%u arena=%s bytes=%u",
		(unsigned)result->offer.room_id, (unsigned long long)result->offer.generation,
		(unsigned)result->stage_epoch, result->arena_id, (unsigned)size);
	return 1;
}

void gridMatchRuntimeDropActive(void)
{
	grid_match_runtime_t *match = s_ActiveMatch;
	if (!match) return;
	fsClearStageArchives();
	s_ActiveMatch = NULL;
	runtimeRetireArena(match);
	runtimeRetireWeapon(match);
	runtimeRetireSources(match);
	runtimeFree(match);
}
