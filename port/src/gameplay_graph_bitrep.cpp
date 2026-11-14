// gameplay_graph_bitrep.cpp - b32 four-byte representation boundary (C++11).
// Authored source only: not compiled, not run, not ABI/layout/FP qualified.
// The static_asserts check the explicit target assumptions documented in the
// header; they do not prove NaN layout, ABI or floating-point environment.

#include "gameplay_graph_bitrep.h"

#include <cfloat>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>

#define B32_OBJECT_BYTES 4u

static_assert(CHAR_BIT == 8, "b32: CHAR_BIT must be 8");
static_assert(sizeof(int32_t) == B32_OBJECT_BYTES, "b32: sizeof(int32_t) must be 4");
static_assert(sizeof(uint32_t) == B32_OBJECT_BYTES, "b32: sizeof(uint32_t) must be 4");
static_assert(sizeof(float) == B32_OBJECT_BYTES, "b32: sizeof(float) must be 4");
static_assert(sizeof(b32_status) == B32_OBJECT_BYTES, "b32: status must be uint32_t");

static_assert(INT32_MIN == (-2147483647 - 1), "b32: INT32_MIN");
static_assert(INT32_MAX == 2147483647, "b32: INT32_MAX");
static_assert(UINT32_MAX == 0xffffffffu, "b32: UINT32_MAX");
static_assert(std::numeric_limits<int32_t>::min() == (-2147483647 - 1), "b32: int32_t min");
static_assert(std::numeric_limits<int32_t>::max() == 2147483647, "b32: int32_t max");
static_assert(std::numeric_limits<uint32_t>::max() == 0xffffffffu, "b32: uint32_t max");
static_assert(std::numeric_limits<uint32_t>::digits == 32, "b32: uint32_t value bits");

static_assert(FLT_RADIX == 2, "b32: FLT_RADIX");
static_assert(FLT_MANT_DIG == 24, "b32: FLT_MANT_DIG");
static_assert(FLT_MIN_EXP == -125, "b32: FLT_MIN_EXP");
static_assert(FLT_MAX_EXP == 128, "b32: FLT_MAX_EXP");

static_assert(B32_OK == 0u, "b32: B32_OK");
static_assert(B32_NULL == 1u, "b32: B32_NULL");

extern "C" {

b32_status b32_i32_to_u32(const int32_t *input, uint32_t *output) noexcept
{
    if (input == nullptr || output == nullptr) {
        return B32_NULL;
    }
    std::memcpy(output, input, B32_OBJECT_BYTES);
    return B32_OK;
}

b32_status b32_u32_to_i32(const uint32_t *input, int32_t *output) noexcept
{
    if (input == nullptr || output == nullptr) {
        return B32_NULL;
    }
    std::memcpy(output, input, B32_OBJECT_BYTES);
    return B32_OK;
}

b32_status b32_f32_to_u32(const float *input, uint32_t *output) noexcept
{
    if (input == nullptr || output == nullptr) {
        return B32_NULL;
    }
    std::memcpy(output, input, B32_OBJECT_BYTES);
    return B32_OK;
}

b32_status b32_u32_to_f32(const uint32_t *input, float *output) noexcept
{
    if (input == nullptr || output == nullptr) {
        return B32_NULL;
    }
    std::memcpy(output, input, B32_OBJECT_BYTES);
    return B32_OK;
}

} // extern "C"
