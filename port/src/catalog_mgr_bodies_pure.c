/*
 * port/src/catalog_mgr_bodies_pure.c -- Catalog Gate 3 Bodies F1: pure
 * validators for the catalog manager bodies module.
 *
 * See port/include/catalog_mgr_bodies_pure.h for the contract.
 *
 * Pure: no globals, no I/O, no allocator dependencies. Tested in pd-tests
 * via tests/test_catalog_mgr_bodies_api.cpp.
 */

#include <PR/ultratypes.h>
#include "catalog_mgr_bodies_pure.h"

s32 catalogMgrBodyIsInRangePure(s32 bodynum)
{
	if (bodynum < 0 || bodynum >= CATALOG_MGR_BODY_COUNT_PURE) {
		return 0;
	}
	return 1;
}

s32 catalogMgrBodyIsIntegratedHeadPure(s32 unk00_01)
{
	return unk00_01 == 1 ? 1 : 0;
}
