#include "gameplay_graph_divshift.h"

#include <climits>
#include <stdint.h>

/* Intended compilation assumptions; these do not establish that any actual
 * compiler or calling environment has been tested. */
static_assert(CHAR_BIT == 8, "8-bit bytes assumed");
static_assert(sizeof(int32_t) == 4, "int32_t must be 4 bytes");
static_assert(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");
static_assert(INT32_MIN == -2147483647 - 1, "INT32_MIN must be -2147483648");
static_assert(INT32_MAX == 2147483647, "INT32_MAX must be 2147483647");
static_assert(UINT32_MAX == 4294967295u, "UINT32_MAX must be 4294967295");

gds_status gds_i32_div_trunc(const int32_t *numerator,
                             const int32_t *denominator, int32_t *output)
{
    if (numerator == nullptr || denominator == nullptr) {
        return GDS_NULL_INPUT;
    }
    if (output == nullptr) {
        return GDS_NULL_OUTPUT;
    }

    /* Snapshot both operands before any store so aliased output is safe. */
    const int32_t n = *numerator;
    const int32_t d = *denominator;

    if (d == 0) {
        return GDS_DIV_ZERO;
    }
    if (n == INT32_MIN && d == -1) {
        return GDS_DIV_OVERFLOW;
    }

    /* Guarded operands: the quotient is representable; C++11 '/' truncates
     * toward zero. Single output assignment. */
    *output = n / d;
    return GDS_OK;
}

gds_status gds_u32_shr(const uint32_t *value,
                       const uint32_t *count, uint32_t *output)
{
    if (value == nullptr || count == nullptr) {
        return GDS_NULL_INPUT;
    }
    if (output == nullptr) {
        return GDS_NULL_OUTPUT;
    }

    /* Snapshot both operands before any store so aliased output is safe. */
    const uint32_t v = *value;
    const uint32_t c = *count;

    if (c >= 32u) {
        return GDS_SHIFT_RANGE;
    }

    /* v is unsigned and any promotion keeps it nonnegative, so this is a
     * logical shift; c is 0..31. Single output assignment. */
    *output = v >> c;
    return GDS_OK;
}
