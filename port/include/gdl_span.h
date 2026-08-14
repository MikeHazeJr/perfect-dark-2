#ifndef PD_GDL_SPAN_H
#define PD_GDL_SPAN_H

#include <stddef.h>
#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the exact bytes remaining from a command-aligned cursor inside a
 * display-list allocation. The end pointer and misaligned cursors are outside
 * the span. out_remaining is cleared on every rejection.
 */
s32 gdlSpanBytesRemaining(const void *span_start, u32 span_bytes,
		const void *cursor, u32 *out_remaining);

/**
 * Return nonzero only when an exact, non-empty Gfx span is wholly contained in
 * its owning allocation. The subtraction form used by the implementation is
 * safe when untrusted offsets are near SIZE_MAX.
 */
s32 gdlSpanFitsAllocation(size_t allocation_bytes, size_t span_offset,
		size_t span_bytes);

/**
 * Return nonzero only when the span is a whole, non-empty Gfx sequence whose
 * final command is G_ENDDL. The caller owns readability of the supplied span.
 */
s32 gdlSpanEndsWithEnddl(const void *span_start, u32 span_bytes);

#ifdef __cplusplus
}
#endif

#endif /* PD_GDL_SPAN_H */
