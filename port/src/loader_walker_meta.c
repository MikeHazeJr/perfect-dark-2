/**
 * loader_walker_meta.c -- c3844 public-source walker for typed metadata archives.
 *
 * The original universal walker covered the first 13 archive families. Later
 * families are still public typed archives and need the same boot-time catalog
 * source binding so source-only verification proves their real archive members
 * feed runtime loading.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "system.h"

typedef struct {
	asset_type_e type;
	const char *kind;
	const char *subdir;
	const char *ext;
	const char *fallback_member;
	const char *primary_keys[4];
} meta_walker_desc_t;

static s32 s_manifestStr(const char *manifest, size_t manifest_len,
	const char *key, char *out, size_t out_n)
{
	if (!key || !key[0]) {
		return 0;
	}
	return loaderWalkerEnvelopeStrCopy(manifest, manifest_len, key, out, out_n);
}

static s32 s_firstManifestMember(const char *manifest, size_t manifest_len,
	const meta_walker_desc_t *meta, char *member, size_t member_n)
{
	for (s32 i = 0; i < 4 && meta->primary_keys[i]; i++) {
		if (s_manifestStr(manifest, manifest_len, meta->primary_keys[i],
				member, member_n) && member[0]) {
			return 1;
		}
	}

	if (meta->fallback_member && meta->fallback_member[0]) {
		strncpy(member, meta->fallback_member, member_n - 1);
		member[member_n - 1] = '\0';
		return 1;
	}

	member[0] = '\0';
	return 0;
}

static asset_entry_t *s_getOrRegisterMetaEntry(const char *id, asset_type_e type)
{
	asset_entry_t *entry = assetCatalogGetMutable(id);
	if (entry) {
		if (entry->type != type) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.META: '%s' type mismatch existing=%d expected=%d",
				id, entry->type, type);
			return NULL;
		}
		return entry;
	}

	return assetCatalogRegister(id, type);
}

static void s_applyTypeFields(asset_entry_t *entry,
	const char *manifest, size_t manifest_len,
	const char *source_path)
{
	char value[FS_MAXPATH + 1];

	switch (entry->type) {
	case ASSET_CHARACTER:
		if (s_manifestStr(manifest, manifest_len, "body_archive",
				value, sizeof(value)) && value[0]) {
			strncpy(entry->ext.character.bodyfile, source_path,
				sizeof(entry->ext.character.bodyfile) - 1);
		}
		if (s_manifestStr(manifest, manifest_len, "head_archive",
				value, sizeof(value)) && value[0]) {
			strncpy(entry->ext.character.headfile, source_path,
				sizeof(entry->ext.character.headfile) - 1);
		}
		break;
	case ASSET_SKIN:
		if (s_manifestStr(manifest, manifest_len, "target",
				value, sizeof(value))) {
			strncpy(entry->ext.skin.target_id, value,
				sizeof(entry->ext.skin.target_id) - 1);
		}
		strncpy(entry->ext.skin.skin_file, source_path,
			sizeof(entry->ext.skin.skin_file) - 1);
		break;
	case ASSET_PROP:
		strncpy(entry->ext.prop.prop_file, source_path,
			sizeof(entry->ext.prop.prop_file) - 1);
		break;
	case ASSET_VEHICLE:
		strncpy(entry->ext.vehicle.physics_file, source_path,
			sizeof(entry->ext.vehicle.physics_file) - 1);
		break;
	case ASSET_MISSION:
		strncpy(entry->ext.mission.mission_graph_file, source_path,
			sizeof(entry->ext.mission.mission_graph_file) - 1);
		break;
	case ASSET_THEME:
		strncpy(entry->ext.theme.theme_file, source_path,
			sizeof(entry->ext.theme.theme_file) - 1);
		break;
	case ASSET_HUD:
		strncpy(entry->ext.hud.layout_file, source_path,
			sizeof(entry->ext.hud.layout_file) - 1);
		break;
	case ASSET_GAMEMODE:
		strncpy(entry->ext.gamemode.rules_file, source_path,
			sizeof(entry->ext.gamemode.rules_file) - 1);
		break;
	case ASSET_BOT_PROFILE:
		strncpy(entry->ext.bot_profile.profile_file, source_path,
			sizeof(entry->ext.bot_profile.profile_file) - 1);
		break;
	case ASSET_EFFECT:
		strncpy(entry->ext.effect.effect_file, source_path,
			sizeof(entry->ext.effect.effect_file) - 1);
		break;
	case ASSET_MATERIAL:
		strncpy(entry->ext.material.material_file, source_path,
			sizeof(entry->ext.material.material_file) - 1);
		break;
	default:
		break;
	}
}

static s32 s_registerMeta(const char *manifest, size_t manifest_len,
	const char *pd_kind, const char *id, const char *file_path,
	const meta_walker_desc_t *meta)
{
	(void)pd_kind;

	char member[FS_MAXPATH + 1];
	char source_path[FS_MAXPATH + 1];
	asset_entry_t *entry = s_getOrRegisterMetaEntry(id, meta->type);
	if (!entry) {
		return -1;
	}

	if (!s_firstManifestMember(manifest, manifest_len, meta,
			member, sizeof(member))) {
		return -1;
	}

	if (!loaderWalkerArchiveMemberPath(file_path, member,
			source_path, sizeof(source_path))) {
		return -1;
	}

	loaderWalkerMarkBaseArchiveEntry(entry);
	catalogSetPrimaryFile(entry, source_path);
	s_applyTypeFields(entry, manifest, manifest_len, source_path);
	return 1;
}

static const meta_walker_desc_t s_MetaFamilies[] = {
	{ ASSET_CHARACTER,   "character",  "characters",  ".pdcharacter",  "character.ini", { "body_archive", "head_archive", NULL, NULL } },
	{ ASSET_SKIN,        "skin",       "skins",       ".pdskin",       "skin.ini",      { "skin_file", "swatches_file", NULL, NULL } },
	{ ASSET_PROP,        "prop",       "props",       ".pdprop",       "prop.ini",      { "prop_file", NULL, NULL, NULL } },
	{ ASSET_VEHICLE,     "vehicle",    "vehicles",    ".pdvehicle",    "vehicle.ini",   { "physics_file", "behavior_graph", "model_file", NULL } },
	{ ASSET_MISSION,     "mission",    "missions",    ".pdmission",    "mission.ini",   { "mission_graph_file", "objectives_file", "briefing_file", "scenario_archive" } },
	{ ASSET_GAMEMODE,    "gamemode",   "gamemodes",   ".pdgamemode",   "rules.json",    { "rules_file", NULL, NULL, NULL } },
	{ ASSET_BOT_PROFILE, "botprofile", "botprofiles", ".pdbotprofile", "profile.json",  { "profile_file", NULL, NULL, NULL } },
	{ ASSET_HUD,         "hud",        "hud",         ".pdhud",        "layout.json",   { "layout_file", "texture_file", NULL, NULL } },
	{ ASSET_EFFECT,      "effect",     "effects",     ".pdeffect",     "effect.graph.json", { "effect_file", "behavior_graph", NULL, NULL } },
	{ ASSET_MATERIAL,    "material",   "materials",   ".pdmaterial",   "material.json", { "material_file", "file_path", "texture_archive", NULL } },
	{ ASSET_THEME,       "theme",      "themes",      ".pdtheme",      "theme.json",    { "theme_file", "ui_archive", "font_archive", NULL } },
};

static const meta_walker_desc_t *s_ActiveMeta;

static s32 s_registerActiveMeta(const char *manifest, size_t manifest_len,
	const char *pd_kind, const char *id, const char *file_path)
{
	if (!s_ActiveMeta) {
		return -1;
	}
	return s_registerMeta(manifest, manifest_len, pd_kind, id, file_path,
		s_ActiveMeta);
}

void loaderWalkerScanMetadataFamilies(const char *tier_dir,
	loader_walker_metadata_result_t *out)
{
	if (out) {
		memset(out, 0, sizeof(*out));
	}

	for (size_t i = 0; i < sizeof(s_MetaFamilies) / sizeof(s_MetaFamilies[0]); i++) {
		const meta_walker_desc_t *meta = &s_MetaFamilies[i];
		loader_walker_kind_desc_t desc;
		loader_walker_kind_result_t kr;

		memset(&desc, 0, sizeof(desc));
		desc.kind_str = meta->kind;
		desc.subdir = meta->subdir;
		desc.extension = meta->ext;
		desc.always_invoke = 1;

		s_ActiveMeta = meta;
		loaderWalkerScanKind(tier_dir, &desc, s_registerActiveMeta, &kr);
		s_ActiveMeta = NULL;

		if (out) {
			out->entries_scanned += kr.entries_scanned;
			out->entries_registered += kr.entries_registered;
			out->envelope_failures += kr.envelope_failures;
			out->register_failures += kr.register_failures;
		}
	}
}
