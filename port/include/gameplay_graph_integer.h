#ifndef GAMEPLAY_GRAPH_INTEGER_H
#define GAMEPLAY_GRAPH_INTEGER_H

/*
 * Bounded fixed-width integer primitives for the gameplay graph.
 *
 * Proof boundary: these pure leaf primitives complete zero gameplay
 * behaviors. The reviewed compiler/IR selects promotions and expression
 * result types before calling them; 8-bit storage does not imply an 8-bit
 * intermediate. Dispatch, compilation, source-hashed editable programs,
 * authority, save, RNG, timing and full semantic coverage are not provided
 * here. No original gameplay function is represented by these operations.
 *
 * Value model:
 *   width     must be exactly 8, 16, 32 or 64.
 *   is_signed must be exactly 0 (unsigned tag) or 1 (two's-complement tag).
 *   bits      holds the value's low `width` bits; all higher bits must be 0.
 * Only the scalar field values of a successful result are promised. No raw
 * struct layout, padding, ABI or serialization guarantee is made.
 *
 * Caller preconditions: input objects are initialized, and output storage
 * does not overlap any input. Inputs are never modified.
 *
 * Output rule: every non-GGI_OK return leaves every nonnull output object
 * byte-for-byte unchanged. On GGI_OK the output is written exactly once.
 *
 * Error precedence (first failing check wins):
 *   ggi_validate: GGI_ERR_NULL, GGI_ERR_BAD_WIDTH, GGI_ERR_BAD_SIGNEDNESS,
 *                 GGI_ERR_UNNORMALIZED.
 *   ggi_compare / ggi_binary:
 *     1. any operand or output pointer null       -> GGI_ERR_NULL
 *     2. op not in this function's category        -> GGI_ERR_UNSUPPORTED_OP
 *     3. operand a: width, signedness, normalized  -> BAD_WIDTH /
 *                                                     BAD_SIGNEDNESS /
 *                                                     UNNORMALIZED
 *     4. operand b: same checks as a
 *     5. a and b differ in width or signedness     -> GGI_ERR_TYPE_MISMATCH
 *     6. operation domain refusal                  -> GGI_ERR_DOMAIN
 *   ggi_unary:   1. null, 2. op category, 3. operand checks.
 *   ggi_convert: 1. null, 2. source operand checks,
 *                3. dst_width not 8/16/32/64       -> GGI_ERR_BAD_WIDTH
 *                4. dst_signed not 0/1             -> GGI_ERR_BAD_SIGNEDNESS
 *
 * Domain refusals (GGI_ERR_DOMAIN):
 *   GGI_OP_LT_S on unsigned-tagged operands.
 *   GGI_OP_LT_U on signed-tagged operands.
 *   GGI_OP_ADD_U / GGI_OP_SUB_U / GGI_OP_MUL_U on signed-tagged operands.
 *     Signed wrap, saturation, division, remainder and shifts are never
 *     provided.
 *
 * Operation tags are plain uint32_t values. Unknown tags and the
 * GGI_OP_EXCLUDED_* tags are always refused with GGI_ERR_UNSUPPORTED_OP and
 * are never executed.
 */

#include <stdint.h>

#if defined(__cplusplus)
#define GGI_NOEXCEPT noexcept
extern "C" {
#else
#define GGI_NOEXCEPT
#endif

typedef struct ggi_int {
    uint64_t bits;
    uint32_t width;
    uint32_t is_signed;
} ggi_int;

typedef uint32_t ggi_status;

#define GGI_OK                 0u
#define GGI_ERR_NULL           1u
#define GGI_ERR_UNSUPPORTED_OP 2u
#define GGI_ERR_BAD_WIDTH      3u
#define GGI_ERR_BAD_SIGNEDNESS 4u
#define GGI_ERR_UNNORMALIZED   5u
#define GGI_ERR_TYPE_MISMATCH  6u
#define GGI_ERR_DOMAIN         7u

/* ggi_compare operations. Result is a uint8_t holding exactly 0 or 1. */
#define GGI_OP_EQ   1u /* either signedness; operand types must match */
#define GGI_OP_LT_S 2u /* signed-tagged operands only */
#define GGI_OP_LT_U 3u /* unsigned-tagged operands only */

/* ggi_binary operations. Result has the operands' width and signedness. */
#define GGI_OP_AND   4u /* either signedness */
#define GGI_OP_OR    5u /* either signedness */
#define GGI_OP_XOR   6u /* either signedness */
#define GGI_OP_ADD_U 7u /* unsigned-tagged only; modulo 2^width */
#define GGI_OP_SUB_U 8u /* unsigned-tagged only; modulo 2^width */
#define GGI_OP_MUL_U 9u /* unsigned-tagged only; modulo 2^width */

/* ggi_unary operations. Result has the operand's width and signedness. */
#define GGI_OP_NOT 10u /* bitwise complement within width; either signedness */

/* Explicitly excluded operations. Always refused, never executed. */
#define GGI_OP_EXCLUDED_ADD_S 0x100u
#define GGI_OP_EXCLUDED_SUB_S 0x101u
#define GGI_OP_EXCLUDED_MUL_S 0x102u
#define GGI_OP_EXCLUDED_DIV_U 0x103u
#define GGI_OP_EXCLUDED_DIV_S 0x104u
#define GGI_OP_EXCLUDED_REM_U 0x105u
#define GGI_OP_EXCLUDED_REM_S 0x106u
#define GGI_OP_EXCLUDED_SHL   0x107u
#define GGI_OP_EXCLUDED_SHR_U 0x108u
#define GGI_OP_EXCLUDED_SHR_S 0x109u
#define GGI_OP_EXCLUDED_FLOAT 0x10Au

/* Checks one value against the value model. */
ggi_status ggi_validate(const ggi_int *v) GGI_NOEXCEPT;

/* EQ / LT_S / LT_U. Writes 0 or 1 to *out01 on success. */
ggi_status ggi_compare(uint32_t op, const ggi_int *a, const ggi_int *b,
                       uint8_t *out01) GGI_NOEXCEPT;

/* AND / OR / XOR / ADD_U / SUB_U / MUL_U. */
ggi_status ggi_binary(uint32_t op, const ggi_int *a, const ggi_int *b,
                      ggi_int *out) GGI_NOEXCEPT;

/* NOT. */
ggi_status ggi_unary(uint32_t op, const ggi_int *a, ggi_int *out) GGI_NOEXCEPT;

/*
 * Converts src to (dst_width, dst_signed). Widening sign-extends when the
 * SOURCE is signed-tagged and zero-extends when it is unsigned-tagged,
 * regardless of the destination tag. Narrowing keeps the low dst_width bits.
 * Same-width conversion preserves bits and changes only the tag.
 */
ggi_status ggi_convert(const ggi_int *src, uint32_t dst_width,
                       uint32_t dst_signed, ggi_int *out) GGI_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#endif /* GAMEPLAY_GRAPH_INTEGER_H */
