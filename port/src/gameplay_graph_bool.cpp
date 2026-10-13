// gameplay_graph_bool.cpp - C++11 implementation of gameplay_graph_bool.h.
//
// Assumption: the include path resolves "gameplay_graph_bool.h" to
// port/include/gameplay_graph_bool.h.
// Handoff gaps: compilation, execution, ABI and caller integration are
// unverified. The caller must pass a nonnull out that points to one live,
// writable uint32_t; only the null case is checked.

#include "gameplay_graph_bool.h"

#include <climits>

static_assert(sizeof(uint32_t) * CHAR_BIT == 32, "uint32_t must be exactly 32 bits");

extern "C" gbp_status gbp_logical_binary(uint32_t opcode, uint32_t a, uint32_t b, uint32_t *out) noexcept
{
    if (out == nullptr) {
        return GBP_NULL_OUTPUT;
    }

    const bool ta = (a != 0u);
    const bool tb = (b != 0u);
    bool result = false;

    switch (opcode) {
    case GBP_AND:
        result = ta && tb;
        break;
    case GBP_OR:
        result = ta || tb;
        break;
    case GBP_XOR:
        result = (ta != tb);
        break;
    default:
        return GBP_BAD_OPCODE;
    }

    *out = result ? 1u : 0u;
    return GBP_OK;
}

extern "C" gbp_status gbp_logical_not(uint32_t a, uint32_t *out) noexcept
{
    if (out == nullptr) {
        return GBP_NULL_OUTPUT;
    }

    *out = (a == 0u) ? 1u : 0u;
    return GBP_OK;
}

extern "C" gbp_status gbp_select_u32(uint32_t condition, uint32_t when_true, uint32_t when_false, uint32_t *out) noexcept
{
    if (out == nullptr) {
        return GBP_NULL_OUTPUT;
    }

    *out = (condition != 0u) ? when_true : when_false;
    return GBP_OK;
}
