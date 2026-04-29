/*
 * port/src/catalog_mgr_weapons_pure.c -- S484 F1: pure validators and
 * spec helpers for the catalog manager weapons module.
 *
 * See port/include/catalog_mgr_weapons_pure.h for the contract.
 *
 * Pure: no globals, no I/O, no allocator dependencies. The whole point
 * of this file is to be testable in isolation by pd-tests.
 */

#include <PR/ultratypes.h>
#include "catalog_mgr_weapons_pure.h"

s32 catalogMgrWeaponIsInRangePure(s32 weapon_id)
{
	if (weapon_id < 0 || weapon_id >= CATALOG_MGR_WEAPON_COUNT_PURE) {
		return 0;
	}
	return 1;
}

eyespy_variant_pure_e catalogMgrWeaponEyespyFromStageIndexPure(
    s32 stage_index, s32 stageindex_airbase,
    s32 stageindex_chicago, s32 stageindex_mbr)
{
	if (stage_index == stageindex_airbase) {
		return EYESPY_VARIANT_PURE_DRUGSPY;
	}
	if (stage_index == stageindex_chicago || stage_index == stageindex_mbr) {
		return EYESPY_VARIANT_PURE_BOMBSPY;
	}
	return EYESPY_VARIANT_PURE_CAMSPY;
}

s32 catalogMgrWeaponEyespyVariantSpecPure(
    eyespy_variant_pure_e variant,
    s32 camspy_langid, s32 drugspy_langid, s32 bombspy_langid,
    u32 determiner_flags_mask,
    eyespy_variant_spec_pure_t *out)
{
	if (out == 0) {
		return 0;
	}

	out->name_langid = 0;
	out->set_flags = 0;
	out->clear_flags = 0;

	switch (variant) {
	case EYESPY_VARIANT_PURE_CAMSPY:
		out->name_langid = camspy_langid;
		out->set_flags = determiner_flags_mask;
		return 1;
	case EYESPY_VARIANT_PURE_DRUGSPY:
		out->name_langid = drugspy_langid;
		out->clear_flags = determiner_flags_mask;
		return 1;
	case EYESPY_VARIANT_PURE_BOMBSPY:
		out->name_langid = bombspy_langid;
		out->clear_flags = determiner_flags_mask;
		return 1;
	default:
		return 0;
	}
}
