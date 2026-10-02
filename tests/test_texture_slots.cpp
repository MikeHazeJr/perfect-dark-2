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
 * + the native unsigned 16-bit identity ceiling).
 */

#include "catch.hpp"
#include <vector>

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

	/* Fill the shared range with retained generations, leaving one custom row.
	 * This exercises real exhaustion without quadratic string dedup setup. */
	std::vector<s32> generations;
	for (s32 i = 0; i < TEXTURE_CUSTOM_COUNT - 1; ++i)
		generations.push_back(assetCatalogReserveTextureGenerationSlot());
	REQUIRE(generations.back() == TEXTURE_CUSTOM_START + 1);
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_0") == TEXTURE_CUSTOM_START);
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_overflow") == -1);
	/* Dedup still works when full. */
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_0") == TEXTURE_CUSTOM_START);
	for (s32 slot : generations) assetCatalogReleaseTextureGenerationSlot(slot);
}

TEST_CASE("texture slots: reset clears reservations",
          "[catalog][texture][slots][c3849]") {
	assetCatalogResetCustomTextureSlots();
	s32 first = assetCatalogResolveTexturePrivateSlot("mod_x:tex_a");
	assetCatalogResetCustomTextureSlots();
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_b") == first);
}

TEST_CASE("texture slots: range anchors (base adjacency + native identity ceiling)",
          "[catalog][texture][slots][c3849]") {
	REQUIRE(TEXTURE_CUSTOM_START == NUM_TEXTURES);
	REQUIRE(TEXTURE_CUSTOM_END == TEXTURE_CUSTOM_START + TEXTURE_CUSTOM_COUNT);
	REQUIRE(TEXTURE_CUSTOM_END == TEXTURE_NATIVE_SLOT_LIMIT);
	REQUIRE(TEXTURE_CUSTOM_COUNT > NUM_TEXTURES);
	REQUIRE(TEXTURE_HIT_GLASS >= TEXTURE_CUSTOM_END);
}

TEST_CASE("custom texture allocation cannot steal a snapshot row after reset", "[catalog][texture-slots]")
{
    assetCatalogResetCustomTextureSlots();
    const s32 retained = assetCatalogResolveTexturePrivateSlot("mod:retained");
    void *saved = assetCatalogSnapshotCustomTextureSlots();
    REQUIRE(saved);
    assetCatalogResetCustomTextureSlots();
    REQUIRE(assetCatalogResolveTexturePrivateSlot("mod:new") != retained);
    texture_slot_usage_t usage;
    assetCatalogGetTextureSlotUsage(&usage);
    REQUIRE(usage.custom == 1);
    REQUIRE(usage.snapshots == 1);
    REQUIRE(usage.free == TEXTURE_CUSTOM_COUNT - 2);
    REQUIRE(assetCatalogRestoreCustomTextureSlots(saved));
    REQUIRE(assetCatalogResolveTexturePrivateSlot("mod:retained") == retained);
    assetCatalogResetCustomTextureSlots();
    assetCatalogDestroyCustomTextureSlotSnapshot(saved);
    REQUIRE(assetCatalogResolveTexturePrivateSlot("mod:released") == retained);
    assetCatalogResetCustomTextureSlots();
}

TEST_CASE("generation texture capacity retains a complete base-sized cohort", "[catalog][texture-slots]")
{
    assetCatalogResetCustomTextureSlots();
    std::vector<s32> generations;
    for (s32 i = 0; i < NUM_TEXTURES; ++i) {
        const s32 slot = assetCatalogReserveTextureGenerationSlot();
        REQUIRE(slot == TEXTURE_CUSTOM_END - i - 1);
        generations.push_back(slot);
    }
    texture_slot_usage_t usage;
    assetCatalogGetTextureSlotUsage(&usage);
    REQUIRE(usage.generations == NUM_TEXTURES);
    REQUIRE(usage.free == TEXTURE_CUSTOM_COUNT - NUM_TEXTURES);
    assetCatalogResetCustomTextureSlots();
    assetCatalogGetTextureSlotUsage(&usage);
    REQUIRE(usage.generations == NUM_TEXTURES);
    for (s32 slot : generations) assetCatalogReleaseTextureGenerationSlot(slot);
    REQUIRE(assetCatalogReserveTextureGenerationSlot() == generations.front());
    assetCatalogReleaseTextureGenerationSlot(generations.front());
}

TEST_CASE("texture slots: admission snapshot restores reservations exactly",
          "[catalog][texture][slots][b1043][T-CATALOG-003]") {
	assetCatalogResetCustomTextureSlots();
	s32 retained = assetCatalogResolveTexturePrivateSlot("mod_x:tex_retained");
	void *snapshot = assetCatalogSnapshotCustomTextureSlots();
	REQUIRE(snapshot != nullptr);
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_rejected") == retained + 1);
	REQUIRE(assetCatalogRestoreCustomTextureSlots(snapshot) == 1);
	assetCatalogDestroyCustomTextureSlotSnapshot(snapshot);
	REQUIRE(assetCatalogResolveTexturePrivateSlot("mod_x:tex_replacement") == retained + 1);
}

TEST_CASE("texture generation reservations survive reset and protect snapshot rollback", "[catalog][texture-slots]")
{
    assetCatalogResetCustomTextureSlots();
    const s32 owner = assetCatalogResolveTexturePrivateSlot("mod:snapshot");
    void *saved = assetCatalogSnapshotCustomTextureSlots();
    REQUIRE(saved);
    assetCatalogResetCustomTextureSlots();
    std::vector<s32> generations;
    for (s32 i = 0; i < TEXTURE_CUSTOM_COUNT - 1; ++i) {
        s32 slot = assetCatalogReserveTextureGenerationSlot();
        REQUIRE(slot >= TEXTURE_CUSTOM_START);
        REQUIRE(slot < TEXTURE_CUSTOM_END);
        REQUIRE(slot != owner);
        generations.push_back(slot);
    }
    REQUIRE(assetCatalogReserveTextureGenerationSlot() == -1);
    REQUIRE(assetCatalogRestoreCustomTextureSlots(saved));
    REQUIRE(assetCatalogResolveTexturePrivateSlot("mod:snapshot") == owner);
    REQUIRE(assetCatalogResolveTexturePrivateSlot("mod:cannot_steal") == -1);
    const s32 reuse = generations.back(); generations.pop_back();
    assetCatalogReleaseTextureGenerationSlot(reuse);
    REQUIRE(assetCatalogResolveTexturePrivateSlot("mod:released") == reuse);
    for (s32 slot : generations) assetCatalogReleaseTextureGenerationSlot(slot);
    assetCatalogDestroyCustomTextureSlotSnapshot(saved);
    assetCatalogResetCustomTextureSlots();
}
