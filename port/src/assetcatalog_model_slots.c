#include <string.h>

#include <PR/ultratypes.h>

#include "constants.h"   /* MODEL_CUSTOM_COUNT / MODEL_CUSTOM_START / NUM_MODELS */
#include "assetcatalog.h"
#include "assetcatalog_model_slots.h"
#include "system.h"

/*
 * B-911 (c3848): catalog-owned private custom-model runtime-slot allocator.
 * See port/include/assetcatalog_model_slots.h. Mirrors
 * port/src/assetcatalog_body_head_slots.c.
 */

static char s_CustomModelCatalogIds[MODEL_CUSTOM_COUNT][CATALOG_ID_LEN];

_Static_assert(MODEL_CUSTOM_COUNT > 0,
    "model custom slot count must be positive");

void assetCatalogResetCustomModelSlots(void)
{
    memset(s_CustomModelCatalogIds, 0, sizeof(s_CustomModelCatalogIds));
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

s32 assetCatalogResolveModelPrivateSlot(const char *catalog_id)
{
    s32 idx = s_allocate(s_CustomModelCatalogIds, MODEL_CUSTOM_COUNT,
        catalog_id);

    if (idx < 0) {
        if (catalog_id && catalog_id[0]) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.MODEL.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom "
                "model slots available (%d)",
                catalog_id, MODEL_CUSTOM_COUNT);
        }
        return -1;
    }

    return MODEL_CUSTOM_START + idx;
}
