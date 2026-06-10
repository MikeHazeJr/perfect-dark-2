#ifndef PD_ASSETCATALOG_TEXTURE_SLOTS_H
#define PD_ASSETCATALOG_TEXTURE_SLOTS_H

#include <PR/ultratypes.h>

/*
 * c3849 Wave 2: catalog-owned private custom-texture slot allocator.
 *
 * The texture analog of port/src/assetcatalog_model_slots.c (B-911). A
 * net-new custom .pdtexture row authors no texture_id (scanner default -1),
 * so it never enters the texnum reverse index and modeldef texconfigs cannot
 * reference it. This allocator maps such a catalog ID to a private texnum in
 * [TEXTURE_CUSTOM_START, TEXTURE_CUSTOM_END) (constants.h). With the slot in
 * source_texnum, catalogLoadInit's reverse index reaches it and texLoad's
 * public-image-source intercept loads texture.png; the grown zero-init
 * g_Textures rows make the native fall-through a defined "no data".
 *
 * The slot is per-process and allocation-order dependent: it is baked into
 * compiled GBI markers in memory each launch and must NEVER be written to
 * wire, save, manifest, or any on-disk cache of compiled GBI. Catalog ID
 * strings stay the identity at every public boundary.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all custom texture slot reservations. Called wherever the catalog is
 * rebuilt, beside the other custom-slot resets. */
void assetCatalogResetCustomTextureSlots(void);

/* Resolve a catalog-owned private texnum for a custom texture row with no
 * authored texture_id. Dedups by catalog id (same id -> same slot). Returns a
 * slot in [TEXTURE_CUSTOM_START, TEXTURE_CUSTOM_END), or -1 when the range is
 * exhausted (logs CATALOG.TEXTURE.CUSTOM_SLOT_FAIL). */
s32 assetCatalogResolveTexturePrivateSlot(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* PD_ASSETCATALOG_TEXTURE_SLOTS_H */
