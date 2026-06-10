#ifndef PD_ASSETCATALOG_MODEL_SLOTS_H
#define PD_ASSETCATALOG_MODEL_SLOTS_H

#include <PR/ultratypes.h>

/*
 * B-911 (c3848): catalog-owned private custom-model runtime-slot allocator.
 *
 * The model analog of port/src/assetcatalog_body_head_slots.c (bodies/heads,
 * B-909) and port/src/assetcatalog_weapon_slots.c (weapons). A custom .pdweapon
 * embeds its own .pdmesh and references it by a catalog-ID model_ref in its
 * projectile graph. That mesh has no base g_ModelStates slot (NUM_MODELS has
 * zero headroom), so catalogResolveModel -> out.modelnum = runtime_index = -1 and
 * nothing renders. This allocator maps such a catalog ID to a private runtime
 * slot in the custom range [MODEL_CUSTOM_START, MODEL_CUSTOM_END) (constants.h),
 * so the existing integer render path (catalogResolveModel -> g_ModelStates[slot]
 * via setupLoadModeldef) assembles the custom mesh from its public source with no
 * signature change to the integer render path.
 *
 * The private slot is migration debt only: it must never cross a public boundary
 * (wire / save / manifest / UI / mod tools). Catalog ID strings stay the identity
 * at every public boundary (modelnum already crosses the wire only as a
 * catalog-ID-derived session ref, netmsg.c netWriteModelRef).
 *
 * The allocator owns ONLY the dedup-by-id -> slot mapping. Registering the
 * embedded mesh as an ASSET_MODEL catalog row and growing the g_ModelStates
 * storage to MODEL_CUSTOM_END are sibling surfaces (the ingest walker and the
 * data.h/general.c storage owner), not this file's job.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all custom model slot reservations. Called wherever the catalog is
 * rebuilt, beside assetCatalogResetCustomWeaponSlots() /
 * assetCatalogResetCustomBodyHeadSlots(). */
void assetCatalogResetCustomModelSlots(void);

/* Resolve a catalog-owned private runtime slot for a custom model (an embedded
 * .pdmesh with no base g_ModelStates index). Dedups by catalog id (same id ->
 * same slot). Returns a slot in [MODEL_CUSTOM_START, MODEL_CUSTOM_END), or -1
 * when the private range is exhausted (logs CATALOG.MODEL.CUSTOM_SLOT_FAIL). */
s32 assetCatalogResolveModelPrivateSlot(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* PD_ASSETCATALOG_MODEL_SLOTS_H */
