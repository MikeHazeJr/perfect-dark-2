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
#include "constants.h"
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
	const char *primary_keys[6];
} meta_walker_desc_t;

static s32 s_manifestStr(const char *manifest, size_t manifest_len,
	const char *key, char *out, size_t out_n)
{
	if (!key || !key[0]) {
		return 0;
	}
	return loaderWalkerEnvelopeStrCopy(manifest, manifest_len, key, out, out_n);
}

static s32 s_manifestInt(const char *manifest, size_t manifest_len,
	const char *key, s64 *out)
{
	if (!key || !key[0]) {
		return 0;
	}
	return loaderWalkerEnvelopeInt(manifest, manifest_len, key, out);
}

static s32 s_namedInt(const char *value, s32 default_value,
	const char *const *keys, const s32 *values, s32 count)
{
	if (!value || !value[0]) {
		return default_value;
	}

	for (s32 i = 0; i < count; i++) {
		if (strcmp(value, keys[i]) == 0) {
			return values[i];
		}
	}

	return default_value;
}

static s32 s_manifestNamedInt(const char *manifest, size_t manifest_len,
	const char *str_key, const char *int_key, s32 default_value,
	const char *const *keys, const s32 *values, s32 count)
{
	char value[64];
	s64 parsed;

	if (s_manifestStr(manifest, manifest_len, str_key, value, sizeof(value))) {
		return s_namedInt(value, default_value, keys, values, count);
	}
	if (s_manifestInt(manifest, manifest_len, int_key, &parsed)) {
		return (s32)parsed;
	}
	return default_value;
}

static s32 s_manifestModeId(const char *manifest, size_t manifest_len)
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
	return s_manifestNamedInt(manifest, manifest_len, "mode_key", "mode_id",
		-1, keys, values, (s32)(sizeof(values) / sizeof(values[0])));
}

static s32 s_manifestBotType(const char *manifest, size_t manifest_len)
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
	return s_manifestNamedInt(manifest, manifest_len, "type_key", "type",
		BOTTYPE_GENERAL, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 s_manifestBotDifficulty(const char *manifest, size_t manifest_len)
{
	static const char *const keys[] = {
		"meat", "easy", "normal", "hard", "perfect", "dark",
	};
	static const s32 values[] = {
		BOTDIFF_MEAT, BOTDIFF_EASY, BOTDIFF_NORMAL, BOTDIFF_HARD,
		BOTDIFF_PERFECT, BOTDIFF_DARK,
	};
	return s_manifestNamedInt(manifest, manifest_len, "difficulty_key",
		"difficulty", BOTDIFF_NORMAL, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 s_firstManifestMember(const char *manifest, size_t manifest_len,
	const meta_walker_desc_t *meta, char *member, size_t member_n)
{
	for (s32 i = 0; i < 6 && meta->primary_keys[i]; i++) {
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

static s32 s_manifestMemberPath(const char *manifest, size_t manifest_len,
	const char *key, const char *archive_path, char *out, size_t out_n)
{
	char member[FS_MAXPATH + 1];

	if (!s_manifestStr(manifest, manifest_len, key, member, sizeof(member))
			|| !member[0]) {
		return 0;
	}

	return loaderWalkerArchiveMemberPath(archive_path, member, out, out_n);
}

static s32 s_copyManifestMemberPath(const char *manifest, size_t manifest_len,
	const char *key, const char *archive_path, char *out, size_t out_n)
{
	char path[FS_MAXPATH + 1];

	if (!out || out_n == 0) {
		return 0;
	}

	if (!s_manifestMemberPath(manifest, manifest_len, key,
			archive_path, path, sizeof(path))) {
		return 0;
	}

	strncpy(out, path, out_n - 1);
	out[out_n - 1] = '\0';
	return 1;
}

static void s_clearPath(char *out, size_t out_n)
{
	if (out && out_n > 0) {
		out[0] = '\0';
	}
}

static s32 s_missionPublicMemberPath(const char *ini, size_t ini_len,
	const char *archive_path, const char *key, char *out, size_t out_n)
{
	char member[FS_MAXPATH + 1];

	if (!loaderWalkerIniValueCopy(ini, ini_len, "mission", key,
			member, sizeof(member)) || !member[0]) {
		return 0;
	}
	return loaderWalkerArchiveMemberPath(archive_path, member, out, out_n);
}

static s32 s_applyMissionPublicDescriptor(asset_entry_t *entry,
	const char *archive_path)
{
	char *ini = NULL;
	size_t ini_len = 0;
	char catalog_id[CATALOG_ID_LEN];
	char obsolete[FS_MAXPATH + 1];
	char obsolete_path[FS_MAXPATH + 1];
	s32 ok = 0;

	if (!entry || !loaderWalkerArchiveTextMember(archive_path, "mission.ini",
			&ini, &ini_len)) {
		return 0;
	}

	catalog_id[0] = '\0';
	obsolete[0] = '\0';
	if (!loaderWalkerIniValueCopy(ini, ini_len, "mission", "catalog_id",
			catalog_id, sizeof(catalog_id)) || strcmp(catalog_id, entry->id) != 0) {
		goto done;
	}
	/* T-ASSETS-014: briefing records and their language tokens are already
	 * authoritative in the embedded public scenario setup.fields.json. Refuse
	 * the retired synthetic slot so a mission can never expose two authored
	 * briefing sources. */
	if (loaderWalkerIniValueCopy(ini, ini_len, "mission", "briefing_file",
			obsolete, sizeof(obsolete))) {
		goto done;
	}
	if (loaderWalkerArchiveMemberPath(archive_path, "briefing.json",
			obsolete_path, sizeof(obsolete_path)) && fsFileSize(obsolete_path) > 0) {
		goto done;
	}
	if (!s_missionPublicMemberPath(ini, ini_len, archive_path,
			"scenario_archive", entry->ext.mission.scenario_archive,
			sizeof(entry->ext.mission.scenario_archive)) ||
			!s_missionPublicMemberPath(ini, ini_len, archive_path,
			"objectives_file", entry->ext.mission.objectives_file,
			sizeof(entry->ext.mission.objectives_file)) ||
			!s_missionPublicMemberPath(ini, ini_len, archive_path,
			"mission_graph_file", entry->ext.mission.mission_graph_file,
			sizeof(entry->ext.mission.mission_graph_file)) ||
			fsFileSize(entry->ext.mission.scenario_archive) <= 0 ||
			fsFileSize(entry->ext.mission.objectives_file) <= 0 ||
			fsFileSize(entry->ext.mission.mission_graph_file) <= 0) {
		goto done;
	}
	ok = 1;

done:
	sysMemFree(ini);
	return ok;
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
	const char *source_path, const char *archive_path)
{
	char value[FS_MAXPATH + 1];
	s64 ivalue;

	switch (entry->type) {
	case ASSET_ARENA:
		s_clearPath(entry->ext.arena.scenario_id,
			sizeof(entry->ext.arena.scenario_id));
		s_clearPath(entry->ext.arena.scenario_archive,
			sizeof(entry->ext.arena.scenario_archive));
		if (s_manifestStr(manifest, manifest_len, "scenario",
				value, sizeof(value))) {
			strncpy(entry->ext.arena.scenario_id, value,
				sizeof(entry->ext.arena.scenario_id) - 1);
			entry->ext.arena.scenario_id[
				sizeof(entry->ext.arena.scenario_id) - 1] = '\0';
		}
		s_copyManifestMemberPath(manifest, manifest_len, "scenario_archive",
			archive_path, entry->ext.arena.scenario_archive,
			sizeof(entry->ext.arena.scenario_archive));
		break;
	case ASSET_CHARACTER:
		s_clearPath(entry->ext.character.body_id,
			sizeof(entry->ext.character.body_id));
		s_clearPath(entry->ext.character.head_id,
			sizeof(entry->ext.character.head_id));
		s_clearPath(entry->ext.character.bodyfile,
			sizeof(entry->ext.character.bodyfile));
		s_clearPath(entry->ext.character.headfile,
			sizeof(entry->ext.character.headfile));
		s_clearPath(entry->ext.character.portrait_file,
			sizeof(entry->ext.character.portrait_file));
		if (s_manifestStr(manifest, manifest_len, "body",
				value, sizeof(value))) {
			strncpy(entry->ext.character.body_id, value,
				sizeof(entry->ext.character.body_id) - 1);
		}
		if (s_manifestStr(manifest, manifest_len, "head",
				value, sizeof(value))) {
			strncpy(entry->ext.character.head_id, value,
				sizeof(entry->ext.character.head_id) - 1);
		}
		if (!s_copyManifestMemberPath(manifest, manifest_len, "body_archive",
				archive_path, entry->ext.character.bodyfile,
				sizeof(entry->ext.character.bodyfile))) {
			s_copyManifestMemberPath(manifest, manifest_len, "bodyfile",
				archive_path, entry->ext.character.bodyfile,
				sizeof(entry->ext.character.bodyfile));
		}
		if (!s_copyManifestMemberPath(manifest, manifest_len, "head_archive",
				archive_path, entry->ext.character.headfile,
				sizeof(entry->ext.character.headfile))) {
			s_copyManifestMemberPath(manifest, manifest_len, "headfile",
				archive_path, entry->ext.character.headfile,
				sizeof(entry->ext.character.headfile));
		}
		s_copyManifestMemberPath(manifest, manifest_len, "portrait_file",
			archive_path, entry->ext.character.portrait_file,
			sizeof(entry->ext.character.portrait_file));
		break;
	case ASSET_BODY:
		s_clearPath(entry->ext.body.mesh_archive,
			sizeof(entry->ext.body.mesh_archive));
		s_clearPath(entry->ext.body.hand_archive,
			sizeof(entry->ext.body.hand_archive));
		s_copyManifestMemberPath(manifest, manifest_len, "mesh_archive",
			archive_path, entry->ext.body.mesh_archive,
			sizeof(entry->ext.body.mesh_archive));
		s_copyManifestMemberPath(manifest, manifest_len, "hand_archive",
			archive_path, entry->ext.body.hand_archive,
			sizeof(entry->ext.body.hand_archive));
		break;
	case ASSET_HEAD:
		s_clearPath(entry->ext.head.mesh_archive,
			sizeof(entry->ext.head.mesh_archive));
		s_copyManifestMemberPath(manifest, manifest_len, "mesh_archive",
			archive_path, entry->ext.head.mesh_archive,
			sizeof(entry->ext.head.mesh_archive));
		break;
	case ASSET_SKIN:
		if (s_manifestStr(manifest, manifest_len, "target",
				value, sizeof(value))) {
			strncpy(entry->ext.skin.target_id, value,
				sizeof(entry->ext.skin.target_id) - 1);
		}
		s_clearPath(entry->ext.skin.skin_file,
			sizeof(entry->ext.skin.skin_file));
		s_clearPath(entry->ext.skin.texture_file,
			sizeof(entry->ext.skin.texture_file));
		s_clearPath(entry->ext.skin.swatches_file,
			sizeof(entry->ext.skin.swatches_file));
		s_clearPath(entry->ext.skin.material_archive,
			sizeof(entry->ext.skin.material_archive));
		s_clearPath(entry->ext.skin.texture_archive,
			sizeof(entry->ext.skin.texture_archive));
		if (!s_copyManifestMemberPath(manifest, manifest_len, "skin_file",
				archive_path, entry->ext.skin.skin_file,
				sizeof(entry->ext.skin.skin_file))) {
			strncpy(entry->ext.skin.skin_file, source_path,
				sizeof(entry->ext.skin.skin_file) - 1);
			entry->ext.skin.skin_file[
				sizeof(entry->ext.skin.skin_file) - 1] = '\0';
		}
		s_copyManifestMemberPath(manifest, manifest_len, "texture_file",
			archive_path, entry->ext.skin.texture_file,
			sizeof(entry->ext.skin.texture_file));
		s_copyManifestMemberPath(manifest, manifest_len, "swatches_file",
			archive_path, entry->ext.skin.swatches_file,
			sizeof(entry->ext.skin.swatches_file));
		s_copyManifestMemberPath(manifest, manifest_len, "material_archive",
			archive_path, entry->ext.skin.material_archive,
			sizeof(entry->ext.skin.material_archive));
		s_copyManifestMemberPath(manifest, manifest_len, "texture_archive",
			archive_path, entry->ext.skin.texture_archive,
			sizeof(entry->ext.skin.texture_archive));
		break;
	case ASSET_PROP:
		s_clearPath(entry->ext.prop.prop_file,
			sizeof(entry->ext.prop.prop_file));
		s_clearPath(entry->ext.prop.model_file,
			sizeof(entry->ext.prop.model_file));
		s_clearPath(entry->ext.prop.behavior_graph,
			sizeof(entry->ext.prop.behavior_graph));
		if (!s_copyManifestMemberPath(manifest, manifest_len, "prop_file",
				archive_path, entry->ext.prop.prop_file,
				sizeof(entry->ext.prop.prop_file))) {
			strncpy(entry->ext.prop.prop_file, source_path,
				sizeof(entry->ext.prop.prop_file) - 1);
			entry->ext.prop.prop_file[
				sizeof(entry->ext.prop.prop_file) - 1] = '\0';
		}
		s_copyManifestMemberPath(manifest, manifest_len, "model_file",
			archive_path, entry->ext.prop.model_file,
			sizeof(entry->ext.prop.model_file));
		if (!s_copyManifestMemberPath(manifest, manifest_len,
				"behavior_graph", archive_path,
				entry->ext.prop.behavior_graph,
				sizeof(entry->ext.prop.behavior_graph))) {
			s_copyManifestMemberPath(manifest, manifest_len, "graph",
				archive_path, entry->ext.prop.behavior_graph,
				sizeof(entry->ext.prop.behavior_graph));
		}
		break;
	case ASSET_VEHICLE:
		s_clearPath(entry->ext.vehicle.model_file,
			sizeof(entry->ext.vehicle.model_file));
		s_clearPath(entry->ext.vehicle.physics_file,
			sizeof(entry->ext.vehicle.physics_file));
		s_clearPath(entry->ext.vehicle.behavior_graph,
			sizeof(entry->ext.vehicle.behavior_graph));
		s_copyManifestMemberPath(manifest, manifest_len, "model_file",
			archive_path, entry->ext.vehicle.model_file,
			sizeof(entry->ext.vehicle.model_file));
		s_copyManifestMemberPath(manifest, manifest_len, "physics_file",
			archive_path, entry->ext.vehicle.physics_file,
			sizeof(entry->ext.vehicle.physics_file));
		s_copyManifestMemberPath(manifest, manifest_len, "behavior_graph",
			archive_path, entry->ext.vehicle.behavior_graph,
			sizeof(entry->ext.vehicle.behavior_graph));
		break;
	case ASSET_MISSION:
		s_clearPath(entry->ext.mission.scenario_archive,
			sizeof(entry->ext.mission.scenario_archive));
		s_clearPath(entry->ext.mission.objectives_file,
			sizeof(entry->ext.mission.objectives_file));
		s_clearPath(entry->ext.mission.mission_graph_file,
			sizeof(entry->ext.mission.mission_graph_file));
		break;
	case ASSET_THEME:
		s_clearPath(entry->ext.theme.theme_file,
			sizeof(entry->ext.theme.theme_file));
		s_clearPath(entry->ext.theme.ui_archive,
			sizeof(entry->ext.theme.ui_archive));
		s_clearPath(entry->ext.theme.font_archive,
			sizeof(entry->ext.theme.font_archive));
		s_clearPath(entry->ext.theme.audio_archive,
			sizeof(entry->ext.theme.audio_archive));
		s_clearPath(entry->ext.theme.music_archive,
			sizeof(entry->ext.theme.music_archive));
		s_clearPath(entry->ext.theme.effect_archive,
			sizeof(entry->ext.theme.effect_archive));
		/* T-ASSETS-026: _meta/manifest.json is compatibility/provenance only.
		 * The public descriptor fixes theme.json as the authored source. Nested
		 * dependency ownership is parsed from theme.ini by T-ASSETS-027; never
		 * let private manifest fields replace public theme authority here. */
		loaderWalkerArchiveMemberPath(archive_path, "theme.json",
			entry->ext.theme.theme_file,
			sizeof(entry->ext.theme.theme_file));
		break;
	case ASSET_HUD:
		s_clearPath(entry->ext.hud.texture_file,
			sizeof(entry->ext.hud.texture_file));
		s_clearPath(entry->ext.hud.layout_file,
			sizeof(entry->ext.hud.layout_file));
		s_copyManifestMemberPath(manifest, manifest_len, "texture_file",
			archive_path, entry->ext.hud.texture_file,
			sizeof(entry->ext.hud.texture_file));
		if (!s_copyManifestMemberPath(manifest, manifest_len, "layout_file",
				archive_path, entry->ext.hud.layout_file,
				sizeof(entry->ext.hud.layout_file))) {
			strncpy(entry->ext.hud.layout_file, source_path,
				sizeof(entry->ext.hud.layout_file) - 1);
			entry->ext.hud.layout_file[
				sizeof(entry->ext.hud.layout_file) - 1] = '\0';
		}
		break;
	case ASSET_GAMEMODE:
		entry->ext.gamemode.mode_id = s_manifestModeId(manifest,
			manifest_len);
		s_clearPath(entry->ext.gamemode.name,
			sizeof(entry->ext.gamemode.name));
		s_clearPath(entry->ext.gamemode.description,
			sizeof(entry->ext.gamemode.description));
		if (s_manifestStr(manifest, manifest_len, "name",
				entry->ext.gamemode.name,
				sizeof(entry->ext.gamemode.name))) {
			entry->ext.gamemode.name[
				sizeof(entry->ext.gamemode.name) - 1] = '\0';
		}
		if (s_manifestStr(manifest, manifest_len, "description",
				entry->ext.gamemode.description,
				sizeof(entry->ext.gamemode.description))) {
			entry->ext.gamemode.description[
				sizeof(entry->ext.gamemode.description) - 1] = '\0';
		}
		entry->ext.gamemode.min_players = 2;
		entry->ext.gamemode.max_players = 8;
		entry->ext.gamemode.team_based = 0;
		entry->ext.gamemode.requirefeature = 0;
		if (s_manifestInt(manifest, manifest_len, "min_players", &ivalue)) {
			entry->ext.gamemode.min_players = (s32)ivalue;
		}
		if (s_manifestInt(manifest, manifest_len, "max_players", &ivalue)) {
			entry->ext.gamemode.max_players = (s32)ivalue;
		}
		if (s_manifestInt(manifest, manifest_len, "team_based", &ivalue)) {
			entry->ext.gamemode.team_based = (s32)ivalue;
		}
		if (s_manifestInt(manifest, manifest_len, "requirefeature", &ivalue)) {
			entry->ext.gamemode.requirefeature = (u8)ivalue;
		}
		s_clearPath(entry->ext.gamemode.rules_file,
			sizeof(entry->ext.gamemode.rules_file));
		if (!s_copyManifestMemberPath(manifest, manifest_len, "rules_file",
				archive_path, entry->ext.gamemode.rules_file,
				sizeof(entry->ext.gamemode.rules_file))) {
			strncpy(entry->ext.gamemode.rules_file, source_path,
				sizeof(entry->ext.gamemode.rules_file) - 1);
			entry->ext.gamemode.rules_file[
				sizeof(entry->ext.gamemode.rules_file) - 1] = '\0';
		}
		break;
	case ASSET_BOT_PROFILE:
		entry->ext.bot_profile.type = s_manifestBotType(manifest,
			manifest_len);
		entry->ext.bot_profile.difficulty = s_manifestBotDifficulty(manifest,
			manifest_len);
		entry->ext.bot_profile.body = -1;
		entry->ext.bot_profile.name_langid = 0;
		entry->ext.bot_profile.requirefeature = 0;
		if (s_manifestInt(manifest, manifest_len, "body", &ivalue)) {
			entry->ext.bot_profile.body = (s16)ivalue;
		}
		if (s_manifestInt(manifest, manifest_len, "name_langid", &ivalue)) {
			entry->ext.bot_profile.name_langid = (s16)ivalue;
		}
		if (s_manifestInt(manifest, manifest_len, "requirefeature", &ivalue)) {
			entry->ext.bot_profile.requirefeature = (u8)ivalue;
		}
		s_clearPath(entry->ext.bot_profile.target_body,
			sizeof(entry->ext.bot_profile.target_body));
		if (s_manifestStr(manifest, manifest_len, "target_body",
				value, sizeof(value))) {
			strncpy(entry->ext.bot_profile.target_body, value,
				sizeof(entry->ext.bot_profile.target_body) - 1);
			entry->ext.bot_profile.target_body[
				sizeof(entry->ext.bot_profile.target_body) - 1] = '\0';
		}
		s_clearPath(entry->ext.bot_profile.profile_file,
			sizeof(entry->ext.bot_profile.profile_file));
		if (!s_copyManifestMemberPath(manifest, manifest_len, "profile_file",
				archive_path, entry->ext.bot_profile.profile_file,
				sizeof(entry->ext.bot_profile.profile_file))) {
			strncpy(entry->ext.bot_profile.profile_file, source_path,
				sizeof(entry->ext.bot_profile.profile_file) - 1);
			entry->ext.bot_profile.profile_file[
				sizeof(entry->ext.bot_profile.profile_file) - 1] = '\0';
		}
		break;
	case ASSET_EFFECT:
		s_clearPath(entry->ext.effect.effect_file,
			sizeof(entry->ext.effect.effect_file));
		s_clearPath(entry->ext.effect.timeline_file,
			sizeof(entry->ext.effect.timeline_file));
		if (!s_copyManifestMemberPath(manifest, manifest_len, "effect_file",
				archive_path, entry->ext.effect.effect_file,
				sizeof(entry->ext.effect.effect_file))
				&& !s_copyManifestMemberPath(manifest, manifest_len,
				"behavior_graph", archive_path,
				entry->ext.effect.effect_file,
				sizeof(entry->ext.effect.effect_file))) {
			strncpy(entry->ext.effect.effect_file, source_path,
				sizeof(entry->ext.effect.effect_file) - 1);
			entry->ext.effect.effect_file[
				sizeof(entry->ext.effect.effect_file) - 1] = '\0';
		}
		if (!s_copyManifestMemberPath(manifest, manifest_len,
				"timeline_file", archive_path,
				entry->ext.effect.timeline_file,
				sizeof(entry->ext.effect.timeline_file))) {
			s_copyManifestMemberPath(manifest, manifest_len,
				"timeline", archive_path,
				entry->ext.effect.timeline_file,
				sizeof(entry->ext.effect.timeline_file));
		}
		break;
	case ASSET_MATERIAL:
		s_clearPath(entry->ext.material.material_file,
			sizeof(entry->ext.material.material_file));
		s_clearPath(entry->ext.material.texture_archive,
			sizeof(entry->ext.material.texture_archive));
		s_clearPath(entry->ext.material.effect_archive,
			sizeof(entry->ext.material.effect_archive));
		if (!s_copyManifestMemberPath(manifest, manifest_len, "material_file",
				archive_path, entry->ext.material.material_file,
				sizeof(entry->ext.material.material_file))
				&& !s_copyManifestMemberPath(manifest, manifest_len,
				"file_path", archive_path,
				entry->ext.material.material_file,
				sizeof(entry->ext.material.material_file))) {
			strncpy(entry->ext.material.material_file, source_path,
				sizeof(entry->ext.material.material_file) - 1);
			entry->ext.material.material_file[
				sizeof(entry->ext.material.material_file) - 1] = '\0';
		}
		if (!s_copyManifestMemberPath(manifest, manifest_len,
				"texture_archive", archive_path,
				entry->ext.material.texture_archive,
				sizeof(entry->ext.material.texture_archive))) {
			s_copyManifestMemberPath(manifest, manifest_len,
				"texture_file", archive_path,
				entry->ext.material.texture_archive,
				sizeof(entry->ext.material.texture_archive));
		}
		s_copyManifestMemberPath(manifest, manifest_len, "effect_archive",
			archive_path, entry->ext.material.effect_archive,
			sizeof(entry->ext.material.effect_archive));
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

	if (meta->type == ASSET_THEME) {
		strncpy(member, "theme.json", sizeof(member) - 1);
		member[sizeof(member) - 1] = '\0';
	} else if (meta->type == ASSET_MISSION) {
		strncpy(member, "mission.graph.json", sizeof(member) - 1);
		member[sizeof(member) - 1] = '\0';
	} else if (!s_firstManifestMember(manifest, manifest_len, meta,
			member, sizeof(member))) {
		return -1;
	}

	if (!loaderWalkerArchiveMemberPath(file_path, member,
			source_path, sizeof(source_path))) {
		return -1;
	}

	s_applyTypeFields(entry, manifest, manifest_len, source_path, file_path);
	if (meta->type == ASSET_MISSION &&
			!s_applyMissionPublicDescriptor(entry, file_path)) {
		entry->enabled = 0;
		sysLogPrintf(LOG_ERROR,
			"LOADER.UNIVERSAL.META: invalid public mission source '%s'",
			file_path);
		return -1;
	}
	loaderWalkerMarkBaseArchiveEntry(entry);
	catalogSetPrimaryFile(entry, source_path);
	return 1;
}

static const meta_walker_desc_t s_MetaFamilies[] = {
	{ ASSET_ARENA,       "arena",      "arenas",      ".pdarena",      "arena.ini",     { "scenario_archive", NULL, NULL, NULL, NULL, NULL } },
	{ ASSET_CHARACTER,   "character",  "characters",  ".pdcharacter",  "character.ini", { "body_archive", "head_archive", "portrait_file", NULL, NULL, NULL } },
	{ ASSET_BODY,        "body",       "bodies",      ".pdbody",       "body.ini",      { "mesh_archive", "hand_archive", NULL, NULL, NULL, NULL } },
	{ ASSET_HEAD,        "head",       "heads",       ".pdhead",       "head.ini",      { "mesh_archive", NULL, NULL, NULL, NULL, NULL } },
	{ ASSET_SKIN,        "skin",       "skins",       ".pdskin",       "skin.ini",      { "skin_file", "swatches_file", "material_archive", "texture_archive" } },
	{ ASSET_PROP,        "prop",       "props",       ".pdprop",       "prop.ini",      { "model_file", "prop_file", "behavior_graph", NULL } },
	{ ASSET_VEHICLE,     "vehicle",    "vehicles",    ".pdvehicle",    "vehicle.ini",   { "model_file", "behavior_graph", "physics_file", NULL } },
	{ ASSET_MISSION,     "mission",    "missions",    ".pdmission",    "mission.ini",   { "mission_graph_file", "objectives_file", "scenario_archive", NULL } },
	{ ASSET_GAMEMODE,    "gamemode",   "gamemodes",   ".pdgamemode",   "rules.json",    { "rules_file", NULL, NULL, NULL } },
	{ ASSET_BOT_PROFILE, "botprofile", "botprofiles", ".pdbotprofile", "profile.json",  { "profile_file", NULL, NULL, NULL } },
	{ ASSET_HUD,         "hud",        "hud",         ".pdhud",        "layout.json",   { "layout_file", "texture_file", NULL, NULL } },
	{ ASSET_EFFECT,      "effect",     "effects",     ".pdeffect",     "effect.graph.json", { "effect_file", "behavior_graph", "timeline_file", "timeline" } },
	{ ASSET_MATERIAL,    "material",   "materials",   ".pdmaterial",   "material.json", { "material_file", "file_path", "texture_archive", "effect_archive" } },
	{ ASSET_THEME,       "theme",      "themes",      ".pdtheme",      "theme.json",    { "theme_file", "ui_archive", "font_archive", "audio_archive", "music_archive", "effect_archive" } },
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
