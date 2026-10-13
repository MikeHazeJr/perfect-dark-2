// gameplay_graph_float.cpp - C++11 implementation of gameplay_graph_float.h.
//
// Source handoff:
// - Files: port/include/gameplay_graph_float.h, this file,
//   tests/test_gameplay_graph_float.cpp. Nothing has been compiled or run.
// - Assumed qualification environment: the selected toolchain's ordinary
//   masked-exception, round-to-nearest floating-point environment. This
//   component neither establishes nor changes that environment.
// - Not promised and untested: actual compiler/ABI/float layout, extended
//   evaluation precision effects (FLT_EVAL_METHOD != 0), subnormal handling
//   (FTZ/DAZ), arithmetic NaN payloads/signs, exception flags, x87 or
//   cross-toolchain NaN/bit behavior, and C99 compilation of the header.
// - gfp_unary edits the representation of the float as received by value;
//   it cannot observe the caller's object before argument passing.
// - Zero application/gameplay behaviors are implemented here.

#include "gameplay_graph_float.h"

#include <cstdint>
#include <cstring>
#include <limits>

static_assert(sizeof(float) == 4, "float must be 4 bytes");
static_assert(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");
static_assert(std::numeric_limits<float>::is_iec559, "float must be IEC 60559");
static_assert(std::numeric_limits<float>::radix == 2, "float radix must be 2");
static_assert(std::numeric_limits<float>::digits == 24, "float digits must be 24");
static_assert(std::numeric_limits<float>::max_exponent == 128,
              "float max_exponent must be 128");

extern "C" {

uint32_t gfp_binary(uint32_t opcode, float a, float b, float *out) GFP_NOEXCEPT
{
    if (out == nullptr) {
        return GFP_NULL_OUTPUT;
    }
    switch (opcode) {
    case GFP_ADD:
        *out = a + b;
        return GFP_OK;
    case GFP_SUBTRACT:
        *out = a - b;
        return GFP_OK;
    case GFP_MULTIPLY:
        *out = a * b;
        return GFP_OK;
    case GFP_DIVIDE:
        *out = a / b;
        return GFP_OK;
    default:
        return GFP_BAD_OPCODE;
    }
}

uint32_t gfp_unary(uint32_t opcode, float a, float *out) GFP_NOEXCEPT
{
    uint32_t bits;
    if (out == nullptr) {
        return GFP_NULL_OUTPUT;
    }
    std::memcpy(&bits, &a, sizeof bits);
    switch (opcode) {
    case GFP_NEGATE:
        bits ^= UINT32_C(0x80000000);
        break;
    case GFP_ABSOLUTE:
        bits &= UINT32_C(0x7FFFFFFF);
        break;
    default:
        return GFP_BAD_OPCODE;
    }
    // Edited bits go directly into the output object; no float value copy.
    std::memcpy(out, &bits, sizeof bits);
    return GFP_OK;
}

uint32_t gfp_compare(uint32_t opcode, float a, float b, uint32_t *out) GFP_NOEXCEPT
{
    if (out == nullptr) {
        return GFP_NULL_OUTPUT;
    }
    switch (opcode) {
    case GFP_EQUAL:
        *out = (a == b) ? UINT32_C(1) : UINT32_C(0);
        return GFP_OK;
    case GFP_NOT_EQUAL:
        *out = (a != b) ? UINT32_C(1) : UINT32_C(0);
        return GFP_OK;
    case GFP_LESS:
        *out = (a < b) ? UINT32_C(1) : UINT32_C(0);
        return GFP_OK;
    case GFP_LESS_EQUAL:
        *out = (a <= b) ? UINT32_C(1) : UINT32_C(0);
        return GFP_OK;
    case GFP_GREATER:
        *out = (a > b) ? UINT32_C(1) : UINT32_C(0);
        return GFP_OK;
    case GFP_GREATER_EQUAL:
        *out = (a >= b) ? UINT32_C(1) : UINT32_C(0);
        return GFP_OK;
    default:
        return GFP_BAD_OPCODE;
    }
}

} // extern "C"
