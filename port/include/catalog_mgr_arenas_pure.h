#ifndef PD_CATALOG_MGR_ARENAS_PURE_H
#define PD_CATALOG_MGR_ARENAS_PURE_H

#include <PR/ultratypes.h>

/*
 * port/include/catalog_mgr_arenas_pure.h -- Catalog Gate 3 Arenas F1: pure
 * validators for the catalog manager arenas module.
 *
 * Pure: no globals, no I/O, no allocator dependencies. The module exists
 * as a separate translation unit so pd-tests can pin the contract without
 * dragging in g_MpArenas[] / s_ArenaNames[] / s_ArenaGroupMap[] /
 * sysLogPrintf / etc. The live router (catalog_mgr_arenas.c) delegates to
 * these for bounds checks and category-mask classification.
 *
 * See port/include/catalog_mgr_arenas.h for the public manager API and
 * context/audits/catalog-gate3-arenas-data-2026-05-02.md for the design.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Equals ARRAYCOUNT(g_MpArenas) post the 2026-04-26 AllInOne / GEX cull.
 * Both client (src/game/mplayer/setup.c:115) and server stub
 * (port/src/server_stubs.c:113) share this size; the catalog seed in
 * port/src/assetcatalog_base.c::s_ArenaNames is sized identically. */
#define CATALOG_MGR_ARENA_COUNT_PURE 47

/* Bounds check for arena_index.
 * Returns 1 if arena_index is in [0, CATALOG_MGR_ARENA_COUNT_PURE).
 * Returns 0 for negative or out-of-range inputs.
 */
s32 catalogMgrArenaIsInRangePure(s32 arena_index);

/* Category-string -> bitmask classification.
 *
 * Returns RNDMASK_* bit per category, or 0 for unknown / "Random"
 * (Random meta entries never participate in random picks, mirroring
 * src/game/mplayer/setup.c::categoryToMask). The pure layer surfaces
 * the predicate so pd-tests can pin the random-pool semantics without
 * dragging globals. The live consumer in setup.c can switch to this
 * helper for symmetry, but the inline version is bit-identical
 * (audit Section I item Ia.1).
 */
#define CATALOG_MGR_ARENA_RNDMASK_DARK         (1u << 0)
#define CATALOG_MGR_ARENA_RNDMASK_CLASSIC      (1u << 1)
#define CATALOG_MGR_ARENA_RNDMASK_BONUS        (1u << 2)
#define CATALOG_MGR_ARENA_RNDMASK_SOLOMISSIONS (1u << 3)

#define CATALOG_MGR_ARENA_RNDMASK_MULTI \
    (CATALOG_MGR_ARENA_RNDMASK_DARK | \
     CATALOG_MGR_ARENA_RNDMASK_CLASSIC | \
     CATALOG_MGR_ARENA_RNDMASK_BONUS)
#define CATALOG_MGR_ARENA_RNDMASK_SOLO \
    (CATALOG_MGR_ARENA_RNDMASK_SOLOMISSIONS)
#define CATALOG_MGR_ARENA_RNDMASK_ANYMP \
    (CATALOG_MGR_ARENA_RNDMASK_MULTI | CATALOG_MGR_ARENA_RNDMASK_SOLO)

u32 catalogMgrArenaCategoryToMaskPure(const char *category);

/* Slug extraction from a catalog ID.
 *
 * Given "base:arena_mp_skedar" returns "mp_skedar" (i.e. the substring
 * after "<namespace>:arena_"). Returns NULL if the input is NULL, empty,
 * or does not match the "<ns>:arena_<slug>" shape. The returned pointer
 * points into the input buffer (no allocation). Used by the manager
 * init pass to derive the slug from the catalog ID without consulting
 * the legacy s_ArenaNames[] table.
 */
const char *catalogMgrArenaSlugFromIdPure(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_MGR_ARENAS_PURE_H */
