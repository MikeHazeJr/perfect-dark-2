#ifndef PD_ASSETCATALOG_SOUND_SLOTS_H
#define PD_ASSETCATALOG_SOUND_SLOTS_H

#include <PR/ultratypes.h>

/*
 * c3849 Wave 2: catalog-owned private custom-sound slot allocator.
 *
 * The sound analog of port/src/assetcatalog_model_slots.c (B-911). A net-new
 * custom .pdsfx/.pdvoice row authors no sound_id (the scanner default is -1),
 * so it never enters the soundnum reverse index and is unplayable. This
 * allocator maps such a catalog ID to a private soundnum in
 * [SND_CUSTOM_START, SND_CUSTOM_END) (constants.h). With the slot stored in
 * ext.audio.sound_id + source_soundnum, the EXISTING chain works unchanged:
 * catalogResolveSound(slot) hits the source_soundnum reverse index, sees a
 * FileProvider primary, and plays sample.wav; the native bank path safely
 * NULLs because slot >= g_NumSounds. No native table grows.
 *
 * Reverse resolution is the source_soundnum index, NOT the catalog runtime
 * cache: custom slots (>= 0x60A) exceed RT_CACHE_SIZE and the audio family
 * does not use catalogIdByRuntime.
 *
 * The private slot is migration debt only: it must never cross a public
 * boundary (wire / save / manifest / UI / mod tools). Catalog ID strings stay
 * the identity at every public boundary.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all custom sound slot reservations. Called wherever the catalog is
 * rebuilt, beside the other custom-slot resets. */
void assetCatalogResetCustomSoundSlots(void);

/* Resolve a catalog-owned private soundnum for a custom SFX/VOICE row with no
 * authored sound_id. Dedups by catalog id (same id -> same slot). Returns a
 * slot in [SND_CUSTOM_START, SND_CUSTOM_END), or -1 when the private range is
 * exhausted (logs CATALOG.SOUND.CUSTOM_SLOT_FAIL). */
s32 assetCatalogResolveSoundPrivateSlot(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* PD_ASSETCATALOG_SOUND_SLOTS_H */
