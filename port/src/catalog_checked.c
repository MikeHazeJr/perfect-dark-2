/*
 * port/src/catalog_checked.c -- INV-1 (player-init-architectural-fixes
 * 2026-04-26): pure validators backing the `_Checked` catalog accessor
 * variants.
 *
 * See port/include/catalog_checked.h for the contract.
 *
 * Pure: no globals, no I/O, no allocator dependencies. The whole point
 * of this file is to be testable in isolation by pd-tests.
 */

#include <PR/ultratypes.h>
#include <stdbool.h>
#include "catalog_checked.h"

catalog_checked_result_e catalogCheckedValidateIndex(s32 idx, s32 count)
{
	if (idx < 0 || idx >= count) {
		return CATALOG_CHECKED_OOB;
	}
	return CATALOG_CHECKED_OK;
}

catalog_checked_result_e catalogCheckedValidateSlot(s32 idx, s32 count, s32 sentinel_value)
{
	if (idx < 0 || idx >= count) {
		return CATALOG_CHECKED_OOB;
	}
	if (sentinel_value == 0) {
		return CATALOG_CHECKED_UNPOPULATED;
	}
	return CATALOG_CHECKED_OK;
}

const char *catalogCheckedResultName(catalog_checked_result_e r)
{
	switch (r) {
	case CATALOG_CHECKED_OK:          return "ok";
	case CATALOG_CHECKED_OOB:         return "oob";
	case CATALOG_CHECKED_UNPOPULATED: return "unpopulated";
	}
	return "unknown";
}
