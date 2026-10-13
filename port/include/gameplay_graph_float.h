/*
 * gameplay_graph_float.h - anonymous binary32 scalar mechanics.
 *
 * Header is intended for C99 and C++11 translation units. Functions have
 * C linkage. GFP_NOEXCEPT expands to noexcept only for C++.
 *
 * This component implements zero application/gameplay behaviors. Caller and
 * build integration are outside this contract.
 */
#ifndef GAMEPLAY_GRAPH_FLOAT_H
#define GAMEPLAY_GRAPH_FLOAT_H

#include <stdint.h>

#if defined(__cplusplus)
#define GFP_NOEXCEPT noexcept
#else
#define GFP_NOEXCEPT
#endif

/* Status codes. */
#define GFP_OK           UINT32_C(0)
#define GFP_NULL_OUTPUT  UINT32_C(1)
#define GFP_BAD_OPCODE   UINT32_C(2)

/* gfp_binary opcodes. */
#define GFP_ADD          UINT32_C(1)
#define GFP_SUBTRACT     UINT32_C(2)
#define GFP_MULTIPLY     UINT32_C(3)
#define GFP_DIVIDE       UINT32_C(4)

/* gfp_unary opcodes. */
#define GFP_NEGATE       UINT32_C(1)
#define GFP_ABSOLUTE     UINT32_C(2)

/* gfp_compare opcodes. */
#define GFP_EQUAL         UINT32_C(1)
#define GFP_NOT_EQUAL     UINT32_C(2)
#define GFP_LESS          UINT32_C(3)
#define GFP_LESS_EQUAL    UINT32_C(4)
#define GFP_GREATER       UINT32_C(5)
#define GFP_GREATER_EQUAL UINT32_C(6)

#if defined(__cplusplus)
extern "C" {
#endif

/* Null output is checked first, then opcode. Refusals write nothing. */
uint32_t gfp_binary(uint32_t opcode, float a, float b, float *out) GFP_NOEXCEPT;
uint32_t gfp_unary(uint32_t opcode, float a, float *out) GFP_NOEXCEPT;
uint32_t gfp_compare(uint32_t opcode, float a, float b, uint32_t *out) GFP_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#endif /* GAMEPLAY_GRAPH_FLOAT_H */
