#ifndef PD_CATALOG_MGR_WEAPONS_PURE_H
#define PD_CATALOG_MGR_WEAPONS_PURE_H

#include <PR/ultratypes.h>

/*
 * port/include/catalog_mgr_weapons_pure.h -- S484 F1: pure validators
 * and spec helpers for the catalog manager weapons module.
 *
 * Pure: no globals, no I/O, no allocator dependencies. The module exists
 * as a separate translation unit so pd-tests can pin the contract
 * without dragging in g_Weapons[] / g_AibotWeaponPreferences[] /
 * invaimsettings_default. The live router (catalog_mgr_weapons.c)
 * delegates to these functions for bounds checks and spec decisions.
 *
 * See port/include/catalog_mgr_weapons.h for the public manager API
 * and context/designs/catalog-full-pipeline-weapons-2026-04-27.md for
 * the full design rationale.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Base WEAPON_* rows occupy 0..0x55. The active runtime also reserves
 * 0x56..0x6c as catalog-owned private custom weapon slots while the
 * legacy held-weapon runtime is still integer-indexed. */
#define CATALOG_MGR_WEAPON_COUNT_PURE 109

typedef enum {
    EYESPY_VARIANT_PURE_CAMSPY  = 0,
    EYESPY_VARIANT_PURE_DRUGSPY = 1,
    EYESPY_VARIANT_PURE_BOMBSPY = 2,
    EYESPY_VARIANT_PURE_INVALID = -1
} eyespy_variant_pure_e;

/* Concrete tuple captured by the bondgunreset / playerreset legacy
 * mutators. The pure spec produces this tuple from a stage-index input;
 * the live router applies it via field writes. */
typedef struct {
    /* Resolved language-bank ID for both name and shortname (legacy
     * pattern: name == shortname for EYESPY variants). */
    s32 name_langid;
    /* Bitmask of WEAPONFLAG_DETERMINER_{S,F}_AN bits to OR into
     * weapon->flags. */
    u32 set_flags;
    /* Bitmask of WEAPONFLAG_DETERMINER_{S,F}_AN bits to clear from
     * weapon->flags. */
    u32 clear_flags;
} eyespy_variant_spec_pure_t;

/* Bounds check for weapon_id. Returns 1 if in [0, CATALOG_MGR_WEAPON_COUNT)
 * else 0. */
s32 catalogMgrWeaponIsInRangePure(s32 weapon_id);

/* Map a stage-index to the EYESPY variant per legacy bondgunreset rules:
 *   STAGEINDEX_AIRBASE -> DrugSpy
 *   STAGEINDEX_CHICAGO -> BombSpy
 *   STAGEINDEX_MBR     -> BombSpy
 *   anything else      -> CamSpy
 *
 * The stage-index numeric values are passed in by the caller (live or
 * test) so this module stays free of constants.h.
 */
eyespy_variant_pure_e catalogMgrWeaponEyespyFromStageIndexPure(
    s32 stage_index, s32 stageindex_airbase,
    s32 stageindex_chicago, s32 stageindex_mbr);

/* Produce the (langid, set_flags, clear_flags) tuple for a variant.
 * Caller passes the langid + flag-bit values so this module stays free
 * of generated lang headers and constants.h.
 *
 * Returns 1 on success, 0 if variant is out of range (and zero-fills
 * the output struct). */
s32 catalogMgrWeaponEyespyVariantSpecPure(
    eyespy_variant_pure_e variant,
    s32 camspy_langid, s32 drugspy_langid, s32 bombspy_langid,
    u32 determiner_flags_mask,
    eyespy_variant_spec_pure_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_MGR_WEAPONS_PURE_H */
