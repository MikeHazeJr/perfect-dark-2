#ifndef GAMEPLAY_GRAPH_DIVSHIFT_H
#define GAMEPLAY_GRAPH_DIVSHIFT_H

/*
 * Fixed-width guarded divide and logical right shift.
 *
 * Intended compilation assumptions (asserted only in the C++ implementation
 * translation unit; no calling environment has been compiled or tested):
 *   CHAR_BIT == 8, sizeof(int32_t) == sizeof(uint32_t) == 4,
 *   INT32_MIN == -2147483648, INT32_MAX == 2147483647,
 *   UINT32_MAX == 4294967295.
 *
 * Check order for both operations:
 *   1. Either input pointer null      -> GDS_NULL_INPUT (even if output is null)
 *   2. Output pointer null            -> GDS_NULL_OUTPUT
 *   3. Both operands are snapshotted, then operand guards apply.
 * Every non-OK return performs no write through any pointer. Inputs are
 * never written. On GDS_OK the output is assigned exactly once.
 *
 * Supported aliasing: the output may equal either input, and the two input
 * pointers may equal each other, for valid aligned objects of the same type.
 * Partial overlap and cross-type aliasing are outside the declared domain.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gds_status {
    GDS_OK = 0,
    GDS_NULL_INPUT = 1,
    GDS_NULL_OUTPUT = 2,
    GDS_DIV_ZERO = 3,
    GDS_DIV_OVERFLOW = 4,
    GDS_SHIFT_RANGE = 5
} gds_status;

/* Signed 32-bit division truncated toward zero.
 * Denominator 0 -> GDS_DIV_ZERO; then INT32_MIN / -1 -> GDS_DIV_OVERFLOW. */
gds_status gds_i32_div_trunc(const int32_t *numerator,
                             const int32_t *denominator, int32_t *output);

/* Unsigned logical right shift; upper bits are filled with zero.
 * Full uint32_t count; counts 0..31 succeed, any count >= 32 -> GDS_SHIFT_RANGE. */
gds_status gds_u32_shr(const uint32_t *value,
                       const uint32_t *count, uint32_t *output);

/* Authored test entry, defined only in the test translation unit.
 * Returns 0 when every authored check passes, otherwise a positive
 * failure count. */
int gds_run_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* GAMEPLAY_GRAPH_DIVSHIFT_H */
