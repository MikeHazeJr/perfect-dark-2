/*
 * tests/test_catalog_checked.cpp -- INV-1 (player-init-architectural-fixes
 * 2026-04-26): pure validators backing the `_Checked` catalog accessor
 * variants.
 *
 * Source under test:
 *   port/src/catalog_checked.c::catalogCheckedValidateIndex
 *   port/src/catalog_checked.c::catalogCheckedValidateSlot
 *   port/src/catalog_checked.c::catalogCheckedResultName
 *
 * The bug shape, restated for the regression record. The legacy catalog
 * accessors silently return 0 / 1.0f / NULL on out-of-bounds index or
 * in-bounds-but-unpopulated slot. The B-219 v2 narrative ("user picked
 * base:remotemine but saw Falcon (Silenced)") is direct evidence that
 * these silent zeros leak into spawn-with-weapon resolution and the
 * player ends up unarmed without diagnosis.
 *
 * INV-1 makes catalog access loud on miss. The wrappers in
 * port/src/assetcatalog_api.c delegate to the pure validators here for
 * the bounds + sentinel decision; the tests below pin that decision so
 * any future refactor that accidentally widens or narrows the OK
 * conditions is caught.
 *
 * The wrappers themselves (which read globals + emit log + return s32)
 * are not directly testable in pd-tests without dragging in g_MpWeapons
 * / g_HeadsAndBodies / sysLogPrintf. The validators ARE testable because
 * they depend only on their arguments.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "catalog_checked.h"
}

static std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());

	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

TEST_CASE("catalogCheckedValidateIndex: in-bounds is OK",
          "[catalog][checked][regression]") {
	REQUIRE(catalogCheckedValidateIndex(0,   8) == CATALOG_CHECKED_OK);
	REQUIRE(catalogCheckedValidateIndex(1,   8) == CATALOG_CHECKED_OK);
	REQUIRE(catalogCheckedValidateIndex(7,   8) == CATALOG_CHECKED_OK);
	REQUIRE(catalogCheckedValidateIndex(127, 128) == CATALOG_CHECKED_OK);
}

TEST_CASE("catalogCheckedValidateIndex: negative is OOB",
          "[catalog][checked][regression]") {
	REQUIRE(catalogCheckedValidateIndex(-1,  8)   == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateIndex(-100, 8)  == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateIndex(-1,  152) == CATALOG_CHECKED_OOB);
}

TEST_CASE("catalogCheckedValidateIndex: at-or-past count is OOB",
          "[catalog][checked][regression]") {
	REQUIRE(catalogCheckedValidateIndex(8,   8)   == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateIndex(9,   8)   == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateIndex(152, 152) == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateIndex(1000, 8)  == CATALOG_CHECKED_OOB);
}

TEST_CASE("catalogCheckedValidateIndex: empty array is always OOB",
          "[catalog][checked][regression]") {
	/* count = 0 means no valid slots; every index is OOB. */
	REQUIRE(catalogCheckedValidateIndex(0,  0) == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateIndex(-1, 0) == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateIndex(1,  0) == CATALOG_CHECKED_OOB);
}

TEST_CASE("catalogCheckedValidateSlot: populated slot is OK",
          "[catalog][checked][regression]") {
	/* Sentinel != 0 means the catalog has registered this slot. */
	REQUIRE(catalogCheckedValidateSlot(0,   152, 0x1234) == CATALOG_CHECKED_OK);
	REQUIRE(catalogCheckedValidateSlot(50,  152, 0x0001) == CATALOG_CHECKED_OK);
	REQUIRE(catalogCheckedValidateSlot(151, 152, 0xFFFF) == CATALOG_CHECKED_OK);
}

TEST_CASE("catalogCheckedValidateSlot: in-bounds with sentinel zero is UNPOPULATED",
          "[catalog][checked][regression]") {
	/* Sentinel == 0 marks an unpopulated slot. The catalog convention is
	 * filenum == 0 means "this g_HeadsAndBodies[] slot was never
	 * registered by catalog load". Reading other fields of such a slot
	 * returns BSS / heap-garbage; the wrapper must surface a miss. */
	REQUIRE(catalogCheckedValidateSlot(0,   152, 0) == CATALOG_CHECKED_UNPOPULATED);
	REQUIRE(catalogCheckedValidateSlot(100, 152, 0) == CATALOG_CHECKED_UNPOPULATED);
	REQUIRE(catalogCheckedValidateSlot(151, 152, 0) == CATALOG_CHECKED_UNPOPULATED);
}

TEST_CASE("catalogCheckedValidateSlot: OOB index is OOB regardless of sentinel",
          "[catalog][checked][regression]") {
	/* OOB beats UNPOPULATED in the failure mode. Caller hit OOB first;
	 * the sentinel value is unread. */
	REQUIRE(catalogCheckedValidateSlot(-1,  152, 0xABCD) == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateSlot(152, 152, 0xABCD) == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateSlot(-1,  152, 0)      == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateSlot(200, 152, 0)      == CATALOG_CHECKED_OOB);
}

TEST_CASE("catalogCheckedValidateSlot: empty array always OOB",
          "[catalog][checked][regression]") {
	REQUIRE(catalogCheckedValidateSlot(0, 0, 0)      == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateSlot(0, 0, 0xFFFF) == CATALOG_CHECKED_OOB);
}

TEST_CASE("catalogCheckedResultName: stable strings for log output",
          "[catalog][checked][regression]") {
	/* Pinned because log harvesters key off these strings. Renaming or
	 * dropping a code without updating the harvester would silently
	 * break analytics. */
	REQUIRE(std::string(catalogCheckedResultName(CATALOG_CHECKED_OK))          == "ok");
	REQUIRE(std::string(catalogCheckedResultName(CATALOG_CHECKED_OOB))         == "oob");
	REQUIRE(std::string(catalogCheckedResultName(CATALOG_CHECKED_UNPOPULATED)) == "unpopulated");

	/* Out-of-band enum values fall through to "unknown" rather than
	 * indexing into invalid memory. */
	REQUIRE(std::string(catalogCheckedResultName((catalog_checked_result_e)99)) == "unknown");
	REQUIRE(std::string(catalogCheckedResultName((catalog_checked_result_e)-1)) == "unknown");
}

TEST_CASE("catalogCheckedValidateSlot: spawn-critical body slot examples",
          "[catalog][checked][regression]") {
	/* Concrete examples from the audit findings:
	 *   bodynum 0 (DJ Bond) is always populated -- filenum non-zero.
	 *   bodynum 100 (mod body that loaded) populated -- filenum non-zero.
	 *   bodynum 100 (slot never registered) unpopulated -- filenum 0.
	 *   bodynum 152 (past array dim) OOB.
	 *   bodynum -1 (negative from upstream miscalc) OOB.
	 *
	 * These cases are what the caller in body0f02ce8c sees after a
	 * catalog miss returns OOB / unpopulated to the legacy accessor. */
	const s32 BODY_ARRAY = 152;

	REQUIRE(catalogCheckedValidateSlot(0,   BODY_ARRAY, 0x0042) == CATALOG_CHECKED_OK);
	REQUIRE(catalogCheckedValidateSlot(100, BODY_ARRAY, 0x1337) == CATALOG_CHECKED_OK);
	REQUIRE(catalogCheckedValidateSlot(100, BODY_ARRAY, 0)      == CATALOG_CHECKED_UNPOPULATED);
	REQUIRE(catalogCheckedValidateSlot(152, BODY_ARRAY, 0x1234) == CATALOG_CHECKED_OOB);
	REQUIRE(catalogCheckedValidateSlot(-1,  BODY_ARRAY, 0x1234) == CATALOG_CHECKED_OOB);
}

TEST_CASE("body0f02ce8c gates head catalog misses before rw allocation",
          "[catalog][checked][static][regression]") {
	const std::string body = readTextFile("src/game/body.c");

	REQUIRE(body.find("catalogGetHeadModeldefChecked(headnum, &headmodeldef)") != std::string::npos);
	REQUIRE(body.find("modelAllocateRwData(headmodeldef);\n\n\t\t\t\t\t/* B-163") == std::string::npos);
	REQUIRE(body.find(
		"if (headmodeldef != NULL) {\n"
		"\t\t\t\t\t\tmodelAllocateRwData(headmodeldef);\n"
		"\t\t\t\t\t\tbodymodeldef->rwdatalen += headmodeldef->rwdatalen;") != std::string::npos);
}

TEST_CASE("body0f02ce8c refuses unresolved body identity instead of slot-zero fallback",
          "[catalog][checked][static][regression]") {
	const std::string body = readTextFile("src/game/body.c");

	REQUIRE(body.find("body_source_id = catalogBodyIdByBodynum(bodynum);") !=
	        std::string::npos);
	REQUIRE(body.find("if (!body_source_id)") != std::string::npos);
	REQUIRE(body.find("BODY.IDENTITY: bodynum=%d has no catalog body; refusing slot-0 visual fallback") !=
	        std::string::npos);
	REQUIRE(body.find("return NULL;\n\t}") <
	        body.find("catalogGetBodyScaleChecked(bodynum, &scaleRaw)"));
	REQUIRE(body.find("substituting bodynum=0") == std::string::npos);
	REQUIRE(body.find("bodynum = 0;") == std::string::npos);
}

TEST_CASE("body0f02ce8c requires a headspot node before attaching a head",
          "[catalog][checked][static][regression]") {
	const std::string body = readTextFile("src/game/body.c");

	REQUIRE(body.find("if (headmodeldef && node != NULL && !catalogGetBodyIsComplete(bodynum))") != std::string::npos);
	REQUIRE(body.find("missing headspot for bodynum") != std::string::npos);
}

TEST_CASE("catalogHealthShouldFatal: only fails on miss AND enforcement",
          "[catalog][checked][regression][c3844]") {
	/* Gate 5 (c3844): the formerly write-only g_CatalogFailure flag now has a
	 * consumer (catalogAssertHealthy). The escalation decision lives in this
	 * pure predicate so it can be pinned: a hard fail requires BOTH a pending
	 * miss AND active source-only enforcement. In normal play (enforcement
	 * off) a miss is loud-reported and tolerated, not fatal, so no live brick
	 * is introduced before every per-family source gate closes. */
	REQUIRE(catalogHealthShouldFatal(0, 0) == 0);   /* clean, no enforcement */
	REQUIRE(catalogHealthShouldFatal(0, 1) == 0);   /* clean, enforcement on */
	REQUIRE(catalogHealthShouldFatal(1, 0) == 0);   /* miss, enforcement off -> loud only */
	REQUIRE(catalogHealthShouldFatal(1, 1) == 1);   /* miss + enforcement -> hard fail */
	/* Any nonzero failure / enforcement value counts as set. */
	REQUIRE(catalogHealthShouldFatal(7, 3) == 1);
	REQUIRE(catalogHealthShouldFatal(-1, 1) == 1);
}

TEST_CASE("catalogAssertHealthy consumes g_CatalogFailure at stage-load entry",
          "[catalog][checked][static][regression][c3844]") {
	const std::string api = readTextFile("port/src/assetcatalog_api.c");
	const std::string lv  = readTextFile("src/game/lv.c");

	/* The consumer exists, reports loudly, escalates under enforcement via the
	 * pure predicate, and clears -- the flag is no longer write-only. */
	REQUIRE(api.find("s32 catalogAssertHealthy(const char *checkpoint)") != std::string::npos);
	REQUIRE(api.find("CATALOG.HEALTH: FAIL at") != std::string::npos);
	REQUIRE(api.find("catalogHealthShouldFatal(g_CatalogFailure") != std::string::npos);
	REQUIRE(api.find("assetSourceDebugOnlyType() != ASSET_NONE") != std::string::npos);
	REQUIRE(api.find("catalogClearHealth();") != std::string::npos);

	/* The flag is consumed at a real phase boundary, not left write-only. */
	REQUIRE(lv.find("catalogAssertHealthy(\"stage-load-entry\")") != std::string::npos);
}
