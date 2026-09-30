#include <string.h>
#include <stdlib.h>
#include <limits.h>

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
static unsigned s_CustomModelPins[MODEL_CUSTOM_COUNT];
static unsigned char s_CustomModelReserved[MODEL_CUSTOM_COUNT];

typedef struct model_slot_snapshot {
    char ids[MODEL_CUSTOM_COUNT][CATALOG_ID_LEN];
    unsigned char reserved[MODEL_CUSTOM_COUNT];
} model_slot_snapshot_t;

void *assetCatalogSnapshotCustomModelSlots(void)
{
    model_slot_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (snapshot) {
        memcpy(snapshot->ids, s_CustomModelCatalogIds, sizeof(snapshot->ids));
        memcpy(snapshot->reserved, s_CustomModelReserved, sizeof(snapshot->reserved));
    }
    return snapshot;
}

s32 assetCatalogRestoreCustomModelSlots(const void *snapshot)
{
    if (!snapshot) return 0;
    const model_slot_snapshot_t *saved = snapshot;
    /* Preflight every slot before changing any reservation. A retained source
     * cannot be rebound, including by restoring the same ID at another slot. */
    for (s32 i = 0; i < MODEL_CUSTOM_COUNT; ++i) {
        if (!s_CustomModelPins[i]) continue;
        if (saved->reserved[i] && strcmp(saved->ids[i], s_CustomModelCatalogIds[i])) {
            sysLogPrintf(LOG_ERROR, "CATALOG.MODEL.SLOT_RESTORE_FAIL: live source identity at slot=%d", MODEL_CUSTOM_START + i);
            return 0;
        }
        for (s32 j = 0; j < MODEL_CUSTOM_COUNT; ++j) {
            if (j != i && saved->reserved[j] && !strcmp(saved->ids[j], s_CustomModelCatalogIds[i])) {
                sysLogPrintf(LOG_ERROR, "CATALOG.MODEL.SLOT_RESTORE_FAIL: live source would move slot=%d", MODEL_CUSTOM_START + i);
                return 0;
            }
        }
    }
    for (s32 i = 0; i < MODEL_CUSTOM_COUNT; ++i) {
        s_CustomModelReserved[i] = saved->reserved[i];
        if (!s_CustomModelPins[i]) {
            if (saved->reserved[i]) memcpy(s_CustomModelCatalogIds[i], saved->ids[i], CATALOG_ID_LEN);
            else s_CustomModelCatalogIds[i][0] = 0;
        }
    }
    return 1;
}

void assetCatalogDestroyCustomModelSlotSnapshot(void *snapshot)
{
    free(snapshot);
}

_Static_assert(MODEL_CUSTOM_COUNT > 0,
    "model custom slot count must be positive");

void assetCatalogResetCustomModelSlots(void)
{
    memset(s_CustomModelReserved, 0, sizeof(s_CustomModelReserved));
    for (s32 i = 0; i < MODEL_CUSTOM_COUNT; ++i) {
        if (!s_CustomModelPins[i]) s_CustomModelCatalogIds[i][0] = 0;
    }
}

/* Dedup-or-allocate a private slot index in [0, count). Returns the local
 * index, or -1 when the range is exhausted (caller logs the loud failure). */
static s32 s_allocate(char ids[][CATALOG_ID_LEN], s32 count,
        const char *catalog_id)
{
    s32 i;

    if (!catalog_id || !catalog_id[0] || strlen(catalog_id) >= CATALOG_ID_LEN) {
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

    s_CustomModelReserved[idx] = 1;
    return MODEL_CUSTOM_START + idx;
}

static s32 s_exactSlot(const char *id, s32 slot)
{
    if (!id || !id[0] || strlen(id) >= CATALOG_ID_LEN
            || slot < MODEL_CUSTOM_START || slot >= MODEL_CUSTOM_END) return -1;
    s32 local = slot - MODEL_CUSTOM_START;
    return strcmp(s_CustomModelCatalogIds[local], id) == 0 ? local : -1;
}

s32 assetCatalogPinModelPrivateSlot(const char *id, s32 slot)
{
    s32 local = s_exactSlot(id, slot);
    if (local < 0 || s_CustomModelPins[local] == UINT_MAX) {
        sysLogPrintf(LOG_ERROR, "CATALOG.MODEL.SLOT_PIN_FAIL: exact source identity required slot=%d", slot);
        return 0;
    }
    ++s_CustomModelPins[local];
    return 1;
}

s32 assetCatalogReleaseModelPrivateSlot(const char *id, s32 slot)
{
    s32 local = s_exactSlot(id, slot);
    if (local < 0 || !s_CustomModelPins[local]) {
        sysLogPrintf(LOG_ERROR, "CATALOG.MODEL.SLOT_RELEASE_FAIL: no matching live pin slot=%d", slot);
        return 0;
    }
    if (--s_CustomModelPins[local] == 0 && !s_CustomModelReserved[local])
        s_CustomModelCatalogIds[local][0] = 0;
    return 1;
}

s32 assetCatalogModelPrivateSourceFilenum(s32 runtime_model_slot)
{
    enum {
        MODEL_CUSTOM_SOURCE_FILENUM_START = 0x7e0,
        MODEL_CUSTOM_SOURCE_FILENUM_END =
            MODEL_CUSTOM_SOURCE_FILENUM_START + MODEL_CUSTOM_COUNT
    };

    _Static_assert(MODEL_CUSTOM_SOURCE_FILENUM_END <= 0x800,
        "custom model source filenums must stay inside ROMDATA_MAX_FILES");

    if (runtime_model_slot < MODEL_CUSTOM_START
            || runtime_model_slot >= MODEL_CUSTOM_END) {
        return -1;
    }

    return MODEL_CUSTOM_SOURCE_FILENUM_START
        + (runtime_model_slot - MODEL_CUSTOM_START);
}
