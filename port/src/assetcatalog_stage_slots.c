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

/* An existing key takes precedence over the first free slot. */
static s32 s_findOrNextStageSlot(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) {
        return -1;
    }

    for (s32 i = 0; i < STAGENUM_CUSTOM_COUNT; i++) {
        if (s_CustomStageCatalogIds[i][0]
                && strncmp(s_CustomStageCatalogIds[i], catalog_id, CATALOG_ID_LEN) == 0) {
            return i;
        }
    }

    for (s32 i = 0; i < STAGENUM_CUSTOM_COUNT; i++) {
        if (!s_CustomStageCatalogIds[i][0]) {
            return i;
        }
    }

    return -1;
}

s32 assetCatalogPeekStagenumPrivateSlot(const char *catalog_id)
{
    const s32 idx = s_findOrNextStageSlot(catalog_id);
    return idx < 0 ? -1 : STAGENUM_CUSTOM_START + idx;
}

s32 assetCatalogResolveStagenumPrivateSlot(const char *catalog_id)
{
    const s32 idx = s_findOrNextStageSlot(catalog_id);

    if (idx < 0) {
        if (catalog_id && catalog_id[0]) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.STAGE.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom "
                "stage slots available (%d)",
                catalog_id, STAGENUM_CUSTOM_COUNT);
        }
        return -1;
    }

    char *slot = s_CustomStageCatalogIds[idx];
    if (!slot[0]) {
        strncpy(slot, catalog_id, CATALOG_ID_LEN - 1);
        slot[CATALOG_ID_LEN - 1] = '\0';
    }

    return STAGENUM_CUSTOM_START + idx;
}

s32 assetCatalogFindStagenumPrivateSlot(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) return -1;
    for (s32 i = 0; i < STAGENUM_CUSTOM_COUNT; i++) {
        if (strcmp(s_CustomStageCatalogIds[i], catalog_id) == 0) {
            return STAGENUM_CUSTOM_START + i;
        }
    }
    return -1;
}

s32 assetCatalogReleaseStagenumPrivateSlot(const char *catalog_id, s32 stagenum)
{
    if (assetCatalogFindStagenumPrivateSlot(catalog_id) != stagenum
            || stagenum < STAGENUM_CUSTOM_START || stagenum >= STAGENUM_CUSTOM_END) {
        return 0;
    }
    s_CustomStageCatalogIds[stagenum - STAGENUM_CUSTOM_START][0] = '\0';
    return 1;
}
