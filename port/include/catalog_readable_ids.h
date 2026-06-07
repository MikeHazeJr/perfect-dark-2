#ifndef PD_CATALOG_READABLE_IDS_H
#define PD_CATALOG_READABLE_IDS_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void catalogReadableAlphaOrdinal(s32 value, char *out, size_t out_n);
void catalogReadableAnimationId(s32 anim_idx, const char *fallback_role,
	char *out, size_t out_n);
void catalogReadableTextureId(s32 texture_idx, char *out, size_t out_n);
void catalogReadableSfxId(s32 sfx_idx, char *out, size_t out_n);
void catalogReadableVoiceId(s32 sfx_idx, char *out, size_t out_n);
void catalogReadableSoundRefId(s32 sound_ref, char *out, size_t out_n);
void catalogReadableSongId(s32 slot_idx, char *out, size_t out_n);
void catalogReadableModelIdForFile(s32 filenum, const char *hint_suffix,
	const char *fallback_role, char *out, size_t out_n);
void catalogReadableModelIdForModelnum(s32 modelnum, s32 filenum,
	char *out, size_t out_n);
void catalogReadableStageSceneId(const char *stage_id, const char *slot,
	s32 filenum, char *out, size_t out_n);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_READABLE_IDS_H */
