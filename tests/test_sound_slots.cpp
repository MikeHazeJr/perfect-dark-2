/*
 * tests/test_sound_slots.cpp -- c3849 Wave 2: catalog-owned private
 * custom-sound slot allocator.
 *
 * Source under test: port/src/assetcatalog_sound_slots.c
 *   assetCatalogResolveSoundPrivateSlot / assetCatalogResetCustomSoundSlots
 *
 * A net-new custom .pdsfx/.pdvoice row (no authored sound_id) gets a private
 * soundnum in [SND_CUSTOM_START, SND_CUSTOM_END) so the existing
 * catalogResolveSound -> source_soundnum reverse index -> file playback chain
 * reaches it with no native bank growth. Pinned: in-range, dedup-by-id,
 * distinct slots, empty/null reject, exhaustion loud-fail, reset, and the
 * range anchors (base adjacency + the 11-bit soundnum ceiling).
 */

#include "catch.hpp"

#include <cstdio>

extern "C" {
#include "constants.h" /* SND_CUSTOM_* */
#include "assetcatalog_sound_slots.h"
}

TEST_CASE("sound slots: allocation is in the private custom range",
          "[catalog][sound][slots][c3849]") {
	assetCatalogResetCustomSoundSlots();
	s32 slot = assetCatalogResolveSoundPrivateSlot("mod_x:sfx_custom_zap");
	REQUIRE(slot >= SND_CUSTOM_START);
	REQUIRE(slot < SND_CUSTOM_END);
}

TEST_CASE("sound slots: same id dedups, distinct ids differ",
          "[catalog][sound][slots][c3849]") {
	assetCatalogResetCustomSoundSlots();
	s32 a = assetCatalogResolveSoundPrivateSlot("mod_x:sfx_a");
	s32 b = assetCatalogResolveSoundPrivateSlot("mod_x:sfx_a");
	s32 c = assetCatalogResolveSoundPrivateSlot("mod_x:sfx_b");
	REQUIRE(a == b);
	REQUIRE(a != c);
}

TEST_CASE("sound slots: empty / null id rejected; exhaustion loud-fails",
          "[catalog][sound][slots][c3849]") {
	assetCatalogResetCustomSoundSlots();
	REQUIRE(assetCatalogResolveSoundPrivateSlot("") == -1);
	REQUIRE(assetCatalogResolveSoundPrivateSlot(nullptr) == -1);

	for (s32 i = 0; i < SND_CUSTOM_COUNT; i++) {
		char id[32];
		snprintf(id, sizeof(id), "mod_x:sfx_%d", i);
		REQUIRE(assetCatalogResolveSoundPrivateSlot(id) >= SND_CUSTOM_START);
	}
	REQUIRE(assetCatalogResolveSoundPrivateSlot("mod_x:sfx_overflow") == -1);
	/* Dedup still works when full. */
	REQUIRE(assetCatalogResolveSoundPrivateSlot("mod_x:sfx_0") == SND_CUSTOM_START);
}

TEST_CASE("sound slots: reset clears reservations",
          "[catalog][sound][slots][c3849]") {
	assetCatalogResetCustomSoundSlots();
	s32 first = assetCatalogResolveSoundPrivateSlot("mod_x:sfx_a");
	assetCatalogResetCustomSoundSlots();
	REQUIRE(assetCatalogResolveSoundPrivateSlot("mod_x:sfx_b") == first);
}

TEST_CASE("sound slots: range anchors (base adjacency + 11-bit ceiling)",
          "[catalog][sound][slots][c3849]") {
	REQUIRE(SND_CUSTOM_START == SND_BASE_COUNT);
	REQUIRE(SND_CUSTOM_END == SND_CUSTOM_START + SND_CUSTOM_COUNT);
	/* union soundnumhack's id field is 11 bits; the custom range must never
	 * reach the mp3priority/hasconfig bit territory. */
	REQUIRE(SND_CUSTOM_END <= 0x800);
}
