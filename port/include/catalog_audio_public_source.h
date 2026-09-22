#ifndef CATALOG_AUDIO_PUBLIC_SOURCE_H
#define CATALOG_AUDIO_PUBLIC_SOURCE_H

#include "assetcatalog_scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct catalog_audio_public_source {
    char file_path[FS_MAXPATH];
    s32 has_keymap, key_min, key_max, key_base, key_detune;
    s32 velocity_min, velocity_max, sample_pan, sample_volume;
    s32 has_loop, has_envelope;
    u32 loop_start_samples, loop_end_samples, loop_count;
    u32 attack_time_us, decay_time_us, release_time_us;
    s32 attack_volume, decay_volume;
    char voice_actor[64], voice_transcript[512], voice_language[16], voice_context[128];
    char subtitle_file[FS_MAXPATH], fallback_locale[16];
    char locale_audio_files[6][FS_MAXPATH];
} catalog_audio_public_source_t;

/* Parse the public INI fields only. Intrinsic source format/rate/size and
 * private/native IDs never participate. out is empty on failure. Paths may
 * already be qualified by the owning scanner; traversal always rejects. */
s32 catalogAudioPublicSourceParse(const ini_section_t *ini, s32 require_file,
    catalog_audio_public_source_t *out);
/* Applies an already prepared candidate. The owner publishes the primary
 * provider path and handles catalog replacement/rollback separately. */
void catalogAudioPublicSourceApply(asset_entry_t *entry,
    const catalog_audio_public_source_t *source);

/* Private extraction provenance only; strict complete JSON and exact native
 * domains. Public authored numeric IDs are not read by the public parser. */
s32 catalogAudioNativeIdentityParse(const char *manifest, size_t length,
    s32 *soundnum, s32 *filenum);

#ifdef __cplusplus
}
#endif

#endif
