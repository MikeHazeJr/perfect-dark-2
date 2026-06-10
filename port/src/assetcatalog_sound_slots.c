#include <string.h>

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
