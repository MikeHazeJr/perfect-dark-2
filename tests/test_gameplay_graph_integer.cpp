#include "gameplay_graph_integer.h"

#include <cstring>
#include <stdint.h>

extern "C" uint32_t ggi_run_tests(void);

namespace {

struct Ctx {
    uint32_t failures;
};

void check(Ctx *ctx, bool ok)
{
    if (!ok) {
        ctx->failures += 1u;
    }
}

ggi_int mk(uint64_t bits, uint32_t width, uint32_t is_signed)
{
    ggi_int v;
    std::memset(&v, 0, sizeof v);
    v.bits = bits;
    v.width = width;
    v.is_signed = is_signed;
    return v;
}

struct Snap {
    unsigned char bytes[sizeof(ggi_int)];
};

Snap snap_of(const ggi_int *v)
{
    Snap s;
    std::memset(s.bytes, 0, sizeof s.bytes);
    if (v != nullptr) {
        std::memcpy(s.bytes, v, sizeof s.bytes);
    }
    return s;
}

bool unchanged(const ggi_int *v, const Snap &s)
{
    return v == nullptr || std::memcmp(v, s.bytes, sizeof s.bytes) == 0;
}

void fill_canary(ggi_int *out)
{
    std::memset(out, 0xA5, sizeof *out);
}

bool canary_intact(const ggi_int *out)
{
    ggi_int ref;
    std::memset(&ref, 0xA5, sizeof ref);
    return std::memcmp(out, &ref, sizeof ref) == 0;
}

uint8_t flag(bool b)
{
    return static_cast<uint8_t>(b ? 1u : 0u);
}

/* ---- success-path helpers ---- */

void expect_compare(Ctx *ctx, uint32_t op, const ggi_int &a, const ggi_int &b,
                    uint8_t expect)
{
    const Snap sa = snap_of(&a);
    const Snap sb = snap_of(&b);
    uint8_t out = 0xA5u;
    const ggi_status st = ggi_compare(op, &a, &b, &out);
    check(ctx, st == GGI_OK);
    check(ctx, out == 0u || out == 1u);
    check(ctx, out == expect);
    check(ctx, unchanged(&a, sa));
    check(ctx, unchanged(&b, sb));
}

void expect_binary(Ctx *ctx, uint32_t op, const ggi_int &a, const ggi_int &b,
                   uint64_t expect)
{
    const Snap sa = snap_of(&a);
    const Snap sb = snap_of(&b);
    ggi_int out;
    fill_canary(&out);
    const ggi_status st = ggi_binary(op, &a, &b, &out);
    check(ctx, st == GGI_OK);
    check(ctx, out.bits == expect);
    check(ctx, out.width == a.width);
    check(ctx, out.is_signed == a.is_signed);
    check(ctx, unchanged(&a, sa));
    check(ctx, unchanged(&b, sb));
}

void expect_unary(Ctx *ctx, uint32_t op, const ggi_int &a, uint64_t expect)
{
    const Snap sa = snap_of(&a);
    ggi_int out;
    fill_canary(&out);
    const ggi_status st = ggi_unary(op, &a, &out);
    check(ctx, st == GGI_OK);
    check(ctx, out.bits == expect);
    check(ctx, out.width == a.width);
    check(ctx, out.is_signed == a.is_signed);
    check(ctx, unchanged(&a, sa));
}

void expect_convert(Ctx *ctx, const ggi_int &src, uint32_t dst_width,
                    uint32_t dst_signed, uint64_t expect)
{
    const Snap ss = snap_of(&src);
    ggi_int out;
    fill_canary(&out);
    const ggi_status st = ggi_convert(&src, dst_width, dst_signed, &out);
    check(ctx, st == GGI_OK);
    check(ctx, out.bits == expect);
    check(ctx, out.width == dst_width);
    check(ctx, out.is_signed == dst_signed);
    check(ctx, unchanged(&src, ss));
}

/* ---- refusal helpers ---- */

void refuse_compare(Ctx *ctx, uint32_t op, const ggi_int *a, const ggi_int *b,
                    bool with_out, ggi_status expect)
{
    const Snap sa = snap_of(a);
    const Snap sb = snap_of(b);
    uint8_t out = 0xA5u;
    const ggi_status st = ggi_compare(op, a, b, with_out ? &out : nullptr);
    check(ctx, st == expect);
    check(ctx, out == 0xA5u);
    check(ctx, unchanged(a, sa));
    check(ctx, unchanged(b, sb));
}

void refuse_binary(Ctx *ctx, uint32_t op, const ggi_int *a, const ggi_int *b,
                   bool with_out, ggi_status expect)
{
    const Snap sa = snap_of(a);
    const Snap sb = snap_of(b);
    ggi_int out;
    fill_canary(&out);
    const ggi_status st = ggi_binary(op, a, b, with_out ? &out : nullptr);
    check(ctx, st == expect);
    check(ctx, canary_intact(&out));
    check(ctx, unchanged(a, sa));
    check(ctx, unchanged(b, sb));
}

void refuse_unary(Ctx *ctx, uint32_t op, const ggi_int *a, bool with_out,
                  ggi_status expect)
{
    const Snap sa = snap_of(a);
    ggi_int out;
    fill_canary(&out);
    const ggi_status st = ggi_unary(op, a, with_out ? &out : nullptr);
    check(ctx, st == expect);
    check(ctx, canary_intact(&out));
    check(ctx, unchanged(a, sa));
}

void refuse_convert(Ctx *ctx, const ggi_int *src, uint32_t dst_width,
                    uint32_t dst_signed, bool with_out, ggi_status expect)
{
    const Snap ss = snap_of(src);
    ggi_int out;
    fill_canary(&out);
    const ggi_status st =
        ggi_convert(src, dst_width, dst_signed, with_out ? &out : nullptr);
    check(ctx, st == expect);
    check(ctx, canary_intact(&out));
    check(ctx, unchanged(src, ss));
}

/* Bitwise oracle built from division and remainder, one bit at a time. */
unsigned long long bit_oracle(unsigned long long a, unsigned long long b,
                              uint32_t op)
{
    unsigned long long result = 0u;
    unsigned long long place = 1u;
    for (uint32_t k = 0u; k < 8u; ++k) {
        const unsigned long long x = (a / place) % 2u;
        const unsigned long long y = (b / place) % 2u;
        unsigned long long r;
        if (op == GGI_OP_AND) {
            r = (x == 1u && y == 1u) ? 1u : 0u;
        } else if (op == GGI_OP_OR) {
            r = (x == 1u || y == 1u) ? 1u : 0u;
        } else {
            r = (x != y) ? 1u : 0u;
        }
        result += r * place;
        place *= 2u;
    }
    return result;
}

/* ---- tests ---- */

void test_validate(Ctx *ctx)
{
    struct Bound {
        uint32_t width;
        uint64_t max;
        uint64_t sign;
        uint64_t pos_max;
    };
    const Bound bounds[4] = {
        {8u, 0xFFull, 0x80ull, 0x7Full},
        {16u, 0xFFFFull, 0x8000ull, 0x7FFFull},
        {32u, 0xFFFFFFFFull, 0x80000000ull, 0x7FFFFFFFull},
        {64u, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull,
         0x7FFFFFFFFFFFFFFFull},
    };
    for (uint32_t i = 0u; i < 4u; ++i) {
        for (uint32_t s = 0u; s < 2u; ++s) {
            const uint64_t vals[4] = {0u, bounds[i].max, bounds[i].sign,
                                      bounds[i].pos_max};
            for (uint32_t j = 0u; j < 4u; ++j) {
                const ggi_int v = mk(vals[j], bounds[i].width, s);
                const Snap sv = snap_of(&v);
                check(ctx, ggi_validate(&v) == GGI_OK);
                check(ctx, unchanged(&v, sv));
            }
        }
    }

    check(ctx, ggi_validate(nullptr) == GGI_ERR_NULL);

    const uint32_t bad_widths[10] = {0u, 1u, 7u, 9u, 12u, 24u,
                                     63u, 65u, 128u, 0xFFFFFFFFu};
    for (uint32_t i = 0u; i < 10u; ++i) {
        const ggi_int v = mk(0u, bad_widths[i], 0u);
        check(ctx, ggi_validate(&v) == GGI_ERR_BAD_WIDTH);
    }

    const uint32_t bad_signs[3] = {2u, 3u, 0xFFFFFFFFu};
    for (uint32_t i = 0u; i < 3u; ++i) {
        const ggi_int v = mk(0u, 8u, bad_signs[i]);
        check(ctx, ggi_validate(&v) == GGI_ERR_BAD_SIGNEDNESS);
    }

    struct Unnorm {
        uint64_t bits;
        uint32_t width;
    };
    const Unnorm unnorm[6] = {
        {0x100ull, 8u},
        {0x8000000000000000ull, 8u},
        {0x10000ull, 16u},
        {0xFFFFFFFFFFFF0000ull, 16u},
        {0x100000000ull, 32u},
        {0xFFFFFFFFFFFFFFFFull, 32u},
    };
    for (uint32_t i = 0u; i < 6u; ++i) {
        for (uint32_t s = 0u; s < 2u; ++s) {
            const ggi_int v = mk(unnorm[i].bits, unnorm[i].width, s);
            check(ctx, ggi_validate(&v) == GGI_ERR_UNNORMALIZED);
        }
    }

    /* Precedence: width before signedness before normalization. */
    const ggi_int all_bad = mk(0x100u, 7u, 2u);
    check(ctx, ggi_validate(&all_bad) == GGI_ERR_BAD_WIDTH);
    const ggi_int sign_and_norm = mk(0x100u, 8u, 2u);
    check(ctx, ggi_validate(&sign_and_norm) == GGI_ERR_BAD_SIGNEDNESS);
}

void test_compare_literals(Ctx *ctx)
{
    struct CmpCase {
        uint32_t op;
        uint32_t width;
        uint32_t is_signed;
        uint64_t a;
        uint64_t b;
        uint8_t expect;
    };
    const CmpCase cases[38] = {
        {GGI_OP_LT_S, 8u, 1u, 0x80ull, 0x7Full, 1u},
        {GGI_OP_LT_S, 8u, 1u, 0x7Full, 0x80ull, 0u},
        {GGI_OP_LT_S, 8u, 1u, 0xFFull, 0x00ull, 1u},
        {GGI_OP_LT_S, 8u, 1u, 0x00ull, 0xFFull, 0u},
        {GGI_OP_LT_S, 8u, 1u, 0x80ull, 0xFFull, 1u},
        {GGI_OP_LT_S, 8u, 1u, 0x80ull, 0x80ull, 0u},
        {GGI_OP_LT_U, 8u, 0u, 0x80ull, 0x7Full, 0u},
        {GGI_OP_LT_U, 8u, 0u, 0x7Full, 0x80ull, 1u},
        {GGI_OP_LT_U, 8u, 0u, 0xFFull, 0x00ull, 0u},
        {GGI_OP_LT_U, 8u, 0u, 0x00ull, 0xFFull, 1u},
        {GGI_OP_LT_S, 16u, 1u, 0x8000ull, 0x7FFFull, 1u},
        {GGI_OP_LT_S, 16u, 1u, 0xFFFFull, 0x0001ull, 1u},
        {GGI_OP_LT_S, 16u, 1u, 0x0001ull, 0xFFFFull, 0u},
        {GGI_OP_LT_U, 16u, 0u, 0x8000ull, 0x7FFFull, 0u},
        {GGI_OP_LT_U, 16u, 0u, 0x0001ull, 0xFFFFull, 1u},
        {GGI_OP_LT_S, 32u, 1u, 0x80000000ull, 0x7FFFFFFFull, 1u},
        {GGI_OP_LT_S, 32u, 1u, 0xFFFFFFFEull, 0xFFFFFFFFull, 1u},
        {GGI_OP_LT_S, 32u, 1u, 0x00000000ull, 0xFFFFFFFFull, 0u},
        {GGI_OP_LT_U, 32u, 0u, 0x80000000ull, 0x7FFFFFFFull, 0u},
        {GGI_OP_LT_U, 32u, 0u, 0x00000000ull, 0xFFFFFFFFull, 1u},
        {GGI_OP_LT_S, 64u, 1u, 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull, 1u},
        {GGI_OP_LT_S, 64u, 1u, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000000ull, 1u},
        {GGI_OP_LT_U, 64u, 0u, 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull, 0u},
        {GGI_OP_LT_U, 64u, 0u, 0x0000000000000000ull, 0xFFFFFFFFFFFFFFFFull, 1u},
        {GGI_OP_EQ, 8u, 0u, 0x80ull, 0x80ull, 1u},
        {GGI_OP_EQ, 8u, 1u, 0x80ull, 0x80ull, 1u},
        {GGI_OP_EQ, 8u, 0u, 0x80ull, 0x7Full, 0u},
        {GGI_OP_EQ, 8u, 1u, 0xFFull, 0x00ull, 0u},
        {GGI_OP_EQ, 16u, 0u, 0xFFFFull, 0xFFFFull, 1u},
        {GGI_OP_EQ, 16u, 1u, 0x8000ull, 0x0000ull, 0u},
        {GGI_OP_EQ, 32u, 0u, 0x80000000ull, 0x80000000ull, 1u},
        {GGI_OP_EQ, 32u, 1u, 0x7FFFFFFFull, 0x80000000ull, 0u},
        {GGI_OP_EQ, 64u, 0u, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 1u},
        {GGI_OP_EQ, 64u, 1u, 0x8000000000000000ull, 0x8000000000000000ull, 1u},
        {GGI_OP_EQ, 64u, 0u, 0x0000000000000000ull, 0x8000000000000000ull, 0u},
        {GGI_OP_EQ, 64u, 1u, 0x7FFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0u},
        {GGI_OP_LT_S, 64u, 1u, 0x0000000000000000ull, 0x0000000000000000ull, 0u},
        {GGI_OP_LT_U, 64u, 0u, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0u},
    };
    for (uint32_t i = 0u; i < 38u; ++i) {
        const ggi_int a = mk(cases[i].a, cases[i].width, cases[i].is_signed);
        const ggi_int b = mk(cases[i].b, cases[i].width, cases[i].is_signed);
        expect_compare(ctx, cases[i].op, a, b, cases[i].expect);
    }
}

void test_binary_unary_literals(Ctx *ctx)
{
    struct BinCase {
        uint32_t op;
        uint32_t width;
        uint32_t is_signed;
        uint64_t a;
        uint64_t b;
        uint64_t expect;
    };
    const BinCase cases[34] = {
        {GGI_OP_AND, 8u, 0u, 0xF0ull, 0x3Cull, 0x30ull},
        {GGI_OP_AND, 8u, 1u, 0xF0ull, 0x3Cull, 0x30ull},
        {GGI_OP_OR, 16u, 1u, 0x8000ull, 0x0001ull, 0x8001ull},
        {GGI_OP_OR, 16u, 0u, 0x00F0ull, 0x0F00ull, 0x0FF0ull},
        {GGI_OP_XOR, 32u, 0u, 0xFFFFFFFFull, 0x0F0F0F0Full, 0xF0F0F0F0ull},
        {GGI_OP_XOR, 32u, 1u, 0x80000000ull, 0x80000001ull, 0x00000001ull},
        {GGI_OP_AND, 64u, 0u, 0xFFFF0000FFFF0000ull, 0x0F0F0F0F0F0F0F0Full,
         0x0F0F00000F0F0000ull},
        {GGI_OP_OR, 64u, 1u, 0x8000000000000000ull, 0x0000000000000001ull,
         0x8000000000000001ull},
        {GGI_OP_XOR, 64u, 1u, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull,
         0x7FFFFFFFFFFFFFFFull},
        {GGI_OP_ADD_U, 8u, 0u, 0xFFull, 0x01ull, 0x00ull},
        {GGI_OP_ADD_U, 8u, 0u, 0x80ull, 0x80ull, 0x00ull},
        {GGI_OP_ADD_U, 16u, 0u, 0xFFFFull, 0xFFFFull, 0xFFFEull},
        {GGI_OP_ADD_U, 32u, 0u, 0xFFFFFFFFull, 0x00000002ull, 0x00000001ull},
        {GGI_OP_ADD_U, 64u, 0u, 0xFFFFFFFFFFFFFFFFull, 0x1ull, 0x0ull},
        {GGI_OP_ADD_U, 64u, 0u, 0x8000000000000000ull, 0x8000000000000000ull,
         0x0ull},
        {GGI_OP_SUB_U, 8u, 0u, 0x00ull, 0x01ull, 0xFFull},
        {GGI_OP_SUB_U, 16u, 0u, 0x0000ull, 0x0001ull, 0xFFFFull},
        {GGI_OP_SUB_U, 32u, 0u, 0x00000001ull, 0x00000002ull, 0xFFFFFFFFull},
        {GGI_OP_SUB_U, 64u, 0u, 0x0ull, 0x1ull, 0xFFFFFFFFFFFFFFFFull},
        {GGI_OP_SUB_U, 64u, 0u, 0x8000000000000000ull, 0x1ull,
         0x7FFFFFFFFFFFFFFFull},
        {GGI_OP_MUL_U, 8u, 0u, 0xFFull, 0xFFull, 0x01ull},
        {GGI_OP_MUL_U, 8u, 0u, 0x10ull, 0x10ull, 0x00ull},
        {GGI_OP_MUL_U, 8u, 0u, 0x0Full, 0x11ull, 0xFFull},
        {GGI_OP_MUL_U, 16u, 0u, 0xFFFFull, 0xFFFFull, 0x0001ull},
        {GGI_OP_MUL_U, 16u, 0u, 0x0100ull, 0x0100ull, 0x0000ull},
        {GGI_OP_MUL_U, 32u, 0u, 0xFFFFFFFFull, 0xFFFFFFFFull, 0x00000001ull},
        {GGI_OP_MUL_U, 32u, 0u, 0x00010000ull, 0x00010000ull, 0x00000000ull},
        {GGI_OP_MUL_U, 32u, 0u, 0x0000FFFFull, 0x0000FFFFull, 0xFFFE0001ull},
        {GGI_OP_MUL_U, 64u, 0u, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
         0x1ull},
        {GGI_OP_MUL_U, 64u, 0u, 0x8000000000000000ull, 0x2ull, 0x0ull},
        {GGI_OP_MUL_U, 64u, 0u, 0x100000000ull, 0x100000000ull, 0x0ull},
        {GGI_OP_MUL_U, 64u, 0u, 0xFFFFFFFFull, 0xFFFFFFFFull,
         0xFFFFFFFE00000001ull},
        {GGI_OP_MUL_U, 64u, 0u, 0x123456789ull, 0x0ull, 0x0ull},
        {GGI_OP_MUL_U, 64u, 0u, 0x123456789ull, 0x1ull, 0x123456789ull},
    };
    for (uint32_t i = 0u; i < 34u; ++i) {
        const ggi_int a = mk(cases[i].a, cases[i].width, cases[i].is_signed);
        const ggi_int b = mk(cases[i].b, cases[i].width, cases[i].is_signed);
        expect_binary(ctx, cases[i].op, a, b, cases[i].expect);
    }

    struct NotCase {
        uint32_t width;
        uint32_t is_signed;
        uint64_t a;
        uint64_t expect;
    };
    const NotCase nots[10] = {
        {8u, 0u, 0x00ull, 0xFFull},
        {8u, 1u, 0x80ull, 0x7Full},
        {16u, 0u, 0x00FFull, 0xFF00ull},
        {16u, 1u, 0xFFFFull, 0x0000ull},
        {32u, 0u, 0x00000000ull, 0xFFFFFFFFull},
        {32u, 1u, 0x7FFFFFFFull, 0x80000000ull},
        {64u, 0u, 0x0000000000000000ull, 0xFFFFFFFFFFFFFFFFull},
        {64u, 1u, 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull},
        {64u, 0u, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000000ull},
        {64u, 1u, 0x0123456789ABCDEFull, 0xFEDCBA9876543210ull},
    };
    for (uint32_t i = 0u; i < 10u; ++i) {
        const ggi_int a = mk(nots[i].a, nots[i].width, nots[i].is_signed);
        expect_unary(ctx, GGI_OP_NOT, a, nots[i].expect);
    }
}

void test_width64_matrix(Ctx *ctx)
{
    const uint64_t cmp_vals[5] = {
        0x0000000000000000ull, 0x0000000000000001ull, 0x7FFFFFFFFFFFFFFFull,
        0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull,
    };
    const uint8_t lt_s[5][5] = {
        {0u, 1u, 1u, 0u, 0u},
        {0u, 0u, 1u, 0u, 0u},
        {0u, 0u, 0u, 0u, 0u},
        {1u, 1u, 1u, 0u, 1u},
        {1u, 1u, 1u, 0u, 0u},
    };
    const uint8_t lt_u[5][5] = {
        {0u, 1u, 1u, 1u, 1u},
        {0u, 0u, 1u, 1u, 1u},
        {0u, 0u, 0u, 1u, 1u},
        {0u, 0u, 0u, 0u, 1u},
        {0u, 0u, 0u, 0u, 0u},
    };
    for (uint32_t i = 0u; i < 5u; ++i) {
        for (uint32_t j = 0u; j < 5u; ++j) {
            const ggi_int sa = mk(cmp_vals[i], 64u, 1u);
            const ggi_int sb = mk(cmp_vals[j], 64u, 1u);
            const ggi_int ua = mk(cmp_vals[i], 64u, 0u);
            const ggi_int ub = mk(cmp_vals[j], 64u, 0u);
            expect_compare(ctx, GGI_OP_LT_S, sa, sb, lt_s[i][j]);
            expect_compare(ctx, GGI_OP_LT_U, ua, ub, lt_u[i][j]);
            expect_compare(ctx, GGI_OP_EQ, sa, sb, flag(i == j));
            expect_compare(ctx, GGI_OP_EQ, ua, ub, flag(i == j));
        }
    }

    const uint64_t ar_vals[4] = {
        0x0000000000000000ull, 0x0000000000000001ull, 0x8000000000000000ull,
        0xFFFFFFFFFFFFFFFFull,
    };
    const uint64_t add[4][4] = {
        {0x0000000000000000ull, 0x0000000000000001ull, 0x8000000000000000ull,
         0xFFFFFFFFFFFFFFFFull},
        {0x0000000000000001ull, 0x0000000000000002ull, 0x8000000000000001ull,
         0x0000000000000000ull},
        {0x8000000000000000ull, 0x8000000000000001ull, 0x0000000000000000ull,
         0x7FFFFFFFFFFFFFFFull},
        {0xFFFFFFFFFFFFFFFFull, 0x0000000000000000ull, 0x7FFFFFFFFFFFFFFFull,
         0xFFFFFFFFFFFFFFFEull},
    };
    const uint64_t sub[4][4] = {
        {0x0000000000000000ull, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull,
         0x0000000000000001ull},
        {0x0000000000000001ull, 0x0000000000000000ull, 0x8000000000000001ull,
         0x0000000000000002ull},
        {0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull, 0x0000000000000000ull,
         0x8000000000000001ull},
        {0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull, 0x7FFFFFFFFFFFFFFFull,
         0x0000000000000000ull},
    };
    const uint64_t mul[4][4] = {
        {0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull,
         0x0000000000000000ull},
        {0x0000000000000000ull, 0x0000000000000001ull, 0x8000000000000000ull,
         0xFFFFFFFFFFFFFFFFull},
        {0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull,
         0x8000000000000000ull},
        {0x0000000000000000ull, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull,
         0x0000000000000001ull},
    };
    for (uint32_t i = 0u; i < 4u; ++i) {
        for (uint32_t j = 0u; j < 4u; ++j) {
            const ggi_int a = mk(ar_vals[i], 64u, 0u);
            const ggi_int b = mk(ar_vals[j], 64u, 0u);
            expect_binary(ctx, GGI_OP_ADD_U, a, b, add[i][j]);
            expect_binary(ctx, GGI_OP_SUB_U, a, b, sub[i][j]);
            expect_binary(ctx, GGI_OP_MUL_U, a, b, mul[i][j]);
        }
    }
}

void test_exhaustive_width8(Ctx *ctx)
{
    const uint32_t bit_ops[3] = {GGI_OP_AND, GGI_OP_OR, GGI_OP_XOR};
    for (uint32_t a = 0u; a < 256u; ++a) {
        for (uint32_t b = 0u; b < 256u; ++b) {
            const unsigned long long wa = a;
            const unsigned long long wb = b;
            const ggi_int ua = mk(a, 8u, 0u);
            const ggi_int ub = mk(b, 8u, 0u);
            const ggi_int sa = mk(a, 8u, 1u);
            const ggi_int sb = mk(b, 8u, 1u);

            expect_binary(ctx, GGI_OP_ADD_U, ua, ub,
                          static_cast<uint64_t>((wa + wb) % 256u));
            expect_binary(ctx, GGI_OP_SUB_U, ua, ub,
                          static_cast<uint64_t>((wa + 256u - wb) % 256u));
            expect_binary(ctx, GGI_OP_MUL_U, ua, ub,
                          static_cast<uint64_t>((wa * wb) % 256u));

            const long long da = wa < 128u ? static_cast<long long>(wa)
                                           : static_cast<long long>(wa) - 256;
            const long long db = wb < 128u ? static_cast<long long>(wb)
                                           : static_cast<long long>(wb) - 256;
            expect_compare(ctx, GGI_OP_LT_U, ua, ub, flag(wa < wb));
            expect_compare(ctx, GGI_OP_LT_S, sa, sb, flag(da < db));
            expect_compare(ctx, GGI_OP_EQ, ua, ub, flag(a == b));
            expect_compare(ctx, GGI_OP_EQ, sa, sb, flag(a == b));

            for (uint32_t k = 0u; k < 3u; ++k) {
                const uint64_t e =
                    static_cast<uint64_t>(bit_oracle(wa, wb, bit_ops[k]));
                expect_binary(ctx, bit_ops[k], ua, ub, e);
                expect_binary(ctx, bit_ops[k], sa, sb, e);
            }
        }
    }

    const unsigned long long dst_max[4] = {
        0xFFull, 0xFFFFull, 0xFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
    };
    const uint32_t dst_width[4] = {8u, 16u, 32u, 64u};
    for (uint32_t a = 0u; a < 256u; ++a) {
        const unsigned long long wa = a;
        const ggi_int ua = mk(a, 8u, 0u);
        const ggi_int sa = mk(a, 8u, 1u);
        expect_unary(ctx, GGI_OP_NOT, ua, static_cast<uint64_t>(255u - wa));
        expect_unary(ctx, GGI_OP_NOT, sa, static_cast<uint64_t>(255u - wa));

        for (uint32_t d = 0u; d < 4u; ++d) {
            const unsigned long long sign_ext =
                wa < 128u ? wa : (dst_max[d] - 255u) + wa;
            for (uint32_t ds = 0u; ds < 2u; ++ds) {
                expect_convert(ctx, ua, dst_width[d], ds,
                               static_cast<uint64_t>(wa));
                expect_convert(ctx, sa, dst_width[d], ds,
                               static_cast<uint64_t>(sign_ext));
            }
        }
    }

    /* Exhaustive 16-bit source narrowing and widening. */
    for (uint32_t v = 0u; v < 65536u; ++v) {
        const unsigned long long wv = v;
        const ggi_int u16 = mk(v, 16u, 0u);
        const ggi_int s16 = mk(v, 16u, 1u);
        expect_convert(ctx, u16, 8u, 0u, static_cast<uint64_t>(wv % 256u));
        expect_convert(ctx, s16, 8u, 1u, static_cast<uint64_t>(wv % 256u));
        expect_convert(ctx, u16, 32u, 1u, static_cast<uint64_t>(wv));
        const unsigned long long ext32 =
            wv < 32768u ? wv : (0xFFFFFFFFull - 0xFFFFull) + wv;
        expect_convert(ctx, s16, 32u, 0u, static_cast<uint64_t>(ext32));
    }
}

void test_convert_literals(Ctx *ctx)
{
    struct ConvCase {
        uint32_t width;
        uint32_t is_signed;
        uint64_t bits;
        uint64_t e8;
        uint64_t e16;
        uint64_t e32;
        uint64_t e64;
    };
    const ConvCase cases[37] = {
        {8u, 0u, 0x00ull, 0x00ull, 0x0000ull, 0x00000000ull, 0x0ull},
        {8u, 0u, 0xFFull, 0xFFull, 0x00FFull, 0x000000FFull, 0xFFull},
        {8u, 0u, 0x80ull, 0x80ull, 0x0080ull, 0x00000080ull, 0x80ull},
        {8u, 0u, 0x7Full, 0x7Full, 0x007Full, 0x0000007Full, 0x7Full},
        {8u, 1u, 0x00ull, 0x00ull, 0x0000ull, 0x00000000ull, 0x0ull},
        {8u, 1u, 0xFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull,
         0xFFFFFFFFFFFFFFFFull},
        {8u, 1u, 0x80ull, 0x80ull, 0xFF80ull, 0xFFFFFF80ull,
         0xFFFFFFFFFFFFFF80ull},
        {8u, 1u, 0x7Full, 0x7Full, 0x007Full, 0x0000007Full, 0x7Full},
        {16u, 0u, 0x0000ull, 0x00ull, 0x0000ull, 0x00000000ull, 0x0ull},
        {16u, 0u, 0xFFFFull, 0xFFull, 0xFFFFull, 0x0000FFFFull, 0xFFFFull},
        {16u, 0u, 0x8000ull, 0x00ull, 0x8000ull, 0x00008000ull, 0x8000ull},
        {16u, 0u, 0x7FFFull, 0xFFull, 0x7FFFull, 0x00007FFFull, 0x7FFFull},
        {16u, 1u, 0x0000ull, 0x00ull, 0x0000ull, 0x00000000ull, 0x0ull},
        {16u, 1u, 0xFFFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull,
         0xFFFFFFFFFFFFFFFFull},
        {16u, 1u, 0x8000ull, 0x00ull, 0x8000ull, 0xFFFF8000ull,
         0xFFFFFFFFFFFF8000ull},
        {16u, 1u, 0x7FFFull, 0xFFull, 0x7FFFull, 0x00007FFFull, 0x7FFFull},
        {32u, 0u, 0x00000000ull, 0x00ull, 0x0000ull, 0x00000000ull, 0x0ull},
        {32u, 0u, 0xFFFFFFFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull,
         0xFFFFFFFFull},
        {32u, 0u, 0x80000000ull, 0x00ull, 0x0000ull, 0x80000000ull,
         0x80000000ull},
        {32u, 0u, 0x7FFFFFFFull, 0xFFull, 0xFFFFull, 0x7FFFFFFFull,
         0x7FFFFFFFull},
        {32u, 1u, 0x00000000ull, 0x00ull, 0x0000ull, 0x00000000ull, 0x0ull},
        {32u, 1u, 0xFFFFFFFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull,
         0xFFFFFFFFFFFFFFFFull},
        {32u, 1u, 0x80000000ull, 0x00ull, 0x0000ull, 0x80000000ull,
         0xFFFFFFFF80000000ull},
        {32u, 1u, 0x7FFFFFFFull, 0xFFull, 0xFFFFull, 0x7FFFFFFFull,
         0x7FFFFFFFull},
        {64u, 0u, 0x0000000000000000ull, 0x00ull, 0x0000ull, 0x00000000ull,
         0x0000000000000000ull},
        {64u, 0u, 0xFFFFFFFFFFFFFFFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull,
         0xFFFFFFFFFFFFFFFFull},
        {64u, 0u, 0x8000000000000000ull, 0x00ull, 0x0000ull, 0x00000000ull,
         0x8000000000000000ull},
        {64u, 0u, 0x7FFFFFFFFFFFFFFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull,
         0x7FFFFFFFFFFFFFFFull},
        {64u, 1u, 0x0000000000000000ull, 0x00ull, 0x0000ull, 0x00000000ull,
         0x0000000000000000ull},
        {64u, 1u, 0xFFFFFFFFFFFFFFFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull,
         0xFFFFFFFFFFFFFFFFull},
        {64u, 1u, 0x8000000000000000ull, 0x00ull, 0x0000ull, 0x00000000ull,
         0x8000000000000000ull},
        {64u, 1u, 0x7FFFFFFFFFFFFFFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull,
         0x7FFFFFFFFFFFFFFFull},
        {64u, 1u, 0x000000FFFFFFFF80ull, 0x80ull, 0xFF80ull, 0xFFFFFF80ull,
         0x000000FFFFFFFF80ull},
        {16u, 0u, 0x1234ull, 0x34ull, 0x1234ull, 0x00001234ull, 0x1234ull},
        {16u, 1u, 0x1234ull, 0x34ull, 0x1234ull, 0x00001234ull, 0x1234ull},
        {16u, 0u, 0x89ABull, 0xABull, 0x89ABull, 0x000089ABull, 0x89ABull},
        {16u, 1u, 0x89ABull, 0xABull, 0x89ABull, 0xFFFF89ABull,
         0xFFFFFFFFFFFF89ABull},
    };
    for (uint32_t i = 0u; i < 37u; ++i) {
        const ggi_int src =
            mk(cases[i].bits, cases[i].width, cases[i].is_signed);
        for (uint32_t ds = 0u; ds < 2u; ++ds) {
            expect_convert(ctx, src, 8u, ds, cases[i].e8);
            expect_convert(ctx, src, 16u, ds, cases[i].e16);
            expect_convert(ctx, src, 32u, ds, cases[i].e32);
            expect_convert(ctx, src, 64u, ds, cases[i].e64);
        }
    }
}

void test_refusals(Ctx *ctx)
{
    const ggi_int u8a = mk(0x12u, 8u, 0u);
    const ggi_int u8b = mk(0x34u, 8u, 0u);
    const ggi_int s8a = mk(0x80u, 8u, 1u);
    const ggi_int u16 = mk(0x1234u, 16u, 0u);
    const ggi_int u64 = mk(0xFFFFFFFFFFFFFFFFull, 64u, 0u);
    const ggi_int s64 = mk(0x8000000000000000ull, 64u, 1u);
    const ggi_int badw = mk(0u, 12u, 0u);
    const ggi_int badw128 = mk(0u, 128u, 1u);
    const ggi_int bads = mk(0u, 8u, 2u);
    const ggi_int unnorm = mk(0x100u, 8u, 0u);
    const ggi_int unnorm32 = mk(0x100000000ull, 32u, 1u);

    const uint32_t unknown[5] = {0u, 11u, 12u, 0x7Fu, 0xFFFFFFFFu};
    const uint32_t excluded[11] = {
        GGI_OP_EXCLUDED_ADD_S, GGI_OP_EXCLUDED_SUB_S, GGI_OP_EXCLUDED_MUL_S,
        GGI_OP_EXCLUDED_DIV_U, GGI_OP_EXCLUDED_DIV_S, GGI_OP_EXCLUDED_REM_U,
        GGI_OP_EXCLUDED_REM_S, GGI_OP_EXCLUDED_SHL,   GGI_OP_EXCLUDED_SHR_U,
        GGI_OP_EXCLUDED_SHR_S, GGI_OP_EXCLUDED_FLOAT,
    };
    const uint32_t binary_ops[6] = {GGI_OP_AND,   GGI_OP_OR,    GGI_OP_XOR,
                                    GGI_OP_ADD_U, GGI_OP_SUB_U, GGI_OP_MUL_U};
    const uint32_t compare_ops[3] = {GGI_OP_EQ, GGI_OP_LT_S, GGI_OP_LT_U};
    const uint32_t arith_ops[3] = {GGI_OP_ADD_U, GGI_OP_SUB_U, GGI_OP_MUL_U};

    /* ---- ggi_compare ---- */
    refuse_compare(ctx, GGI_OP_EQ, nullptr, &u8b, true, GGI_ERR_NULL);
    refuse_compare(ctx, GGI_OP_EQ, &u8a, nullptr, true, GGI_ERR_NULL);
    refuse_compare(ctx, GGI_OP_EQ, &u8a, &u8b, false, GGI_ERR_NULL);
    refuse_compare(ctx, 0u, nullptr, &u8b, true, GGI_ERR_NULL);
    refuse_compare(ctx, GGI_OP_EQ, &badw, nullptr, true, GGI_ERR_NULL);
    for (uint32_t i = 0u; i < 5u; ++i) {
        refuse_compare(ctx, unknown[i], &u8a, &u8b, true,
                       GGI_ERR_UNSUPPORTED_OP);
    }
    for (uint32_t i = 0u; i < 11u; ++i) {
        refuse_compare(ctx, excluded[i], &u8a, &u8b, true,
                       GGI_ERR_UNSUPPORTED_OP);
    }
    for (uint32_t i = 0u; i < 6u; ++i) {
        refuse_compare(ctx, binary_ops[i], &u8a, &u8b, true,
                       GGI_ERR_UNSUPPORTED_OP);
    }
    refuse_compare(ctx, GGI_OP_NOT, &u8a, &u8b, true, GGI_ERR_UNSUPPORTED_OP);
    refuse_compare(ctx, 0xFFFFFFFFu, &badw, &bads, true,
                   GGI_ERR_UNSUPPORTED_OP);
    for (uint32_t i = 0u; i < 3u; ++i) {
        const uint32_t op = compare_ops[i];
        refuse_compare(ctx, op, &badw, &u8b, true, GGI_ERR_BAD_WIDTH);
        refuse_compare(ctx, op, &u8a, &badw, true, GGI_ERR_BAD_WIDTH);
        refuse_compare(ctx, op, &badw128, &badw128, true, GGI_ERR_BAD_WIDTH);
        refuse_compare(ctx, op, &bads, &u8b, true, GGI_ERR_BAD_SIGNEDNESS);
        refuse_compare(ctx, op, &u8a, &bads, true, GGI_ERR_BAD_SIGNEDNESS);
        refuse_compare(ctx, op, &unnorm, &u8b, true, GGI_ERR_UNNORMALIZED);
        refuse_compare(ctx, op, &u8a, &unnorm, true, GGI_ERR_UNNORMALIZED);
        refuse_compare(ctx, op, &unnorm32, &unnorm32, true,
                       GGI_ERR_UNNORMALIZED);
        refuse_compare(ctx, op, &badw, &bads, true, GGI_ERR_BAD_WIDTH);
        refuse_compare(ctx, op, &bads, &badw, true, GGI_ERR_BAD_SIGNEDNESS);
        refuse_compare(ctx, op, &unnorm, &u16, true, GGI_ERR_UNNORMALIZED);
        refuse_compare(ctx, op, &u16, &unnorm, true, GGI_ERR_UNNORMALIZED);
        refuse_compare(ctx, op, &u8a, &u16, true, GGI_ERR_TYPE_MISMATCH);
        refuse_compare(ctx, op, &u8a, &s8a, true, GGI_ERR_TYPE_MISMATCH);
        refuse_compare(ctx, op, &u64, &s64, true, GGI_ERR_TYPE_MISMATCH);
    }
    refuse_compare(ctx, GGI_OP_LT_S, &u8a, &u8b, true, GGI_ERR_DOMAIN);
    refuse_compare(ctx, GGI_OP_LT_S, &u64, &u64, true, GGI_ERR_DOMAIN);
    refuse_compare(ctx, GGI_OP_LT_U, &s8a, &s8a, true, GGI_ERR_DOMAIN);
    refuse_compare(ctx, GGI_OP_LT_U, &s64, &s64, true, GGI_ERR_DOMAIN);

    /* ---- ggi_binary ---- */
    refuse_binary(ctx, GGI_OP_AND, nullptr, &u8b, true, GGI_ERR_NULL);
    refuse_binary(ctx, GGI_OP_AND, &u8a, nullptr, true, GGI_ERR_NULL);
    refuse_binary(ctx, GGI_OP_AND, &u8a, &u8b, false, GGI_ERR_NULL);
    refuse_binary(ctx, 0u, nullptr, nullptr, true, GGI_ERR_NULL);
    for (uint32_t i = 0u; i < 5u; ++i) {
        refuse_binary(ctx, unknown[i], &u8a, &u8b, true,
                      GGI_ERR_UNSUPPORTED_OP);
    }
    for (uint32_t i = 0u; i < 11u; ++i) {
        refuse_binary(ctx, excluded[i], &u8a, &u8b, true,
                      GGI_ERR_UNSUPPORTED_OP);
        refuse_binary(ctx, excluded[i], &s8a, &s8a, true,
                      GGI_ERR_UNSUPPORTED_OP);
    }
    for (uint32_t i = 0u; i < 3u; ++i) {
        refuse_binary(ctx, compare_ops[i], &u8a, &u8b, true,
                      GGI_ERR_UNSUPPORTED_OP);
    }
    refuse_binary(ctx, GGI_OP_NOT, &u8a, &u8b, true, GGI_ERR_UNSUPPORTED_OP);
    refuse_binary(ctx, 11u, &unnorm, &badw, true, GGI_ERR_UNSUPPORTED_OP);
    for (uint32_t i = 0u; i < 6u; ++i) {
        const uint32_t op = binary_ops[i];
        refuse_binary(ctx, op, &badw, &u8b, true, GGI_ERR_BAD_WIDTH);
        refuse_binary(ctx, op, &u8a, &badw, true, GGI_ERR_BAD_WIDTH);
        refuse_binary(ctx, op, &bads, &u8b, true, GGI_ERR_BAD_SIGNEDNESS);
        refuse_binary(ctx, op, &u8a, &bads, true, GGI_ERR_BAD_SIGNEDNESS);
        refuse_binary(ctx, op, &unnorm, &u8b, true, GGI_ERR_UNNORMALIZED);
        refuse_binary(ctx, op, &u8a, &unnorm, true, GGI_ERR_UNNORMALIZED);
        refuse_binary(ctx, op, &badw, &bads, true, GGI_ERR_BAD_WIDTH);
        refuse_binary(ctx, op, &unnorm, &u16, true, GGI_ERR_UNNORMALIZED);
        refuse_binary(ctx, op, &u8a, &u16, true, GGI_ERR_TYPE_MISMATCH);
        refuse_binary(ctx, op, &u8a, &s8a, true, GGI_ERR_TYPE_MISMATCH);
        refuse_binary(ctx, op, &s8a, &u8a, true, GGI_ERR_TYPE_MISMATCH);
        refuse_binary(ctx, op, &u64, &s64, true, GGI_ERR_TYPE_MISMATCH);
    }
    for (uint32_t i = 0u; i < 3u; ++i) {
        refuse_binary(ctx, arith_ops[i], &s8a, &s8a, true, GGI_ERR_DOMAIN);
        refuse_binary(ctx, arith_ops[i], &s64, &s64, true, GGI_ERR_DOMAIN);
    }

    /* ---- ggi_unary ---- */
    refuse_unary(ctx, GGI_OP_NOT, nullptr, true, GGI_ERR_NULL);
    refuse_unary(ctx, GGI_OP_NOT, &u8a, false, GGI_ERR_NULL);
    refuse_unary(ctx, 0u, nullptr, true, GGI_ERR_NULL);
    for (uint32_t i = 0u; i < 5u; ++i) {
        refuse_unary(ctx, unknown[i], &u8a, true, GGI_ERR_UNSUPPORTED_OP);
    }
    for (uint32_t i = 0u; i < 11u; ++i) {
        refuse_unary(ctx, excluded[i], &u8a, true, GGI_ERR_UNSUPPORTED_OP);
    }
    for (uint32_t i = 0u; i < 3u; ++i) {
        refuse_unary(ctx, compare_ops[i], &u8a, true, GGI_ERR_UNSUPPORTED_OP);
    }
    for (uint32_t i = 0u; i < 6u; ++i) {
        refuse_unary(ctx, binary_ops[i], &u8a, true, GGI_ERR_UNSUPPORTED_OP);
    }
    refuse_unary(ctx, 0u, &badw, true, GGI_ERR_UNSUPPORTED_OP);
    refuse_unary(ctx, GGI_OP_NOT, &badw, true, GGI_ERR_BAD_WIDTH);
    refuse_unary(ctx, GGI_OP_NOT, &badw128, true, GGI_ERR_BAD_WIDTH);
    refuse_unary(ctx, GGI_OP_NOT, &bads, true, GGI_ERR_BAD_SIGNEDNESS);
    refuse_unary(ctx, GGI_OP_NOT, &unnorm, true, GGI_ERR_UNNORMALIZED);
    refuse_unary(ctx, GGI_OP_NOT, &unnorm32, true, GGI_ERR_UNNORMALIZED);

    /* ---- ggi_convert ---- */
    refuse_convert(ctx, nullptr, 8u, 0u, true, GGI_ERR_NULL);
    refuse_convert(ctx, &u8a, 8u, 0u, false, GGI_ERR_NULL);
    refuse_convert(ctx, &u8a, 7u, 2u, false, GGI_ERR_NULL);
    refuse_convert(ctx, &badw, 8u, 0u, true, GGI_ERR_BAD_WIDTH);
    refuse_convert(ctx, &bads, 8u, 0u, true, GGI_ERR_BAD_SIGNEDNESS);
    refuse_convert(ctx, &unnorm, 16u, 0u, true, GGI_ERR_UNNORMALIZED);
    refuse_convert(ctx, &unnorm32, 64u, 1u, true, GGI_ERR_UNNORMALIZED);
    const uint32_t bad_dst_widths[6] = {0u, 1u, 7u, 12u, 128u, 0xFFFFFFFFu};
    for (uint32_t i = 0u; i < 6u; ++i) {
        refuse_convert(ctx, &u8a, bad_dst_widths[i], 0u, true,
                       GGI_ERR_BAD_WIDTH);
        refuse_convert(ctx, &s64, bad_dst_widths[i], 1u, true,
                       GGI_ERR_BAD_WIDTH);
    }
    const uint32_t bad_dst_signs[3] = {2u, 3u, 0xFFFFFFFFu};
    for (uint32_t i = 0u; i < 3u; ++i) {
        refuse_convert(ctx, &u8a, 16u, bad_dst_signs[i], true,
                       GGI_ERR_BAD_SIGNEDNESS);
        refuse_convert(ctx, &s64, 64u, bad_dst_signs[i], true,
                       GGI_ERR_BAD_SIGNEDNESS);
    }
    /* Source checks precede destination checks; width precedes signedness. */
    refuse_convert(ctx, &bads, 7u, 0u, true, GGI_ERR_BAD_SIGNEDNESS);
    refuse_convert(ctx, &unnorm, 0u, 2u, true, GGI_ERR_UNNORMALIZED);
    refuse_convert(ctx, &u8a, 7u, 2u, true, GGI_ERR_BAD_WIDTH);
}

/* Claude review T1/T2/T3/T4/T6: additional independently authored coverage.
 * Expected tables below are literal mathematical boundary results. No sign-bit
 * transform, production mask helper, or implementation-derived oracle is used.
 * The provider's original seven test bodies above remain unchanged.
 */
void test_reviewed_boundary_completion(Ctx *ctx)
{
    const uint32_t widths[2] = {16u, 32u};
    /* Ordered bits: zero, one, positive maximum, sign boundary, all ones. */
    const uint64_t values[2][5] = {
        {0x0000ull, 0x0001ull, 0x7FFFull, 0x8000ull, 0xFFFFull},
        {0x00000000ull, 0x00000001ull, 0x7FFFFFFFull,
         0x80000000ull, 0xFFFFFFFFull},
    };
    const uint8_t eq[5][5] = {
        {1u, 0u, 0u, 0u, 0u},
        {0u, 1u, 0u, 0u, 0u},
        {0u, 0u, 1u, 0u, 0u},
        {0u, 0u, 0u, 1u, 0u},
        {0u, 0u, 0u, 0u, 1u},
    };
    const uint8_t lt_u[5][5] = {
        {0u, 1u, 1u, 1u, 1u},
        {0u, 0u, 1u, 1u, 1u},
        {0u, 0u, 0u, 1u, 1u},
        {0u, 0u, 0u, 0u, 1u},
        {0u, 0u, 0u, 0u, 0u},
    };
    /* Signed order of the same bit patterns is: minimum, -1, 0, 1, max. */
    const uint8_t lt_s[5][5] = {
        {0u, 1u, 1u, 0u, 0u},
        {0u, 0u, 1u, 0u, 0u},
        {0u, 0u, 0u, 0u, 0u},
        {1u, 1u, 1u, 0u, 1u},
        {1u, 1u, 1u, 0u, 0u},
    };
    const uint64_t add[2][5][5] = {
        {
            {0x0000ull, 0x0001ull, 0x7FFFull, 0x8000ull, 0xFFFFull},
            {0x0001ull, 0x0002ull, 0x8000ull, 0x8001ull, 0x0000ull},
            {0x7FFFull, 0x8000ull, 0xFFFEull, 0xFFFFull, 0x7FFEull},
            {0x8000ull, 0x8001ull, 0xFFFFull, 0x0000ull, 0x7FFFull},
            {0xFFFFull, 0x0000ull, 0x7FFEull, 0x7FFFull, 0xFFFEull},
        },
        {
            {0x00000000ull, 0x00000001ull, 0x7FFFFFFFull, 0x80000000ull, 0xFFFFFFFFull},
            {0x00000001ull, 0x00000002ull, 0x80000000ull, 0x80000001ull, 0x00000000ull},
            {0x7FFFFFFFull, 0x80000000ull, 0xFFFFFFFEull, 0xFFFFFFFFull, 0x7FFFFFFEull},
            {0x80000000ull, 0x80000001ull, 0xFFFFFFFFull, 0x00000000ull, 0x7FFFFFFFull},
            {0xFFFFFFFFull, 0x00000000ull, 0x7FFFFFFEull, 0x7FFFFFFFull, 0xFFFFFFFEull},
        },
    };
    const uint64_t sub[2][5][5] = {
        {
            {0x0000ull, 0xFFFFull, 0x8001ull, 0x8000ull, 0x0001ull},
            {0x0001ull, 0x0000ull, 0x8002ull, 0x8001ull, 0x0002ull},
            {0x7FFFull, 0x7FFEull, 0x0000ull, 0xFFFFull, 0x8000ull},
            {0x8000ull, 0x7FFFull, 0x0001ull, 0x0000ull, 0x8001ull},
            {0xFFFFull, 0xFFFEull, 0x8000ull, 0x7FFFull, 0x0000ull},
        },
        {
            {0x00000000ull, 0xFFFFFFFFull, 0x80000001ull, 0x80000000ull, 0x00000001ull},
            {0x00000001ull, 0x00000000ull, 0x80000002ull, 0x80000001ull, 0x00000002ull},
            {0x7FFFFFFFull, 0x7FFFFFFEull, 0x00000000ull, 0xFFFFFFFFull, 0x80000000ull},
            {0x80000000ull, 0x7FFFFFFFull, 0x00000001ull, 0x00000000ull, 0x80000001ull},
            {0xFFFFFFFFull, 0xFFFFFFFEull, 0x80000000ull, 0x7FFFFFFFull, 0x00000000ull},
        },
    };
    const uint64_t mul[2][5][5] = {
        {
            {0x0000ull, 0x0000ull, 0x0000ull, 0x0000ull, 0x0000ull},
            {0x0000ull, 0x0001ull, 0x7FFFull, 0x8000ull, 0xFFFFull},
            {0x0000ull, 0x7FFFull, 0x0001ull, 0x8000ull, 0x8001ull},
            {0x0000ull, 0x8000ull, 0x8000ull, 0x0000ull, 0x8000ull},
            {0x0000ull, 0xFFFFull, 0x8001ull, 0x8000ull, 0x0001ull},
        },
        {
            {0x00000000ull, 0x00000000ull, 0x00000000ull, 0x00000000ull, 0x00000000ull},
            {0x00000000ull, 0x00000001ull, 0x7FFFFFFFull, 0x80000000ull, 0xFFFFFFFFull},
            {0x00000000ull, 0x7FFFFFFFull, 0x00000001ull, 0x80000000ull, 0x80000001ull},
            {0x00000000ull, 0x80000000ull, 0x80000000ull, 0x00000000ull, 0x80000000ull},
            {0x00000000ull, 0xFFFFFFFFull, 0x80000001ull, 0x80000000ull, 0x00000001ull},
        },
    };
    for (uint32_t w = 0u; w < 2u; ++w) {
        for (uint32_t i = 0u; i < 5u; ++i) {
            for (uint32_t j = 0u; j < 5u; ++j) {
                const ggi_int ua = mk(values[w][i], widths[w], 0u);
                const ggi_int ub = mk(values[w][j], widths[w], 0u);
                const ggi_int sa = mk(values[w][i], widths[w], 1u);
                const ggi_int sb = mk(values[w][j], widths[w], 1u);
                expect_compare(ctx, GGI_OP_EQ, ua, ub, eq[i][j]);
                expect_compare(ctx, GGI_OP_EQ, sa, sb, eq[i][j]);
                expect_compare(ctx, GGI_OP_LT_U, ua, ub, lt_u[i][j]);
                expect_compare(ctx, GGI_OP_LT_S, sa, sb, lt_s[i][j]);
                expect_binary(ctx, GGI_OP_ADD_U, ua, ub, add[w][i][j]);
                expect_binary(ctx, GGI_OP_SUB_U, ua, ub, sub[w][i][j]);
                expect_binary(ctx, GGI_OP_MUL_U, ua, ub, mul[w][i][j]);
            }
        }
    }

    /* T2: matched types reach domain refusal; mixed tags refuse earlier. */
    const uint32_t arithmetic_ops[3] = {GGI_OP_ADD_U, GGI_OP_SUB_U, GGI_OP_MUL_U};
    for (uint32_t w = 0u; w < 2u; ++w) {
        const ggi_int u = mk(values[w][3], widths[w], 0u);
        const ggi_int s = mk(values[w][3], widths[w], 1u);
        refuse_compare(ctx, GGI_OP_LT_S, &u, &u, true, GGI_ERR_DOMAIN);
        refuse_compare(ctx, GGI_OP_LT_U, &s, &s, true, GGI_ERR_DOMAIN);
        refuse_compare(ctx, GGI_OP_LT_S, &u, &s, true, GGI_ERR_TYPE_MISMATCH);
        refuse_compare(ctx, GGI_OP_LT_U, &s, &u, true, GGI_ERR_TYPE_MISMATCH);
        for (uint32_t op = 0u; op < 3u; ++op) {
            refuse_binary(ctx, arithmetic_ops[op], &s, &s, true, GGI_ERR_DOMAIN);
            refuse_binary(ctx, arithmetic_ops[op], &s, &u, true, GGI_ERR_TYPE_MISMATCH);
            refuse_binary(ctx, arithmetic_ops[op], &u, &s, true, GGI_ERR_TYPE_MISMATCH);
        }
    }

    /* T3: every excluded category wins over invalid nonnull operands. */
    const uint32_t excluded[11] = {
        GGI_OP_EXCLUDED_ADD_S, GGI_OP_EXCLUDED_SUB_S, GGI_OP_EXCLUDED_MUL_S,
        GGI_OP_EXCLUDED_DIV_U, GGI_OP_EXCLUDED_DIV_S, GGI_OP_EXCLUDED_REM_U,
        GGI_OP_EXCLUDED_REM_S, GGI_OP_EXCLUDED_SHL, GGI_OP_EXCLUDED_SHR_U,
        GGI_OP_EXCLUDED_SHR_S, GGI_OP_EXCLUDED_FLOAT,
    };
    const ggi_int valid = mk(0x1234ull, 16u, 0u);
    const ggi_int invalid[6] = {
        mk(0x10000ull, 12u, 2u),
        mk(0x10000ull, 16u, 2u),
        mk(0x10000ull, 16u, 0u),
        mk(0xFFFFFFFFFFFF0000ull, 16u, 1u),
        mk(0x100000000ull, 32u, 0u),
        mk(0xFFFFFFFFFFFFFFFFull, 32u, 1u),
    };
    for (uint32_t op = 0u; op < 11u; ++op) {
        for (uint32_t v = 0u; v < 6u; ++v) {
            refuse_compare(ctx, excluded[op], &invalid[v], &valid, true, GGI_ERR_UNSUPPORTED_OP);
            refuse_compare(ctx, excluded[op], &valid, &invalid[v], true, GGI_ERR_UNSUPPORTED_OP);
            refuse_compare(ctx, excluded[op], &invalid[v], &invalid[v], true, GGI_ERR_UNSUPPORTED_OP);
            refuse_binary(ctx, excluded[op], &invalid[v], &valid, true, GGI_ERR_UNSUPPORTED_OP);
            refuse_binary(ctx, excluded[op], &valid, &invalid[v], true, GGI_ERR_UNSUPPORTED_OP);
            refuse_binary(ctx, excluded[op], &invalid[v], &invalid[v], true, GGI_ERR_UNSUPPORTED_OP);
            refuse_unary(ctx, excluded[op], &invalid[v], true, GGI_ERR_UNSUPPORTED_OP);
        }
        /* Null remains first; a real nonnull output canary is still passed. */
        refuse_compare(ctx, excluded[op], nullptr, &invalid[0], true, GGI_ERR_NULL);
        refuse_binary(ctx, excluded[op], &invalid[0], nullptr, true, GGI_ERR_NULL);
        refuse_unary(ctx, excluded[op], nullptr, true, GGI_ERR_NULL);
    }

    /* T6: both tags, both operands, just-outside-width and high-bit garbage. */
    const uint64_t unnormalized[2][2] = {
        {0x10000ull, 0xFFFFFFFFFFFF0000ull},
        {0x100000000ull, 0xFFFFFFFFFFFFFFFFull},
    };
    const uint32_t compare_ops[3] = {GGI_OP_EQ, GGI_OP_LT_S, GGI_OP_LT_U};
    const uint32_t binary_ops[6] = {GGI_OP_AND, GGI_OP_OR, GGI_OP_XOR,
                                   GGI_OP_ADD_U, GGI_OP_SUB_U, GGI_OP_MUL_U};
    for (uint32_t w = 0u; w < 2u; ++w) {
        for (uint32_t sign = 0u; sign < 2u; ++sign) {
            const ggi_int good = mk(values[w][4], widths[w], sign);
            for (uint32_t v = 0u; v < 2u; ++v) {
                const ggi_int bad = mk(unnormalized[w][v], widths[w], sign);
                const Snap bad_snap = snap_of(&bad);
                check(ctx, ggi_validate(&bad) == GGI_ERR_UNNORMALIZED);
                check(ctx, unchanged(&bad, bad_snap));
                for (uint32_t op = 0u; op < 3u; ++op) {
                    refuse_compare(ctx, compare_ops[op], &bad, &good, true, GGI_ERR_UNNORMALIZED);
                    refuse_compare(ctx, compare_ops[op], &good, &bad, true, GGI_ERR_UNNORMALIZED);
                }
                for (uint32_t op = 0u; op < 6u; ++op) {
                    refuse_binary(ctx, binary_ops[op], &bad, &good, true, GGI_ERR_UNNORMALIZED);
                    refuse_binary(ctx, binary_ops[op], &good, &bad, true, GGI_ERR_UNNORMALIZED);
                }
                refuse_unary(ctx, GGI_OP_NOT, &bad, true, GGI_ERR_UNNORMALIZED);
                refuse_convert(ctx, &bad, 12u, 2u, true, GGI_ERR_UNNORMALIZED);
            }
        }
    }

    /* T4: systematic boundary neighbors at every relevant truncation/sign
     * discontinuity. Each destination tag uses the same literal bits; source
     * signedness alone decides widening. Existing exhaustive tests are kept.
     */
    struct ConversionBoundary {
        uint32_t width;
        uint64_t bits;
        uint64_t e8;
        uint64_t e16;
        uint64_t e32_u;
        uint64_t e32_s;
        uint64_t e64_u;
        uint64_t e64_s;
    };
    const ConversionBoundary conversions[] = {
        {16u, 0x007Full, 0x7Full, 0x007Full, 0x007Full, 0x007Full, 0x007Full, 0x007Full},
        {16u, 0x0080ull, 0x80ull, 0x0080ull, 0x0080ull, 0x0080ull, 0x0080ull, 0x0080ull},
        {16u, 0x00FFull, 0xFFull, 0x00FFull, 0x00FFull, 0x00FFull, 0x00FFull, 0x00FFull},
        {16u, 0x0100ull, 0x00ull, 0x0100ull, 0x0100ull, 0x0100ull, 0x0100ull, 0x0100ull},
        {16u, 0x7FFEull, 0xFEull, 0x7FFEull, 0x7FFEull, 0x7FFEull, 0x7FFEull, 0x7FFEull},
        {16u, 0x8001ull, 0x01ull, 0x8001ull, 0x8001ull, 0xFFFF8001ull, 0x8001ull, 0xFFFFFFFFFFFF8001ull},
        {16u, 0xFFFEull, 0xFEull, 0xFFFEull, 0xFFFEull, 0xFFFFFFFEull, 0xFFFEull, 0xFFFFFFFFFFFFFFFEull},
        {32u, 0x000000FFull, 0xFFull, 0x00FFull, 0x000000FFull, 0x000000FFull, 0x000000FFull, 0x000000FFull},
        {32u, 0x00000100ull, 0x00ull, 0x0100ull, 0x00000100ull, 0x00000100ull, 0x00000100ull, 0x00000100ull},
        {32u, 0x00007FFFull, 0xFFull, 0x7FFFull, 0x00007FFFull, 0x00007FFFull, 0x00007FFFull, 0x00007FFFull},
        {32u, 0x00008000ull, 0x00ull, 0x8000ull, 0x00008000ull, 0x00008000ull, 0x00008000ull, 0x00008000ull},
        {32u, 0x0000FFFFull, 0xFFull, 0xFFFFull, 0x0000FFFFull, 0x0000FFFFull, 0x0000FFFFull, 0x0000FFFFull},
        {32u, 0x00010000ull, 0x00ull, 0x0000ull, 0x00010000ull, 0x00010000ull, 0x00010000ull, 0x00010000ull},
        {32u, 0x7FFFFFFEull, 0xFEull, 0xFFFEull, 0x7FFFFFFEull, 0x7FFFFFFEull, 0x7FFFFFFEull, 0x7FFFFFFEull},
        {32u, 0x80000001ull, 0x01ull, 0x0001ull, 0x80000001ull, 0x80000001ull, 0x80000001ull, 0xFFFFFFFF80000001ull},
        {32u, 0xFFFFFFFEull, 0xFEull, 0xFFFEull, 0xFFFFFFFEull, 0xFFFFFFFEull, 0xFFFFFFFEull, 0xFFFFFFFFFFFFFFFEull},
        {64u, 0x00000000000000FFull, 0xFFull, 0x00FFull, 0x000000FFull, 0x000000FFull, 0x00000000000000FFull, 0x00000000000000FFull},
        {64u, 0x0000000000000100ull, 0x00ull, 0x0100ull, 0x00000100ull, 0x00000100ull, 0x0000000000000100ull, 0x0000000000000100ull},
        {64u, 0x000000000000FFFFull, 0xFFull, 0xFFFFull, 0x0000FFFFull, 0x0000FFFFull, 0x000000000000FFFFull, 0x000000000000FFFFull},
        {64u, 0x0000000000010000ull, 0x00ull, 0x0000ull, 0x00010000ull, 0x00010000ull, 0x0000000000010000ull, 0x0000000000010000ull},
        {64u, 0x000000007FFFFFFFull, 0xFFull, 0xFFFFull, 0x7FFFFFFFull, 0x7FFFFFFFull, 0x000000007FFFFFFFull, 0x000000007FFFFFFFull},
        {64u, 0x0000000080000000ull, 0x00ull, 0x0000ull, 0x80000000ull, 0x80000000ull, 0x0000000080000000ull, 0x0000000080000000ull},
        {64u, 0x00000000FFFFFFFFull, 0xFFull, 0xFFFFull, 0xFFFFFFFFull, 0xFFFFFFFFull, 0x00000000FFFFFFFFull, 0x00000000FFFFFFFFull},
        {64u, 0x0000000100000000ull, 0x00ull, 0x0000ull, 0x00000000ull, 0x00000000ull, 0x0000000100000000ull, 0x0000000100000000ull},
        {64u, 0x7FFFFFFFFFFFFFFEull, 0xFEull, 0xFFFEull, 0xFFFFFFFEull, 0xFFFFFFFEull, 0x7FFFFFFFFFFFFFFEull, 0x7FFFFFFFFFFFFFFEull},
        {64u, 0x8000000000000001ull, 0x01ull, 0x0001ull, 0x00000001ull, 0x00000001ull, 0x8000000000000001ull, 0x8000000000000001ull},
        {64u, 0xFFFFFFFFFFFFFFFEull, 0xFEull, 0xFFFEull, 0xFFFFFFFEull, 0xFFFFFFFEull, 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFEull},
    };
    for (uint32_t i = 0u; i < sizeof conversions / sizeof conversions[0]; ++i) {
        const ConversionBoundary &row = conversions[i];
        for (uint32_t source_sign = 0u; source_sign < 2u; ++source_sign) {
            const ggi_int src = mk(row.bits, row.width, source_sign);
            const uint64_t e32 = source_sign == 0u ? row.e32_u : row.e32_s;
            const uint64_t e64 = source_sign == 0u ? row.e64_u : row.e64_s;
            for (uint32_t destination_sign = 0u; destination_sign < 2u; ++destination_sign) {
                expect_convert(ctx, src, 8u, destination_sign, row.e8);
                expect_convert(ctx, src, 16u, destination_sign, row.e16);
                expect_convert(ctx, src, 32u, destination_sign, e32);
                expect_convert(ctx, src, 64u, destination_sign, e64);
            }
        }
    }
}

} // namespace

extern "C" uint32_t ggi_run_tests(void)
{
    Ctx ctx;
    ctx.failures = 0u;
    test_validate(&ctx);
    test_compare_literals(&ctx);
    test_binary_unary_literals(&ctx);
    test_width64_matrix(&ctx);
    test_convert_literals(&ctx);
    test_exhaustive_width8(&ctx);
    test_refusals(&ctx);
    test_reviewed_boundary_completion(&ctx);
    return ctx.failures;
}

#if defined(GAMEPLAY_GRAPH_INTEGER_STANDALONE_TEST_MAIN)
int main()
{
    return ggi_run_tests() == 0u ? 0 : 1;
}
#endif
