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
#include "assetcatalog.h"
#include "catalog_audio_source_identity.h"
#include "catalog_audio_public_source.h"
#include "loader_walker_common.h"
#include "lang_source.h"
#include "assetcatalog_load.h"
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
#include "assetcatalog_sound_slots.h" /* c3849 Wave 2 */
#include "assetcatalog_texture_slots.h" /* c3849 Wave 2 */
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
#include "modarchive.h"
#include "pdeffect_source.h"
#include "effect_dependencies.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"
#include "romdata.h"
#include "system.h"
#include "fs.h"

typedef struct external_scan_transaction {
	catalog_entry_snapshot_t *rows;
	s32 row_count;
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

static s32 externalScanTransactionBegin(
		external_scan_transaction_t *transaction)
{
	if (!transaction) return 0;
	memset(transaction, 0, sizeof(*transaction));
	transaction->row_count = assetCatalogGetPoolSize();
	if (transaction->row_count > 0) {
		transaction->rows = calloc((size_t)transaction->row_count,
			sizeof(*transaction->rows));
		if (!transaction->rows) goto fail;
		for (s32 i = 0; i < transaction->row_count; i++) {
			const asset_entry_t *row = assetCatalogGetByIndex(i);
			if (row) catalogEntrySnapshotCapture(&transaction->rows[i], row);
		}
	}
	if (!catalogDepSnapshotCreate(&transaction->deps)
			|| !fileProviderCheckpointCreate(&transaction->provider)
			|| !(transaction->weapon_slots =
				assetCatalogSnapshotCustomWeaponSlots())
			|| !(transaction->body_head_slots =
				assetCatalogSnapshotCustomBodyHeadSlots())
			|| !(transaction->model_slots = assetCatalogSnapshotCustomModelSlots())
			|| !(transaction->sound_slots =
				assetCatalogSnapshotCustomSoundSlots())
			|| !(transaction->texture_slots =
				assetCatalogSnapshotCustomTextureSlots())
			|| !(transaction->anim_slots =
				assetCatalogSnapshotCustomAnimSlots())
			|| !(transaction->stage_slots =
				assetCatalogSnapshotCustomStageSlots())
			|| !(transaction->stage_table = stageTableSnapshotCreate())
			|| !(transaction->loader_pool = loaderPoolSnapshotCreate())
			|| !(transaction->body_manager =
				catalogManagerBodySnapshotCreate())
			|| !(transaction->head_manager =
				catalogManagerHeadSnapshotCreate())) goto fail;
	transaction->active = 1;
	return 1;

fail:
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
	return 0;
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

static s32 externalScanTransactionRollback(
		external_scan_transaction_t *transaction)
{
	s32 ok = 1;
	if (!transaction || !transaction->active) return 0;

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

s32 iniParseBuffer(const char *label, const char *data, u32 len, ini_section_t *out)
{
	if (!data || !out) {
		return 0;
	}

	memset(out, 0, sizeof(*out));

	char *buf = (char *)malloc((size_t)len + 1);
	if (!buf) {
		sysLogPrintf(LOG_ERROR,
			"assetcatalog_scanner: out of memory parsing INI '%s'",
			label ? label : "(memory)");
		return 0;
	}
	memcpy(buf, data, len);
	buf[len] = '\0';

	s32 in_section = 0;
	char *line = buf;

	while (line && *line) {
		char *next = strpbrk(line, "\r\n");
		if (next) {
			char eol = *next;
			*next++ = '\0';
			if (eol == '\r' && *next == '\n') {
				next++;
			}
		}

		char *p = trimWhitespace(line);

		/* skip blank lines and comments */
		if (*p == '\0' || *p == '#' || *p == ';') {
			line = next;
			continue;
		}

		/* section header: [type] */
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) {
				*end = '\0';
				if (out->type[0] == '\0') {
					if (strlen(p + 1) >= sizeof(out->type)) {
						sysLogPrintf(LOG_ERROR,
							"assetcatalog_scanner: INI section name too long in '%s'",
							label ? label : "(memory)");
						free(buf);
						return 0;
					}
					strncpy(out->type, p + 1, sizeof(out->type) - 1);
				}
				in_section = 1;
			}
			line = next;
			continue;
		}

		/* key = value */
		if (in_section) {
			char *eq = strchr(p, '=');
			if (!eq) {
				continue;
			}

			*eq = '\0';
			char *key = trimWhitespace(p);
			char *val = trimWhitespace(eq + 1);

			if (out->count >= INI_MAX_PAIRS) {
				sysLogPrintf(LOG_ERROR,
					"assetcatalog_scanner: INI pair limit %d exceeded in '%s'",
					INI_MAX_PAIRS, label ? label : "(memory)");
				free(buf);
				return 0;
			}
			if (strlen(key) >= sizeof(out->pairs[0].key)
					|| strlen(val) >= sizeof(out->pairs[0].value)) {
				sysLogPrintf(LOG_ERROR,
					"assetcatalog_scanner: INI key/value too long in '%s' (key '%s')",
					label ? label : "(memory)", key);
				free(buf);
				return 0;
			}
			strncpy(out->pairs[out->count].key, key,
				sizeof(out->pairs[0].key) - 1);
			strncpy(out->pairs[out->count].value, val,
				sizeof(out->pairs[0].value) - 1);
			out->count++;
		}

		line = next;
	}

	free(buf);
	return (out->type[0] != '\0') ? 1 : 0;
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

static void registerDependencyList(const char *owner_id, const char *deps,
                                   s32 is_bundled)
{
	if (!owner_id || !owner_id[0] || !deps || !deps[0]) {
		return;
	}

	char buf[512];
	strncpy(buf, deps, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *tok = buf;
	while (tok) {
		char *next = strpbrk(tok, ",|");
		if (next) {
			*next++ = '\0';
		}

		char *dep = trimWhitespace(tok);
		if (dep[0]) {
			catalogDepRegister(owner_id, dep, is_bundled);
		}

		tok = next;
	}
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
    if (!externalScanTransactionBegin(&transaction)) goto done;
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
    if (is_head ? !loaderPoolInstallPublicHead(&head) : !loaderPoolInstallPublicBody(&body)) goto done;
    if (!catalogDepRegisterTyped(id, mesh.id, ASSET_MODEL, bundled) ||
            (hand.present && !catalogDepRegisterTyped(id, hand.id, ASSET_MODEL, bundled)) ||
            !bodyHeadRegisterDeclaredDependencies(id, deps.declared, bundled)) goto done;
    catalogDepPruneOwner(id, bodyHeadKeepDependency, &deps);
    if (deps.invalid) goto done;
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

s32 assetCatalogRegisterBodyHeadArchive(const char *archive_path, const char *expected_id,
    s32 is_head, s32 base_slot_hint, s32 bundled, char *error, size_t error_capacity)
{
    u32 size = 0, descriptor_size = 0;
    void *bytes = NULL;
    char *text = NULL;
    ini_section_t *ini = calloc(1, sizeof(*ini));
    char descriptor_path[FS_MAXPATH];
    s32 ok = 0;
    if (error && error_capacity) error[0] = 0;
    if (!archive_path || !archive_path[0] || !bodyHeadSourceCatalogIdValid(expected_id) ||
            assetArchiveTypeForPath(archive_path) != (is_head ? ASSET_HEAD : ASSET_BODY)) goto done;
    bytes = fsFileLoad(archive_path, &size);
    if (!bytes || !ini || assetArchiveValidateBytes(bytes, size, archive_path,
            ASSET_ARCHIVE_VALIDATE_RELEASE, error, error_capacity) != 0) goto done;
    text = modArchiveExtractMemAlloc(bytes, size, is_head ? "head.ini" : "body.ini", &descriptor_size);
    if (!bodyHeadSourceReadIniBytes(text, descriptor_size, is_head ? "head" : "body", ini,
            error, error_capacity) || !qualifyTypedArchiveSourcePaths(ini, archive_path) ||
            !assetPathJoinChecked(descriptor_path, sizeof(descriptor_path), archive_path,
                "::", is_head ? "head.ini" : "body.ini")) goto done;
    ok = registerBodyHeadPublicIni(ini, expected_id, archive_path, descriptor_path,
        bundled ? "base" : iniGet(ini, "category", "custom"), base_slot_hint, bundled, -1, NULL,
        error, error_capacity);
done:
    free(text);
    free(ini);
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
	e->runtime_index = -1;  /* assigned later during callsite migration */

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
		e->ext.arena.load_mode = iniGetInt(ini, "load_mode", ARENA_LOADMODE_PLAYABLE);
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
			if (pf[0]) {
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

	case ASSET_SCENARIO:
		e->ext.scenario.stagenum = s_mintCustomStagenum(e, e->id); /* c3849 */
		e->ext.scenario.mode = parseModeString(iniGet(ini, "mode", ""));
		e->ext.scenario.source_room_count = iniGetInt(ini,
			"source_room_count", 0);
		{
			const char *sf = iniGet(ini, "scene_file",
				iniGet(ini, "scene",
				iniGet(ini, "runtime_source_file",
				iniGet(ini, "blender_scene_file",
				iniGet(ini, "visual_scene_file", "")))));
			const char *cf = iniGet(ini, "collision_file",
				iniGet(ini, "collision_source_file",
				iniGet(ini, "collision_source", "")));
			const char *rf = iniGet(ini, "rooms_file",
				iniGet(ini, "rooms",
				iniGet(ini, "geometry_file",
				iniGet(ini, "geometry", ""))));
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
			if (sf[0]) {
				catalogSetPrimaryFile(e, sf);
			} else if (rf[0]) {
				catalogSetPrimaryFile(e, rf);
			}
		}
		break;

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
		char *err, size_t err_cap)
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
		if (!loaderWalkerMeshSourcePlanManifest((const char *)manifest,
				manifest_size, p->catalog_id, p->archive_ref,
				weaponNestedMeshPublicGeometry(&p->ini),
				existing ? existing->source_filenum : -1,
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
		const char *weapon_id, char *err, size_t err_cap)
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
				err, err_cap);
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
	if (weaponGraphArchiveReadDescriptorBytes(weapon_bytes, weapon_size, ASSET_WEAPON,
			&desc, err, err_cap) != 0) return 0;
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
	if (!weaponNestedSelectGraphModels(pending, count, edges, weapon_id,
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

static s32 registerWeaponNestedDependenciesWithAnimationBatch(const char *weapon_id,
		const char *weapon_archive, s32 bundled, char *err, size_t err_cap,
		loader_animation_source_batch_t *batch)
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
			entry_name, expected_type, err, err_cap);
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
			&pending_capacity, &mesh_edges, weapon_id, err, err_cap)
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

s32 assetCatalogRegisterWeaponNestedDependencies(const char *weapon_id,
        const char *weapon_archive, s32 bundled, char *err, size_t err_cap)
{
    char owner[CATALOG_ID_LEN];
    if (!weapon_id || !assetPathCopyChecked(owner, sizeof(owner), weapon_id)) return -1;
    external_scan_transaction_t transaction = {0};
    loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
    if (!batch || !externalScanTransactionBegin(&transaction)) {
        loaderAnimationSourceBatchDestroy(batch);
        return -1;
    }
    s32 result = registerWeaponNestedDependenciesWithAnimationBatch(owner,
        weapon_archive, bundled, err, err_cap, batch);
    if (result >= 0 && !loaderAnimationSourceBatchCommit(batch)) {
        nestedSetErr(err, err_cap, "weapon command closure rejected: %s%s", owner, "");
        result = -1;
    }
    if (result < 0 && !externalScanTransactionRollback(&transaction))
        sysLoudFailf("PDWEAPON.NESTED.ROLLBACK", "could not restore complete nested source transaction");
    externalScanTransactionDestroy(&transaction);
    loaderAnimationSourceBatchDestroy(batch);
    return result;
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
						sizeof(nested_err), batch) < 0) {
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
		|| strcmp(leaf, "theme.ini") == 0;
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
							entry_ref, 0, nested_err, sizeof(nested_err), batch) < 0) {
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
