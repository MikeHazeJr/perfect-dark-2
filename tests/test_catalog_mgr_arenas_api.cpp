/*
 * tests/test_catalog_mgr_arenas_api.cpp -- Catalog Gate 3 Arenas F1
 * catalog manager arenas API contract spec.
 *
 * Source under test:
 *   port/src/catalog_mgr_arenas_pure.c (pure validators)
 *
 * The live router (port/src/catalog_mgr_arenas.c) wraps the pure
 * helpers and adds catalog row dereferences. Live router is exercised
 * at runtime; pd-tests pin the contract via the pure layer so the
 * test stays globals-free.
 *
 * Coverage:
 *   - CATALOG_MGR_ARENA_COUNT_PURE pin: 47 (matches g_MpArenas[] size
 *     in src/game/mplayer/setup.c:115 + port/src/server_stubs.c:113
 *     post the 2026-04-26 AllInOne / GEX cull).
 *   - Bounds-check rule: in-range = 1, out-of-range = 0.
 *   - Category-mask predicate: known categories return their bit; "Random"
 *     and unknown categories return 0 (would-recurse + safety).
 *   - Slug extractor: parses "ns:arena_<slug>" shape; returns NULL for
 *     malformed inputs.
 *
 * @SYNC: changes to src/game/mplayer/setup.c::g_MpArenas[] size or
 *        port/src/assetcatalog_base.c::s_ArenaGroupMap[] category names
 *        must be reflected here.
 */

#include "catch.hpp"
#include <cstring>

extern "C" {
#include "catalog_mgr_arenas_pure.h"
}

namespace {
constexpr s32 kArenaCount = 47;
}  /* anonymous namespace */

TEST_CASE("catalog-mgr-arena: count pin == 47",
          "[catalog-mgr-arena][gate3][f1]") {
	REQUIRE(CATALOG_MGR_ARENA_COUNT_PURE == kArenaCount);
}

TEST_CASE("catalog-mgr-arena: bounds-check in-range",
          "[catalog-mgr-arena][gate3][f1]") {
	REQUIRE(catalogMgrArenaIsInRangePure(0) == 1);
	REQUIRE(catalogMgrArenaIsInRangePure(1) == 1);
	REQUIRE(catalogMgrArenaIsInRangePure(12) == 1);  /* end of Dark group */
	REQUIRE(catalogMgrArenaIsInRangePure(13) == 1);  /* start of Solo Missions */
	REQUIRE(catalogMgrArenaIsInRangePure(26) == 1);  /* end of Solo Missions */
	REQUIRE(catalogMgrArenaIsInRangePure(45) == 1);  /* Random Multi meta */
	REQUIRE(catalogMgrArenaIsInRangePure(46) == 1);  /* Random Solo meta */
	REQUIRE(catalogMgrArenaIsInRangePure(CATALOG_MGR_ARENA_COUNT_PURE - 1) == 1);
}

TEST_CASE("catalog-mgr-arena: bounds-check out-of-range",
          "[catalog-mgr-arena][gate3][f1]") {
	REQUIRE(catalogMgrArenaIsInRangePure(-1) == 0);
	REQUIRE(catalogMgrArenaIsInRangePure(-100) == 0);
	REQUIRE(catalogMgrArenaIsInRangePure(CATALOG_MGR_ARENA_COUNT_PURE) == 0);
	REQUIRE(catalogMgrArenaIsInRangePure(CATALOG_MGR_ARENA_COUNT_PURE + 1) == 0);
	REQUIRE(catalogMgrArenaIsInRangePure(1000) == 0);
}

TEST_CASE("catalog-mgr-arena: category-mask known categories",
          "[catalog-mgr-arena][gate3][f1]") {
	REQUIRE(catalogMgrArenaCategoryToMaskPure("Dark") ==
	        CATALOG_MGR_ARENA_RNDMASK_DARK);
	REQUIRE(catalogMgrArenaCategoryToMaskPure("Classic") ==
	        CATALOG_MGR_ARENA_RNDMASK_CLASSIC);
	REQUIRE(catalogMgrArenaCategoryToMaskPure("Bonus") ==
	        CATALOG_MGR_ARENA_RNDMASK_BONUS);
	REQUIRE(catalogMgrArenaCategoryToMaskPure("Solo Missions") ==
	        CATALOG_MGR_ARENA_RNDMASK_SOLOMISSIONS);
}

TEST_CASE("catalog-mgr-arena: category-mask unknown / random returns 0",
          "[catalog-mgr-arena][gate3][f1]") {
	/* "Random" returns 0: a Random meta arena cannot be the resolution
	 * target of another Random pick (would recurse). */
	REQUIRE(catalogMgrArenaCategoryToMaskPure("Random") == 0u);

	/* Unknown / case mismatch / empty / NULL also return 0. */
	REQUIRE(catalogMgrArenaCategoryToMaskPure("dark") == 0u);
	REQUIRE(catalogMgrArenaCategoryToMaskPure("") == 0u);
	REQUIRE(catalogMgrArenaCategoryToMaskPure(NULL) == 0u);
	REQUIRE(catalogMgrArenaCategoryToMaskPure("UnknownGroup") == 0u);
}

TEST_CASE("catalog-mgr-arena: category-mask composites",
          "[catalog-mgr-arena][gate3][f1]") {
	const u32 multi = CATALOG_MGR_ARENA_RNDMASK_MULTI;
	const u32 solo = CATALOG_MGR_ARENA_RNDMASK_SOLO;
	const u32 anymp = CATALOG_MGR_ARENA_RNDMASK_ANYMP;

	REQUIRE((multi & CATALOG_MGR_ARENA_RNDMASK_DARK) != 0u);
	REQUIRE((multi & CATALOG_MGR_ARENA_RNDMASK_CLASSIC) != 0u);
	REQUIRE((multi & CATALOG_MGR_ARENA_RNDMASK_BONUS) != 0u);
	REQUIRE((multi & CATALOG_MGR_ARENA_RNDMASK_SOLOMISSIONS) == 0u);

	REQUIRE((solo & CATALOG_MGR_ARENA_RNDMASK_SOLOMISSIONS) != 0u);
	REQUIRE((solo & CATALOG_MGR_ARENA_RNDMASK_DARK) == 0u);

	REQUIRE((anymp & multi) == multi);
	REQUIRE((anymp & solo) == solo);
}

TEST_CASE("catalog-mgr-arena: slug extractor canonical IDs",
          "[catalog-mgr-arena][gate3][f1]") {
	const char *slug;

	slug = catalogMgrArenaSlugFromIdPure("base:arena_mp_skedar");
	REQUIRE(slug != nullptr);
	REQUIRE(strcmp(slug, "mp_skedar") == 0);

	slug = catalogMgrArenaSlugFromIdPure("base:arena_test_lam");
	REQUIRE(slug != nullptr);
	REQUIRE(strcmp(slug, "test_lam") == 0);

	slug = catalogMgrArenaSlugFromIdPure("mods/foo:arena_mymap");
	REQUIRE(slug != nullptr);
	REQUIRE(strcmp(slug, "mymap") == 0);
}

TEST_CASE("catalog-mgr-arena: slug extractor rejects malformed",
          "[catalog-mgr-arena][gate3][f1]") {
	REQUIRE(catalogMgrArenaSlugFromIdPure(NULL) == nullptr);
	REQUIRE(catalogMgrArenaSlugFromIdPure("") == nullptr);
	REQUIRE(catalogMgrArenaSlugFromIdPure("noprefix") == nullptr);
	REQUIRE(catalogMgrArenaSlugFromIdPure("base:body_carrington") == nullptr);
	REQUIRE(catalogMgrArenaSlugFromIdPure("base:arena_") == nullptr);
}

TEST_CASE("catalog-mgr-arena: slug extractor empty-namespace edge",
          "[catalog-mgr-arena][gate3][f1]") {
	/* Implementation finds the colon then the "arena_" prefix; an empty
	 * namespace is technically permitted ("colon at position 0").  This
	 * is intentional: the predicate is shape-checking, not
	 * namespace-validating. The shape ":arena_x" yields slug "x". */
	const char *slug = catalogMgrArenaSlugFromIdPure(":arena_x");
	REQUIRE(slug != nullptr);
	REQUIRE(strcmp(slug, "x") == 0);
}
