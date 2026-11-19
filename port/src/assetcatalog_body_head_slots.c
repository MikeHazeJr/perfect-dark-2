#include <string.h>
#include <stdlib.h>

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

typedef struct body_head_slot_snapshot {
    char bodies[CATALOG_MGR_BODY_CUSTOM_COUNT][CATALOG_ID_LEN];
    char heads[CATALOG_MGR_HEAD_CUSTOM_COUNT][CATALOG_ID_LEN];
} body_head_slot_snapshot_t;

void assetCatalogResetCustomBodyHeadSlots(void)
{
    memset(s_CustomBodyCatalogIds, 0, sizeof(s_CustomBodyCatalogIds));
    memset(s_CustomHeadCatalogIds, 0, sizeof(s_CustomHeadCatalogIds));
}

void *assetCatalogSnapshotCustomBodyHeadSlots(void)
{
    body_head_slot_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return NULL;
    memcpy(snapshot->bodies, s_CustomBodyCatalogIds,
        sizeof(snapshot->bodies));
    memcpy(snapshot->heads, s_CustomHeadCatalogIds,
        sizeof(snapshot->heads));
    return snapshot;
}

s32 assetCatalogRestoreCustomBodyHeadSlots(const void *opaque)
{
    const body_head_slot_snapshot_t *snapshot = opaque;
    if (!snapshot) return 0;
    memcpy(s_CustomBodyCatalogIds, snapshot->bodies,
        sizeof(s_CustomBodyCatalogIds));
    memcpy(s_CustomHeadCatalogIds, snapshot->heads,
        sizeof(s_CustomHeadCatalogIds));
    return 1;
}

void assetCatalogDestroyCustomBodyHeadSlotSnapshot(void *snapshot)
{
    free(snapshot);
}

static s32 s_find(char ids[][CATALOG_ID_LEN], s32 count, const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) return -1;
    for (s32 i = 0; i < count; i++) {
        if (ids[i][0] && strncmp(ids[i], catalog_id, CATALOG_ID_LEN) == 0) return i;
    }
    return -1;
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

    i = s_find(ids, count, catalog_id);
    if (i >= 0) return i;

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

s32 assetCatalogFindBodyHeadPrivateSlot(s32 is_head, const char *catalog_id)
{
    const s32 index = is_head
        ? s_find(s_CustomHeadCatalogIds, CATALOG_MGR_HEAD_CUSTOM_COUNT, catalog_id)
        : s_find(s_CustomBodyCatalogIds, CATALOG_MGR_BODY_CUSTOM_COUNT, catalog_id);
    if (index < 0) return -1;
    return index + (is_head ? CATALOG_MGR_HEAD_CUSTOM_START : CATALOG_MGR_BODY_CUSTOM_START);
}

s32 assetCatalogRetireBodyHeadReservation(s32 is_head, const char *catalog_id, s32 slot)
{
    if (slot < 0 || assetCatalogFindBodyHeadPrivateSlot(is_head, catalog_id) != slot) return 0;
    char *reservation = is_head
        ? s_CustomHeadCatalogIds[slot - CATALOG_MGR_HEAD_CUSTOM_START]
        : s_CustomBodyCatalogIds[slot - CATALOG_MGR_BODY_CUSTOM_START];
    memset(reservation, 0, CATALOG_ID_LEN);
    return 1;
}
