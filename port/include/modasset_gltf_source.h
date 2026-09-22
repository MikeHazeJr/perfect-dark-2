#ifndef PD_MODASSET_GLTF_SOURCE_H
#define PD_MODASSET_GLTF_SOURCE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct modasset_gltf_repeat_range {
	s16 repeattoframe;
	s16 repeatfromframe;
} modasset_gltf_repeat_range_t;

/* Resolve a URI relative to its source member without crossing its directory
 * or an archive boundary. URI percent encoding is decoded exactly once. */
s32 modAssetGltfBufferPath(const char *source_path, const char *uri,
		char *out, size_t out_size);
/* GLB allows up to three padding bytes outside the declared buffer. */
s32 modAssetGltfBufferSize(u32 declared_size, u32 actual_size, s32 glb);
/* Validate supported buffer data URI headers, complete canonical base64, and
 * exact decoded length without allocating decoded storage. */
s32 modAssetGltfDataUriSize(const char *uri, u32 declared_size);
typedef struct modasset_gltf_glb_chunks {
	const u8 *json;
	u32 json_size;
	const u8 *bin;
	u32 bin_size;
} modasset_gltf_glb_chunks_t;
/* Borrowed slices of one complete GLB v2 container; outputs clear on failure. */
s32 modAssetGltfGlbChunks(const u8 *data, u32 size,
		modasset_gltf_glb_chunks_t *out);
s32 modAssetGltfAccessorBounds(u32 buffer_size, u32 view_offset, u32 view_size,
		u32 accessor_offset, u32 count, u32 stride, u32 element_size);
/* Cache identity includes JSON and the exact resolved binary source bytes. */
s32 modAssetGltfSourceHash(const void *json, u32 json_size,
		const void *buffer, u32 buffer_size, u8 out_digest[32]);

/* Parse complete JSON arrays. Outputs are owned malloc allocations and remain
 * empty on rejection. Limits derive from the native u16 header representation,
 * not the former 512-row importer storage. Frame values must fit positive s16. */
s32 modAssetGltfRepeatRanges(const char *json, size_t size,
		modasset_gltf_repeat_range_t **out, s32 *out_count);
s32 modAssetGltfCutSkipFrames(const char *json, size_t size,
		s16 **out, s32 *out_count);

#ifdef __cplusplus
}
#endif
#endif
