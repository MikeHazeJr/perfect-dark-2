#ifndef _IN_MOD_H
#define _IN_MOD_H

#include <PR/ultratypes.h>

struct animtableentry;

typedef struct mod_texture_rgba32_source {
	u8 *pixels;
	s32 width;
	s32 height;
	s32 stride_pixels;
	s32 data_size;
	s32 catalog_id;
	const char *path;
} mod_texture_rgba32_source_t;

s32 modTextureLoad(u16 num, void *dst, u32 dstSize);
s32 modTextureLoadRgba32Source(u16 num, mod_texture_rgba32_source_t *out);
void modTextureFreeRgba32Source(mod_texture_rgba32_source_t *source);

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim);
void *modAnimationLoadData(u16 num);

/* C-6 supplement: returns loaded animation data if a catalog mod override
 * exists for this ROM-based animation (data != 0xffffffff). Returns NULL if
 * no override is registered — caller falls through to ROM DMA. */
void *modAnimationTryCatalogOverride(u16 num);

void *modSequenceLoad(u16 num, u32 *outSize);
s32 modSequencePlayAudioSource(u16 num);
s32 modSequenceVirtualTrackForCatalogId(const char *catalog_id);
const char *modSequenceVirtualTrackId(s32 tracknum);
s32 modSequenceIsVirtualTrack(s32 tracknum);

#endif
