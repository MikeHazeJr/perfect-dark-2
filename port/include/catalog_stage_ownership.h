/**
 * catalog_stage_ownership.h -- pure stage-owner ledger for catalog entries
 *
 * The aggregate asset ref_count says how many references exist, not who owns
 * them.  Stage transitions use this independent one-owner ledger so they can
 * release only the reference acquired by the stage-category loader.
 */

#ifndef _IN_CATALOG_STAGE_OWNERSHIP_H
#define _IN_CATALOG_STAGE_OWNERSHIP_H

#include <PR/ultratypes.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Return 1 only when the current stage owns a reference to entry. */
s32 catalogStageOwnershipHasRef(const asset_entry_t *entry);

/**
 * Record the single stage-owner reference.
 * Returns 1 when newly acquired, 0 when already owned, and -1 on invalid input.
 */
s32 catalogStageOwnershipAcquire(asset_entry_t *entry);

/**
 * Remove the single stage-owner reference.
 * Returns 1 when released, 0 when no stage reference existed, and -1 on
 * invalid input.  This ledger never mutates the aggregate ref_count.
 */
s32 catalogStageOwnershipRelease(asset_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* _IN_CATALOG_STAGE_OWNERSHIP_H */
