/*
 * gameplay_graph_bool.h - anonymous uint32 truth and selection component.
 *
 * C99-compatible header. Functions have C linkage and are noexcept in C++.
 *
 * Semantics:
 *   - Inputs are passed by value. An input is true iff it is nonzero.
 *   - gbp_logical_binary checks for null output first, then the opcode.
 *     On success it writes canonical 0 or 1. GBP_XOR means the two truth
 *     values differ; it is not a bitwise XOR of the input patterns.
 *   - gbp_logical_not writes 1 iff a == 0, else 0.
 *   - gbp_select_u32 writes the entire selected uint32_t pattern unchanged:
 *     when_true iff condition != 0, else when_false.
 *   - Error returns write nothing to *out.
 *
 * Caller requirement (unchecked): a nonnull out must point to one live,
 * writable uint32_t object. Pointer validity beyond the null test is not
 * checked.
 *
 * Handoff gaps: compilation, execution, header/ABI compatibility and actual
 * caller integration are unverified. No application behavior is provided.
 */
#ifndef GAMEPLAY_GRAPH_BOOL_H
#define GAMEPLAY_GRAPH_BOOL_H

#include <stdint.h>

#ifdef __cplusplus
#define GBP_NOEXCEPT noexcept
#else
#define GBP_NOEXCEPT
#endif

typedef uint32_t gbp_status;

#define GBP_OK          ((uint32_t)0u)
#define GBP_NULL_OUTPUT ((uint32_t)1u)
#define GBP_BAD_OPCODE  ((uint32_t)2u)

#define GBP_AND ((uint32_t)1u)
#define GBP_OR  ((uint32_t)2u)
#define GBP_XOR ((uint32_t)3u)

#ifdef __cplusplus
extern "C" {
#endif

gbp_status gbp_logical_binary(uint32_t opcode, uint32_t a, uint32_t b, uint32_t *out) GBP_NOEXCEPT;

gbp_status gbp_logical_not(uint32_t a, uint32_t *out) GBP_NOEXCEPT;

gbp_status gbp_select_u32(uint32_t condition, uint32_t when_true, uint32_t when_false, uint32_t *out) GBP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* GAMEPLAY_GRAPH_BOOL_H */
