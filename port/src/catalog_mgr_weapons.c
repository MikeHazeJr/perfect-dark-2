/*
 * port/src/catalog_mgr_weapons.c -- S484 F1: Catalog Manager for weapons.
 *
 * See port/include/catalog_mgr_weapons.h for the public contract and
 * context/designs/catalog-full-pipeline-weapons-2026-04-27.md for the
 * full design rationale.
 *
 * Phase 2 (F1-F10): this manager is a thin pass-through router over
 * the legacy g_Weapons[] / g_AibotWeaponPreferences[] /
 * invaimsettings_default / invnoisesettings_silent globals. F2 wires
 * weaponFindById to call catalogManagerGetWeaponByIndex; F3-F8 migrate
 * remaining direct table accesses through the manager. F11-F13 (next
 * session) replace the legacy backing tables with manager-owned data
 * sourced from the per-asset envelope files.
 *
 * Pure validators live in port/src/catalog_mgr_weapons_pure.c and are
 * pinned by tests/test_catalog_mgr_weapons_api.cpp.
 */

#include <ultra64.h>
#include <stddef.h>
#include "data.h"
#include "types.h"
#include "constants.h"
#include "system.h"
#include "assetcatalog.h"
#include "catalog_mgr_weapons.h"
#include "catalog_mgr_weapons_pure.h"
#include "loader_pool.h"
#include "lang.h"

_Static_assert(CATALOG_MGR_WEAPON_COUNT == WEAPON_CUSTOM_END,
    "catalog manager weapon pool must cover every private runtime identity");
_Static_assert(CATALOG_MGR_WEAPON_COUNT_PURE == CATALOG_MGR_WEAPON_COUNT,
    "pure and production weapon bounds must remain identical");

/* S484 F13: g_Weapons[], g_AibotWeaponPreferences[],
 * invaimsettings_default, and invnoisesettings_silent were retired in
 * favour of the loader's typed pools (port/src/loader_pool.c).
 * Every accessor below now goes through the loader. There is no
 * legacy fallback any more -- the parity-period bridge lived in F12. */

s32 catalogManagerWeaponCount(void)
{
	return CATALOG_MGR_WEAPON_COUNT;
}

struct weapon *catalogManagerGetWeaponByIndex(s32 weapon_id)
{
	/* Negative IDs are a normal "no weapon equipped" sentinel that
	 * callers (gunctrl.weaponnum, hand->gset.weaponnum) pass during
	 * unarmed / between-weapon states. Silent NULL preserves legacy
	 * weaponFindById parity. */
	if (weapon_id < 0) {
		return NULL;
	}
	/* Over-positive (>= count) IS a bug surface. The B-263 / S483c
	 * SPAWNWEAPON_FIESTA_SENTINEL class lives here; loud-fail per
	 * INV-1 discipline. */
	if (weapon_id >= CATALOG_MGR_WEAPON_COUNT) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.WEAPON.MISS: weapon_id=%d out of range [0, %d)",
			weapon_id, CATALOG_MGR_WEAPON_COUNT);
		return NULL;
	}
	/* F13: loader-owned pool is the sole data source. Returns NULL
	 * if the loader hasn't initialised yet (very early startup) -- the
	 * caller's existing NULL handling kicks in. */
	return (struct weapon *)loaderPoolGetWeapon(weapon_id);
}

struct weapon *catalogManagerGetWeaponAt(s32 iter_index)
{
	if (iter_index < 0 || iter_index >= CATALOG_MGR_WEAPON_COUNT) {
		return NULL;
	}
	return (struct weapon *)loaderPoolGetWeapon(iter_index);
}

struct weapon *catalogManagerGetWeaponById(const char *catalog_id)
{
	const asset_entry_t *e;
	s32 weapon_num;

	if (catalog_id == NULL || catalog_id[0] == '\0') {
		return NULL;
	}

	e = assetCatalogResolve(catalog_id);
	if (e == NULL || e->type != ASSET_WEAPON) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.WEAPON.MISS: catalog_id=\"%s\" not registered",
			catalog_id);
		return NULL;
	}

	/* runtime_index for ASSET_WEAPON entries is the WEAPON_* enum
	 * (set in assetcatalog_base_extended.c::registerBaseWeapons). */
	weapon_num = e->runtime_index;
	if (!catalogMgrWeaponIsInRangePure(weapon_num)) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.WEAPON.MISS: catalog_id=\"%s\" runtime_index=%d invalid",
			catalog_id, weapon_num);
		return NULL;
	}
	return catalogManagerGetWeaponByIndex(weapon_num);
}

const struct invaimsettings *catalogManagerWeaponDefaultAimSettings(void)
{
	return loaderPoolGetDefaultAim();
}

const struct noisesettings *catalogManagerWeaponDefaultNoiseSettings(void)
{
	return loaderPoolGetDefaultNoise();
}

void catalogManagerWeaponSetEyespyVariant(eyespy_variant_e variant)
{
	struct weapon *w;
	eyespy_variant_spec_pure_t spec;
	s32 ok;

	w = catalogManagerGetWeaponByIndex(WEAPON_EYESPY);
	if (w == NULL) {
		return;
	}

	ok = catalogMgrWeaponEyespyVariantSpecPure(
		(eyespy_variant_pure_e)variant,
		L_GUN_060, L_GUN_061, L_GUN_062,
		(WEAPONFLAG_DETERMINER_S_AN | WEAPONFLAG_DETERMINER_F_AN),
		&spec);
	if (!ok) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.WEAPON.MUTATE: eyespy unknown variant=%d",
			(s32)variant);
		return;
	}

	w->name = (u16)spec.name_langid;
	w->shortname = (u16)spec.name_langid;
	w->flags = (w->flags & ~spec.clear_flags) | spec.set_flags;

	sysLogPrintf(LOG_NOTE,
		"CATALOG.MGR.WEAPON.MUTATE: eyespy variant=%d langid=%d flags=0x%08x",
		(s32)variant, spec.name_langid, w->flags);
}

void catalogManagerWeaponSetEyespyForStage(s32 stage_index)
{
	eyespy_variant_pure_e v = catalogMgrWeaponEyespyFromStageIndexPure(
		stage_index, STAGEINDEX_AIRBASE, STAGEINDEX_CHICAGO, STAGEINDEX_MBR);
	catalogManagerWeaponSetEyespyVariant((eyespy_variant_e)v);
}

const struct aibotweaponpreference *catalogManagerGetWeaponBotPref(s32 weapon_id)
{
	/* Mirrors weaponFindById policy: negative is silent (legitimate
	 * "no weapon" sentinel), over-positive is loud (bug surface). */
	if (weapon_id < 0) {
		return NULL;
	}
	if (weapon_id >= CATALOG_MGR_WEAPON_COUNT) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.WEAPON.MISS: bot_pref weapon_id=%d out of range [0, %d)",
			weapon_id, CATALOG_MGR_WEAPON_COUNT);
		return NULL;
	}
	return loaderPoolGetBotPref(weapon_id);
}
