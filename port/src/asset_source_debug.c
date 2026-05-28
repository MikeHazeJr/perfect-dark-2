/**
 * asset_source_debug.c -- Debug-only asset source enforcement.
 */

#include <PR/ultratypes.h>
#include <string.h>

#include "asset_source_debug.h"
#include "assetload.h"
#include "assetprovider.h"
#include "config.h"
#include "platform.h"
#include "system.h"

static s32 s_SourceOnlyType = ASSET_NONE;

#ifndef PD_TESTS
PD_CONSTRUCTOR static void assetSourceDebugConfigInit(void)
{
	configRegisterInt("Debug.AssetSourceOnlyType", &s_SourceOnlyType,
	                  ASSET_NONE, ASSET_TYPE_COUNT - 1);
}
#endif

static s32 s_pathSep(char c)
{
	return c == '/' || c == '\\';
}

static char s_lower(char c)
{
	if (c >= 'A' && c <= 'Z') {
		return (char)(c - 'A' + 'a');
	}
	return c;
}

static s32 s_endsWithNoCase(const char *path, const char *suffix)
{
	size_t path_len;
	size_t suffix_len;
	size_t i;

	if (!path || !suffix) {
		return 0;
	}

	path_len = strlen(path);
	suffix_len = strlen(suffix);
	if (suffix_len > path_len) {
		return 0;
	}

	path += path_len - suffix_len;
	for (i = 0; i < suffix_len; i++) {
		if (s_lower(path[i]) != s_lower(suffix[i])) {
			return 0;
		}
	}

	return 1;
}

static s32 s_pathHasSegment(const char *path, const char *segment)
{
	size_t segment_len;
	const char *p;

	if (!path || !segment || !segment[0]) {
		return 0;
	}

	segment_len = strlen(segment);

	for (p = path; *p; p++) {
		size_t i;

		if (p != path && !s_pathSep(p[-1])) {
			continue;
		}

		for (i = 0; i < segment_len; i++) {
			if (s_lower(p[i]) != s_lower(segment[i])) {
				break;
			}
		}

		if (i == segment_len && (p[i] == '\0' || s_pathSep(p[i]))) {
			return 1;
		}
	}

	return 0;
}

static s32 s_pathIsRawExtractedRomPayload(const char *path)
{
	if (!path || !path[0]) {
		return 1;
	}

	if (s_endsWithNoCase(path, ".bin")) {
		return 1;
	}

	/* data/<romid>/files and data/<romid>/segments are bootstrap/cache
	 * extraction products. They are not the clean public typed-archive source
	 * that source-only mode is meant to prove. */
	if (s_pathHasSegment(path, "data")
			&& (s_pathHasSegment(path, "files")
				|| s_pathHasSegment(path, "segments"))) {
		return 1;
	}

	return 0;
}

asset_type_e assetSourceDebugOnlyType(void)
{
	if (s_SourceOnlyType <= ASSET_NONE || s_SourceOnlyType >= ASSET_TYPE_COUNT) {
		return ASSET_NONE;
	}
	return (asset_type_e)s_SourceOnlyType;
}

void assetSourceDebugSetOnlyType(asset_type_e type)
{
	if (type <= ASSET_NONE || type >= ASSET_TYPE_COUNT) {
		s_SourceOnlyType = ASSET_NONE;
		return;
	}
	s_SourceOnlyType = (s32)type;
}

s32 assetSourceDebugIsEnabledFor(asset_type_e type)
{
	return type != ASSET_NONE && type == assetSourceDebugOnlyType();
}

s32 assetSourceDebugEntryUsesPublicFileSource(const asset_entry_t *entry)
{
	if (!entry) {
		return 0;
	}

	if (assetSourceDebugHandleUsesPublicFileSource(entry->source.override)) {
		return 1;
	}

	return assetSourceDebugHandleUsesPublicFileSource(entry->source.primary);
}

s32 assetSourceDebugEntryRequiresPublicFileSource(const asset_entry_t *entry)
{
	return entry
	    && assetSourceDebugIsEnabledFor(entry->type)
	    && !assetSourceDebugEntryUsesPublicFileSource(entry);
}

s32 assetSourceDebugHandleUsesPublicFileSource(asset_data_handle_t handle)
{
	const char *path;

	if (assetHandleIsNull(handle) || handle.provider != fileProvider()) {
		return 0;
	}

	path = fileProviderPath(handle);
	return !s_pathIsRawExtractedRomPayload(path);
}

s32 assetSourceDebugHandleRequiresPublicFileSource(asset_type_e type,
	asset_data_handle_t handle)
{
	return assetSourceDebugIsEnabledFor(type)
	    && !assetSourceDebugHandleUsesPublicFileSource(handle);
}

void assetSourceDebugFatalHandleFallback(asset_type_e type,
	const char *context, const char *asset_id, asset_data_handle_t handle)
{
	char desc[160];

	if (!assetSourceDebugHandleRequiresPublicFileSource(type, handle)) {
		return;
	}

	sysFatalError("ASSET.SOURCE_ONLY: %s '%s' %s still uses %s; "
	              "refusing ROM/static fallback.",
	              assetSourceDebugTypeLabel(type),
	              asset_id && asset_id[0] ? asset_id : "?",
	              context && context[0] ? context : "runtime payload",
	              assetDescribe(handle, desc, sizeof(desc)));
}

const char *assetSourceDebugTypeLabel(asset_type_e type)
{
	switch (type) {
	case ASSET_NONE:        return "None";
	case ASSET_MAP:         return "Map";
	case ASSET_CHARACTER:   return "Character";
	case ASSET_SKIN:        return "Skin";
	case ASSET_BOT_VARIANT: return "Bot Variant";
	case ASSET_WEAPON:      return "Weapon";
	case ASSET_TEXTURES:    return "Texture Pack";
	case ASSET_SFX:         return "SFX";
	case ASSET_MUSIC:       return "Music";
	case ASSET_PROP:        return "Prop";
	case ASSET_VEHICLE:     return "Vehicle";
	case ASSET_MISSION:     return "Mission";
	case ASSET_UI:          return "UI";
	case ASSET_TOOL:        return "Tool";
	case ASSET_ARENA:       return "Arena";
	case ASSET_BODY:        return "Body";
	case ASSET_HEAD:        return "Head";
	case ASSET_ANIMATION:   return "Animation";
	case ASSET_TEXTURE:     return "Texture";
	case ASSET_GAMEMODE:    return "Gamemode";
	case ASSET_AUDIO:       return "Audio";
	case ASSET_HUD:         return "HUD";
	case ASSET_EFFECT:      return "Effect";
	case ASSET_MODEL:       return "Model";
	case ASSET_LANG:        return "Language";
	case ASSET_BOT_PROFILE: return "Bot Profile";
	case ASSET_PROJECTILE:  return "Projectile";
	case ASSET_ENTITY:      return "Entity";
	case ASSET_MATERIAL:    return "Material";
	case ASSET_FONT:        return "Font";
	case ASSET_SCENARIO:    return "Scenario";
	case ASSET_THEME:       return "Theme";
	default:                return "Unknown";
	}
}
