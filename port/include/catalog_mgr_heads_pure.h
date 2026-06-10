#ifndef PD_CATALOG_MGR_HEADS_PURE_H
#define PD_CATALOG_MGR_HEADS_PURE_H

#include <PR/ultratypes.h>

/*
 * port/include/catalog_mgr_heads_pure.h -- Catalog Gate 3 F1: pure
 * validators for the catalog manager heads module.
 *
 * Pure: no globals, no I/O, no allocator dependencies. The module exists
 * as a separate translation unit so pd-tests can pin the contract without
 * dragging in g_HeadsAndBodies[] / sysLogPrintf / etc. The live router
 * (catalog_mgr_heads.c) delegates to these for bounds checks and
 * gender-pool eligibility.
 *
 * See port/include/catalog_mgr_heads.h for the public manager API and
 * context/audits/catalog-gate3-heads-data-2026-05-01.md for the design.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Equals ARRAYCOUNT(g_HeadsAndBodies) = 152. The legacy table is sized
 * identically (src/include/data.h:390). */
#define CATALOG_MGR_HEAD_COUNT_PURE 152

/* c3844 Gate 2: total addressable head slots = base + private custom range.
 * Kept in sync with CATALOG_MGR_HEAD_TOTAL in catalog_mgr_heads.h. The
 * in-range bound uses TOTAL; base count stays 152 for population loops. */
#define CATALOG_MGR_HEAD_CUSTOM_COUNT_PURE 32
#define CATALOG_MGR_HEAD_TOTAL_PURE (CATALOG_MGR_HEAD_COUNT_PURE + CATALOG_MGR_HEAD_CUSTOM_COUNT_PURE)

/* HEAD_RANDOM_GENDER sentinel, mirrored here so the pure layer can
 * surface it without dragging constants.h. Live routers pass this value
 * through; the pure layer treats it as "not a real index". */
#define CATALOG_MGR_HEAD_RANDOM_GENDER_PURE 1000

/* Bounds check for headnum.
 * Returns 1 if headnum is in [0, CATALOG_MGR_HEAD_COUNT_PURE) and is
 * not the HEAD_RANDOM_GENDER sentinel. Returns 0 for negative,
 * out-of-range, or sentinel inputs.
 */
s32 catalogMgrHeadIsInRangePure(s32 headnum);

/* Gender-pool eligibility predicate.
 * Returns 1 if (ismale != 0) == want_male AND unk00_01 == 1 (canonical
 * head slot, not a body slot or sentinel). The unk00_01 gate ensures
 * the random-gender pool only contains real head models, mirroring the
 * legacy g_MpMaleHeads / g_MpFemaleHeads contract.
 *
 * Caller passes ismale + unk00_01 + want_male flags so this module
 * stays free of the head_data_t struct shape. Live router unpacks
 * head_data_t fields and forwards.
 */
s32 catalogMgrHeadIsGenderPoolEligiblePure(
    s32 ismale, s32 unk00_01, s32 want_male);

/* HEAD_RANDOM_GENDER detection. Returns 1 if headnum equals the
 * 1000 sentinel value, 0 otherwise.
 */
s32 catalogMgrHeadIsRandomGenderSentinelPure(s32 headnum);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_MGR_HEADS_PURE_H */
