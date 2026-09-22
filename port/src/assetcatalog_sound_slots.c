#include <string.h>
#include <stdlib.h>

#include <PR/ultratypes.h>

#include "constants.h"   /* SND_CUSTOM_* */
#include "assetcatalog.h"
#include "assetcatalog_sound_slots.h"
#include "system.h"

/*
 * c3849 Wave 2: catalog-owned private custom-sound slot allocator.
 * See port/include/assetcatalog_sound_slots.h. Mirrors
 * port/src/assetcatalog_model_slots.c.
 */

static char s_CustomSoundCatalogIds[SND_CUSTOM_COUNT][CATALOG_ID_LEN];
static u8 s_GenerationSlots[SND_CUSTOM_COUNT];
static u32 s_SnapshotReservations[SND_CUSTOM_COUNT];
typedef struct sound_slot_snapshot {
    char ids[SND_CUSTOM_COUNT][CATALOG_ID_LEN];
} sound_slot_snapshot_t;

_Static_assert(SND_CUSTOM_COUNT > 0,
    "sound custom slot count must be positive");
/* union soundnumhack's id field is 11 bits; bits above 0x800 carry
 * mp3priority/hasconfig semantics and must never collide with a slot. */
_Static_assert(SND_CUSTOM_END <= 0x800,
    "sound custom slots must fit the 11-bit soundnum id field");

void assetCatalogResetCustomSoundSlots(void)
{
    memset(s_CustomSoundCatalogIds, 0, sizeof(s_CustomSoundCatalogIds));
}

void *assetCatalogSnapshotCustomSoundSlots(void)
{
    sound_slot_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return NULL;
    memcpy(snapshot->ids, s_CustomSoundCatalogIds, sizeof(snapshot->ids));
    for (s32 i = 0; i < SND_CUSTOM_COUNT; ++i)
        if (snapshot->ids[i][0]) ++s_SnapshotReservations[i];
    return snapshot;
}
s32 assetCatalogRestoreCustomSoundSlots(const void *snapshot)
{
    if (!snapshot) return 0;
    const sound_slot_snapshot_t *saved = snapshot;
    for (s32 i = 0; i < SND_CUSTOM_COUNT; ++i)
        if (s_GenerationSlots[i] && saved->ids[i][0]) return 0;
    memcpy(s_CustomSoundCatalogIds, saved->ids, sizeof(s_CustomSoundCatalogIds));
    return 1;
}
void assetCatalogDestroyCustomSoundSlotSnapshot(void *snapshot)
{
    if (!snapshot) return;
    const sound_slot_snapshot_t *saved = snapshot;
    for (s32 i = 0; i < SND_CUSTOM_COUNT; ++i)
        if (saved->ids[i][0]) --s_SnapshotReservations[i];
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
        if (!ids[i][0] && !s_GenerationSlots[i]) {
            strncpy(ids[i], catalog_id, CATALOG_ID_LEN - 1);
            ids[i][CATALOG_ID_LEN - 1] = '\0';
            return i;
        }
    }

    return -1;
}

s32 assetCatalogResolveSoundPrivateSlot(const char *catalog_id)
{
    s32 idx = s_allocate(s_CustomSoundCatalogIds, SND_CUSTOM_COUNT,
        catalog_id);

    if (idx < 0) {
        if (catalog_id && catalog_id[0]) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.SOUND.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom "
                "sound slots available (%d)",
                catalog_id, SND_CUSTOM_COUNT);
        }
        return -1;
    }

    return SND_CUSTOM_START + idx;
}

s32 assetCatalogReserveSoundGenerationSlot(void)
{
    for (s32 i = SND_CUSTOM_COUNT; i-- > 0;) {
        if (!s_CustomSoundCatalogIds[i][0] && !s_GenerationSlots[i] && !s_SnapshotReservations[i]) {
            s_GenerationSlots[i] = 1;
            return SND_CUSTOM_START + i;
        }
    }
    return -1;
}
void assetCatalogReleaseSoundGenerationSlot(s32 slot)
{
    if (slot >= SND_CUSTOM_START && slot < SND_CUSTOM_END)
        s_GenerationSlots[slot - SND_CUSTOM_START] = 0;
}
