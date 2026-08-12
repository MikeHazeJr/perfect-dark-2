#include <limits.h>

#include "mp3_source_bounds.h"

s32 mp3SourceAllocationSize(u32 source_size, u32 *out_size)
{
	u32 allocation_size;

	if (!out_size || source_size == 0 || source_size > (u32)INT_MAX) {
		return 0;
	}

	allocation_size = source_size + MP3_SOURCE_DMA_WINDOW;
	*out_size = allocation_size;
	return 1;
}

s32 mp3SourceClampRead(s32 source_size, s32 offset, s32 requested)
{
	s32 remaining;

	if (source_size <= 0 || offset < 0 || offset >= source_size || requested <= 0) {
		return 0;
	}

	remaining = source_size - offset;
	return requested < remaining ? requested : remaining;
}
