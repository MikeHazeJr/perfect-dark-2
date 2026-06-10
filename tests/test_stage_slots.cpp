/*
 * tests/test_stage_slots.cpp -- c3849 Wave 2: catalog-owned private
 * custom-stage stagenum allocator.
 *
 * Source under test: port/src/assetcatalog_stage_slots.c
 *   assetCatalogResolveStagenumPrivateSlot / assetCatalogResetCustomStageSlots
 *
 * A net-new custom map/arena/scenario (no authored stagenum) gets a private
 * stagenum in [STAGENUM_CUSTOM_START, STAGENUM_CUSTOM_END), outside the base
 * logical range and inside the 7-bit mpsetup save field. Pinned: in-range,
 * dedup-by-mint-key, distinct slots, empty/null reject, exhaustion
 * loud-fail, reset, and the range anchors.
 */

#include "catch.hpp"

#include <cstdio>

extern "C" {
#include "constants.h" /* SND_CUSTOM_* */
#include "assetcatalog_stage_slots.h"
}

TEST_CASE("stage slots: allocation is in the private custom range",
          "[catalog][stage][slots][c3849]") {
	assetCatalogResetCustomStageSlots();
	s32 slot = assetCatalogResolveStagenumPrivateSlot("mod_x:stage_custom_zap");
	REQUIRE(slot >= STAGENUM_CUSTOM_START);
	REQUIRE(slot < STAGENUM_CUSTOM_END);
}

TEST_CASE("stage slots: same id dedups, distinct ids differ",
          "[catalog][stage][slots][c3849]") {
	assetCatalogResetCustomStageSlots();
	s32 a = assetCatalogResolveStagenumPrivateSlot("mod_x:stage_a");
	s32 b = assetCatalogResolveStagenumPrivateSlot("mod_x:stage_a");
	s32 c = assetCatalogResolveStagenumPrivateSlot("mod_x:stage_b");
	REQUIRE(a == b);
	REQUIRE(a != c);
}

TEST_CASE("stage slots: empty / null id rejected; exhaustion loud-fails",
          "[catalog][stage][slots][c3849]") {
	assetCatalogResetCustomStageSlots();
	REQUIRE(assetCatalogResolveStagenumPrivateSlot("") == -1);
	REQUIRE(assetCatalogResolveStagenumPrivateSlot(nullptr) == -1);

	for (s32 i = 0; i < STAGENUM_CUSTOM_COUNT; i++) {
		char id[32];
		snprintf(id, sizeof(id), "mod_x:stage_%d", i);
		REQUIRE(assetCatalogResolveStagenumPrivateSlot(id) >= STAGENUM_CUSTOM_START);
	}
	REQUIRE(assetCatalogResolveStagenumPrivateSlot("mod_x:stage_overflow") == -1);
	/* Dedup still works when full. */
	REQUIRE(assetCatalogResolveStagenumPrivateSlot("mod_x:stage_0") == STAGENUM_CUSTOM_START);
}

TEST_CASE("stage slots: reset clears reservations",
          "[catalog][stage][slots][c3849]") {
	assetCatalogResetCustomStageSlots();
	s32 first = assetCatalogResolveStagenumPrivateSlot("mod_x:stage_a");
	assetCatalogResetCustomStageSlots();
	REQUIRE(assetCatalogResolveStagenumPrivateSlot("mod_x:stage_b") == first);
}

TEST_CASE("stage slots: range anchors (logical-range + 7-bit save anchors)",
          "[catalog][stage][slots][c3849]") {
	REQUIRE(STAGENUM_CUSTOM_START == 0x60);
	REQUIRE(STAGENUM_CUSTOM_END == STAGENUM_CUSTOM_START + STAGENUM_CUSTOM_COUNT);
	/* Above the base logical max (0x5f) and inside the 7-bit save field. */
	REQUIRE(STAGENUM_CUSTOM_START > 0x5f);
	REQUIRE(STAGENUM_CUSTOM_END <= 0x80);
}
