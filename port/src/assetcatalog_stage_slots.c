#include <string.h>
#include <stdlib.h>

#include <PR/ultratypes.h>

#include "constants.h"   /* STAGENUM_CUSTOM_* */
#include "assetcatalog.h"
#include "assetcatalog_stage_slots.h"
#include "system.h"

/*
 * c3849 Wave 2: catalog-owned private custom-stage stagenum allocator.
 * See port/include/assetcatalog_stage_slots.h. Mirrors
 * port/src/assetcatalog_model_slots.c.
 */

static char s_CustomStageCatalogIds[STAGENUM_CUSTOM_COUNT][CATALOG_ID_LEN];

_Static_assert(STAGENUM_CUSTOM_COUNT > 0,
    "stage custom slot count must be positive");
_Static_assert(STAGENUM_CUSTOM_START > 0x5f,
    "custom stagenums must mint outside the base logical range");
/* The mpsetup save field is 7 bits wide. */
_Static_assert(STAGENUM_CUSTOM_END <= 0x80,
    "custom stagenums must fit the 7-bit mpsetup save field");

void assetCatalogResetCustomStageSlots(void)
{
    memset(s_CustomStageCatalogIds, 0, sizeof(s_CustomStageCatalogIds));
}

void *assetCatalogSnapshotCustomStageSlots(void)
{
    void *snapshot = malloc(sizeof(s_CustomStageCatalogIds));
    if (snapshot) memcpy(snapshot, s_CustomStageCatalogIds,
        sizeof(s_CustomStageCatalogIds));
    return snapshot;
}

s32 assetCatalogRestoreCustomStageSlots(const void *snapshot)
{
    if (!snapshot) return 0;
    memcpy(s_CustomStageCatalogIds, snapshot, sizeof(s_CustomStageCatalogIds));
    return 1;
}

void assetCatalogDestroyCustomStageSlotSnapshot(void *snapshot)
{
    free(snapshot);
}

/* Dedup-or-allocate a private slot index in [0, count). Returns the local
 * index, or -1 when the range is exhausted (caller logs the loud failure). */
static s32 s_allocate(char ids[][CATALOG_ID_LEN], s32 count,
        const char *catalog_id)
{
    s32 i;

    if (!catalog_id || !catalog_id[0]) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (ids[i][0] && strncmp(ids[i], catalog_id, CATALOG_ID_LEN) == 0) {
            return i;
        }
    }

    for (i = 0; i < count; i++) {
        if (!ids[i][0]) {
            strncpy(ids[i], catalog_id, CATALOG_ID_LEN - 1);
            ids[i][CATALOG_ID_LEN - 1] = '\0';
            return i;
        }
    }

    return -1;
}

s32 assetCatalogResolveStagenumPrivateSlot(const char *catalog_id)
{
    s32 idx = s_allocate(s_CustomStageCatalogIds, STAGENUM_CUSTOM_COUNT,
        catalog_id);

    if (idx < 0) {
        if (catalog_id && catalog_id[0]) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.STAGE.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom "
                "stage slots available (%d)",
                catalog_id, STAGENUM_CUSTOM_COUNT);
        }
        return -1;
    }

    return STAGENUM_CUSTOM_START + idx;
}
