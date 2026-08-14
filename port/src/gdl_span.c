#include <stdint.h>

#include "gdl_span.h"

s32 gdlSpanBytesRemaining(const void *span_start, u32 span_bytes,
		const void *cursor, u32 *out_remaining)
{
	uintptr_t start;
	uintptr_t current;
	uintptr_t offset;

	if (out_remaining) {
		*out_remaining = 0;
	}
	if (!span_start || !cursor || !out_remaining ||
			span_bytes < sizeof(Gfx) ||
			span_bytes % sizeof(Gfx) != 0) {
		return 0;
	}

	start = (uintptr_t)span_start;
	current = (uintptr_t)cursor;
	if (current < start) {
		return 0;
	}

	offset = current - start;
	if (offset >= span_bytes || offset % sizeof(Gfx) != 0) {
		return 0;
	}

	*out_remaining = span_bytes - (u32)offset;
	return 1;
}

s32 gdlSpanFitsAllocation(size_t allocation_bytes, size_t span_offset,
		size_t span_bytes)
{
	if (span_bytes < sizeof(Gfx) || span_bytes % sizeof(Gfx) != 0 ||
			span_offset > allocation_bytes) {
		return 0;
	}

	return span_bytes <= allocation_bytes - span_offset;
}

s32 gdlSpanEndsWithEnddl(const void *span_start, u32 span_bytes)
{
	const Gfx *commands;
	u32 command_count;

	if (!span_start || span_bytes < sizeof(Gfx) ||
			span_bytes % sizeof(Gfx) != 0) {
		return 0;
	}

	commands = (const Gfx *)span_start;
	command_count = span_bytes / sizeof(Gfx);
	return commands[command_count - 1].bytes[GFX_W0_BYTE(0)] ==
		(u8)G_ENDDL;
}
