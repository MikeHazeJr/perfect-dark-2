#ifndef PD_ASSETCATALOG_STAGE_SLOTS_H
#define PD_ASSETCATALOG_STAGE_SLOTS_H

#include <PR/ultratypes.h>

/*
 * c3849 Wave 2: catalog-owned private custom-stage stagenum allocator.
 *
 * The stage analog of port/src/assetcatalog_model_slots.c. A net-new custom
 * map/arena/scenario that authors no INI stagenum could never reach runtime
 * (stagenum -1 flowed unfiltered). This allocator mints a private stagenum in
 * [STAGENUM_CUSTOM_START, STAGENUM_CUSTOM_END) (constants.h: above the base
 * logical range, inside the 7-bit save field); the scanner/netdistrib mint
 * helper then appends an idempotent g_Stages row (file IDs -1 so loading
 * routes through the scenario-source path, never base ROM handles).
 *
 * The minted stagenum is a MACHINE-LOCAL runtime bridge only (allocation-order
 * dependent): wire identity is the catalog ID string / session ref, and the
 * mpsetup save-load path rejects custom stagenums. It must never be
 * serialized to wire, manifest, or save as identity.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all custom stage slot reservations. Called wherever the catalog is
 * rebuilt, beside the other custom-slot resets. */
void assetCatalogResetCustomStageSlots(void);

/* Resolve a catalog-owned private stagenum for a custom stage component with
 * no authored stagenum. Dedups by mint key (arena keys to its scenario_id so
 * both share one stagenum). Returns a stagenum in
 * [STAGENUM_CUSTOM_START, STAGENUM_CUSTOM_END), or -1 when exhausted (logs
 * CATALOG.STAGE.CUSTOM_SLOT_FAIL). */
s32 assetCatalogResolveStagenumPrivateSlot(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* PD_ASSETCATALOG_STAGE_SLOTS_H */
