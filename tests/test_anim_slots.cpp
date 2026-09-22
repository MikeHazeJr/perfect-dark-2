/*
 * tests/test_anim_slots.cpp -- c3849 Wave 2: catalog-owned private
 * custom-animation slot allocator.
 *
 * Source under test: port/src/assetcatalog_anim_slots.c
 *   assetCatalogResolveAnimPrivateSlot / assetCatalogResetCustomAnimSlots
 *
 * A net-new custom .pdanim row (no authored anim_id) gets a private animnum
 * in [ANIM_CUSTOM_START, ANIM_CUSTOM_END_SLOT); g_Anims + the per-animnum
 * arrays grow by the custom range with catalog-seeded rows. Pinned: in-range,
 * dedup-by-id, distinct slots, empty/null reject, exhaustion loud-fail,
 * reset, and the range anchors (base adjacency + the LOAD_MAX_ANIMS ceiling).
 */

#include "catch.hpp"

#include <cstdio>

extern "C" {
#include "assetcatalog_anim_slots.h"
}

TEST_CASE("anim slots: allocation is in the private custom range",
          "[catalog][anim][slots][c3849]") {
	assetCatalogResetCustomAnimSlots();
	s32 slot = assetCatalogResolveAnimPrivateSlot("mod_x:anim_custom_zap");
	REQUIRE(slot >= ANIM_CUSTOM_START);
	REQUIRE(slot < ANIM_CUSTOM_END_SLOT);
}

TEST_CASE("anim slots: same id dedups, distinct ids differ",
          "[catalog][anim][slots][c3849]") {
	assetCatalogResetCustomAnimSlots();
	s32 a = assetCatalogResolveAnimPrivateSlot("mod_x:anim_a");
	s32 b = assetCatalogResolveAnimPrivateSlot("mod_x:anim_a");
	s32 c = assetCatalogResolveAnimPrivateSlot("mod_x:anim_b");
	REQUIRE(a == b);
	REQUIRE(a != c);
}

TEST_CASE("anim slots: empty / null id rejected; exhaustion loud-fails",
          "[catalog][anim][slots][c3849]") {
	assetCatalogResetCustomAnimSlots();
	REQUIRE(assetCatalogResolveAnimPrivateSlot("") == -1);
	REQUIRE(assetCatalogResolveAnimPrivateSlot(nullptr) == -1);

	for (s32 i = 0; i < ANIM_CUSTOM_COUNT; i++) {
		char id[32];
		snprintf(id, sizeof(id), "mod_x:anim_%d", i);
		REQUIRE(assetCatalogResolveAnimPrivateSlot(id) >= ANIM_CUSTOM_START);
	}
	REQUIRE(assetCatalogResolveAnimPrivateSlot("mod_x:anim_overflow") == -1);
	/* Dedup still works when full. */
	REQUIRE(assetCatalogResolveAnimPrivateSlot("mod_x:anim_0") == ANIM_CUSTOM_START);
}

TEST_CASE("anim slots: reset clears reservations",
          "[catalog][anim][slots][c3849]") {
	assetCatalogResetCustomAnimSlots();
	s32 first = assetCatalogResolveAnimPrivateSlot("mod_x:anim_a");
	assetCatalogResetCustomAnimSlots();
	REQUIRE(assetCatalogResolveAnimPrivateSlot("mod_x:anim_b") == first);
}

TEST_CASE("anim slots: range anchors (base adjacency + override-index ceiling)",
          "[catalog][anim][slots][c3849]") {
	REQUIRE(ANIM_CUSTOM_START == ANIM_END);
	REQUIRE(ANIM_CUSTOM_END_SLOT == ANIM_CUSTOM_START + ANIM_CUSTOM_COUNT);
	/* LOAD_MAX_ANIMS bounds the animnum override reverse index. */
	REQUIRE(ANIM_CUSTOM_END_SLOT == 0x8000);
	REQUIRE(ANIM_CUSTOM_COUNT > 32);
}

TEST_CASE("weapon command graphs never consume character animation slots",
          "[catalog][anim][slots][b1035]") {
	REQUIRE_FALSE(assetCatalogAnimationCategoryUsesCharacterClip(
		"weapon_animation"));
	REQUIRE(assetCatalogAnimationCategoryUsesCharacterClip(
		"character_animation"));
	/* Preserve legacy custom character archives that predate the category
	 * discriminator. Command archives always carry weapon_animation. */
	REQUIRE(assetCatalogAnimationCategoryUsesCharacterClip(""));
	REQUIRE(assetCatalogAnimationCategoryUsesCharacterClip(nullptr));
}

TEST_CASE("anim slots: admission snapshot restores reservations exactly",
          "[catalog][anim][slots][b1043][T-CATALOG-003]") {
	assetCatalogResetCustomAnimSlots();
	s32 retained = assetCatalogResolveAnimPrivateSlot("mod_x:anim_retained");
	void *snapshot = assetCatalogSnapshotCustomAnimSlots();
	REQUIRE(snapshot != nullptr);
	REQUIRE(assetCatalogResolveAnimPrivateSlot("mod_x:anim_rejected") == retained + 1);
	REQUIRE(assetCatalogRestoreCustomAnimSlots(snapshot) == 1);
	assetCatalogDestroyCustomAnimSlotSnapshot(snapshot);
	REQUIRE(assetCatalogResolveAnimPrivateSlot("mod_x:anim_replacement") == retained + 1);
}

TEST_CASE("anim generations: catalog reset and rollback preserve reserved native slots",
          "[catalog][anim][slots][generation]") {
    assetCatalogResetCustomAnimSlots();
    const s32 first = assetCatalogResolveAnimPrivateSlot("mod:generation_catalog");
    void *snapshot = assetCatalogSnapshotCustomAnimSlots();
    REQUIRE(snapshot != nullptr);
    const s32 generation = assetCatalogReserveAnimGenerationSlot();
    REQUIRE(generation == ANIM_CUSTOM_END_SLOT - 1);
    assetCatalogResetCustomAnimSlots();
    const s32 another = assetCatalogReserveAnimGenerationSlot();
    REQUIRE(another == generation - 1);
    REQUIRE(assetCatalogRestoreCustomAnimSlots(snapshot));
    REQUIRE(assetCatalogResolveAnimPrivateSlot("mod:generation_catalog") == first);
    REQUIRE(assetCatalogResolveAnimPrivateSlot("mod:ordinary_after_reset") != generation);
    assetCatalogReleaseAnimGenerationSlot(generation);
    REQUIRE(assetCatalogReserveAnimGenerationSlot() == generation);
    assetCatalogReleaseAnimGenerationSlot(generation);
    assetCatalogReleaseAnimGenerationSlot(another);
    REQUIRE(assetCatalogRestoreCustomAnimSlots(snapshot));
    assetCatalogDestroyCustomAnimSlotSnapshot(snapshot);
    assetCatalogResetCustomAnimSlots();
}
