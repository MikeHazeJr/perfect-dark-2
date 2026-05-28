/**
 * asset_source_debug.c -- Debug-only asset source enforcement.
 */

#include <PR/ultratypes.h>

#include "asset_source_debug.h"
#include "assetprovider.h"
#include "config.h"
#include "platform.h"

static s32 s_SourceOnlyType = ASSET_NONE;

#ifndef PD_TESTS
PD_CONSTRUCTOR static void assetSourceDebugConfigInit(void)
{
	configRegisterInt("Debug.AssetSourceOnlyType", &s_SourceOnlyType,
	                  ASSET_NONE, ASSET_TYPE_COUNT - 1);
}
#endif

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

	if (entry->source.override.provider == fileProvider()) {
		return 1;
	}

	return entry->source.primary.provider == fileProvider();
}

s32 assetSourceDebugEntryRequiresPublicFileSource(const asset_entry_t *entry)
{
	return entry
	    && assetSourceDebugIsEnabledFor(entry->type)
	    && !assetSourceDebugEntryUsesPublicFileSource(entry);
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
