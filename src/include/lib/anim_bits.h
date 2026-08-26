#ifndef _IN_LIB_ANIM_BITS_H
#define _IN_LIB_ANIM_BITS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

enum anim_frame_layout_result {
	ANIM_FRAME_LAYOUT_OK = 0,
	ANIM_FRAME_LAYOUT_INVALID_ARGUMENT,
	ANIM_FRAME_LAYOUT_INVALID_FLAGS,
	ANIM_FRAME_LAYOUT_HEADER_TRUNCATED,
	ANIM_FRAME_LAYOUT_FIELD_WIDTH_INVALID,
	ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED,
	ANIM_FRAME_LAYOUT_DESCRIPTOR_TRAILING,
};

struct anim_frame_part_layout {
	const u8 *field_header;
	u32 frame_bit_offset;
	u32 frame_bit_count;
	u8 flags;
};

struct anim_frame_stream_layout {
	u32 descriptor_bytes;
	u32 frame_bit_count;
};

/* These pure helpers deliberately use s32 rather than the engine's bool macro
 * so they can be consumed by C++ validation code without importing types.h. */
s32 animReadBitsBounded(const u8 *ptr, u32 bytelen, u8 readbitlen,
		u32 bitoffset, u32 *result);
enum anim_frame_layout_result animFrameMeasurePartBounded(
		const u8 *field_header, u32 field_header_len, u8 flags,
		u32 *descriptor_bytes, u32 *frame_bits);
enum anim_frame_layout_result animFrameMeasureDescriptorStreamBounded(
		const u8 *header, u32 headerlen, u32 framebytelen, u32 partcount,
		struct anim_frame_stream_layout *layout);
enum anim_frame_layout_result animFrameLocatePartBounded(
		const u8 *header, u32 headerlen, u32 framebytelen, s32 part,
		struct anim_frame_part_layout *layout);
const char *animFrameLayoutResultString(enum anim_frame_layout_result result);

#ifdef __cplusplus
}
#endif

#endif
