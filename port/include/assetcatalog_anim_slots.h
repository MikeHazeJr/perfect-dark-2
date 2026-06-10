#ifndef PD_ASSETCATALOG_ANIM_SLOTS_H
#define PD_ASSETCATALOG_ANIM_SLOTS_H

#include <PR/ultratypes.h>

/* ANIM_END (the base anim-table count) comes from the generated
 * animations.h, but that header has NO include guard and is already
 * pre-included in every pd translation unit through the constants.h
 * precompiled header -- including it directly redeclares enum animnum
 * on a fresh build. Route through the guarded constants.h instead
 * (found while building c3849 Wave 3). */
#include "constants.h"

/*
 * c3849 Wave 2: catalog-owned private custom-animation slot allocator.
 *
 * The animation analog of port/src/assetcatalog_model_slots.c (B-911). A
 * net-new custom .pdanim row authors no anim_id, so it could never install a
 * compiled clip (the install clamp rejected anything >= g_NumAnimations).
 * This allocator maps such a catalog ID to a private animnum in
 * [ANIM_CUSTOM_START, ANIM_CUSTOM_END). The g_Anims table + per-animnum
 * arrays grow by the custom range (zero-init rows; custom rows are seeded
 * from catalog metadata with the 0xffffffff data sentinel so playback routes
 * through the clip-replacement machinery, never the ROM segment).
 *
 * Reverse resolution is the source_animnum override index (LOAD_MAX_ANIMS),
 * NOT the catalog runtime cache: the base anim count already exceeds
 * RT_CACHE_SIZE, and the anim family does not use catalogIdByRuntime.
 *
 * The private slot is migration debt only: it must never cross a public
 * boundary (wire / save / manifest / UI / mod tools). Catalog ID strings stay
 * the identity at every public boundary.
 */

#define ANIM_CUSTOM_COUNT 0x20
#define ANIM_CUSTOM_START ANIM_END
#define ANIM_CUSTOM_END_SLOT (ANIM_END + ANIM_CUSTOM_COUNT)

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all custom anim slot reservations. Called wherever the catalog is
 * rebuilt, beside the other custom-slot resets. */
void assetCatalogResetCustomAnimSlots(void);

/* Resolve a catalog-owned private animnum for a custom animation row with no
 * authored anim_id. Dedups by catalog id (same id -> same slot). Returns a
 * slot in [ANIM_CUSTOM_START, ANIM_CUSTOM_END_SLOT), or -1 when the private
 * range is exhausted (logs CATALOG.ANIM.CUSTOM_SLOT_FAIL). */
s32 assetCatalogResolveAnimPrivateSlot(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* PD_ASSETCATALOG_ANIM_SLOTS_H */
