/*
 * tests/test_catalog_mgr_bodies_api.cpp -- Catalog Gate 3 Bodies F1
 * catalog manager bodies API contract spec.
 *
 * Source under test:
 *   port/src/catalog_mgr_bodies_pure.c (pure validators)
 *
 * The live router (port/src/catalog_mgr_bodies.c) wraps the pure helpers
 * and adds g_HeadsAndBodies[] dereferences. Live router is exercised at
 * runtime; pd-tests pin the contract via the pure layer so the test
 * stays globals-free.
 *
 * Coverage:
 *   - CATALOG_MGR_BODY_COUNT_PURE pin: 152 (matches ARRAYCOUNT(g_HeadsAndBodies)
 *     in src/include/data.h:390).
 *   - Bounds-check rule: in-range = ok, out-of-range = miss.  Bodies
 *     have no RANDOM_GENDER sentinel (heads-only).
 *   - Integrated-head predicate: catalogMgrBodyIsIntegratedHeadPure(1) == 1
 *     for self-contained bodies such as Skedar / Dr Caroll / EyeSpy / Chicrob; (0) and any other value
 *     == 0.  This is the S593g warning gate's truth.
 *
 * @SYNC: changes to src/include/data.h (g_HeadsAndBodies size) must be
 *        reflected here.
 */

#include "catch.hpp"

extern "C" {
#include "catalog_mgr_bodies_pure.h"
}

namespace {
constexpr s32 kBodyCount = 152;
}  /* anonymous namespace */

TEST_CASE("catalog-mgr-body: count pin == 152",
          "[catalog-mgr-body][gate3][f1]") {
	REQUIRE(CATALOG_MGR_BODY_COUNT_PURE == kBodyCount);
}

TEST_CASE("catalog-mgr-body: bounds-check in-range",
          "[catalog-mgr-body][gate3][f1]") {
	REQUIRE(catalogMgrBodyIsInRangePure(0) == 1);
	REQUIRE(catalogMgrBodyIsInRangePure(1) == 1);
	REQUIRE(catalogMgrBodyIsInRangePure(63) == 1);  /* near the MP body edge */
	REQUIRE(catalogMgrBodyIsInRangePure(92) == 1);  /* BODY_SKEDAR */
	REQUIRE(catalogMgrBodyIsInRangePure(CATALOG_MGR_BODY_COUNT_PURE - 1) == 1);
	/* c3844 Gate 2: the private custom range [152, TOTAL) is now in-range. */
	REQUIRE(catalogMgrBodyIsInRangePure(CATALOG_MGR_BODY_COUNT_PURE) == 1);
	REQUIRE(catalogMgrBodyIsInRangePure(CATALOG_MGR_BODY_TOTAL_PURE - 1) == 1);
}

TEST_CASE("catalog-mgr-body: bounds-check out-of-range",
          "[catalog-mgr-body][gate3][f1]") {
	REQUIRE(catalogMgrBodyIsInRangePure(-1) == 0);
	REQUIRE(catalogMgrBodyIsInRangePure(-100) == 0);
	/* c3844 Gate 2: TOTAL (= base + custom) is the new upper bound. */
	REQUIRE(catalogMgrBodyIsInRangePure(CATALOG_MGR_BODY_TOTAL_PURE) == 0);
	REQUIRE(catalogMgrBodyIsInRangePure(CATALOG_MGR_BODY_TOTAL_PURE + 1) == 0);
	REQUIRE(catalogMgrBodyIsInRangePure(255) == 0);
}

TEST_CASE("catalog-mgr-body: integrated-head predicate",
          "[catalog-mgr-body][gate3][f1][integrated-head]") {
	/* Self-contained body semantic: unk00_01 == 1 for body slots. */
	REQUIRE(catalogMgrBodyIsIntegratedHeadPure(1) == 1);
	/* Normal body: unk00_01 == 0. */
	REQUIRE(catalogMgrBodyIsIntegratedHeadPure(0) == 0);
	/* Defensive: any non-1 value (corrupt slot, stale read) is NOT
	 * treated as integrated-head. The S593g warning gate would otherwise
	 * silently suppress legitimate head-load failures. */
	REQUIRE(catalogMgrBodyIsIntegratedHeadPure(2) == 0);
	REQUIRE(catalogMgrBodyIsIntegratedHeadPure(255) == 0);
	REQUIRE(catalogMgrBodyIsIntegratedHeadPure(-1) == 0);
}

/* ===========================================================================
 * F2 routing pin: assetcatalog_api.c::catalogGetBody{IsMale,Type,Height,
 * AnimScale,CanVaryHeight,IsComplete,HandFilenum} now route through
 * catalogManagerGetBodyByIndex. Static-text grep against the source file
 * confirms the legacy direct-read pattern is gone for BODY accessors.
 *
 * Path is relative to the project root (matches the heads audit test
 * pattern at tests/test_catalog_mgr_heads_api.cpp). pd-tests is run
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

TEST_CASE("catalog-mgr-body: F2 catalogGetBody* routes through manager",
          "[catalog-mgr-body][gate3][f2]") {
	const std::string src = readSourceFile("port/src/assetcatalog_api.c");
	REQUIRE(!src.empty());

	/* The F2 migration replaced seven direct-read sites with manager calls. */
	REQUIRE(src.find("catalogManagerGetBodyByIndex(bodynum)") != std::string::npos);

	/* Legacy direct-read patterns for BODY accessors are gone. */
	REQUIRE(src.find("g_HeadsAndBodies[bodynum].ismale") == std::string::npos);
	REQUIRE(src.find("g_HeadsAndBodies[bodynum].type") == std::string::npos);
	REQUIRE(src.find("g_HeadsAndBodies[bodynum].height") == std::string::npos);
	REQUIRE(src.find("g_HeadsAndBodies[bodynum].animscale") == std::string::npos);
	REQUIRE(src.find("g_HeadsAndBodies[bodynum].canvaryheight") == std::string::npos);
	REQUIRE(src.find("g_HeadsAndBodies[bodynum].unk00_01") == std::string::npos);
	REQUIRE(src.find("g_HeadsAndBodies[bodynum].handfilenum") == std::string::npos);
}

/* ===========================================================================
 * F3 + F4 cache pin: body modeldef cache moved to the manager pool.
 * catalogGetBodyModeldef + catalogResetBodyModeldef route through the
 * manager. catalogResetAllModeldefs no longer walks the legacy
 * g_HeadsAndBodies[].modeldef array; both head and body caches are
 * cleared via their respective managers.
 * =========================================================================== */

TEST_CASE("catalog-mgr-body: F3 modeldef accessors route through manager",
          "[catalog-mgr-body][gate3][f3]") {
	const std::string src = readSourceFile("port/src/assetcatalog_api.c");
	REQUIRE(!src.empty());

	REQUIRE(src.find("catalogManagerGetBodyModeldef(bodynum)") != std::string::npos);
	REQUIRE(src.find("catalogManagerResetBodyModeldef(bodynum)") != std::string::npos);

	/* Legacy modeldef cache slot reads are gone. */
	REQUIRE(src.find("g_HeadsAndBodies[bodynum].modeldef") == std::string::npos);
}

TEST_CASE("catalog-mgr-body: F4 catalogResetAllModeldefs uses manager calls",
          "[catalog-mgr-body][gate3][f4]") {
	const std::string src = readSourceFile("port/src/assetcatalog_api.c");
	REQUIRE(!src.empty());

	REQUIRE(src.find("catalogManagerResetAllHeadModeldefs()") != std::string::npos);
	REQUIRE(src.find("catalogManagerResetAllBodyModeldefs()") != std::string::npos);

	/* The legacy walk loop is gone. */
	REQUIRE(src.find("g_HeadsAndBodies[i].modeldef = NULL") == std::string::npos);
}

/* ===========================================================================
 * F13 grep-guard pin: live BODY-data reads outside the catalog API and the
 * manager's parity-period mirror are gone. Allowed sites:
 *   - port/src/assetcatalog_api.c    (catalog API)
 *   - port/src/assetcatalog_base.c   (registration; iterates g_HeadsAndBodies
 *     and g_MpBodies at startup)
 *   - port/src/assetcatalog_base_extended.c (B-275 hand model registration;
 *     reads g_HeadsAndBodies[i].handfilenum directly per audit H.2)
 *   - port/src/catalog_mgr_bodies.c  (manager pool; parity-period mirror)
 *   - port/src/loader_pool.c         (loader pool; populates s_BodiesPool)
 *   - port/src/modelcatalog.c        (validation walk; per audit H.3)
 *   - src/game/modeldata/robot.c     (data definition site)
 *   - src/include/data.h             (extern decl)
 *   - src/include/types.h            (struct headorbody decl)
 *   - bounds-check sites in body.c, mplayer/setup.c, training.c
 * Anywhere else reintroducing `g_HeadsAndBodies[b].<body-field>` is a
 * regression.
 * =========================================================================== */

TEST_CASE("catalog-mgr-body: F13 no new direct body-field reads in selectors",
          "[catalog-mgr-body][gate3][f13]") {
	/* The set of files we expect to NEVER reintroduce a direct
	 * g_HeadsAndBodies[b].<body-field> read. */
	const char *files[] = {
		"port/fast3d/pdgui_menu_agentcreate.cpp",
		"port/fast3d/pdgui_menu_botsetup.cpp",
		"port/fast3d/pdgui_menu_playerconfig.cpp",
		"port/fast3d/pdgui_menu_room.cpp",
		"src/game/bot.c",
		"src/game/botmgr.c",
		"src/game/chraction.c",
		"src/game/player.c",
		"port/src/net/netmanifest.c",
		"port/src/swarm_test.c",
	};
	const char *bad_patterns[] = {
		"g_HeadsAndBodies[bodynum].ismale",
		"g_HeadsAndBodies[bodynum].type",
		"g_HeadsAndBodies[bodynum].height",
		"g_HeadsAndBodies[bodynum].animscale",
		"g_HeadsAndBodies[bodynum].canvaryheight",
		"g_HeadsAndBodies[bodynum].unk00_01",
		"g_HeadsAndBodies[bodynum].handfilenum",
		"g_HeadsAndBodies[bodynum].scale",
		"g_HeadsAndBodies[bodynum].filenum",
		"g_HeadsAndBodies[bodynum].modeldef",
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
