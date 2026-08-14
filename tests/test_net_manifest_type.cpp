#include "catch.hpp"

extern "C" {
#include "net/net_manifest_type.h"
}

TEST_CASE("manifest type mapping preserves many-to-one catalog families",
		"[net][manifest][catalog][b1075]")
{
	REQUIRE(netManifestTypeForCatalogAsset(ASSET_MAP) == MANIFEST_TYPE_STAGE);
	REQUIRE(netManifestTypeForCatalogAsset(ASSET_ARENA) == MANIFEST_TYPE_STAGE);
	REQUIRE(netManifestTypeForCatalogAsset(ASSET_AUDIO) == MANIFEST_TYPE_AUDIO);
	REQUIRE(netManifestTypeForCatalogAsset(ASSET_SFX) == MANIFEST_TYPE_AUDIO);
	REQUIRE(netManifestTypeForCatalogAsset(ASSET_MUSIC) == MANIFEST_TYPE_AUDIO);
	REQUIRE(netManifestTypeForCatalogAsset(ASSET_EFFECT) == MANIFEST_TYPE_ASSET);
}

TEST_CASE("manifest type admission checks the concrete catalog type",
		"[net][manifest][catalog][b1075]")
{
	REQUIRE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH, ASSET_MAP));
	REQUIRE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH, ASSET_ARENA));
	REQUIRE_FALSE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH, ASSET_SCENARIO));

	REQUIRE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_AUDIO, MANIFEST_SLOT_MATCH, ASSET_AUDIO));
	REQUIRE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_AUDIO, MANIFEST_SLOT_MATCH, ASSET_SFX));
	REQUIRE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_AUDIO, MANIFEST_SLOT_MATCH, ASSET_MUSIC));
	REQUIRE_FALSE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_AUDIO, MANIFEST_SLOT_MATCH, ASSET_EFFECT));
}

TEST_CASE("generic manifest entries require an exact asset-type slot",
		"[net][manifest][catalog][b1075]")
{
	REQUIRE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_ASSET, (u8)ASSET_EFFECT, ASSET_EFFECT));
	REQUIRE_FALSE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_ASSET, (u8)ASSET_THEME, ASSET_EFFECT));
	REQUIRE_FALSE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_ASSET, (u8)ASSET_ARENA, ASSET_ARENA));
	REQUIRE_FALSE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_COMPONENT, MANIFEST_SLOT_MATCH, ASSET_NONE));
	REQUIRE_FALSE(netManifestTypeAcceptsCatalogAsset(
		MANIFEST_TYPE_ASSET, (u8)ASSET_TYPE_COUNT,
		(asset_type_e)ASSET_TYPE_COUNT));
}
