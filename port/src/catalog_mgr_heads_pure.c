/*
 * port/src/catalog_mgr_heads_pure.c -- Catalog Gate 3 F1: pure validators
 * for the catalog manager heads module.
 *
 * See port/include/catalog_mgr_heads_pure.h for the contract.
 *
 * Pure: no globals, no I/O, no allocator dependencies. Tested in pd-tests
 * via tests/test_catalog_mgr_heads_api.cpp.
 */

#include <PR/ultratypes.h>
#include "catalog_mgr_heads_pure.h"

s32 catalogMgrHeadIsInRangePure(s32 headnum)
{
	/* c3844 Gate 2: accept base [0,152) AND the private custom range
	 * [152, TOTAL); the RANDOM_GENDER sentinel (1000) is still rejected. */
	if (headnum < 0 || headnum >= CATALOG_MGR_HEAD_TOTAL_PURE) {
		return 0;
	}
	if (headnum == CATALOG_MGR_HEAD_RANDOM_GENDER_PURE) {
		return 0;
	}
	return 1;
}

s32 catalogMgrHeadIsGenderPoolEligiblePure(
    s32 ismale, s32 unk00_01, s32 want_male)
{
	if (unk00_01 == 0) {
		return 0;
	}
	if (want_male) {
		return ismale != 0 ? 1 : 0;
	}
	return ismale == 0 ? 1 : 0;
}

s32 catalogMgrHeadIsRandomGenderSentinelPure(s32 headnum)
{
	return headnum == CATALOG_MGR_HEAD_RANDOM_GENDER_PURE ? 1 : 0;
}
