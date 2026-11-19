/*
 * assetcatalog_scanner.c -- D3R-4: Component scanner + INI loader
 *
 * Scans mod component directories and registers assets in the catalog.
 * Two-pass approach:
 *   1. Enumerate mod directories under mods/
 *   2. For each mod, scan category subdirectories (maps, characters, textures)
 *   3. For each component folder, parse the .ini manifest
 *   4. Register a catalog entry based on the INI type and fields
 *
 * The INI parser is minimal but sufficient for the component .ini format:
 *   - First section names the asset type: [map], [weapon], [head], etc.
 *   - Later sections may group settings for modders; keys share one flat map.
 *   - Key = value pairs, one per line
 *   - Hash and semicolon comments, blank lines ignored
 *   - No multiline values, no escaping
 *
 * Auto-discovered by CMake glob. No build system changes needed.
 */

#include <PR/ultratypes.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include "types.h"
#include "constants.h"
#include "asset_archive_policy.h"
#include "grid_archive_policy.h"
#include "grid_match_package_io.h"
#include "assetcatalog.h"
#include "catalog_audio_source_identity.h"
#include "catalog_audio_public_source.h"
#include "loader_walker_common.h"
#include "lang_source.h"
#include "assetcatalog_load.h"
#include "asset_runtime.h"
#include "catalog_activation_ledger.h"
#include "catalog_entry_snapshot.h"
#include "assetprovider.h"
#include "assetprovider_internal.h"
#include "assetcatalog_body_head_slots.h"
#include "assetcatalog_model_slots.h"
#include "body_head_source.h"
#include "body_head_source_bind.h"
#include "assetcatalog_weapon_slots.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_heads.h"
#include "catalog_model_generation.h"
#include "assetcatalog_sound_slots.h" /* c3849 Wave 2 */
#include "assetcatalog_texture_slots.h" /* c3849 Wave 2 */
#include "catalog_texture_generation.h"
#include "texture_source_runtime.h"
#include "assetcatalog_anim_slots.h" /* c3849 Wave 2 */
#include "assetcatalog_stage_slots.h" /* c3849 Wave 2 */
#include "game/stagetable.h" /* c3849 Wave 2: stageTableAppend for minted stagenums */
#include "assetcatalog_deps.h"
#include "assetcatalog_scanner.h"
#include "character_head_policy.h"
#include "asset_path_contract.h"
#include "loader_pool.h"
#include "loader_walker_mesh_source.h"
#include "loader_walker_common.h"
#include "mod.h"
#include "modarchive.h"
#include "modvfs.h"
#include "mod_asset_validate.h"
#include "mods_roots.h"
#include "net/net.h"
#include "net/netmanifest.h"
#include "pdeffect_source.h"
#include "effect_dependencies.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"
#include "weapon_graph_v2_ingress.h"
#include "romdata.h"
#include "sha256.h"
#include "system.h"
#include "fs.h"

typedef struct external_scan_transaction {
	catalog_entry_snapshot_t *rows;
	s32 row_count;
	s32 existing_rows_only;
	s32 weapon_rows_begin;
	catalog_dep_snapshot_t deps;
	file_provider_checkpoint_t provider;
	void *weapon_slots;
	void *body_head_slots;
	void *model_slots;
	void *sound_slots;
	void *texture_slots;
	void *anim_slots;
	void *stage_slots;
	void *stage_table;
	loader_pool_snapshot_t *loader_pool;
	catalog_manager_body_snapshot_t *body_manager;
	catalog_manager_head_snapshot_t *head_manager;
	s32 active;
} external_scan_transaction_t;

static const catalog_entry_snapshot_t *externalScanPriorRow(
		const external_scan_transaction_t *transaction, const char *id)
{
	if (!transaction || !id || !id[0]) return NULL;
	for (s32 i = 0; i < transaction->row_count; i++) {
		const catalog_entry_snapshot_t *snapshot = &transaction->rows[i];
		if (snapshot->row.occupied && strcmp(snapshot->row.id, id) == 0) return snapshot;
	}
	return NULL;
}

static void externalScanTransactionDestroy(
		external_scan_transaction_t *transaction)
{
	if (!transaction) return;
	assetCatalogDestroyCustomModelSlotSnapshot(transaction->model_slots);
	assetCatalogDestroyCustomWeaponSlotSnapshot(transaction->weapon_slots);
	assetCatalogDestroyCustomBodyHeadSlotSnapshot(transaction->body_head_slots);
	assetCatalogDestroyCustomSoundSlotSnapshot(transaction->sound_slots);
	assetCatalogDestroyCustomTextureSlotSnapshot(transaction->texture_slots);
	assetCatalogDestroyCustomAnimSlotSnapshot(transaction->anim_slots);
	assetCatalogDestroyCustomStageSlotSnapshot(transaction->stage_slots);
	stageTableSnapshotDestroy(transaction->stage_table);
	loaderPoolSnapshotDestroy(transaction->loader_pool);
	catalogManagerBodySnapshotDestroy(transaction->body_manager);
	catalogManagerHeadSnapshotDestroy(transaction->head_manager);
	catalogDepSnapshotDestroy(&transaction->deps);
	free(transaction->rows);
	memset(transaction, 0, sizeof(*transaction));
}

static s32 externalScanTransactionCaptureSharedState(
		external_scan_transaction_t *transaction)
{
	return catalogDepSnapshotCreate(&transaction->deps)
		&& fileProviderCheckpointCreate(&transaction->provider)
		&& (transaction->weapon_slots = assetCatalogSnapshotCustomWeaponSlots())
		&& (transaction->body_head_slots = assetCatalogSnapshotCustomBodyHeadSlots())
		&& (transaction->model_slots = assetCatalogSnapshotCustomModelSlots())
		&& (transaction->sound_slots = assetCatalogSnapshotCustomSoundSlots())
		&& (transaction->texture_slots = assetCatalogSnapshotCustomTextureSlots())
		&& (transaction->anim_slots = assetCatalogSnapshotCustomAnimSlots())
		&& (transaction->stage_slots = assetCatalogSnapshotCustomStageSlots())
		&& (transaction->stage_table = stageTableSnapshotCreate())
		&& (transaction->loader_pool = loaderPoolSnapshotCreate())
		&& (transaction->body_manager = catalogManagerBodySnapshotCreate())
		&& (transaction->head_manager = catalogManagerHeadSnapshotCreate());
}

static s32 externalScanTransactionBegin(
		external_scan_transaction_t *transaction)
{
	if (!transaction) return 0;
	memset(transaction, 0, sizeof(*transaction));
	const u64 started = sysGetMicroseconds();
	if (!assetCatalogCaptureEntrySnapshots(&transaction->rows,
			&transaction->row_count)) return 0;
	const u64 rows_finished = sysGetMicroseconds();
	if (!externalScanTransactionCaptureSharedState(transaction)) {
		externalScanTransactionDestroy(transaction);
		return 0;
	}
	transaction->active = 1;
	sysLogPrintf(LOG_VERBOSE, "CATALOG.CHECKPOINT: rows=%d rows_us=%llu total_us=%llu",
		transaction->row_count, (unsigned long long)(rows_finished - started),
		(unsigned long long)(sysGetMicroseconds() - started));
	return 1;
}

/* A bundled head/body refresh can only change its existing owner and model
 * rows. It has fixed slots and publishes the loader record after its last
 * fallible catalog operation. Keep the general transaction for allocation and
 * creator admission paths. */
static s32 externalScanTransactionBeginExistingRows(
		external_scan_transaction_t *transaction, const asset_entry_t *const *rows,
		s32 count)
{
	const u64 started = sysGetMicroseconds();
	transaction->rows = malloc((size_t)count * sizeof(*transaction->rows));
	if (!transaction->rows) return 0;
	transaction->row_count = count;
	for (s32 i = 0; i < count; ++i) {
		catalogEntrySnapshotCapture(&transaction->rows[i], rows[i]);
	}
	if (!catalogDepSnapshotCreate(&transaction->deps)
			|| !fileProviderCheckpointCreate(&transaction->provider)) {
		externalScanTransactionDestroy(transaction);
		return 0;
	}
	transaction->existing_rows_only = 1;
	transaction->active = 1;
	sysLogPrintf(LOG_VERBOSE, "CATALOG.CHECKPOINT: scoped_rows=%d total_us=%llu",
		count, (unsigned long long)(sysGetMicroseconds() - started));
	return 1;
}

static s32 externalScanTransactionRollbackWeaponRows(
		const external_scan_transaction_t *transaction)
{
	s32 ok = 1;
	/* The boot walker holds the publication lock. Only this weapon could
	 * have appended rows since the checkpoint. */
	for (s32 i = assetCatalogGetPoolSize(); i-- > transaction->weapon_rows_begin;) {
		const asset_entry_t *row = assetCatalogGetByIndex(i);
		if (!row) continue;
		char id[CATALOG_ID_LEN];
		memcpy(id, row->id, sizeof(id));
		if (!assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
	}
	for (s32 i = transaction->row_count; i-- > 0;) {
		const catalog_entry_snapshot_t *prior = &transaction->rows[i];
		asset_entry_t *row = assetCatalogGetMutable(prior->row.id);
		if (!row || !catalogEntrySnapshotRestorePreserved(row, prior)) ok = 0;
	}
	return ok;
}

static s32 externalScanTransactionRollbackAllRows(
		const external_scan_transaction_t *transaction)
{
	s32 ok = 1;
	/* New rows are removed in reverse pool order. Replaced or retired rows are
	 * restored as metadata-only identities; their prior payload pointers were
	 * already retired and must be reacquired after the filesystem rolls back. */
	for (s32 i = assetCatalogGetPoolSize(); i-- > 0;) {
		const asset_entry_t *current = assetCatalogGetByIndex(i);
		asset_entry_t current_copy;
		const catalog_entry_snapshot_t *prior;
		if (!current) continue;
		current_copy = *current;
		prior = externalScanPriorRow(transaction, current_copy.id);
		if (!prior) {
			if (!assetCatalogRollbackUnactivatedRegistration(current_copy.id)) ok = 0;
		} else if (!catalogEntrySnapshotMatches(prior, current)) {
			asset_entry_t *mutable_row = assetCatalogGetMutable(current_copy.id);
			if (!mutable_row) ok = 0;
			else catalogActivationLedgerRestoreRetiredSnapshot(mutable_row, &prior->row);
		}
	}
	/* Scanner registration never intentionally deletes a prior row, but restore
	 * it defensively if a failed nested transaction did. */
	for (s32 i = 0; i < transaction->row_count; i++) {
		const asset_entry_t *prior = &transaction->rows[i].row;
		if (!prior->occupied || assetCatalogResolve(prior->id)) continue;
		asset_entry_t *row = assetCatalogRegister(prior->id, prior->type);
		if (!row) ok = 0;
		else catalogActivationLedgerRestoreRetiredSnapshot(row, prior);
	}
	return ok;
}

static s32 externalScanTransactionRollback(
		external_scan_transaction_t *transaction)
{
	s32 ok = 1;
	if (!transaction || !transaction->active) return 0;

	if (transaction->existing_rows_only) {
		for (s32 i = transaction->row_count; i-- > 0;) {
			const catalog_entry_snapshot_t *prior = &transaction->rows[i];
			asset_entry_t *row = assetCatalogGetMutable(prior->row.id);
			if (!row) ok = 0;
			else if (!catalogEntrySnapshotMatches(prior, row))
				catalogActivationLedgerRestoreRetiredSnapshot(row, &prior->row);
		}
		if (!catalogDepSnapshotRestore(&transaction->deps)) ok = 0;
		if (!fileProviderCheckpointRestore(&transaction->provider)) ok = 0;
		transaction->active = 0;
		return ok;
	}

	ok = transaction->weapon_rows_begin > 0
		? externalScanTransactionRollbackWeaponRows(transaction)
		: externalScanTransactionRollbackAllRows(transaction);
	if (!catalogDepSnapshotRestore(&transaction->deps)) ok = 0;
	if (!fileProviderCheckpointRestore(&transaction->provider)) ok = 0;
	if (!assetCatalogRestoreCustomWeaponSlots(transaction->weapon_slots)) ok = 0;
	if (!assetCatalogRestoreCustomBodyHeadSlots(transaction->body_head_slots)) ok = 0;
	if (!assetCatalogRestoreCustomModelSlots(transaction->model_slots)) ok = 0;
	if (!assetCatalogRestoreCustomSoundSlots(transaction->sound_slots)) ok = 0;
	if (!assetCatalogRestoreCustomTextureSlots(transaction->texture_slots)) ok = 0;
	if (!assetCatalogRestoreCustomAnimSlots(transaction->anim_slots)) ok = 0;
	if (!assetCatalogRestoreCustomStageSlots(transaction->stage_slots)) ok = 0;
	if (!stageTableSnapshotRestore(transaction->stage_table)) ok = 0;
	if (!loaderPoolSnapshotRestore(transaction->loader_pool)) ok = 0;
	if (!catalogManagerBodySnapshotRestore(transaction->body_manager)) ok = 0;
	if (!catalogManagerHeadSnapshotRestore(transaction->head_manager)) ok = 0;
	transaction->active = 0;
	return ok;
}

/* ========================================================================
 * INI Parser
 * ======================================================================== */

/**
 * Trim leading and trailing whitespace from a string in-place.
 * Returns pointer to first non-whitespace character.
 */
static char *trimWhitespace(char *str)
{
	/* leading */
	while (*str && isspace((u8)*str)) {
		str++;
	}

	if (*str == '\0') {
		return str;
	}

	/* trailing */
	char *end = str + strlen(str) - 1;
	while (end > str && isspace((u8)*end)) {
		*end = '\0';
		end--;
	}

	return str;
}

/* The parser itself is iniParseText (ini_parse.c), which has no log and no
 * catalog; this adds the log line. A text with no section header is not a
 * descriptor and was never logged. */
s32 iniParseBuffer(const char *label, const char *data, u32 len, ini_section_t *out)
{
	char why[160];

	if (!data || !out) {
		return 0;
	}

	why[0] = '\0';
	if (iniParseText(data, len, out, why, sizeof(why))) {
		return 1;
	}
	if (why[0]) {
		sysLogPrintf(LOG_ERROR, "assetcatalog_scanner: INI '%s' refused: %s",
			label ? label : "(memory)", why);
	}
	return 0;
}

s32 iniParse(const char *filepath, ini_section_t *out)
{
	FILE *fp = fopen(filepath, "rb");
	if (!fp) {
		return 0;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return 0;
	}

	long size = ftell(fp);
	if (size < 0) {
		fclose(fp);
		return 0;
	}
	rewind(fp);

	char *buf = (char *)malloc((size_t)size + 1);
	if (!buf) {
		fclose(fp);
		return 0;
	}

	size_t got = fread(buf, 1, (size_t)size, fp);
	fclose(fp);

	s32 ok = 0;
	if (got == (size_t)size) {
		ok = iniParseBuffer(filepath, buf, (u32)size, out);
	}

	free(buf);
	return ok;
}

static s32 iniWriteAppend(char *out, u32 out_cap, u32 *used, const char *text)
{
	if (!out || !used || !text) {
		return 0;
	}

	u32 len = (u32)strlen(text);
	if (*used + len >= out_cap) {
		return 0;
	}

	memcpy(out + *used, text, len);
	*used += len;
	out[*used] = '\0';
	return 1;
}

s32 iniWriteBuffer(const ini_section_t *ini, char *out, u32 out_cap, u32 *out_len)
{
	if (!ini || !ini->type[0] || !out || out_cap == 0) {
		return 0;
	}

	u32 used = 0;
	out[0] = '\0';

	char line[384];
	snprintf(line, sizeof(line), "[%s]\n", ini->type);
	if (!iniWriteAppend(out, out_cap, &used, line)) {
		return 0;
	}

	for (s32 i = 0; i < ini->count; i++) {
		snprintf(line, sizeof(line), "%s = %s\n",
			ini->pairs[i].key, ini->pairs[i].value);
		if (!iniWriteAppend(out, out_cap, &used, line)) {
			return 0;
		}
	}

	if (out_len) {
		*out_len = used;
	}
	return 1;
}

s32 iniWriteFile(const char *filepath, const ini_section_t *ini)
{
	if (!filepath || !filepath[0] || !ini) {
		return 0;
	}

	char buf[16384];
	u32 len = 0;
	if (!iniWriteBuffer(ini, buf, sizeof(buf), &len)) {
		return 0;
	}

	FILE *fp = fopen(filepath, "wb");
	if (!fp) {
		return 0;
	}

	size_t wrote = fwrite(buf, 1, len, fp);
	fclose(fp);
	return wrote == len ? 1 : 0;
}

const char *modiniTemplateForKind(const char *kind)
{
	if (!kind) {
		return NULL;
	}

	if (strcmp(kind, "weapon") == 0) {
		return
			"; weapon.ini - external weapon metadata\n"
			"; The folder name is the catalog id unless an importer overrides it.\n"
			"[weapon]\n"
			"; catalog_id = mod:weapon_catalog_name\n"
			"name = New Weapon\n"
			"dual_wieldable = 0\n"
			"; requirefeature = 0\n"
			"\n"
			"[models]\n"
			"model_file = model.gltf\n"
			"; lo_model_file = model_lod.gltf\n"
			"\n"
			"[animations]\n"
			"; equip_animation = equip.gltf\n"
			"; unequip_animation = unequip.gltf\n"
			"; pritosec_animation = primary_to_secondary.gltf\n"
			"; sectopri_animation = secondary_to_primary.gltf\n"
			"\n"
			"[gameplay]\n"
			"; ammo_type = default\n"
			"; fire_mode = semiauto\n"
			"; muzzle_z = 0.0\n";
	}

	if (strcmp(kind, "projectile") == 0) {
		return
			"; projectile.ini - physical projectile behavior metadata\n"
			"[projectile]\n"
			"; catalog_id = mod:projectile_id\n"
			"name = New Projectile\n"
			"model_file = model.gltf\n"
			"behavior_graph = behavior.graph.json\n"
			"; entity_ref = mod:deployed_entity\n"
			"\n"
			"[motion]\n"
			"; launch_speed = 0\n"
			"; gravity = 0\n"
			"; guidance = none\n"
			"; lifetime_ticks = 0\n"
			"\n"
			"[impact]\n"
			"; on_impact = detonate\n";
	}

	if (strcmp(kind, "entity") == 0) {
		return
			"; entity.ini - deployed/stuck behavior archetype metadata\n"
			"[entity]\n"
			"; catalog_id = mod:entity_id\n"
			"name = New Entity\n"
			"archetype = deployed_object\n"
			"model_file = model.gltf\n"
			"behavior_graph = behavior.graph.json\n"
			"\n"
			"[behavior]\n"
			"; owner_policy = owner_team\n"
			"; activation = armed\n"
			"; cleanup = owner_or_lifetime\n";
	}

	if (strcmp(kind, "material") == 0) {
		return
			"; material.ini - reusable render/surface material metadata\n"
			"[material]\n"
			"; catalog_id = mod:material_id\n"
			"name = New Material\n"
			"material_file = material.json\n"
			"; texture_archive = dependencies/assets/texture/texture.pdtexture\n"
			"; effect_archive = dependencies/assets/effect/effect.pdeffect\n";
	}

	if (strcmp(kind, "texture") == 0) {
		return
			"; texture.ini - reusable texture metadata\n"
			"[texture]\n"
			"; catalog_id = mod:texture_id\n"
			"name = New Texture\n"
			"file_path = texture.png\n"
			"; texture_file = texture.tga\n";
	}

	if (strcmp(kind, "skin") == 0) {
		return
			"; skin.ini - character or surface skin metadata\n"
			"[skin]\n"
			"; catalog_id = mod:skin_id\n"
			"name = New Skin\n"
			"; target = base:character\n"
			"texture_file = texture.tga\n"
			"; material_archive = dependencies/assets/material/material.pdmaterial\n";
	}

	if (strcmp(kind, "effect") == 0) {
		return
			"; effect.ini - reusable visual/feedback effect metadata\n"
			"[effect]\n"
			"; catalog_id = mod:effect_id\n"
			"name = New Effect\n"
			"effect_file = effect.graph.json\n"
			"timeline_file = timeline.json\n"
			"; texture_archive = dependencies/assets/texture/texture.pdtexture\n"
			"; audio_archive = dependencies/assets/audio/effect.pdsfx\n";
	}

	if (strcmp(kind, "prop") == 0) {
		return
			"; prop.ini - reusable spawnable prop metadata\n"
			"[prop]\n"
			"; catalog_id = mod:prop_id\n"
			"name = New Prop\n"
			"prop_key = object\n"
			"model_file = model.gltf\n"
			"health = 100\n"
			"; flags = 0\n"
			"; behavior_graph = behavior.graph.json\n";
	}

	if (strcmp(kind, "vehicle") == 0) {
		return
			"; vehicle.ini - reusable vehicle metadata\n"
			"[vehicle]\n"
			"; catalog_id = mod:vehicle_id\n"
			"name = New Vehicle\n"
			"model_file = model.gltf\n"
			"; physics_file = physics.json\n"
			"; behavior_graph = behavior.graph.json\n";
	}

	if (strcmp(kind, "mission") == 0) {
		return
			"; mission.ini - mission wrapper metadata\n"
			"[mission]\n"
			"; catalog_id = mod:mission_id\n"
			"name = New Mission\n"
			"; scenario_archive = dependencies/assets/scenario/mission.pdscenario\n"
			"mission_graph_file = mission.graph.json\n"
			"; objectives_file = objectives.json\n"
			"; briefing records live in the nested scenario setup.fields.json\n";
	}

	if (strcmp(kind, "head") == 0) {
		return
			"; head.ini - external multiplayer head metadata\n"
			"[head]\n"
			"; catalog_id = mod:head_id\n"
			"rig_class = human_male_neck_standard\n"
			"; requirefeature = 0\n"
			"\n"
			"[model]\n"
			"model_file = model.gltf\n"
			"; scale = 1.0\n"
			"; animscale = 1.0\n"
			"\n"
			"[dependencies]\n"
			"; deps = mod:shared_texture, mod:blink_animation\n";
	}

	if (strcmp(kind, "body") == 0) {
		return
			"; body.ini - external multiplayer body metadata\n"
			"[body]\n"
			"; catalog_id = mod:body_id\n"
			"display_name = New Body\n"
			"rig_class = human_male_neck_standard\n"
			"; name_langid = 0\n"
			"; requirefeature = 0\n"
			"\n"
			"[model]\n"
			"model_file = model.gltf\n"
			"; hand_model_file = hand.gltf\n"
			"; scale = 1.0\n"
			"; animscale = 1.0\n"
			"\n"
			"[dependencies]\n"
			"; deps = mod:walk_animation, mod:body_texture\n";
	}

	if (strcmp(kind, "arena") == 0) {
		return
			"; arena.ini - external map/arena metadata\n"
			"[arena]\n"
			"; catalog_id = mod:arena_id\n"
			"; name_langid = 0\n"
			"; requirefeature = 0\n"
			"\n"
			"[geometry]\n"
			"geometry_file = geometry.obj\n"
			"; collision_file = collision.obj\n"
			"; pads_file = pads.ini\n"
			"; setup_file = setup.ini\n"
			"\n"
			"[music]\n"
			"; music_file = audio/music/track/music.ini\n";
	}

	if (strcmp(kind, "scenario") == 0) {
		return
			"; scenario.ini - external scenario source metadata\n"
			"[scenario]\n"
			"; catalog_id = mod:scenario_id\n"
			"name = New Scenario\n"
			"mode = mp|solo\n"
			"\n"
			"[source]\n"
			"scene_file = scene.glb\n"
			"scene_format = GLB\n"
			"runtime_source_file = scene.glb\n"
			"; collision_file = collision.obj\n"
			"collision_fallback = override\n"
			"\n"
			"[setup]\n"
			"portals_file = portals.json\n"
			"pads_file = pads.json\n"
			"spawns_file = spawns.json\n"
			"volumes_file = volumes.json\n"
			"objects_file = objects.json\n"
			"setup_fields_file = setup.fields.json\n"
			"ai_lists_file = ai/ailists.json\n"
			"objectives_file = objectives.json\n"
			"navigation_file = navigation.ini\n"
			"waypoints_file = navigation/waypoints.json\n"
			"waygroups_file = navigation/waygroups.json\n"
			"covers_file = navigation/covers.json\n"
			"paths_file = navigation/paths.json\n"
			"level_graph_file = level.graph.json\n"
			"; rooms_file = rooms.obj\n";
	}

	if (strcmp(kind, "gamemode") == 0) {
		return
			"; gamemode.ini - Combat Simulator/custom rules metadata\n"
			"[gamemode]\n"
			"; catalog_id = mod:gamemode_id\n"
			"name = New Game Mode\n"
			"mode_key = custom\n"
			"min_players = 2\n"
			"max_players = 8\n"
			"team_based = 0\n"
			"rules_file = rules.json\n"
			"; scenario_tags = combat,classic\n"
			"; requirefeature = 0\n";
	}

	if (strcmp(kind, "botprofile") == 0 || strcmp(kind, "bot_profile") == 0) {
		return
			"; botprofile.ini - reusable bot skill/personality metadata\n"
			"[bot_profile]\n"
			"; catalog_id = mod:bot_profile_id\n"
			"type_key = general\n"
			"difficulty_key = normal\n"
			"profile_file = profile.json\n"
			"; target_body = base:body_id\n"
			"; name_langid = 0\n"
			"; requirefeature = 0\n"
			"; character_archive = dependencies/assets/character/bot.pdcharacter\n";
	}

	if (strcmp(kind, "hud") == 0) {
		return
			"; hud.ini - reusable gameplay HUD composition metadata\n"
			"[hud]\n"
			"; catalog_id = mod:hud_id\n"
			"name = New HUD\n"
			"hud_key = crosshair\n"
			"texture_file = texture.png\n"
			"; font_archive = dependencies/assets/font/body.pdfont\n";
	}

	if (strcmp(kind, "theme") == 0) {
		return
			"; theme.ini - menu/UI theme bundle metadata\n"
			"[theme]\n"
			"; catalog_id = mod:theme_id\n"
			"name = New Theme\n"
			"theme_file = theme.json\n"
			"; ui_archive = dependencies/assets/ui/chrome.pdui\n"
			"; font_archive = dependencies/assets/font/body.pdfont\n"
			"; audio_archive = dependencies/assets/audio/click.pdsfx\n"
			"; music_archive = dependencies/assets/music/menu.pdsong\n"
			"; effect_archive = dependencies/assets/effects/glow.pdeffect\n";
	}

	if (strcmp(kind, "animation") == 0) {
		return
			"; animation.ini - external animation metadata\n"
			"[animation]\n"
			"; catalog_id = mod:animation_id\n"
			"name = New Animation\n"
			"frame_count = 0\n"
			"; target_body = mod:body_id\n"
			"\n"
			"[source]\n"
			"animation_file = animation.gltf\n"
			"; category = weapon_animation\n";
	}

	if (strcmp(kind, "voice") == 0) {
		return
			"; voice.ini - external voice metadata\n"
			"[audio]\n"
			"name = New Voice Line\n"
			"audio_category = voice\n"
			"duration_ms = 0\n"
			"\n"
			"[source]\n"
			"; Voice authoring supports WAV or MP3.\n"
			"file_path = sample.wav\n"
			"; file_path = sample.mp3\n"
			"\n"
			"[voice]\n"
			"; actor = unknown\n"
			"; transcript =\n"
			"; language =\n"
			"; context =\n";
	}

	if (strcmp(kind, "music") == 0) {
		return
			"; music.ini - external music metadata\n"
			"[audio]\n"
			"name = New Music Track\n"
			"audio_category = music\n"
			"duration_ms = 0\n"
			"\n"
			"[source]\n"
			"; Music authoring supports OGG, MP3, or WAV.\n"
			"file_path = track.ogg\n"
			"; file_path = track.mp3\n"
			"; file_path = track.wav\n"
			"; midi_file = track.mid\n";
	}

	if (strcmp(kind, "sfx") == 0 || strcmp(kind, "audio") == 0) {
		return
			"; sound.ini - external SFX metadata\n"
			"[audio]\n"
			"name = New Audio\n"
			"audio_category = sfx\n"
			"duration_ms = 0\n"
			"\n"
			"[source]\n"
			"; SFX authoring uses WAV.\n"
			"file_path = sample.wav\n"
			"\n"
			"[voice]\n"
			"; actor = unknown\n"
			"; transcript =\n"
			"; language =\n"
			"; context =\n";
	}

	if (strcmp(kind, "ui") == 0) {
		return
			"; ui.ini - external UI texture metadata\n"
			"[ui]\n"
			"name = New UI Texture\n"
			"; catalog_id = mod:ui_texture\n"
			"\n"
			"[source]\n"
			"; UI texture authoring supports PNG or TGA.\n"
			"texture_file = texture.png\n"
			"; texture_file = texture.tga\n"
			"\n"
			"[nine_slice]\n"
			"; left = 0\n"
			"; right = 0\n"
			"; top = 0\n"
			"; bottom = 0\n"
			"; edge_mode = stretch\n"
			"; center_mode = stretch\n";
	}

	if (strcmp(kind, "font") == 0) {
		return
			"; font.ini - external UI font metadata\n"
			"[font]\n"
			"name = New Font\n"
			"; catalog_id = mod:font_name\n"
			"\n"
			"[source]\n"
			"; Font authoring supports TTF or OTF.\n"
			"font_file = font.ttf\n"
			"; font_file = font.otf\n"
			"\n"
			"[rendering]\n"
			"size_px = 24\n"
			"; oversample_v = 2\n"
			"; shadow = false\n"
			"; glow = false\n";
	}

	if (strcmp(kind, "lang") == 0 || strcmp(kind, "language") == 0) {
		return
			"; lang.ini - external UTF-8 language-bank metadata\n"
			"[lang]\n"
			"; LANGBANK_* slot to override or provide.\n"
			"bank_id = -1\n"
			"; locale = en\n"
			"\n"
			"[source]\n"
			"; strings.json is editable JSON with indexed text entries.\n"
			"strings_file = strings.json\n"
			"; strings = strings.json\n";
	}

	return NULL;
}

const char *iniGet(const ini_section_t *ini, const char *key, const char *defval)
{
	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) == 0) {
			return ini->pairs[i].value;
		}
	}
	return defval;
}

s32 iniGetInt(const ini_section_t *ini, const char *key, s32 defval)
{
	const char *val = iniGet(ini, key, NULL);
	if (!val) {
		return defval;
	}
	return (s32)strtol(val, NULL, 0);
}

f32 iniGetFloat(const ini_section_t *ini, const char *key, f32 defval)
{
	const char *val = iniGet(ini, key, NULL);
	if (!val) {
		return defval;
	}
	return strtof(val, NULL);
}

u8 catalogArenaLoadModeFromIni(const ini_section_t *ini)
{
	/* B-1235: arena.ini carries the mode's name, as the extractor and the
	 * mod author write it (romextract_pdarena.c). Read as a number it was 0
	 * for every name, so a mod arena made from a canvas template loaded as
	 * a playable one, mission scripts and all. A number is still read. */
	const char *mode = iniGet(ini, "load_mode", "");

	if (strcmp(mode, "ARENA_LOADMODE_CANVAS") == 0) {
		return ARENA_LOADMODE_CANVAS;
	}
	if (strcmp(mode, "ARENA_LOADMODE_PLAYABLE") == 0) {
		return ARENA_LOADMODE_PLAYABLE;
	}
	return (u8)iniGetInt(ini, "load_mode", ARENA_LOADMODE_PLAYABLE);
}

void catalogArenaDisplayNameFromIni(const ini_section_t *ini, char *out, size_t out_size)
{
	/* Forge F1: a mod level's own name. Base arenas carry no display_name
	 * and stay named by name_langid; a level saved by the mod author
	 * carries the name its author typed. */
	if (!out || out_size == 0) {
		return;
	}
	snprintf(out, out_size, "%s", iniGet(ini, "display_name", ""));
}

/* ========================================================================
 * Map Mode Parser
 * ======================================================================== */

/**
 * Parse a pipe-separated mode string from an INI "mode" key into a MAP_MODE_*
 * bitmask. Recognised tokens: "mp", "solo", "coop". Unknown tokens are ignored.
 * Returns 0 if val is NULL or empty (caller treats 0 as "all modes").
 *
 * Examples:
 *   "mp"         -> MAP_MODE_MP
 *   "solo"       -> MAP_MODE_SOLO
 *   "mp|solo"    -> MAP_MODE_MP | MAP_MODE_SOLO
 *   "mp|coop"    -> MAP_MODE_MP | MAP_MODE_COOP
 */
static s32 parseModeString(const char *val)
{
	if (!val || !val[0]) {
		return 0;
	}

	s32 mode = 0;

	/* work on a local copy since we mutate it */
	char buf[64];
	strncpy(buf, val, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *tok = buf;
	while (tok) {
		char *pipe = strchr(tok, '|');
		if (pipe) {
			*pipe = '\0';
		}

		char *t = trimWhitespace(tok);
		if (strcmp(t, "mp") == 0) {
			mode |= MAP_MODE_MP;
		} else if (strcmp(t, "solo") == 0) {
			mode |= MAP_MODE_SOLO;
		} else if (strcmp(t, "coop") == 0) {
			mode |= MAP_MODE_COOP;
		}

		tok = pipe ? pipe + 1 : NULL;
	}

	return mode;
}

/* ========================================================================
 * Category Handling
 * ======================================================================== */

/**
 * Map a category directory name to an asset type.
 */
static asset_type_e categoryToType(const char *dirname)
{
	if (strcmp(dirname, "maps") == 0)        return ASSET_MAP;
	if (strcmp(dirname, "characters") == 0)  return ASSET_CHARACTER;
	if (strcmp(dirname, "skins") == 0)       return ASSET_SKIN;
	if (strcmp(dirname, "bot_variants") == 0) return ASSET_BOT_VARIANT;
	if (strcmp(dirname, "weapons") == 0)     return ASSET_WEAPON;
	if (strcmp(dirname, "projectiles") == 0) return ASSET_PROJECTILE;
	if (strcmp(dirname, "entities") == 0)    return ASSET_ENTITY;
	if (strcmp(dirname, "materials") == 0)   return ASSET_MATERIAL;
	if (strcmp(dirname, "textures") == 0)    return ASSET_TEXTURES;
	if (strcmp(dirname, "texture") == 0)     return ASSET_TEXTURE;
	if (strcmp(dirname, "sfx") == 0)         return ASSET_SFX;
	if (strcmp(dirname, "music") == 0)       return ASSET_MUSIC;
	if (strcmp(dirname, "props") == 0)       return ASSET_PROP;
	if (strcmp(dirname, "vehicles") == 0)    return ASSET_VEHICLE;
	if (strcmp(dirname, "missions") == 0)    return ASSET_MISSION;
	if (strcmp(dirname, "ui") == 0)          return ASSET_UI;
	if (strcmp(dirname, "fonts") == 0)       return ASSET_FONT;
	if (strcmp(dirname, "tools") == 0)       return ASSET_TOOL;
	if (strcmp(dirname, "animations") == 0)  return ASSET_ANIMATION;
	if (strcmp(dirname, "hud") == 0)         return ASSET_HUD;
	if (strcmp(dirname, "gamemodes") == 0)   return ASSET_GAMEMODE;
	if (strcmp(dirname, "scenarios") == 0)   return ASSET_SCENARIO;
	if (strcmp(dirname, "audio") == 0)       return ASSET_AUDIO;
	if (strcmp(dirname, "lang_banks") == 0)  return ASSET_LANG;
	if (strcmp(dirname, "lang") == 0)        return ASSET_LANG;
	if (strcmp(dirname, "arenas") == 0)      return ASSET_ARENA;
	if (strcmp(dirname, "bodies") == 0)      return ASSET_BODY;
	if (strcmp(dirname, "heads") == 0)       return ASSET_HEAD;
	if (strcmp(dirname, "effects") == 0)     return ASSET_EFFECT;
	if (strcmp(dirname, "botprofiles") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(dirname, "bot_profiles") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(dirname, "themes") == 0)      return ASSET_THEME;
	if (strcmp(dirname, "ai") == 0)          return ASSET_AI_LISTS;
	return ASSET_NONE;
}

/**
 * Map an INI section type string to an asset type.
 */
static asset_type_e sectionToType(const char *section)
{
	if (strcmp(section, "map") == 0)          return ASSET_MAP;
	if (strcmp(section, "character") == 0)    return ASSET_CHARACTER;
	if (strcmp(section, "skin") == 0)         return ASSET_SKIN;
	if (strcmp(section, "bot_variant") == 0)  return ASSET_BOT_VARIANT;
	if (strcmp(section, "weapon") == 0)       return ASSET_WEAPON;
	if (strcmp(section, "projectile") == 0)   return ASSET_PROJECTILE;
	if (strcmp(section, "entity") == 0)       return ASSET_ENTITY;
	if (strcmp(section, "material") == 0)     return ASSET_MATERIAL;
	if (strcmp(section, "textures") == 0)     return ASSET_TEXTURES;
	if (strcmp(section, "sfx") == 0)          return ASSET_AUDIO;
	if (strcmp(section, "voice") == 0)        return ASSET_AUDIO;
	if (strcmp(section, "music") == 0)        return ASSET_AUDIO;
	if (strcmp(section, "prop") == 0)         return ASSET_PROP;
	if (strcmp(section, "vehicle") == 0)      return ASSET_VEHICLE;
	if (strcmp(section, "mission") == 0)      return ASSET_MISSION;
	if (strcmp(section, "ui") == 0)           return ASSET_UI;
	if (strcmp(section, "font") == 0)         return ASSET_FONT;
	if (strcmp(section, "tool") == 0)         return ASSET_TOOL;
	if (strcmp(section, "animation") == 0)    return ASSET_ANIMATION;
	if (strcmp(section, "hud") == 0)          return ASSET_HUD;
	if (strcmp(section, "gamemode") == 0)     return ASSET_GAMEMODE;
	if (strcmp(section, "scenario") == 0)     return ASSET_SCENARIO;
	if (strcmp(section, "audio") == 0)        return ASSET_AUDIO;
	if (strcmp(section, "texture") == 0)      return ASSET_TEXTURE;
	if (strcmp(section, "lang_bank") == 0)    return ASSET_LANG;
	if (strcmp(section, "lang") == 0)         return ASSET_LANG;
	if (strcmp(section, "arena") == 0)        return ASSET_ARENA;
	if (strcmp(section, "body") == 0)         return ASSET_BODY;
	if (strcmp(section, "head") == 0)         return ASSET_HEAD;
	if (strcmp(section, "mesh") == 0)         return ASSET_MODEL;
	if (strcmp(section, "model") == 0)        return ASSET_MODEL;
	if (strcmp(section, "effect") == 0)       return ASSET_EFFECT;
	if (strcmp(section, "botprofile") == 0)   return ASSET_BOT_PROFILE;
	if (strcmp(section, "bot_profile") == 0)  return ASSET_BOT_PROFILE;
	if (strcmp(section, "theme") == 0)        return ASSET_THEME;
	if (strcmp(section, "ai_lists") == 0)     return ASSET_AI_LISTS;
	return ASSET_NONE;
}

static s32 pathEndsWithNoCase(const char *s, const char *suffix)
{
	if (!s || !suffix) {
		return 0;
	}
	size_t n = strlen(s);
	size_t m = strlen(suffix);
	if (m > n) {
		return 0;
	}
	const char *tail = s + n - m;
	for (size_t i = 0; i < m; i++) {
		if (tolower((u8)tail[i]) != tolower((u8)suffix[i])) {
			return 0;
		}
	}
	return 1;
}

static asset_type_e typedPdContentTypeForPath(const char *path)
{
	return assetArchiveTypeForPath(path);
}

static const char *typedPdArchiveDescriptorLeaf(const char *path)
{
	return assetArchiveDescriptorForPath(path);
}

static const char *pathLeafAnySeparator(const char *path)
{
	const char *slash = path ? strrchr(path, '/') : NULL;
	const char *backslash = path ? strrchr(path, '\\') : NULL;
	if (backslash && (!slash || backslash > slash)) {
		slash = backslash;
	}
	return slash ? slash + 1 : path;
}

static void pathDirnameAnySeparator(const char *path, char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return;
	}
	out[0] = '\0';
	if (!path) {
		return;
	}

	const char *slash = strrchr(path, '/');
	const char *backslash = strrchr(path, '\\');
	if (backslash && (!slash || backslash > slash)) {
		slash = backslash;
	}
	if (!slash) {
		return;
	}

	size_t len = (size_t)(slash - path);
	if (len >= outsz) {
		len = outsz - 1;
	}
	memcpy(out, path, len);
	out[len] = '\0';
}

static s32 typedPdDescriptorComponentDir(const char *descriptor_path,
                                         char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return 0;
	}
	out[0] = '\0';
	if (!descriptor_path) {
		return 0;
	}

	char dir[FS_MAXPATH];
	pathDirnameAnySeparator(descriptor_path, dir, sizeof(dir));

	const char *leaf = pathLeafAnySeparator(descriptor_path);
	char stem[128];
	strncpy(stem, leaf ? leaf : "", sizeof(stem) - 1);
	stem[sizeof(stem) - 1] = '\0';
	char *dot = strrchr(stem, '.');
	if (dot) {
		*dot = '\0';
	}

	if (!stem[0]) {
		return assetPathCopyChecked(out, outsz, dir);
	} else if (dir[0]) {
		return assetPathJoinChecked(out, outsz, dir, "/", stem);
	} else {
		return assetPathCopyChecked(out, outsz, stem);
	}
}

/* ========================================================================
 * External Source Path Handling
 * ======================================================================== */

static s32 qualifyIniSourcePath(ini_section_t *ini, const char *key,
                                 const char *component_dir)
{
	if (!ini || !key || !component_dir || !component_dir[0]) {
		return 0;
	}

	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) {
			continue;
		}
		char full[sizeof(ini->pairs[i].value)];
		if (!assetPathQualifyFilesystemChecked(full, FS_MAXPATH, component_dir,
				ini->pairs[i].value) ||
				!assetPathCopyChecked(ini->pairs[i].value,
					sizeof(ini->pairs[i].value), full)) return 0;
	}
	return 1;
}

static s32 qualifyIniSourcePaths(ini_section_t *ini, const char *component_dir)
{
	static const char *keys[] = {
		"bodyfile",
		"headfile",
		"mesh_archive",
		"hand_archive",
		"body_archive",
		"head_archive",
		"portrait_file",
		"model_file",
		"model",
		"lo_model_file",
		"hand_model_file",
		"prop_file",
		"behavior_graph",
		"graph",
		"primary_graph",
		"secondary_graph",
		"settings_file",
		"settings",
		"variables_file",
		"variables",
		"shared_context_file",
		"shared_context",
		"presentation_file",
		"primary_projectile_archive",
		"deployed_entity_archive",
		"fire_sound_archive",
		"idle_animation_archive",
		"reticle_archive",
		"nested_payloads",
		"scene_file",
		"scene",
		"runtime_source_file",
		"geometry_file",
		"geometry",
		"blender_scene_file",
		"blender_scene",
		"visual_scene_file",
		"visual_scene",
		"visual_material_file",
		"visual_materials_file",
		"collision_file",
		"collision_source_file",
		"collision_source",
		"tiles_file",
		"portals_file",
		"pads_file",
		"spawns_file",
		"volumes_file",
		"objects_file",
		"setup_fields_file",
		"ai_lists_file",
		"setup_file",
		"mpsetup_file",
		"rooms_file",
		"rooms",
		"material_file",
		"props_file",
		"props",
		"objectives_file",
		"objectives",
		"navigation_file",
		"waypoints_file",
		"waygroups_file",
		"covers_file",
		"paths_file",
		"level_graph_file",
		"mission_graph_file",
		"scenario_archive",
		"rules_file",
		"profile_file",
		"visual_source_file",
		"texture_manifest_file",
		"music_file",
		"midi_file",
		"file_path",
		"subtitle_file",
		"locale_en_file",
		"locale_fr_file",
		"locale_de_file",
		"locale_it_file",
		"locale_es_file",
		"locale_ja_file",
		"effect_file",
		"timeline_file",
		"theme_file",
		"ui_archive",
		"font_archive",
		"audio_archive",
		"music_archive",
		"effect_archive",
		"animation_file",
		"commands_file",
		"material_archive",
		"texture_file",
		"layout_file",
		"nineslice_file",
		"swatches_file",
		"texture_archive",
		"texture",
		"physics_file",
		"font_file",
		"glyphs_file",
		"font",
		"strings_file",
		"strings",
		NULL
	};

	(void)keys; /* inventory lives in asset_path_contract.h */
	for (size_t i = 0; i < ASSET_PATH_SOURCE_KEY_COUNT; i++) {
		if (!qualifyIniSourcePath(ini, g_AssetPathSourceKeys[i], component_dir))
			return 0;
	}
	return 1;
}

static s32 archiveInnerPathIsSafe(const char *value)
{
	if (!value || !value[0]) {
		return 0;
	}
	if (strstr(value, "::")) {
		return 0;
	}
	if (value[0] == '/' || value[0] == '\\') {
		return 0;
	}
	if (isalpha((u8)value[0]) && value[1] == ':') {
		return 0;
	}
	const char *p = value;
	while (*p) {
		const char *end = p;
		while (*end && *end != '/' && *end != '\\') {
			end++;
		}
		if ((end - p) == 2 && p[0] == '.' && p[1] == '.') {
			return 0;
		}
		p = (*end) ? end + 1 : end;
	}
	return 1;
}

static s32 qualifyTypedArchiveSourcePath(ini_section_t *ini, const char *key,
                                          const char *archive_ref)
{
	if (!ini || !key || !archive_ref || !archive_ref[0]) {
		return 0;
	}

	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) {
			continue;
		}
		char full[sizeof(ini->pairs[i].value)];
		if (!assetPathQualifyArchiveChecked(full, FS_MAXPATH, archive_ref,
				ini->pairs[i].value) ||
				!assetPathCopyChecked(ini->pairs[i].value,
					sizeof(ini->pairs[i].value), full)) return 0;
	}
	return 1;
}

static s32 qualifyTypedArchiveSourcePaths(ini_section_t *ini,
                                           const char *archive_ref)
{
	static const char *keys[] = {
		"bodyfile",
		"headfile",
		"body_archive",
		"head_archive",
		"portrait_file",
		"model_file",
		"model",
		"lo_model_file",
		"hand_model_file",
		"mesh_archive",
		"hand_archive",
		"prop_file",
		"behavior_graph",
		"graph",
		"primary_graph",
		"secondary_graph",
		"settings_file",
		"settings",
		"variables_file",
		"variables",
		"shared_context_file",
		"shared_context",
		"presentation_file",
		"primary_projectile_archive",
		"deployed_entity_archive",
		"fire_sound_archive",
		"idle_animation_archive",
		"reticle_archive",
		"nested_payloads",
		"scene_file",
		"scene",
		"runtime_source_file",
		"geometry_file",
		"geometry",
		"blender_scene_file",
		"blender_scene",
		"visual_scene_file",
		"visual_scene",
		"visual_material_file",
		"visual_materials_file",
		"collision_file",
		"collision_source_file",
		"collision_source",
		"tiles_file",
		"portals_file",
		"pads_file",
		"spawns_file",
		"volumes_file",
		"objects_file",
		"setup_fields_file",
		"ai_lists_file",
		"setup_file",
		"mpsetup_file",
		"rooms_file",
		"rooms",
		"material_file",
		"props_file",
		"props",
		"objectives_file",
		"objectives",
		"navigation_file",
		"waypoints_file",
		"waygroups_file",
		"covers_file",
		"paths_file",
		"level_graph_file",
		"mission_graph_file",
		"scenario_archive",
		"rules_file",
		"profile_file",
		"visual_source_file",
		"texture_manifest_file",
		"music_file",
		"midi_file",
		"file_path",
		"subtitle_file",
		"locale_en_file",
		"locale_fr_file",
		"locale_de_file",
		"locale_it_file",
		"locale_es_file",
		"locale_ja_file",
		"effect_file",
		"timeline_file",
		"theme_file",
		"ui_archive",
		"font_archive",
		"audio_archive",
		"music_archive",
		"effect_archive",
		"animation_file",
		"commands_file",
		"material_archive",
		"texture_file",
		"layout_file",
		"nineslice_file",
		"swatches_file",
		"texture_archive",
		"texture",
		"physics_file",
		"font_file",
		"glyphs_file",
		"font",
		"strings_file",
		"strings",
		NULL
	};

	(void)keys;
	for (size_t i = 0; i < ASSET_PATH_SOURCE_KEY_COUNT; i++) {
		if (!qualifyTypedArchiveSourcePath(ini, g_AssetPathSourceKeys[i],
				archive_ref)) return 0;
	}
	return 1;
}

/* ========================================================================
 * Component Registration
 * ======================================================================== */

static s32 parseAudioCategoryValue(const char *value, s32 default_category)
{
	if (!value || !value[0]) {
		return default_category;
	}

	/* Numeric form: 0/1/2 */
	if (((u8)value[0] >= '0' && (u8)value[0] <= '9') || value[0] == '-' || value[0] == '+') {
		s32 n = (s32)strtol(value, NULL, 10);
		if (n >= AUDIO_CAT_SFX && n <= AUDIO_CAT_VOICE) {
			return n;
		}
		return default_category;
	}

	/* Text form: music/sfx/voice (+ aliases) */
	char lower[32];
	s32 i = 0;
	while (value[i] && i < (s32)sizeof(lower) - 1) {
		lower[i] = (char)tolower((u8)value[i]);
		i++;
	}
	lower[i] = '\0';

	if (strcmp(lower, "music") == 0 || strcmp(lower, "track") == 0) {
		return AUDIO_CAT_MUSIC;
	}
	if (strcmp(lower, "sfx") == 0 || strcmp(lower, "sound") == 0 || strcmp(lower, "soundfx") == 0) {
		return AUDIO_CAT_SFX;
	}
	if (strcmp(lower, "voice") == 0 || strcmp(lower, "dialog") == 0 || strcmp(lower, "dialogue") == 0) {
		return AUDIO_CAT_VOICE;
	}

	return default_category;
}

static s32 audioCategoryForSection(const ini_section_t *ini)
{
	if (ini && strcmp(ini->type, "voice") == 0) return AUDIO_CAT_VOICE;
	if (ini && strcmp(ini->type, "music") == 0) return AUDIO_CAT_MUSIC;
	return AUDIO_CAT_SFX;
}

static void lowerKey(const char *value, char *out, size_t out_n)
{
	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';
	if (!value) {
		return;
	}

	size_t i = 0;
	while (value[i] && i + 1 < out_n) {
		char c = (char)tolower((u8)value[i]);
		if (c == '-' || c == ' ') {
			c = '_';
		}
		out[i] = c;
		i++;
	}
	out[i] = '\0';
}

static s32 parseNamedIntValue(const char *value, s32 default_value,
                              const char *const *keys,
                              const s32 *values, s32 count)
{
	if (!value || !value[0]) {
		return default_value;
	}
	if (((u8)value[0] >= '0' && (u8)value[0] <= '9')
			|| value[0] == '-' || value[0] == '+') {
		return (s32)strtol(value, NULL, 10);
	}

	char lower[64];
	lowerKey(value, lower, sizeof(lower));
	for (s32 i = 0; i < count; i++) {
		if (strcmp(lower, keys[i]) == 0) {
			return values[i];
		}
	}
	return default_value;
}

static s32 parseModeKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"combat",
		"hold_the_briefcase",
		"hacker_central",
		"pop_a_cap",
		"king_of_the_hill",
		"capture_the_case",
	};
	static const s32 values[] = { 0, 1, 2, 3, 4, 5 };
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseBotTypeKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"general", "peace", "shield", "rocket", "kaze", "fist",
		"prey", "coward", "judge", "feud", "speed", "turtle", "venge",
	};
	static const s32 values[] = {
		BOTTYPE_GENERAL, BOTTYPE_PEACE, BOTTYPE_SHIELD, BOTTYPE_ROCKET,
		BOTTYPE_KAZE, BOTTYPE_FIST, BOTTYPE_PREY, BOTTYPE_COWARD,
		BOTTYPE_JUDGE, BOTTYPE_FEUD, BOTTYPE_SPEED, BOTTYPE_TURTLE,
		BOTTYPE_VENGE,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseBotDifficultyKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"meat", "easy", "normal", "hard", "perfect", "dark",
	};
	static const s32 values[] = {
		BOTDIFF_MEAT, BOTDIFF_EASY, BOTDIFF_NORMAL, BOTDIFF_HARD,
		BOTDIFF_PERFECT, BOTDIFF_DARK,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseHudElementKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"crosshair", "ammo", "radar", "health", "timer", "score",
	};
	static const s32 values[] = {
		HUD_ELEM_CROSSHAIR, HUD_ELEM_AMMO, HUD_ELEM_RADAR,
		HUD_ELEM_HEALTH, HUD_ELEM_TIMER, HUD_ELEM_SCORE,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parsePropKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"object", "door", "character", "weapon_pickup",
		"eyespy", "player", "explosion", "smoke",
	};
	static const s32 values[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseEffectTypeKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"tint", "glow", "shimmer", "darken", "screen", "particle",
	};
	static const s32 values[] = {
		EFFECT_TYPE_TINT, EFFECT_TYPE_GLOW, EFFECT_TYPE_SHIMMER,
		EFFECT_TYPE_DARKEN, EFFECT_TYPE_SCREEN, EFFECT_TYPE_PARTICLE,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseEffectTargetKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"scene", "player", "character", "prop", "weapon", "level",
	};
	static const s32 values[] = {
		EFFECT_TARGET_SCENE, EFFECT_TARGET_PLAYER, EFFECT_TARGET_CHR,
		EFFECT_TARGET_PROP, EFFECT_TARGET_WEAPON, EFFECT_TARGET_LEVEL,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

s32 assetCatalogVisitDeclaredDependencies(const char *deps,
	asset_catalog_declared_dep_fn visit, void *userdata)
{
	if (!visit) return 0;
	if (!deps || !deps[0]) return 1;
	char buf[ASSET_CATALOG_DECLARED_DEP_BYTES];
	strncpy(buf, deps, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *tok = buf;
	while (tok) {
		char *next = strpbrk(tok, ",|");
		if (next) {
			*next++ = '\0';
		}

		char *dep = trimWhitespace(tok);
		if (dep[0] && !visit(dep, userdata)) return 0;

		tok = next;
	}
	return 1;
}

typedef struct declared_dependency_owner {
	const char *id;
	s32 bundled;
} declared_dependency_owner_t;

static s32 registerDeclaredDependency(const char *id, void *userdata)
{
	const declared_dependency_owner_t *owner = userdata;
	catalogDepRegister(owner->id, id, owner->bundled);
	return 1;
}

static void registerDependencyList(const char *owner_id, const char *deps, s32 is_bundled)
{
	if (!owner_id || !owner_id[0]) return;
	declared_dependency_owner_t owner = {owner_id, is_bundled};
	assetCatalogVisitDeclaredDependencies(deps, registerDeclaredDependency, &owner);
}

static s32 registerAnimationCommandSource(const char *id,
                                          const char *source_path,
                                          loader_animation_source_batch_t *batch)
{
	char *json;
	u32 json_size = 0;

	if (!source_path || !source_path[0]) {
		return 0;
	}

	json = (char *)fsFileLoad(source_path, &json_size);
	if (!json || json_size == 0) {
		if (json) {
			free(json);
		}
		sysLogPrintf(LOG_WARNING,
			"assetcatalog_scanner: animation command source missing for '%s': %s",
			id ? id : "", source_path);
		return 0;
	}

	if (!loaderAnimationSourceBatchStage(batch, json, json_size, source_path, id)) {
		sysLogPrintf(LOG_WARNING,
			"assetcatalog_scanner: animation command source rejected for '%s': %s",
			id ? id : "", source_path);
		free(json);
		return 0;
	}

	sysLogPrintf(LOG_NOTE,
		"assetcatalog_scanner: animation command source staged for '%s': %s",
		id ? id : "", source_path);
	free(json);
	return 1;
}

static const char *findLastArchiveSeparator(const char *path)
{
	const char *last = NULL;
	const char *p = path;

	while (p && *p) {
		const char *sep = strstr(p, "::");
		if (!sep) {
			break;
		}
		last = sep;
		p = sep + 2;
	}

	return last;
}

/**
 * Register a single component from a parsed INI section.
 *
 * @param ini       Parsed INI section
 * @param dirpath   Absolute path to the component directory
 * @param mod_id    Mod identifier (e.g., "my_mod")
 * @return 1 on success, 0 on failure
 */

/* c3849 Wave 2: mint a private stagenum for a custom stage component that
 * authored no INI stagenum, and (client only) ensure an idempotent g_Stages
 * row exists so stageGetIndex/s_fillStageResult resolve it. File IDs are 0 (the fileids are u16; -1 would wrap to 65535 and pass the
 * fileid > 0 handle guard) so loading routes through the scenario-source path, never base ROM handles.
 * Returns the minted stagenum or -1 (already loudly logged). */
static s32 s_mintCustomStagenum(asset_entry_t *e, const char *mint_key)
{
	s32 stagenum = assetCatalogResolveStagenumPrivateSlot(mint_key);

	if (stagenum < 0) {
		return -1;
	}

	if (g_Stages != NULL) { /* dedicated server has no stage table */
		s32 idx = stageGetIndex(stagenum);

		if (idx < 0) {
			s32 template_idx = stageGetIndex(STAGE_MP_SKEDAR);

			if (template_idx >= 0) {
				struct stagetableentry tmpl = *stageGetEntry(template_idx);
				tmpl.id = (s16)stagenum;
				tmpl.bgfileid = 0;
				tmpl.tilefileid = 0;
				tmpl.padsfileid = 0;
				tmpl.setupfileid = 0;
				tmpl.mpsetupfileid = 0;
				idx = stageTableAppend(&tmpl);
			}
		}

		if (idx >= 0) {
			e->runtime_index = idx;
		}
	}

	return stagenum;
}

static const char *s_scenarioPrimarySource(const ini_section_t *ini,
	const char **out_scene, const char **out_rooms)
{
	const char *sf = iniGet(ini, "scene_file",
		iniGet(ini, "scene",
		iniGet(ini, "runtime_source_file",
		iniGet(ini, "blender_scene_file",
		iniGet(ini, "visual_scene_file", "")))));
	const char *rf = iniGet(ini, "rooms_file",
		iniGet(ini, "rooms",
		iniGet(ini, "geometry_file",
		iniGet(ini, "geometry", ""))));
	if (out_scene) *out_scene = sf;
	if (out_rooms) *out_rooms = rf;
	return sf[0] ? sf : rf;
}

static const char *s_bindScenarioSourceFields(asset_entry_t *e, const ini_section_t *ini)
{
	e->ext.scenario.mode = parseModeString(iniGet(ini, "mode", ""));
	e->ext.scenario.source_room_count = iniGetInt(ini,
		"source_room_count", 0);
	const char *sf, *rf;
	const char *primary = s_scenarioPrimarySource(ini, &sf, &rf);
	const char *cf = iniGet(ini, "collision_file",
		iniGet(ini, "collision_source_file",
		iniGet(ini, "collision_source", "")));
	strncpy(e->ext.scenario.scene_file, sf,
		sizeof(e->ext.scenario.scene_file) - 1);
	strncpy(e->ext.scenario.collision_file, cf,
		sizeof(e->ext.scenario.collision_file) - 1);
	strncpy(e->ext.scenario.rooms_file, rf,
		sizeof(e->ext.scenario.rooms_file) - 1);
	strncpy(e->ext.scenario.portals_file,
		iniGet(ini, "portals_file", ""),
		sizeof(e->ext.scenario.portals_file) - 1);
	strncpy(e->ext.scenario.pads_file,
		iniGet(ini, "pads_file", ""),
		sizeof(e->ext.scenario.pads_file) - 1);
	strncpy(e->ext.scenario.spawns_file,
		iniGet(ini, "spawns_file", ""),
		sizeof(e->ext.scenario.spawns_file) - 1);
	strncpy(e->ext.scenario.volumes_file,
		iniGet(ini, "volumes_file", ""),
		sizeof(e->ext.scenario.volumes_file) - 1);
	strncpy(e->ext.scenario.objects_file,
		iniGet(ini, "objects_file",
		iniGet(ini, "props_file", "")),
		sizeof(e->ext.scenario.objects_file) - 1);
	strncpy(e->ext.scenario.setup_fields_file,
		iniGet(ini, "setup_fields_file", ""),
		sizeof(e->ext.scenario.setup_fields_file) - 1);
	strncpy(e->ext.scenario.ai_lists_file,
		iniGet(ini, "ai_lists_file", ""),
		sizeof(e->ext.scenario.ai_lists_file) - 1);
	strncpy(e->ext.scenario.objectives_file,
		iniGet(ini, "objectives_file", ""),
		sizeof(e->ext.scenario.objectives_file) - 1);
	strncpy(e->ext.scenario.navigation_file,
		iniGet(ini, "navigation_file", ""),
		sizeof(e->ext.scenario.navigation_file) - 1);
	strncpy(e->ext.scenario.navigation_waypoints_file,
		iniGet(ini, "waypoints_file", ""),
		sizeof(e->ext.scenario.navigation_waypoints_file) - 1);
	strncpy(e->ext.scenario.navigation_waygroups_file,
		iniGet(ini, "waygroups_file", ""),
		sizeof(e->ext.scenario.navigation_waygroups_file) - 1);
	strncpy(e->ext.scenario.navigation_covers_file,
		iniGet(ini, "covers_file", ""),
		sizeof(e->ext.scenario.navigation_covers_file) - 1);
	strncpy(e->ext.scenario.navigation_paths_file,
		iniGet(ini, "paths_file", ""),
		sizeof(e->ext.scenario.navigation_paths_file) - 1);
	strncpy(e->ext.scenario.level_graph_file,
		iniGet(ini, "level_graph_file",
		iniGet(ini, "level_graph", "")),
		sizeof(e->ext.scenario.level_graph_file) - 1);
	return primary;
}

/* Shared public body/head transaction. Scanner and specialized walker both
 * enter here before registering or clearing the owner catalog row. */
typedef struct body_head_public_dependencies {
    char owner[CATALOG_ID_LEN];
    char mesh[CATALOG_ID_LEN], hand[CATALOG_ID_LEN];
    char declared[sizeof(((ini_pair_t *)0)->value)];
    s32 invalid;
} body_head_public_dependencies_t;

static s32 bodyHeadDeclaredDependencyContains(const char *list, const char *id)
{
    const char *p = list;
    while (p && *p) {
        const char *end = p;
        while (*end && *end != ',' && *end != '|') ++end;
        const char *a = p, *b = end;
        while (a < b && (*a == ' ' || *a == '\t')) ++a;
        while (b > a && (b[-1] == ' ' || b[-1] == '\t')) --b;
        if (strlen(id) == (size_t)(b - a) && !memcmp(a, id, (size_t)(b - a))) return 1;
        p = *end ? end + 1 : end;
    }
    return 0;
}

static s32 bodyHeadKeepDependency(const char *id, asset_type_e type, void *opaque)
{
    body_head_public_dependencies_t *deps = opaque;
    const asset_entry_t *row = assetCatalogResolveAny(id);
    if (!strcmp(id, deps->mesh) || !strcmp(id, deps->hand)) {
        if (type != ASSET_MODEL || !row || !row->occupied || row->type != ASSET_MODEL) deps->invalid = 1;
        return 1;
    }
    if (bodyHeadDeclaredDependencyContains(deps->declared, id)) {
        if (row && type != ASSET_NONE && row->type != type) deps->invalid = 1;
        return 1;
    }
    /* Preserve a reverse declaration only when its actual source still names
     * this owner, and its recorded expected type agrees with that source. */
    if (row && row->occupied &&
            ((row->type == ASSET_ANIMATION && !strcmp(row->ext.anim.target_body, deps->owner)) ||
             (row->type == ASSET_BOT_PROFILE && !strcmp(row->ext.bot_profile.target_body, deps->owner)))) {
        if (type != ASSET_NONE && type != row->type) deps->invalid = 1;
        return 1;
    }
    return 0;
}

static s32 bodyHeadRegisterDeclaredDependencies(const char *owner, const char *list,
    s32 bundled)
{
    char text[sizeof(((ini_pair_t *)0)->value)];
    if (!assetPathCopyChecked(text, sizeof(text), list)) return 0;
    char *p = text;
    while (p && *p) {
        char *end = strpbrk(p, ",|");
        if (end) *end++ = 0;
        char *id = trimWhitespace(p);
        const asset_entry_t *row = id[0] ? assetCatalogResolveAny(id) : NULL;
        if (id[0] && (!bodyHeadSourceCatalogIdValid(id) || !strcmp(owner, id) ||
                !catalogDepRegisterTyped(owner, id, row ? row->type : ASSET_NONE, bundled))) return 0;
        p = end;
    }
    return 1;
}

static s32 bodyHeadNativeSlotAvailable(asset_type_e type, const char *id, s32 slot)
{
    for (s32 i = 0; i < assetCatalogGetPoolSize(); ++i) {
        const asset_entry_t *row = assetCatalogGetByIndex(i);
        if (row && row->occupied && row->type == type && row->runtime_index == slot &&
                strcmp(row->id, id)) return 0;
    }
    return 1;
}

static s32 registerBodyHeadPublicIni(const ini_section_t *ini, const char *id,
    const char *dirpath, const char *descriptor_path, const char *category,
    s32 base_slot_hint, s32 force_bundled, s32 temporary,
    external_scan_transaction_t *retained_transaction, char *error, size_t error_capacity)
{
    body_head_source_t source;
    body_head_mesh_binding_t mesh = {0}, hand = {0};
    body_head_public_dependencies_t deps = {0};
    external_scan_transaction_t transaction = {0};
    asset_type_e type = !strcmp(ini->type, "head") ? ASSET_HEAD : ASSET_BODY;
    s32 is_head = type == ASSET_HEAD;
    s32 slot = -1, mesh_file = 0, hand_file = 0, ok = 0;
    const asset_entry_t *existing;
    asset_entry_t *entry;
    body_data_t body = {0};
    head_data_t head = {0};
    s32 bundled, enabled;
    if (error && error_capacity) error[0] = 0;
    if (!bodyHeadSourceParseIni(ini, id, is_head, &source, error, error_capacity)) return 0;
    bundled = force_bundled < 0 ? 0 : (force_bundled || source.bundled);
    enabled = source.enabled;
    if (!source.mesh_archive[0] || !dirpath || !descriptor_path ||
            strlen(dirpath) >= FS_MAXPATH || strlen(descriptor_path) >= FS_MAXPATH ||
            !category || strlen(category) >= CATALOG_CATEGORY_LEN ||
            !assetPathCopyChecked(deps.declared, sizeof(deps.declared), iniGet(ini, "deps", ""))) {
        if (error && error_capacity) snprintf(error, error_capacity, "missing/oversized public body/head source fields: %s", id);
        return 0;
    }
    if (!bodyHeadSourcePrepareMesh(source.mesh_archive, source.mesh_id, 1, &mesh, error, error_capacity) ||
            (source.hand_archive[0] && !bodyHeadSourcePrepareMesh(source.hand_archive,
                source.hand_id, 1, &hand, error, error_capacity))) return 0;
    if (!strcmp(id, mesh.id) || (hand.present && !strcmp(id, hand.id))) goto done;
    strcpy(deps.owner, id);
    strcpy(deps.mesh, mesh.id);
    if (hand.present) strcpy(deps.hand, hand.id);
    existing = assetCatalogResolveAny(id);
    if (existing && (!existing->occupied || existing->type != type)) goto done;
    if (existing && (!loaderWalkerMeshSourceChangeAllowed(existing->load_state,
            existing->bundled, existing->ref_count, existing->loaded_data != NULL, existing->payload_kind) ||
            (existing->runtime_index >= 0 && (is_head ? catalogManagerHeadIsModeldefLoaded(existing->runtime_index) :
                catalogManagerBodyIsModeldefLoaded(existing->runtime_index))))) {
        if (error && error_capacity) snprintf(error, error_capacity,
            "resident body/head must be retired before source admission: %s", id);
        goto done;
    }
    if (existing && existing->runtime_index >= 0) {
        slot = existing->runtime_index;
        if ((is_head ? existing->ext.head.headnum : existing->ext.body.bodynum) != slot) goto done;
    } else if (force_bundled > 0 && !strncmp(id, "base:", 5) &&
            base_slot_hint >= 0 && base_slot_hint < (is_head ? CATALOG_MGR_HEAD_COUNT : CATALOG_MGR_BODY_COUNT)) {
        slot = base_slot_hint;
    }
    if (slot >= (is_head ? CATALOG_MGR_HEAD_TOTAL : CATALOG_MGR_BODY_TOTAL) ||
            (slot >= 0 && !bodyHeadNativeSlotAvailable(type, id, slot))) goto done;
    const asset_entry_t *mesh_row = assetCatalogResolveAny(mesh.id);
    const asset_entry_t *hand_row = hand.present ? assetCatalogResolveAny(hand.id) : NULL;
    if (force_bundled > 0 && !retained_transaction && existing && existing->bundled &&
            existing->runtime_index >= 0 && mesh_row && mesh_row->bundled &&
            mesh.plan.source_filenum > 0 &&
            (!hand.present || (hand_row && hand_row->bundled && hand.plan.source_filenum > 0))) {
        const asset_entry_t *rows[] = { existing, mesh_row, hand_row };
        if (!externalScanTransactionBeginExistingRows(&transaction, rows,
                hand.present && hand_row != mesh_row ? 3 : 2)) goto done;
    } else if (!externalScanTransactionBegin(&transaction)) goto done;
    if (slot < 0) slot = is_head ? assetCatalogResolveHeadPrivateSlot(id) : assetCatalogResolveBodyPrivateSlot(id);
    if (slot < 0 || !bodyHeadNativeSlotAvailable(type, id, slot)) goto done;
    if (!catalogDepReserve(2 + ini->count)) goto done;
    /* Each call can grow the catalog pool. Never retain the owner pointer. */
    if (!bodyHeadSourceBindMesh(&mesh, bundled, temporary > 0, &mesh_file, error, error_capacity) ||
            (hand.present && !bodyHeadSourceBindMesh(&hand, bundled, temporary > 0, &hand_file, error, error_capacity))) goto done;
    if (hand.present) strcpy(source.hand_id, hand.id);
    if (is_head ? !bodyHeadSourceBuildHead(&source, slot, mesh_file, &head) :
            !bodyHeadSourceBuildBody(&source, slot, mesh_file, hand_file, &body)) goto done;
    existing = assetCatalogResolveAny(id);
    const asset_entry_t *model = assetCatalogResolve(mesh.id);
    if (!model || model->type != ASSET_MODEL || model->source.primary.provider != fileProvider()) goto done;
    asset_data_handle_t primary = model->source.primary;
    entry = existing ? assetCatalogGetMutable(id) : assetCatalogRegister(id, type);
    if (!entry) goto done;
    /* Keep existing selector indices, default-head relationship, and loaded
     * ownership. Only source-owned fields are published on the stable row. */
    if (!existing && !is_head) entry->ext.body.headnum = -1;
    strcpy(entry->category, category);
    strcpy(entry->dirpath, dirpath);
    strcpy(entry->descriptor_path, descriptor_path);
    entry->model_scale = source.model_scale;
    entry->enabled = enabled;
    entry->runtime_index = slot;
    entry->source_filenum = mesh_file;
    entry->bundled = bundled;
    entry->grid_offer_only = iniGet(ini, GRID_ARENA_PROVENANCE_KEY, NULL) != NULL;
    if (temporary >= 0) entry->temporary = !!temporary;
    if (is_head) {
        entry->ext.head.headnum = slot;
        entry->ext.head.requirefeature = source.requirefeature;
        strcpy(entry->ext.head.mesh_archive, source.mesh_archive);
        catalogSetHeadRigClass(entry, source.rig_class);
    } else {
        entry->ext.body.bodynum = slot;
        if (source.has_name_langid) entry->ext.body.name_langid = source.name_langid;
        entry->ext.body.requirefeature = source.requirefeature;
        strcpy(entry->ext.body.mesh_archive, source.mesh_archive);
        strcpy(entry->ext.body.hand_archive, source.hand_archive);
        if (source.has_display_name || !existing) catalogSetBodyDisplayName(entry, source.display_name);
        catalogSetBodyRigClass(entry, source.rig_class);
    }
    catalogSetPrimary(entry, primary);
    if (force_bundled > 0) loaderWalkerMarkBaseArchiveEntry(entry);
    if (!catalogDepRegisterTyped(id, mesh.id, ASSET_MODEL, bundled) ||
            (hand.present && !catalogDepRegisterTyped(id, hand.id, ASSET_MODEL, bundled)) ||
            !bodyHeadRegisterDeclaredDependencies(id, deps.declared, bundled)) goto done;
    catalogDepPruneOwner(id, bodyHeadKeepDependency, &deps);
    if (deps.invalid) goto done;
    if (is_head ? !loaderPoolInstallPublicHead(&head) : !loaderPoolInstallPublicBody(&body)) goto done;
    ok = 1;
done:
    if (!ok) {
        if (transaction.active && !externalScanTransactionRollback(&transaction))
            sysLoudFailf("BODYHEAD.SOURCE.ROLLBACK", "could not restore failed public source admission for %s", id);
        if (error && error_capacity && !error[0]) snprintf(error, error_capacity, "public body/head source admission failed: %s", id);
    }
    if (ok && retained_transaction) {
        *retained_transaction = transaction;
        memset(&transaction, 0, sizeof(transaction));
    }
    externalScanTransactionDestroy(&transaction);
    return ok;
}

/* Re-read the selected descriptor before admission: the general INI scanner
 * intentionally flattens sections and cannot be the strict body/head parser. */
static s32 registerBodyHeadPublicDescriptor(const char *descriptor_path,
    const char *expected_id, const char *fallback_dir, const char *default_category, s32 is_head,
    s32 temporary, external_scan_transaction_t *retained_transaction,
    char *error, size_t error_capacity)
{
    u32 size = 0;
    char *text = descriptor_path ? fsFileLoad(descriptor_path, &size) : NULL;
    ini_section_t *ini = calloc(1, sizeof(*ini));
    char root[FS_MAXPATH];
    const char *separator = "/", *last_archive = NULL, *p;
    s32 ok = 0;
    if (error && error_capacity) error[0] = 0;
    if (!bodyHeadSourceReadIniBytes(text, size, is_head ? "head" : "body", ini,
            error, error_capacity) || !assetPathCopyChecked(root, sizeof(root), descriptor_path)) goto done;
    for (p = root; (p = strstr(p, "::")) != NULL; p += 2) last_archive = p;
    char *leaf = root + (last_archive ? last_archive - root + 2 : 0);
    char *slash = strrchr(leaf, '/'), *backslash = strrchr(leaf, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    if (slash) *slash = 0;
    else if (last_archive) { root[last_archive - root] = 0; separator = "::"; }
    else if (!assetPathCopyChecked(root, sizeof(root), fallback_dir)) goto done;
    for (s32 i = 0; i < ini->count; ++i) {
        if (strcmp(ini->pairs[i].key, "mesh_archive") && strcmp(ini->pairs[i].key, "hand_archive")) continue;
        const char *value = ini->pairs[i].value;
        char qualified[FS_MAXPATH];
        if (!value[0]) continue;
        if (assetPathHasParentTraversal(value) ||
                (last_archive && assetPathIsAbsolute(value))) goto done;
        if (strstr(value, "::") || assetPathIsAbsolute(value)) {
            if (!assetPathCopyChecked(qualified, sizeof(qualified), value)) goto done;
        } else if (!assetPathJoinChecked(qualified, sizeof(qualified), root, separator, value)) goto done;
        if (!assetPathCopyChecked(ini->pairs[i].value, sizeof(ini->pairs[i].value), qualified)) goto done;
    }
    const char *id = iniGet(ini, "catalog_id", iniGet(ini, "id", ""));
    if (expected_id && strcmp(expected_id, id)) goto done;
    ok = registerBodyHeadPublicIni(ini, id, root, descriptor_path,
        iniGet(ini, "category", default_category), -1, temporary >= 0 ? -1 : 0,
        temporary, retained_transaction, error, error_capacity);
done:
    if (text) sysMemFree(text);
    free(ini);
    if (!ok && error && error_capacity && !error[0]) snprintf(error, error_capacity,
        "invalid selected public body/head descriptor: %s", descriptor_path ? descriptor_path : "");
    return ok;
}

typedef struct prepared_body_head_archive {
    ini_section_t ini;
    char descriptor_path[FS_MAXPATH];
    char id[CATALOG_ID_LEN];
    s32 base_slot_hint;
} prepared_body_head_archive_t;

static s32 bodyHeadArchiveSlotHint(const void *bytes, u32 size, s32 is_head)
{
    u32 manifest_size = 0;
    char *manifest = modArchiveExtractMemAlloc(bytes, size,
        ASSET_ARCHIVE_META_MANIFEST_PATH, &manifest_size);
    if (!manifest && modArchiveLastError() == MODARCHIVE_ERR_NOTFOUND) {
        manifest = modArchiveExtractMemAlloc(bytes, size,
            ASSET_ARCHIVE_META_MANIFEST, &manifest_size);
    }
    s64 hint = -1;
    loaderWalkerEnvelopeInt(manifest, manifest_size, is_head ? "headnum" : "bodynum", &hint);
    free(manifest);
    return hint >= 0 && hint < (is_head ? CATALOG_MGR_HEAD_COUNT : CATALOG_MGR_BODY_COUNT) ? (s32)hint : -1;
}

static s32 prepareBodyHeadArchive(const void *bytes, u32 size, const char *archive_path,
    s32 is_head, const char *expected_id, prepared_body_head_archive_t *prepared,
    char *error, size_t error_capacity)
{
    const char *member = is_head ? "head.ini" : "body.ini";
    u32 descriptor_size = 0;
    char *text = modArchiveExtractMemAlloc(bytes, size, member, &descriptor_size);
    s32 parsed = bodyHeadSourceReadIniBytes(text, descriptor_size, is_head ? "head" : "body",
        &prepared->ini, error, error_capacity);
    free(text);
    if (!parsed) return 0;

    const char *alias = iniGet(&prepared->ini, "id", NULL);
    const char *id = iniGet(&prepared->ini, "catalog_id", alias);
    if (!bodyHeadSourceCatalogIdValid(id) || (alias && strcmp(alias, id)) ||
            (expected_id && strcmp(expected_id, id))) {
        if (error && error_capacity) snprintf(error, error_capacity, "invalid public body/head catalog_id");
        return 0;
    }
    strcpy(prepared->id, id);

    if (assetArchiveValidateBytes(bytes, size, archive_path, ASSET_ARCHIVE_VALIDATE_RELEASE,
            error, error_capacity) != 0) return -1;
    if (!qualifyTypedArchiveSourcePaths(&prepared->ini, archive_path) ||
            !assetPathJoinChecked(prepared->descriptor_path, sizeof(prepared->descriptor_path),
                archive_path, "::", member)) return -1;
    return 1;
}

static s32 publishBodyHeadArchive(const prepared_body_head_archive_t *prepared,
    const char *archive_path, s32 bundled, char *error, size_t error_capacity)
{
    return registerBodyHeadPublicIni(&prepared->ini, prepared->id, archive_path,
        prepared->descriptor_path, bundled ? "base" : iniGet(&prepared->ini, "category", "custom"),
        prepared->base_slot_hint, bundled, -1, NULL, error, error_capacity);
}

/* The loose walker has already checked for a mounted replacement. Read the
 * selected disk file without touching the VFS cache from a boot worker. */
static void *readLooseBodyHeadArchive(const char *archive_path, u32 *size)
{
    char full_path[FS_MAXPATH + 1];
    FILE *file = fopen(fsFullPath(archive_path, full_path, sizeof(full_path)), "rb");
    if (!file) return NULL;
    void *bytes = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        if (length > 0 && (u64)length <= UINT32_MAX && fseek(file, 0, SEEK_SET) == 0) {
            bytes = malloc((size_t)length);
            if (bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length) {
                *size = (u32)length;
            } else {
                free(bytes);
                bytes = NULL;
            }
        }
    }
    fclose(file);
    return bytes;
}

static s32 prepareBodyHeadWalkerArchive(const char *archive_path, void **out)
{
    u32 size = 0;
    char error[256] = {0};
    s32 is_head = assetArchiveTypeForPath(archive_path) == ASSET_HEAD;
    void *bytes = readLooseBodyHeadArchive(archive_path, &size);
    prepared_body_head_archive_t *prepared = bytes ? malloc(sizeof(*prepared)) : NULL;
    s32 result = prepared ? prepareBodyHeadArchive(bytes, size, archive_path, is_head,
        NULL, prepared, error, sizeof(error)) : 0;
    if (result > 0) {
        prepared->base_slot_hint = bodyHeadArchiveSlotHint(bytes, size, is_head);
        *out = prepared;
    } else {
        if (result < 0) {
            sysLoudFailf("LOAD.BODYHEAD.PUBLIC_SOURCE", "%s: %s", prepared->id,
                error[0] ? error : "invalid public body/head archive path");
        } else {
            sysLoudFailf("LOAD.UNIVERSAL.PUBLIC_SOURCE", "invalid public descriptor in %s", archive_path);
        }
        free(prepared);
    }
    free(bytes);
    return result;
}

static s32 publishBodyHeadWalkerArchive(const void *data, const char *archive_path)
{
    const prepared_body_head_archive_t *prepared = data;
    char error[256];
    if (publishBodyHeadArchive(prepared, archive_path, 1, error, sizeof(error))) return 1;
    sysLoudFailf("LOAD.BODYHEAD.PUBLIC_SOURCE", "%s: %s", prepared->id, error);
    return -1;
}

const loader_walker_archive_preparation_t g_BodyHeadArchivePreparation = {
    prepareBodyHeadWalkerArchive,
    publishBodyHeadWalkerArchive,
};

s32 assetCatalogRegisterBodyHeadArchive(const char *archive_path, const char *expected_id,
    s32 is_head, s32 base_slot_hint, s32 bundled, char *error, size_t error_capacity)
{
    u32 size = 0;
    void *bytes = NULL;
    prepared_body_head_archive_t *prepared = NULL;
    s32 ok = 0;
    if (error && error_capacity) error[0] = 0;
    if (!archive_path || !archive_path[0] || !bodyHeadSourceCatalogIdValid(expected_id) ||
            assetArchiveTypeForPath(archive_path) != (is_head ? ASSET_HEAD : ASSET_BODY)) goto done;
    bytes = fsFileLoad(archive_path, &size);
    prepared = bytes ? malloc(sizeof(*prepared)) : NULL;
    if (!prepared || prepareBodyHeadArchive(bytes, size, archive_path, is_head, expected_id,
            prepared, error, error_capacity) <= 0) goto done;
    prepared->base_slot_hint = base_slot_hint;
    ok = publishBodyHeadArchive(prepared, archive_path, bundled, error, error_capacity);
done:
    free(prepared);
    if (bytes) sysMemFree(bytes);
    if (!ok && error && error_capacity && !error[0]) snprintf(error, error_capacity,
        "could not read selected public body/head archive: %s", archive_path ? archive_path : "");
    return ok;
}

void *assetCatalogPrepareBodyHeadDescriptor(const char *descriptor_path,
    const char *expected_id, const char *component_dir, const char *category,
    s32 is_head, s32 temporary, char *error, size_t error_capacity)
{
    if (error && error_capacity) error[0] = 0;
    external_scan_transaction_t *transaction = calloc(1, sizeof(*transaction));
    if (!transaction || !bodyHeadSourceCatalogIdValid(expected_id) ||
            !registerBodyHeadPublicDescriptor(descriptor_path, expected_id, component_dir,
                category, is_head, temporary, transaction, error, error_capacity)) {
        free(transaction);
        if (error && error_capacity && !error[0]) snprintf(error, error_capacity,
            "could not prepare public body/head descriptor: %s", expected_id ? expected_id : "");
        return NULL;
    }
    return transaction;
}

s32 assetCatalogFinishBodyHeadAdmission(void *admission, s32 commit)
{
    external_scan_transaction_t *transaction = admission;
    if (!transaction) return 1;
    s32 ok = commit || externalScanTransactionRollback(transaction);
    externalScanTransactionDestroy(transaction);
    free(transaction);
    return ok;
}

/*
 * The game finds a vehicle by its model number: the hoverbike code asks
 * assetRuntimeVehicleForModelnum for the binding whose runtime id is the
 * object's modelnum. A base vehicle is bound to its native model
 * (MODEL_HOVBIKE). A mod vehicle's embedded .pdmesh gets a private model slot
 * through the shared mesh binder, forced private because a vehicle saved from
 * the base hoverbike still names FILE_PHOVBIKE, and the vehicle row takes that
 * slot as its runtime index. A vehicle whose model is not an embedded typed
 * mesh (the typed example's loose glTF) stays unplaceable, as before.
 * A vehicle with a pd.vehicle_graph.v1 behavior graph is also checked here:
 * the graph must compile, and the parts it shows and hides must be toggle
 * parts of this model (read from the mesh's part and node tables), so a
 * broken graph refuses the vehicle when it registers, not when it is first
 * used.
 * Returns 1 when bound or when there is nothing to bind, 0 when rejected; a
 * rejection restores the catalog.
 */
static s32 registerVehicleModel(const char *vehicle_id, char *err, size_t err_cap)
{
	char id[CATALOG_ID_LEN];
	char model_path[FS_MAXPATH];
	char behavior_path[FS_MAXPATH];
	char graph_err[256];
	body_head_mesh_binding_t mesh;
	external_scan_transaction_t transaction;
	const asset_entry_t *vehicle;
	const asset_entry_t *model;
	asset_entry_t *row;
	s32 filenum = 0, temporary, ok = 0;

	if (err && err_cap) err[0] = '\0';
	vehicle = vehicle_id && vehicle_id[0] ? assetCatalogResolveAny(vehicle_id) : NULL;
	if (!vehicle || !vehicle->occupied || vehicle->type != ASSET_VEHICLE) {
		if (err && err_cap) snprintf(err, err_cap, "vehicle row is missing: %s",
			vehicle_id ? vehicle_id : "");
		return 0;
	}
	if (vehicle->bundled) return 1;
	if (assetArchiveTypeForPath(vehicle->ext.vehicle.model_file) != ASSET_MODEL) {
		sysLogPrintf(LOG_NOTE,
			"PDVEHICLE.MODEL: id=%s model=%s is not an embedded .pdmesh; "
			"the vehicle cannot be placed", vehicle->id, vehicle->ext.vehicle.model_file);
		return 1;
	}
	/* The catalog pool can grow below: keep copies, never the row pointer. */
	if (!assetPathCopyChecked(id, sizeof(id), vehicle->id)
			|| !assetPathCopyChecked(model_path, sizeof(model_path),
				vehicle->ext.vehicle.model_file)
			|| !assetPathCopyChecked(behavior_path, sizeof(behavior_path),
				vehicle->ext.vehicle.behavior_graph)) {
		if (err && err_cap) snprintf(err, err_cap, "vehicle model path exceeds capacity: %s",
			vehicle_id);
		return 0;
	}
	temporary = vehicle->temporary;
	memset(&mesh, 0, sizeof(mesh));
	if (!meshSourcePreparePrivate(model_path, NULL, &mesh, err, err_cap)) return 0;
	if (!externalScanTransactionBegin(&transaction)) {
		if (err && err_cap) snprintf(err, err_cap, "could not checkpoint the catalog for %s", id);
		return 0;
	}
	if (!catalogDepReserve(1)
			|| !bodyHeadSourceBindMesh(&mesh, 0, temporary, &filenum, err, err_cap)) goto done;
	model = assetCatalogResolve(mesh.id);
	if (!model || model->type != ASSET_MODEL
			|| model->runtime_index < MODEL_CUSTOM_START
			|| model->runtime_index >= MODEL_CUSTOM_END) {
		if (err && err_cap) snprintf(err, err_cap, "model %s has no private model slot", mesh.id);
		goto done;
	}
	row = assetCatalogGetMutable(id);
	if (!row || row->type != ASSET_VEHICLE) goto done;
	row->runtime_index = model->runtime_index;
	if (!catalogDepContains(id, mesh.id)
			&& !catalogDepRegisterTyped(id, mesh.id, ASSET_MODEL, 0)) goto done;
	if (!assetRuntimeVehicleGraphAdmits(model_path, behavior_path, id,
			graph_err, sizeof(graph_err))) {
		if (err && err_cap) snprintf(err, err_cap, "behavior graph refused: %s", graph_err);
		goto done;
	}
	sysLogPrintf(LOG_NOTE, "PDVEHICLE.MODEL: id=%s model=%s modelnum=%d filenum=%d",
		id, mesh.id, row->runtime_index, filenum);
	ok = 1;
done:
	if (!ok) {
		if (!externalScanTransactionRollback(&transaction))
			sysLoudFailf("PDVEHICLE.MODEL.ROLLBACK", "could not restore failed vehicle model admission for %s", id);
		if (err && err_cap && !err[0]) snprintf(err, err_cap, "vehicle model admission failed: %s", id);
	}
	externalScanTransactionDestroy(&transaction);
	return ok;
}

static s32 gridPrepareOriginalStandaloneMesh(const char *descriptor, const char *id,
	loader_walker_mesh_source_plan_t *plan, char *err, size_t err_cap);
static s32 gridBindStandaloneMesh(asset_entry_t *row, loader_walker_mesh_source_plan_t *plan,
	char *err, size_t err_cap);

static s32 registerComponentWithAnimationBatch(const ini_section_t *ini, const char *dirpath,
                              const char *mod_id, const char *descriptor_path,
                              loader_animation_source_batch_t *batch)
{
	enum { WEAPON_ID_UNASSIGNED = -1 };
	ini_section_t local_ini;
	if (ini) {
		local_ini = *ini;
		if (!qualifyIniSourcePaths(&local_ini, dirpath)) {
			sysLogPrintf(LOG_WARNING,
				"ASSET.PATH.REJECT: source path exceeds repository capacity in %s",
				dirpath ? dirpath : "<unknown>");
			return 0;
		}
		ini = &local_ini;
	}

	asset_type_e type = sectionToType(ini->type);
	if (type == ASSET_NONE) {
		sysLogPrintf(LOG_WARNING, "assetcatalog_scanner: unknown section type '%s' in %s",
			ini->type, dirpath);
		return 0;
	}
	/* T-ASSETS-024: these proposed binding files never acquired a production
	 * renderer or hand-animation consumer. Reject them instead of qualifying
	 * and silently dropping them. Nested .pdmesh owns rendered materials and
	 * hierarchy; the established weapon placement fields own held position. */
	if (type == ASSET_WEAPON &&
			(iniGet(ini, "material_slots_file", "")[0] ||
			 iniGet(ini, "grip_sockets_file", "")[0])) {
		sysLogPrintf(LOG_WARNING,
			"PDWEAPON.BINDINGS.REJECT: %s declares retired material_slots_file or grip_sockets_file",
			dirpath ? dirpath : "<unknown>");
		return 0;
	}

	/* Build the asset ID. The INI "name" field is display-only; the
	 * folder name is the default ID, and catalog_id/id can override it
	 * when canonical layouts have repeated leaf names such as animation
	 * categories using "idle" in more than one family. */
	const char *category = iniGet(ini, "category", mod_id);
	const char *explicit_id = iniGet(ini, "catalog_id", iniGet(ini, "id", ""));

	/* Extract folder name from dirpath (last path component) */
	const char *folder = strrchr(dirpath, '/');
	folder = folder ? folder + 1 : dirpath;

	/* Build ID: use category + folder name, or just folder if category matches */
	char idbuf[CATALOG_ID_LEN];
	if (type == ASSET_AUDIO && strlen(explicit_id[0] ? explicit_id : folder)
			>= sizeof(idbuf)) return 0;
	snprintf(idbuf, sizeof(idbuf), "%s", explicit_id[0] ? explicit_id : folder);

	s32 preserved_weapon_id = WEAPON_ID_UNASSIGNED;
	if (type == ASSET_BODY || type == ASSET_HEAD) {
		char source_error[256];
		s32 result = registerBodyHeadPublicDescriptor(descriptor_path, NULL, dirpath,
			mod_id, type == ASSET_HEAD, -1, NULL, source_error, sizeof(source_error));
		if (!result) sysLogPrintf(LOG_WARNING, "BODYHEAD.SOURCE.REJECT: %s: %s", idbuf, source_error);
		return result;
	}
	s32 preserved_runtime_index = -1;
	s16 preserved_mp_index = -1;
	u8 preserved_weapon_requirefeature = 0;
	s32 preserved_dual_wieldable = 0;
	char preserved_weapon_name[64];
	preserved_weapon_name[0] = '\0';
	if (type == ASSET_WEAPON) {
		const asset_entry_t *existing = assetCatalogResolve(idbuf);
		if (existing && existing->type == ASSET_WEAPON) {
			preserved_weapon_id = existing->ext.weapon.weapon_id;
			preserved_runtime_index = existing->runtime_index;
			preserved_mp_index = existing->mp_index;
			preserved_weapon_requirefeature = existing->ext.weapon.requirefeature;
			preserved_dual_wieldable = existing->ext.weapon.dual_wieldable;
			strncpy(preserved_weapon_name, existing->ext.weapon.name,
				sizeof(preserved_weapon_name) - 1);
			preserved_weapon_name[sizeof(preserved_weapon_name) - 1] = '\0';
		}
	}

	loader_walker_mesh_source_plan_t model_plan;
	s32 planned_model = 0;
	s32 native_model_slot = -1;
	s32 had_model_row = 0;
	char model_error[256] = {0};
	if (type == ASSET_MODEL && iniGetInt(ini, "enabled", 1) && !iniGetInt(ini, "bundled", 0)
			&& strcmp(iniGet(ini, GRID_ARENA_PROVENANCE_KEY, ""), GRID_ARENA_PROVENANCE_ORIGINAL) == 0) {
		planned_model = gridPrepareOriginalStandaloneMesh(descriptor_path, idbuf, &model_plan,
			model_error, sizeof(model_error));
		if (planned_model < 0) {
			sysLogPrintf(LOG_WARNING, "PDMESH.NATIVE.REJECT: id=%s reason=%s", idbuf, model_error);
			return 0;
		}
		if (planned_model) {
			const asset_entry_t *prior = assetCatalogResolveAny(idbuf);
			if (prior && (prior->type != ASSET_MODEL || prior->bundled
					|| !loaderWalkerMeshSourceChangeAllowed(prior->load_state, prior->bundled,
						prior->ref_count, prior->loaded_data != NULL, prior->payload_kind))) {
				sysLogPrintf(LOG_WARNING, "PDMESH.NATIVE.REJECT: id=%s already has an incompatible or resident source", idbuf);
				return 0;
			}
			had_model_row = prior != NULL;
		}
	}

	catalog_audio_source_identity_t audio_identity;
	catalog_audio_public_source_t audio_public;
	void *audio_slot_snapshot = NULL;
	if (type == ASSET_AUDIO) {
		s32 audio_category = parseAudioCategoryValue(
			iniGet(ini, "audio_category", iniGet(ini, "kind",
				iniGet(ini, "category", ""))), audioCategoryForSection(ini));
		if (!catalogAudioPublicSourceParse(ini, audio_category != AUDIO_CAT_MUSIC, &audio_public)) {
			sysLogPrintf(LOG_WARNING, "CATALOG.AUDIO.SOURCE.REJECT: invalid public controls %s", idbuf);
			return 0;
		}
		if (!catalogAudioSourceIdentityPrepare(assetCatalogResolve(idbuf),
				idbuf, audio_category, &audio_identity)) {
			sysLogPrintf(LOG_WARNING,
				"CATALOG.AUDIO.IDENTITY.REJECT: conflicting source identity %s", idbuf);
			return 0;
		}
		const char *audio_file = iniGet(ini, "file_path", "");
		const char *primary_file = audio_file[0] ? audio_file :
			iniGet(ini, "music_file", iniGet(ini, "midi_file", ""));
		if (!primary_file[0]) return 0;
		/* Reserve new private identity before touching a prior row. Older
		 * component/archive scanners do not all hold a folder transaction. */
		if (audio_identity.sound_id < 0 && audio_identity.source_soundnum < 0
				&& audio_identity.source_filenum < 0
				&& audio_category != AUDIO_CAT_MUSIC && primary_file[0]) {
			audio_slot_snapshot = assetCatalogSnapshotCustomSoundSlots();
			if (!audio_slot_snapshot) return 0;
			s32 slot = assetCatalogResolveSoundPrivateSlot(idbuf);
			if (slot < 0) {
				assetCatalogRestoreCustomSoundSlots(audio_slot_snapshot);
				assetCatalogDestroyCustomSoundSlotSnapshot(audio_slot_snapshot);
				return 0;
			}
			audio_identity.sound_id = audio_identity.source_soundnum = slot;
		}
	}

	/* Register the base entry */
	asset_entry_t *e = assetCatalogRegister(idbuf, type);
	if (!e) {
		if (audio_slot_snapshot) {
			assetCatalogRestoreCustomSoundSlots(audio_slot_snapshot);
			assetCatalogDestroyCustomSoundSlotSnapshot(audio_slot_snapshot);
		}
		sysLogPrintf(LOG_ERROR, "assetcatalog_scanner: failed to register '%s'", idbuf);
		return 0;
	}
	assetCatalogDestroyCustomSoundSlotSnapshot(audio_slot_snapshot);
	if (planned_model) {
		native_model_slot = gridBindStandaloneMesh(e, &model_plan, model_error, sizeof(model_error));
		if (native_model_slot < 0) {
			if (!had_model_row) assetCatalogUnregister(idbuf);
			sysLogPrintf(LOG_WARNING, "PDMESH.NATIVE.REJECT: id=%s reason=%s", idbuf, model_error);
			return 0;
		}
	}

	/* Common fields */
	strncpy(e->category, category, CATALOG_CATEGORY_LEN - 1);
	strncpy(e->dirpath, dirpath, FS_MAXPATH - 1);
	if (descriptor_path) {
		strncpy(e->descriptor_path, descriptor_path,
			sizeof(e->descriptor_path) - 1);
		e->descriptor_path[sizeof(e->descriptor_path) - 1] = '\0';
	}
	e->model_scale = iniGetFloat(ini, "model_scale", 1.0f);
	e->enabled = iniGetInt(ini, "enabled", 1);
	e->bundled = iniGetInt(ini, "bundled", 0);
	e->temporary = 0;
	/* Marked native archives travel only in an exact Grid source set. */
	e->grid_offer_only = iniGet(ini, GRID_ARENA_PROVENANCE_KEY, NULL) != NULL;
	e->runtime_index = native_model_slot; /* Other typed native indices are assigned below. */

	/* Type-specific fields */
	switch (type) {
	case ASSET_MAP:
		e->ext.map.stagenum = s_mintCustomStagenum(e, e->id); /* c3849 */
		e->ext.map.mode = parseModeString(iniGet(ini, "mode", ""));
		{
			const char *mf = iniGet(ini, "music_file", "");
			if (mf[0]) {
				strncpy(e->ext.map.music_file, mf, FS_MAXPATH - 1);
			}
		}
		break;

	case ASSET_CHARACTER:
		{
			if (!characterSourceApplyIni(e, ini)) {
				e->enabled = 0;
				sysLogPrintf(LOG_WARNING,
					"PDCHARACTER.SOURCE.REJECT: invalid head policy or dependencies in %s",
					dirpath ? dirpath : "<unknown>");
				return 0;
			}
			char dependency_error[256];
			char owner_id[CATALOG_ID_LEN];
			strcpy(owner_id, e->id);
			s32 dependency_result = assetCatalogRegisterCharacterDependencies(e, e->bundled,
				dependency_error, sizeof(dependency_error));
			/* Nested registration and rollback can both move the catalog pool. */
			e = assetCatalogGetMutable(owner_id);
			if (dependency_result < 0 || !e) {
				if (e) e->enabled = 0;
				sysLogPrintf(LOG_WARNING, "PDCHARACTER.DEPENDENCY.REJECT: %s: %s",
					owner_id, dependency_error);
				return 0;
			}
			const char *bf = e->ext.character.bodyfile;

			/* C-2-ext: resolve bodyfile basename to ROM filenum for reverse-index.
			 * INI path is like "files/Cbond_bodyZ" — basename matches fileSlots[n].name. */
			if (bf[0]) {
				const char *bn = strrchr(bf, '/');
				bn = bn ? bn + 1 : bf;
				s32 fnum = romdataFileGetNumForName(bn);
				if (fnum > 0) {
					e->source_filenum = fnum;
				}
				/* Asset Provider: mod characters are served by FileProvider
				 * from a loose file on disk. The bodyfile path is relative
				 * to the FS base dir (fsFileLoad resolves it). */
				catalogSetPrimaryFile(e, bf);
			}
		}
		break;

	case ASSET_SKIN:
		{
			const char *target = iniGet(ini, "target", "");
			const char *tf = iniGet(ini, "texture_file",
				iniGet(ini, "texture", ""));
			const char *sf = iniGet(ini, "skin_file",
				iniGet(ini, "file_path", ""));
			const char *swatches = iniGet(ini, "swatches_file", "");
			const char *material_archive = iniGet(ini, "material_archive", "");
			const char *texture_archive = iniGet(ini, "texture_archive", "");
			strncpy(e->ext.skin.target_id, target, CATALOG_ID_LEN - 1);
			strncpy(e->ext.skin.skin_file, sf, sizeof(e->ext.skin.skin_file) - 1);
			e->ext.skin.skin_file[sizeof(e->ext.skin.skin_file) - 1] = '\0';
			strncpy(e->ext.skin.texture_file, tf, sizeof(e->ext.skin.texture_file) - 1);
			e->ext.skin.texture_file[sizeof(e->ext.skin.texture_file) - 1] = '\0';
			strncpy(e->ext.skin.swatches_file, swatches,
				sizeof(e->ext.skin.swatches_file) - 1);
			e->ext.skin.swatches_file[
				sizeof(e->ext.skin.swatches_file) - 1] = '\0';
			strncpy(e->ext.skin.material_archive, material_archive,
				sizeof(e->ext.skin.material_archive) - 1);
			e->ext.skin.material_archive[
				sizeof(e->ext.skin.material_archive) - 1] = '\0';
			strncpy(e->ext.skin.texture_archive, texture_archive,
				sizeof(e->ext.skin.texture_archive) - 1);
			e->ext.skin.texture_archive[
				sizeof(e->ext.skin.texture_archive) - 1] = '\0';
			if (e->ext.skin.texture_file[0]) {
				catalogSetPrimaryFile(e, e->ext.skin.texture_file);
			} else if (e->ext.skin.skin_file[0]) {
				catalogSetPrimaryFile(e, e->ext.skin.skin_file);
			}
		}
		break;

	case ASSET_BOT_VARIANT:
		{
			const char *bt = iniGet(ini, "base_type", "NormalSim");
			strncpy(e->ext.bot_variant.base_type, bt, 31);
			e->ext.bot_variant.accuracy = iniGetFloat(ini, "accuracy", 0.5f);
			e->ext.bot_variant.reaction_time = iniGetFloat(ini, "reaction_time", 0.5f);
			e->ext.bot_variant.aggression = iniGetFloat(ini, "aggression", 0.5f);
		}
		break;

	case ASSET_ARENA:
		e->ext.arena.stagenum = -1;
		strncpy(e->ext.arena.scenario_id, iniGet(ini, "scenario", ""),
			sizeof(e->ext.arena.scenario_id) - 1);
		/* c3849: key the mint to the scenario so arena+scenario share
		 * one stagenum (FindModMapByStagenum pairing holds). */
		e->ext.arena.stagenum = s_mintCustomStagenum(e,
			e->ext.arena.scenario_id[0] ? e->ext.arena.scenario_id : e->id);
		strncpy(e->ext.arena.scenario_archive,
			iniGet(ini, "scenario_archive", ""),
			sizeof(e->ext.arena.scenario_archive) - 1);
		e->ext.arena.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
		e->ext.arena.name_langid = iniGetInt(ini, "name_langid", 0);
		e->ext.arena.load_mode = catalogArenaLoadModeFromIni(ini); /* B-1235: by name */
		catalogArenaDisplayNameFromIni(ini, e->ext.arena.display_name,
			sizeof(e->ext.arena.display_name));
		if (e->ext.arena.scenario_archive[0]) {
			catalogSetPrimaryFile(e, e->ext.arena.scenario_archive);
		}
		break;

	case ASSET_MODEL:
		{
			const char *pf = iniGet(ini, "model_file",
				iniGet(ini, "model",
				iniGet(ini, "geometry_file",
				iniGet(ini, "geometry",
				iniGet(ini, "file_path", "")))));
			if (pf[0] && native_model_slot < 0) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_WEAPON:
		e->ext.weapon.weapon_id = preserved_weapon_id;
		if (e->ext.weapon.weapon_id >= 0
				&& e->ext.weapon.weapon_id < NUM_MPWEAPONS) {
			e->mp_index = (s16)e->ext.weapon.weapon_id;
			e->runtime_index = catalogGetMpWeaponNum(e->ext.weapon.weapon_id);
		} else {
			s32 runtime_weapon_id = WEAPON_ID_UNASSIGNED;
			s32 mp_weapon_id = WEAPON_ID_UNASSIGNED;
			if (assetCatalogResolveWeaponPrivateSlots(idbuf, -1, 0,
					&runtime_weapon_id, &mp_weapon_id)) {
				e->ext.weapon.weapon_id = mp_weapon_id;
				e->mp_index = (s16)mp_weapon_id;
				e->runtime_index = runtime_weapon_id;
			} else {
				e->mp_index = preserved_mp_index;
				e->runtime_index = preserved_runtime_index;
			}
		}
		{
			const char *weapon_name = iniGet(ini, "name", "");
			strncpy(e->ext.weapon.name,
				weapon_name[0] ? weapon_name : preserved_weapon_name,
				sizeof(e->ext.weapon.name) - 1);
		}
		strncpy(e->ext.weapon.model_file, iniGet(ini, "model_file", ""), sizeof(e->ext.weapon.model_file) - 1);
		strncpy(e->ext.weapon.behavior_graph,
			iniGet(ini, "behavior_graph", iniGet(ini, "graph", "")),
			sizeof(e->ext.weapon.behavior_graph) - 1);
		strncpy(e->ext.weapon.primary_graph,
			iniGet(ini, "primary_graph", ""),
			sizeof(e->ext.weapon.primary_graph) - 1);
		strncpy(e->ext.weapon.secondary_graph,
			iniGet(ini, "secondary_graph", ""),
			sizeof(e->ext.weapon.secondary_graph) - 1);
		strncpy(e->ext.weapon.shared_context,
			iniGet(ini, "shared_context_file",
				iniGet(ini, "shared_context", "")),
			sizeof(e->ext.weapon.shared_context) - 1);
		strncpy(e->ext.weapon.settings_file,
			iniGet(ini, "settings_file", iniGet(ini, "settings", "")),
			sizeof(e->ext.weapon.settings_file) - 1);
		strncpy(e->ext.weapon.variables_file,
			iniGet(ini, "variables_file", iniGet(ini, "variables", "")),
			sizeof(e->ext.weapon.variables_file) - 1);
		/* c3849 Wave 5f: presentation fold-in. Field-for-field parity with
		 * loader_walker_weapon.c s_bindWeaponArchiveSourceMembers and
		 * netdistrib.c distribApplyIniToEntry -- keep all three in sync. */
		strncpy(e->ext.weapon.presentation_file,
			iniGet(ini, "presentation_file", iniGet(ini, "presentation", "")),
			sizeof(e->ext.weapon.presentation_file) - 1);
		if (e->ext.weapon.primary_graph[0]) {
			catalogSetPrimaryFile(e, e->ext.weapon.primary_graph);
		}
		/* S484 F9 / Mike I.2 (2026-04-27): damage/fire_rate/ammo_type
		 * shadow fields dropped from ext.weapon. The catalog manager
		 * is the single source of truth for those gameplay numbers
		 * (catalogManagerGetWeaponByIndex(weapon_num)->...). The mod
		 * INI parser ignores those keys silently if present. */
		e->ext.weapon.dual_wieldable = iniGetInt(ini, "dual_wieldable",
			preserved_dual_wieldable);
		e->ext.weapon.requirefeature = preserved_weapon_requirefeature;
		break;

	case ASSET_PROJECTILE:
		strncpy(e->ext.projectile.name, iniGet(ini, "name", ""), sizeof(e->ext.projectile.name) - 1);
		strncpy(e->ext.projectile.model_file, iniGet(ini, "model_file",
			iniGet(ini, "model", "")), sizeof(e->ext.projectile.model_file) - 1);
		strncpy(e->ext.projectile.behavior_graph, iniGet(ini, "behavior_graph",
			iniGet(ini, "graph", "")), sizeof(e->ext.projectile.behavior_graph) - 1);
		strncpy(e->ext.projectile.entity_ref, iniGet(ini, "entity_ref",
			iniGet(ini, "transition_entity", "")), sizeof(e->ext.projectile.entity_ref) - 1);
		if (e->ext.projectile.behavior_graph[0]) {
			catalogSetPrimaryFile(e, e->ext.projectile.behavior_graph);
		}
		break;

	case ASSET_ENTITY:
		strncpy(e->ext.entity.name, iniGet(ini, "name", ""), sizeof(e->ext.entity.name) - 1);
		strncpy(e->ext.entity.archetype, iniGet(ini, "archetype", ""), sizeof(e->ext.entity.archetype) - 1);
		strncpy(e->ext.entity.model_file, iniGet(ini, "model_file",
			iniGet(ini, "model", "")), sizeof(e->ext.entity.model_file) - 1);
		strncpy(e->ext.entity.behavior_graph, iniGet(ini, "behavior_graph",
			iniGet(ini, "graph", "")), sizeof(e->ext.entity.behavior_graph) - 1);
		if (e->ext.entity.behavior_graph[0]) {
			catalogSetPrimaryFile(e, e->ext.entity.behavior_graph);
		}
		break;

	case ASSET_PROP:
		e->ext.prop.prop_type = parsePropKeyValue(
			iniGet(ini, "prop_key", iniGet(ini, "prop_type", "")), 0);
		strncpy(e->ext.prop.name, iniGet(ini, "name", ""), sizeof(e->ext.prop.name) - 1);
		strncpy(e->ext.prop.prop_file, iniGet(ini, "prop_file",
			iniGet(ini, "file_path", "")), sizeof(e->ext.prop.prop_file) - 1);
		strncpy(e->ext.prop.model_file, iniGet(ini, "model_file", ""), sizeof(e->ext.prop.model_file) - 1);
		strncpy(e->ext.prop.behavior_graph,
			iniGet(ini, "behavior_graph", iniGet(ini, "graph", "")),
			sizeof(e->ext.prop.behavior_graph) - 1);
		if (e->ext.prop.model_file[0]) {
			catalogSetPrimaryFile(e, e->ext.prop.model_file);
		} else if (e->ext.prop.prop_file[0]) {
			catalogSetPrimaryFile(e, e->ext.prop.prop_file);
		}
		e->ext.prop.flags = (u32)iniGetInt(ini, "flags", 0);
		e->ext.prop.health = iniGetFloat(ini, "health", 100.0f);
		break;

	case ASSET_ANIMATION:
		e->ext.anim.anim_id = -1;
		e->source_animnum = -1;
		e->runtime_index = -1;
		if (assetCatalogAnimationCategoryUsesCharacterClip(e->category)) {
			/* c3849 Wave 2: custom anim with no base animnum gets a
			 * catalog-owned private slot. */
			s32 slot = assetCatalogResolveAnimPrivateSlot(e->id);
			if (slot >= 0) {
				e->ext.anim.anim_id = slot;
				e->source_animnum = slot;
				e->runtime_index = slot;
			}
		}
		strncpy(e->ext.anim.name, iniGet(ini, "name", ""), sizeof(e->ext.anim.name) - 1);
		e->ext.anim.frame_count = iniGetInt(ini, "frame_count", 0);
		e->ext.anim.bytes_per_frame = iniGetInt(ini, "bytes_per_frame", 0);
		e->ext.anim.header_len = iniGetInt(ini, "header_len", 0);
		e->ext.anim.framelen = iniGetInt(ini, "framelen", 0);
		e->ext.anim.flags = iniGetInt(ini, "flags", 0);
		strncpy(e->ext.anim.target_body, iniGet(ini, "target_body", ""), sizeof(e->ext.anim.target_body) - 1);
		{
			const char *af = iniGet(ini, "animation_file",
				iniGet(ini, "commands_file", iniGet(ini, "file_path", "")));
			if (af[0]) {
				catalogSetPrimaryFile(e, af);
			}
			const char *cf = iniGet(ini, "commands_file", "");
			if (cf[0] && !registerAnimationCommandSource(e->id, cf, batch)) {
				/* The public command source is the runtime source. Keeping a
				 * catalog row alive after it fails to parse would silently expose
				 * an unusable animation or let a legacy pool entry win. */
				e->enabled = 0;
				e->load_state = ASSET_STATE_REGISTERED;
				return 0;
			}
		}
		break;

	case ASSET_TEXTURE:
		e->ext.texture.texture_id = -1;
		if (e->ext.texture.texture_id >= 0) {
			e->source_texnum = e->ext.texture.texture_id;
		} else {
			/* c3849 Wave 2: a net-new custom texture (no base texnum) gets a
			 * catalog-owned private texnum so modeldef texconfigs can reach
			 * it. texture_id stays -1 ("no base ROM texnum"); consumers read
			 * source_texnum. Exhaustion already logged loud. */
			s32 slot = assetCatalogResolveTexturePrivateSlot(e->id);
			if (slot >= 0) {
				e->source_texnum = slot;
				e->runtime_index = slot;
			}
		}
		e->ext.texture.width = iniGetInt(ini, "width", 0);
		e->ext.texture.height = iniGetInt(ini, "height", 0);
		e->ext.texture.format = iniGetInt(ini, "format", 0);
		strncpy(e->ext.texture.file_path, iniGet(ini, "file_path",
			iniGet(ini, "texture_file", "")), sizeof(e->ext.texture.file_path) - 1);
		if (e->ext.texture.file_path[0]) {
			catalogSetPrimaryFile(e, e->ext.texture.file_path);
		}
		break;

	case ASSET_MATERIAL:
		{
			const char *pf = iniGet(ini, "material_file",
				iniGet(ini, "file_path", ""));
			strncpy(e->ext.material.material_file,
				iniGet(ini, "material_file", iniGet(ini, "file_path", "")),
				sizeof(e->ext.material.material_file) - 1);
			strncpy(e->ext.material.texture_archive,
				iniGet(ini, "texture_archive", ""),
				sizeof(e->ext.material.texture_archive) - 1);
			strncpy(e->ext.material.effect_archive,
				iniGet(ini, "effect_archive", ""),
				sizeof(e->ext.material.effect_archive) - 1);
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_GAMEMODE:
		e->ext.gamemode.mode_id = parseModeKeyValue(
			iniGet(ini, "mode_key", iniGet(ini, "mode_id", "")), -1);
		strncpy(e->ext.gamemode.name, iniGet(ini, "name", ""), sizeof(e->ext.gamemode.name) - 1);
		strncpy(e->ext.gamemode.description, iniGet(ini, "description", ""), sizeof(e->ext.gamemode.description) - 1);
		e->ext.gamemode.min_players = iniGetInt(ini, "min_players", 2);
		e->ext.gamemode.max_players = iniGetInt(ini, "max_players", 4);
		e->ext.gamemode.team_based = iniGetInt(ini, "team_based", 0);
		e->ext.gamemode.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
		{
			const char *rf = iniGet(ini, "rules_file",
				iniGet(ini, "file_path", ""));
			strncpy(e->ext.gamemode.rules_file, rf,
				sizeof(e->ext.gamemode.rules_file) - 1);
			if (rf[0]) {
				catalogSetPrimaryFile(e, rf);
			}
		}
		break;

	case ASSET_SCENARIO: {
		e->ext.scenario.stagenum = s_mintCustomStagenum(e, e->id); /* c3849 */
		const char *primary = s_bindScenarioSourceFields(e, ini);
		if (primary[0]) catalogSetPrimaryFile(e, primary);
		break;
	}

	case ASSET_AUDIO:
		if (!catalogAudioSourceIdentityApply(e, &audio_identity)) return 0;
		strncpy(e->ext.audio.name, iniGet(ini, "name", ""), sizeof(e->ext.audio.name) - 1);
		e->ext.audio.duration_ms = iniGetInt(ini, "duration_ms", 0);
		catalogAudioPublicSourceApply(e, &audio_public);

		{
			const char *audio_file = iniGet(ini, "file_path", "");
			const char *primary_file = audio_file[0] ? audio_file :
				iniGet(ini, "music_file", iniGet(ini, "midi_file", ""));
			strncpy(e->ext.audio.file_path, audio_file,
				sizeof(e->ext.audio.file_path) - 1);
			if (primary_file[0]) {
				catalogSetPrimaryFile(e, primary_file);
			}
			/* Private/native source identity was prepared before registration. */
		}
		break;

	case ASSET_HUD:
		e->ext.hud.element_type = parseHudElementKeyValue(
			iniGet(ini, "hud_key",
				iniGet(ini, "element",
				iniGet(ini, "element_type", ""))),
			HUD_ELEM_CROSSHAIR);
		e->ext.hud.hud_id = iniGetInt(ini, "hud_id",
			e->ext.hud.element_type);
		strncpy(e->ext.hud.name, iniGet(ini, "name", ""), sizeof(e->ext.hud.name) - 1);
		strncpy(e->ext.hud.texture_file, iniGet(ini, "texture_file", ""), sizeof(e->ext.hud.texture_file) - 1);
		strncpy(e->ext.hud.layout_file, iniGet(ini, "layout_file",
			iniGet(ini, "file_path", "")), sizeof(e->ext.hud.layout_file) - 1);
		if (e->ext.hud.layout_file[0]) {
			catalogSetPrimaryFile(e, e->ext.hud.layout_file);
		}
		break;

	case ASSET_EFFECT:
		strncpy(e->ext.effect.name, iniGet(ini, "name", ""),
			sizeof(e->ext.effect.name) - 1);
		e->ext.effect.effect_type = parseEffectTypeKeyValue(
			iniGet(ini, "effect_key", iniGet(ini, "effect_type", "")),
			EFFECT_TYPE_PARTICLE);
		e->ext.effect.target = parseEffectTargetKeyValue(
			iniGet(ini, "target_key", iniGet(ini, "target", "")),
			EFFECT_TARGET_SCENE);
		strncpy(e->ext.effect.effect_file, iniGet(ini, "effect_file",
			iniGet(ini, "behavior_graph",
			iniGet(ini, "file_path", ""))),
			sizeof(e->ext.effect.effect_file) - 1);
		strncpy(e->ext.effect.timeline_file, iniGet(ini, "timeline_file",
			iniGet(ini, "timeline", "")),
			sizeof(e->ext.effect.timeline_file) - 1);
		strncpy(e->ext.effect.shader_id, iniGet(ini, "shader_id", ""),
			sizeof(e->ext.effect.shader_id) - 1);
		e->ext.effect.intensity = iniGetFloat(ini, "intensity", 1.0f);
		{
			const char *pf = e->ext.effect.effect_file;
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			} else if (e->ext.effect.timeline_file[0]) {
				catalogSetPrimaryFile(e, e->ext.effect.timeline_file);
			}
		}
		break;

	case ASSET_UI:
		{
			const char *pf = iniGet(ini, "file_path",
				iniGet(ini, "texture_file",
				iniGet(ini, "texture",
				iniGet(ini, "ui_file", ""))));
			const char *lf = iniGet(ini, "layout_file", "");
			const char *nf = iniGet(ini, "nineslice_file", "");
			const char *tn = iniGet(ini, "texture_name", "");
			strncpy(e->ext.ui.texture_file, pf,
				sizeof(e->ext.ui.texture_file) - 1);
			e->ext.ui.texture_file[sizeof(e->ext.ui.texture_file) - 1] = '\0';
			strncpy(e->ext.ui.layout_file, lf,
				sizeof(e->ext.ui.layout_file) - 1);
			e->ext.ui.layout_file[sizeof(e->ext.ui.layout_file) - 1] = '\0';
			strncpy(e->ext.ui.nineslice_file, nf,
				sizeof(e->ext.ui.nineslice_file) - 1);
			e->ext.ui.nineslice_file[sizeof(e->ext.ui.nineslice_file) - 1] = '\0';
			strncpy(e->ext.ui.texture_name, tn,
				sizeof(e->ext.ui.texture_name) - 1);
			e->ext.ui.texture_name[sizeof(e->ext.ui.texture_name) - 1] = '\0';
			e->ext.ui.width = iniGetInt(ini, "width", 0);
			e->ext.ui.height = iniGetInt(ini, "height", 0);
			e->ext.ui.data_size = iniGetInt(ini, "data_size", 0);
			e->ext.ui.nineslice_left = iniGetInt(ini, "nineslice_left", 0);
			e->ext.ui.nineslice_right = iniGetInt(ini, "nineslice_right", 0);
			e->ext.ui.nineslice_top = iniGetInt(ini, "nineslice_top", 0);
			e->ext.ui.nineslice_bottom = iniGetInt(ini, "nineslice_bottom", 0);
			strncpy(e->ext.ui.nineslice_edge_mode,
				iniGet(ini, "nineslice_edge_mode", ""),
				sizeof(e->ext.ui.nineslice_edge_mode) - 1);
			e->ext.ui.nineslice_edge_mode[
				sizeof(e->ext.ui.nineslice_edge_mode) - 1] = '\0';
			strncpy(e->ext.ui.nineslice_center_mode,
				iniGet(ini, "nineslice_center_mode", ""),
				sizeof(e->ext.ui.nineslice_center_mode) - 1);
			e->ext.ui.nineslice_center_mode[
				sizeof(e->ext.ui.nineslice_center_mode) - 1] = '\0';
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_FONT:
		{
			const char *pf = iniGet(ini, "font_file",
				iniGet(ini, "glyphs_file",
				iniGet(ini, "file_path",
				iniGet(ini, "font", ""))));
			const char *mf = iniGet(ini, "metrics_file", "");
			strncpy(e->ext.font.font_file, pf,
				sizeof(e->ext.font.font_file) - 1);
			strncpy(e->ext.font.metrics_file, mf,
				sizeof(e->ext.font.metrics_file) - 1);
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_LANG:
		/* bank_id: the LANGBANK_* slot this mod lang bank occupies.
		 * Mod declares an integer bank_id/source_bank (0-68) in its component INI. */
		e->ext.lang.bank_id = iniGetInt(ini, "bank_id",
			iniGetInt(ini, "source_bank", -1));
		{
			const char *locale = iniGet(ini, "locale", "");
			const char *category = iniGet(ini, "category", "");
			const char *sf = iniGet(ini, "strings_file",
				iniGet(ini, "strings",
				iniGet(ini, "file_path", "")));
			strncpy(e->ext.lang.locale, locale, sizeof(e->ext.lang.locale) - 1);
			e->ext.lang.locale[sizeof(e->ext.lang.locale) - 1] = '\0';
			strncpy(e->ext.lang.lang_category, category,
				sizeof(e->ext.lang.lang_category) - 1);
			e->ext.lang.lang_category[sizeof(e->ext.lang.lang_category) - 1] = '\0';
			const char *count_text = iniGet(ini, "string_count", NULL);
			e->ext.lang.string_count_declared = count_text != NULL;
			e->ext.lang.string_count = 0;
			if (count_text && !langSourceParseCount(count_text, &e->ext.lang.string_count)) {
				/* Keep invalid presence visible until activation rejects it. */
				e->ext.lang.string_count = UINT32_MAX;
			}
			strncpy(e->ext.lang.strings_file, sf, sizeof(e->ext.lang.strings_file) - 1);
			e->ext.lang.strings_file[sizeof(e->ext.lang.strings_file) - 1] = '\0';
			if (sf[0]) {
				catalogSetPrimaryFile(e, sf);
			}
		}
		break;

	case ASSET_BOT_PROFILE:
		e->ext.bot_profile.type = parseBotTypeKeyValue(
			iniGet(ini, "type_key", iniGet(ini, "type", "")),
			BOTTYPE_GENERAL);
		e->ext.bot_profile.difficulty = parseBotDifficultyKeyValue(
			iniGet(ini, "difficulty_key", iniGet(ini, "difficulty", "")),
			BOTDIFF_NORMAL);
		e->ext.bot_profile.body = (s16)iniGetInt(ini, "body", -1);
		e->ext.bot_profile.name_langid = (s16)iniGetInt(ini, "name_langid", 0);
		e->ext.bot_profile.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
		strncpy(e->ext.bot_profile.target_body,
			iniGet(ini, "target_body",
			iniGet(ini, "body_id",
			iniGet(ini, "body_ref", ""))),
			sizeof(e->ext.bot_profile.target_body) - 1);
		strncpy(e->ext.bot_profile.profile_file, iniGet(ini, "profile_file",
			iniGet(ini, "file_path", "")), sizeof(e->ext.bot_profile.profile_file) - 1);
		if (e->ext.bot_profile.profile_file[0]) {
			catalogSetPrimaryFile(e, e->ext.bot_profile.profile_file);
		}
		break;

	case ASSET_VEHICLE:
		{
			strncpy(e->ext.vehicle.model_file,
				iniGet(ini, "model_file", iniGet(ini, "file_path", "")),
				sizeof(e->ext.vehicle.model_file) - 1);
			strncpy(e->ext.vehicle.physics_file,
				iniGet(ini, "physics_file", ""),
				sizeof(e->ext.vehicle.physics_file) - 1);
			strncpy(e->ext.vehicle.behavior_graph,
				iniGet(ini, "behavior_graph", ""),
				sizeof(e->ext.vehicle.behavior_graph) - 1);
			const char *pf = e->ext.vehicle.model_file[0] ?
				e->ext.vehicle.model_file :
				e->ext.vehicle.behavior_graph;
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_MISSION:
		{
			strncpy(e->ext.mission.scenario_archive,
				iniGet(ini, "scenario_archive", iniGet(ini, "file_path", "")),
				sizeof(e->ext.mission.scenario_archive) - 1);
			strncpy(e->ext.mission.objectives_file,
				iniGet(ini, "objectives_file", ""),
				sizeof(e->ext.mission.objectives_file) - 1);
			strncpy(e->ext.mission.mission_graph_file,
				iniGet(ini, "mission_graph_file",
				iniGet(ini, "graph", "")),
				sizeof(e->ext.mission.mission_graph_file) - 1);
			const char *pf = e->ext.mission.mission_graph_file;
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_THEME:
		{
			strncpy(e->ext.theme.theme_file,
				iniGet(ini, "theme_file", iniGet(ini, "file_path", "")),
				sizeof(e->ext.theme.theme_file) - 1);
			strncpy(e->ext.theme.ui_archive,
				iniGet(ini, "ui_archive", ""),
				sizeof(e->ext.theme.ui_archive) - 1);
			strncpy(e->ext.theme.font_archive,
				iniGet(ini, "font_archive", ""),
				sizeof(e->ext.theme.font_archive) - 1);
			strncpy(e->ext.theme.audio_archive,
				iniGet(ini, "audio_archive", ""),
				sizeof(e->ext.theme.audio_archive) - 1);
			strncpy(e->ext.theme.music_archive,
				iniGet(ini, "music_archive", ""),
				sizeof(e->ext.theme.music_archive) - 1);
			strncpy(e->ext.theme.effect_archive,
				iniGet(ini, "effect_archive", ""),
				sizeof(e->ext.theme.effect_archive) - 1);
			const char *pf = e->ext.theme.theme_file;
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_AI_LISTS:
		/* global-ai-lists.md: ai.ini names the public rows member
		 * (ai/global_ailists.json in a .pdai). */
		{
			strncpy(e->ext.ai_lists.lists_file,
				iniGet(ini, "ai_lists_file", iniGet(ini, "file_path", "")),
				sizeof(e->ext.ai_lists.lists_file) - 1);
			const char *pf = e->ext.ai_lists.lists_file;
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	default:
		/* ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC, ASSET_TOOL -- no extra fields needed */
		break;
	}

	registerDependencyList(e->id, iniGet(ini, "deps", ""), e->bundled);
	if (type == ASSET_PROJECTILE && e->ext.projectile.entity_ref[0]) {
		catalogDepRegister(e->id, e->ext.projectile.entity_ref, e->bundled);
	}
	if (type == ASSET_ANIMATION && e->ext.anim.target_body[0]) {
		catalogDepRegister(e->ext.anim.target_body, e->id, e->bundled);
	}

	return 1;
}

static s32 registerComponent(const ini_section_t *ini, const char *dirpath,
                              const char *mod_id, const char *descriptor_path)
{
	loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
	if (!batch) return 0;
	external_scan_transaction_t transaction = {0};
	const s32 owns_animation = ini && sectionToType(ini->type) == ASSET_ANIMATION;
	if (owns_animation && !externalScanTransactionBegin(&transaction)) {
		loaderAnimationSourceBatchDestroy(batch);
		return 0;
	}
	s32 ok = registerComponentWithAnimationBatch(ini, dirpath, mod_id, descriptor_path, batch);
	if (ok) ok = loaderAnimationSourceBatchCommit(batch);
	if (!ok && owns_animation && !externalScanTransactionRollback(&transaction))
		sysLoudFailf("CATALOG.ANIMATION.ROLLBACK", "could not restore standalone command admission");
	if (owns_animation) externalScanTransactionDestroy(&transaction);
	loaderAnimationSourceBatchDestroy(batch);
	return ok;
}

/* Legacy audio.ini callers supply a stable ID explicitly: filesystem
 * storage segments are not catalog identities. Share the scanner transaction
 * with typed audio, including provider paths and private-slot reservations. */
s32 assetCatalogRegisterAudioIni(const char *catalog_id, const char *mod_id,
	const char *dirpath, const ini_section_t *source, s32 default_category,
	s32 defer_reloads)
{
	ini_section_t ini;
	external_scan_transaction_t transaction;
	char descriptor[FS_MAXPATH];
	if (!catalog_id || !catalog_id[0]
			|| !memchr(catalog_id, '\0', CATALOG_ID_LEN)
			|| !mod_id || !mod_id[0] || !dirpath || !source
			|| source->count < 0 || source->count > INI_MAX_PAIRS
			|| sectionToType(source->type) != ASSET_AUDIO
			|| default_category < AUDIO_CAT_SFX || default_category > AUDIO_CAT_VOICE
			|| !assetPathJoinChecked(descriptor, sizeof(descriptor), dirpath,
				"/", "audio.ini")) return -1;
	ini = *source;
	for (s32 i = 0; i < ini.count; i++) {
		if ((strcmp(ini.pairs[i].key, "catalog_id") == 0
				|| strcmp(ini.pairs[i].key, "id") == 0)
				&& strcmp(ini.pairs[i].value, catalog_id) != 0) return -1;
	}
	if (!iniGet(&ini, "catalog_id", NULL) && !iniGet(&ini, "id", NULL)) {
		if (ini.count == INI_MAX_PAIRS) return -1;
		strcpy(ini.pairs[ini.count].key, "catalog_id");
		strcpy(ini.pairs[ini.count++].value, catalog_id);
	}
	/* Normalize the caller's established default once, preserving the shared
	 * category aliases. This prevents an empty key from shadowing a default. */
	s32 category = parseAudioCategoryValue(iniGet(&ini, "audio_category",
		iniGet(&ini, "kind", iniGet(&ini, "category", ""))), default_category);
	s32 category_pair = -1;
	for (s32 i = 0; i < ini.count; i++) {
		if (strcmp(ini.pairs[i].key, "audio_category") == 0) {
			category_pair = i;
			break;
		}
	}
	if (category_pair < 0) {
		if (ini.count == INI_MAX_PAIRS) return -1;
		category_pair = ini.count++;
		strcpy(ini.pairs[category_pair].key, "audio_category");
	}
	snprintf(ini.pairs[category_pair].value, sizeof(ini.pairs[0].value), "%d", category);
	if (!iniGet(&ini, "file_path", "")[0]
			&& !iniGet(&ini, "music_file", "")[0]
			&& !iniGet(&ini, "midi_file", "")[0]) return -1;
	if (!externalScanTransactionBegin(&transaction)) return -1;
	s32 result = registerComponent(&ini, dirpath, mod_id, descriptor) ? 1 : -1;
	if (result < 0) {
		s32 restored = externalScanTransactionRollback(&transaction);
		sysLogPrintf(restored ? LOG_WARNING : LOG_ERROR,
			"CATALOG.AUDIO.TRANSACTION.ROLLBACK: id=%s result=%s",
			catalog_id, restored ? "restored" : "failed");
	}
	externalScanTransactionDestroy(&transaction);
	if (result < 0 && !defer_reloads) (void)catalogReloadInvalidatedTypedAssets();
	return result;
}

/* Base walkers supply only validated private native identity. Everything
 * playable/editable is prepared from the public descriptor before retirement. */
static void baseAudioHasDependency(const char *id, asset_type_e type, void *userdata)
{
    (void)id; (void)type;
    *(s32 *)userdata = 1;
}

s32 assetCatalogRegisterBaseAudioSource(const char *catalog_id,
    const char *archive_ref, const ini_section_t *source, s32 category,
    s32 source_soundnum, s32 source_filenum)
{
    ini_section_t ini;
    catalog_audio_public_source_t parsed;
    catalog_audio_source_identity_t identity;
    external_scan_transaction_t transaction = {0};
    asset_entry_t *passive_backup = NULL;
    asset_data_handle_t prepared_handle = ASSET_HANDLE_NULL_INIT;
    s32 has_dependency = 0;
    char full[FS_MAXPATH];
    const char *resolved;
    mod_archive_t *archive = NULL;
    asset_entry_t *entry;
    const asset_entry_t *prior;
    const char *member;
    s32 result = -1;
    if (!catalog_id || !catalog_id[0] || !memchr(catalog_id, '\0', CATALOG_ID_LEN)
            || !archive_ref || !source || source_soundnum < -1 || source_soundnum > 65535
            || source_filenum < -1 || source_filenum == 0 || source_filenum > 2047
            || (source_soundnum < 0 && source_filenum < 0)
            || (category != AUDIO_CAT_SFX && category != AUDIO_CAT_VOICE)
            || !catalogAudioPublicSourceParse(source, 1, &parsed)) return -1;
    member = parsed.file_path;
    if (strcmp(member, "sample.wav") && strcmp(member, "sample.mp3") && strcmp(member, "sample.ogg")) return -1;
    for (s32 i = 0; i < source->count; i++) {
        if ((!strcmp(source->pairs[i].key, "catalog_id") || !strcmp(source->pairs[i].key, "id"))
                && strcmp(source->pairs[i].value, catalog_id)) return -1;
    }
    resolved = fsFullPath(archive_ref, full, sizeof(full));
    if (!resolved || !(archive = modArchiveOpen(resolved))) return -1;
    /* Source and every declared voice companion must remain inside this archive.
     * Prove membership before qualification; arbitrary prequalified refs reject. */
    const char *members[8] = { parsed.file_path, parsed.subtitle_file,
        parsed.locale_audio_files[0], parsed.locale_audio_files[1], parsed.locale_audio_files[2],
        parsed.locale_audio_files[3], parsed.locale_audio_files[4], parsed.locale_audio_files[5] };
    for (s32 i = 0; i < 8; i++) {
        if (!members[i][0]) continue;
        s32 index;
        if (!archiveInnerPathIsSafe(members[i])
                || (index = modArchiveFindEntry(archive, members[i])) < 0
                || modArchiveGetEntrySize(archive, index) == 0) goto done;
    }
    ini = *source;
    if (!qualifyTypedArchiveSourcePaths(&ini, archive_ref)
            || !catalogAudioPublicSourceParse(&ini, 1, &parsed)) goto done;
    prior = assetCatalogResolve(catalog_id);
    if (!catalogAudioSourceIdentityPrepare(prior, catalog_id, category, &identity)) goto done;
    if (!prior) {
        identity.sound_id = source_soundnum;
        identity.source_soundnum = source_soundnum;
        identity.source_filenum = source_filenum;
    }
    /* Intern the only allocating provider binding before any retirement. The
     * committed primary handle is then an infallible value copy. */
    prepared_handle = catalogHandleForSourceFile(parsed.file_path);
    if (assetHandleIsNull(prepared_handle)) goto done;
    if (prior) {
        catalogDepForEachTyped(catalog_id, baseAudioHasDependency, &has_dependency);
        if (prior->loaded_data || prior->payload_kind != ASSET_PAYLOAD_NONE
                || prior->stage_ref_count || (prior->ref_count > 0 && prior->ref_count != ASSET_REF_BUNDLED)
                || has_dependency || catalogActivationLedgerActiveCount()
                || catalogActivationLedgerPendingCount()) {
            if (!externalScanTransactionBegin(&transaction)) goto done;
        } else {
            /* Ordinary boot rows have no payload/closure to retire. A bounded
             * single-row backup avoids copying the entire catalog per sample. */
            passive_backup = malloc(sizeof(*passive_backup));
            if (!passive_backup) goto done;
            *passive_backup = *prior;
        }
    }
    entry = assetCatalogRegisterAudio(catalog_id, identity.sound_id,
        iniGet(&ini, "name", ""), category, 0, "");
    if (entry) {
        /* Every remaining publication is a bounded copy from the candidate. */
        if (catalogAudioSourceIdentityApply(entry, &identity)) {
            catalogAudioPublicSourceApply(entry, &parsed);
            catalogSetPrimary(entry, prepared_handle);
            loaderWalkerMarkBaseArchiveEntry(entry);
            result = 1;
        }
    }
    if (result < 0 && transaction.active) {
        s32 restored = externalScanTransactionRollback(&transaction);
        sysLogPrintf(restored ? LOG_WARNING : LOG_ERROR,
            "CATALOG.AUDIO.BASE.ROLLBACK: id=%s result=%s", catalog_id,
            restored ? "restored" : "failed");
    } else if (result < 0 && passive_backup) {
        asset_entry_t *restored = assetCatalogGetMutable(catalog_id);
        if (restored) *restored = *passive_backup;
        else sysLogPrintf(LOG_ERROR, "CATALOG.AUDIO.BASE.ROLLBACK: passive row missing %s", catalog_id);
    } else if (result < 0 && !prior) {
        (void)assetCatalogRollbackUnactivatedRegistration(catalog_id);
    }
    if (result < 0 && transaction.active) {
        externalScanTransactionDestroy(&transaction);
        (void)catalogReloadInvalidatedTypedAssets();
    }
done:
    externalScanTransactionDestroy(&transaction);
    free(passive_backup);
    modArchiveClose(archive);
    return result;
}

/* ========================================================================
 * Nested weapon animation/audio registration
 * ======================================================================== */

typedef struct weapon_nested_media_scan {
	char (*entries)[FS_MAXPATH];
	size_t count;
	size_t capacity;
	s32 allocation_failed;
} weapon_nested_media_scan_t;

static s32 weaponNestedMediaType(const char *path, asset_type_e *out_type)
{
	asset_type_e type = assetArchiveTypeForPath(path);
	if (type == ASSET_MODEL && pathEndsWithNoCase(path, ".pdmesh")) {
		if (out_type) *out_type = type;
		return 1;
	}
	if (type == ASSET_PROJECTILE
			&& pathEndsWithNoCase(path, ".pdprojectile")) {
		if (out_type) *out_type = type;
		return 1;
	}
	if (type == ASSET_ENTITY && pathEndsWithNoCase(path, ".pdentity")) {
		if (out_type) *out_type = type;
		return 1;
	}
	if (type == ASSET_ANIMATION) {
		if (out_type) *out_type = type;
		return 1;
	}
	if (type == ASSET_AUDIO && (pathEndsWithNoCase(path, ".pdsfx")
			|| pathEndsWithNoCase(path, ".pdvoice"))) {
		if (out_type) *out_type = type;
		return 1;
	}
	if (type == ASSET_UI && pathEndsWithNoCase(path, ".pdui")) {
		if (out_type) *out_type = type;
		return 1;
	}
	if (type == ASSET_EFFECT && pathEndsWithNoCase(path, ".pdeffect")) {
		if (out_type) *out_type = type;
		return 1;
	}
	return 0;
}

static s32 collectWeaponNestedMedia(const char *entry_name,
		u32 uncompressed_size, void *userdata)
{
	weapon_nested_media_scan_t *scan = (weapon_nested_media_scan_t *)userdata;
	asset_type_e type;
	(void)uncompressed_size;
	if (!scan || !weaponNestedMediaType(entry_name, &type)) {
		return 0;
	}
	if (scan->count == scan->capacity) {
		size_t next = scan->capacity ? scan->capacity * 2 : 16;
		if (next < scan->count + 1
				|| next > SIZE_MAX / sizeof(*scan->entries)) {
			scan->allocation_failed = 1;
			return MODARCHIVE_ERR_MEM;
		}
		char (*grown)[FS_MAXPATH] = realloc(scan->entries,
			next * sizeof(*scan->entries));
		if (!grown) {
			scan->allocation_failed = 1;
			return MODARCHIVE_ERR_MEM;
		}
		scan->entries = grown;
		scan->capacity = next;
	}
	if (!assetPathCopyChecked(scan->entries[scan->count],
			sizeof(scan->entries[scan->count]), entry_name)) {
		return MODARCHIVE_ERR_FORMAT;
	}
	scan->count++;
	return 0;
}

static s32 catalogIdSharesNamespace(const char *parent_id, const char *child_id)
{
	const char *parent_colon = parent_id ? strchr(parent_id, ':') : NULL;
	const char *child_colon = child_id ? strchr(child_id, ':') : NULL;
	if (!parent_colon || !child_colon || parent_colon == parent_id
			|| child_colon == child_id) {
		return 0;
	}
	size_t parent_len = (size_t)(parent_colon - parent_id);
	size_t child_len = (size_t)(child_colon - child_id);
	return parent_len == child_len
		&& memcmp(parent_id, child_id, parent_len) == 0;
}

static void nestedSetErr(char *err, size_t err_cap, const char *fmt,
		const char *a, const char *b)
{
	if (!err || err_cap == 0) return;
	snprintf(err, err_cap, fmt, a ? a : "", b ? b : "");
	err[err_cap - 1] = '\0';
}

static s32 nestedContentMatchesExisting(const asset_entry_t *existing,
		const void *nested, u32 nested_size)
{
	char archive_ref[sizeof(existing->descriptor_path)];
	const char *source_ref;
	const char *separator;
	u32 existing_size = 0;
	void *existing_bytes;
	char existing_digest[SHA256_HEX_SIZE];
	char nested_digest[SHA256_HEX_SIZE];

	if (!existing || !nested || nested_size == 0) {
		return 0;
	}
	source_ref = existing->descriptor_path;
	if (!source_ref[0]) {
		asset_data_handle_t handle = catalogEffectiveHandle(existing);
		if (handle.provider != fileProvider()) return 0;
		source_ref = fileProviderPath(handle);
	}
	if (!source_ref || !source_ref[0]) return 0;
	separator = strrchr(source_ref, ':');
	if (!separator || separator == source_ref
			|| separator[-1] != ':') {
		return 0;
	}
	separator--;
	size_t len = (size_t)(separator - source_ref);
	if (len == 0 || len >= sizeof(archive_ref)) {
		return 0;
	}
	memcpy(archive_ref, source_ref, len);
	archive_ref[len] = '\0';
	existing_bytes = fsFileLoad(archive_ref, &existing_size);
	if (!existing_bytes || existing_size == 0) {
		if (existing_bytes) sysMemFree(existing_bytes);
		return 0;
	}
	s32 ok = weaponGraphArchiveCanonicalSha256Bytes(existing_bytes,
		existing_size, existing_digest) == 0
		&& weaponGraphArchiveCanonicalSha256Bytes(nested, nested_size,
			nested_digest) == 0
		&& strcmp(existing_digest, nested_digest) == 0;
	sysMemFree(existing_bytes);
	return ok;
}

typedef struct character_nested_dependency {
	char id[CATALOG_ID_LEN];
	char archive_ref[FS_MAXPATH];
	char descriptor_ref[FS_MAXPATH];
	ini_section_t ini;
	asset_type_e type;
	s32 existing;
} character_nested_dependency_t;

static s32 characterDependencyMatchesExisting(const asset_entry_t *existing,
	const void *bytes, u32 size)
{
	char archive_ref[FS_MAXPATH];
	char old_digest[SHA256_HEX_SIZE], new_digest[SHA256_HEX_SIZE];
	const char *mesh, *separator;
	u32 old_size = 0;
	void *old_bytes;
	if (nestedContentMatchesExisting(existing, bytes, size)) return 1;
	/* Base body/head walkers bind the nested mesh directly and may have no
	 * descriptor_path. Its containing archive is still the exact public source. */
	mesh = existing->type == ASSET_BODY ? existing->ext.body.mesh_archive
		: existing->ext.head.mesh_archive;
	separator = findLastArchiveSeparator(mesh);
	if (!separator || separator == mesh ||
			(size_t)(separator - mesh) >= sizeof(archive_ref)) return 0;
	memcpy(archive_ref, mesh, (size_t)(separator - mesh));
	archive_ref[separator - mesh] = 0;
	old_bytes = fsFileLoad(archive_ref, &old_size);
	if (!old_bytes) return 0;
	s32 same = weaponGraphArchiveCanonicalSha256Bytes(old_bytes, old_size, old_digest) == 0
		&& weaponGraphArchiveCanonicalSha256Bytes(bytes, size, new_digest) == 0
		&& strcmp(old_digest, new_digest) == 0;
	sysMemFree(old_bytes);
	return same;
}

typedef struct character_dependency_owner {
	char id[CATALOG_ID_LEN];
	char body_id[CATALOG_ID_LEN];
	char head_id[CATALOG_ID_LEN];
	char bodyfile[FS_MAXPATH];
	char headfile[FS_MAXPATH];
	s32 temporary;
} character_dependency_owner_t;

static s32 keepCharacterDependency(const char *id, asset_type_e type, void *userdata)
{
	const character_dependency_owner_t *owner = userdata;
	if (type == ASSET_BODY) return strcmp(id, owner->body_id) == 0;
	if (type == ASSET_HEAD) return strcmp(id, owner->head_id) == 0;
	return 1;
}

int assetCatalogRegisterCharacterDependencies(const asset_entry_t *entry,
	int bundled, char *error, size_t error_capacity)
{
	character_nested_dependency_t *pending = NULL;
	character_dependency_owner_t owner;
	external_scan_transaction_t transaction;
	character_head_policy_e policy = characterSourcePolicy(entry);
	s32 count, result = -1, needs_registration = 0;
	catalog_dep_snapshot_t edges = {0};
	s32 have_edges = 0;
	memset(&transaction, 0, sizeof(transaction));
	if (error && error_capacity) error[0] = 0;
	if (policy == CHARACTER_HEAD_POLICY_INVALID) {
		nestedSetErr(error, error_capacity, "invalid character policy %s%s", "", "");
		return -1;
	}
	/* Catalog registration may realloc its pool. Keep only the immutable
	 * operation inputs, never a row pointer or a row to republish afterward. */
	strcpy(owner.id, entry->id);
	strcpy(owner.body_id, entry->ext.character.body_id);
	strcpy(owner.head_id, entry->ext.character.head_id);
	strcpy(owner.bodyfile, entry->ext.character.bodyfile);
	strcpy(owner.headfile, entry->ext.character.headfile);
	owner.temporary = entry->temporary;
	count = policy == CHARACTER_HEAD_POLICY_FIXED ? 2 : 1;
	pending = calloc((size_t)count, sizeof(*pending));
	if (!pending) goto done;
	/* Validate both declarations and every collision before publishing a child
	 * or edge. Empty-head policies never inspect or acquire a head archive. */
	for (s32 i = 0; i < count; ++i) {
		character_nested_dependency_t *dep = &pending[i];
		const char *id = i ? owner.head_id : owner.body_id;
		const char *path = i ? owner.headfile : owner.bodyfile;
		const char *leaf = NULL;
		const asset_entry_t *existing;
		u32 size = 0, descriptor_size = 0;
		void *bytes;
		char *descriptor;
		dep->type = i ? ASSET_HEAD : ASSET_BODY;
		strcpy(dep->id, id);
		strcpy(dep->archive_ref, path);
		if (strcmp(id, owner.id) == 0 ||
				(i && strcmp(id, owner.body_id) == 0) ||
				!pathEndsWithNoCase(path, i ? ".pdhead" : ".pdbody")) {
			nestedSetErr(error, error_capacity, "invalid character dependency %s source %s", id, path);
			goto done;
		}
		bytes = fsFileLoad(path, &size);
		if (!bytes || !size) {
			if (bytes) sysMemFree(bytes);
			nestedSetErr(error, error_capacity, "missing character dependency %s source %s", id, path);
			goto done;
		}
		if (assetArchiveValidateBytes(bytes, size, path,
				ASSET_ARCHIVE_VALIDATE_RELEASE, error, error_capacity) != 0) {
			sysMemFree(bytes);
			goto done;
		}
		descriptor = assetArchiveExtractDescriptorMemAlloc(bytes, size, path,
			ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size, &leaf);
		if (!descriptor || !leaf || memchr(descriptor, 0, descriptor_size) ||
				!iniParseBuffer(path, descriptor, descriptor_size, &dep->ini) ||
				!characterDependencySourceMatches(&dep->ini, id, i != 0,
					i == 0 && policy == CHARACTER_HEAD_POLICY_INTEGRATED) ||
				!assetPathJoinChecked(dep->descriptor_ref, sizeof(dep->descriptor_ref),
					path, "::", leaf)) {
			free(descriptor);
			sysMemFree(bytes);
			nestedSetErr(error, error_capacity,
				"character dependency ID/type/completeness mismatch %s source %s", id, path);
			goto done;
		}
		free(descriptor);
		existing = assetCatalogResolveAny(id);
		if (existing && (!existing->occupied || !existing->enabled || existing->type != dep->type ||
				!characterDependencyMatchesExisting(existing, bytes, size))) {
			sysMemFree(bytes);
			nestedSetErr(error, error_capacity, "character dependency ID/content collision %s source %s", id, path);
			goto done;
		}
		sysMemFree(bytes);
		dep->existing = existing != NULL;
		if (!dep->existing) {
			needs_registration = 1;
			if (!qualifyTypedArchiveSourcePaths(&dep->ini, path)) {
				nestedSetErr(error, error_capacity, "character dependency source path exceeds capacity %s%s", path, "");
				goto done;
			}
		}
	}
	/* Existing base dependencies need only a small edge snapshot. New custom
	 * rows use the established full scanner transaction, including managers and
	 * private slots, so a failed second child cannot strand the first. */
	if (needs_registration) {
		if (!externalScanTransactionBegin(&transaction)) goto done;
	} else {
		if (!catalogDepSnapshotCreate(&edges)) goto done;
		have_edges = 1;
	}
	if (!catalogDepReserve(count)) goto done;
	for (s32 i = 0; i < count; ++i) {
		character_nested_dependency_t *dep = &pending[i];
		if (!dep->existing) {
			if (!registerComponent(&dep->ini, dep->archive_ref,
					bundled ? "base" : owner.id, dep->descriptor_ref)) {
				nestedSetErr(error, error_capacity, "could not register character dependency %s%s", dep->id, "");
				goto done;
			}
			asset_entry_t *child = assetCatalogGetMutable(dep->id);
			if (!child || child->type != dep->type || child->runtime_index < 0) {
				nestedSetErr(error, error_capacity, "unbound character dependency %s%s", dep->id, "");
				goto done;
			}
			child->enabled = 0;
			child->bundled = bundled ? 1 : 0;
			child->temporary = owner.temporary;
		}
	}
	for (s32 i = 0; i < count; ++i) {
		character_nested_dependency_t *dep = &pending[i];
		if (!catalogDepRegisterTyped(owner.id, dep->id, dep->type, bundled)) {
			nestedSetErr(error, error_capacity, "could not register character ownership %s -> %s", owner.id, dep->id);
			goto done;
		}
	}
	for (s32 i = 0; i < count; ++i) {
		if (!pending[i].existing) assetCatalogGetMutable(pending[i].id)->enabled = 1;
	}
	catalogDepPruneOwner(owner.id, keepCharacterDependency, &owner);
	result = count;
done:
	if (result < 0) {
		if (transaction.active && !externalScanTransactionRollback(&transaction))
			sysLoudFailf("PDCHARACTER.ROLLBACK", "could not restore character dependency state for %s", owner.id);
		if (have_edges && !catalogDepSnapshotRestore(&edges))
			sysLoudFailf("PDCHARACTER.ROLLBACK", "could not restore character dependency edges for %s", owner.id);
		if (error && error_capacity && !error[0])
			nestedSetErr(error, error_capacity, "could not prepare character dependencies %s%s", owner.id, "");
	}
	externalScanTransactionDestroy(&transaction);
	catalogDepSnapshotDestroy(&edges);
	free(pending);
	return result;
}

/* ========================================================================
 * Strict .pdeffect embedded dependency registration
 * ======================================================================== */

typedef struct effect_nested_member_scan {
	char (*entries)[FS_MAXPATH + 1];
	size_t count;
	size_t capacity;
	s32 allocation_failed;
} effect_nested_member_scan_t;

typedef struct effect_nested_child {
	char catalog_id[CATALOG_ID_LEN];
	char archive_ref[FS_MAXPATH + 1];
	char descriptor_ref[FS_MAXPATH + 1];
	ini_section_t ini;
	asset_type_e expected_type;
	s32 existing;
	s32 created;
} effect_nested_child_t;

typedef struct effect_nested_registration {
	char owner_id[CATALOG_ID_LEN];
	effect_dependency_list_t deps;
	effect_nested_child_t *children;
	size_t child_count;
	u8 *edge_created;
} effect_nested_registration_t;

static s32 effectNestedMemberType(const char *path, asset_type_e *out_type)
{
	static const char audio_prefix[] = "dependencies/assets/audio/";
	static const char material_prefix[] = "dependencies/assets/materials/";
	static const char texture_prefix[] = "dependencies/assets/textures/";
	asset_type_e type = ASSET_NONE;

	if (!path) return 0;
	if (strncmp(path, audio_prefix, sizeof(audio_prefix) - 1) == 0
			&& pathEndsWithNoCase(path, ".pdsfx")) {
		type = ASSET_AUDIO;
	} else if (strncmp(path, material_prefix, sizeof(material_prefix) - 1) == 0
			&& pathEndsWithNoCase(path, ".pdmaterial")) {
		type = ASSET_MATERIAL;
	} else if (strncmp(path, texture_prefix, sizeof(texture_prefix) - 1) == 0
			&& pathEndsWithNoCase(path, ".pdtexture")) {
		type = ASSET_TEXTURE;
	}
	if (type == ASSET_NONE) return 0;
	if (out_type) *out_type = type;
	return 1;
}

static s32 collectEffectNestedMember(const char *entry_name,
		u32 uncompressed_size, void *userdata)
{
	effect_nested_member_scan_t *scan =
		(effect_nested_member_scan_t *)userdata;
	asset_type_e type;
	(void)uncompressed_size;
	if (!scan || !effectNestedMemberType(entry_name, &type)) return 0;
	if (scan->count == scan->capacity) {
		size_t next = scan->capacity ? scan->capacity * 2 : 8;
		if (next < scan->count + 1
				|| next > SIZE_MAX / sizeof(*scan->entries)) {
			scan->allocation_failed = 1;
			return MODARCHIVE_ERR_MEM;
		}
		char (*grown)[FS_MAXPATH + 1] = realloc(scan->entries,
			next * sizeof(*scan->entries));
		if (!grown) {
			scan->allocation_failed = 1;
			return MODARCHIVE_ERR_MEM;
		}
		scan->entries = grown;
		scan->capacity = next;
	}
	if (!assetPathCopyChecked(scan->entries[scan->count],
			sizeof(scan->entries[scan->count]), entry_name)) {
		return MODARCHIVE_ERR_FORMAT;
	}
	scan->count++;
	return 0;
}

static s32 appendEffectNestedScanEntry(effect_nested_member_scan_t *scan,
		const char *entry_name)
{
	if (!scan || !entry_name) return MODARCHIVE_ERR_FORMAT;
	if (scan->count == scan->capacity) {
		size_t next = scan->capacity ? scan->capacity * 2 : 8;
		if (next < scan->count + 1
				|| next > SIZE_MAX / sizeof(*scan->entries)) {
			scan->allocation_failed = 1;
			return MODARCHIVE_ERR_MEM;
		}
		char (*grown)[FS_MAXPATH + 1] = realloc(scan->entries,
			next * sizeof(*scan->entries));
		if (!grown) {
			scan->allocation_failed = 1;
			return MODARCHIVE_ERR_MEM;
		}
		scan->entries = grown;
		scan->capacity = next;
	}
	if (!assetPathCopyChecked(scan->entries[scan->count],
			sizeof(scan->entries[scan->count]), entry_name)) {
		return MODARCHIVE_ERR_FORMAT;
	}
	scan->count++;
	return 0;
}

static s32 collectWeaponNestedEffectContainer(const char *entry_name,
		u32 uncompressed_size, void *userdata)
{
	asset_type_e type = assetArchiveTypeForPath(entry_name);
	(void)uncompressed_size;
	if (type != ASSET_PROJECTILE && type != ASSET_ENTITY) return 0;
	return appendEffectNestedScanEntry(
		(effect_nested_member_scan_t *)userdata, entry_name);
}

static s32 collectEmbeddedEffectArchive(const char *entry_name,
		u32 uncompressed_size, void *userdata)
{
	(void)uncompressed_size;
	if (assetArchiveTypeForPath(entry_name) != ASSET_EFFECT
			|| !pathEndsWithNoCase(entry_name, ".pdeffect")) return 0;
	return appendEffectNestedScanEntry(
		(effect_nested_member_scan_t *)userdata, entry_name);
}

static s32 keepCurrentEffectDependency(const char *dep_id,
	asset_type_e expected_type, void *userdata)
{
	const effect_dependency_list_t *deps =
		(const effect_dependency_list_t *)userdata;
	for (size_t i = 0; deps && i < deps->count; i++) {
		if (deps->items[i].type == expected_type
				&& strcmp(deps->items[i].catalog_id, dep_id) == 0) return 1;
	}
	return 0;
}

static s32 effectNestedDependencyDeclared(
		const effect_nested_registration_t *registration,
		asset_type_e type, const char *catalog_id)
{
	for (size_t i = 0; registration && i < registration->deps.count; i++) {
		if (registration->deps.items[i].type == type
				&& strcmp(registration->deps.items[i].catalog_id, catalog_id) == 0) {
			return 1;
		}
	}
	return 0;
}

static void effectNestedRollback(effect_nested_registration_t *registration)
{
	if (!registration) return;
	for (size_t i = registration->deps.count; i-- > 0; ) {
		if (registration->edge_created && registration->edge_created[i]) {
			catalogDepUnregister(registration->owner_id,
				registration->deps.items[i].catalog_id);
			registration->edge_created[i] = 0;
		}
	}
	for (size_t i = registration->child_count; i-- > 0; ) {
		if (registration->children[i].created) {
			assetCatalogUnregister(registration->children[i].catalog_id);
			registration->children[i].created = 0;
		}
	}
}

static void effectNestedFree(effect_nested_registration_t *registration)
{
	if (!registration) return;
	free(registration->edge_created);
	free(registration->children);
	effectDependenciesFree(&registration->deps);
	memset(registration, 0, sizeof(*registration));
}

static s32 effectNestedPrepareBytes(effect_nested_registration_t *registration,
		const char *effect_id, const char *effect_archive,
		const void *effect_bytes, u32 effect_size, char *err, size_t err_cap)
{
	effect_nested_member_scan_t scan;
	s32 enum_result;

	if (!registration || !effect_id || !effect_id[0] || !effect_archive
			|| !effect_archive[0] || !effect_bytes || effect_size == 0) {
		nestedSetErr(err, err_cap,
			"effect nested dependency scan missing %s%s", effect_id,
			effect_archive);
		return 0;
	}
	memset(registration, 0, sizeof(*registration));
	memset(&scan, 0, sizeof(scan));
	if (!assetPathCopyChecked(registration->owner_id,
			sizeof(registration->owner_id), effect_id)) {
		nestedSetErr(err, err_cap, "effect catalog ID exceeds capacity %s%s",
			effect_id, "");
		return 0;
	}
	if (effectDependenciesCollectArchiveBytes(effect_bytes, effect_size,
			effect_id, &registration->deps, err, err_cap) < 0) {
		goto fail;
	}
	enum_result = modArchiveMemForEachEntry(effect_bytes, effect_size,
		collectEffectNestedMember, &scan);
	if (enum_result != MODARCHIVE_OK) {
		nestedSetErr(err, err_cap,
			scan.allocation_failed
				? "out of memory enumerating effect archive %s%s"
				: "could not enumerate effect archive %s%s",
			effect_archive, "");
		goto fail;
	}
	if (scan.count > SIZE_MAX / sizeof(*registration->children)) {
		nestedSetErr(err, err_cap, "effect %s nested child count overflows %s",
			effect_id, "platform capacity");
		goto fail;
	}
	if (scan.count) {
		registration->children = calloc(scan.count,
			sizeof(*registration->children));
		if (!registration->children) {
			nestedSetErr(err, err_cap,
				"out of memory preflighting effect %s%s", effect_id, "");
			goto fail;
		}
	}

	for (size_t i = 0; i < scan.count; i++) {
		effect_nested_child_t *child = &registration->children[i];
		void *nested = NULL;
		u32 nested_size = 0;
		u32 descriptor_size = 0;
		const char *descriptor_leaf = NULL;
		char *descriptor = NULL;
		const char *catalog_id;

		effectNestedMemberType(scan.entries[i], &child->expected_type);
		nested = modArchiveExtractMemAlloc(effect_bytes, effect_size,
			scan.entries[i], &nested_size);
		if (!nested || nested_size == 0) {
			free(nested);
			nestedSetErr(err, err_cap,
				"could not read embedded effect dependency %s%s",
				scan.entries[i], "");
			goto fail;
		}
		if (assetArchiveValidateBytes(nested, nested_size, scan.entries[i],
				ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) != 0) {
			free(nested);
			goto fail;
		}
		descriptor = assetArchiveExtractDescriptorMemAlloc(nested, nested_size,
			scan.entries[i], ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size,
			&descriptor_leaf);
		if (!descriptor || !iniParseBuffer(descriptor_leaf, descriptor,
				descriptor_size, &child->ini)) {
			free(descriptor);
			free(nested);
			nestedSetErr(err, err_cap,
				"invalid public descriptor in embedded effect dependency %s%s",
				scan.entries[i], "");
			goto fail;
		}
		free(descriptor);
		if (sectionToType(child->ini.type) != child->expected_type) {
			free(nested);
			nestedSetErr(err, err_cap,
				"embedded effect dependency type mismatch %s%s",
				scan.entries[i], "");
			goto fail;
		}
		catalog_id = iniGet(&child->ini, "catalog_id",
			iniGet(&child->ini, "id", ""));
		if (!catalog_id[0] || !pdEffectCatalogIdValid(catalog_id)
				|| !catalogIdSharesNamespace(effect_id, catalog_id)) {
			free(nested);
			nestedSetErr(err, err_cap,
				"embedded effect dependency %s has missing or foreign catalog ID %s",
				scan.entries[i], catalog_id);
			goto fail;
		}
		if (!effectNestedDependencyDeclared(registration,
				child->expected_type, catalog_id)) {
			free(nested);
			nestedSetErr(err, err_cap,
				"embedded effect dependency %s is not referenced by graph %s",
				catalog_id, effect_id);
			goto fail;
		}
		for (size_t prior = 0; prior < i; prior++) {
			if (strcmp(registration->children[prior].catalog_id, catalog_id) == 0) {
				free(nested);
				nestedSetErr(err, err_cap,
					"duplicate embedded effect dependency ID %s%s", catalog_id, "");
				goto fail;
			}
		}
		if (!assetPathCopyChecked(child->catalog_id, sizeof(child->catalog_id),
				catalog_id)
				|| !assetPathJoinChecked(child->archive_ref,
					sizeof(child->archive_ref), effect_archive, "::",
					scan.entries[i])
				|| !assetPathJoinChecked(child->descriptor_ref,
					sizeof(child->descriptor_ref), child->archive_ref, "::",
					descriptor_leaf ? descriptor_leaf
						: assetArchiveDescriptorForPath(scan.entries[i]))
				|| !qualifyTypedArchiveSourcePaths(&child->ini,
					child->archive_ref)) {
			free(nested);
			nestedSetErr(err, err_cap,
				"embedded effect dependency path exceeds capacity %s%s",
				scan.entries[i], "");
			goto fail;
		}
		const asset_entry_t *existing = assetCatalogResolve(child->catalog_id);
		if (existing) {
			if (existing->type != child->expected_type || (!existing->bundled
					&& !nestedContentMatchesExisting(existing, nested,
						nested_size))) {
				free(nested);
				nestedSetErr(err, err_cap,
					"embedded effect dependency ID collision %s%s",
					child->catalog_id, "");
				goto fail;
			}
			child->existing = 1;
		}
		free(nested);
		registration->child_count++;
	}
	free(scan.entries);
	return 1;

fail:
	free(scan.entries);
	effectNestedFree(registration);
	return 0;
}

static s32 effectNestedCommit(effect_nested_registration_t *registration,
		s32 bundled, char *err, size_t err_cap)
{
	if (!registration) return 0;
	if (registration->deps.count > INT_MAX
			|| !catalogDepReserve((s32)registration->deps.count)) {
		nestedSetErr(err, err_cap,
			"could not reserve embedded effect edges for %s%s",
			registration->owner_id, "");
		return 0;
	}
	if (registration->deps.count) {
		registration->edge_created = calloc(registration->deps.count, 1);
		if (!registration->edge_created) {
			nestedSetErr(err, err_cap,
				"out of memory tracking embedded effect edges %s%s",
				registration->owner_id, "");
			return 0;
		}
	}
	for (size_t i = 0; i < registration->child_count; i++) {
		effect_nested_child_t *child = &registration->children[i];
		if (child->existing) continue;
		child->created = 1;
		if (!registerComponent(&child->ini, child->archive_ref,
				bundled ? "base" : registration->owner_id,
				child->descriptor_ref)) {
			nestedSetErr(err, err_cap,
				"could not register embedded effect dependency %s%s",
				child->catalog_id, "");
			goto fail;
		}
		asset_entry_t *entry = assetCatalogGetMutable(child->catalog_id);
		if (!entry) {
			nestedSetErr(err, err_cap,
				"embedded effect dependency disappeared after register %s%s",
				child->catalog_id, "");
			goto fail;
		}
		entry->bundled = bundled ? 1 : 0;
		entry->enabled = 1;
		entry->temporary = 0;
		sysLogPrintf(LOG_NOTE,
			"PDEFFECT.NESTED.REGISTER: owner=%s id=%s type=%d source=%s",
			registration->owner_id, child->catalog_id,
			(s32)child->expected_type, child->archive_ref);
	}
	for (size_t i = 0; i < registration->deps.count; i++) {
		effect_dependency_t *dep = &registration->deps.items[i];
		asset_type_e prior = catalogDepExpectedType(registration->owner_id,
			dep->catalog_id);
		if (catalogDepContains(registration->owner_id, dep->catalog_id)) {
			if (prior != ASSET_NONE && prior != dep->type) {
				nestedSetErr(err, err_cap,
					"embedded effect dependency type conflict %s%s",
					dep->catalog_id, "");
				goto fail;
			}
			continue;
		}
		if (!catalogDepRegisterTyped(registration->owner_id, dep->catalog_id,
				dep->type, bundled ? 1 : 0)) {
			nestedSetErr(err, err_cap,
				"could not register embedded effect edge %s%s",
				dep->catalog_id, "");
			goto fail;
		}
		registration->edge_created[i] = 1;
	}
	return 1;

fail:
	effectNestedRollback(registration);
	return 0;
}

s32 assetCatalogRegisterEffectNestedDependencies(const char *effect_id,
		const char *effect_archive, s32 bundled, char *err, size_t err_cap)
{
	effect_nested_registration_t registration;
	u32 effect_size = 0;
	void *effect_bytes = NULL;
	s32 result = -1;

	if (err && err_cap) err[0] = '\0';
	effect_bytes = effect_archive ? fsFileLoad(effect_archive, &effect_size) : NULL;
	if (!effect_bytes || effect_size == 0) {
		nestedSetErr(err, err_cap, "could not read effect archive %s%s",
			effect_archive, "");
		if (effect_bytes) sysMemFree(effect_bytes);
		return -1;
	}
	if (effectNestedPrepareBytes(&registration, effect_id, effect_archive,
			effect_bytes, effect_size, err, err_cap)
			&& effectNestedCommit(&registration, bundled, err, err_cap)) {
		catalogDepPruneOwner(effect_id, keepCurrentEffectDependency,
			&registration.deps);
		result = (s32)registration.child_count;
	}
	effectNestedFree(&registration);
	sysMemFree(effect_bytes);
	return result;
}

typedef struct weapon_nested_preflight {
	char catalog_id[CATALOG_ID_LEN];
	char archive_ref[FS_MAXPATH * 2 + 4];
	char descriptor_ref[FS_MAXPATH * 2 + 132];
	ini_section_t ini;
	void *bytes;
	u32 size;
	asset_type_e expected_type;
	s32 existing;
	s32 created;
	s32 edge_created;
	loader_walker_mesh_source_plan_t mesh_source;
	s32 has_mesh_source;
	s32 needs_graph_model;
	catalog_entry_snapshot_t existing_snapshot;
	s32 existing_snapshot_valid;
	effect_nested_registration_t effect_nested;
	u8 *effect_dep_edges_created;
} weapon_nested_preflight_t;

static const char *weaponNestedMeshPublicGeometry(const ini_section_t *ini)
{
	if (!ini) return NULL;
	for (size_t i = 0; i < loaderWalkerMeshPublicGeometryKeyCount(); i++) {
		const char *value = iniGet(ini,
			loaderWalkerMeshPublicGeometryKey(i), NULL);
		if (value && value[0]) return value;
	}
	return "model.obj";
}

static s32 weaponNestedPendingReserve(weapon_nested_preflight_t **pending,
		size_t *capacity, size_t needed)
{
	weapon_nested_preflight_t *grown;
	size_t next;
	if (!pending || !capacity) return 0;
	if (needed <= *capacity) return 1;
	next = *capacity ? *capacity : 16;
	while (next < needed) {
		if (next > SIZE_MAX / 2) return 0;
		next *= 2;
	}
	if (next > SIZE_MAX / sizeof(**pending)) return 0;
	grown = realloc(*pending, next * sizeof(**pending));
	if (!grown) return 0;
	memset(grown + *capacity, 0,
		(next - *capacity) * sizeof(*grown));
	*pending = grown;
	*capacity = next;
	return 1;
}

/* Shared preparation for direct and projectile/entity-owned members. No
 * second mesh descriptor parser or catalog mutation is allowed at graph time.
 * Return 2 only for a byte-equivalent mesh already present in pending. */
static s32 weaponNestedPrepareMember(weapon_nested_preflight_t *p,
		weapon_nested_preflight_t *pending, size_t pending_count,
		const char *weapon_id, const char *container_archive,
		const void *container_bytes, u32 container_size,
		const char *entry_name, asset_type_e expected_type,
		s32 owner_bundled, char *err, size_t err_cap)
{
	u32 descriptor_size = 0;
	const char *descriptor_leaf = NULL;
	char *descriptor = NULL;
	const char *catalog_id;
	p->expected_type = expected_type;
	p->bytes = modArchiveExtractMemAlloc(container_bytes, container_size,
		entry_name, &p->size);
	if (!p->bytes || p->size == 0) {
		nestedSetErr(err, err_cap, "could not read nested dependency %s%s",
			entry_name, "");
		return 0;
	}
	if (assetArchiveValidateBytes(p->bytes, p->size, entry_name,
			ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) != 0) return 0;
	descriptor = assetArchiveExtractDescriptorMemAlloc(p->bytes, p->size,
		entry_name, ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size,
		&descriptor_leaf);
	if (!descriptor || !iniParseBuffer(descriptor_leaf, descriptor,
			descriptor_size, &p->ini)) {
		free(descriptor);
		nestedSetErr(err, err_cap, "invalid public descriptor in %s%s",
			entry_name, "");
		return 0;
	}
	free(descriptor);
	if (sectionToType(p->ini.type) != expected_type) {
		nestedSetErr(err, err_cap, "nested dependency type mismatch %s%s",
			entry_name, "");
		return 0;
	}
	catalog_id = iniGet(&p->ini, "catalog_id", iniGet(&p->ini, "id", ""));
	if (!catalog_id[0] || !catalogIdSharesNamespace(weapon_id, catalog_id)) {
		nestedSetErr(err, err_cap,
			"nested dependency %s has missing or foreign catalog ID %s",
			entry_name, catalog_id);
		return 0;
	}
	for (size_t prior = 0; prior < pending_count; prior++) {
		if (strcmp(pending[prior].catalog_id, catalog_id) == 0) {
			char a[SHA256_HEX_SIZE], b[SHA256_HEX_SIZE];
			if (expected_type == ASSET_MODEL
					&& pending[prior].expected_type == ASSET_MODEL
					&& weaponGraphArchiveCanonicalSha256Bytes(pending[prior].bytes,
						pending[prior].size, a) == 0
					&& weaponGraphArchiveCanonicalSha256Bytes(p->bytes, p->size, b) == 0
					&& strcmp(a, b) == 0) {
				/* Multiple payload owners may share exactly one immutable source. */
				assetPathCopyChecked(p->catalog_id, sizeof(p->catalog_id), catalog_id);
				return 2;
			}
			nestedSetErr(err, err_cap, "duplicate nested dependency ID %s%s",
				catalog_id, "");
			return 0;
		}
	}
	if (!assetPathCopyChecked(p->catalog_id, sizeof(p->catalog_id),
			catalog_id)) {
		nestedSetErr(err, err_cap,
			"nested dependency catalog ID exceeds capacity %s%s",
			catalog_id, "");
		return 0;
	}
	if (!assetPathJoinChecked(p->archive_ref, FS_MAXPATH, container_archive,
			"::", entry_name) ||
			!assetPathJoinChecked(p->descriptor_ref, FS_MAXPATH,
				p->archive_ref, "::", descriptor_leaf ? descriptor_leaf
					: assetArchiveDescriptorForPath(entry_name))) {
		nestedSetErr(err, err_cap,
			"nested dependency archive chain exceeds capacity: %s%s",
			entry_name, "");
		return 0;
	}
	if (expected_type == ASSET_EFFECT
			&& !effectNestedPrepareBytes(&p->effect_nested, p->catalog_id,
				p->archive_ref, p->bytes, p->size, err, err_cap)) {
		return 0;
	}
	const asset_entry_t *existing = assetCatalogResolve(p->catalog_id);
	if (existing) {
		if (existing->type != expected_type || (!existing->bundled
				&& !nestedContentMatchesExisting(existing, p->bytes,
					p->size))) {
			nestedSetErr(err, err_cap,
				"nested dependency ID collision %s%s", p->catalog_id, "");
			return 0;
		}
		p->existing = 1;
	}
	if (expected_type == ASSET_MODEL) {
		const char *kind = iniGet(&p->ini, "kind", "mesh");
		if (strcmp(kind, "mesh") != 0) {
			nestedSetErr(err, err_cap, "nested mesh %s has incompatible public kind %s",
				p->catalog_id, kind);
			return 0;
		}
		u32 manifest_size = 0;
		void *manifest = modArchiveExtractMemAlloc(p->bytes, p->size,
			ASSET_ARCHIVE_META_MANIFEST_PATH, &manifest_size);
		/* B-1152: for a mesh that is not the base game's, the native file its
		 * manifest names is provenance only. A weapon mod saved from a base
		 * gun before the mod author stopped copying that name still carries
		 * it; honoured, the mod's mesh took the base gun's file slot and the
		 * base gun drew the mod's model. Such a mesh keeps the slot its row
		 * already has, or gets a private one when it is bound, as a mod
		 * vehicle's mesh does (body_head_source_bind.c, force_private). */
		const s32 own_slot = !owner_bundled && !(existing && existing->bundled);
		if (!loaderWalkerMeshSourcePlanManifest((const char *)manifest,
				manifest_size, p->catalog_id, p->archive_ref,
				weaponNestedMeshPublicGeometry(&p->ini),
				own_slot ? -1 : (existing ? existing->source_filenum : -1),
				&p->mesh_source, err, err_cap)) {
			free(manifest);
			if (!err || !err[0]) {
				nestedSetErr(err, err_cap,
					"could not plan nested mesh source %s%s",
					p->catalog_id, "");
			}
			return 0;
		}
		free(manifest);
		if (own_slot) {
			if (p->mesh_source.source_symbol_present) {
				sysLogPrintf(LOG_WARNING,
					"PDWEAPON.NESTED.MESH_SOURCE: owner=%s id=%s keeps a slot of its own: its manifest names file %d, which is the base game's (saved before B-1152's fix; saving the mod again removes the name)",
					weapon_id, p->catalog_id, p->mesh_source.source_filenum);
			}
			p->mesh_source.source_filenum = existing && existing->source_filenum > 0
				? existing->source_filenum : -1;
			p->mesh_source.source_symbol_present = 0;
		}
		/* Planning does not read geometry. Verify the exact selected member
		 * now so a missing source cannot publish an otherwise valid catalog row.
		 * Geometry semantics remain the production model compiler's authority. */
		{
			u32 geometry_size = 0;
			void *geometry = modArchiveExtractMemAlloc(p->bytes, p->size,
				p->mesh_source.geometry_member, &geometry_size);
			s32 present = geometry && geometry_size > 0;
			free(geometry);
			if (!present) {
				nestedSetErr(err, err_cap, "nested mesh %s has no selected geometry %s",
					p->catalog_id, p->mesh_source.geometry_member);
				return 0;
			}
		}
		p->has_mesh_source = 1;
		if (existing && existing->source.primary.provider == fileProvider()) {
			const char *current_path = fileProviderPath(existing->source.primary);
			if ((!current_path
					|| strcmp(current_path, p->mesh_source.source_path) != 0)
					&& !loaderWalkerMeshSourceMatchesEntry(existing,
						p->archive_ref, p->bytes, p->size, err, err_cap)) {
				if (!err || !err[0]) {
					nestedSetErr(err, err_cap,
						"nested typed mesh source collision %s%s",
						p->catalog_id, "");
				}
				return 0;
			}
			/* The shared binder owns first-archive retention and retired-member
			 * changes. Do not bypass its resident-source checks here. */
		}
	}
	return 1;
}

/* Included in the scanner draft after weaponNestedPrepareMember. */
typedef struct weapon_nested_mesh_edge {
	char owner[CATALOG_ID_LEN];
	char child[CATALOG_ID_LEN];
	s32 created;
} weapon_nested_mesh_edge_t;

typedef struct weapon_nested_mesh_edges {
	weapon_nested_mesh_edge_t *items;
	size_t count;
	size_t capacity;
} weapon_nested_mesh_edges_t;

static s32 weaponNestedAddMeshEdge(weapon_nested_mesh_edges_t *edges,
		const char *owner, const char *child, char *err, size_t err_cap)
{
	for (size_t i = 0; i < edges->count; i++) {
		if (strcmp(edges->items[i].owner, owner) == 0
				&& strcmp(edges->items[i].child, child) == 0) return 1;
	}
	if (edges->count == edges->capacity) {
		size_t next = edges->capacity ? edges->capacity * 2 : 16;
		if (next < edges->count + 1 || next > SIZE_MAX / sizeof(*edges->items)) {
			nestedSetErr(err, err_cap, "nested mesh edge capacity overflow %s%s", owner, "");
			return 0;
		}
		weapon_nested_mesh_edge_t *grown = realloc(edges->items,
			next * sizeof(*edges->items));
		if (!grown) {
			nestedSetErr(err, err_cap, "out of memory tracking mesh owners %s%s", owner, "");
			return 0;
		}
		edges->items = grown;
		edges->capacity = next;
	}
	weapon_nested_mesh_edge_t *edge = &edges->items[edges->count];
	memset(edge, 0, sizeof(*edge));
	if (!assetPathCopyChecked(edge->owner, sizeof(edge->owner), owner)
			|| !assetPathCopyChecked(edge->child, sizeof(edge->child), child)) {
		nestedSetErr(err, err_cap, "nested mesh owner ID exceeds capacity %s%s", owner, child);
		return 0;
	}
	edges->count++;
	return 1;
}

static s32 collectWeaponEmbeddedMesh(const char *entry, u32 size, void *user)
{
	if (assetArchiveTypeForPath(entry) != ASSET_MODEL
			|| !pathEndsWithNoCase(entry, ".pdmesh")) return 0;
	return collectWeaponNestedMedia(entry, size, user);
}

static s32 weaponNestedPrepareRecursiveMeshes(weapon_nested_preflight_t **pending,
		size_t *count, size_t *capacity, weapon_nested_mesh_edges_t *edges,
		const char *weapon_id, s32 owner_bundled, char *err, size_t err_cap)
{
	/* The executable v1 payload inventory owns direct projectile/entity archives.
	 * Enumerate their mesh children through the same public descriptor preflight.
	 * Capture values before reserve: pending itself may relocate. */
	const size_t container_count = *count;
	for (size_t i = 0; i < container_count; i++) {
		weapon_nested_media_scan_t scan = {0};
		char container_archive[FS_MAXPATH], owner[CATALOG_ID_LEN];
		const void *bytes = (*pending)[i].bytes;
		u32 size = (*pending)[i].size;
		if ((*pending)[i].expected_type != ASSET_PROJECTILE
				&& (*pending)[i].expected_type != ASSET_ENTITY) continue;
		if (!assetPathCopyChecked(container_archive, sizeof(container_archive),
				(*pending)[i].archive_ref)
				|| !assetPathCopyChecked(owner, sizeof(owner), (*pending)[i].catalog_id)) return 0;
		if (modArchiveMemForEachEntry(bytes, size, collectWeaponEmbeddedMesh, &scan)
				!= MODARCHIVE_OK) {
			nestedSetErr(err, err_cap, "could not enumerate payload meshes in %s%s", owner, "");
			free(scan.entries);
			return 0;
		}
		for (size_t j = 0; j < scan.count; j++) {
			if (!weaponNestedPendingReserve(pending, capacity, *count + 1)) {
				nestedSetErr(err, err_cap, "out of memory preparing payload meshes %s%s", owner, "");
				free(scan.entries);
				return 0;
			}
			weapon_nested_preflight_t *p = &(*pending)[*count];
			s32 result = weaponNestedPrepareMember(p, *pending, *count,
				weapon_id, container_archive, bytes, size, scan.entries[j], ASSET_MODEL,
				owner_bundled, err, err_cap);
			if (!result || !weaponNestedAddMeshEdge(edges, owner, p->catalog_id, err, err_cap)) {
				free(scan.entries);
				return 0;
			}
			if (result == 2) {
				free(p->bytes);
				memset(p, 0, sizeof(*p));
			} else {
				(*count)++;
			}
		}
		free(scan.entries);
	}
	return 1;
}

static s32 weaponNestedSelectGraphModels(weapon_nested_preflight_t *pending,
		size_t count, weapon_nested_mesh_edges_t *edges,
		const char *owner_id, const char *archive, asset_type_e type,
		char *err, size_t err_cap)
{
	weapon_graph_ir_t *ir = calloc(1, sizeof(*ir));
	if (!ir) {
		nestedSetErr(err, err_cap, "out of memory preparing graph model references %s%s", owner_id, "");
		return 0;
	}
	s32 ok = weaponGraphCompileArchiveFile(archive, type, ir, err, err_cap) == 0;
	for (s32 i = 0; ok && i < ir->param_count; i++) {
		const weapon_graph_ir_param_t *param = &ir->params[i];
		if (strcmp(param->key, "model_ref") != 0
				&& strcmp(param->key, "model_catalog_id") != 0
				&& strcmp(param->key, "projectile_model_ref") != 0
				&& strcmp(param->key, "projectile_model_catalog_id") != 0) continue;
		if (param->type == WEAPON_GRAPH_PARAM_NULL
				|| (param->type == WEAPON_GRAPH_PARAM_STRING && !param->value[0])) continue;
		if (param->type != WEAPON_GRAPH_PARAM_STRING) { ok = 0; break; }
		size_t j;
		for (j = 0; j < count; j++) {
			if (strcmp(pending[j].catalog_id, param->value) != 0) continue;
			if (pending[j].expected_type != ASSET_MODEL) { ok = 0; break; }
			pending[j].needs_graph_model = 1;
			break;
		}
		if (j == count) {
			const asset_entry_t *existing = assetCatalogResolve(param->value);
			if (!existing || existing->type != ASSET_MODEL || existing->runtime_index < 0) ok = 0;
		}
		if (ok) ok = weaponNestedAddMeshEdge(edges, owner_id, param->value, err, err_cap);
		if (!ok && (!err || !err[0])) {
			nestedSetErr(err, err_cap, "graph %s has unresolved native model reference %s",
				owner_id, param->value);
		}
	}
	weaponGraphIrFree(ir);
	free(ir);
	return ok;
}

static s32 weaponNestedPrepareMeshConsumers(weapon_nested_preflight_t *pending,
		size_t count, weapon_nested_mesh_edges_t *edges,
		const char *weapon_id, const char *weapon_archive,
		const void *weapon_bytes, u32 weapon_size, char *held_mesh_id,
		char *err, size_t err_cap)
{
	weapon_graph_archive_descriptor_t desc;
	s32 has_mesh = 0;
	held_mesh_id[0] = '\0';
	for (size_t i = 0; i < count; i++) if (pending[i].has_mesh_source) has_mesh = 1;
	s32 version = wgV2ArchiveInspect(weapon_bytes, weapon_size, &desc, err, err_cap);
	if (version < 0) return 0;
	if (pathEndsWithNoCase(desc.model_file, ".pdmesh")) {
		char selected[FS_MAXPATH];
		if (!assetPathJoinChecked(selected, sizeof(selected), weapon_archive, "::", desc.model_file)) return 0;
		for (size_t i = 0; i < count; i++) {
			if (pending[i].has_mesh_source && strcmp(pending[i].archive_ref, selected) == 0) {
				assetPathCopyChecked(held_mesh_id, CATALOG_ID_LEN, pending[i].catalog_id);
				break;
			}
		}
		/* A duplicate byte-equivalent member may use another member path. Resolve
		 * its public ID through the same INI parser, then require a prepared row. */
		if (!held_mesh_id[0]) {
			u32 mesh_size = 0, descriptor_size = 0;
			const char *leaf = NULL;
			void *mesh = modArchiveExtractMemAlloc(weapon_bytes, weapon_size, desc.model_file, &mesh_size);
			char *descriptor = mesh ? assetArchiveExtractDescriptorMemAlloc(mesh, mesh_size,
				desc.model_file, ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size, &leaf) : NULL;
			ini_section_t ini;
			if (descriptor && iniParseBuffer(leaf, descriptor, descriptor_size, &ini)) {
				const char *id = iniGet(&ini, "catalog_id", iniGet(&ini, "id", ""));
				for (size_t i = 0; i < count; i++) {
					if (pending[i].has_mesh_source && strcmp(pending[i].catalog_id, id) == 0) {
						assetPathCopyChecked(held_mesh_id, CATALOG_ID_LEN, id);
						break;
					}
				}
			}
			free(descriptor);
			free(mesh);
		}
		if (!held_mesh_id[0]) {
			nestedSetErr(err, err_cap, "weapon %s selected mesh was not prepared: %s", weapon_id, desc.model_file);
			return 0;
		}
	}
	if (!has_mesh) return 1;
	if (version == 0 && !weaponNestedSelectGraphModels(pending, count, edges, weapon_id,
			weapon_archive, ASSET_WEAPON, err, err_cap)) return 0;
	for (size_t i = 0; i < count; i++) {
		if (pending[i].expected_type != ASSET_PROJECTILE && pending[i].expected_type != ASSET_ENTITY) continue;
		if (!weaponNestedSelectGraphModels(pending, count, edges, pending[i].catalog_id,
				pending[i].archive_ref, pending[i].expected_type, err, err_cap)) return 0;
	}
	return 1;
}

static s32 weaponNestedPrepareRecursiveEffects(
		weapon_nested_preflight_t **pending, size_t *pending_count,
		size_t *pending_capacity, const char *weapon_id,
		const char *weapon_archive, const void *weapon_bytes, u32 weapon_size,
		char *err, size_t err_cap)
{
	effect_nested_member_scan_t containers;
	s32 enum_result;

	memset(&containers, 0, sizeof(containers));
	enum_result = modArchiveMemForEachEntry(weapon_bytes, weapon_size,
		collectWeaponNestedEffectContainer, &containers);
	if (enum_result != MODARCHIVE_OK) {
		nestedSetErr(err, err_cap,
			containers.allocation_failed
				? "out of memory enumerating weapon payloads %s%s"
				: "could not enumerate weapon payloads %s%s",
			weapon_archive, "");
		free(containers.entries);
		return 0;
	}

	for (size_t container_index = 0;
			container_index < containers.count; container_index++) {
		const char *container_entry = containers.entries[container_index];
		asset_type_e container_type = assetArchiveTypeForPath(container_entry);
		effect_nested_member_scan_t effects;
		char container_ref[FS_MAXPATH + 1];
		u32 container_size = 0;
		void *container_bytes = NULL;

		memset(&effects, 0, sizeof(effects));
		container_bytes = modArchiveExtractMemAlloc(weapon_bytes, weapon_size,
			container_entry, &container_size);
		if (!container_bytes || container_size == 0
				|| assetArchiveValidateBytes(container_bytes, container_size,
					container_entry, ASSET_ARCHIVE_VALIDATE_RELEASE,
					err, err_cap) != 0
				|| !assetPathJoinChecked(container_ref, sizeof(container_ref),
					weapon_archive, "::", container_entry)) {
			if ((!err || !err[0]) && container_type != ASSET_NONE) {
				nestedSetErr(err, err_cap,
					"could not preflight nested weapon payload %s%s",
					container_entry, "");
			}
			free(container_bytes);
			free(containers.entries);
			return 0;
		}
		enum_result = modArchiveMemForEachEntry(container_bytes, container_size,
			collectEmbeddedEffectArchive, &effects);
		if (enum_result != MODARCHIVE_OK) {
			nestedSetErr(err, err_cap,
				effects.allocation_failed
					? "out of memory enumerating effects in %s%s"
					: "could not enumerate effects in %s%s",
				container_entry, "");
			free(effects.entries);
			free(container_bytes);
			free(containers.entries);
			return 0;
		}

		for (size_t effect_index = 0; effect_index < effects.count;
				effect_index++) {
			const char *effect_entry = effects.entries[effect_index];
			weapon_nested_preflight_t *p;
			u32 descriptor_size = 0;
			const char *descriptor_leaf = NULL;
			char *descriptor = NULL;
			const char *catalog_id;

			if (!weaponNestedPendingReserve(pending, pending_capacity,
					*pending_count + 1)) {
				nestedSetErr(err, err_cap,
					"out of memory preflighting recursive effects for %s%s",
					weapon_id, "");
				free(effects.entries);
				free(container_bytes);
				free(containers.entries);
				return 0;
			}
			p = &(*pending)[*pending_count];
			p->expected_type = ASSET_EFFECT;
			p->bytes = modArchiveExtractMemAlloc(container_bytes, container_size,
				effect_entry, &p->size);
			if (!p->bytes || p->size == 0
					|| assetArchiveValidateBytes(p->bytes, p->size,
						effect_entry, ASSET_ARCHIVE_VALIDATE_RELEASE,
						err, err_cap) != 0) {
				if (!err || !err[0]) {
					nestedSetErr(err, err_cap,
						"could not preflight recursive effect %s%s",
						effect_entry, "");
				}
				free(effects.entries);
				free(container_bytes);
				free(containers.entries);
				return 0;
			}
			descriptor = assetArchiveExtractDescriptorMemAlloc(p->bytes, p->size,
				effect_entry, ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size,
				&descriptor_leaf);
			if (!descriptor || !iniParseBuffer(descriptor_leaf, descriptor,
					descriptor_size, &p->ini)) {
				free(descriptor);
				nestedSetErr(err, err_cap,
					"invalid public descriptor in recursive effect %s%s",
					effect_entry, "");
				free(effects.entries);
				free(container_bytes);
				free(containers.entries);
				return 0;
			}
			free(descriptor);
			if (sectionToType(p->ini.type) != ASSET_EFFECT) {
				nestedSetErr(err, err_cap,
					"recursive nested effect type mismatch %s%s",
					effect_entry, "");
				free(effects.entries);
				free(container_bytes);
				free(containers.entries);
				return 0;
			}
			catalog_id = iniGet(&p->ini, "catalog_id",
				iniGet(&p->ini, "id", ""));
			if (!catalog_id[0] || !pdEffectCatalogIdValid(catalog_id)
					|| !catalogIdSharesNamespace(weapon_id, catalog_id)) {
				nestedSetErr(err, err_cap,
					"recursive effect %s has missing or foreign catalog ID %s",
					effect_entry, catalog_id);
				free(effects.entries);
				free(container_bytes);
				free(containers.entries);
				return 0;
			}
			for (size_t prior = 0; prior < *pending_count; prior++) {
				if (strcmp((*pending)[prior].catalog_id, catalog_id) == 0) {
					nestedSetErr(err, err_cap,
						"duplicate recursive nested dependency ID %s%s",
						catalog_id, "");
					free(effects.entries);
					free(container_bytes);
					free(containers.entries);
					return 0;
				}
			}
			if (!assetPathCopyChecked(p->catalog_id, sizeof(p->catalog_id),
					catalog_id)
					|| !assetPathJoinChecked(p->archive_ref, FS_MAXPATH,
						container_ref, "::", effect_entry)
					|| !assetPathJoinChecked(p->descriptor_ref, FS_MAXPATH,
						p->archive_ref, "::", descriptor_leaf
							? descriptor_leaf
							: assetArchiveDescriptorForPath(effect_entry))) {
				nestedSetErr(err, err_cap,
					"recursive effect archive chain exceeds capacity %s%s",
					effect_entry, "");
				free(effects.entries);
				free(container_bytes);
				free(containers.entries);
				return 0;
			}
			if (!effectNestedPrepareBytes(&p->effect_nested, p->catalog_id,
					p->archive_ref, p->bytes, p->size, err, err_cap)) {
				free(effects.entries);
				free(container_bytes);
				free(containers.entries);
				return 0;
			}
			const asset_entry_t *existing = assetCatalogResolve(p->catalog_id);
			if (existing) {
				if (existing->type != ASSET_EFFECT || (!existing->bundled
						&& !nestedContentMatchesExisting(existing, p->bytes,
							p->size))) {
					nestedSetErr(err, err_cap,
						"recursive nested effect ID collision %s%s",
						p->catalog_id, "");
					free(effects.entries);
					free(container_bytes);
					free(containers.entries);
					return 0;
				}
				p->existing = 1;
			}
			(*pending_count)++;
		}
		free(effects.entries);
		free(container_bytes);
	}
	free(containers.entries);
	return 1;
}

static s32 weaponNestedOrdinaryBundledRow(const asset_entry_t *entry, asset_type_e type)
{
	return entry && entry->type == type && entry->enabled && entry->bundled
		&& !entry->temporary && !entry->grid_offer_only;
}

static s32 weaponNestedCanCheckpointRow(const char *id, asset_type_e type,
		const ini_section_t *ini, s32 existing)
{
	/* ResolveAny includes disabled rows. An occupied identity the public
	 * preflight treated as absent must use the general transaction. */
	const asset_entry_t *entry = assetCatalogResolveAny(id);
	return !iniGet(ini, GRID_ARENA_PROVENANCE_KEY, NULL)
		&& (existing ? weaponNestedOrdinaryBundledRow(entry, type) : !entry);
}

static s32 weaponNestedBeginBootTransaction(external_scan_transaction_t *transaction,
		const char *weapon_id, const weapon_nested_preflight_t *pending, size_t count)
{
	const u64 started = sysGetMicroseconds();
	const asset_entry_t *owner = assetCatalogResolveAny(weapon_id);
	if (!weaponNestedOrdinaryBundledRow(owner, ASSET_WEAPON)) {
		return externalScanTransactionBegin(transaction);
	}
	size_t row_count = 1; /* Only the owner and reused mesh metadata can change. */
	for (size_t i = 0; i < count; i++) {
		const weapon_nested_preflight_t *member = &pending[i];
		if (!weaponNestedCanCheckpointRow(member->catalog_id, member->expected_type,
				&member->ini, member->existing)) {
			return externalScanTransactionBegin(transaction);
		}
		for (size_t j = 0; j < member->effect_nested.child_count; j++) {
			const effect_nested_child_t *child = &member->effect_nested.children[j];
			if (!weaponNestedCanCheckpointRow(child->catalog_id, child->expected_type,
					&child->ini, child->existing)) {
				return externalScanTransactionBegin(transaction);
			}
		}
		if (member->existing && member->has_mesh_source) row_count++;
	}
	if (row_count > INT_MAX || row_count > SIZE_MAX / sizeof(*transaction->rows)) return 0;
	transaction->rows = malloc(row_count * sizeof(*transaction->rows));
	if (!transaction->rows) return 0;
	catalogEntrySnapshotCapture(&transaction->rows[transaction->row_count++], owner);
	for (size_t i = 0; i < count; i++) {
		const weapon_nested_preflight_t *member = &pending[i];
		if (member->existing && member->has_mesh_source) {
			const asset_entry_t *mesh = assetCatalogResolveAny(member->catalog_id);
			catalogEntrySnapshotCapture(&transaction->rows[transaction->row_count++], mesh);
		}
	}
	transaction->weapon_rows_begin = assetCatalogGetPoolSize();
	const u64 rows_finished = sysGetMicroseconds();
	if (!externalScanTransactionCaptureSharedState(transaction)) {
		externalScanTransactionDestroy(transaction);
		return 0;
	}
	transaction->active = 1;
	sysLogPrintf(LOG_VERBOSE, "CATALOG.CHECKPOINT: weapon_scoped_rows=%d rows_us=%llu total_us=%llu",
		transaction->row_count, (unsigned long long)(rows_finished - started),
		(unsigned long long)(sysGetMicroseconds() - started));
	return 1;
}

static s32 registerWeaponNestedDependenciesWithAnimationBatch(const char *weapon_id,
		const char *weapon_archive, s32 bundled, char *err, size_t err_cap,
		loader_animation_source_batch_t *batch, external_scan_transaction_t *boot_transaction)
{
	u32 weapon_size = 0;
	void *weapon_bytes = NULL;
	weapon_nested_media_scan_t scan;
	weapon_nested_preflight_t *pending = NULL;
	size_t pending_count = 0;
	size_t pending_capacity = 0;
	file_provider_checkpoint_t provider_checkpoint;
	s32 provider_checkpoint_valid = 0;
	void *model_slot_checkpoint = NULL;
	weapon_nested_mesh_edges_t mesh_edges = {0};
	char held_mesh_id[CATALOG_ID_LEN] = {0};
	char owner_copy[CATALOG_ID_LEN], archive_copy[FS_MAXPATH];
	s32 previous_held_filenum = -1, held_filenum_changed = 0;
	s32 result = -1;

	if (err && err_cap) err[0] = '\0';
	memset(&scan, 0, sizeof(scan));
	if (!weapon_id || !weapon_id[0] || !weapon_archive || !weapon_archive[0]) {
		nestedSetErr(err, err_cap, "nested dependency scan missing %s%s",
			weapon_id, weapon_archive);
		return -1;
	}
	/* Registration may grow the catalog that owns either incoming string. */
	if (!assetPathCopyChecked(owner_copy, sizeof(owner_copy), weapon_id)
			|| !assetPathCopyChecked(archive_copy, sizeof(archive_copy), weapon_archive)) {
		nestedSetErr(err, err_cap, "weapon closure input exceeds capacity %s%s", weapon_id, "");
		return -1;
	}
	weapon_id = owner_copy;
	weapon_archive = archive_copy;
	weapon_bytes = fsFileLoad(weapon_archive, &weapon_size);
	if (!weapon_bytes || weapon_size == 0) {
		nestedSetErr(err, err_cap, "could not read weapon archive %s%s",
			weapon_archive, "");
		goto done;
	}
	if (modArchiveMemForEachEntry(weapon_bytes, weapon_size,
			collectWeaponNestedMedia, &scan) != MODARCHIVE_OK) {
		nestedSetErr(err, err_cap,
			scan.allocation_failed
				? "out of memory enumerating weapon archive %s%s"
				: "could not enumerate weapon archive %s%s",
			weapon_archive, "");
		goto done;
	}
	if (scan.count > (size_t)INT_MAX
			|| scan.count > SIZE_MAX / sizeof(*pending)) {
		nestedSetErr(err, err_cap, "weapon %s nested media count overflows %s",
			weapon_id, "platform capacity");
		goto done;
	}
	if (scan.count) {
		if (!weaponNestedPendingReserve(&pending, &pending_capacity,
				scan.count)) {
			nestedSetErr(err, err_cap,
				"out of memory preflighting weapon %s%s", weapon_id, "");
			goto done;
		}
	}

	/* Preflight the complete closure without mutating catalog state. Keep the
	 * production order explicit: UI, audio, animation, mesh, entity, projectile,
	 * then effect. Entity precedes projectile so a directly activated projectile
	 * never observes its embedded transition target as an unpublished sibling.
	 * A corrupt late member still cannot leak an earlier row because publication
	 * begins only after every pass and recursive effect dependency has passed. */
	for (s32 pass = 0; pass < 7; pass++) {
	for (size_t i = 0; i < scan.count; i++) {
		asset_type_e expected_type = ASSET_NONE;
		const char *entry_name = scan.entries[i];
		weapon_nested_preflight_t *p;
		weaponNestedMediaType(entry_name, &expected_type);
		if ((pass == 0 && expected_type != ASSET_UI)
				|| (pass == 1 && expected_type != ASSET_AUDIO)
				|| (pass == 2 && expected_type != ASSET_ANIMATION)
				|| (pass == 3 && expected_type != ASSET_MODEL)
				|| (pass == 4 && expected_type != ASSET_ENTITY)
				|| (pass == 5 && expected_type != ASSET_PROJECTILE)
				|| (pass == 6 && expected_type != ASSET_EFFECT)) continue;
		p = &pending[pending_count];
		s32 prepared = weaponNestedPrepareMember(p, pending, pending_count,
			weapon_id, weapon_archive, weapon_bytes, weapon_size,
			entry_name, expected_type, bundled, err, err_cap);
		if (!prepared) goto rollback;
		if (prepared == 2) {
			free(p->bytes);
			memset(p, 0, sizeof(*p));
			continue;
		}
		pending_count++;
	}
	}
	if (!weaponNestedPrepareRecursiveMeshes(&pending, &pending_count,
			&pending_capacity, &mesh_edges, weapon_id, bundled, err, err_cap)
			|| !weaponNestedPrepareMeshConsumers(pending, pending_count, &mesh_edges,
				weapon_id, weapon_archive, weapon_bytes, weapon_size, held_mesh_id,
				err, err_cap)) goto rollback;
	if (!weaponNestedPrepareRecursiveEffects(&pending, &pending_count,
			&pending_capacity, weapon_id, weapon_archive, weapon_bytes,
			weapon_size, err, err_cap)) {
		goto rollback;
	}
	/* Source qualification is complete-closure preflight for reused, direct,
	 * and recursively discovered rows. Publication below cannot discover a
	 * late VFS-capacity failure after an earlier sibling has mutated state. */
	for (size_t i = 0; i < pending_count; i++) {
		if (!qualifyTypedArchiveSourcePaths(&pending[i].ini,
				pending[i].archive_ref)) {
			nestedSetErr(err, err_cap,
				"nested dependency path exceeds repository capacity: %s%s",
				pending[i].catalog_id, "");
			goto rollback;
		}
	}
	if (boot_transaction && !weaponNestedBeginBootTransaction(boot_transaction,
			weapon_id, pending, pending_count)) {
		nestedSetErr(err, err_cap, "could not checkpoint bundled weapon %s%s", weapon_id, "");
		goto rollback;
	}
	{
		if (mesh_edges.count > SIZE_MAX - pending_count) goto rollback;
		size_t edge_count = pending_count + mesh_edges.count;
		for (size_t i = 0; i < pending_count; i++) {
			if (pending[i].effect_nested.deps.count >
					(SIZE_MAX - edge_count) / 2) {
				nestedSetErr(err, err_cap, "nested dependency edge count overflow %s%s",
					weapon_id, "");
				goto rollback;
			}
			edge_count += pending[i].effect_nested.deps.count * 2;
		}
		if (edge_count > (size_t)INT_MAX
				|| !catalogDepReserve((s32)edge_count)) {
		nestedSetErr(err, err_cap, "could not reserve nested edges for %s%s",
			weapon_id, "");
		goto rollback;
		}
	}
	if (!fileProviderCheckpointCreate(&provider_checkpoint)) {
		nestedSetErr(err, err_cap,
			"could not checkpoint nested source providers for %s%s",
			weapon_id, "");
		goto rollback;
	}
	provider_checkpoint_valid = 1;
	model_slot_checkpoint = assetCatalogSnapshotCustomModelSlots();
	if (!model_slot_checkpoint) {
		nestedSetErr(err, err_cap, "could not checkpoint graph model slots for %s%s", weapon_id, "");
		goto rollback;
	}

	for (size_t i = 0; i < pending_count; i++) {
		weapon_nested_preflight_t *p = &pending[i];
		if (!p->existing) {
			p->created = 1; /* rollback even if registerComponent fails late */
			if (!registerComponentWithAnimationBatch(&p->ini, p->archive_ref,
					bundled ? "base" : weapon_id, p->descriptor_ref, batch)) {
				nestedSetErr(err, err_cap,
					"could not register nested dependency %s%s", p->catalog_id, "");
				goto rollback;
			}
			asset_entry_t *entry = assetCatalogGetMutable(p->catalog_id);
			if (!entry) {
				nestedSetErr(err, err_cap,
					"nested dependency disappeared after register %s%s",
					p->catalog_id, "");
				goto rollback;
			}
			entry->bundled = bundled ? 1 : 0;
			entry->enabled = 1;
			entry->temporary = 0;
		}
		if (p->has_mesh_source) {
			asset_entry_t *entry = assetCatalogGetMutable(p->catalog_id);
			const char *source_action = "claimed";
			const char *source_path = p->mesh_source.source_path;
			if (!entry || entry->type != ASSET_MODEL) {
				nestedSetErr(err, err_cap,
					"nested mesh row disappeared before source bind %s%s",
					p->catalog_id, "");
				goto rollback;
			}
			if (p->existing) {
				catalogEntrySnapshotCapture(&p->existing_snapshot, entry);
				p->existing_snapshot_valid = 1;
			}
			s32 selected_held = held_mesh_id[0]
				&& strcmp(p->catalog_id, held_mesh_id) == 0;
			if ((p->needs_graph_model && entry->runtime_index < 0)
					|| (selected_held && p->mesh_source.source_filenum <= 0)) {
				if (bundled || entry->bundled) {
					nestedSetErr(err, err_cap,
						"base mesh %s has no required native model identity %s",
						p->catalog_id, "");
					goto rollback;
				}
				s32 slot = entry->runtime_index >= 0 ? entry->runtime_index
					: assetCatalogResolveModelPrivateSlot(p->catalog_id);
				if (slot < 0) {
					nestedSetErr(err, err_cap, "no private model slot for %s%s", p->catalog_id, "");
					goto rollback;
				}
				entry->runtime_index = slot;
				if (p->mesh_source.source_filenum <= 0) {
					p->mesh_source.source_filenum = assetCatalogModelPrivateSourceFilenum(slot);
					if (p->mesh_source.source_filenum <= 0) goto rollback;
				}
			}
			if (!loaderWalkerBindMeshSource(entry, &p->mesh_source, 1,
					err, err_cap)) goto rollback;
			source_path = fileProviderPath(entry->source.primary);
			if (p->existing) {
				source_action = "kept";
			}
			if (p->existing && strcmp(source_path, p->mesh_source.source_path) == 0) {
				/* Preserve runtime identity/lifecycle while making the typed
				 * source and its public descriptor discoverable diagnostics. */
				if (!assetPathCopyChecked(entry->dirpath,
						sizeof(entry->dirpath), p->archive_ref)
						|| !assetPathCopyChecked(entry->descriptor_path,
							sizeof(entry->descriptor_path), p->descriptor_ref)) {
					nestedSetErr(err, err_cap,
						"nested mesh source metadata exceeds capacity %s%s",
						p->catalog_id, "");
					goto rollback;
				}
				entry->model_scale = iniGetFloat(&p->ini, "model_scale",
					entry->model_scale);
			}
			sysLogPrintf(LOG_NOTE,
				"PDWEAPON.NESTED.MESH_SOURCE: owner=%s id=%s action=%s filenum=%d source=%s",
				weapon_id, p->catalog_id, source_action,
				p->mesh_source.source_filenum,
				source_path ? source_path : "");
		}
		if (p->expected_type == ASSET_EFFECT
				&& !effectNestedCommit(&p->effect_nested, bundled, err, err_cap)) {
			goto rollback;
		}
		if (!catalogDepContains(weapon_id, p->catalog_id)) {
			if (!catalogDepRegisterTyped(weapon_id, p->catalog_id,
					p->expected_type, bundled ? 1 : 0)) {
				nestedSetErr(err, err_cap,
					"could not register nested dependency edge %s%s",
					p->catalog_id, "");
				goto rollback;
			}
			p->edge_created = 1;
		}
		if (p->effect_nested.deps.count) {
			p->effect_dep_edges_created = (u8 *)calloc(
				p->effect_nested.deps.count, 1);
			if (!p->effect_dep_edges_created) {
				nestedSetErr(err, err_cap, "out of memory tracking nested effect deps %s%s",
					p->catalog_id, "");
				goto rollback;
			}
			for (size_t j = 0; j < p->effect_nested.deps.count; j++) {
				effect_dependency_t *dep = &p->effect_nested.deps.items[j];
				if (!catalogDepContains(weapon_id, dep->catalog_id)) {
					if (!catalogDepRegisterTyped(weapon_id, dep->catalog_id,
							dep->type, bundled ? 1 : 0)) {
						nestedSetErr(err, err_cap,
							"could not register nested effect dependency %s%s",
							dep->catalog_id, "");
						goto rollback;
					}
					p->effect_dep_edges_created[j] = 1;
				}
			}
		}
		sysLogPrintf(LOG_NOTE,
			"PDWEAPON.NESTED.REGISTER: owner=%s id=%s type=%d source=%s",
			weapon_id, p->catalog_id, (s32)p->expected_type, p->archive_ref);
	}
	for (size_t i = 0; i < mesh_edges.count; i++) {
		weapon_nested_mesh_edge_t *edge = &mesh_edges.items[i];
		if (catalogDepContains(edge->owner, edge->child)) continue;
		if (!catalogDepRegisterTyped(edge->owner, edge->child, ASSET_MODEL, bundled ? 1 : 0)) {
			nestedSetErr(err, err_cap, "could not register payload mesh edge %s -> %s", edge->owner, edge->child);
			goto rollback;
		}
		edge->created = 1;
	}
	if (held_mesh_id[0]) {
		const asset_entry_t *mesh = assetCatalogResolve(held_mesh_id);
		asset_entry_t *owner = assetCatalogGetMutable(weapon_id);
		if (!owner || owner->type != ASSET_WEAPON || !mesh || mesh->source_filenum <= 0
				|| ((bundled || owner->bundled) && owner->source_filenum > 0
					&& owner->source_filenum != mesh->source_filenum)) {
			nestedSetErr(err, err_cap, "weapon %s has invalid selected held mesh %s", weapon_id, held_mesh_id);
			goto rollback;
		}
		previous_held_filenum = owner->source_filenum;
		held_filenum_changed = 1;
		owner->source_filenum = mesh->source_filenum;
	}
	for (size_t i = 0; i < pending_count; i++) {
		if (pending[i].expected_type == ASSET_EFFECT) {
			catalogDepPruneOwner(pending[i].catalog_id,
				keepCurrentEffectDependency, &pending[i].effect_nested.deps);
		}
	}
	result = (s32)pending_count;
	goto done;

rollback:
	if (held_filenum_changed) {
		asset_entry_t *owner = assetCatalogGetMutable(weapon_id);
		if (owner) owner->source_filenum = previous_held_filenum;
	}
	for (size_t i = mesh_edges.count; i-- > 0;) {
		if (mesh_edges.items[i].created)
			catalogDepUnregister(mesh_edges.items[i].owner, mesh_edges.items[i].child);
	}
	for (size_t i = pending_count; i-- > 0; ) {
		for (size_t j = pending[i].effect_nested.deps.count; j-- > 0; ) {
			if (pending[i].effect_dep_edges_created
					&& pending[i].effect_dep_edges_created[j]) {
				catalogDepUnregister(weapon_id,
					pending[i].effect_nested.deps.items[j].catalog_id);
			}
		}
		effectNestedRollback(&pending[i].effect_nested);
		if (pending[i].edge_created) {
			catalogDepUnregister(weapon_id, pending[i].catalog_id);
		}
		if (pending[i].created) {
			assetCatalogUnregister(pending[i].catalog_id);
		} else if (pending[i].existing_snapshot_valid) {
			asset_entry_t *entry = assetCatalogGetMutable(pending[i].catalog_id);
			if (entry && !catalogEntrySnapshotRestorePreserved(entry, &pending[i].existing_snapshot))
				sysLoudFailf("PDWEAPON.NESTED.ROLLBACK",
					"cannot restore source metadata for %s: runtime ownership changed; current owner preserved",
					pending[i].catalog_id);
		}
	}
	if (model_slot_checkpoint && !assetCatalogRestoreCustomModelSlots(model_slot_checkpoint)) {
		sysLoudFailf("PDWEAPON.NESTED.ROLLBACK", "could not restore graph model slots for owner=%s", weapon_id);
	}
	if (provider_checkpoint_valid) {
		if (!fileProviderCheckpointRestore(&provider_checkpoint)) {
			sysLoudFailf("PDWEAPON.NESTED.ROLLBACK",
				"could not restore FileProvider checkpoint for owner=%s",
				weapon_id);
			if (err && err_cap && !err[0]) {
				nestedSetErr(err, err_cap,
					"nested source provider rollback failed for %s%s",
					weapon_id, "");
			}
		}
	}
done:
	assetCatalogDestroyCustomModelSlotSnapshot(model_slot_checkpoint);
	free(mesh_edges.items);
	for (size_t i = 0; i < pending_capacity; i++) {
		free(pending ? pending[i].bytes : NULL);
		free(pending ? pending[i].effect_dep_edges_created : NULL);
		if (pending) effectNestedFree(&pending[i].effect_nested);
	}
	free(pending);
	free(scan.entries);
	if (weapon_bytes) sysMemFree(weapon_bytes);
	return result;
}

static s32 registerWeaponNestedDependencies(const char *weapon_id,
        const char *weapon_archive, s32 bundled, s32 boot, char *err, size_t err_cap)
{
    char owner[CATALOG_ID_LEN];
    if (!weapon_id || !assetPathCopyChecked(owner, sizeof(owner), weapon_id)) return -1;
    const s32 scoped_boot = boot && !strncmp(owner, "base:", 5)
        && weapon_archive && weapon_archive[0] && !strstr(weapon_archive, "::")
        && !modVfsCanResolve(weapon_archive)
        && weaponNestedOrdinaryBundledRow(assetCatalogResolveAny(owner), ASSET_WEAPON);
    external_scan_transaction_t transaction = {0};
    loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
    if (!batch || (!scoped_boot && !externalScanTransactionBegin(&transaction))) {
        loaderAnimationSourceBatchDestroy(batch);
        return -1;
    }
    s32 result = registerWeaponNestedDependenciesWithAnimationBatch(owner,
        weapon_archive, bundled, err, err_cap, batch, scoped_boot ? &transaction : NULL);
    if (result >= 0 && !loaderAnimationSourceBatchCommit(batch)) {
        nestedSetErr(err, err_cap, "weapon command closure rejected: %s%s", owner, "");
        result = -1;
    }
    if (result < 0 && transaction.active && !externalScanTransactionRollback(&transaction))
        sysLoudFailf("PDWEAPON.NESTED.ROLLBACK", "could not restore complete nested source transaction");
    externalScanTransactionDestroy(&transaction);
    loaderAnimationSourceBatchDestroy(batch);
    return result;
}

s32 assetCatalogRegisterWeaponNestedDependencies(const char *weapon_id,
        const char *weapon_archive, s32 bundled, char *err, size_t err_cap)
{
    return registerWeaponNestedDependencies(weapon_id, weapon_archive, bundled, 0, err, err_cap);
}

s32 assetCatalogRegisterBootWeaponNestedDependencies(const char *weapon_id,
        const char *weapon_archive, char *err, size_t err_cap)
{
    return registerWeaponNestedDependencies(weapon_id, weapon_archive, 1, 1, err, err_cap);
}

/* ========================================================================
 * Nested theme dependency registration
 * ======================================================================== */

#define THEME_NESTED_ROLE_COUNT 4

typedef struct theme_nested_role {
	const char *key;
	const char *extension;
	asset_type_e expected_type;
} theme_nested_role_t;

typedef struct theme_nested_preflight {
	char member[FS_MAXPATH];
	char catalog_id[CATALOG_ID_LEN];
	char descriptor_leaf[64];
	ini_section_t ini;
	asset_type_e expected_type;
	s32 existing;
	s32 registered;
} theme_nested_preflight_t;

static const theme_nested_role_t s_ThemeNestedRoles[THEME_NESTED_ROLE_COUNT] = {
	{ "ui_archive", ".pdui", ASSET_UI },
	{ "font_archive", ".pdfont", ASSET_FONT },
	{ "audio_archive", ".pdsfx", ASSET_AUDIO },
	{ "music_archive", ".pdsong", ASSET_AUDIO },
};

s32 assetCatalogRegisterThemeNestedDependencies(const char *theme_id,
		const char *theme_archive, s32 bundled, char *err, size_t err_cap)
{
	u32 theme_size = 0;
	void *theme_bytes = NULL;
	u32 descriptor_size = 0;
	const char *descriptor_leaf = NULL;
	char *descriptor = NULL;
	ini_section_t theme_ini;
	theme_nested_preflight_t pending[THEME_NESTED_ROLE_COUNT];
	s32 pending_count = 0;
	s32 result = -1;

	if (err && err_cap) err[0] = '\0';
	memset(pending, 0, sizeof(pending));
	if (!theme_id || !theme_id[0] || !theme_archive || !theme_archive[0]) {
		nestedSetErr(err, err_cap, "theme dependency scan missing %s%s",
			theme_id, theme_archive);
		return -1;
	}

	theme_bytes = fsFileLoad(theme_archive, &theme_size);
	if (!theme_bytes || theme_size == 0) {
		nestedSetErr(err, err_cap, "could not read theme archive %s%s",
			theme_archive, "");
		goto done;
	}
	if (assetArchiveValidateBytes(theme_bytes, theme_size, theme_archive,
			ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) != 0) {
		goto done;
	}
	descriptor = assetArchiveExtractDescriptorMemAlloc(theme_bytes, theme_size,
		theme_archive, ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size,
		&descriptor_leaf);
	if (!descriptor || !descriptor_leaf
			|| !iniParseBuffer(descriptor_leaf, descriptor, descriptor_size,
				&theme_ini)
			|| sectionToType(theme_ini.type) != ASSET_THEME) {
		nestedSetErr(err, err_cap, "invalid public theme descriptor in %s%s",
			theme_archive, "");
		goto done;
	}
	{
		const char *declared_id = iniGet(&theme_ini, "catalog_id",
			iniGet(&theme_ini, "id", ""));
		if (!declared_id[0] || strcmp(declared_id, theme_id) != 0) {
			nestedSetErr(err, err_cap,
				"theme catalog identity mismatch %s%s", theme_id, declared_id);
			goto done;
		}
	}
	if (iniGet(&theme_ini, "effect_archive", "")[0]) {
		nestedSetErr(err, err_cap,
			"theme effect_archive is retired; use inline %s%s",
			"caustics/borderEffects", "");
		goto done;
	}

	/* Validate every declaration and collision before registering any child or
	 * edge.  User/data errors therefore cannot expose a partially owned theme. */
	for (s32 role_index = 0; role_index < THEME_NESTED_ROLE_COUNT;
			role_index++) {
		const theme_nested_role_t *role = &s_ThemeNestedRoles[role_index];
		const char *member = iniGet(&theme_ini, role->key, "");
		u32 nested_size = 0;
		void *nested = NULL;
		u32 nested_descriptor_size = 0;
		const char *nested_descriptor_leaf = NULL;
		char *nested_descriptor = NULL;
		const char *catalog_id;
		const asset_entry_t *existing;

		if (!member[0]) continue;
		if (!archiveInnerPathIsSafe(member)
				|| !pathEndsWithNoCase(member, role->extension)) {
			nestedSetErr(err, err_cap, "theme role has invalid archive %s%s",
				role->key, member);
			goto done;
		}
		nested = modArchiveExtractMemAlloc(theme_bytes, theme_size, member,
			&nested_size);
		if (!nested || nested_size == 0) {
			free(nested);
			nestedSetErr(err, err_cap, "theme dependency is missing %s%s",
				role->key, member);
			goto done;
		}
		if (assetArchiveValidateBytes(nested, nested_size, member,
				ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) != 0) {
			free(nested);
			goto done;
		}
		nested_descriptor = assetArchiveExtractDescriptorMemAlloc(nested,
			nested_size, member, ASSET_ARCHIVE_VALIDATE_RELEASE,
			&nested_descriptor_size, &nested_descriptor_leaf);
		if (!nested_descriptor || !nested_descriptor_leaf
				|| !iniParseBuffer(nested_descriptor_leaf, nested_descriptor,
					nested_descriptor_size, &pending[pending_count].ini)
				|| sectionToType(pending[pending_count].ini.type)
					!= role->expected_type) {
			free(nested_descriptor);
			free(nested);
			nestedSetErr(err, err_cap, "theme dependency type mismatch %s%s",
				role->key, member);
			goto done;
		}
		free(nested_descriptor);
		catalog_id = iniGet(&pending[pending_count].ini, "catalog_id",
			iniGet(&pending[pending_count].ini, "id", ""));
		if (!catalog_id[0]) {
			free(nested);
			nestedSetErr(err, err_cap,
				"theme dependency has missing catalog ID %s%s",
				role->key, catalog_id);
			goto done;
		}
		for (s32 i = 0; i < pending_count; i++) {
			if (strcmp(pending[i].catalog_id, catalog_id) == 0) {
				free(nested);
				nestedSetErr(err, err_cap,
					"theme roles reuse dependency ID %s%s", catalog_id, "");
				goto done;
			}
		}
		existing = assetCatalogResolve(catalog_id);
		if (existing && (existing->type != role->expected_type
				|| !nestedContentMatchesExisting(existing, nested, nested_size))) {
			free(nested);
			nestedSetErr(err, err_cap,
				"theme dependency ID/content collision %s%s", catalog_id, "");
			goto done;
		}

		strncpy(pending[pending_count].member, member,
			sizeof(pending[pending_count].member) - 1);
		strncpy(pending[pending_count].catalog_id, catalog_id,
			sizeof(pending[pending_count].catalog_id) - 1);
		strncpy(pending[pending_count].descriptor_leaf,
			nested_descriptor_leaf,
			sizeof(pending[pending_count].descriptor_leaf) - 1);
		pending[pending_count].expected_type = role->expected_type;
		pending[pending_count].existing = existing ? 1 : 0;
		pending_count++;
		free(nested);
	}
	if (!catalogDepReserve(pending_count)) {
		nestedSetErr(err, err_cap,
			"could not reserve theme dependency edges %s%s", theme_id, "");
		goto done;
	}

	for (s32 i = 0; i < pending_count; i++) {
		char archive_ref[FS_MAXPATH * 2 + 4];
		char descriptor_ref[FS_MAXPATH * 2 + 132];
		if (!assetPathJoinChecked(archive_ref, FS_MAXPATH, theme_archive, "::",
				pending[i].member) ||
				!assetPathJoinChecked(descriptor_ref, FS_MAXPATH, archive_ref, "::",
					pending[i].descriptor_leaf)) {
			nestedSetErr(err, err_cap,
				"theme dependency archive chain exceeds capacity: %s%s",
				pending[i].catalog_id, "");
			goto done;
		}
		if (!pending[i].existing) {
			if (!qualifyTypedArchiveSourcePaths(&pending[i].ini, archive_ref)) {
				nestedSetErr(err, err_cap,
					"theme dependency path exceeds repository capacity: %s%s",
					pending[i].catalog_id, "");
				goto done;
			}
			if (!registerComponent(&pending[i].ini, archive_ref,
					bundled ? "base" : theme_id, descriptor_ref)) {
				nestedSetErr(err, err_cap,
					"could not register theme dependency %s%s",
					pending[i].catalog_id, "");
				goto done;
			}
			asset_entry_t *child = assetCatalogGetMutable(pending[i].catalog_id);
			if (!child || child->type != pending[i].expected_type) {
				nestedSetErr(err, err_cap,
					"theme dependency disappeared after register %s%s",
					pending[i].catalog_id, "");
				goto done;
			}
			child->bundled = bundled ? 1 : 0;
			/* Keep newly created rows inert until every child has registered and
			 * every ownership edge can be committed. */
			child->enabled = 0;
			child->temporary = 0;
			pending[i].registered = 1;
		}
	}
	/* Commit ownership edges only after every new child row is registered. */
	for (s32 i = 0; i < pending_count; i++) {
		char archive_ref[FS_MAXPATH * 2 + 4];
		if (!assetPathJoinChecked(archive_ref, FS_MAXPATH, theme_archive, "::",
				pending[i].member)) {
			nestedSetErr(err, err_cap,
				"theme dependency commit path exceeds capacity: %s%s",
				pending[i].catalog_id, "");
			goto done;
		}
		catalogDepRegister(theme_id, pending[i].catalog_id,
			bundled ? 1 : 0);
		if (pending[i].registered) {
			asset_entry_t *child = assetCatalogGetMutable(pending[i].catalog_id);
			if (child) child->enabled = 1;
		}
		sysLogPrintf(LOG_NOTE,
			"PDTHEME.NESTED.REGISTER: owner=%s id=%s type=%d source=%s",
			theme_id, pending[i].catalog_id, (s32)pending[i].expected_type,
			archive_ref);
	}
	result = pending_count;

done:
	if (result < 0) {
		/* Catalog rows are stable-address records and cannot be removed safely.
		 * Any row created by a failed transaction remains disabled and therefore
		 * cannot become a partially owned production dependency. */
		for (s32 i = 0; i < pending_count; i++) {
			if (pending[i].registered) {
				asset_entry_t *child = assetCatalogGetMutable(
					pending[i].catalog_id);
				if (child) child->enabled = 0;
			}
		}
	}
	free(descriptor);
	if (theme_bytes) sysMemFree(theme_bytes);
	return result;
}

/* ========================================================================
 * Directory Scanning
 * ======================================================================== */

/**
 * Check if a path is a directory.
 */
static s32 isDirectory(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/**
 * Find and parse the .ini file in a component directory.
 * Looks for any file ending in .ini (there should be exactly one).
 */
static s32 findAndParseIni(const char *component_dir, ini_section_t *out,
                           char *out_path, size_t out_path_cap)
{
	DIR *dp = opendir(component_dir);
	if (!dp) {
		return 0;
	}

	char inibuf[FS_MAXPATH];
	s32 found = 0;
	struct dirent *ent;

	while ((ent = readdir(dp)) != NULL) {
		const char *name = ent->d_name;
		s32 len = (s32)strlen(name);

		if (len > 4 && strcmp(name + len - 4, ".ini") == 0) {
			if (!assetPathJoinChecked(inibuf, sizeof(inibuf), component_dir,
					"/", name)) break;
			found = iniParse(inibuf, out);
			if (found && out_path && out_path_cap > 0) {
				if (!assetPathCopyChecked(out_path, out_path_cap, inibuf)) found = 0;
			}
			break;
		}
	}

	closedir(dp);
	return found;
}

static s32 isRegularFile(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static s32 registerComponentIniFile(const char *component_dir,
                                    const char *ini_path,
                                    asset_type_e expected,
                                    const char *label,
                                    const char *mod_id,
		loader_animation_source_batch_t *batch)
{
	ini_section_t ini;
	if (!iniParse(ini_path, &ini)) {
		sysLogPrintf(LOG_WARNING,
			"assetcatalog_scanner: invalid external-layout INI '%s'",
			ini_path);
		return -1;
	}

	asset_type_e ini_type = sectionToType(ini.type);
	if (expected != ASSET_NONE && ini_type != expected) {
		sysLogPrintf(LOG_WARNING,
			"assetcatalog_scanner: external-layout type mismatch in '%s': "
			"expected %d, got [%s]=%d",
			label ? label : ini_path, expected, ini.type, ini_type);
		return -1;
	}

	return registerComponentWithAnimationBatch(&ini, component_dir, mod_id, ini_path, batch) ? 1 : -1;
}

static s32 scanExternalDescriptorChildren(const char *base_dir,
                                          const char *leaf,
                                          asset_type_e expected,
                                          const char *mod_id,
		loader_animation_source_batch_t *batch)
{
	if (!isDirectory(base_dir)) {
		return 0;
	}

	DIR *dp = opendir(base_dir);
	if (!dp) {
		return -1;
	}

	s32 count = 0;
	s32 rejected = 0;
	struct dirent *ent;
	char component_dir[FS_MAXPATH];
	char ini_path[FS_MAXPATH];
	char label[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		if (ent->d_name[0] == '.') {
			continue;
		}

		if (!assetPathJoinChecked(component_dir, sizeof(component_dir), base_dir,
				"/", ent->d_name)) {
			rejected = 1;
			continue;
		}
		if (!isDirectory(component_dir)) {
			continue;
		}

		if (!assetPathJoinChecked(ini_path, sizeof(ini_path), component_dir,
				"/", leaf)) {
			rejected = 1;
			continue;
		}
		if (!isRegularFile(ini_path)) {
			continue;
		}

		if (!assetPathJoinChecked(label, sizeof(label), ent->d_name, "/", leaf)) {
			rejected = 1;
			continue;
		}
		s32 candidate_result = registerComponentIniFile(component_dir, ini_path,
			expected, label, mod_id, batch);
		if (candidate_result < 0) rejected = 1;
		else count += candidate_result;
	}

	closedir(dp);
	return rejected ? -(count + 1) : count;
}

static s32 scanExternalDescriptorPath(const char *mod_dir,
                                      const char *relative_dir,
                                      const char *leaf,
                                      asset_type_e expected,
                                      const char *mod_id,
		loader_animation_source_batch_t *batch)
{
	char base_dir[FS_MAXPATH];
	if (!assetPathJoinChecked(base_dir, sizeof(base_dir), mod_dir, "/",
			relative_dir)) return -1;
	return scanExternalDescriptorChildren(base_dir, leaf, expected, mod_id, batch);
}

/*
 * Forge F1: the scenario inside a mod level.
 *
 * A base arena names a scenario that is also a file of its own in the
 * scenarios folder, and that file is what gets a catalog row. A level saved
 * by the mod author names a scenario that exists only as a member of its own
 * .pdarena (arena.ini's scenario_archive). Nothing registered that member, so
 * the scenario runtime found no source for the level's stage and the load
 * stopped at "missing scenario source".
 *
 * This registers the member as an ASSET_SCENARIO row under the id the arena
 * names, read from the member's own scenario.ini, with its files named
 * through the chain "<arena archive>::<scenario member>::<file>". The arena
 * and the scenario key their stage number to that same id, so the two rows
 * share one stage (CORE's rule, week note 2026-11-17 04:00 UTC).
 *
 * Only for an arena outside the base namespace whose scenario id is in its
 * own namespace: the id must be the mod's, never its template's. A scenario
 * some other source already registered (a file of its own beside the arena,
 * or an earlier scan of this archive) is left as it is.
 *
 * Returns 1 when a row was registered, 0 when there was nothing to do, and
 * -1 with the reason when the member is not a scenario this arena can use.
 */
static s32 registerArenaEmbeddedScenario(const char *arena_id, const char *mod_id,
		loader_animation_source_batch_t *batch, char *err, size_t err_cap)
{
	char owner[CATALOG_ID_LEN];
	char scenario_id[CATALOG_ID_LEN];
	char chain[FS_MAXPATH];
	char outer[FS_MAXPATH];
	char descriptor_ref[FS_MAXPATH];
	char component_dir[FS_MAXPATH];
	const asset_entry_t *arena = arena_id ? assetCatalogResolve(arena_id) : NULL;
	asset_entry_t *row;
	const char *descriptor_leaf = NULL;
	const char *found_id;
	const char *cut;
	char *descriptor = NULL;
	void *bytes = NULL;
	u32 size = 0;
	u32 descriptor_size = 0;
	ini_section_t ini;
	s32 result = -1;

	if (err && err_cap) err[0] = '\0';
	if (!arena || arena->type != ASSET_ARENA) {
		nestedSetErr(err, err_cap, "arena row is missing %s%s", arena_id ? arena_id : "", "");
		return -1;
	}
	if (!arena->ext.arena.scenario_id[0] || !arena->ext.arena.scenario_archive[0]) {
		return 0;
	}
	const s32 grid_offer_only = arena->grid_offer_only;
	/* Registration may grow the catalog that owns these strings. */
	if (!assetPathCopyChecked(owner, sizeof(owner), arena_id)
			|| !assetPathCopyChecked(scenario_id, sizeof(scenario_id),
				arena->ext.arena.scenario_id)
			|| !assetPathCopyChecked(chain, sizeof(chain),
				arena->ext.arena.scenario_archive)) {
		nestedSetErr(err, err_cap, "arena %s scenario reference exceeds capacity %s", arena_id, "");
		return -1;
	}
	cut = strstr(chain, "::");
	if (strncmp(owner, "base:", 5) == 0 || !catalogIdSharesNamespace(owner, scenario_id) || !cut) {
		return 0;
	}
	if (assetCatalogResolve(scenario_id)) {
		/* An in-place save can mark an already registered arena. Follow
		 * that mark only for the exact scenario row it owns. */
		row = assetCatalogGetMutable(scenario_id);
		size_t len = strlen(chain);
		if (row && row->type == ASSET_SCENARIO
				&& strncmp(row->descriptor_path, chain, len) == 0
				&& row->descriptor_path[len] == ':' && row->descriptor_path[len + 1] == ':') {
			row->grid_offer_only = grid_offer_only;
		}
		return 0;
	}

	bytes = fsFileLoad(chain, &size);
	if (!bytes || size == 0) {
		nestedSetErr(err, err_cap, "could not read the scenario inside %s: %s", owner, chain);
		goto done;
	}
	if (assetArchiveValidateBytes(bytes, size, chain, ASSET_ARCHIVE_VALIDATE_RELEASE,
			err, err_cap) != 0) {
		goto done;
	}
	descriptor = assetArchiveExtractDescriptorMemAlloc(bytes, size, chain,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size, &descriptor_leaf);
	if (!descriptor || !descriptor_leaf
			|| !iniParseBuffer(descriptor_leaf, descriptor, descriptor_size, &ini)) {
		nestedSetErr(err, err_cap, "the scenario inside %s has no valid descriptor: %s", owner, chain);
		goto done;
	}
	if (sectionToType(ini.type) != ASSET_SCENARIO) {
		nestedSetErr(err, err_cap, "the archive %s names as its scenario is not one: %s", owner, chain);
		goto done;
	}
	found_id = iniGet(&ini, "catalog_id", iniGet(&ini, "id", ""));
	if (strcmp(found_id, scenario_id) != 0) {
		nestedSetErr(err, err_cap, "the scenario inside the arena is %s, not the %s the arena names",
			found_id, scenario_id);
		goto done;
	}
	if ((size_t)(cut - chain) >= sizeof(outer)
			|| !qualifyTypedArchiveSourcePaths(&ini, chain)
			|| !assetPathJoinChecked(descriptor_ref, sizeof(descriptor_ref), chain, "::",
				descriptor_leaf)) {
		nestedSetErr(err, err_cap, "the scenario inside %s has paths that are too long: %s", owner, chain);
		goto done;
	}
	/* The row's folder is the arena archive's, as it is for the arena. */
	memcpy(outer, chain, (size_t)(cut - chain));
	outer[cut - chain] = '\0';
	pathDirnameAnySeparator(outer, component_dir, sizeof(component_dir));
	if (!component_dir[0]
			|| !registerComponentWithAnimationBatch(&ini, component_dir, mod_id,
				descriptor_ref, batch)) {
		nestedSetErr(err, err_cap, "could not register scenario %s of %s", scenario_id, owner);
		goto done;
	}
	row = assetCatalogGetMutable(scenario_id);
	if (!row || row->type != ASSET_SCENARIO) {
		nestedSetErr(err, err_cap, "scenario %s of %s did not get a catalog row", scenario_id, owner);
		goto done;
	}
	row->grid_offer_only = grid_offer_only;
	sysLogPrintf(LOG_NOTE,
		"PDARENA.SCENARIO: owner=%s id=%s stagenum=0x%02x source=%s",
		owner, scenario_id, row->ext.scenario.stagenum, chain);
	result = 1;

done:
	free(descriptor);
	if (bytes) sysMemFree(bytes);
	return result;
}

static s32 registerTypedPdDescriptorFile(const char *descriptor_path,
                                         asset_type_e expected,
                                         const char *mod_id,
										 s32 defer_reloads,
		loader_animation_source_batch_t *batch)
{
	pd_effect_source_info_t effect_source;
	asset_entry_t prior_effect;
	s32 had_prior_effect = 0;
	s32 published_effect_candidate = 0;
	s32 has_effect_source = 0;
	if (expected == ASSET_EFFECT) {
		char effect_error[256];
		effect_error[0] = '\0';
		if (!pdEffectSourceParseArchiveFile(descriptor_path, NULL, &effect_source,
				effect_error, sizeof(effect_error))) {
			sysLogPrintf(LOG_ERROR,
			"assetcatalog_scanner: invalid public .pdeffect source '%s': %s",
				descriptor_path, effect_error);
			return -1;
		}
		has_effect_source = 1;
	}
	ini_section_t ini;
	if (!iniParse(descriptor_path, &ini)) {
		const char *descriptor_leaf = typedPdArchiveDescriptorLeaf(descriptor_path);
		if (!descriptor_leaf) {
			return -1;
		}

		mod_archive_t *arc = modArchiveOpen(descriptor_path);
		if (!arc) {
			/* Legacy JSON .pd* files are still valid through their existing
			 * loaders; this scanner consumes readable INI descriptors and
			 * zip-openable typed asset archives. */
			return -1;
		}

		const char *found_descriptor = NULL;
		s32 idx = assetArchiveFindDescriptorEntry(arc, descriptor_path,
			ASSET_ARCHIVE_VALIDATE_MIGRATION, &found_descriptor);
		if (idx < 0) {
			modArchiveClose(arc);
			return -1;
		}

		u32 ini_size = 0;
		char *ini_bytes = (char *)modArchiveExtractAlloc(arc, idx, &ini_size);
		modArchiveClose(arc);
		if (!ini_bytes) {
			return -1;
		}

		s32 ok = iniParseBuffer(found_descriptor ? found_descriptor : descriptor_leaf,
			ini_bytes, ini_size, &ini);
		free(ini_bytes);
		if (!ok) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: invalid typed archive descriptor '%s' in '%s'",
				descriptor_leaf, descriptor_path);
			return -1;
		}

		if (!qualifyTypedArchiveSourcePaths(&ini, descriptor_path)) {
			sysLogPrintf(LOG_WARNING,
				"ASSET.PATH.REJECT: typed source chain exceeds repository capacity: %s",
				descriptor_path);
			return -1;
		}
	}

	asset_type_e ini_type = sectionToType(ini.type);
	if (expected != ASSET_NONE && ini_type != expected) {
		sysLogPrintf(LOG_WARNING,
			"assetcatalog_scanner: typed .pd* descriptor type mismatch in '%s': "
			"expected %d, got [%s]=%d",
			descriptor_path, expected, ini.type, ini_type);
		return -1;
	}

	char component_dir[FS_MAXPATH];
	if (!typedPdDescriptorComponentDir(descriptor_path, component_dir,
			sizeof(component_dir))) return -1;
	if (!isDirectory(component_dir)) {
		pathDirnameAnySeparator(descriptor_path, component_dir, sizeof(component_dir));
	}
	if (!component_dir[0]) {
		return -1;
	}
	if (has_effect_source) {
		const asset_entry_t *prior = assetCatalogResolve(effect_source.catalog_id);
		if (prior) {
			prior_effect = *prior;
			had_prior_effect = 1;
		}
	}

	{
		char descriptor_ref[FS_MAXPATH];
		if (!assetPathJoinChecked(descriptor_ref, sizeof(descriptor_ref),
				descriptor_path, "::",
				assetArchiveDescriptorForPath(descriptor_path))) {
			goto effect_publish_fail;
		}
		if (!registerComponentWithAnimationBatch(&ini, component_dir, mod_id, descriptor_ref, batch)) {
			goto effect_publish_fail;
		}
		if (has_effect_source) {
			published_effect_candidate = 1;
			asset_entry_t *effect = assetCatalogGetMutable(effect_source.catalog_id);
			if (!effect) {
				goto effect_publish_fail;
			}
			if (effect_source.format == PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY) {
				effect->ext.effect.effect_type =
					effect_source.profile_kind == PD_EFFECT_PROFILE_EXPLOSION
					? EFFECT_TYPE_EXPLOSION
					: effect_source.profile_kind == PD_EFFECT_PROFILE_SPARK
						? EFFECT_TYPE_SPARK : EFFECT_TYPE_SMOKE;
				effect->ext.effect.target = EFFECT_TARGET_CALLSITE;
			}
			char nested_err[256];
			nested_err[0] = '\0';
			if (assetCatalogRegisterEffectNestedDependencies(
					effect_source.catalog_id, descriptor_path, 0,
					nested_err, sizeof(nested_err)) < 0) {
				sysLogPrintf(LOG_WARNING,
					"PDEFFECT.NESTED.REJECT: owner=%s source=%s error=%s",
					effect_source.catalog_id, descriptor_path,
					nested_err[0] ? nested_err : "nested registration failed");
				goto effect_publish_fail;
			}
		}
		if (expected == ASSET_WEAPON) {
			const char *weapon_id = iniGet(&ini, "catalog_id",
				iniGet(&ini, "id", ""));
			char nested_err[256];
			nested_err[0] = '\0';
			if (!weapon_id[0]
					|| registerWeaponNestedDependenciesWithAnimationBatch(weapon_id,
						descriptor_path, 0, nested_err,
						sizeof(nested_err), batch, NULL) < 0) {
				asset_entry_t *weapon = weapon_id[0]
					? assetCatalogGetMutable(weapon_id) : NULL;
				if (weapon) {
					weapon->enabled = 0;
					weapon->load_state = ASSET_STATE_REGISTERED;
				}
				sysLogPrintf(LOG_WARNING,
					"PDWEAPON.NESTED.REJECT: owner=%s source=%s error=%s",
					weapon_id[0] ? weapon_id : "<missing>", descriptor_path,
					nested_err[0] ? nested_err : "nested registration failed");
				return -1;
			}
		}
		if (expected == ASSET_THEME) {
			const char *theme_id = iniGet(&ini, "catalog_id",
				iniGet(&ini, "id", ""));
			char nested_err[256];
			nested_err[0] = '\0';
			if (!theme_id[0]
					|| assetCatalogRegisterThemeNestedDependencies(theme_id,
						descriptor_path, 0, nested_err,
						sizeof(nested_err)) < 0) {
				asset_entry_t *theme = theme_id[0]
					? assetCatalogGetMutable(theme_id) : NULL;
				if (theme) {
					theme->enabled = 0;
					theme->load_state = ASSET_STATE_REGISTERED;
				}
				sysLogPrintf(LOG_WARNING,
					"PDTHEME.NESTED.REJECT: owner=%s source=%s error=%s",
					theme_id[0] ? theme_id : "<missing>", descriptor_path,
					nested_err[0] ? nested_err : "nested registration failed");
				return -1;
			}
		}
		if (expected == ASSET_VEHICLE) {
			const char *vehicle_id = iniGet(&ini, "catalog_id",
				iniGet(&ini, "id", ""));
			char model_err[256];
			model_err[0] = '\0';
			if (!vehicle_id[0]
					|| !registerVehicleModel(vehicle_id, model_err, sizeof(model_err))) {
				asset_entry_t *vehicle = vehicle_id[0]
					? assetCatalogGetMutable(vehicle_id) : NULL;
				if (vehicle) {
					vehicle->enabled = 0;
					vehicle->load_state = ASSET_STATE_REGISTERED;
				}
				sysLogPrintf(LOG_WARNING,
					"PDVEHICLE.MODEL.REJECT: owner=%s source=%s error=%s",
					vehicle_id[0] ? vehicle_id : "<missing>", descriptor_path,
					model_err[0] ? model_err : "vehicle model admission failed");
				return -1;
			}
		}
		if (ini_type == ASSET_ARENA) {
			const char *arena_id = iniGet(&ini, "catalog_id",
				iniGet(&ini, "id", ""));
			char scenario_err[256];
			scenario_err[0] = '\0';
			if (!arena_id[0]
					|| registerArenaEmbeddedScenario(arena_id, mod_id, batch,
						scenario_err, sizeof(scenario_err)) < 0) {
				asset_entry_t *arena = arena_id[0]
					? assetCatalogGetMutable(arena_id) : NULL;
				if (arena) {
					arena->enabled = 0;
					arena->load_state = ASSET_STATE_REGISTERED;
				}
				sysLogPrintf(LOG_WARNING,
					"PDARENA.SCENARIO.REJECT: owner=%s source=%s error=%s",
					arena_id[0] ? arena_id : "<missing>", descriptor_path,
					scenario_err[0] ? scenario_err : "scenario registration failed");
				return -1;
			}
		}
		return 1;
	}

effect_publish_fail:
	if (has_effect_source) {
		asset_entry_t *published = assetCatalogGetMutable(effect_source.catalog_id);
		if (had_prior_effect && published && published_effect_candidate) {
			catalogActivationLedgerRestoreRetiredSnapshot(published, &prior_effect);
		}
		else if (!had_prior_effect && published) assetCatalogUnregister(effect_source.catalog_id);
		if (had_prior_effect && !defer_reloads) {
			/* A rejected replacement restored the exact prior row. Reactivate
			 * only roots whose complete restored closure still passes. */
			(void)catalogReloadInvalidatedTypedAssets();
		}
	}
	return -1;
}

static s32 gridLeaseError(char *err, size_t cap, const char *reason)
{
	if (err && cap) snprintf(err, cap, "%s", reason);
	return 0;
}

static s32 gridLeaseCanonicalId(const char *id)
{
	if (!id || strlen(id) >= CATALOG_ID_LEN
			|| strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_.-") != strlen(id)) {
		return 0;
	}
	const char *colon = strchr(id, ':');
	return colon && colon != id && colon[1] && !strchr(colon + 1, ':')
		&& strncmp(id, "base:", 5) != 0;
}

static const char *gridLeaseDigestLeaf(const char *path)
{
	if (!path || !fsPathIsAbsolute(path) || strstr(path, "::") || assetPathHasParentTraversal(path)) {
		return NULL;
	}
	const char *leaf = path;
	for (const char *p = path; *p; p++) {
		if (*p == '/' || *p == '\\') leaf = p + 1;
	}
	const size_t hex_length = SHA256_HEX_SIZE - 1;
	return strspn(leaf, "0123456789abcdef") == hex_length
		&& strcmp(leaf + hex_length, ".pdarena") == 0 ? leaf : NULL;
}

static s32 gridLeaseFilePolicyMatches(const char *path, const char *leaf,
	asset_catalog_grid_arena_file_policy_e file_policy)
{
	char directory[FS_MAXPATH + 1], pinned[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	const s32 has_pin_path = fsPathIsAbsolute(directory)
		&& assetPathJoinChecked(pinned, sizeof(pinned), directory, "/", leaf);
	if (file_policy == GRID_ARENA_LEASE_TEMPORARY_FILE) {
		return !has_pin_path || !modsRootsSameDir(path, pinned);
	}
	return file_policy == GRID_ARENA_LEASE_PINNED_FILE && has_pin_path && strcmp(path, pinned) == 0;
}

static s32 gridLeaseParseDescriptor(const char *text, u32 len, asset_type_e type,
	const char *id, ini_section_t *out)
{
	if (!text || !iniParseText(text, len, out, NULL, 0)
			|| sectionToType(out->type) != type
			|| strcmp(iniGet(out, "catalog_id", ""), id) != 0
			|| !iniGetInt(out, "enabled", 1) || iniGetInt(out, "bundled", 0)) {
		return 0;
	}
	for (s32 i = 0; i < out->count; i++) {
		const ini_pair_t *pair = &out->pairs[i];
		if (assetPathKeyIsSource(pair->key)
				&& (assetPathIsAbsolute(pair->value) || assetPathHasParentTraversal(pair->value)
					|| strstr(pair->value, "::") || strchr(pair->value, '\\'))) {
			return 0;
		}
	}
	return 1;
}

static s32 gridLeaseReadDescriptor(const void *bytes, u32 size, const char *path,
	asset_type_e type, const char *id, ini_section_t *out)
{
	u32 len = 0;
	char *text = assetArchiveExtractDescriptorMemAlloc(bytes, size, path,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &len, NULL);
	const s32 ok = gridLeaseParseDescriptor(text, len, type, id, out);
	free(text);
	return ok;
}

static s32 gridSourceSetReadDescriptor(const void *bytes, u32 size, const char *path,
	asset_type_e type, const char *id, ini_section_t *out)
{
	const char *leaf = assetArchiveDescriptorForPath(path);
	u32 len = 0;
	char *text = leaf ? gridMatchPackageReadMember(bytes, size, leaf, 1024u * 1024u, &len, NULL) : NULL;
	const s32 ok = text && !memchr(text, '\0', len) && gridLeaseParseDescriptor(text, len, type, id, out);
	free(text);
	return ok;
}

s32 assetCatalogReadGridArenaSource(const char *text, u32 len, const char *arena_id,
	asset_catalog_grid_arena_source_t *out)
{
	if (!text || !len || memchr(text, '\0', len) || !out || !gridLeaseCanonicalId(arena_id)) return 0;
	ini_section_t *ini = malloc(sizeof(*ini));
	if (!ini) return 0;

	asset_catalog_grid_arena_source_t candidate = {0};
	s32 ok = gridLeaseParseDescriptor(text, len, ASSET_ARENA, arena_id, ini)
		&& catalogArenaLoadModeFromIni(ini) == ARENA_LOADMODE_PLAYABLE;
	if (ok) {
		const char *id = iniGet(ini, "scenario", "");
		const char *member = iniGet(ini, "scenario_archive", "");
		ok = gridLeaseCanonicalId(id) && strcmp(arena_id, id) != 0
			&& catalogIdSharesNamespace(arena_id, id)
			&& assetArchiveTypeForPath(member) == ASSET_SCENARIO
			&& assetPathCopyChecked(candidate.scenario_id, sizeof(candidate.scenario_id), id)
			&& assetPathCopyChecked(candidate.scenario_member, sizeof(candidate.scenario_member), member);
	}
	free(ini);
	if (ok) *out = candidate;
	return ok;
}

s32 assetCatalogBindGridScenarioDescriptor(const char *text, u32 len, const char *scenario_ref,
	const char *scenario_id, asset_entry_t *out)
{
	char descriptor[FS_MAXPATH];
	if (!text || !len || memchr(text, '\0', len) || !out || !gridLeaseCanonicalId(scenario_id)
			|| assetArchiveTypeForPath(scenario_ref) != ASSET_SCENARIO
			|| !strstr(scenario_ref, "::")
			|| !assetPathJoinChecked(descriptor, sizeof(descriptor), scenario_ref, "::", "scenario.ini")) {
		return 0;
	}
	ini_section_t *ini = malloc(sizeof(*ini));
	if (!ini) return 0;

	/* The legacy level_graph alias is not in the native source path inventory. */
	const s32 ok = gridLeaseParseDescriptor(text, len, ASSET_SCENARIO, scenario_id, ini)
		&& !iniGet(ini, "level_graph", "")[0]
		&& qualifyTypedArchiveSourcePaths(ini, scenario_ref);
	if (ok) {
		memset(out, 0, sizeof(*out));
		out->type = ASSET_SCENARIO;
		out->enabled = 1;
		out->runtime_index = -1;
		out->mp_index = -1;
		out->ext.scenario.stagenum = -1;
		assetPathCopyChecked(out->id, sizeof(out->id), scenario_id);
		assetPathCopyChecked(out->descriptor_path, sizeof(out->descriptor_path), descriptor);
		s_bindScenarioSourceFields(out, ini);
	}
	free(ini);
	return ok;
}

static s32 gridLeaseScenarioOwnsPath(const char *scenario_ref, const char *path)
{
	const size_t prefix_len = strlen(scenario_ref);
	return path && strncmp(path, scenario_ref, prefix_len) == 0
		&& strncmp(path + prefix_len, "::", 2) == 0;
}

static s32 gridLeaseInspect(const char *archive_path, const char *leaf, const char *arena_id,
	const char *scenario_id, const void *bytes, u32 size, asset_catalog_grid_arena_source_t *out,
	char *err, size_t err_cap)
{
	const char *invalid_archive = "the private archive is not the offered original arena and scenario";
	if (!bytes || !size || size > NET_DISTRIB_MAX_COMP) return gridLeaseError(err, err_cap, invalid_archive);

	u8 digest[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, hex);
	if (memcmp(hex, leaf, SHA256_HEX_SIZE - 1) != 0
			|| assetArchiveValidateBytes(bytes, size, archive_path, ASSET_ARCHIVE_VALIDATE_RELEASE, NULL, 0) != 0
			|| gridArchiveOrigin(archive_path, bytes, size) != GRID_ARCHIVE_ORIGINAL) {
		return gridLeaseError(err, err_cap, invalid_archive);
	}

	const u32 descriptor_limit = 1024u * 1024u;
	u32 length = 0;
	char *descriptor = gridMatchPackageReadMember(bytes, size, "arena.ini", descriptor_limit, &length, NULL);
	asset_catalog_grid_arena_source_t candidate;
	const s32 arena_valid = descriptor && assetCatalogReadGridArenaSource(descriptor, length, arena_id, &candidate)
		&& strcmp(candidate.scenario_id, scenario_id) == 0;
	free(descriptor);
	char scenario_ref[FS_MAXPATH], descriptor_ref[FS_MAXPATH];
	if (!arena_valid || !assetPathJoinChecked(descriptor_ref, sizeof(descriptor_ref), archive_path, "::", "arena.ini")
			|| !assetPathJoinChecked(scenario_ref, sizeof(scenario_ref), archive_path, "::", candidate.scenario_member)
			|| !assetPathJoinChecked(descriptor_ref, sizeof(descriptor_ref), scenario_ref, "::", "scenario.ini")) {
		return gridLeaseError(err, err_cap, invalid_archive);
	}
	u32 scenario_size = 0;
	void *scenario = gridMatchPackageReadMember(bytes, size, candidate.scenario_member,
		NET_DISTRIB_MAX_COMP, &scenario_size, NULL);
	ini_section_t *ini = scenario ? malloc(sizeof(*ini)) : NULL;
	s32 ok = ini && gridLeaseReadDescriptor(scenario, scenario_size, candidate.scenario_member,
		ASSET_SCENARIO, scenario_id, ini) && qualifyTypedArchiveSourcePaths(ini, scenario_ref);
	if (ok) {
		const char *primary = s_scenarioPrimarySource(ini, NULL, NULL);
		const char *paths[] = {scenario_ref, primary};
		if (!gridLeaseScenarioOwnsPath(scenario_ref, primary)) {
			ok = gridLeaseError(err, err_cap, "the offered scenario has no primary source inside its archive");
		} else if (!fileProviderCanInternPaths(paths, sizeof(paths) / sizeof(paths[0]))) {
			ok = gridLeaseError(err, err_cap, "the FileProvider has no room for both Grid arena source paths");
		}
	} else {
		gridLeaseError(err, err_cap, invalid_archive);
	}
	free(ini);
	free(scenario);
	if (ok) *out = candidate;
	return ok;
}

static void gridLeaseMarkDependency(const char *id, asset_type_e type, void *userdata)
{
	(void)id;
	(void)type;
	*(s32 *)userdata = 1;
}

static s32 gridLeaseDropDependency(const char *id, asset_type_e type, void *userdata)
{
	(void)id;
	(void)type;
	(void)userdata;
	return 0;
}

s32 assetCatalogPreviewGridArenaLeaseStage(const char *scenario_id)
{
	if (!g_Stages || !gridLeaseCanonicalId(scenario_id)
			|| assetCatalogFindStagenumPrivateSlot(scenario_id) >= 0) return -1;
	const s32 stagenum = assetCatalogPeekStagenumPrivateSlot(scenario_id);
	if (stagenum < 0) return -1;

	s32 has_template = 0;
	for (s32 i = 0; i < g_NumStages; i++) {
		const struct stagetableentry *row = stageGetEntry(i);
		if (!row) continue;
		/* The scanner would reuse this occupied row instead of minting a new one. */
		if (row->id == stagenum) return -1;
		if (row->id == STAGE_MP_SKEDAR) has_template = 1;
	}
	return has_template ? stagenum : -1;
}

s32 assetCatalogPreflightGridArenaLease(const char *archive_path, const char *arena_id,
	const char *scenario_id, asset_catalog_grid_arena_file_policy_e file_policy, const void *bytes, u32 size,
	asset_catalog_grid_arena_source_t *source, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const char *leaf = gridLeaseDigestLeaf(archive_path);
	if (!source || !leaf || !gridLeaseCanonicalId(arena_id) || !gridLeaseCanonicalId(scenario_id)
			|| strcmp(arena_id, scenario_id) == 0 || !catalogIdSharesNamespace(arena_id, scenario_id)) {
		return gridLeaseError(err, err_cap, "invalid Grid arena lease identity or private archive path");
	}
	if (!gridLeaseFilePolicyMatches(archive_path, leaf, file_policy)) {
		return gridLeaseError(err, err_cap, "the private archive path violates the Grid file policy");
	}
	s32 prior_deps = 0;
	catalogDepForEachTyped(arena_id, gridLeaseMarkDependency, &prior_deps);
	catalogDepForEachTyped(scenario_id, gridLeaseMarkDependency, &prior_deps);
	if (assetCatalogResolveAny(arena_id) || assetCatalogResolveAny(scenario_id)
			|| assetCatalogFindStagenumPrivateSlot(scenario_id) >= 0 || prior_deps) {
		return gridLeaseError(err, err_cap, "the offered arena or scenario conflicts with local catalog ownership");
	}
	if (assetCatalogPreviewGridArenaLeaseStage(scenario_id) < 0) {
		return gridLeaseError(err, err_cap, "no private native stage is available for the offered Grid arena");
	}
	return gridLeaseInspect(archive_path, leaf, arena_id, scenario_id, bytes, size, source, err, err_cap);
}

static s32 gridLeaseOwnsRows(const asset_catalog_arena_lease_t *lease,
	const asset_entry_t *arena, const asset_entry_t *scenario)
{
	char chain[FS_MAXPATH], descriptor[FS_MAXPATH];
	if (!arena || !scenario || arena->type != ASSET_ARENA || scenario->type != ASSET_SCENARIO
			|| !arena->enabled || !scenario->enabled || arena->bundled || scenario->bundled
			|| !arena->grid_offer_only || !scenario->grid_offer_only
			|| arena->ext.arena.load_mode != ARENA_LOADMODE_PLAYABLE
			|| arena->ext.arena.stagenum != lease->stagenum || scenario->ext.scenario.stagenum != lease->stagenum
			|| arena->runtime_index != lease->stage_index || scenario->runtime_index != lease->stage_index
			|| strcmp(arena->ext.arena.scenario_id, lease->scenario_id) != 0
			|| assetCatalogFindStagenumPrivateSlot(lease->scenario_id) != lease->stagenum
			|| !stageTableOwnsCustom(lease->stage_index, lease->stagenum)
			|| !assetPathJoinChecked(chain, sizeof(chain), lease->archive_path, "::", lease->scenario_member)
			|| strcmp(arena->ext.arena.scenario_archive, chain) != 0) {
		return 0;
	}
	const struct stagetableentry *stage = stageGetEntry(lease->stage_index);
	const char *arena_source = fileProviderPath(catalogEffectiveHandle(arena));
	const char *scene_source = fileProviderPath(catalogEffectiveHandle(scenario));
	return stage && !stage->bgfileid && !stage->tilefileid && !stage->padsfileid
		&& !stage->setupfileid && !stage->mpsetupfileid
		&& arena_source && strcmp(arena_source, chain) == 0
		&& gridLeaseScenarioOwnsPath(chain, scene_source)
		&& assetPathJoinChecked(descriptor, sizeof(descriptor), chain, "::", "scenario.ini")
		&& strcmp(scenario->descriptor_path, descriptor) == 0
		&& assetPathJoinChecked(descriptor, sizeof(descriptor), lease->archive_path, "::", "arena.ini")
		&& strcmp(arena->descriptor_path, descriptor) == 0;
}

s32 assetCatalogAdmitGridArenaLease(const char *archive_path, const char *arena_id,
	const char *scenario_id, asset_catalog_grid_arena_file_policy_e file_policy,
	asset_catalog_arena_lease_t *lease, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const char *leaf = gridLeaseDigestLeaf(archive_path);
	if (!lease || lease->arena_id[0] || !leaf || !gridLeaseFilePolicyMatches(archive_path, leaf, file_policy)) {
		return gridLeaseError(err, err_cap, "invalid Grid arena lease or file policy");
	}
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(archive_path, &size);
	asset_catalog_grid_arena_source_t source;
	const s32 verified = assetCatalogPreflightGridArenaLease(archive_path, arena_id, scenario_id,
		file_policy, bytes, size, &source, err, err_cap);
	free(bytes);
	if (!verified) return 0;

	asset_catalog_arena_lease_t candidate = { .file_policy = file_policy };
	if (!assetPathCopyChecked(candidate.archive_path, sizeof(candidate.archive_path), archive_path)
			|| !assetPathCopyChecked(candidate.arena_id, sizeof(candidate.arena_id), arena_id)
			|| !assetPathCopyChecked(candidate.scenario_id, sizeof(candidate.scenario_id), scenario_id)
			|| !assetPathCopyChecked(candidate.scenario_member, sizeof(candidate.scenario_member), source.scenario_member)) {
		return gridLeaseError(err, err_cap, "the private archive binding is too long");
	}

	_Static_assert(STAGENUM_CUSTOM_COUNT <= sizeof(u32) * CHAR_BIT, "custom stages fit occupancy mask");
	u32 occupied_stages = 0;
	for (s32 i = 0; i < g_NumStages; i++) {
		const struct stagetableentry *row = stageGetEntry(i);
		if (row && row->id >= STAGENUM_CUSTOM_START && row->id < STAGENUM_CUSTOM_END) {
			occupied_stages |= 1u << (row->id - STAGENUM_CUSTOM_START);
		}
	}
	char namespace_id[CATALOG_ID_LEN];
	size_t namespace_len = (size_t)(strchr(arena_id, ':') - arena_id);
	memcpy(namespace_id, arena_id, namespace_len);
	namespace_id[namespace_len] = '\0';

	loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
	external_scan_transaction_t transaction;
	if (!batch || !externalScanTransactionBegin(&transaction)) {
		loaderAnimationSourceBatchDestroy(batch);
		return gridLeaseError(err, err_cap, "could not prepare the Grid arena catalog transaction");
	}
	candidate.provider_before = transaction.provider;
	s32 ok = registerTypedPdDescriptorFile(archive_path, ASSET_ARENA, namespace_id, 1, batch) == 1
		&& loaderAnimationSourceBatchCommit(batch);
	loaderAnimationSourceBatchDestroy(batch);

	asset_entry_t *arena = assetCatalogGetMutable(arena_id);
	asset_entry_t *scenario = assetCatalogGetMutable(scenario_id);
	candidate.stagenum = arena ? arena->ext.arena.stagenum : -1;
	candidate.stage_index = arena ? arena->runtime_index : -1;
	ok = ok && assetCatalogGetPoolSize() == transaction.row_count + 2
		&& gridLeaseOwnsRows(&candidate, arena, scenario)
		&& !(occupied_stages & (1u << (candidate.stagenum - STAGENUM_CUSTOM_START)));
	for (s32 i = 0; ok && i < transaction.row_count; i++) {
		const asset_entry_t *row = assetCatalogGetByIndex(i);
		const catalog_entry_snapshot_t *prior = &transaction.rows[i];
		ok = row ? catalogEntrySnapshotMatches(prior, row) : !prior->row.occupied;
	}
	u8 digest[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	if (ok && modArchiveSha256(archive_path, digest) == MODARCHIVE_OK) {
		sha256ToHex(digest, hex);
		ok = memcmp(hex, leaf, SHA256_HEX_SIZE - 1) == 0
			&& fileProviderCheckpointCreate(&candidate.provider_after);
	} else {
		ok = 0;
	}
	if (!ok) {
		s32 restored = externalScanTransactionRollback(&transaction);
		externalScanTransactionDestroy(&transaction);
		assetCatalogTrimUnoccupiedTail();
		return gridLeaseError(err, err_cap, restored ? "the typed scanner did not admit the exact Grid arena pair"
			: "the Grid arena catalog rollback failed");
	}
	arena->temporary = 1;
	scenario->temporary = 1;
	externalScanTransactionDestroy(&transaction);
	*lease = candidate;
	return 1;
}

s32 assetCatalogRetireGridArenaLease(asset_catalog_arena_lease_t *lease,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!lease) return gridLeaseError(err, err_cap, "invalid Grid arena lease");
	if (!lease->arena_id[0]) return 1;
	const asset_entry_t *arena = assetCatalogResolveAny(lease->arena_id);
	const asset_entry_t *scenario = assetCatalogResolveAny(lease->scenario_id);
	if (!gridLeaseOwnsRows(lease, arena, scenario)
			|| !assetCatalogRetireUnactivatedArenaPair(lease->arena_id, lease->scenario_id)) {
		return gridLeaseError(err, err_cap, "the Grid arena lease changed or still has active catalog references");
	}
	catalogDepPruneOwner(lease->arena_id, gridLeaseDropDependency, NULL);
	catalogDepPruneOwner(lease->scenario_id, gridLeaseDropDependency, NULL);
	/* Preconditions above establish both owners on the main thread. */
	stageTableRetireCustom(lease->stage_index, lease->stagenum);
	assetCatalogReleaseStagenumPrivateSlot(lease->scenario_id, lease->stagenum);

	/* The intern pool is append-only. Rewind only when no other readers
	 * registered paths after this lease was admitted. */
	file_provider_checkpoint_t current;
	if (fileProviderCheckpointCreate(&current)
			&& current.pool_used == lease->provider_after.pool_used
			&& current.path_count == lease->provider_after.path_count
			&& current.warned == lease->provider_after.warned) {
		(void)fileProviderCheckpointRestore(&lease->provider_before);
	}
	memset(lease, 0, sizeof(*lease));
	return 1;
}

#define GRID_WEAPON_LEASE_MAX_ROWS 32u

struct asset_catalog_grid_weapon_lease {
	char path[FS_MAXPATH + 1];
	grid_match_source_root_t rows[GRID_WEAPON_LEASE_MAX_ROWS];
	char descriptors[GRID_WEAPON_LEASE_MAX_ROWS][FS_MAXPATH];
	s32 slots[GRID_WEAPON_LEASE_MAX_ROWS];
	size_t count;
	file_provider_checkpoint_t provider_before;
	file_provider_checkpoint_t provider_after;
};

typedef struct grid_weapon_admission {
	const grid_match_sources_t *plan;
	size_t package_index;
	asset_catalog_grid_weapon_lease_t *lease;
	ini_section_t *ini;
	u64 remaining;
	u8 *seen_members;
	s32 strict_inventory;
	s32 allow_scenario;
} grid_weapon_admission_t;

typedef struct grid_weapon_member_scan {
	grid_weapon_admission_t *admission;
	const void *bytes;
	u32 size;
	const char *chain;
	s32 depth;
	s32 valid;
} grid_weapon_member_scan_t;

static s32 gridWeaponLeaseOwns(const char *id, void *userdata)
{
	const asset_catalog_grid_weapon_lease_t *lease = userdata;
	for (size_t i = 0; i < lease->count; i++) {
		if (strcmp(lease->rows[i].id, id) == 0) return 1;
	}
	return 0;
}

static s32 gridWeaponChildType(asset_type_e type, const char *name)
{
	switch (type) {
	case ASSET_MODEL: return pathEndsWithNoCase(name, ".pdmesh");
	case ASSET_PROJECTILE: return pathEndsWithNoCase(name, ".pdprojectile");
	case ASSET_ENTITY: return pathEndsWithNoCase(name, ".pdentity");
	case ASSET_EFFECT: return pathEndsWithNoCase(name, ".pdeffect");
	case ASSET_AUDIO: return pathEndsWithNoCase(name, ".pdsfx");
	case ASSET_UI: return pathEndsWithNoCase(name, ".pdui");
	case ASSET_MATERIAL: return pathEndsWithNoCase(name, ".pdmaterial");
	default: return 0;
	}
}

static s32 gridSourceSetChildType(asset_type_e type, asset_type_e outer_type, const char *chain)
{
	if (gridWeaponChildType(type, chain)) return 1;
	if (type != ASSET_TEXTURE) return 0;

	asset_type_e owner, child;
	const char *member;
	return gridMatchPackageMemberOwner(outer_type, chain, &owner, &member)
		&& owner == ASSET_EFFECT && effectNestedMemberType(member, &child) && child == ASSET_TEXTURE;
}

static s32 gridSourceSetValidateTexture(const void *bytes, u32 size, const char *id,
	const ini_section_t *ini, char *err, size_t err_cap);
static s32 gridWeaponInspectMembers(grid_weapon_admission_t *admission, const void *bytes,
	u32 size, const char *chain, s32 depth);

static s32 gridWeaponInspectMember(const char *name, u32 size, void *userdata)
{
	grid_weapon_member_scan_t *scan = userdata;
	grid_weapon_admission_t *admission = scan->admission;
	if (size > admission->remaining) {
		scan->valid = 0;
		return 1;
	}
	admission->remaining -= size;
	if (!assetArchivePathIsTyped(name)) return 0;

	char chain[FS_MAXPATH];
	if (scan->depth >= MOD_ASSET_VALIDATE_MAX_DEPTH
			|| !(scan->chain[0]
				? assetPathJoinChecked(chain, sizeof(chain), scan->chain, "::", name)
				: assetPathCopyChecked(chain, sizeof(chain), name))) {
		scan->valid = 0;
		return 1;
	}
	const grid_match_sources_t *plan = admission->plan;
	const grid_match_source_entry_t *entry = NULL;
	for (size_t i = 0; i < plan->entry_count; i++) {
		if (plan->entries[i].package_index == admission->package_index
				&& strcmp(plan->entries[i].member_chain, chain) == 0) {
			entry = &plan->entries[i];
			break;
		}
	}
	const s32 native_child = entry && (admission->strict_inventory
		? gridSourceSetChildType(entry->asset.type, plan->packages[admission->package_index].owner.type, chain)
		: gridWeaponChildType(entry->asset.type, name));
	const s32 arena_scenario = entry && admission->allow_scenario
		&& entry->asset.type == ASSET_SCENARIO && assetArchiveTypeForPath(name) == ASSET_SCENARIO;
	if (!native_child && !arena_scenario) {
		scan->valid = 0;
		return 1;
	}
	if (admission->seen_members) {
		u8 *seen = &admission->seen_members[entry - plan->entries];
		if (*seen) {
			scan->valid = 0;
			return 1;
		}
		*seen = 1;
	}
	u32 length = 0;
	void *nested = modArchiveExtractMemAlloc(scan->bytes, scan->size, name, &length);
	if (admission->strict_inventory) {
		scan->valid = nested && length == size
			&& gridWeaponInspectMembers(admission, nested, length, chain, scan->depth + 1)
			&& assetArchiveValidateBytes(nested, length, name, ASSET_ARCHIVE_VALIDATE_RELEASE, NULL, 0) == 0
			&& gridSourceSetReadDescriptor(nested, length, name, entry->asset.type, entry->asset.id, admission->ini)
			&& (entry->asset.type != ASSET_TEXTURE
				|| gridSourceSetValidateTexture(nested, length, entry->asset.id, admission->ini, NULL, 0));
	} else {
		scan->valid = nested && length == size
			&& gridLeaseReadDescriptor(nested, length, name, entry->asset.type, entry->asset.id, admission->ini)
			&& gridWeaponInspectMembers(admission, nested, length, chain, scan->depth + 1);
	}
	free(nested);
	return !scan->valid;
}

static s32 gridWeaponInspectMembers(grid_weapon_admission_t *admission, const void *bytes,
	u32 size, const char *chain, s32 depth)
{
	grid_weapon_member_scan_t scan = {
		.admission = admission, .bytes = bytes, .size = size, .chain = chain,
		.depth = depth, .valid = 1,
	};
	return modArchiveMemForEachEntry(bytes, size, gridWeaponInspectMember, &scan) == MODARCHIVE_OK
		&& scan.valid;
}

static s32 gridWeaponAcceptEdge(const char *owner, const char *dependency, asset_type_e type,
	s32 bundled, void *userdata)
{
	grid_weapon_admission_t *admission = userdata;
	if (bundled || !gridWeaponLeaseOwns(owner, admission->lease)) return 0;
	const asset_entry_t *row = assetCatalogResolve(dependency);
	if (!row || !row->enabled || row->type != type) return 0;
	const grid_match_sources_t *plan = admission->plan;
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (strcmp(entry->asset.id, dependency) == 0) {
			return entry->asset.type == type
				&& plan->packages[entry->package_index].bundled == !!row->bundled;
		}
	}
	return 0;
}

static s32 gridSourceIsBodyHead(asset_type_e type)
{
	return type == ASSET_BODY || type == ASSET_HEAD;
}

static s32 gridWeaponFindSlot(const grid_match_source_root_t *row)
{
	switch (row->type) {
	case ASSET_WEAPON: return assetCatalogFindWeaponPrivateSlot(row->id);
	case ASSET_MODEL: return assetCatalogFindModelPrivateSlot(row->id);
	case ASSET_AUDIO: return assetCatalogFindSoundPrivateSlot(row->id);
	case ASSET_TEXTURE: return assetCatalogFindTexturePrivateSlot(row->id);
	case ASSET_BODY:
	case ASSET_HEAD: return assetCatalogFindBodyHeadPrivateSlot(row->type == ASSET_HEAD, row->id);
	default: return -1;
	}
}

static s32 gridWeaponOwnsRow(const asset_catalog_grid_weapon_lease_t *lease, size_t index,
	const asset_entry_t *row)
{
	if (!row || !row->enabled || row->bundled || row->type != lease->rows[index].type
			|| strcmp(row->descriptor_path, lease->descriptors[index]) != 0) return 0;
	const asset_data_handle_t source = catalogEffectiveHandle(row);
	if (assetHandleIsNull(source)) return 1;
	const char *path = fileProviderPath(source);
	const size_t length = strlen(lease->path);
	return path && strncmp(path, lease->path, length) == 0
		&& path[length] == ':' && path[length + 1] == ':';
}

static s32 gridWeaponRecordRows(asset_catalog_grid_weapon_lease_t *lease,
	const grid_match_sources_t *plan, size_t package_index)
{
	const grid_match_source_package_t *package = &plan->packages[package_index];
	s32 has_owner = 0;
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (entry->package_index != package_index) continue;
		const s32 is_owner = !entry->member_chain[0];
		const size_t index = lease->count;
		const char *name = is_owner ? lease->path : entry->member_chain;
		const char *descriptor = assetArchiveDescriptorForPath(name);
		if (index == GRID_WEAPON_LEASE_MAX_ROWS || !descriptor
				|| !catalogIdSharesNamespace(package->owner.id, entry->asset.id)
				|| (!is_owner && !gridWeaponChildType(entry->asset.type, name))
				|| assetCatalogResolveAny(entry->asset.id) || gridWeaponFindSlot(&entry->asset) >= 0) return 0;
		s32 prior_deps = 0;
		catalogDepForEachTyped(entry->asset.id, gridLeaseMarkDependency, &prior_deps);
		if (prior_deps) return 0;

		char root[FS_MAXPATH];
		if (!(is_owner ? assetPathCopyChecked(root, sizeof(root), lease->path)
				: assetPathJoinChecked(root, sizeof(root), lease->path, "::", name))
				|| !assetPathJoinChecked(lease->descriptors[index], sizeof(lease->descriptors[index]),
					root, "::", descriptor)) return 0;
		lease->rows[index] = entry->asset;
		lease->slots[index] = -1;
		lease->count++;
		has_owner |= is_owner;
	}
	return has_owner && catalogDepCanRetireOwnerSet(gridWeaponLeaseOwns, lease);
}

static s32 gridWeaponValidateRows(grid_weapon_admission_t *admission,
	const external_scan_transaction_t *transaction)
{
	asset_catalog_grid_weapon_lease_t *lease = admission->lease;
	if ((size_t)assetCatalogGetPoolSize() != (size_t)transaction->row_count + lease->count) return 0;
	for (s32 i = 0; i < transaction->row_count; i++) {
		const asset_entry_t *row = assetCatalogGetByIndex(i);
		const catalog_entry_snapshot_t *prior = &transaction->rows[i];
		if (row ? !catalogEntrySnapshotMatches(prior, row) : prior->row.occupied) return 0;
	}
	for (size_t i = 0; i < lease->count; i++) {
		const asset_entry_t *row = assetCatalogResolveAny(lease->rows[i].id);
		if (!gridWeaponOwnsRow(lease, i, row)) return 0;
		const s32 slot = gridWeaponFindSlot(&lease->rows[i]);
		if ((row->type == ASSET_WEAPON && (slot < 0 || row->runtime_index != slot))
				|| (row->type == ASSET_MODEL && (row->runtime_index != slot
					|| (slot < 0 ? row->source_filenum > 0
						: row->source_filenum != assetCatalogModelPrivateSourceFilenum(slot))))
				|| (row->type == ASSET_AUDIO && (slot < 0 || row->ext.audio.category != AUDIO_CAT_SFX
					|| row->source_soundnum != slot || row->ext.audio.sound_id != slot))) return 0;
		lease->slots[i] = slot;
	}
	const grid_match_source_root_t *owner = &admission->plan->packages[admission->package_index].owner;
	return catalogDepValidateAppended(&transaction->deps, gridWeaponAcceptEdge, admission)
		&& catalogCanDeactivateTypedAsset(owner->type, owner->id);
}

s32 assetCatalogAdmitGridWeaponLease(const char *archive_path, const grid_match_sources_t *plan,
	size_t package_index, asset_catalog_grid_weapon_lease_t **out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!archive_path || !plan || !out || *out || package_index >= plan->package_count) {
		return gridLeaseError(err, err_cap, "invalid Grid weapon lease request");
	}
	grid_match_sources_t sealed = *plan;
	if (!gridMatchSourcesSeal(&sealed, err, err_cap) || sealed.total_bytes != plan->total_bytes
			|| memcmp(sealed.digest, plan->digest, sizeof(sealed.digest)) != 0) {
		return gridLeaseError(err, err_cap, "Grid weapon lease needs an exact sealed source plan");
	}
	const grid_match_source_package_t *package = &plan->packages[package_index];
	if (package->owner.type != ASSET_WEAPON || strcmp(package->suffix, ".pdweapon") != 0
			|| package->bundled || !gridLeaseCanonicalId(package->owner.id)) {
		return gridLeaseError(err, err_cap, "Grid weapon lease needs one nonbundled native weapon");
	}

	char folder[FS_MAXPATH + 1], expected[FS_MAXPATH + 1], digest_hex[SHA256_HEX_SIZE];
	fsFullPath("$S/grid-session", folder, sizeof(folder));
	sha256ToHex(package->digest, digest_hex);
	const int written = snprintf(expected, sizeof(expected), "%s/%s.pdweapon", folder, digest_hex);
	if (!fsPathIsAbsolute(folder) || written <= 0 || written >= (int)sizeof(expected)
			|| strcmp(expected, archive_path) != 0 || fsFileSize(archive_path) != (s32)package->size) {
		return gridLeaseError(err, err_cap, "Grid weapon archive is outside its private digest path or wrong-sized");
	}

	asset_catalog_grid_weapon_lease_t *lease = calloc(1, sizeof(*lease));
	ini_section_t *ini = malloc(sizeof(*ini));
	const u32 size = package->size;
	void *bytes = malloc(size);
	u8 digest[SHA256_DIGEST_SIZE];
	grid_weapon_admission_t admission = {
		.plan = plan, .package_index = package_index, .lease = lease, .ini = ini,
		.remaining = NET_DISTRIB_MAX_SESSION,
	};
	s32 ok = lease && ini && bytes && fsFileLoadTo(archive_path, bytes, size) == (s32)size;
	if (ok) {
		sha256Hash(bytes, size, digest);
		memcpy(lease->path, expected, (size_t)written + 1);
		ok = memcmp(digest, package->digest, sizeof(digest)) == 0
			&& gridWeaponRecordRows(lease, plan, package_index)
			&& gridWeaponInspectMembers(&admission, bytes, size, "", 0)
			&& assetArchiveValidateBytes(bytes, size, archive_path,
				ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) == 0
			&& gridArchiveOrigin(archive_path, bytes, size) == GRID_ARCHIVE_ORIGINAL
			&& gridLeaseReadDescriptor(bytes, size, archive_path, ASSET_WEAPON, package->owner.id, ini);
	}
	free(ini);
	free(bytes);
	if (!ok) {
		free(lease);
		return gridLeaseError(err, err_cap, "invalid original Grid weapon, unsupported child, or local identity conflict");
	}

	char namespace_id[CATALOG_ID_LEN];
	const size_t length = (size_t)(strchr(package->owner.id, ':') - package->owner.id);
	memcpy(namespace_id, package->owner.id, length);
	namespace_id[length] = '\0';
	loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
	external_scan_transaction_t transaction;
	if (!batch || !externalScanTransactionBegin(&transaction)) {
		loaderAnimationSourceBatchDestroy(batch);
		free(lease);
		return gridLeaseError(err, err_cap, "could not prepare the Grid weapon catalog transaction");
	}
	lease->provider_before = transaction.provider;
	ok = registerTypedPdDescriptorFile(archive_path, ASSET_WEAPON, namespace_id, 1, batch) == 1
		&& gridWeaponValidateRows(&admission, &transaction)
		&& modArchiveSha256(archive_path, digest) == MODARCHIVE_OK
		&& memcmp(digest, package->digest, sizeof(digest)) == 0
		&& fileProviderCheckpointCreate(&lease->provider_after)
		&& loaderAnimationSourceBatchCommit(batch);
	loaderAnimationSourceBatchDestroy(batch);
	if (!ok) {
		const s32 restored = externalScanTransactionRollback(&transaction);
		externalScanTransactionDestroy(&transaction);
		assetCatalogTrimUnoccupiedTail();
		free(lease);
		return gridLeaseError(err, err_cap, restored ? "typed scanner did not admit the exact Grid weapon closure"
			: "Grid weapon catalog rollback failed");
	}
	for (size_t i = 0; i < lease->count; i++) {
		asset_entry_t *row = assetCatalogGetMutable(lease->rows[i].id);
		row->temporary = 1;
		row->grid_offer_only = 1;
	}
	externalScanTransactionDestroy(&transaction);
	catalogRefreshSoundOverrides();
	*out = lease;
	return 1;
}

const char *assetCatalogGridWeaponLeasePath(const asset_catalog_grid_weapon_lease_t *lease)
{
	return lease ? lease->path : NULL;
}

static s32 gridWeaponRetireSlot(const grid_match_source_root_t *row, s32 slot, s32 commit)
{
	switch (row->type) {
	case ASSET_WEAPON:
		return commit ? assetCatalogRetireWeaponReservation(row->id, slot)
			: assetCatalogCanRetireWeaponReservation(row->id, slot);
	case ASSET_MODEL:
		if (slot < 0) return 1;
		return commit ? assetCatalogRetireModelReservation(row->id, slot)
			: assetCatalogCanRetireModelReservation(row->id, slot);
	case ASSET_AUDIO:
		return commit ? assetCatalogRetireSoundReservation(row->id, slot)
			: assetCatalogCanRetireSoundReservation(row->id, slot);
	case ASSET_TEXTURE:
		if (!commit) return assetCatalogCanRetireTextureReservation(row->id, slot)
			&& !textureSourceRuntimeHasStageRead(slot)
			&& !catalogTextureGenerationHasLiveIdentity(row->id);
		textureSourceRuntimeInvalidateSlot(slot);
		return assetCatalogRetireTextureReservation(row->id, slot);
	default: return 1;
	}
}

s32 assetCatalogReleaseGridWeaponLeaseStageRefs(const asset_catalog_grid_weapon_lease_t *lease,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!lease) return 1;
	if (!lease->count || lease->count > GRID_WEAPON_LEASE_MAX_ROWS) {
		return gridLeaseError(err, err_cap, "invalid Grid weapon lease for stage release");
	}
	for (size_t i = 0; i < lease->count; i++) {
		const asset_entry_t *row = assetCatalogResolveAny(lease->rows[i].id);
		if (!gridWeaponOwnsRow(lease, i, row) || !row->temporary || !row->grid_offer_only) {
			return gridLeaseError(err, err_cap, "the Grid weapon lease changed before stage release");
		}
		if (row->stage_ref_count && (row->stage_ref_count != 1 || row->ref_count <= 0
				|| row->ref_count == ASSET_REF_BUNDLED
				|| !catalogCanDeactivateTypedAsset(row->type, row->id))) {
			return gridLeaseError(err, err_cap, "the Grid weapon stage reference could not be verified");
		}
	}
	for (size_t i = 0; i < lease->count; i++) {
		const asset_entry_t *row = assetCatalogResolveAny(lease->rows[i].id);
		if (row->stage_ref_count) catalogReleaseStageAsset(row->type, row->id);
	}
	return 1;
}

s32 assetCatalogRetireGridWeaponLease(asset_catalog_grid_weapon_lease_t **owned,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!owned) return gridLeaseError(err, err_cap, "invalid Grid weapon retirement request");
	asset_catalog_grid_weapon_lease_t *lease = *owned;
	if (!lease) return 1;
	if (!catalogDepCanRetireOwnerSet(gridWeaponLeaseOwns, lease)) {
		return gridLeaseError(err, err_cap, "another catalog source depends on the temporary Grid weapon");
	}
	for (size_t i = 0; i < lease->count; i++) {
		const asset_entry_t *row = assetCatalogResolveAny(lease->rows[i].id);
		if (!gridWeaponOwnsRow(lease, i, row) || !row->temporary || !row->grid_offer_only
				|| row->ref_count || row->stage_ref_count || row->loaded_data
				|| !gridWeaponRetireSlot(&lease->rows[i], lease->slots[i], 0)) {
			return gridLeaseError(err, err_cap, "the Grid weapon lease changed or still has active references");
		}
	}
	for (size_t i = 0; i < lease->count; i++) {
		if (!catalogDeactivateTypedAsset(lease->rows[i].type, lease->rows[i].id)) {
			return gridLeaseError(err, err_cap, "the Grid weapon lease runtime could not be detached");
		}
	}
	if (!assetCatalogRetireUnactivatedGridWeaponRows(lease->rows, lease->count)) {
		return gridLeaseError(err, err_cap, "the Grid weapon lease catalog rows could not be retired");
	}
	/* The exact reservations were preflighted on this same main thread. */
	for (size_t i = 0; i < lease->count; i++) {
		(void)gridWeaponRetireSlot(&lease->rows[i], lease->slots[i], 1);
		catalogDepPruneOwner(lease->rows[i].id, gridLeaseDropDependency, NULL);
	}
	catalogRefreshSoundOverrides();
	file_provider_checkpoint_t current;
	if (fileProviderCheckpointCreate(&current)
			&& current.pool_used == lease->provider_after.pool_used
			&& current.path_count == lease->provider_after.path_count
			&& current.warned == lease->provider_after.warned) {
		(void)fileProviderCheckpointRestore(&lease->provider_before);
	}
	free(lease);
	*owned = NULL;
	return 1;
}

typedef struct grid_source_set_package {
	u16 index;
	char path[FS_MAXPATH + 1];
} grid_source_set_package_t;

typedef struct grid_source_set_binding {
	s32 slot;
	s32 sequence;
	s32 body_head_model;
	s32 standalone_model;
	char descriptor[FS_MAXPATH];
} grid_source_set_binding_t;

struct asset_catalog_grid_source_set {
	size_t package_count, row_count;
	grid_source_set_package_t *packages;
	grid_match_source_root_t *rows;
	grid_source_set_binding_t *bindings;
	asset_catalog_arena_lease_t arena;
	catalog_dep_snapshot_t prior_deps;
};

static void gridSourceSetFree(asset_catalog_grid_source_set_t *set)
{
	if (!set) return;
	catalogDepSnapshotDestroy(&set->prior_deps);
	free(set->bindings);
	free(set->rows);
	free(set->packages);
	free(set);
}

static size_t gridSourceSetPackageIndex(const asset_catalog_grid_source_set_t *set, size_t package_index)
{
	size_t first = 0, last = set->package_count;
	while (first < last) {
		const size_t middle = (first + last) / 2;
		if (set->packages[middle].index < package_index) first = middle + 1;
		else last = middle;
	}
	return first < set->package_count && set->packages[first].index == package_index ? first : set->package_count;
}

static size_t gridSourceSetFindRow(const asset_catalog_grid_source_set_t *set, const char *id)
{
	size_t first = 0, last = set->row_count;
	while (first < last) {
		const size_t middle = (first + last) / 2;
		if (strcmp(set->rows[middle].id, id) < 0) first = middle + 1;
		else last = middle;
	}
	return first < set->row_count && strcmp(set->rows[first].id, id) == 0 ? first : set->row_count;
}

static s32 gridSourceSetOwnsId(const char *id, void *userdata)
{
	const asset_catalog_grid_source_set_t *set = userdata;
	return gridSourceSetFindRow(set, id) < set->row_count;
}

static s32 gridSourceSetOwnsRow(const asset_catalog_grid_source_set_t *set, size_t index,
	const asset_entry_t *row)
{
	const grid_source_set_binding_t *binding = &set->bindings[index];
	if (!row || !row->enabled || row->bundled || row->type != set->rows[index].type
			|| strcmp(row->descriptor_path, binding->descriptor) != 0) return 0;
	const asset_data_handle_t source = catalogEffectiveHandle(row);
	if (assetHandleIsNull(source)) {
		return row->type != ASSET_ARENA && row->type != ASSET_SCENARIO
			&& row->type != ASSET_AUDIO && row->type != ASSET_MODEL && row->type != ASSET_TEXTURE
			&& row->type != ASSET_VEHICLE && !gridSourceIsBodyHead(row->type);
	}
	const char *path = fileProviderPath(source);
	const char *leaf = strrchr(binding->descriptor, ':');
	if (!path || !leaf || leaf == binding->descriptor || leaf[-1] != ':') return 0;
	const size_t owner_len = (size_t)(leaf - binding->descriptor - 1);
	return strncmp(path, binding->descriptor, owner_len) == 0
		&& strncmp(path + owner_len, "::", 2) == 0;
}

static s32 gridSourceSetRecordRows(asset_catalog_grid_source_set_t *set, const grid_match_sources_t *plan,
	const char *arena_id, const char *scenario_id)
{
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		const size_t package_index = gridSourceSetPackageIndex(set, entry->package_index);
		if (package_index == set->package_count) continue;
		const grid_match_source_package_t *owner = &plan->packages[entry->package_index];
		const s32 child = entry->member_chain[0] != '\0';
		const s32 stage_owner = owner->owner.type == ASSET_ARENA;
		if (!gridLeaseCanonicalId(entry->asset.id) || !catalogIdSharesNamespace(owner->owner.id, entry->asset.id)
				|| assetCatalogResolveAny(entry->asset.id) || gridWeaponFindSlot(&entry->asset) >= 0) return 0;
		if (stage_owner) {
			if (!scenario_id || strcmp(owner->owner.id, arena_id) != 0
					|| (child ? entry->asset.type != ASSET_SCENARIO || strcmp(entry->asset.id, scenario_id) != 0
						: entry->asset.type != ASSET_ARENA)) return 0;
		} else if (child && !gridSourceSetChildType(entry->asset.type, owner->owner.type, entry->member_chain)) {
			return 0;
		}
		s32 prior_deps = 0;
		catalogDepForEachTyped(entry->asset.id, gridLeaseMarkDependency, &prior_deps);
		if (prior_deps) return 0;

		const size_t index = set->row_count;
		grid_source_set_binding_t *binding = &set->bindings[index];
		const char *path = set->packages[package_index].path;
		const char *leaf = assetArchiveDescriptorForPath(child ? entry->member_chain : path);
		char qualified[FS_MAXPATH];
		if (!leaf || !(child ? assetPathJoinChecked(qualified, sizeof(qualified), path, "::", entry->member_chain)
					: assetPathCopyChecked(qualified, sizeof(qualified), path))
				|| !assetPathJoinChecked(binding->descriptor, sizeof(binding->descriptor), qualified, "::", leaf)) return 0;
		binding->slot = -1;
		binding->sequence = !child && owner->owner.type == ASSET_AUDIO
			&& strcmp(owner->suffix, ".pdsong") == 0;
		binding->body_head_model = child && entry->asset.type == ASSET_MODEL
			&& gridSourceIsBodyHead(owner->owner.type);
		binding->standalone_model = !child && entry->asset.type == ASSET_MODEL;
		set->rows[index] = entry->asset;
		set->row_count++;
	}
	return set->row_count >= set->package_count;
}

static s32 gridPlanStandaloneMesh(const char *archive, const char *id, const void *bytes, u32 size,
	const ini_section_t *ini, loader_walker_mesh_source_plan_t *plan, char *err, size_t err_cap)
{
	u32 manifest_size = 0;
	void *manifest = gridMatchPackageReadMember(bytes, size, ASSET_ARCHIVE_META_MANIFEST_PATH,
		1024u * 1024u, &manifest_size, NULL);
	const s32 planned = manifest && loaderWalkerMeshSourcePlanManifest(manifest, manifest_size, id,
		archive, weaponNestedMeshPublicGeometry(ini), -1, plan, err, err_cap);
	free(manifest);
	if (!planned) {
		if (err && err_cap && err[0]) return 0;
		return gridLeaseError(err, err_cap, "the original .pdmesh source plan is invalid");
	}

	u32 geometry_size = 0;
	void *geometry = gridMatchPackageReadMember(bytes, size, plan->geometry_member,
		NET_DISTRIB_MAX_SESSION, &geometry_size, NULL);
	free(geometry);
	if (!geometry_size) return gridLeaseError(err, err_cap, "the original .pdmesh has no exact geometry member");
	return 1;
}

/* Installed and temporary standalone sources share this native bridge. Meshes
 * embedded in other typed owners still use their own compound registrar. */
static s32 gridPrepareOriginalStandaloneMesh(const char *descriptor, const char *id,
	loader_walker_mesh_source_plan_t *plan, char *err, size_t err_cap)
{
	char archive[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	if (!descriptor || !gridMatchPackageLocation(descriptor, archive, sizeof(archive), chain, sizeof(chain), NULL)
			|| chain[0] || assetArchiveTypeForPath(archive) != ASSET_MODEL) return 0;
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(archive, &size);
	ini_section_t *ini = malloc(sizeof(*ini));
	const s32 ok = gridLeaseCanonicalId(id) && strncmp(id, "base:", 5) != 0
		&& bytes && ini && size <= NET_DISTRIB_MAX_COMP
		&& gridMatchPackageOrigin(archive, bytes, size) == GRID_ARCHIVE_ORIGINAL
		&& gridSourceSetReadDescriptor(bytes, size, archive, ASSET_MODEL, id, ini)
		&& gridPlanStandaloneMesh(archive, id, bytes, size, ini, plan, err, err_cap);
	free(ini);
	free(bytes);
	if (ok) return 1;
	if (err && err_cap && !err[0]) gridLeaseError(err, err_cap, "could not read the exact original .pdmesh source");
	return -1;
}

static s32 gridBindStandaloneMesh(asset_entry_t *row, loader_walker_mesh_source_plan_t *plan,
	char *err, size_t err_cap)
{
	const asset_data_handle_t prior = row->source.primary;
	if (prior.provider == fileProvider()) {
		const char *path = fileProviderPath(prior);
		const size_t length = strlen(plan->archive_path);
		if (!path || strncmp(path, plan->archive_path, length) != 0
				|| strncmp(path + length, "::", 2) != 0) {
			gridLeaseError(err, err_cap, "another archive already owns this model source");
			return -1;
		}
	}
	const s32 old_slot = assetCatalogFindModelPrivateSlot(row->id);
	const s32 slot = assetCatalogResolveModelPrivateSlot(row->id);
	plan->source_symbol_present = 0;
	plan->source_filenum = assetCatalogModelPrivateSourceFilenum(slot);
	const char *slot_owner = slot >= 0 ? catalogModelIdByModelnum(slot) : NULL;
	const char *file_owner = plan->source_filenum > 0
		? catalogIdBySourceFilenum(ASSET_MODEL, plan->source_filenum) : NULL;
	if (slot < 0 || plan->source_filenum <= 0
			|| (slot_owner && strcmp(slot_owner, row->id) != 0)
			|| (file_owner && strcmp(file_owner, row->id) != 0)
			|| !loaderWalkerBindMeshSource(row, plan, 1, err, err_cap)) {
		if (slot >= 0 && old_slot < 0) (void)assetCatalogRetireModelReservation(row->id, slot);
		if (err && err_cap && !err[0]) gridLeaseError(err, err_cap, "no exact private model source binding is available");
		return -1;
	}
	return slot;
}

static s32 gridSourceSetValidateTexture(const void *bytes, u32 size, const char *id,
	const ini_section_t *ini, char *err, size_t err_cap)
{
	const char *source = iniGet(ini, "file_path", iniGet(ini, "texture_file", ""));
	if (!pathEndsWithNoCase(source, ".png") && !pathEndsWithNoCase(source, ".tga")
			&& !pathEndsWithNoCase(source, ".jpg") && !pathEndsWithNoCase(source, ".jpeg")
			&& !pathEndsWithNoCase(source, ".bmp")) {
		return gridLeaseError(err, err_cap, "Grid texture requires an editable image member");
	}
	u32 descriptor_size = 0;
	char *descriptor = gridMatchPackageReadMember(bytes, size, "texture.ini",
		NET_DISTRIB_MAX_COMP, &descriptor_size, NULL);
	texture_source_properties_t properties;
	const s32 descriptor_ok = descriptor && textureSourceReadProperties(descriptor,
		descriptor_size, id, &properties, err, err_cap);
	free(descriptor);
	if (!descriptor_ok) return 0;

	u32 image_size = 0;
	void *image = gridMatchPackageReadMember(bytes, size, source, NET_DISTRIB_MAX_SESSION, &image_size, NULL);
	if (!image) return gridLeaseError(err, err_cap, "Grid texture image member could not be read");
	mod_texture_rgba32_source_t rgba;
	const s32 decoded = modTextureDecodeRgba32Source(image, image_size, &rgba, err, err_cap);
	modTextureFreeRgba32Source(&rgba);
	free(image);
	return decoded > 0;
}

static s32 gridSourceSetInspectVehicle(const void *bytes, u32 size, const ini_section_t *ini,
	char *err, size_t err_cap)
{
	const char *const members[] = {
		iniGet(ini, "model_file", iniGet(ini, "file_path", "")),
		iniGet(ini, "physics_file", ""),
		iniGet(ini, "behavior_graph", ""),
	};
	if (assetArchiveTypeForPath(members[0]) != ASSET_MODEL) {
		return gridLeaseError(err, err_cap, "a Grid vehicle requires an exact embedded native model");
	}
	for (size_t i = 0; i < ARRAYCOUNT(members); i++) {
		u32 length = 0;
		void *member = gridMatchPackageReadMember(bytes, size, members[i],
			NET_DISTRIB_MAX_SESSION, &length, NULL);
		const s32 valid = member && length;
		free(member);
		if (!valid) return gridLeaseError(err, err_cap, "a Grid vehicle is missing its exact model, physics or behavior source");
	}
	return 1;
}

s32 assetCatalogGridVehicleSourceShareable(const char *name, const void *bytes, u32 size)
{
	if (assetArchiveTypeForPath(name) != ASSET_VEHICLE || !size || size > NET_DISTRIB_MAX_COMP) return 0;
	u32 length = 0;
	char *text = gridMatchPackageReadMember(bytes, size, "vehicle.ini", 1024u * 1024u, &length, NULL);
	ini_section_t *ini = malloc(sizeof(*ini));
	char id[CATALOG_ID_LEN];
	const s32 ok = text && ini && !memchr(text, '\0', length) && iniParseText(text, length, ini, NULL, 0)
		&& assetPathCopyChecked(id, sizeof(id), iniGet(ini, "catalog_id", ""))
		&& gridLeaseCanonicalId(id) && strncmp(id, "base:", 5) != 0
		&& gridLeaseParseDescriptor(text, length, ASSET_VEHICLE, id, ini)
		&& gridSourceSetInspectVehicle(bytes, size, ini, NULL, 0);
	free(ini);
	free(text);
	return ok;
}

typedef struct grid_body_head_members {
	const char *mesh;
	const char *hand;
	s32 valid;
} grid_body_head_members_t;

static s32 gridBodyHeadInspectMember(const char *name, u32 size, void *userdata)
{
	(void)size;
	grid_body_head_members_t *members = userdata;
	if (assetArchivePathIsTyped(name)
			&& (!members->mesh || strcmp(name, members->mesh) != 0)
			&& (!members->hand || strcmp(name, members->hand) != 0)) {
		members->valid = 0;
		return 1;
	}
	return 0;
}

static s32 gridBodyHeadInspectMesh(const void *bytes, u32 size, const char *member,
	const char *expected_id, const char *owner, ini_section_t *ini, char id[CATALOG_ID_LEN],
	char *err, size_t err_cap)
{
	u32 nested_size = 0, member_size = 0, manifest_size = 0;
	void *nested = assetArchiveTypeForPath(member) == ASSET_MODEL
		? gridMatchPackageReadMember(bytes, size, member, NET_DISTRIB_MAX_SESSION, &nested_size, NULL) : NULL;
	char *text = nested
		? gridMatchPackageReadMember(nested, nested_size, "mesh.ini", 1024u * 1024u, &member_size, NULL) : NULL;
	grid_body_head_members_t inventory = { .valid = 1 };
	s32 ok = text && !memchr(text, '\0', member_size)
		&& modArchiveMemForEachEntry(nested, nested_size, gridBodyHeadInspectMember, &inventory) == MODARCHIVE_OK
		&& inventory.valid && assetArchiveValidateBytes(nested, nested_size, member,
			ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) == 0
		&& bodyHeadSourceReadIniBytes(text, member_size, "model", ini, err, err_cap)
		&& assetPathCopyChecked(id, CATALOG_ID_LEN, iniGet(ini, "catalog_id", ""))
		&& gridLeaseCanonicalId(id) && catalogIdSharesNamespace(owner, id) && strcmp(owner, id) != 0
		&& (!expected_id[0] || strcmp(id, expected_id) == 0)
		&& strcmp(iniGet(ini, "id", id), id) == 0
		&& gridLeaseParseDescriptor(text, member_size, ASSET_MODEL, id, ini)
		&& strcmp(iniGet(ini, "kind", ""), "mesh") == 0;

	const char *geometry = NULL;
	for (size_t i = 0; ok && i < loaderWalkerMeshPublicGeometryKeyCount(); ++i) {
		const char *value = iniGet(ini, loaderWalkerMeshPublicGeometryKey(i), NULL);
		if (!value) continue;
		ok = value[0] && (!geometry || strcmp(geometry, value) == 0);
		geometry = value;
	}
	s32 manifest_absent = 0;
	char *manifest = ok ? gridMatchPackageReadMember(nested, nested_size, ASSET_ARCHIVE_META_MANIFEST_PATH,
		1024u * 1024u, &manifest_size, &manifest_absent) : NULL;
	loader_walker_mesh_source_plan_t plan;
	ok = ok && (manifest || manifest_absent)
		&& loaderWalkerMeshSourcePlanManifest(manifest ? manifest : "{}", manifest ? manifest_size : 2,
			id, member, geometry ? geometry : "model.obj", -1, &plan, err, err_cap)
		&& plan.source_filenum <= 0;
	void *model = ok ? gridMatchPackageReadMember(nested, nested_size, plan.geometry_member,
		NET_DISTRIB_MAX_SESSION, &member_size, NULL) : NULL;
	ok = ok && model && member_size;
	free(model);
	free(manifest);
	free(text);
	free(nested);
	return ok;
}

static s32 gridSourceSetInspectBodyHead(const char *name, const void *bytes, u32 size,
	const char *expected_id, char *err, size_t err_cap)
{
	const asset_type_e type = assetArchiveTypeForPath(name);
	if (!gridSourceIsBodyHead(type) || !bytes || !size || size > NET_DISTRIB_MAX_COMP) return 0;
	const s32 is_head = type == ASSET_HEAD;
	u32 length = 0;
	char *text = gridMatchPackageReadMember(bytes, size, is_head ? "head.ini" : "body.ini",
		1024u * 1024u, &length, NULL);
	ini_section_t *ini = malloc(sizeof(*ini));
	char id[CATALOG_ID_LEN], mesh_id[CATALOG_ID_LEN], hand_id[CATALOG_ID_LEN];
	body_head_source_t source;
	s32 ok = text && ini && !memchr(text, '\0', length)
		&& bodyHeadSourceReadIniBytes(text, length, is_head ? "head" : "body", ini, err, err_cap)
		&& assetPathCopyChecked(id, sizeof(id), iniGet(ini, "catalog_id", ""))
		&& gridLeaseCanonicalId(id) && strncmp(id, "base:", 5) != 0
		&& (!expected_id || strcmp(id, expected_id) == 0)
		&& strcmp(iniGet(ini, GRID_ARENA_PROVENANCE_KEY, ""), GRID_ARENA_PROVENANCE_ORIGINAL) == 0
		&& bodyHeadSourceParseIni(ini, id, is_head, &source, err, err_cap)
		&& source.enabled && !source.bundled && source.rig_class[0] && source.mesh_archive[0]
		&& gridLeaseParseDescriptor(text, length, type, id, ini);
	if (ok) {
		grid_body_head_members_t members = { source.mesh_archive, source.hand_archive, 1 };
		ok = modArchiveMemForEachEntry(bytes, size, gridBodyHeadInspectMember, &members) == MODARCHIVE_OK
			&& members.valid && gridBodyHeadInspectMesh(bytes, size, source.mesh_archive,
				source.mesh_id, id, ini, mesh_id, err, err_cap);
		if (ok && source.hand_archive[0]) {
			ok = gridBodyHeadInspectMesh(bytes, size, source.hand_archive, source.hand_id,
				id, ini, hand_id, err, err_cap)
				&& (strcmp(mesh_id, hand_id) != 0 || strcmp(source.mesh_archive, source.hand_archive) == 0);
		}
	}
	free(ini);
	free(text);
	if (ok || (err && err_cap && err[0])) return ok;
	return gridLeaseError(err, err_cap, "Grid body/head requires exact private native model sources");
}

s32 assetCatalogGridBodyHeadSourceShareable(const char *name, const void *bytes, u32 size)
{
	return name && gridSourceSetInspectBodyHead(name, bytes, size, NULL, NULL, 0);
}

/* Verify what the exact lease would publish without opening a scratch path
 * or consulting local runtime reservations. Both callers use strict inventory. */
static s32 gridSourceSetInspectInventory(grid_weapon_admission_t *scan, size_t package_index,
	const void *bytes, u32 size, char *err, size_t err_cap)
{
	const grid_match_sources_t *plan = scan->plan;
	if (!plan || !plan->packages || !plan->entries || package_index >= plan->package_count
			|| plan->entry_count > MANIFEST_MAX_ENTRIES || !bytes || !size
			|| size > NET_DISTRIB_MAX_COMP || size != plan->packages[package_index].size) {
		return gridLeaseError(err, err_cap, "invalid Grid source inventory input");
	}
	const grid_match_source_package_t *package = &plan->packages[package_index];
	const s32 arena = package->owner.type == ASSET_ARENA;
	s32 owner_count = 0, scenarios = 0;
	for (size_t i = 0; i < plan->entry_count; ++i) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (entry->package_index != package_index) continue;
		if (!gridLeaseCanonicalId(entry->asset.id)
				|| !catalogIdSharesNamespace(package->owner.id, entry->asset.id)) {
			return gridLeaseError(err, err_cap, "a Grid source inventory row belongs to a different namespace");
		}
		if (!entry->member_chain[0]) {
			if (entry->asset.type != package->owner.type || strcmp(entry->asset.id, package->owner.id) != 0) {
				return gridLeaseError(err, err_cap, "the Grid source inventory root differs from its package owner");
			}
			++owner_count;
		} else if (arena) {
			if (entry->asset.type != ASSET_SCENARIO || assetArchiveTypeForPath(entry->member_chain) != ASSET_SCENARIO
					|| strstr(entry->member_chain, "::")) {
				return gridLeaseError(err, err_cap, "an arena may only supply its exact embedded scenario");
			}
			++scenarios;
		} else if (!gridSourceSetChildType(entry->asset.type, package->owner.type, entry->member_chain)) {
			return gridLeaseError(err, err_cap, "the required archive contains an unsupported Grid typed child");
		}
	}
	if (owner_count != 1 || (arena && scenarios != 1)) {
		return gridLeaseError(err, err_cap, "the Grid source inventory omits its owner or exact scenario");
	}
	u8 seen_members[MANIFEST_MAX_ENTRIES] = {0};
	scan->package_index = package_index;
	scan->allow_scenario = arena;
	scan->seen_members = seen_members;
	const s32 valid = gridWeaponInspectMembers(scan, bytes, size, "", 0);
	scan->seen_members = NULL;
	if (!valid) {
		return gridLeaseError(err, err_cap, "the archive contains unplanned or invalid Grid typed members");
	}
	for (size_t i = 0; i < plan->entry_count; ++i) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (entry->package_index == package_index && entry->member_chain[0] && !seen_members[i]) {
			return gridLeaseError(err, err_cap, "the archive is missing a planned Grid typed member");
		}
	}
	return 1;
}

s32 assetCatalogGridPackageInventoryEligible(const grid_match_sources_t *plan, size_t package_index,
	const void *bytes, u32 size, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	ini_section_t *ini = malloc(sizeof(*ini));
	if (!ini) return gridLeaseError(err, err_cap, "could not allocate a Grid inventory inspection");
	grid_weapon_admission_t scan = {
		.plan = plan, .ini = ini, .remaining = NET_DISTRIB_MAX_SESSION, .strict_inventory = 1,
	};
	const s32 ok = gridSourceSetInspectInventory(&scan, package_index, bytes, size, err, err_cap);
	free(ini);
	return ok;
}

static s32 gridSourceSetInspectPackage(asset_catalog_grid_source_set_t *set, size_t package_index,
	const char *arena_id, const char *scenario_id, grid_weapon_admission_t *scan, char *err, size_t err_cap)
{
	const grid_match_source_package_t *package = &scan->plan->packages[package_index];
	const size_t position = gridSourceSetPackageIndex(set, package_index);
	grid_source_set_package_t *staged = &set->packages[position];
	const char *path = staged->path;
	const s32 arena = package->owner.type == ASSET_ARENA;
	u8 sha[SHA256_DIGEST_SIZE];
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(path, &size);
	if (!bytes || size != package->size) {
		free(bytes);
		return gridLeaseError(err, err_cap, "the staged Grid package is absent or has a different size");
	}
	sha256Hash(bytes, size, sha);
	s32 ok = memcmp(sha, package->digest, sizeof(sha)) == 0
		&& gridSourceSetInspectInventory(scan, package_index, bytes, size, err, err_cap)
		&& assetArchiveValidateBytes(bytes, size, path, ASSET_ARCHIVE_VALIDATE_RELEASE, err, err_cap) == 0
		&& gridMatchPackageOrigin(path, bytes, size) == GRID_ARCHIVE_ORIGINAL
		&& gridSourceSetReadDescriptor(bytes, size, path, package->owner.type, package->owner.id, scan->ini);
	if (ok && package->owner.type == ASSET_MODEL) {
		loader_walker_mesh_source_plan_t mesh;
		ok = gridPlanStandaloneMesh(path, package->owner.id, bytes, size, scan->ini, &mesh, err, err_cap);
	}
	if (ok && package->owner.type == ASSET_TEXTURE) {
		ok = gridSourceSetValidateTexture(bytes, size, package->owner.id, scan->ini, err, err_cap);
	}
	if (ok && package->owner.type == ASSET_AUDIO && strcmp(package->suffix, ".pdsong") == 0) {
		ok = modSequenceValidateGridSource(package->suffix, bytes, size, err, err_cap);
	}
	if (ok && package->owner.type == ASSET_VEHICLE) {
		ok = gridSourceSetInspectVehicle(bytes, size, scan->ini, err, err_cap);
	}
	if (ok && gridSourceIsBodyHead(package->owner.type)) {
		ok = gridSourceSetInspectBodyHead(path, bytes, size, package->owner.id, err, err_cap);
	}
	if (ok && arena) {
		asset_catalog_grid_arena_source_t source;
		ok = assetCatalogPreflightGridArenaLease(path, arena_id, scenario_id,
			GRID_ARENA_LEASE_TEMPORARY_FILE, bytes, size, &source, err, err_cap);
		if (ok) {
			asset_catalog_arena_lease_t *lease = &set->arena;
			lease->file_policy = GRID_ARENA_LEASE_TEMPORARY_FILE;
			assetPathCopyChecked(lease->archive_path, sizeof(lease->archive_path), path);
			assetPathCopyChecked(lease->arena_id, sizeof(lease->arena_id), arena_id);
			assetPathCopyChecked(lease->scenario_id, sizeof(lease->scenario_id), scenario_id);
			assetPathCopyChecked(lease->scenario_member, sizeof(lease->scenario_member), source.scenario_member);
			lease->stagenum = assetCatalogPreviewGridArenaLeaseStage(scenario_id);
			ok = lease->stagenum >= 0;
		}
	}
	free(bytes);
	if (ok || (err && err_cap && err[0])) return ok;
	return gridLeaseError(err, err_cap, "invalid original Grid source, archive inventory or exact identity");
}

/* The supported rows defer runtime activation. Reserve source-bearing leaves
 * first, then graph owners and finally the one playable native stage. */
static s32 gridSourceSetFamilyOrder(asset_type_e type)
{
	switch (type) {
	case ASSET_MODEL: case ASSET_MATERIAL: case ASSET_AUDIO: case ASSET_UI:
	case ASSET_TEXTURE: return 0;
	case ASSET_EFFECT: case ASSET_PROJECTILE: case ASSET_ENTITY: return 1;
	case ASSET_WEAPON: case ASSET_VEHICLE: case ASSET_BODY: case ASSET_HEAD: return 2;
	case ASSET_ARENA: return 3;
	default: return -1;
	}
}

typedef struct grid_source_set_admission {
	const grid_match_sources_t *plan;
	asset_catalog_grid_source_set_t *set;
} grid_source_set_admission_t;

static s32 gridSourceSetAcceptEdge(const char *owner, const char *dependency, asset_type_e type,
	s32 bundled, void *userdata)
{
	const grid_source_set_admission_t *admission = userdata;
	if (bundled || !gridSourceSetOwnsId(owner, admission->set)) return 0;
	const asset_entry_t *row = assetCatalogResolve(dependency);
	if (!row || !row->enabled || row->type != type) return 0;
	const grid_match_sources_t *plan = admission->plan;
	size_t first = 0, last = plan->entry_count;
	while (first < last) {
		const size_t middle = (first + last) / 2;
		if (strcmp(plan->entries[middle].asset.id, dependency) < 0) first = middle + 1;
		else last = middle;
	}
	if (first == plan->entry_count) return 0;
	const grid_match_source_entry_t *entry = &plan->entries[first];
	return strcmp(entry->asset.id, dependency) == 0 && entry->asset.type == type
		&& plan->packages[entry->package_index].bundled == !!row->bundled;
}

static s32 gridSourceSetValidateRows(grid_source_set_admission_t *admission,
	const external_scan_transaction_t *transaction, char *err, size_t err_cap)
{
	asset_catalog_grid_source_set_t *set = admission->set;
	if ((size_t)assetCatalogGetPoolSize() != (size_t)transaction->row_count + set->row_count) return 0;
	for (s32 i = 0; i < transaction->row_count; i++) {
		const asset_entry_t *row = assetCatalogGetByIndex(i);
		const catalog_entry_snapshot_t *prior = &transaction->rows[i];
		if (row ? !catalogEntrySnapshotMatches(prior, row) : prior->row.occupied) return 0;
	}
	if (set->arena.arena_id[0]) {
		const asset_entry_t *arena = assetCatalogResolveAny(set->arena.arena_id);
		const asset_entry_t *scenario = assetCatalogResolveAny(set->arena.scenario_id);
		if (!arena) return 0;
		set->arena.stage_index = arena->runtime_index;
		if (!gridLeaseOwnsRows(&set->arena, arena, scenario)) return 0;
	}
	if (!catalogDepValidateAppended(&transaction->deps, gridSourceSetAcceptEdge, admission)) return 0;
	for (size_t i = 0; i < set->row_count; i++) {
		const asset_entry_t *row = assetCatalogResolveAny(set->rows[i].id);
		if (!gridSourceSetOwnsRow(set, i, row) || row->ref_count || row->stage_ref_count || row->loaded_data
				|| row->payload_kind != ASSET_PAYLOAD_NONE) return 0;
		const s32 slot = gridWeaponFindSlot(&set->rows[i]);
		const s32 sequence = set->bindings[i].sequence;
		if ((row->type == ASSET_WEAPON && (slot < 0 || row->runtime_index != slot))
				|| (row->type == ASSET_MODEL && (row->runtime_index != slot
					|| (set->bindings[i].standalone_model && slot < 0)
					|| (slot < 0 ? row->source_filenum > 0
						: row->source_filenum != assetCatalogModelPrivateSourceFilenum(slot))))
				|| (sequence && (row->type != ASSET_AUDIO || row->ext.audio.category != AUDIO_CAT_MUSIC
					|| slot >= 0 || row->source_soundnum != -1 || row->ext.audio.sound_id != -1
					|| row->ext.audio.file_path[0]))
				|| (row->type == ASSET_AUDIO && !sequence && (slot < 0 || row->ext.audio.category != AUDIO_CAT_SFX
					|| row->source_soundnum != slot || row->ext.audio.sound_id != slot))
				|| (row->type == ASSET_TEXTURE && (slot < 0 || row->source_texnum != slot
					|| row->runtime_index != slot || row->ext.texture.texture_id != -1))
				|| !catalogCanDeactivateTypedAsset(row->type, row->id)) return 0;
		if (gridSourceIsBodyHead(row->type)) {
			const s32 is_head = row->type == ASSET_HEAD;
			if (slot < 0 || row->runtime_index != slot
					|| (is_head ? row->ext.head.headnum : row->ext.body.bodynum) != slot
					|| !loaderPoolRetirePublicBodyHead(is_head, row->id, slot, 0)
					|| !(is_head ? catalogManagerRetireCustomHead(row->id, slot, 0)
						: catalogManagerRetireCustomBody(row->id, slot, 0))) return 0;
		}
		if (row->type == ASSET_VEHICLE && !assetRuntimeVehiclePlacementSourceValid(row, err, err_cap)) return 0;
		set->bindings[i].slot = slot;
	}
	return 1;
}

static s32 gridSourceSetReserveSequences(asset_catalog_grid_source_set_t *set, char *err, size_t err_cap)
{
	for (size_t i = 0; i < set->row_count; i++) {
		grid_source_set_binding_t *binding = &set->bindings[i];
		if (!binding->sequence) continue;
		binding->slot = modSequenceReserveGridTrack(set->rows[i].id);
		if (binding->slot < 0) return gridLeaseError(err, err_cap, "could not reserve the exact Grid sequence track");
	}
	return 1;
}

static s32 gridSourceSetCancelSequences(const asset_catalog_grid_source_set_t *set)
{
	s32 ok = 1;
	for (size_t i = 0; i < set->row_count; i++) {
		const grid_source_set_binding_t *binding = &set->bindings[i];
		if (binding->sequence && binding->slot >= 0
				&& !modSequenceRetireGridTrack(set->rows[i].id, binding->slot)) ok = 0;
	}
	return ok;
}

s32 assetCatalogAdmitGridSourceSet(const grid_match_sources_t *plan, size_t count,
	const u16 *indices, const char *const *paths, const char *arena_id, const char *scenario_id,
	asset_catalog_grid_source_set_t **out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!plan || !out || *out || !count || count > plan->package_count || !indices || !paths
			|| !gridLeaseCanonicalId(arena_id)) {
		return gridLeaseError(err, err_cap, "invalid Grid source set request");
	}
	grid_match_sources_t sealed = *plan;
	if (!gridMatchSourcesSeal(&sealed, err, err_cap) || sealed.total_bytes != plan->total_bytes
			|| memcmp(sealed.digest, plan->digest, sizeof(sealed.digest)) != 0) {
		return gridLeaseError(err, err_cap, "Grid source set needs an exact sealed plan");
	}

	asset_catalog_grid_source_set_t *set = calloc(1, sizeof(*set));
	if (!set) return gridLeaseError(err, err_cap, "could not allocate the Grid source set");
	set->package_count = count;
	set->arena.stagenum = -1;
	set->packages = calloc(count, sizeof(*set->packages));
	set->rows = calloc(plan->entry_count, sizeof(*set->rows));
	set->bindings = calloc(plan->entry_count, sizeof(*set->bindings));
	ini_section_t *ini = malloc(sizeof(*ini));
	s32 ok = set->packages && set->rows && set->bindings && ini;
	for (size_t i = 0; ok && i < count; i++) {
		if (indices[i] >= plan->package_count || (i && indices[i - 1] >= indices[i]) || !paths[i]) {
			ok = 0;
			break;
		}
		const grid_match_source_package_t *package = &plan->packages[indices[i]];
		char folder[FS_MAXPATH + 1], expected[FS_MAXPATH + 1];
		ok = !package->bundled && gridLeaseCanonicalId(package->owner.id)
			&& gridSourceSetFamilyOrder(package->owner.type) >= 0
			&& gridMatchPackageTypeShareable(package->owner.type, package->suffix)
			&& (package->owner.type != ASSET_ARENA
				|| (strcmp(package->owner.id, arena_id) == 0 && gridLeaseCanonicalId(scenario_id)))
			&& gridMatchPackagePrivatePath(package->digest, package->suffix, folder, expected)
			&& strcmp(expected, paths[i]) == 0
			&& assetPathCopyChecked(set->packages[i].path, sizeof(set->packages[i].path), expected);
		set->packages[i].index = indices[i];
	}
	ok = ok && gridSourceSetRecordRows(set, plan, arena_id, scenario_id);
	grid_weapon_admission_t scan = {
		.plan = plan, .ini = ini, .remaining = NET_DISTRIB_MAX_SESSION, .strict_inventory = 1,
	};
	for (size_t i = 0; ok && i < count; i++) {
		ok = gridSourceSetInspectPackage(set, indices[i], arena_id, scenario_id, &scan, err, err_cap);
	}
	free(ini);
	if (!ok) {
		gridSourceSetFree(set);
		if (err && err_cap && err[0]) return 0;
		return gridLeaseError(err, err_cap, "invalid, unsupported or locally occupied Grid source set");
	}

	loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
	external_scan_transaction_t transaction;
	if (!batch || !externalScanTransactionBegin(&transaction)) {
		loaderAnimationSourceBatchDestroy(batch);
		gridSourceSetFree(set);
		return gridLeaseError(err, err_cap, "could not prepare the Grid source set catalog transaction");
	}
	for (s32 pass = 0; ok && pass < 4; pass++) {
		for (size_t i = 0; ok && i < count; i++) {
			const grid_match_source_package_t *package = &plan->packages[indices[i]];
			if (gridSourceSetFamilyOrder(package->owner.type) != pass) continue;
			if (gridSourceIsBodyHead(package->owner.type)) {
				ok = assetCatalogRegisterBodyHeadArchive(set->packages[i].path, package->owner.id,
					package->owner.type == ASSET_HEAD, -1, 0, err, err_cap);
				continue;
			}
			char namespace_id[CATALOG_ID_LEN];
			const size_t length = (size_t)(strchr(package->owner.id, ':') - package->owner.id);
			memcpy(namespace_id, package->owner.id, length);
			namespace_id[length] = '\0';
			ok = registerTypedPdDescriptorFile(set->packages[i].path, package->owner.type,
				namespace_id, 1, batch) == 1;
		}
	}
	grid_source_set_admission_t admission = { .plan = plan, .set = set };
	ok = ok && gridSourceSetValidateRows(&admission, &transaction, err, err_cap);
	for (size_t i = 0; ok && i < count; ++i) {
		const grid_match_source_package_t *package = &plan->packages[indices[i]];
		u8 digest[SHA256_DIGEST_SIZE];
		ok = modArchiveSha256(set->packages[i].path, digest) == MODARCHIVE_OK
			&& memcmp(digest, package->digest, sizeof(digest)) == 0;
	}
	ok = ok && gridSourceSetReserveSequences(set, err, err_cap)
		&& loaderAnimationSourceBatchCommit(batch);
	loaderAnimationSourceBatchDestroy(batch);
	if (!ok) {
		s32 restored = gridSourceSetCancelSequences(set);
		if (!externalScanTransactionRollback(&transaction)) restored = 0;
		externalScanTransactionDestroy(&transaction);
		assetCatalogTrimUnoccupiedTail();
		gridSourceSetFree(set);
		if (restored && err && err_cap && err[0]) return 0;
		return gridLeaseError(err, err_cap, restored ? "the typed scanner refused the exact Grid source set"
			: "the Grid source set catalog rollback failed");
	}
	/* Installed rows can declare one of these previously absent sources.
	 * Preserve the preexisting dormant edges and refuse new external owners. */
	set->prior_deps = transaction.deps;
	memset(&transaction.deps, 0, sizeof(transaction.deps));
	s32 textures_changed = 0;
	for (size_t i = 0; i < set->row_count; i++) {
		asset_entry_t *row = assetCatalogGetMutable(set->rows[i].id);
		row->temporary = 1;
		row->grid_offer_only = 1;
		if (row->type == ASSET_TEXTURE) {
			textureSourceRuntimeInvalidateSlot(set->bindings[i].slot);
			textures_changed = 1;
		}
	}
	externalScanTransactionDestroy(&transaction);
	catalogRefreshSoundOverrides();
	if (textures_changed) catalogRefreshTextureOverrides();
	*out = set;
	return 1;
}

s32 assetCatalogGridSourceSetStage(const asset_catalog_grid_source_set_t *set)
{
	return set ? set->arena.stagenum : -1;
}

const char *assetCatalogGridSourceSetPath(const asset_catalog_grid_source_set_t *set, size_t package_index)
{
	if (!set) return NULL;
	const size_t index = gridSourceSetPackageIndex(set, package_index);
	return index < set->package_count ? set->packages[index].path : NULL;
}

s32 assetCatalogReleaseGridSourceSetStageRefs(const asset_catalog_grid_source_set_t *set, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!set) return 1;
	for (size_t i = 0; i < set->row_count; i++) {
		const asset_entry_t *row = assetCatalogResolveAny(set->rows[i].id);
		if (!gridSourceSetOwnsRow(set, i, row) || !row->temporary || !row->grid_offer_only
				|| (row->stage_ref_count && (row->stage_ref_count != 1 || row->ref_count <= 0
					|| row->ref_count == ASSET_REF_BUNDLED || !catalogCanDeactivateTypedAsset(row->type, row->id)))) {
			return gridLeaseError(err, err_cap, "the Grid source set changed before stage release");
		}
	}
	for (size_t i = 0; i < set->row_count; ++i) {
		const asset_entry_t *row = assetCatalogResolveAny(set->rows[i].id);
		if (row->stage_ref_count) catalogReleaseStageAsset(row->type, row->id);
	}
	return 1;
}

static s32 gridSourceSetAllowsNewRetirementEdge(const char *owner, const char *dependency,
	asset_type_e type, s32 bundled, void *userdata)
{
	(void)type;
	(void)bundled;
	return !gridSourceSetOwnsId(dependency, userdata) || gridSourceSetOwnsId(owner, userdata);
}

static s32 gridSourceSetRetireBodyHead(const grid_match_source_root_t *row, s32 slot, s32 commit)
{
	const s32 is_head = row->type == ASSET_HEAD;
	if (slot < 0 || gridWeaponFindSlot(row) != slot
			|| (!commit && catalogModelGenerationHasLiveIdentity(row->id))) return 0;
	if (!(is_head ? catalogManagerRetireCustomHead(row->id, slot, 0)
			: catalogManagerRetireCustomBody(row->id, slot, 0))) return 0;
	if (!loaderPoolRetirePublicBodyHead(is_head, row->id, slot, commit)) return 0;
	if (!commit) return 1;
	const s32 cleared = is_head ? catalogManagerRetireCustomHead(row->id, slot, 1)
		: catalogManagerRetireCustomBody(row->id, slot, 1);
	return cleared && assetCatalogRetireBodyHeadReservation(is_head, row->id, slot);
}

static s32 gridSourceSetRetireBinding(const asset_catalog_grid_source_set_t *set, size_t i, s32 commit)
{
	const grid_source_set_binding_t *binding = &set->bindings[i];
	const grid_match_source_root_t *row = &set->rows[i];
	if (gridSourceIsBodyHead(row->type)) return gridSourceSetRetireBodyHead(row, binding->slot, commit);
	if (!commit && binding->body_head_model && catalogModelGenerationHasLiveIdentity(row->id)) return 0;
	if (binding->sequence) {
		return commit ? modSequenceRetireGridTrack(row->id, binding->slot)
			: modSequenceCanRetireGridTrack(row->id, binding->slot);
	}
	return (commit || gridWeaponFindSlot(row) == binding->slot)
		&& gridWeaponRetireSlot(row, binding->slot, commit);
}

s32 assetCatalogRetireGridSourceSet(asset_catalog_grid_source_set_t **owned, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!owned) return gridLeaseError(err, err_cap, "invalid Grid source set retirement request");
	asset_catalog_grid_source_set_t *set = *owned;
	if (!set) return 1;
	if (!catalogDepCanRetireOwnerSet(gridSourceSetOwnsId, set)
			&& !catalogDepValidateAppended(&set->prior_deps, gridSourceSetAllowsNewRetirementEdge, set)) {
		return gridLeaseError(err, err_cap, "another catalog source now depends on the temporary Grid source set");
	}
	if (set->arena.arena_id[0] && !gridLeaseOwnsRows(&set->arena, assetCatalogResolveAny(set->arena.arena_id),
			assetCatalogResolveAny(set->arena.scenario_id))) {
		return gridLeaseError(err, err_cap, "the temporary Grid source set no longer owns its native stage");
	}
	for (size_t i = 0; i < set->row_count; i++) {
		const asset_entry_t *row = assetCatalogResolveAny(set->rows[i].id);
		if (!gridSourceSetOwnsRow(set, i, row) || !row->temporary || !row->grid_offer_only
				|| row->ref_count || row->stage_ref_count || row->loaded_data || row->payload_kind != ASSET_PAYLOAD_NONE
				|| !gridSourceSetRetireBinding(set, i, 0)) {
			return gridLeaseError(err, err_cap, "the Grid source set changed or still has active references");
		}
	}
	for (size_t i = 0; i < set->row_count; i++) {
		if (!catalogDeactivateTypedAsset(set->rows[i].type, set->rows[i].id)) {
			return gridLeaseError(err, err_cap, "the Grid source set native adapters could not be detached");
		}
	}
	if (!assetCatalogRetireUnactivatedGridWeaponRows(set->rows, set->row_count)) {
		return gridLeaseError(err, err_cap, "the Grid source set catalog rows could not be retired");
	}
	/* Main-thread preflight above owns the exact native reservations. */
	s32 textures_changed = 0;
	for (size_t i = 0; i < set->row_count; i++) {
		(void)gridSourceSetRetireBinding(set, i, 1);
		catalogDepPruneOwner(set->rows[i].id, gridLeaseDropDependency, NULL);
		if (set->rows[i].type == ASSET_TEXTURE) textures_changed = 1;
	}
	if (set->arena.arena_id[0]) {
		stageTableRetireCustom(set->arena.stage_index, set->arena.stagenum);
		assetCatalogReleaseStagenumPrivateSlot(set->arena.scenario_id, set->arena.stagenum);
	}
	catalogRefreshSoundOverrides();
	if (textures_changed) catalogRefreshTextureOverrides();
	/* A live stage can intern new provider paths. Only the synchronous
	 * transaction rollback may rewind this shared append-only pool. */
	gridSourceSetFree(set);
	*owned = NULL;
	return 1;
}

static s32 scanTypedPdDescriptorsRecurse(const char *root_dir,
                                         const char *rel_dir,
                                         const char *mod_id,
										 s32 defer_reloads,
		loader_animation_source_batch_t *batch)
{
	char abs_dir[FS_MAXPATH];
	if (rel_dir && rel_dir[0]) {
		if (!assetPathJoinChecked(abs_dir, sizeof(abs_dir), root_dir, "/",
				rel_dir)) return -1;
	} else {
		if (!assetPathCopyChecked(abs_dir, sizeof(abs_dir), root_dir)) return -1;
	}

	DIR *dp = opendir(abs_dir);
	if (!dp) {
		return -1;
	}

	s32 count = 0;
	s32 rejected = 0;
	struct dirent *ent;
	while ((ent = readdir(dp)) != NULL) {
		if (!ent->d_name || ent->d_name[0] == '.') {
			continue;
		}

		char child_rel[FS_MAXPATH];
		if (rel_dir && rel_dir[0]) {
			if (!assetPathJoinChecked(child_rel, sizeof(child_rel), rel_dir, "/",
					ent->d_name)) {
				rejected = 1;
				continue;
			}
		} else {
			if (!assetPathCopyChecked(child_rel, sizeof(child_rel), ent->d_name)) {
				rejected = 1;
				continue;
			}
		}

		char child_abs[FS_MAXPATH];
		if (!assetPathJoinChecked(child_abs, sizeof(child_abs), root_dir, "/",
				child_rel)) {
			rejected = 1;
			continue;
		}

		if (isDirectory(child_abs)) {
			s32 child_result = scanTypedPdDescriptorsRecurse(root_dir, child_rel,
				mod_id, defer_reloads, batch);
			if (child_result < 0) rejected = 1;
			else count += child_result;
			continue;
		}

		if (!isRegularFile(child_abs)) {
			continue;
		}

		asset_type_e expected = typedPdContentTypeForPath(child_abs);
		if (expected == ASSET_NONE) {
			continue;
		}

		s32 candidate_result = registerTypedPdDescriptorFile(child_abs, expected,
			mod_id, defer_reloads, batch);
		if (candidate_result < 0) rejected = 1;
		else count += candidate_result;
	}

	closedir(dp);
	/* Preserve the accepted count for ordinary scans, but never let sibling
	 * success erase a recognized typed-source rejection. */
	return rejected ? -(count + 1) : count;
}

/**
 * Scan a single category directory within a mod's _components/ folder.
 * e.g., mods/my_mod/_components/maps/
 *
 * @param category_dir  Full path to the category directory
 * @param category_name Directory name ("maps", "characters", etc.)
 * @param mod_id        Mod identifier
 * @return Number of components registered
 */
static s32 scanCategoryDir(const char *category_dir, const char *category_name,
                            const char *mod_id,
		loader_animation_source_batch_t *batch)
{
	asset_type_e expected = categoryToType(category_name);
	if (expected == ASSET_NONE) {
		sysLogPrintf(LOG_NOTE, "assetcatalog_scanner: skipping unknown category '%s'",
			category_name);
		return 0;
	}

	DIR *dp = opendir(category_dir);
	if (!dp) {
		return 0;
	}

	s32 count = 0, rejected = 0;
	struct dirent *ent;
	char pathbuf[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		/* skip . and .. */
		if (ent->d_name[0] == '.') {
			continue;
		}

		if (!assetPathJoinChecked(pathbuf, sizeof(pathbuf), category_dir, "/",
				ent->d_name)) continue;

		if (!isDirectory(pathbuf)) {
			continue;
		}

		/* Parse the .ini in this component directory */
		ini_section_t ini;
		char descriptor_path[FS_MAXPATH];
		if (!findAndParseIni(pathbuf, &ini,
				descriptor_path, sizeof(descriptor_path))) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: no valid .ini in component '%s/%s'",
				category_name, ent->d_name);
			continue;
		}

		/* Verify the INI section type matches the category */
		asset_type_e ini_type = sectionToType(ini.type);
		if (ini_type != expected) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: type mismatch in '%s/%s': "
				"expected %d, got [%s]=%d",
				category_name, ent->d_name, expected, ini.type, ini_type);
			/* Register anyway -- the INI section type takes precedence */
		}

		if (registerComponentWithAnimationBatch(&ini, pathbuf, mod_id, descriptor_path, batch)) {
			count++;
		} else rejected = 1;
	}

	closedir(dp);
	return rejected ? -(count + 1) : count;
}

/**
 * Scan a single mod directory for _components/ subdirectory.
 *
 * @param mod_dir  Full path to the mod directory (e.g., mods/my_mod)
 * @param mod_id   Mod identifier (e.g., "my_mod")
 * @return Number of components registered
 */
static s32 scanModDir(const char *mod_dir, const char *mod_id)
{
	char components_dir[FS_MAXPATH];
	if (!assetPathJoinChecked(components_dir, sizeof(components_dir), mod_dir,
			"/", "_components")) return 0;

	if (!isDirectory(components_dir)) {
		return 0;  /* no _components/ directory -- legacy mod, skip */
	}

	DIR *dp = opendir(components_dir);
	if (!dp) {
		return 0;
	}

    external_scan_transaction_t transaction = {0};
    loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
    if (!batch || !externalScanTransactionBegin(&transaction)) {
        loaderAnimationSourceBatchDestroy(batch); closedir(dp); return -1;
    }

	s32 count = 0, rejected = 0;
	struct dirent *ent;
	char pathbuf[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		if (ent->d_name[0] == '.') {
			continue;
		}

		if (!assetPathJoinChecked(pathbuf, sizeof(pathbuf), components_dir, "/",
				ent->d_name)) continue;

		if (!isDirectory(pathbuf)) {
			continue;
		}

		s32 n = scanCategoryDir(pathbuf, ent->d_name, mod_id, batch);
		if (n > 0) {
			sysLogPrintf(LOG_NOTE, "assetcatalog_scanner: %s/%s: %d components",
				mod_id, ent->d_name, n);
		}
		if (n < 0) { rejected = 1; count += -n - 1; }
		else count += n;
	}

	closedir(dp);
    if (!rejected && !loaderAnimationSourceBatchCommit(batch)) rejected = 1;
    if (rejected && !externalScanTransactionRollback(&transaction))
        sysLoudFailf("CATALOG.COMPONENT.ROLLBACK", "could not restore legacy mod import");
    externalScanTransactionDestroy(&transaction);
    loaderAnimationSourceBatchDestroy(batch);
    return rejected ? -(count + 1) : count;
}

typedef struct external_descriptor_spec {
	const char *relative_dir;
	const char *leaf;
	asset_type_e expected;
} external_descriptor_spec_t;

static void externalScanAccumulate(s32 result, s32 *total, s32 *rejected)
{
	if (result < 0) {
		if (rejected) *rejected = 1;
		/* Recursive scanners encode the accepted sibling count as -(n + 1). */
		if (total) *total += -result - 1;
	} else if (total) {
		*total += result;
	}
}

static s32 assetCatalogScanExternalLayoutFolderInternal(const char *mod_id,
		const char *mod_dir, s32 defer_reloads)
{
	static const external_descriptor_spec_t root_specs[] = {
		{ "", "weapon.ini", ASSET_WEAPON },
		{ "", "projectile.ini", ASSET_PROJECTILE },
		{ "", "entity.ini", ASSET_ENTITY },
		{ "", "material.ini", ASSET_MATERIAL },
		{ "", "texture.ini", ASSET_TEXTURE },
		{ "", "skin.ini", ASSET_SKIN },
		{ "", "character.ini", ASSET_CHARACTER },
		{ "", "head.ini", ASSET_HEAD },
		{ "", "body.ini", ASSET_BODY },
		{ "", "arena.ini", ASSET_ARENA },
		{ "", "map.ini", ASSET_MAP },
		{ "", "scenario.ini", ASSET_SCENARIO },
		{ "", "prop.ini", ASSET_PROP },
		{ "", "vehicle.ini", ASSET_VEHICLE },
		{ "", "mission.ini", ASSET_MISSION },
		{ "", "gamemode.ini", ASSET_GAMEMODE },
		{ "", "botprofile.ini", ASSET_BOT_PROFILE },
		{ "", "effect.ini", ASSET_EFFECT },
		{ "", "hud.ini", ASSET_HUD },
		{ "", "sound.ini", ASSET_AUDIO },
		{ "", "sfx.ini", ASSET_AUDIO },
		{ "", "voice.ini", ASSET_AUDIO },
		{ "", "music.ini", ASSET_AUDIO },
		{ "", "ui.ini", ASSET_UI },
		{ "", "font.ini", ASSET_FONT },
		{ "", "lang.ini", ASSET_LANG },
		/* .pdai is the only public AI list unit; a loose ai.ini is not read
		 * (global-ai-lists.md, a new type with no loose mods to keep). */
		{ "", "animation.ini", ASSET_ANIMATION },
	};
	static const external_descriptor_spec_t specs[] = {
		{ "weapons", "weapon.ini", ASSET_WEAPON },
		{ "projectiles", "projectile.ini", ASSET_PROJECTILE },
		{ "entities", "entity.ini", ASSET_ENTITY },
		{ "materials", "material.ini", ASSET_MATERIAL },
		{ "textures", "texture.ini", ASSET_TEXTURE },
		{ "skins", "skin.ini", ASSET_SKIN },
		{ "characters", "character.ini", ASSET_CHARACTER },
		{ "characters/heads", "head.ini", ASSET_HEAD },
		{ "characters/bodies", "body.ini", ASSET_BODY },
		{ "maps", "arena.ini", ASSET_ARENA },
		{ "maps", "map.ini", ASSET_MAP },
		{ "scenarios", "scenario.ini", ASSET_SCENARIO },
		{ "props", "prop.ini", ASSET_PROP },
		{ "vehicles", "vehicle.ini", ASSET_VEHICLE },
		{ "missions", "mission.ini", ASSET_MISSION },
		{ "gamemodes", "gamemode.ini", ASSET_GAMEMODE },
		{ "botprofiles", "botprofile.ini", ASSET_BOT_PROFILE },
		{ "bot_profiles", "botprofile.ini", ASSET_BOT_PROFILE },
		{ "effects", "effect.ini", ASSET_EFFECT },
		{ "hud", "hud.ini", ASSET_HUD },
		/* .pdtheme is the only public theme unit; loose theme.ini is rejected. */
		{ "audio/sfx", "sound.ini", ASSET_AUDIO },
		{ "audio/sfx", "sfx.ini", ASSET_AUDIO },
		{ "audio/voice", "voice.ini", ASSET_AUDIO },
		{ "audio/music", "music.ini", ASSET_AUDIO },
		{ "ui", "ui.ini", ASSET_UI },
		{ "ui", "texture.ini", ASSET_UI },
		{ "fonts", "font.ini", ASSET_FONT },
		{ "lang", "lang.ini", ASSET_LANG },
		/* .pdai is the only public AI list unit; loose ai.ini is rejected. */
		{ "animations", "animation.ini", ASSET_ANIMATION },
		{ "animations/weapon", "animation.ini", ASSET_ANIMATION },
		{ "animations/character", "animation.ini", ASSET_ANIMATION },
	};
	external_scan_transaction_t transaction;
	loader_animation_source_batch_t *batch = NULL;
	s32 total = 0;
	s32 rejected = 0;

	if (!mod_id || !mod_id[0] || !mod_dir || !mod_dir[0]) return 0;
	if (!externalScanTransactionBegin(&transaction)) {
		sysLogPrintf(LOG_ERROR,
			"assetcatalog_scanner: could not snapshot folder admission for %s",
			mod_id);
		return -1;
	}

	batch = loaderAnimationSourceBatchCreate();
	if (!batch) { externalScanTransactionDestroy(&transaction); return -1; }

	/* Received PDCA candidates place a single editable descriptor directly at
	 * the component root. Route those through the same scanner transaction as
	 * installed folder layouts so restart recovery never needs a second loose-
	 * INI registrar. Loose theme.ini remains intentionally unsupported. */
	for (size_t i = 0; i < sizeof(root_specs) / sizeof(root_specs[0]); i++) {
		char descriptor[FS_MAXPATH];
		if (!assetPathJoinChecked(descriptor, sizeof(descriptor), mod_dir, "/",
				root_specs[i].leaf)) {
			rejected = 1;
			continue;
		}
		if (!isRegularFile(descriptor)) continue;
		externalScanAccumulate(registerComponentIniFile(mod_dir, descriptor,
			root_specs[i].expected, root_specs[i].leaf, mod_id, batch),
			&total, &rejected);
	}

	for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
		externalScanAccumulate(scanExternalDescriptorPath(mod_dir,
			specs[i].relative_dir, specs[i].leaf, specs[i].expected, mod_id, batch),
			&total, &rejected);
	}
	externalScanAccumulate(scanTypedPdDescriptorsRecurse(mod_dir, "", mod_id,
		defer_reloads, batch), &total, &rejected);

	if (!rejected && !loaderAnimationSourceBatchCommit(batch)) rejected = 1;
	loaderAnimationSourceBatchDestroy(batch);
	if (rejected) {
		s32 rollback_ok = externalScanTransactionRollback(&transaction);
		sysLogPrintf(rollback_ok ? LOG_WARNING : LOG_ERROR,
			"CATALOG.SCAN.TRANSACTION.ROLLBACK: folder=%s accepted=%d result=%s",
			mod_id, total, rollback_ok ? "restored" : "failed");
		externalScanTransactionDestroy(&transaction);
		if (!defer_reloads) (void)catalogReloadInvalidatedTypedAssets();
		return -(total + 1);
	}

	externalScanTransactionDestroy(&transaction);
	if (total > 0) {
		sysLogPrintf(LOG_NOTE,
			"assetcatalog_scanner: folder %s: %d external-layout/.pd* descriptors registered",
			mod_id, total);
	}
	return total;
}

s32 assetCatalogScanExternalLayoutFolder(const char *mod_id,
		const char *mod_dir)
{
	return assetCatalogScanExternalLayoutFolderInternal(mod_id, mod_dir, 0);
}

s32 assetCatalogScanExternalLayoutFolderDeferred(const char *mod_id,
		const char *mod_dir)
{
	return assetCatalogScanExternalLayoutFolderInternal(mod_id, mod_dir, 1);
}

#ifndef PD_SERVER
static s32 stringEndsWithNoCase(const char *s, const char *suffix)
{
	if (!s || !suffix) {
		return 0;
	}
	size_t n = strlen(s);
	size_t m = strlen(suffix);
	if (m > n) {
		return 0;
	}
	const char *tail = s + n - m;
	for (size_t i = 0; i < m; i++) {
		if (tolower((u8)tail[i]) != tolower((u8)suffix[i])) {
			return 0;
		}
	}
	return 1;
}

static const char *pathLeaf(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void pathDirname(const char *path, char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return;
	}
	out[0] = '\0';
	if (!path) {
		return;
	}
	const char *slash = strrchr(path, '/');
	if (!slash) {
		return;
	}
	size_t len = (size_t)(slash - path);
	if (len >= outsz) {
		len = outsz - 1;
	}
	memcpy(out, path, len);
	out[len] = '\0';
}

static void pathSegment(const char *path, s32 segment_index, char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return;
	}
	out[0] = '\0';
	if (!path || segment_index < 0) {
		return;
	}

	const char *p = path;
	for (s32 i = 0; i < segment_index; i++) {
		const char *slash = strchr(p, '/');
		if (!slash) {
			return;
		}
		p = slash + 1;
	}

	const char *end = strchr(p, '/');
	size_t len = end ? (size_t)(end - p) : strlen(p);
	if (len >= outsz) {
		len = outsz - 1;
	}
	memcpy(out, p, len);
	out[len] = '\0';
}

static s32 archiveIniIsDescriptor(const char *entry_name)
{
	if (!stringEndsWithNoCase(entry_name, ".ini")) {
		return 0;
	}

	char seg0[64];
	pathSegment(entry_name, 0, seg0, sizeof(seg0));

	/* Legacy folder-mod component layout inside .pdmod. */
	if (strcmp(seg0, "_components") == 0) {
		return 1;
	}

	const char *leaf = pathLeaf(entry_name);
	return strcmp(leaf, "weapon.ini") == 0
		|| strcmp(leaf, "projectile.ini") == 0
		|| strcmp(leaf, "entity.ini") == 0
		|| strcmp(leaf, "material.ini") == 0
		|| strcmp(leaf, "character.ini") == 0
		|| strcmp(leaf, "head.ini") == 0
		|| strcmp(leaf, "body.ini") == 0
		|| strcmp(leaf, "arena.ini") == 0
		|| strcmp(leaf, "map.ini") == 0
		|| strcmp(leaf, "scenario.ini") == 0
		|| strcmp(leaf, "skin.ini") == 0
		|| strcmp(leaf, "effect.ini") == 0
		|| strcmp(leaf, "prop.ini") == 0
		|| strcmp(leaf, "vehicle.ini") == 0
		|| strcmp(leaf, "mission.ini") == 0
		|| strcmp(leaf, "gamemode.ini") == 0
		|| strcmp(leaf, "botprofile.ini") == 0
		|| strcmp(leaf, "sound.ini") == 0
		|| strcmp(leaf, "sfx.ini") == 0
		|| strcmp(leaf, "voice.ini") == 0
		|| strcmp(leaf, "music.ini") == 0
		|| strcmp(leaf, "audio.ini") == 0
		|| strcmp(leaf, "ui.ini") == 0
		|| strcmp(leaf, "texture.ini") == 0
		|| strcmp(leaf, "font.ini") == 0
		|| strcmp(leaf, "lang.ini") == 0
		|| strcmp(leaf, "animation.ini") == 0
		|| strcmp(leaf, "hud.ini") == 0
		|| strcmp(leaf, "theme.ini") == 0
		|| strcmp(leaf, "ai.ini") == 0;
}

static s32 archiveEntryIsDescriptor(const char *entry_name)
{
	return archiveIniIsDescriptor(entry_name)
		|| typedPdContentTypeForPath(entry_name) != ASSET_NONE;
}

static asset_type_e archiveExpectedTypeForPath(const char *entry_name)
{
	char seg0[64];
	char seg1[64];
	const char *leaf = pathLeaf(entry_name);

	asset_type_e typed = typedPdContentTypeForPath(entry_name);
	if (typed != ASSET_NONE) {
		return typed;
	}

	pathSegment(entry_name, 0, seg0, sizeof(seg0));
	pathSegment(entry_name, 1, seg1, sizeof(seg1));

	if (strcmp(seg0, "_components") == 0) {
		char category[64];
		pathSegment(entry_name, 1, category, sizeof(category));
		return categoryToType(category);
	}

	if (strcmp(seg0, "weapons") == 0) return ASSET_WEAPON;
	if (strcmp(seg0, "projectiles") == 0) return ASSET_PROJECTILE;
	if (strcmp(seg0, "entities") == 0) return ASSET_ENTITY;
	if (strcmp(seg0, "materials") == 0) return ASSET_MATERIAL;
	if (strcmp(seg0, "textures") == 0) return ASSET_TEXTURE;
	if (strcmp(seg0, "skins") == 0) return ASSET_SKIN;
	if (strcmp(seg0, "animations") == 0) return ASSET_ANIMATION;
	if (strcmp(seg0, "audio") == 0) return ASSET_AUDIO;
	if (strcmp(seg0, "ui") == 0) return ASSET_UI;
	if (strcmp(seg0, "fonts") == 0) return ASSET_FONT;
	if (strcmp(seg0, "lang") == 0) return ASSET_LANG;
	if (strcmp(seg0, "scenarios") == 0) return ASSET_SCENARIO;
	if (strcmp(seg0, "props") == 0) return ASSET_PROP;
	if (strcmp(seg0, "vehicles") == 0) return ASSET_VEHICLE;
	if (strcmp(seg0, "missions") == 0) return ASSET_MISSION;
	if (strcmp(seg0, "gamemodes") == 0) return ASSET_GAMEMODE;
	if (strcmp(seg0, "botprofiles") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(seg0, "bot_profiles") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(seg0, "effects") == 0) return ASSET_EFFECT;
	if (strcmp(seg0, "hud") == 0) return ASSET_HUD;
	if (strcmp(seg0, "themes") == 0) return ASSET_THEME;
	if (strcmp(seg0, "ai") == 0) return ASSET_AI_LISTS;

	if (strcmp(seg0, "characters") == 0) {
		if (strcmp(seg1, "heads") == 0) return ASSET_HEAD;
		if (strcmp(seg1, "bodies") == 0) return ASSET_BODY;
		return ASSET_CHARACTER;
	}

	if (strcmp(seg0, "maps") == 0) {
		if (strcmp(leaf, "arena.ini") == 0) return ASSET_ARENA;
		return ASSET_MAP;
	}

	return ASSET_NONE;
}

static s32 qualifyArchiveIniPath(ini_section_t *ini, const char *key,
                                  const char *component_dir)
{
	if (!ini || !key || !component_dir || !component_dir[0]) {
		return 0;
	}

	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) {
			continue;
		}
		char full[sizeof(ini->pairs[i].value)];
		if (!assetPathQualifyFilesystemChecked(full, FS_MAXPATH, component_dir,
				ini->pairs[i].value) ||
				!assetPathCopyChecked(ini->pairs[i].value,
					sizeof(ini->pairs[i].value), full)) return 0;
	}
	return 1;
}

static s32 qualifyArchiveIniPaths(ini_section_t *ini, const char *component_dir)
{
	static const char *keys[] = {
		"bodyfile",
		"headfile",
		"body_archive",
		"head_archive",
		"portrait_file",
		"model_file",
		"model",
		"lo_model_file",
		"hand_model_file",
		"mesh_archive",
		"hand_archive",
		"prop_file",
		"behavior_graph",
		"graph",
		"primary_graph",
		"secondary_graph",
		"settings_file",
		"settings",
		"variables_file",
		"variables",
		"shared_context_file",
		"shared_context",
		"presentation_file",
		"primary_projectile_archive",
		"deployed_entity_archive",
		"fire_sound_archive",
		"idle_animation_archive",
		"reticle_archive",
		"nested_payloads",
		"scene_file",
		"scene",
		"runtime_source_file",
		"geometry_file",
		"geometry",
		"blender_scene_file",
		"blender_scene",
		"visual_scene_file",
		"visual_scene",
		"visual_material_file",
		"visual_materials_file",
		"material_file",
		"material",
		"material_archive",
		"collision_file",
		"collision_source_file",
		"collision_source",
		"portals_file",
		"pads_file",
		"spawns_file",
		"volumes_file",
		"objects_file",
		"setup_fields_file",
		"ai_lists_file",
		"setup_file",
		"rooms_file",
		"rooms",
		"props_file",
		"props",
		"objectives_file",
		"objectives",
		"navigation_file",
		"waypoints_file",
		"waygroups_file",
		"covers_file",
		"paths_file",
		"level_graph_file",
		"mission_graph_file",
		"visual_source_file",
		"music_file",
		"midi_file",
		"file_path",
		"subtitle_file",
		"locale_en_file",
		"locale_fr_file",
		"locale_de_file",
		"locale_it_file",
		"locale_es_file",
		"locale_ja_file",
		"effect_file",
		"timeline_file",
		"behavior_graph",
		"graph",
		"theme_file",
		"rules_file",
		"profile_file",
		"scenario_archive",
		"ui_archive",
		"audio_archive",
		"music_archive",
		"effect_archive",
		"animation_file",
		"commands_file",
		"texture_file",
		"layout_file",
		"nineslice_file",
		"swatches_file",
		"texture_archive",
		"texture",
		"texture_manifest_file",
		"font_file",
		"glyphs_file",
		"physics_file",
		"font_archive",
		"font",
		"strings_file",
		"strings",
		NULL
	};

	(void)keys;
	for (size_t i = 0; i < ASSET_PATH_SOURCE_KEY_COUNT; i++) {
		if (!qualifyArchiveIniPath(ini, g_AssetPathSourceKeys[i], component_dir))
			return 0;
	}
	return 1;
}
#endif

/* ========================================================================
 * Public API
 * ======================================================================== */

s32 assetCatalogScanComponents(const char *modsdir)
{
	if (!modsdir || !modsdir[0]) {
		sysLogPrintf(LOG_ERROR, "assetcatalog_scanner: NULL or empty mods directory");
		return -1;
	}

	DIR *dp = opendir(modsdir);
	if (!dp) {
		sysLogPrintf(LOG_WARNING, "assetcatalog_scanner: cannot open mods directory '%s'",
			modsdir);
		return -1;
	}

	s32 total = 0, rejected = 0;
	struct dirent *ent;
	char pathbuf[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		/* only scan mod_* directories */
		if (strncmp(ent->d_name, "mod_", 4) != 0) {
			continue;
		}

		if (!assetPathJoinChecked(pathbuf, sizeof(pathbuf), modsdir, "/",
				ent->d_name)) {
			rejected = 1;
			sysLogPrintf(LOG_WARNING, "assetcatalog_scanner: component path is too long: '%s'", ent->d_name);
			continue;
		}

		if (!isDirectory(pathbuf)) {
			continue;
		}

		s32 n = scanModDir(pathbuf, ent->d_name);
		if (n > 0) {
			sysLogPrintf(LOG_NOTE, "assetcatalog_scanner: %s: %d total components",
				ent->d_name, n);
		}
		if (n < 0) { rejected = 1; total += -n - 1; }
		else total += n;
	}

	closedir(dp);
	sysLogPrintf(LOG_NOTE, "assetcatalog_scanner: scan complete, %d mod components registered",
		total);
	return rejected ? -(total + 1) : total;
}

s32 assetCatalogScanComponentsFromArchive(const char *mod_id, mod_archive_t *archive)
{
#ifdef PD_SERVER
	(void)mod_id;
	(void)archive;
	return 0;
#else
	if (!mod_id || !mod_id[0] || !archive) {
		return 0;
	}

    external_scan_transaction_t transaction = {0};
    loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
    if (!batch || !externalScanTransactionBegin(&transaction)) {
        loaderAnimationSourceBatchDestroy(batch); return -1;
    }
    s32 rejected = 0;
	s32 total = 0;
	s32 entries = modArchiveGetEntryCount(archive);

	for (s32 i = 0; i < entries; i++) {
		const char *entry_name = modArchiveGetEntryName(archive, i);
		if (!entry_name || !archiveEntryIsDescriptor(entry_name)) {
			continue;
		}
		const char *archive_path = modArchiveGetPath(archive);
		char entry_ref[FS_MAXPATH * 2 + 4];
		if (archive_path && archive_path[0]) {
			if (!assetPathJoinChecked(entry_ref, FS_MAXPATH, archive_path, "::",
					entry_name)) { rejected = 1; continue; }
		} else if (!assetPathCopyChecked(entry_ref, FS_MAXPATH, entry_name)) {
			{ rejected = 1; continue; }
		}

		char component_dir[FS_MAXPATH];
		if (typedPdContentTypeForPath(entry_name) != ASSET_NONE) {
			if (!typedPdDescriptorComponentDir(entry_name, component_dir,
					sizeof(component_dir))) { rejected = 1; continue; }
		} else {
			pathDirname(entry_name, component_dir, sizeof(component_dir));
		}
		if (!component_dir[0]) {
			{ rejected = 1; continue; }
		}

		u32 ini_size = 0;
		char *ini_bytes = (char *)modArchiveExtractAlloc(archive, i, &ini_size);
		if (!ini_bytes) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: could not read archive INI '%s' for '%s'",
				entry_name, mod_id);
			{ rejected = 1; continue; }
		}

		ini_section_t ini;
		s32 typed_archive_entry = typedPdContentTypeForPath(entry_name) != ASSET_NONE;
		s32 typed_archive_sources_qualified = 0;
		s32 parsed = iniParseBuffer(entry_name, ini_bytes, ini_size, &ini);
		if (!parsed && typed_archive_entry) {
			const char *descriptor_leaf = typedPdArchiveDescriptorLeaf(entry_name);
			if (descriptor_leaf) {
				u32 nested_size = 0;
				const char *found_descriptor = NULL;
				char *nested_ini = assetArchiveExtractDescriptorMemAlloc(
					ini_bytes, ini_size, entry_name,
					ASSET_ARCHIVE_VALIDATE_MIGRATION,
					&nested_size, &found_descriptor);
				if (nested_ini) {
					parsed = iniParseBuffer(found_descriptor ? found_descriptor : descriptor_leaf,
						nested_ini, nested_size, &ini);
					free(nested_ini);
					if (parsed) {
						if (!qualifyTypedArchiveSourcePaths(&ini, entry_ref)) {
							sysLogPrintf(LOG_WARNING,
								"ASSET.PATH.REJECT: nested archive chain exceeds repository capacity: %s",
								entry_ref);
							free(ini_bytes);
							{ rejected = 1; continue; }
						}
						typed_archive_sources_qualified = 1;
					}
				}
			}
		}
		if (!parsed) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: invalid archive descriptor '%s' for '%s'",
				entry_name, mod_id);
			free(ini_bytes);
			{ rejected = 1; continue; }
		}
		free(ini_bytes);

		asset_type_e expected = archiveExpectedTypeForPath(entry_name);
		asset_type_e ini_type = sectionToType(ini.type);
		if (expected != ASSET_NONE && ini_type != expected) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: archive type mismatch in '%s': "
				"expected %d, got [%s]=%d",
				entry_name, expected, ini.type, ini_type);
			rejected = 1;
			continue;
		}

		if (!typed_archive_sources_qualified) {
			if (!qualifyArchiveIniPaths(&ini, component_dir)
					|| !qualifyTypedArchiveSourcePaths(&ini, archive_path)) { rejected = 1; continue; }
		}

		char descriptor_ref[FS_MAXPATH * 2 + 132];
		if (typed_archive_entry) {
			const char *descriptor_leaf =
				assetArchiveDescriptorForPath(entry_name);
			if (!assetPathJoinChecked(descriptor_ref, FS_MAXPATH, entry_ref, "::",
					descriptor_leaf ? descriptor_leaf : "")) { rejected = 1; continue; }
		} else {
			if (!assetPathCopyChecked(descriptor_ref, FS_MAXPATH, entry_ref)) { rejected = 1; continue; }
		}

		asset_entry_t prior_effect;
		asset_entry_t prior_weapon;
		s32 had_prior_effect = 0;
		s32 had_prior_weapon = 0;
		const char *effect_id = NULL;
		const char *weapon_id = NULL;
		if (typed_archive_entry && ini_type == ASSET_EFFECT) {
			effect_id = iniGet(&ini, "catalog_id", iniGet(&ini, "id", ""));
			const asset_entry_t *prior = effect_id[0]
				? assetCatalogResolve(effect_id) : NULL;
			if (prior) {
				prior_effect = *prior;
				had_prior_effect = 1;
			}
		}
		if (typed_archive_entry && ini_type == ASSET_WEAPON) {
			weapon_id = iniGet(&ini, "catalog_id", iniGet(&ini, "id", ""));
			const asset_entry_t *prior = weapon_id[0]
				? assetCatalogResolve(weapon_id) : NULL;
			if (prior) {
				prior_weapon = *prior;
				had_prior_weapon = 1;
			}
		}

		if (registerComponentWithAnimationBatch(&ini, component_dir, mod_id, descriptor_ref, batch)) {
			s32 accepted = 1;
			if (typed_archive_entry && ini_type == ASSET_WEAPON) {
				char nested_err[256];
				nested_err[0] = '\0';
				if (!weapon_id || !weapon_id[0]
						|| registerWeaponNestedDependenciesWithAnimationBatch(weapon_id,
							entry_ref, 0, nested_err, sizeof(nested_err), batch, NULL) < 0) {
					asset_entry_t *weapon = weapon_id && weapon_id[0]
						? assetCatalogGetMutable(weapon_id) : NULL;
					if (had_prior_weapon && weapon) catalogActivationLedgerRestoreRetiredSnapshot(weapon, &prior_weapon);
					else if (!had_prior_weapon && weapon) assetCatalogUnregister(weapon_id);
					/* Defer restoration reload until the entire archive rolls back. */
					sysLogPrintf(LOG_WARNING,
						"PDWEAPON.NESTED.REJECT: owner=%s source=%s error=%s",
						weapon_id && weapon_id[0] ? weapon_id : "<missing>",
						entry_ref, nested_err[0] ? nested_err :
							"nested registration failed");
					accepted = 0;
				}
			}
			if (typed_archive_entry && ini_type == ASSET_EFFECT) {
				char nested_err[256];
				nested_err[0] = '\0';
				if (!effect_id || !effect_id[0]
						|| assetCatalogRegisterEffectNestedDependencies(effect_id,
							entry_ref, 0, nested_err, sizeof(nested_err)) < 0) {
					asset_entry_t *effect = effect_id && effect_id[0]
						? assetCatalogGetMutable(effect_id) : NULL;
					if (had_prior_effect && effect) {
						catalogActivationLedgerRestoreRetiredSnapshot(effect, &prior_effect);
					}
					else if (!had_prior_effect && effect) assetCatalogUnregister(effect_id);
					/* Defer restoration reload until the entire archive rolls back. */
					sysLogPrintf(LOG_WARNING,
						"PDEFFECT.NESTED.REJECT: owner=%s source=%s error=%s",
						effect_id && effect_id[0] ? effect_id : "<missing>",
						entry_ref, nested_err[0] ? nested_err :
							"nested registration failed");
					accepted = 0;
				}
			}
			if (typed_archive_entry && ini_type == ASSET_THEME) {
				const char *theme_id = iniGet(&ini, "catalog_id",
					iniGet(&ini, "id", ""));
				char nested_err[256];
				nested_err[0] = '\0';
				if (!theme_id[0]
						|| assetCatalogRegisterThemeNestedDependencies(theme_id,
							entry_ref, 0, nested_err,
							sizeof(nested_err)) < 0) {
					asset_entry_t *theme = theme_id[0]
						? assetCatalogGetMutable(theme_id) : NULL;
					if (theme) {
						theme->enabled = 0;
						theme->load_state = ASSET_STATE_REGISTERED;
					}
					sysLogPrintf(LOG_WARNING,
						"PDTHEME.NESTED.REJECT: owner=%s source=%s error=%s",
						theme_id[0] ? theme_id : "<missing>", entry_ref,
						nested_err[0] ? nested_err :
							"nested registration failed");
					accepted = 0;
				}
			}
			if (accepted && typed_archive_entry && ini_type == ASSET_VEHICLE) {
				const char *vehicle_id = iniGet(&ini, "catalog_id",
					iniGet(&ini, "id", ""));
				char model_err[256];
				model_err[0] = '\0';
				if (!vehicle_id[0]
						|| !registerVehicleModel(vehicle_id, model_err, sizeof(model_err))) {
					asset_entry_t *vehicle = vehicle_id[0]
						? assetCatalogGetMutable(vehicle_id) : NULL;
					if (vehicle) {
						vehicle->enabled = 0;
						vehicle->load_state = ASSET_STATE_REGISTERED;
					}
					sysLogPrintf(LOG_WARNING,
						"PDVEHICLE.MODEL.REJECT: owner=%s source=%s error=%s",
						vehicle_id[0] ? vehicle_id : "<missing>", entry_ref,
						model_err[0] ? model_err : "vehicle model admission failed");
					accepted = 0;
				}
			}
			if (accepted && typed_archive_entry && ini_type == ASSET_ARENA) {
				const char *arena_id = iniGet(&ini, "catalog_id",
					iniGet(&ini, "id", ""));
				char scenario_err[256];
				scenario_err[0] = '\0';
				if (!arena_id[0]
						|| registerArenaEmbeddedScenario(arena_id, mod_id, batch,
							scenario_err, sizeof(scenario_err)) < 0) {
					asset_entry_t *arena = arena_id[0]
						? assetCatalogGetMutable(arena_id) : NULL;
					if (arena) {
						arena->enabled = 0;
						arena->load_state = ASSET_STATE_REGISTERED;
					}
					sysLogPrintf(LOG_WARNING,
						"PDARENA.SCENARIO.REJECT: owner=%s source=%s error=%s",
						arena_id[0] ? arena_id : "<missing>", entry_ref,
						scenario_err[0] ? scenario_err : "scenario registration failed");
					accepted = 0;
				}
			}
			if (accepted) total++;
			else rejected = 1;
		} else rejected = 1;
	}

	if (total > 0) {
		sysLogPrintf(LOG_NOTE,
			"assetcatalog_scanner: archive %s: %d component descriptors registered",
			mod_id, total);
	}
    if (!rejected && !loaderAnimationSourceBatchCommit(batch)) rejected = 1;
    if (rejected && !externalScanTransactionRollback(&transaction))
        sysLoudFailf("CATALOG.ARCHIVE.ROLLBACK", "could not restore complete archive import");
    externalScanTransactionDestroy(&transaction);
    loaderAnimationSourceBatchDestroy(batch);
    return rejected ? -(total + 1) : total;
#endif
}

/* ========================================================================
 * D3R-8: Flat Bot Variant Scanner
 * ======================================================================== */

/**
 * Scan the flat bot_variants/ directory directly under modsdir.
 *
 * Unlike assetCatalogScanComponents(), bot variants created by the in-game
 * customizer are stored directly at:
 *   {modsdir}/bot_variants/{component_name}/bot.ini
 *
 * This function is called at startup after assetCatalogScanComponents() so
 * variants saved in a previous session are available immediately.
 * New variants saved this session are hot-registered via botVariantSave().
 *
 * @param modsdir  Path to the mods directory (e.g., "mods/")
 * @return Registered count, optional absence 0, or -(admitted + 1) on failure.
 */
s32 assetCatalogScanBotVariants(const char *modsdir)
{
	if (!modsdir || !modsdir[0]) {
		return -1;
	}

	char bot_variants_dir[FS_MAXPATH];
	if (!assetPathJoinChecked(bot_variants_dir, sizeof(bot_variants_dir), modsdir,
			"/", "bot_variants")) return -1;

	DIR *dp = opendir(bot_variants_dir);
	if (!dp) {
		/* Absence is optional; unreadable/non-directory paths are failures. */
		if (errno == ENOENT) return 0;
		sysLogPrintf(LOG_WARNING, "assetcatalog_scanner: cannot open bot variants '%s'", bot_variants_dir);
		return -1;
	}

	s32 count = 0, rejected = 0;
	struct dirent *ent;
	char pathbuf[FS_MAXPATH];

	for (;;) {
		errno = 0;
		ent = readdir(dp);
		if (!ent) {
			if (errno) rejected = 1;
			break;
		}
		if (ent->d_name[0] == '.') {
			continue;
		}

		if (!assetPathJoinChecked(pathbuf, sizeof(pathbuf), bot_variants_dir, "/",
				ent->d_name)) {
			rejected = 1;
			continue;
		}

		struct stat child;
		if (stat(pathbuf, &child) != 0) {
			rejected = 1;
			continue;
		}
		if (!S_ISDIR(child.st_mode)) {
			continue;
		}

		ini_section_t ini;
		char descriptor_path[FS_MAXPATH];
		if (!findAndParseIni(pathbuf, &ini,
				descriptor_path, sizeof(descriptor_path))) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: no valid .ini in bot_variants/%s",
				ent->d_name);
			rejected = 1;
			continue;
		}

		/* Use "custom" as the mod_id for user-created variants */
		if (registerComponent(&ini, pathbuf, "custom", descriptor_path)) {
			count++;
		} else rejected = 1;
	}

	if (closedir(dp) != 0) rejected = 1;

	if (count > 0) {
		sysLogPrintf(LOG_NOTE,
			"assetcatalog_scanner: bot_variants: %d variant(s) registered", count);
	}
	/* Preserve admitted-count information without claiming atomic rollback. */
	return rejected ? -(count + 1) : count;
}
