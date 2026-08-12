#include <string.h>
#include <stdlib.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_anim_slots.h"
#include "system.h"

/*
 * c3849 Wave 2: catalog-owned private custom-animation slot allocator.
 * See port/include/assetcatalog_anim_slots.h. Mirrors
 * port/src/assetcatalog_model_slots.c.
 */

static char s_CustomAnimCatalogIds[ANIM_CUSTOM_COUNT][CATALOG_ID_LEN];

_Static_assert(ANIM_CUSTOM_COUNT > 0,
    "anim custom slot count must be positive");
/* LOAD_MAX_ANIMS (assetcatalog_load.c) bounds the animnum override index. */
_Static_assert(ANIM_CUSTOM_END_SLOT <= 2048,
    "anim custom slots must fit the LOAD_MAX_ANIMS override index");

void assetCatalogResetCustomAnimSlots(void)
{
    memset(s_CustomAnimCatalogIds, 0, sizeof(s_CustomAnimCatalogIds));
}

void *assetCatalogSnapshotCustomAnimSlots(void)
{
    void *snapshot = malloc(sizeof(s_CustomAnimCatalogIds));
    if (snapshot) memcpy(snapshot, s_CustomAnimCatalogIds,
        sizeof(s_CustomAnimCatalogIds));
    return snapshot;
}

s32 assetCatalogRestoreCustomAnimSlots(const void *snapshot)
{
    if (!snapshot) return 0;
    memcpy(s_CustomAnimCatalogIds, snapshot, sizeof(s_CustomAnimCatalogIds));
    return 1;
}

void assetCatalogDestroyCustomAnimSlotSnapshot(void *snapshot)
{
    free(snapshot);
}

s32 assetCatalogAnimationCategoryUsesCharacterClip(const char *category)
{
    return !category || strcmp(category, "weapon_animation") != 0;
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

s32 assetCatalogResolveAnimPrivateSlot(const char *catalog_id)
{
    s32 idx = s_allocate(s_CustomAnimCatalogIds, ANIM_CUSTOM_COUNT,
        catalog_id);

    if (idx < 0) {
        if (catalog_id && catalog_id[0]) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.ANIM.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom "
                "anim slots available (%d)",
                catalog_id, ANIM_CUSTOM_COUNT);
        }
        return -1;
    }

    return ANIM_CUSTOM_START + idx;
}
