#include "gameplay_graph_integer.h"

#include <climits>

namespace {

/* Internal arithmetic type: at least 64 bits and never promoted to int. */
typedef unsigned long long ggi_wide;

static_assert(sizeof(ggi_wide) * CHAR_BIT >= 64u,
              "unsigned long long must hold at least 64 bits");

bool width_supported(uint32_t width) noexcept
{
    return width == 8u || width == 16u || width == 32u || width == 64u;
}

/* width must be supported. Width 64 avoids a shift by 64. */
ggi_wide width_mask(uint32_t width) noexcept
{
    if (width >= 64u) {
        return 0xFFFFFFFFFFFFFFFFull;
    }
    return (static_cast<ggi_wide>(1u) << width) - 1u;
}

/* width must be supported. */
ggi_wide sign_bit(uint32_t width) noexcept
{
    return static_cast<ggi_wide>(1u) << (width - 1u);
}

ggi_status check_value(const ggi_int &v) noexcept
{
    if (!width_supported(v.width)) {
        return GGI_ERR_BAD_WIDTH;
    }
    if (v.is_signed > 1u) {
        return GGI_ERR_BAD_SIGNEDNESS;
    }
    if ((static_cast<ggi_wide>(v.bits) & ~width_mask(v.width)) != 0u) {
        return GGI_ERR_UNNORMALIZED;
    }
    return GGI_OK;
}

ggi_int make_value(ggi_wide bits, uint32_t width, uint32_t is_signed) noexcept
{
    ggi_int r;
    r.bits = static_cast<uint64_t>(bits & width_mask(width));
    r.width = width;
    r.is_signed = is_signed;
    return r;
}

bool is_compare_op(uint32_t op) noexcept
{
    return op == GGI_OP_EQ || op == GGI_OP_LT_S || op == GGI_OP_LT_U;
}

bool is_unsigned_arith_op(uint32_t op) noexcept
{
    return op == GGI_OP_ADD_U || op == GGI_OP_SUB_U || op == GGI_OP_MUL_U;
}

bool is_binary_op(uint32_t op) noexcept
{
    return op == GGI_OP_AND || op == GGI_OP_OR || op == GGI_OP_XOR ||
           is_unsigned_arith_op(op);
}

ggi_status check_pair(const ggi_int &a, const ggi_int &b) noexcept
{
    ggi_status status = check_value(a);
    if (status != GGI_OK) {
        return status;
    }
    status = check_value(b);
    if (status != GGI_OK) {
        return status;
    }
    if (a.width != b.width || a.is_signed != b.is_signed) {
        return GGI_ERR_TYPE_MISMATCH;
    }
    return GGI_OK;
}

} // namespace

ggi_status ggi_validate(const ggi_int *v) GGI_NOEXCEPT
{
    if (v == nullptr) {
        return GGI_ERR_NULL;
    }
    return check_value(*v);
}

ggi_status ggi_compare(uint32_t op, const ggi_int *a, const ggi_int *b,
                       uint8_t *out01) GGI_NOEXCEPT
{
    if (a == nullptr || b == nullptr || out01 == nullptr) {
        return GGI_ERR_NULL;
    }
    if (!is_compare_op(op)) {
        return GGI_ERR_UNSUPPORTED_OP;
    }
    const ggi_status status = check_pair(*a, *b);
    if (status != GGI_OK) {
        return status;
    }
    if (op == GGI_OP_LT_S && a->is_signed != 1u) {
        return GGI_ERR_DOMAIN;
    }
    if (op == GGI_OP_LT_U && a->is_signed != 0u) {
        return GGI_ERR_DOMAIN;
    }

    const ggi_wide x = a->bits;
    const ggi_wide y = b->bits;
    bool result;
    if (op == GGI_OP_EQ) {
        result = x == y;
    } else if (op == GGI_OP_LT_U) {
        result = x < y;
    } else {
        /* Flipping the sign bit maps two's-complement order onto unsigned
           order without any signed cast. */
        const ggi_wide flip = sign_bit(a->width);
        result = (x ^ flip) < (y ^ flip);
    }
    *out01 = static_cast<uint8_t>(result ? 1u : 0u);
    return GGI_OK;
}

ggi_status ggi_binary(uint32_t op, const ggi_int *a, const ggi_int *b,
                      ggi_int *out) GGI_NOEXCEPT
{
    if (a == nullptr || b == nullptr || out == nullptr) {
        return GGI_ERR_NULL;
    }
    if (!is_binary_op(op)) {
        return GGI_ERR_UNSUPPORTED_OP;
    }
    const ggi_status status = check_pair(*a, *b);
    if (status != GGI_OK) {
        return status;
    }
    if (is_unsigned_arith_op(op) && a->is_signed != 0u) {
        return GGI_ERR_DOMAIN;
    }

    const ggi_wide x = a->bits;
    const ggi_wide y = b->bits;
    ggi_wide result;
    switch (op) {
    case GGI_OP_AND:
        result = x & y;
        break;
    case GGI_OP_OR:
        result = x | y;
        break;
    case GGI_OP_XOR:
        result = x ^ y;
        break;
    case GGI_OP_ADD_U:
        result = x + y;
        break;
    case GGI_OP_SUB_U:
        result = x - y;
        break;
    case GGI_OP_MUL_U:
        result = x * y;
        break;
    default:
        return GGI_ERR_UNSUPPORTED_OP;
    }
    *out = make_value(result, a->width, a->is_signed);
    return GGI_OK;
}

ggi_status ggi_unary(uint32_t op, const ggi_int *a, ggi_int *out) GGI_NOEXCEPT
{
    if (a == nullptr || out == nullptr) {
        return GGI_ERR_NULL;
    }
    if (op != GGI_OP_NOT) {
        return GGI_ERR_UNSUPPORTED_OP;
    }
    const ggi_status status = check_value(*a);
    if (status != GGI_OK) {
        return status;
    }
    const ggi_wide x = a->bits;
    *out = make_value(~x, a->width, a->is_signed);
    return GGI_OK;
}

ggi_status ggi_convert(const ggi_int *src, uint32_t dst_width,
                       uint32_t dst_signed, ggi_int *out) GGI_NOEXCEPT
{
    if (src == nullptr || out == nullptr) {
        return GGI_ERR_NULL;
    }
    const ggi_status status = check_value(*src);
    if (status != GGI_OK) {
        return status;
    }
    if (!width_supported(dst_width)) {
        return GGI_ERR_BAD_WIDTH;
    }
    if (dst_signed > 1u) {
        return GGI_ERR_BAD_SIGNEDNESS;
    }

    ggi_wide value = src->bits;
    if (src->is_signed == 1u && (value & sign_bit(src->width)) != 0u) {
        value |= ~width_mask(src->width);
    }
    *out = make_value(value, dst_width, dst_signed);
    return GGI_OK;
}
