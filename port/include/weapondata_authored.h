#ifndef PD_WEAPONDATA_AUTHORED_H
#define PD_WEAPONDATA_AUTHORED_H

/*
 * port/include/weapondata_authored.h -- Catalog Universality BYOR
 * Completion (2026-05-03).
 *
 * Public surface for the weapon AUTHORING source-of-truth.
 *
 * THIS HEADER IS FOR THE RUNTIME EMITTER ONLY.
 * Engine code (gameplay, UI, networking, AI) MUST NOT include this
 * header; engine code reads weapon data through the catalog
 * (catalog_mgr_weapons) which loads the per-asset .pdweapon files
 * authored at startup by the romextract_pdweapon emitter.
 *
 * Allowed includers:
 *   - port/src/weapondata_authored.c          (the definitions)
 *   - port/src/animdata_authored.c            (cross-references invanim_*)
 *   - port/src/romextract_pdweapon.c          (the emitter)
 *   - port/src/romextract_pdarena.c           (scenario setup weapon refs)
 *   - port/src/romextract_pdmesh.c            (walks weapon hi/lo meshes)
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>

struct weapon;
struct aibotweaponpreference;

/*
 * Iteration table -- 86 entries, one per weapon, in WEAPON_* enum order.
 * NULL slots are not present (every entry is a valid weapon pointer).
 * Array size matches the historical g_Weapons[] array.
 */
extern struct weapon *g_WeaponData[];
extern const s32 g_WeaponDataCount;

/*
 * Catalog ID slug per weapon index. Parallel to g_WeaponData; entry i
 * is the catalog ID for g_WeaponData[i]. Disambiguated when the
 * underlying invitem_* symbol repeats (keycard / hammer / rocket).
 */
extern const char *const g_WeaponDataCatalogIds[];

/*
 * Bot weapon preferences -- parallel to g_WeaponData by WEAPON_* index.
 * The emitter attaches g_BotPrefData[i] as the bot_pref subdoc on the
 * .pdweapon for g_WeaponData[i].
 */
extern struct aibotweaponpreference g_BotPrefData[];
extern const s32 g_BotPrefDataCount;

#endif /* PD_WEAPONDATA_AUTHORED_H */
