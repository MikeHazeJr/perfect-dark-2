#include "catalog_stage_ownership.h"

s32 catalogStageOwnershipHasRef(const asset_entry_t *entry)
{
    return entry && entry->stage_ref_count > 0;
}

s32 catalogStageOwnershipAcquire(asset_entry_t *entry)
{
    if (!entry) {
        return -1;
    }
    if (entry->stage_ref_count > 0) {
        return 0;
    }
    entry->stage_ref_count = 1;
    return 1;
}

s32 catalogStageOwnershipRelease(asset_entry_t *entry)
{
    if (!entry) {
        return -1;
    }
    if (entry->stage_ref_count <= 0) {
        entry->stage_ref_count = 0;
        return 0;
    }
    entry->stage_ref_count = 0;
    return 1;
}
