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

TEST_CASE("Gate 5: body/head field accessors surface in-range unregistered misses",
          "[catalog][checked][static][regression][c3844]") {
	/* The body/head field accessors used to return a zeroed field for an
	 * in-range but unregistered slot, masking a catalog miss. They now route
	 * through s_bodyFieldRecordChecked / s_headFieldRecordChecked which set
	 * g_CatalogFailure on a genuine miss. */
	const std::string api = readTextFile("port/src/assetcatalog_api.c");

	REQUIRE(api.find("s_bodyFieldRecordChecked(s32 bodynum, const char *accessor)") != std::string::npos);
	REQUIRE(api.find("s_headFieldRecordChecked(s32 headnum, const char *accessor)") != std::string::npos);

	/* The "registered" discriminator must include catalog_id, not just
	 * filenum: B-909 custom bodies/heads use public mesh source with
	 * filenum==0 and must NOT be mis-flagged as unregistered. */
	REQUIRE(api.find("b->filenum != 0 || b->catalog_id[0] != '\\0'") != std::string::npos);
	REQUIRE(api.find("h->filenum != 0 || h->catalog_id[0] != '\\0'") != std::string::npos);

	/* A genuine in-range-unregistered read sets the health flag + message. */
	REQUIRE(api.find("CATALOG-MISS: %s bodynum=%d in-range but unregistered") != std::string::npos);
	REQUIRE(api.find("CATALOG-MISS: %s headnum=%d in-range but unregistered") != std::string::npos);

	/* All 10 field accessors route through the checked helpers (spot-check
	 * the first/last of each family). */
	REQUIRE(api.find("s_bodyFieldRecordChecked(bodynum, \"catalogGetBodyIsMale\")") != std::string::npos);
	REQUIRE(api.find("s_bodyFieldRecordChecked(bodynum, \"catalogGetBodyHandFilenum\")") != std::string::npos);
	REQUIRE(api.find("s_headFieldRecordChecked(headnum, \"catalogGetHeadIsMale\")") != std::string::npos);
	REQUIRE(api.find("s_headFieldRecordChecked(headnum, \"catalogGetHeadHeight\")") != std::string::npos);
}

TEST_CASE("Gate 5: checked body/head spawn accessors accept private custom slots",
          "[catalog][checked][static][regression][c3844]") {
	/* Custom body/head slots live beyond the authored 0..151 base range. The
	 * checked spawn/render accessors must validate against the catalog manager
	 * TOTAL bounds and the source-backed manager records, not body/head authored
	 * tables capped at 152. */
	const std::string api = readTextFile("port/src/assetcatalog_api.c");

	REQUIRE(api.find("s_bodyFieldRecordChecked(bodynum, \"catalogGetBodyScaleChecked\")") !=
	        std::string::npos);
	REQUIRE(api.find("s_bodyFieldRecordChecked(bodynum, \"catalogGetBodyAnimScaleChecked\")") !=
	        std::string::npos);
	REQUIRE(api.find("s_bodyFieldRecordChecked(bodynum, \"catalogGetBodyHandFilenumChecked\")") !=
	        std::string::npos);
	REQUIRE(api.find("s_bodyFieldRecordChecked(bodynum, \"catalogGetBodyModeldefChecked\")") !=
	        std::string::npos);
	REQUIRE(api.find("s_headFieldRecordChecked(headnum, \"catalogGetHeadModeldefChecked\")") !=
	        std::string::npos);

	REQUIRE(api.find("catalogCheckedValidateSlot(bodynum, CATALOG_MGR_BODY_TOTAL, b ? 1 : 0)") !=
	        std::string::npos);
	REQUIRE(api.find("catalogCheckedValidateSlot(headnum, CATALOG_MGR_HEAD_TOTAL, h ? 1 : 0)") !=
	        std::string::npos);
	REQUIRE(api.find("catalogCheckedValidateSlot(bodynum, 152") == std::string::npos);
	REQUIRE(api.find("catalogCheckedValidateSlot(headnum, 152") == std::string::npos);
	REQUIRE(api.find("bodyDataLookupByBodynum(bodynum)") == std::string::npos);
	REQUIRE(api.find("headDataLookupByHeadnum(headnum)") == std::string::npos);
}

TEST_CASE("Gate 5: sparse loader-pool body/head slots fall back to authored records",
          "[catalog][checked][static][regression][c3844]") {
	/* A custom .pdbody/.pdhead can make the loader pool active while most base
	 * body/head slots were never parsed into that pool. The getters must be
	 * sparse-aware: an unpopulated active-pool slot returns NULL so the catalog
	 * managers fall back to authored base records. Otherwise a valid base body
	 * such as Skedar (slot 92) becomes an all-zero record and poisons
	 * catalogAssertHealthy at stage-load. */
	const std::string pool = readTextFile("port/src/loader_pool.c");
	const std::string bodies = readTextFile("port/src/catalog_mgr_bodies.c");
	const std::string heads = readTextFile("port/src/catalog_mgr_heads.c");

	REQUIRE(pool.find("s_HeadsPoolPopulated[CATALOG_MGR_HEAD_TOTAL]") !=
	        std::string::npos);
	REQUIRE(pool.find("s_BodiesPoolPopulated[CATALOG_MGR_BODY_TOTAL]") !=
	        std::string::npos);
	REQUIRE(pool.find("memset(s_HeadsPoolPopulated, 0, sizeof(s_HeadsPoolPopulated))") !=
	        std::string::npos);
	REQUIRE(pool.find("memset(s_BodiesPoolPopulated, 0, sizeof(s_BodiesPoolPopulated))") !=
	        std::string::npos);
	REQUIRE(pool.find("if (!s_HeadsPoolPopulated[headnum])") !=
	        std::string::npos);
	REQUIRE(pool.find("s_HeadsPoolPopulated[headnum] = 1;") !=
	        std::string::npos);
	REQUIRE(pool.find("if (!s_BodiesPoolPopulated[bodynum])") !=
	        std::string::npos);
	REQUIRE(pool.find("s_BodiesPoolPopulated[bodynum] = 1;") !=
	        std::string::npos);
	REQUIRE(pool.find("if (!s_HeadsPoolPopulated[idx]) return NULL;") !=
	        std::string::npos);
	REQUIRE(pool.find("if (!s_BodiesPoolPopulated[idx]) return NULL;") !=
	        std::string::npos);

	REQUIRE(bodies.find("const body_data_t *src = loaderPoolGetBody(bodynum);") !=
	        std::string::npos);
	REQUIRE(bodies.find("if (bodynum >= CATALOG_MGR_BODY_CUSTOM_START)") !=
	        std::string::npos);
	REQUIRE(bodies.find("custom->filenum != 0 || custom->catalog_id[0] != '\\0'") !=
	        std::string::npos);
	REQUIRE(bodies.find("s_populateFromAuthored(bodynum);") !=
	        std::string::npos);
	REQUIRE(heads.find("const head_data_t *src = loaderPoolGetHead(headnum);") !=
	        std::string::npos);
	REQUIRE(heads.find("if (headnum >= CATALOG_MGR_HEAD_CUSTOM_START)") !=
	        std::string::npos);
	REQUIRE(heads.find("custom->filenum != 0 || custom->catalog_id[0] != '\\0'") !=
	        std::string::npos);
	REQUIRE(heads.find("s_populateFromAuthored(headnum);") !=
	        std::string::npos);
}

TEST_CASE("Gate 5: validateSlot discriminator over the body/head TOTAL range",
          "[catalog][checked][regression][c3844]") {
	/* The helper passes a "registered marker" (1 if filenum!=0 or catalog_id
	 * set, else 0) as the sentinel. TOTAL = 152 base + 32 custom = 184. */
	const s32 TOTAL = 152 + 32;
	REQUIRE(catalogCheckedValidateSlot(0,   TOTAL, 1) == CATALOG_CHECKED_OK);           /* registered base */
	REQUIRE(catalogCheckedValidateSlot(160, TOTAL, 1) == CATALOG_CHECKED_OK);           /* registered custom slot */
	REQUIRE(catalogCheckedValidateSlot(160, TOTAL, 0) == CATALOG_CHECKED_UNPOPULATED);  /* unregistered in-range */
	REQUIRE(catalogCheckedValidateSlot(0,   TOTAL, 0) == CATALOG_CHECKED_UNPOPULATED);
	REQUIRE(catalogCheckedValidateSlot(TOTAL, TOTAL, 1) == CATALOG_CHECKED_OOB);        /* past TOTAL */
}
