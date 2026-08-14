#include "net/net_manifest_type.h"

u8 netManifestTypeForCatalogAsset(asset_type_e asset_type)
{
	switch (asset_type) {
	case ASSET_BODY:       return MANIFEST_TYPE_BODY;
	case ASSET_HEAD:       return MANIFEST_TYPE_HEAD;
	case ASSET_MAP:
	case ASSET_ARENA:      return MANIFEST_TYPE_STAGE;
	case ASSET_WEAPON:     return MANIFEST_TYPE_WEAPON;
	case ASSET_MODEL:      return MANIFEST_TYPE_MODEL;
	case ASSET_ANIMATION:  return MANIFEST_TYPE_ANIM;
	case ASSET_TEXTURE:    return MANIFEST_TYPE_TEXTURE;
	case ASSET_LANG:       return MANIFEST_TYPE_LANG;
	case ASSET_AUDIO:
	case ASSET_SFX:
	case ASSET_MUSIC:      return MANIFEST_TYPE_AUDIO;
	case ASSET_PROJECTILE: return MANIFEST_TYPE_PROJECTILE;
	case ASSET_ENTITY:     return MANIFEST_TYPE_ENTITY;
	case ASSET_NONE:       return MANIFEST_TYPE_COMPONENT;
	default:               return MANIFEST_TYPE_ASSET;
	}
}

bool netManifestTypeAcceptsCatalogAsset(u8 manifest_type, u8 slot_index,
		asset_type_e asset_type)
{
	if (asset_type <= ASSET_NONE || asset_type >= ASSET_TYPE_COUNT
			|| manifest_type != netManifestTypeForCatalogAsset(asset_type)) {
		return false;
	}

	return manifest_type != MANIFEST_TYPE_ASSET
		|| slot_index == (u8)asset_type;
}
