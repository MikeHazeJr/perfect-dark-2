#ifndef CATALOG_AUDIO_SOURCE_IDENTITY_H
#define CATALOG_AUDIO_SOURCE_IDENTITY_H

#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Derived exclusively from the prior canonical catalog row, never public
 * numeric metadata. Capture before registration can retire/reset that row. */
typedef struct catalog_audio_source_identity {
    char id[CATALOG_ID_LEN];
    s32 category;
    s32 sound_id;
    s32 source_soundnum;
    s32 source_filenum;
} catalog_audio_source_identity_t;

s32 catalogAudioSourceIdentityPrepare(const asset_entry_t *prior,
    const char *id, s32 category, catalog_audio_source_identity_t *out);
s32 catalogAudioSourceIdentityApply(asset_entry_t *entry,
    const catalog_audio_source_identity_t *identity);

#ifdef __cplusplus
}
#endif
#endif
