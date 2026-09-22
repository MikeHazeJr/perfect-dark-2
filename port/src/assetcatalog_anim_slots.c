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
static u8 s_GenerationSlots[ANIM_CUSTOM_COUNT];
static u32 s_SnapshotReservations[ANIM_CUSTOM_COUNT];
typedef struct anim_slot_snapshot {
    char ids[ANIM_CUSTOM_COUNT][CATALOG_ID_LEN];
} anim_slot_snapshot_t;

_Static_assert(ANIM_CUSTOM_COUNT > 0,
    "anim custom slot count must be positive");
/* LOAD_MAX_ANIMS (assetcatalog_load.c) bounds the animnum override index. */
_Static_assert(ANIM_CUSTOM_END_SLOT == 0x8000,
    "animation slots must fit signed native cache and command records");

void assetCatalogResetCustomAnimSlots(void)
{
    memset(s_CustomAnimCatalogIds, 0, sizeof(s_CustomAnimCatalogIds));
}

void *assetCatalogSnapshotCustomAnimSlots(void)
{
    anim_slot_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return NULL;
    memcpy(snapshot->ids, s_CustomAnimCatalogIds, sizeof(snapshot->ids));
    for (s32 i = 0; i < ANIM_CUSTOM_COUNT; ++i)
        if (snapshot->ids[i][0]) ++s_SnapshotReservations[i];
    return snapshot;
}

s32 assetCatalogRestoreCustomAnimSlots(const void *snapshot)
{
    if (!snapshot) return 0;
    const anim_slot_snapshot_t *saved = snapshot;
    for (s32 i = 0; i < ANIM_CUSTOM_COUNT; ++i)
        if (s_GenerationSlots[i] && saved->ids[i][0]) return 0;
    memcpy(s_CustomAnimCatalogIds, saved->ids, sizeof(s_CustomAnimCatalogIds));
    return 1;
}

void assetCatalogDestroyCustomAnimSlotSnapshot(void *snapshot)
{
    if (!snapshot) return;
    const anim_slot_snapshot_t *saved = snapshot;
    for (s32 i = 0; i < ANIM_CUSTOM_COUNT; ++i)
        if (saved->ids[i][0]) --s_SnapshotReservations[i];
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
        if (!ids[i][0] && !s_GenerationSlots[i]) {
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

s32 assetCatalogReserveAnimGenerationSlot(void)
{
    /* Snapshot reservations prevent in-flight rollback reclaiming this slot. */
    for (s32 i = ANIM_CUSTOM_COUNT; i-- > 0;) {
        if (!s_CustomAnimCatalogIds[i][0] && !s_GenerationSlots[i] && !s_SnapshotReservations[i]) {
            s_GenerationSlots[i] = 1;
            return ANIM_CUSTOM_START + i;
        }
    }
    return -1;
}

void assetCatalogReleaseAnimGenerationSlot(s32 slot)
{
    if (slot >= ANIM_CUSTOM_START && slot < ANIM_CUSTOM_END_SLOT)
        s_GenerationSlots[slot - ANIM_CUSTOM_START] = 0;
}
