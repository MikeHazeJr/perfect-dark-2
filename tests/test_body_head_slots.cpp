/*
 * tests/test_body_head_slots.cpp -- c3844 Gate 2: catalog-owned private
 * body/head runtime-slot allocator.
 *
 * Source under test: port/src/assetcatalog_body_head_slots.c
 *   assetCatalogResolveBodyPrivateSlot / ...HeadPrivateSlot
 *   assetCatalogResetCustomBodyHeadSlots
 *
 * The allocator is the analog of the proven weapon custom-slot allocator. It
 * maps a fully-new custom .pdbody/.pdhead catalog ID (one with no legacy
 * bodynum/headnum) to a private runtime slot above the 152 base ceiling so the
 * existing integer render path assembles it from public mesh source. Pinned
 * behaviors: in-range, dedup-by-id, distinct ids get distinct slots, empty id
 * rejected, exhaustion loud-fails (-1), reset clears.
 */

#include "catch.hpp"

#include <cstdio>

extern "C" {
#include "assetcatalog_body_head_slots.h"
#include "catalog_mgr_bodies_pure.h"
#include "catalog_mgr_heads_pure.h"
}

/* The pure constants are defined equal to the manager constants
 * (catalog_mgr_bodies.h/heads.h) by construction; use the light pure headers
 * here so the test stays free of the heavy manager includes. */
#define CATALOG_MGR_BODY_CUSTOM_START CATALOG_MGR_BODY_COUNT_PURE
#define CATALOG_MGR_BODY_TOTAL        CATALOG_MGR_BODY_TOTAL_PURE
#define CATALOG_MGR_BODY_CUSTOM_COUNT CATALOG_MGR_BODY_CUSTOM_COUNT_PURE
#define CATALOG_MGR_HEAD_CUSTOM_START CATALOG_MGR_HEAD_COUNT_PURE
#define CATALOG_MGR_HEAD_TOTAL        CATALOG_MGR_HEAD_TOTAL_PURE
#define CATALOG_MGR_HEAD_CUSTOM_COUNT CATALOG_MGR_HEAD_CUSTOM_COUNT_PURE

TEST_CASE("body slots: allocation is in the private custom range",
          "[catalog][bodyhead][slots][c3844]") {
	assetCatalogResetCustomBodyHeadSlots();
	s32 slot = assetCatalogResolveBodyPrivateSlot("mod_x:body_needler_grunt");
	REQUIRE(slot >= CATALOG_MGR_BODY_CUSTOM_START);
	REQUIRE(slot < CATALOG_MGR_BODY_TOTAL);
}

TEST_CASE("body slots: same id dedups to the same slot",
          "[catalog][bodyhead][slots][c3844]") {
	assetCatalogResetCustomBodyHeadSlots();
	s32 a = assetCatalogResolveBodyPrivateSlot("mod_x:body_a");
	s32 b = assetCatalogResolveBodyPrivateSlot("mod_x:body_a");
	REQUIRE(a == b);
}

TEST_CASE("body slots: distinct ids get distinct slots",
          "[catalog][bodyhead][slots][c3844]") {
	assetCatalogResetCustomBodyHeadSlots();
	s32 a = assetCatalogResolveBodyPrivateSlot("mod_x:body_a");
	s32 b = assetCatalogResolveBodyPrivateSlot("mod_x:body_b");
	REQUIRE(a != b);
	REQUIRE(a >= CATALOG_MGR_BODY_CUSTOM_START);
	REQUIRE(b >= CATALOG_MGR_BODY_CUSTOM_START);
}

TEST_CASE("body slots: empty / null id is rejected",
          "[catalog][bodyhead][slots][c3844]") {
	assetCatalogResetCustomBodyHeadSlots();
	REQUIRE(assetCatalogResolveBodyPrivateSlot("") == -1);
	REQUIRE(assetCatalogResolveBodyPrivateSlot(nullptr) == -1);
}

TEST_CASE("body slots: exhaustion loud-fails with -1",
          "[catalog][bodyhead][slots][c3844]") {
	assetCatalogResetCustomBodyHeadSlots();
	/* Fill every private slot with a distinct id. */
	for (s32 i = 0; i < CATALOG_MGR_BODY_CUSTOM_COUNT; i++) {
		char id[32];
		snprintf(id, sizeof(id), "mod_x:body_%d", i);
		s32 slot = assetCatalogResolveBodyPrivateSlot(id);
		REQUIRE(slot >= CATALOG_MGR_BODY_CUSTOM_START);
		REQUIRE(slot < CATALOG_MGR_BODY_TOTAL);
	}
	/* One more distinct id has nowhere to go. */
	REQUIRE(assetCatalogResolveBodyPrivateSlot("mod_x:body_overflow") == -1);
	/* A previously-seen id still dedups even when full. */
	REQUIRE(assetCatalogResolveBodyPrivateSlot("mod_x:body_0")
	        == CATALOG_MGR_BODY_CUSTOM_START);
}

TEST_CASE("body slots: reset clears reservations",
          "[catalog][bodyhead][slots][c3844]") {
	assetCatalogResetCustomBodyHeadSlots();
	s32 first = assetCatalogResolveBodyPrivateSlot("mod_x:body_a");
	assetCatalogResetCustomBodyHeadSlots();
	/* After reset a different id can take the first slot again. */
	s32 reused = assetCatalogResolveBodyPrivateSlot("mod_x:body_b");
	REQUIRE(reused == first);
}

TEST_CASE("head slots: independent range, dedup, exhaustion",
          "[catalog][bodyhead][slots][c3844]") {
	assetCatalogResetCustomBodyHeadSlots();
	s32 h = assetCatalogResolveHeadPrivateSlot("mod_x:head_a");
	REQUIRE(h >= CATALOG_MGR_HEAD_CUSTOM_START);
	REQUIRE(h < CATALOG_MGR_HEAD_TOTAL);
	REQUIRE(assetCatalogResolveHeadPrivateSlot("mod_x:head_a") == h);

	for (s32 i = 0; i < CATALOG_MGR_HEAD_CUSTOM_COUNT; i++) {
		char id[32];
		snprintf(id, sizeof(id), "mod_x:head_%d", i);
		(void)assetCatalogResolveHeadPrivateSlot(id);
	}
	REQUIRE(assetCatalogResolveHeadPrivateSlot("mod_x:head_overflow") == -1);
}

TEST_CASE("body and head slot domains are independent",
          "[catalog][bodyhead][slots][c3844]") {
	assetCatalogResetCustomBodyHeadSlots();
	/* Allocating a body slot does not consume head slots and vice versa. */
	s32 b = assetCatalogResolveBodyPrivateSlot("mod_x:shared_name");
	s32 h = assetCatalogResolveHeadPrivateSlot("mod_x:shared_name");
	REQUIRE(b == CATALOG_MGR_BODY_CUSTOM_START);
	REQUIRE(h == CATALOG_MGR_HEAD_CUSTOM_START);
}
