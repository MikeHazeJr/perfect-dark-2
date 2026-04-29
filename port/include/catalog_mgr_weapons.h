#ifndef PD_CATALOG_MGR_WEAPONS_H
#define PD_CATALOG_MGR_WEAPONS_H

#include <PR/ultratypes.h>

/*
 * port/include/catalog_mgr_weapons.h -- S484 F1: Catalog Manager for weapons.
 *
 * Background and design rationale: see
 *   context/designs/catalog-full-pipeline-weapons-2026-04-27.md
 *
 * Phase 2 (F1-F10) status: this manager is a thin pass-through router
 * over the legacy g_Weapons[] / g_AibotWeaponPreferences[] /
 * invaimsettings_default / invnoisesettings_silent globals. F2 wires
 * weaponFindById to call catalogManagerGetWeaponByIndex; F3-F8 migrate
 * remaining direct table accesses through the manager. F11-F13 (next
 * session) replace the legacy backing tables with manager-owned data
 * sourced from .pdbase JSON files.
 *
 * Logging: every miss path emits a CATALOG.MGR.WEAPON.MISS: warning so
 * upstream callers can be diagnosed. Every mutation emits
 * CATALOG.MGR.WEAPON.MUTATE:.
 *
 * The accessor signatures intentionally match the legacy struct weapon *
 * shape so consumers of struct weapon * pointers can be migrated by
 * routing the call through the manager without changing field-chained
 * reads.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct weapon;
struct aibotweaponpreference;
struct invaimsettings;
struct noisesettings;

/* Range bound for weapon_id. Equals WEAPON_SUICIDEPILL + 1 = 0x56 = 86.
 * The legacy g_Weapons[] is sized identically (src/include/game/inv.h:9). */
#define CATALOG_MGR_WEAPON_COUNT 86

/* EYESPY variant enum (S484 F5 mutator surface).
 * 0 CamSpy / 1 DrugSpy / 2 BombSpy. Per-stage spec lives in
 * catalog_mgr_weapons_pure.{h,c} so pd-tests can pin it. */
typedef enum {
    EYESPY_VARIANT_CAMSPY  = 0,
    EYESPY_VARIANT_DRUGSPY = 1,
    EYESPY_VARIANT_BOMBSPY = 2
} eyespy_variant_e;

/* ========================================================================
 * Index-based hot-path accessor. O(1).
 * Returns NULL on out-of-range; logs CATALOG.MGR.WEAPON.MISS:.
 * Returns NULL on in-range-but-empty slot (legacy g_Weapons[i] == NULL),
 * silently (this is the parity-only behaviour matching legacy
 * weaponFindById; the LOG_WARNING is reserved for genuine bounds misses).
 * ======================================================================== */
struct weapon *catalogManagerGetWeaponByIndex(s32 weapon_id);

/* String-id accessor. Boundary use only; not a hot path.
 * Resolves catalog_id via assetCatalogResolve, expects ASSET_WEAPON,
 * dereferences runtime_index back to the same weapon entry as
 * catalogManagerGetWeaponByIndex. */
struct weapon *catalogManagerGetWeaponById(const char *catalog_id);

/* Iterator support: count of slots and accessor by 0-based iter index.
 * iter_index >= count returns NULL. The iterator surfaces ALL slots,
 * including empty (NULL) ones, so callers can enumerate over the full
 * range; consumers must NULL-check. */
s32 catalogManagerWeaponCount(void);
struct weapon *catalogManagerGetWeaponAt(s32 iter_index);

/* Default fallback aim and noise settings. Replaces the legacy
 * invaimsettings_default and invnoisesettings_silent extern reads.
 * Pointers are stable across calls. Const: never mutates. */
const struct invaimsettings *catalogManagerWeaponDefaultAimSettings(void);
const struct noisesettings  *catalogManagerWeaponDefaultNoiseSettings(void);

/* EYESPY variant mutator (S484 F5).
 * Replaces the bondgunreset.c / playerreset.c direct
 * g_Weapons[WEAPON_EYESPY]->name|shortname|flags writes. */
void catalogManagerWeaponSetEyespyVariant(eyespy_variant_e variant);

/* Convenience: map stage_index to the matching EYESPY variant per
 * legacy bondgunreset rules and apply via SetEyespyVariant.
 * Single call site for the bondgunreset.c / playerreset.c migration. */
void catalogManagerWeaponSetEyespyForStage(s32 stage_index);

/* Bot AI preference accessor. Replaces direct
 * g_AibotWeaponPreferences[i] reads in bot.c / botinv.c (S484 F7). */
const struct aibotweaponpreference *catalogManagerGetWeaponBotPref(s32 weapon_id);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_MGR_WEAPONS_H */
