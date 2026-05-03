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

/* ===========================================================================
 * F2 routing pin: assetcatalog_api.c::catalogGetHeadIsMale/Type/Height now
 * route through catalogManagerGetHeadByIndex. Static-text grep against the
 * source file confirms the legacy direct-read pattern is gone for HEAD
 * accessors, while body accessors keep the legacy pattern (bodies session
 * migrates them later).
 *
 * Path is relative to the project root (matches the weapons audit test
 * pattern at tests/test_weapon_direct_reads_audit.cpp). pd-tests is run
 * from the project root so the relative path resolves regardless of
 * worktree vs. main-copy build location.
 * =========================================================================== */

#include <fstream>
#include <sstream>
#include <string>

namespace {
std::string readSourceFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}
}  /* anonymous namespace */

TEST_CASE("catalog-mgr-head: F2 catalogGetHead* routes through manager",
          "[catalog-mgr-head][gate3][f2]") {
	const std::string src = readSourceFile("port/src/assetcatalog_api.c");
	REQUIRE(!src.empty());

	/* The F2 migration replaced three direct-read sites with manager calls.
	 * Grep for the routed pattern. */
	REQUIRE(src.find("catalogManagerGetHeadByIndex(headnum)") != std::string::npos);

	/* The legacy direct-read patterns for HEAD accessors are gone.  Body
	 * counterparts keep the legacy pattern (bodies session migrates them). */
	REQUIRE(src.find("g_HeadsAndBodies[headnum].ismale") == std::string::npos);
	REQUIRE(src.find("g_HeadsAndBodies[headnum].type") == std::string::npos);
	REQUIRE(src.find("g_HeadsAndBodies[headnum].height") == std::string::npos);
}

/* ===========================================================================
 * F6 retirement pin: g_MpMaleHeads / g_MpFemaleHeads were retired from
 * src/game/mplayer/mplayer.c. The retirement comment block stays in place
 * but the array literals are gone.
 * =========================================================================== */

TEST_CASE("catalog-mgr-head: F6 g_MpMaleHeads / g_MpFemaleHeads retired",
          "[catalog-mgr-head][gate3][f6]") {
	const std::string src = readSourceFile("src/game/mplayer/mplayer.c");
	REQUIRE(!src.empty());

	/* The static array definitions are gone. */
	REQUIRE(src.find("u32 g_MpMaleHeads[] = {") == std::string::npos);
	REQUIRE(src.find("u32 g_MpFemaleHeads[] = {") == std::string::npos);

	/* Manager helpers are referenced. */
	REQUIRE(src.find("catalogManagerHeadPickRandomMale()") != std::string::npos);
	REQUIRE(src.find("catalogManagerHeadPickRandomFemale()") != std::string::npos);
}

/* ===========================================================================
 * F13 grep-guard pin: live HEAD-data reads outside the catalog API and the
 * manager's parity-period mirror are gone. Allowed sites:
 *   - port/src/assetcatalog_api.c    (catalog API; bodies-side reads keep
 *     direct g_HeadsAndBodies reads until bodies session migrates)
 *   - port/src/assetcatalog_base.c   (registration; iterates g_HeadsAndBodies
 *     and g_MpHeads at startup)
 *   - port/src/catalog_mgr_heads.c   (manager pool; parity-period mirror)
 *   - port/src/loader_pool.c         (loader pool; populates s_HeadsPool)
 *   - src/game/modeldata/robot.c     (data definition site)
 *   - src/include/data.h             (extern decl)
 *   - src/include/types.h            (struct headorbody decl)
 *   - bounds-check sites in body.c, mplayer/setup.c, training.c
 * Anywhere else reintroducing `g_HeadsAndBodies[h].<head-field>` is a
 * regression.
 * =========================================================================== */

TEST_CASE("catalog-mgr-head: F13 no new direct head-field reads in selectors",
          "[catalog-mgr-head][gate3][f13]") {
	/* The set of files we expect to NEVER reintroduce a direct
	 * g_HeadsAndBodies[h].<headfield> read. */
	const char *files[] = {
		"port/fast3d/pdgui_menu_agentcreate.cpp",
		"port/fast3d/pdgui_menu_botsetup.cpp",
		"port/fast3d/pdgui_menu_playerconfig.cpp",
		"port/fast3d/pdgui_menu_room.cpp",
		"src/game/chraction.c",
		"src/game/player.c",
		"port/src/net/netmanifest.c",
	};
	const char *bad_patterns[] = {
		"g_HeadsAndBodies[headnum].ismale",
		"g_HeadsAndBodies[headnum].type",
		"g_HeadsAndBodies[headnum].height",
		"g_HeadsAndBodies[headnum].scale",
		"g_HeadsAndBodies[headnum].animscale",
		"g_HeadsAndBodies[headnum].filenum",
		"g_HeadsAndBodies[headnum].modeldef",
	};
	for (const char *path : files) {
		const std::string src = readSourceFile(path);
		REQUIRE(!src.empty());
		for (const char *bad : bad_patterns) {
			INFO("file=" << path << " pattern=" << bad);
			REQUIRE(src.find(bad) == std::string::npos);
		}
	}
}
