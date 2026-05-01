/*
 * tests/test_catalog_mgr_heads_api.cpp -- Catalog Gate 3 F1 catalog
 * manager heads API contract spec.
 *
 * Source under test:
 *   port/src/catalog_mgr_heads_pure.c (pure validators)
 *
 * The live router (port/src/catalog_mgr_heads.c) wraps the pure helpers
 * and adds g_HeadsAndBodies[] dereferences. Live router is exercised at
 * runtime; pd-tests pin the contract via the pure layer so the test
 * stays globals-free.
 *
 * Coverage:
 *   - CATALOG_MGR_HEAD_COUNT_PURE pin: 152 (matches ARRAYCOUNT(g_HeadsAndBodies)
 *     in src/include/data.h:390).
 *   - HEAD_RANDOM_GENDER pin: 1000 (matches the constants.h sentinel).
 *   - Bounds-check rule: in-range = ok, out-of-range = miss, sentinel = miss.
 *   - Gender-pool predicate: must be (ismale matches want_male) AND
 *     (unk00_01 == 1). Body slots and uninitialised slots are excluded.
 *
 * @SYNC: changes to src/include/data.h (g_HeadsAndBodies size) or
 *        src/include/constants.h (HEAD_RANDOM_GENDER sentinel) must be
 *        reflected here.
 */

#include "catch.hpp"

extern "C" {
#include "catalog_mgr_heads_pure.h"
}

namespace {
constexpr s32 kHeadCount = 152;
constexpr s32 kHeadRandomGender = 1000;
}  /* anonymous namespace */

TEST_CASE("catalog-mgr-head: count pin == 152",
          "[catalog-mgr-head][gate3][f1]") {
	REQUIRE(CATALOG_MGR_HEAD_COUNT_PURE == kHeadCount);
}

TEST_CASE("catalog-mgr-head: random-gender sentinel pin",
          "[catalog-mgr-head][gate3][f1]") {
	REQUIRE(CATALOG_MGR_HEAD_RANDOM_GENDER_PURE == kHeadRandomGender);
}

TEST_CASE("catalog-mgr-head: bounds-check in-range",
          "[catalog-mgr-head][gate3][f1]") {
	REQUIRE(catalogMgrHeadIsInRangePure(0) == 1);
	REQUIRE(catalogMgrHeadIsInRangePure(1) == 1);
	REQUIRE(catalogMgrHeadIsInRangePure(75) == 1);  /* near the MP head edge */
	REQUIRE(catalogMgrHeadIsInRangePure(CATALOG_MGR_HEAD_COUNT_PURE - 1) == 1);
}

TEST_CASE("catalog-mgr-head: bounds-check out-of-range",
          "[catalog-mgr-head][gate3][f1]") {
	REQUIRE(catalogMgrHeadIsInRangePure(-1) == 0);
	REQUIRE(catalogMgrHeadIsInRangePure(-100) == 0);
	REQUIRE(catalogMgrHeadIsInRangePure(CATALOG_MGR_HEAD_COUNT_PURE) == 0);
	REQUIRE(catalogMgrHeadIsInRangePure(CATALOG_MGR_HEAD_COUNT_PURE + 1) == 0);
	REQUIRE(catalogMgrHeadIsInRangePure(255) == 0);
}

TEST_CASE("catalog-mgr-head: HEAD_RANDOM_GENDER sentinel rejected",
          "[catalog-mgr-head][gate3][f1]") {
	REQUIRE(catalogMgrHeadIsInRangePure(CATALOG_MGR_HEAD_RANDOM_GENDER_PURE) == 0);
	REQUIRE(catalogMgrHeadIsRandomGenderSentinelPure(
		CATALOG_MGR_HEAD_RANDOM_GENDER_PURE) == 1);
	REQUIRE(catalogMgrHeadIsRandomGenderSentinelPure(0) == 0);
	REQUIRE(catalogMgrHeadIsRandomGenderSentinelPure(75) == 0);
}

TEST_CASE("catalog-mgr-head: gender-pool eligible male",
          "[catalog-mgr-head][gate3][f1]") {
	/* ismale=1, unk00_01=1, want_male=1 -> eligible */
	REQUIRE(catalogMgrHeadIsGenderPoolEligiblePure(1, 1, 1) == 1);
	/* ismale=0, unk00_01=1, want_male=1 -> not eligible */
	REQUIRE(catalogMgrHeadIsGenderPoolEligiblePure(0, 1, 1) == 0);
}

TEST_CASE("catalog-mgr-head: gender-pool eligible female",
          "[catalog-mgr-head][gate3][f1]") {
	/* ismale=0, unk00_01=1, want_male=0 -> eligible */
	REQUIRE(catalogMgrHeadIsGenderPoolEligiblePure(0, 1, 0) == 1);
	/* ismale=1, unk00_01=1, want_male=0 -> not eligible */
	REQUIRE(catalogMgrHeadIsGenderPoolEligiblePure(1, 1, 0) == 0);
}

TEST_CASE("catalog-mgr-head: gender-pool excludes body slots (unk00_01==0)",
          "[catalog-mgr-head][gate3][f1]") {
	/* unk00_01=0 means body slot or sentinel; never eligible */
	REQUIRE(catalogMgrHeadIsGenderPoolEligiblePure(1, 0, 1) == 0);
	REQUIRE(catalogMgrHeadIsGenderPoolEligiblePure(0, 0, 1) == 0);
	REQUIRE(catalogMgrHeadIsGenderPoolEligiblePure(1, 0, 0) == 0);
	REQUIRE(catalogMgrHeadIsGenderPoolEligiblePure(0, 0, 0) == 0);
}
