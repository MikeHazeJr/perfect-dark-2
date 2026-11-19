#ifndef _IN_MOD_H
#define _IN_MOD_H

#include <PR/ultratypes.h>
#include <stddef.h>

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
/* Decode exact borrowed image bytes into caller-owned native RGBA storage.
 * No catalog lookup, file reopen, fatal error or ROM fallback; free with
 * modTextureFreeRgba32Source. Uses the same decoder/layout as normal loading. */
s32 modTextureDecodeRgba32Source(const void *bytes, u32 size,
    mod_texture_rgba32_source_t *out, char *error, size_t cap);
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

/* Grid standalone sequence packages use the native public source compiler.
 * Shareable checks the exact sequenced archive shape without reserving a slot. */
s32 modSequenceGridSourceShareable(const char *name, const void *bytes, u32 size);
s32 modSequenceValidateGridSource(const char *name, const void *bytes, u32 size,
    char *err, size_t err_cap);

/* Temporary sequence reservations are separate from installed playlist IDs.
 * Retire only after the native stage, its cached tracks and its players stop. */
s32 modSequenceReserveGridTrack(const char *catalog_id);
s32 modSequenceCanRetireGridTrack(const char *catalog_id, s32 tracknum);
void modSequenceForgetGridStageTracks(void);
s32 modSequenceRetireGridTrack(const char *catalog_id, s32 tracknum);

#endif
