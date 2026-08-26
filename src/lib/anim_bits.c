#include "lib/anim_bits.h"
#include "constants.h"

#define ANIM_FRAME_KNOWN_FLAGS (ANIMFIELD_S16_ROTATE \
		| ANIMFIELD_S16_TRANSLATE | ANIMFIELD_08 \
		| ANIMFIELD_F32_ROTATE | ANIMFIELD_S32_TRANSLATE \
		| ANIMFIELD_CAMERA | ANIMFIELD_F32_SCALE)

enum anim_frame_layout_result animFrameMeasurePartBounded(
		const u8 *header, u32 headerlen, u8 flags,
		u32 *headerbytes, u32 *framebits)
{
	const u8 *ptr = header;
	u32 needed = 0;
	u64 bits = 0;
	u8 translationmodes;

	if (header == NULL || headerbytes == NULL || framebits == NULL) {
		return ANIM_FRAME_LAYOUT_INVALID_ARGUMENT;
	}

	if ((flags & ~ANIM_FRAME_KNOWN_FLAGS) != 0) {
		return ANIM_FRAME_LAYOUT_INVALID_FLAGS;
	}

	translationmodes = ((flags & ANIMFIELD_08) != 0)
		+ ((flags & ANIMFIELD_S16_TRANSLATE) != 0)
		+ ((flags & ANIMFIELD_S32_TRANSLATE) != 0);

	if (translationmodes > 1
			|| ((flags & ANIMFIELD_S16_ROTATE) != 0
				&& (flags & ANIMFIELD_F32_ROTATE) != 0)) {
		return ANIM_FRAME_LAYOUT_INVALID_FLAGS;
	}

	if (flags & ANIMFIELD_08) {
		needed += 12;
	} else if (flags & ANIMFIELD_S16_TRANSLATE) {
		needed += 9;
	} else if (flags & ANIMFIELD_S32_TRANSLATE) {
		needed += 15;
	}

	if (flags & ANIMFIELD_S16_ROTATE) {
		needed += 9;
	}

	if (flags & ANIMFIELD_CAMERA) {
		needed += 5;
	}

	if (needed > headerlen) {
		return ANIM_FRAME_LAYOUT_HEADER_TRUNCATED;
	}

#define ANIM_ADD_FIELD_BITS(width, maxwidth) \
	do { \
		u8 anim_width = (width); \
		if (anim_width > (maxwidth)) { \
			return ANIM_FRAME_LAYOUT_FIELD_WIDTH_INVALID; \
		} \
		bits += anim_width; \
	} while (0)

	if (flags & ANIMFIELD_08) {
		ANIM_ADD_FIELD_BITS(ptr[2], 32);
		ANIM_ADD_FIELD_BITS(ptr[5], 32);
		ANIM_ADD_FIELD_BITS(ptr[8], 32);
		ANIM_ADD_FIELD_BITS(ptr[11], 32);
		ptr += 12;
	} else if (flags & ANIMFIELD_S16_TRANSLATE) {
		ANIM_ADD_FIELD_BITS(ptr[2], 16);
		ANIM_ADD_FIELD_BITS(ptr[5], 16);
		ANIM_ADD_FIELD_BITS(ptr[8], 16);
		ptr += 9;
	} else if (flags & ANIMFIELD_S32_TRANSLATE) {
		ANIM_ADD_FIELD_BITS(ptr[0], 32);
		ANIM_ADD_FIELD_BITS(ptr[5], 32);
		ANIM_ADD_FIELD_BITS(ptr[10], 32);
		ptr += 15;
	}

	if (flags & ANIMFIELD_S16_ROTATE) {
		ANIM_ADD_FIELD_BITS(ptr[2], 16);
		ANIM_ADD_FIELD_BITS(ptr[5], 16);
		ANIM_ADD_FIELD_BITS(ptr[8], 16);
		ptr += 9;
	} else if (flags & ANIMFIELD_F32_ROTATE) {
		bits += 96;
	}

	if (flags & ANIMFIELD_CAMERA) {
		ANIM_ADD_FIELD_BITS(ptr[0], 32);
	}

	if (flags & ANIMFIELD_F32_SCALE) {
		bits += 96;
	}

#undef ANIM_ADD_FIELD_BITS

	*headerbytes = needed;
	*framebits = (u32)bits;
	return ANIM_FRAME_LAYOUT_OK;
}

/**
 * Measure an exact number of framed part descriptors through the same bounded
 * per-part contract used by runtime lookup. Producers use this before
 * publishing reconstructed animation data, so a flags byte cannot be omitted
 * or reclassified as out-of-band metadata without failing admission.
 */
enum anim_frame_layout_result animFrameMeasureDescriptorStreamBounded(
		const u8 *header, u32 headerlen, u32 framebytelen, u32 partcount,
		struct anim_frame_stream_layout *layout)
{
	const u8 *ptr = header;
	u64 bitoffset = 0;
	u64 bitcapacity = (u64)framebytelen * 8u;
	u32 index;

	if (header == NULL || layout == NULL || partcount == 0) {
		return ANIM_FRAME_LAYOUT_INVALID_ARGUMENT;
	}

	for (index = 0; index < partcount; index++) {
		enum anim_frame_layout_result result;
		u32 headerbytes;
		u32 framebits;
		u8 flags;

		if ((u64)(ptr - header) >= (u64)headerlen) {
			return ANIM_FRAME_LAYOUT_HEADER_TRUNCATED;
		}

		flags = *ptr++;
		result = animFrameMeasurePartBounded(ptr,
			headerlen - (u32)(ptr - header), flags,
			&headerbytes, &framebits);

		if (result != ANIM_FRAME_LAYOUT_OK) {
			return result;
		}

		if (bitoffset + framebits > bitcapacity) {
			return ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED;
		}

		ptr += headerbytes;
		bitoffset += framebits;
	}

	if ((u32)(ptr - header) != headerlen) {
		return ANIM_FRAME_LAYOUT_DESCRIPTOR_TRAILING;
	}

	layout->descriptor_bytes = (u32)(ptr - header);
	layout->frame_bit_count = (u32)bitoffset;
	return ANIM_FRAME_LAYOUT_OK;
}

/**
 * Read one big-endian bit-field from a frame payload without crossing the
 * payload boundary. Unlike animReadBits, this entry point owns the complete
 * span and can therefore reject truncated or internally inconsistent public
 * animation data before a decoder reads beyond it or stops making progress.
 */
s32 animReadBitsBounded(const u8 *ptr, u32 bytelen, u8 readbitlen,
		u32 bitoffset, u32 *result)
{
	u32 value = 0;
	u32 remaining = readbitlen;
	u64 bitlength = (u64)bytelen * 8u;
	u64 endoffset = (u64)bitoffset + (u64)readbitlen;

	if (result == NULL || readbitlen > 32 || endoffset > bitlength
			|| (ptr == NULL && readbitlen != 0)) {
		return false;
	}

	/* A zero-width field is a real legacy no-op, including for a zero-byte
	 * frame payload. Do not require a backing pointer when no byte is read. */
	if (readbitlen == 0) {
		*result = 0;
		return true;
	}

	while (remaining > 0) {
		u32 byteindex = bitoffset >> 3;
		u32 bitinbyte = bitoffset & 7u;
		u32 available = 8u - bitinbyte;
		u32 take = remaining < available ? remaining : available;
		u32 shift = available - take;
		u32 mask = (1u << take) - 1u;

		value = (value << take) | ((ptr[byteindex] >> shift) & mask);
		bitoffset += take;
		remaining -= take;
	}

	*result = value;
	return true;
}

/**
 * Locate one part descriptor and its frame-bit span without crossing either
 * the animation header or the fixed per-frame payload. This is the shared
 * admission boundary for the optimized decoder and its legacy generic
 * fallback: a format variant may select the generic path, but neither path is
 * allowed to rediscover bounds from unrelated pointers.
 */
enum anim_frame_layout_result animFrameLocatePartBounded(
		const u8 *header, u32 headerlen, u32 framebytelen, s32 part,
		struct anim_frame_part_layout *layout)
{
	const u8 *ptr = header;
	u64 bitoffset = 0;
	u64 bitcapacity = (u64)framebytelen * 8u;
	s32 index;

	if (header == NULL || layout == NULL || part < 0) {
		return ANIM_FRAME_LAYOUT_INVALID_ARGUMENT;
	}

	for (index = 0; index <= part; index++) {
		enum anim_frame_layout_result result;
		u32 headerbytes;
		u32 framebits;
		u8 flags;

		if ((u64)(ptr - header) >= (u64)headerlen) {
			return ANIM_FRAME_LAYOUT_HEADER_TRUNCATED;
		}

		flags = *ptr++;
		result = animFrameMeasurePartBounded(ptr,
			headerlen - (u32)(ptr - header), flags,
			&headerbytes, &framebits);

		if (result != ANIM_FRAME_LAYOUT_OK) {
			return result;
		}

		if (bitoffset + framebits > bitcapacity) {
			return ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED;
		}

		if (index == part) {
			layout->field_header = ptr;
			layout->frame_bit_offset = (u32)bitoffset;
			layout->frame_bit_count = framebits;
			layout->flags = flags;
			return ANIM_FRAME_LAYOUT_OK;
		}

		ptr += headerbytes;
		bitoffset += framebits;
	}

	return ANIM_FRAME_LAYOUT_INVALID_ARGUMENT;
}

const char *animFrameLayoutResultString(enum anim_frame_layout_result result)
{
	switch (result) {
	case ANIM_FRAME_LAYOUT_OK: return "ok";
	case ANIM_FRAME_LAYOUT_INVALID_ARGUMENT: return "invalid_argument";
	case ANIM_FRAME_LAYOUT_INVALID_FLAGS: return "invalid_flags";
	case ANIM_FRAME_LAYOUT_HEADER_TRUNCATED: return "header_truncated";
	case ANIM_FRAME_LAYOUT_FIELD_WIDTH_INVALID: return "field_width_invalid";
	case ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED: return "payload_truncated";
	case ANIM_FRAME_LAYOUT_DESCRIPTOR_TRAILING: return "descriptor_trailing";
	default: return "unknown";
	}
}
