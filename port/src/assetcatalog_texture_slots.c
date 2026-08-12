#include <string.h>
#include <stdlib.h>

#include <PR/ultratypes.h>

#include "constants.h"   /* TEXTURE_CUSTOM_* */
#include "assetcatalog.h"
#include "assetcatalog_texture_slots.h"
#include "system.h"

/*
 * c3849 Wave 2: catalog-owned private custom-texture slot allocator.
 * See port/include/assetcatalog_texture_slots.h. Mirrors
 * port/src/assetcatalog_model_slots.c.
 */

static char s_CustomTextureCatalogIds[TEXTURE_CUSTOM_COUNT][CATALOG_ID_LEN];

_Static_assert(TEXTURE_CUSTOM_COUNT > 0,
    "texture custom slot count must be positive");
/* struct tex texturenum is a 12-bit field and the G_NOOP marker pack
 * clamps at 4096; a slot past that would silently alias another texture. */
_Static_assert(TEXTURE_CUSTOM_END <= 4096,
    "texture custom slots must fit the 12-bit texturenum field");

void assetCatalogResetCustomTextureSlots(void)
{
    memset(s_CustomTextureCatalogIds, 0, sizeof(s_CustomTextureCatalogIds));
}

void *assetCatalogSnapshotCustomTextureSlots(void)
{
    void *snapshot = malloc(sizeof(s_CustomTextureCatalogIds));
    if (snapshot) memcpy(snapshot, s_CustomTextureCatalogIds,
        sizeof(s_CustomTextureCatalogIds));
    return snapshot;
}

s32 assetCatalogRestoreCustomTextureSlots(const void *snapshot)
{
    if (!snapshot) return 0;
    memcpy(s_CustomTextureCatalogIds, snapshot,
        sizeof(s_CustomTextureCatalogIds));
    return 1;
}

void assetCatalogDestroyCustomTextureSlotSnapshot(void *snapshot)
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

s32 assetCatalogResolveTexturePrivateSlot(const char *catalog_id)
{
    s32 idx = s_allocate(s_CustomTextureCatalogIds, TEXTURE_CUSTOM_COUNT,
        catalog_id);

    if (idx < 0) {
        if (catalog_id && catalog_id[0]) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.TEXTURE.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom "
                "texture slots available (%d)",
                catalog_id, TEXTURE_CUSTOM_COUNT);
        }
        return -1;
    }

    return TEXTURE_CUSTOM_START + idx;
}
