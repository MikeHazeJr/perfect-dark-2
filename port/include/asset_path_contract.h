#ifndef _IN_ASSET_PATH_CONTRACT_H
#define _IN_ASSET_PATH_CONTRACT_H

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

/* Single key inventory shared by loose, typed-archive, and network ingress. */
static const char *const g_AssetPathSourceKeys[] = {
	"bodyfile", "headfile", "mesh_archive", "hand_archive",
	"body_archive", "head_archive", "portrait_file", "model_file", "model",
	"lo_model_file", "hand_model_file", "prop_file", "behavior_graph", "graph",
	"primary_graph", "secondary_graph", "settings_file", "settings",
	"variables_file", "variables", "shared_context_file", "shared_context",
	"presentation_file", "primary_projectile_archive", "deployed_entity_archive",
	"fire_sound_archive", "idle_animation_archive", "reticle_archive",
	"nested_payloads", "scene_file", "scene", "runtime_source_file",
	"geometry_file", "geometry", "blender_scene_file", "blender_scene",
	"visual_scene_file", "visual_scene", "visual_material_file",
	"visual_materials_file", "collision_file", "collision_source_file",
	"collision_source", "tiles_file", "portals_file", "pads_file", "spawns_file",
	"volumes_file", "objects_file", "setup_fields_file", "ai_lists_file",
	"setup_file", "mpsetup_file", "rooms_file", "rooms", "material_file",
	"props_file", "props", "objectives_file", "objectives", "navigation_file",
	"waypoints_file", "waygroups_file", "covers_file", "paths_file",
	"level_graph_file", "mission_graph_file", "scenario_archive", "rules_file",
	"profile_file", "visual_source_file", "texture_manifest_file", "music_file",
	"midi_file", "file_path", "subtitle_file", "locale_en_file", "locale_fr_file",
	"locale_de_file", "locale_it_file", "locale_es_file", "locale_ja_file",
	"effect_file", "timeline_file", "theme_file", "ui_archive", "font_archive",
	"audio_archive", "music_archive", "effect_archive", "animation_file",
	"commands_file", "material_archive", "skin_file", "texture_file", "layout_file",
	"nineslice_file", "swatches_file", "texture_archive", "texture",
	"physics_file", "font_file", "glyphs_file", "font", "strings_file",
	"strings",
};
#define ASSET_PATH_SOURCE_KEY_COUNT \
	(sizeof(g_AssetPathSourceKeys) / sizeof(g_AssetPathSourceKeys[0]))

static inline s32 assetPathKeyIsSource(const char *key)
{
	if (!key) return 0;
	for (size_t i = 0; i < ASSET_PATH_SOURCE_KEY_COUNT; i++) {
		if (strcmp(key, g_AssetPathSourceKeys[i]) == 0) return 1;
	}
	return 0;
}

/* Public asset paths are data, not labels. Never return a plausible prefix. */
static inline s32 assetPathCopyChecked(char *out, size_t out_cap,
		const char *source)
{
	size_t length;
	if (!out || out_cap == 0) return 0;
	out[0] = '\0';
	if (!source) return 1;
	length = strlen(source);
	if (length >= out_cap) return 0;
	memcpy(out, source, length + 1);
	return 1;
}

static inline s32 assetPathJoinChecked(char *out, size_t out_cap,
		const char *left, const char *separator, const char *right)
{
	size_t left_len, separator_len, right_len;
	if (!out || out_cap == 0) return 0;
	out[0] = '\0';
	if (!left || !separator || !right) return 0;
	left_len = strlen(left);
	separator_len = strlen(separator);
	right_len = strlen(right);
	if (left_len >= out_cap || separator_len >= out_cap - left_len ||
			right_len >= out_cap - left_len - separator_len) return 0;
	memcpy(out, left, left_len);
	memcpy(out + left_len, separator, separator_len);
	memcpy(out + left_len + separator_len, right, right_len + 1);
	return 1;
}

static inline s32 assetPathIsAbsolute(const char *path)
{
	if (!path || !path[0]) return 0;
	return path[0] == '/' || path[0] == '\\'
		|| (((path[0] >= 'A' && path[0] <= 'Z')
				|| (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':');
}

static inline s32 assetPathHasParentTraversal(const char *path)
{
	const char *part = path;
	if (!path) return 0;
	while (*part) {
		const char *end = part;
		while (*end && *end != '/' && *end != '\\'
				&& !(end[0] == ':' && end[1] == ':')) end++;
		if (end - part == 2 && part[0] == '.' && part[1] == '.') return 1;
		if (!*end) break;
		part = end + ((end[0] == ':' && end[1] == ':') ? 2 : 1);
	}
	return 0;
}

/* Qualify a public loose/network source against its real component root.
 * Already-qualified absolute or archive-VFS paths are retained exactly.
 * Relative paths, including nested subdirectories, are always rooted. */
static inline s32 assetPathQualifyFilesystemChecked(char *out, size_t out_cap,
		const char *root, const char *value)
{
	if (!out || out_cap == 0) return 0;
	out[0] = '\0';
	if (!value || !value[0]) return 1;
	if (assetPathHasParentTraversal(value)) return 0;
	if (assetPathIsAbsolute(value) || strstr(value, "::")) {
		return assetPathCopyChecked(out, out_cap, value);
	}
	if (!root || !root[0]) return 0;
	return assetPathJoinChecked(out, out_cap, root, "/", value);
}

/* Qualify a member declared inside a typed archive. Public descriptors may
 * retain a previously-qualified VFS chain, but may not escape the archive. */
static inline s32 assetPathQualifyArchiveChecked(char *out, size_t out_cap,
		const char *archive_ref, const char *value)
{
	if (!out || out_cap == 0) return 0;
	out[0] = '\0';
	if (!value || !value[0]) return 1;
	if (assetPathHasParentTraversal(value)) return 0;
	if (strstr(value, "::")) return assetPathCopyChecked(out, out_cap, value);
	if (assetPathIsAbsolute(value)) return 0;
	if (!archive_ref || !archive_ref[0]) return 0;
	return assetPathJoinChecked(out, out_cap, archive_ref, "::", value);
}

#endif
