#ifndef PD_ASSETCATALOG_BODY_HEAD_SLOTS_H
#define PD_ASSETCATALOG_BODY_HEAD_SLOTS_H

#include <PR/ultratypes.h>

/*
 * c3844 Gate 2: catalog-owned private body/head runtime-slot allocator.
 *
 * The analog of port/src/assetcatalog_weapon_slots.c for character bodies and
 * heads. A fully-new user-created .pdbody/.pdhead has no legacy bodynum/headnum
 * (the 152 base table has zero headroom). This allocator maps such a catalog ID
 * to a private runtime slot in the custom range
 * [CATALOG_MGR_BODY_CUSTOM_START, CATALOG_MGR_BODY_TOTAL) so the existing
 * integer render path (body0f02ce8c -> catalogManagerGetBodyModeldef ->
 * s_Bodies[slot]) assembles the custom body from its public mesh source with no
 * signature change.
 *
 * The private slot is migration debt only: it must never cross a public
 * boundary (wire / save / manifest / UI / mod tools). Catalog ID strings stay
 * the identity at every public boundary.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all custom body/head slot reservations. Called wherever the catalog is
 * rebuilt, beside assetCatalogResetCustomWeaponSlots(). */
void assetCatalogResetCustomBodyHeadSlots(void);

/* Resolve a catalog-owned private runtime slot for a custom body with no legacy
 * bodynum. Dedups by catalog id (same id -> same slot). Returns a slot in
 * [CATALOG_MGR_BODY_CUSTOM_START, CATALOG_MGR_BODY_TOTAL), or -1 when the
 * private range is exhausted (logs CATALOG.BODY.CUSTOM_SLOT_FAIL). */
s32 assetCatalogResolveBodyPrivateSlot(const char *catalog_id);

/* Head equivalent; range [CATALOG_MGR_HEAD_CUSTOM_START,
 * CATALOG_MGR_HEAD_TOTAL); logs CATALOG.HEAD.CUSTOM_SLOT_FAIL on exhaustion. */
s32 assetCatalogResolveHeadPrivateSlot(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* PD_ASSETCATALOG_BODY_HEAD_SLOTS_H */
