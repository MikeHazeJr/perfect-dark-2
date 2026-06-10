#ifndef PD_CATALOG_MGR_BODIES_PURE_H
#define PD_CATALOG_MGR_BODIES_PURE_H

#include <PR/ultratypes.h>

/*
 * port/include/catalog_mgr_bodies_pure.h -- Catalog Gate 3 Bodies F1: pure
 * validators for the catalog manager bodies module.
 *
 * Pure: no globals, no I/O, no allocator dependencies. The module exists
 * as a separate translation unit so pd-tests can pin the contract without
 * dragging in g_HeadsAndBodies[] / sysLogPrintf / etc. The live router
 * (catalog_mgr_bodies.c) delegates to these for bounds checks and
 * integrated-head detection.
 *
 * See port/include/catalog_mgr_bodies.h for the public manager API and
 * context/audits/catalog-gate3-bodies-data-2026-05-02.md for the design.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Equals ARRAYCOUNT(g_HeadsAndBodies) = 152. The legacy table is sized
 * identically (src/include/data.h:390). Bodies share the index space
 * with heads; the underlying array carries both. */
#define CATALOG_MGR_BODY_COUNT_PURE 152

/* c3844 Gate 2: total addressable body slots = base + private custom range.
 * Kept in sync with CATALOG_MGR_BODY_TOTAL in catalog_mgr_bodies.h (the pure
 * TU cannot include the manager header). The in-range bound below uses TOTAL
 * so a catalog-owned custom body slot is a valid index; the base count stays
 * 152 for population/enumeration loops. */
#define CATALOG_MGR_BODY_CUSTOM_COUNT_PURE 32
#define CATALOG_MGR_BODY_TOTAL_PURE (CATALOG_MGR_BODY_COUNT_PURE + CATALOG_MGR_BODY_CUSTOM_COUNT_PURE)

/* Bounds check for bodynum.
 * Returns 1 if bodynum is in [0, CATALOG_MGR_BODY_TOTAL_PURE) (base +
 * private custom range). Returns 0 for negative or out-of-range inputs.
 * Bodies have no RANDOM_GENDER sentinel (that's a head-side concept), so
 * unlike the heads predicate this one only rejects out-of-range.
 */
s32 catalogMgrBodyIsInRangePure(s32 bodynum);

/* Integrated-head body predicate.
 *
 * Returns 1 if unk00_01 == 1 (self-contained body: Skedar, Dr Caroll,
 * EyeSpy, Chicrob). Returns 0 otherwise (normal body, body sentinel slot, or
 * bodies with separate head models).
 *
 * The S593g bodyAllocateModel warning gate at body.c:417 reads through
 * catalogGetBodyIsComplete which after F2 routes through the manager;
 * the manager reads body_data_t::unk00_01 and forwards. The pure layer
 * exists so pd-tests can pin the predicate without dragging the
 * head_data_t / body_data_t struct shape.
 *
 * Caller passes unk00_01 directly (defensive: any non-1 value returns 0
 * so a stale or unset slot does not accidentally claim integrated-head
 * status).
 */
s32 catalogMgrBodyIsIntegratedHeadPure(s32 unk00_01);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_MGR_BODIES_PURE_H */
