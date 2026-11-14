/*
 * gameplay_graph_bitrep.h - anonymous four-byte pointer bit-representation
 * boundary (b32). C99-compatible public header; C linkage; C++ noexcept.
 *
 * Status: authored source only. Not compiled, not run, not ABI/layout/FP
 * qualified. No application behavior is assigned.
 *
 * Every scalar payload crosses this boundary through a pointer. No API
 * accepts, returns or forwards a float by value.
 *
 * Semantics of each conversion API:
 *   1. If input is null or output is null (including both), return B32_NULL
 *      before any read or write.
 *   2. Otherwise memcpy exactly four bytes from *input to *output, then
 *      return B32_OK. The source is read only. Nothing else is stored, and
 *      there is no validation, fallback, retry or refusal after the copy.
 *
 * Unchecked caller preconditions (NOT verified by these APIs):
 *   - Every nonnull pointer designates an initialized, live, appropriately
 *     aligned object of its declared type.
 *   - The four-byte representation of *input is valid for the assumed PC
 *     model.
 *   - The input and output objects do not overlap.
 *   - The caller prevents concurrent mutation of both objects and keeps both
 *     alive for the whole call.
 *   - The null checks do not prove pointer validity, aliasing or lifetime.
 *     No overlap or address-ordering check is performed.
 *
 * Explicit target assumptions (caller/target model, NOT a portable-ABI
 * qualification claim):
 *   - CHAR_BIT == 8.
 *   - sizeof(int32_t) == sizeof(uint32_t) == sizeof(float) == 4.
 *   - int32_t is the ordinary two's-complement 32-bit domain.
 *   - uint32_t has 32 value bits.
 *   - float uses the PC IEEE-754 binary32 object representation.
 * The implementation's static_assert checks on standard limits cannot prove
 * NaN layout, ABI, struct layout or floating-point environment properties.
 *
 * There is no serialized identity, allocation, ownership transfer or
 * interpretation of any pointer's address.
 */
#ifndef GAMEPLAY_GRAPH_BITREP_H
#define GAMEPLAY_GRAPH_BITREP_H

#include <stdint.h>

#ifdef __cplusplus
#define B32_NOEXCEPT noexcept
extern "C" {
#else
#define B32_NOEXCEPT
#endif

typedef uint32_t b32_status;

#define B32_OK ((b32_status)0u)
#define B32_NULL ((b32_status)1u)

b32_status b32_i32_to_u32(const int32_t *input, uint32_t *output) B32_NOEXCEPT;
b32_status b32_u32_to_i32(const uint32_t *input, int32_t *output) B32_NOEXCEPT;
b32_status b32_f32_to_u32(const float *input, uint32_t *output) B32_NOEXCEPT;
b32_status b32_u32_to_f32(const uint32_t *input, float *output) B32_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* GAMEPLAY_GRAPH_BITREP_H */
