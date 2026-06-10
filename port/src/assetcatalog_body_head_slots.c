#include <string.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_body_head_slots.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_heads.h"
#include "system.h"

/*
 * c3844 Gate 2: catalog-owned private body/head runtime-slot allocator.
 * See port/include/assetcatalog_body_head_slots.h. Mirrors
 * port/src/assetcatalog_weapon_slots.c.
 */

static char s_CustomBodyCatalogIds[CATALOG_MGR_BODY_CUSTOM_COUNT][CATALOG_ID_LEN];
static char s_CustomHeadCatalogIds[CATALOG_MGR_HEAD_CUSTOM_COUNT][CATALOG_ID_LEN];

void assetCatalogResetCustomBodyHeadSlots(void)
{
    memset(s_CustomBodyCatalogIds, 0, sizeof(s_CustomBodyCatalogIds));
    memset(s_CustomHeadCatalogIds, 0, sizeof(s_CustomHeadCatalogIds));
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

s32 assetCatalogResolveBodyPrivateSlot(const char *catalog_id)
{
    s32 idx = s_allocate(s_CustomBodyCatalogIds, CATALOG_MGR_BODY_CUSTOM_COUNT,
        catalog_id);

    if (idx < 0) {
        if (catalog_id && catalog_id[0]) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.BODY.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom "
                "body slots available (%d)",
                catalog_id, CATALOG_MGR_BODY_CUSTOM_COUNT);
        }
        return -1;
    }

    return CATALOG_MGR_BODY_CUSTOM_START + idx;
}

s32 assetCatalogResolveHeadPrivateSlot(const char *catalog_id)
{
    s32 idx = s_allocate(s_CustomHeadCatalogIds, CATALOG_MGR_HEAD_CUSTOM_COUNT,
        catalog_id);

    if (idx < 0) {
        if (catalog_id && catalog_id[0]) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.HEAD.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom "
                "head slots available (%d)",
                catalog_id, CATALOG_MGR_HEAD_CUSTOM_COUNT);
        }
        return -1;
    }

    return CATALOG_MGR_HEAD_CUSTOM_START + idx;
}
