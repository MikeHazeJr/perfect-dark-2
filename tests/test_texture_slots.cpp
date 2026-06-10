/*
 * tests/test_texture_slots.cpp -- c3849 Wave 2: catalog-owned private
 * custom-texture slot allocator.
 *
 * Source under test: port/src/assetcatalog_texture_slots.c
 *   assetCatalogResolveTexturePrivateSlot / assetCatalogResetCustomTextureSlots
 *
 * A net-new custom .pdtexture row (no authored texture_id) gets a private
 * texnum in [TEXTURE_CUSTOM_START, TEXTURE_CUSTOM_END) so modeldef
 * texconfigs can reach it; g_Textures grows by the custom range with
 * zero-init rows. Pinned: in-range, dedup-by-id, distinct slots, empty/null
 * reject, exhaustion loud-fail, reset, and the range anchors (base adjacency
 * + the hard 12-bit texturenum ceiling).
 */

#include "catch.hpp"

#include <cstdio>

extern "C" {
#include "constants.h" /* TEXTURE_CUSTOM_* */
#include "assetcatalog_texture_slots.h"
}

TEST_CASE("texture slots: allocation is in the private custom range",
          "[catalog][texture][slots][c3849]") {
	assetCatalogResetCustomTextureSlots();
	s32 slot = assetCatalogResolveTexturePrivateSlot("mod_x:tex_custom_zap");
	REQUIRE(slot >= TEXTURE_CUSTOM_START);
	REQUIRE(slot < TEXTURE_CUSTOM_END);
}

TEST_CASE("texture slots: same id dedups, distinct ids differ",
          "[catalog][texture][slots][c3849]") {
	assetCatalogResetCustomTextureSlots();
	s32 a = assetCatalogResolveTexturePrivateSlot("mod_x:tex_a");
	s32 b = assetCatalogResolveTexturePrivateSlot("mod_x:tex_a");
	s32 c = assetCatalogResolveTexturePrivateSlot("mod_x:tex_b");
	REQUIRE(a == b);
	REQUIRE(a != c);
}

TEST_CASE("texture slots: empty / null id rejected; exhaustion loud-fails",
          "[catalog][texture][slots][c3849]") {
	assetCatalogResetCustomTextureSlots();
	REQUIRE(assetCatalogResolveTexturePrivateSlot("") == -1);
	REQUIRE(assetCatalogResolveTexturePrivateSlot(nullptr) == -1);

	for (s32 i = 0; i < TEXTURE_CUSTOM_COUNT; i++) {
		char id[32];
		snprintf(id, sizeof(id), "mod_x:tex_%d", i);
		REQUIRE(assetCatalogResolveTexturePrivateSlot(id) >= TEXTURE_CUSTOM_START);
	}
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_overflow") == -1);
	/* Dedup still works when full. */
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_0") == TEXTURE_CUSTOM_START);
}

TEST_CASE("texture slots: reset clears reservations",
          "[catalog][texture][slots][c3849]") {
	assetCatalogResetCustomTextureSlots();
	s32 first = assetCatalogResolveTexturePrivateSlot("mod_x:tex_a");
	assetCatalogResetCustomTextureSlots();
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_b") == first);
}

TEST_CASE("texture slots: range anchors (base adjacency + 12-bit ceiling)",
          "[catalog][texture][slots][c3849]") {
	REQUIRE(TEXTURE_CUSTOM_START == NUM_TEXTURES);
	REQUIRE(TEXTURE_CUSTOM_END == TEXTURE_CUSTOM_START + TEXTURE_CUSTOM_COUNT);
	/* struct tex texturenum is a 12-bit field; the G_NOOP marker pack clamps
	 * at 4096 -- a slot past that would silently alias another texture. */
	REQUIRE(TEXTURE_CUSTOM_END <= 4096);
}
