// test_gameplay_graph_float.cpp - authored standalone tests for
// gameplay_graph_float.h. These tests have NOT been compiled or executed;
// no build, test or runtime verdict is claimed.
//
// Checks authored:
// - Constant values of every status/opcode macro.
// - Binary: exact literal input/result bits for every opcode, including
//   negative/fractional operands, determinate signed-zero and infinity
//   results; NaN-producing cases classified as NaN without payload/sign checks.
// - Unary: 15 literal representations, each with literal NEGATE and ABSOLUTE
//   expected bits (finite, subnormal, signed zeros, infinities, quiet NaNs).
//   No signaling NaNs.
// - Compare: literal six-result 0/1 truth tables over ordered, equal, signed
//   zero, infinity and quiet NaN (either operand and both) rows.
// - Errors: every valid opcode with null output, each bad opcode class
//   (including 0 and 0xFFFFFFFF) with null and nonnull output, null-first
//   precedence, byte-preserving output on refusal, adjacent canaries.
//
// Slot structs are not assumed padding-free: whole-object bytes are
// initialized before snapshots and canary/output members are compared
// independently. Assumptions match the implementation handoff: ordinary
// masked-exception, round-to-nearest native environment; quiet-NaN payload
// preservation through by-value float passing is assumed, not verified.
// No global/static/thread-local objects; all tables are function-local.

#include "gameplay_graph_float.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#define GFPT_PRE_CANARY    UINT32_C(0xC0FFEE01)
#define GFPT_POST_CANARY   UINT32_C(0xC0FFEE02)
#define GFPT_ERROR_CANARY  UINT32_C(0x7FC0FFEE)
#define GFPT_SENTINEL      UINT32_C(0x5EED5EED)
#define GFPT_WORD_SENTINEL UINT32_C(0xA5A5A5A5)

extern "C" uint32_t gfp_run_tests(void) noexcept;

struct FloatSlot {
    uint32_t pre;
    float out;
    uint32_t post;
};

struct WordSlot {
    uint32_t pre;
    uint32_t out;
    uint32_t post;
};

static void expect_true(uint32_t *failures, bool ok)
{
    if (!ok) {
        *failures += 1u;
    }
}

static float float_of(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

static uint32_t bits_at(const float *p)
{
    uint32_t bits;
    std::memcpy(&bits, p, sizeof bits);
    return bits;
}

static bool is_nan_bits(uint32_t bits)
{
    return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0u;
}

static void prepare_float_slot(FloatSlot *slot, uint32_t out_bits)
{
    std::memset(slot, 0xA5, sizeof *slot);
    slot->pre = GFPT_PRE_CANARY;
    std::memcpy(&slot->out, &out_bits, sizeof out_bits);
    slot->post = GFPT_POST_CANARY;
}

static void prepare_word_slot(WordSlot *slot, uint32_t out_value)
{
    std::memset(slot, 0xA5, sizeof *slot);
    slot->pre = GFPT_PRE_CANARY;
    slot->out = out_value;
    slot->post = GFPT_POST_CANARY;
}

static void check_float_canaries(uint32_t *failures, const FloatSlot *slot)
{
    expect_true(failures, slot->pre == GFPT_PRE_CANARY);
    expect_true(failures, slot->post == GFPT_POST_CANARY);
}

static void check_word_canaries(uint32_t *failures, const WordSlot *slot)
{
    expect_true(failures, slot->pre == GFPT_PRE_CANARY);
    expect_true(failures, slot->post == GFPT_POST_CANARY);
}

static void check_float_unchanged(uint32_t *failures, const FloatSlot *before,
                                  const FloatSlot *after)
{
    expect_true(failures, std::memcmp(&before->pre, &after->pre, sizeof before->pre) == 0);
    expect_true(failures, std::memcmp(&before->out, &after->out, sizeof before->out) == 0);
    expect_true(failures, std::memcmp(&before->post, &after->post, sizeof before->post) == 0);
}

static void check_word_unchanged(uint32_t *failures, const WordSlot *before,
                                 const WordSlot *after)
{
    expect_true(failures, std::memcmp(&before->pre, &after->pre, sizeof before->pre) == 0);
    expect_true(failures, std::memcmp(&before->out, &after->out, sizeof before->out) == 0);
    expect_true(failures, std::memcmp(&before->post, &after->post, sizeof before->post) == 0);
}

static void test_constants(uint32_t *failures)
{
    expect_true(failures, GFP_OK == 0u);
    expect_true(failures, GFP_NULL_OUTPUT == 1u);
    expect_true(failures, GFP_BAD_OPCODE == 2u);
    expect_true(failures, GFP_ADD == 1u);
    expect_true(failures, GFP_SUBTRACT == 2u);
    expect_true(failures, GFP_MULTIPLY == 3u);
    expect_true(failures, GFP_DIVIDE == 4u);
    expect_true(failures, GFP_NEGATE == 1u);
    expect_true(failures, GFP_ABSOLUTE == 2u);
    expect_true(failures, GFP_EQUAL == 1u);
    expect_true(failures, GFP_NOT_EQUAL == 2u);
    expect_true(failures, GFP_LESS == 3u);
    expect_true(failures, GFP_LESS_EQUAL == 4u);
    expect_true(failures, GFP_GREATER == 5u);
    expect_true(failures, GFP_GREATER_EQUAL == 6u);
}

static void test_binary_exact(uint32_t *failures)
{
    struct Row {
        uint32_t op;
        uint32_t a;
        uint32_t b;
        uint32_t expect;
    };
    const Row rows[] = {
        { GFP_ADD,      0x3FC00000u, 0x40100000u, 0x40700000u }, // 1.5 + 2.25 = 3.75
        { GFP_ADD,      0xBFC00000u, 0x3E800000u, 0xBFA00000u }, // -1.5 + 0.25 = -1.25
        { GFP_ADD,      0x00000000u, 0x80000000u, 0x00000000u }, // +0 + -0 = +0
        { GFP_ADD,      0x80000000u, 0x80000000u, 0x80000000u }, // -0 + -0 = -0
        { GFP_ADD,      0x7F800000u, 0x3F800000u, 0x7F800000u }, // +inf + 1 = +inf
        { GFP_ADD,      0xFF800000u, 0xBF800000u, 0xFF800000u }, // -inf + -1 = -inf
        { GFP_SUBTRACT, 0xC0200000u, 0x3F400000u, 0xC0500000u }, // -2.5 - 0.75 = -3.25
        { GFP_SUBTRACT, 0x3F000000u, 0x3FA00000u, 0xBF400000u }, // 0.5 - 1.25 = -0.75
        { GFP_SUBTRACT, 0x3F800000u, 0x3F800000u, 0x00000000u }, // 1 - 1 = +0
        { GFP_SUBTRACT, 0x80000000u, 0x00000000u, 0x80000000u }, // -0 - +0 = -0
        { GFP_SUBTRACT, 0x3F800000u, 0x7F800000u, 0xFF800000u }, // 1 - +inf = -inf
        { GFP_MULTIPLY, 0xBFC00000u, 0x40200000u, 0xC0700000u }, // -1.5 * 2.5 = -3.75
        { GFP_MULTIPLY, 0x3F000000u, 0xBE800000u, 0xBE000000u }, // 0.5 * -0.25 = -0.125
        { GFP_MULTIPLY, 0x80000000u, 0x40A00000u, 0x80000000u }, // -0 * 5 = -0
        { GFP_MULTIPLY, 0x80000000u, 0x80000000u, 0x00000000u }, // -0 * -0 = +0
        { GFP_MULTIPLY, 0x7F800000u, 0xC0000000u, 0xFF800000u }, // +inf * -2 = -inf
        { GFP_DIVIDE,   0x40E00000u, 0xBF000000u, 0xC1600000u }, // 7 / -0.5 = -14
        { GFP_DIVIDE,   0x3F400000u, 0xBE800000u, 0xC0400000u }, // 0.75 / -0.25 = -3
        { GFP_DIVIDE,   0x3F800000u, 0x40400000u, 0x3EAAAAABu }, // 1 / 3, nearest
        { GFP_DIVIDE,   0x3F800000u, 0x7F800000u, 0x00000000u }, // 1 / +inf = +0
        { GFP_DIVIDE,   0x00000000u, 0xC0400000u, 0x80000000u }, // +0 / -3 = -0
        { GFP_DIVIDE,   0xBF800000u, 0x00000000u, 0xFF800000u }, // -1 / +0 = -inf
        { GFP_DIVIDE,   0x3F800000u, 0x80000000u, 0xFF800000u }, // 1 / -0 = -inf
    };
    for (std::size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
        FloatSlot slot;
        prepare_float_slot(&slot, GFPT_SENTINEL);
        const uint32_t status =
            gfp_binary(rows[i].op, float_of(rows[i].a), float_of(rows[i].b), &slot.out);
        expect_true(failures, status == GFP_OK);
        expect_true(failures, bits_at(&slot.out) == rows[i].expect);
        check_float_canaries(failures, &slot);
    }
}

static void test_binary_nan(uint32_t *failures)
{
    struct Row {
        uint32_t op;
        uint32_t a;
        uint32_t b;
    };
    // Results are only classified as NaN; payload and sign are not checked.
    const Row rows[] = {
        { GFP_ADD,      0x7F800000u, 0xFF800000u }, // +inf + -inf
        { GFP_ADD,      0x7FC12345u, 0x3F800000u }, // qNaN + 1
        { GFP_SUBTRACT, 0x7F800000u, 0x7F800000u }, // +inf - +inf
        { GFP_SUBTRACT, 0x3F800000u, 0xFFC00000u }, // 1 - -qNaN
        { GFP_MULTIPLY, 0x00000000u, 0x7F800000u }, // 0 * inf
        { GFP_MULTIPLY, 0x7FC00000u, 0xC0000000u }, // qNaN * -2
        { GFP_DIVIDE,   0x00000000u, 0x00000000u }, // 0 / 0
        { GFP_DIVIDE,   0x7F800000u, 0xFF800000u }, // inf / -inf
        { GFP_DIVIDE,   0x3F800000u, 0x7FC00000u }, // 1 / qNaN
    };
    for (std::size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
        FloatSlot slot;
        prepare_float_slot(&slot, GFPT_SENTINEL);
        const uint32_t status =
            gfp_binary(rows[i].op, float_of(rows[i].a), float_of(rows[i].b), &slot.out);
        expect_true(failures, status == GFP_OK);
        expect_true(failures, is_nan_bits(bits_at(&slot.out)));
        check_float_canaries(failures, &slot);
    }
}

static void test_unary(uint32_t *failures)
{
    struct Row {
        uint32_t in;
        uint32_t neg;
        uint32_t abs;
    };
    const Row rows[] = {
        { 0x3F800000u, 0xBF800000u, 0x3F800000u }, // 1
        { 0xBF000000u, 0x3F000000u, 0x3F000000u }, // -0.5
        { 0xC0490FDBu, 0x40490FDBu, 0x40490FDBu }, // -pi
        { 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F7FFFFFu }, // FLT_MAX
        { 0x00800000u, 0x80800000u, 0x00800000u }, // FLT_MIN
        { 0x00000001u, 0x80000001u, 0x00000001u }, // smallest subnormal
        { 0x807FFFFFu, 0x007FFFFFu, 0x007FFFFFu }, // -largest subnormal
        { 0x00000000u, 0x80000000u, 0x00000000u }, // +0
        { 0x80000000u, 0x00000000u, 0x00000000u }, // -0
        { 0x7F800000u, 0xFF800000u, 0x7F800000u }, // +inf
        { 0xFF800000u, 0x7F800000u, 0x7F800000u }, // -inf
        { 0x7FC00000u, 0xFFC00000u, 0x7FC00000u }, // +qNaN
        { 0xFFC00000u, 0x7FC00000u, 0x7FC00000u }, // -qNaN
        { 0x7FC12345u, 0xFFC12345u, 0x7FC12345u }, // +qNaN payload
        { 0xFFEABCDEu, 0x7FEABCDEu, 0x7FEABCDEu }, // -qNaN payload
    };
    for (std::size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
        FloatSlot slot;
        prepare_float_slot(&slot, GFPT_SENTINEL);
        expect_true(failures, gfp_unary(GFP_NEGATE, float_of(rows[i].in), &slot.out) == GFP_OK);
        expect_true(failures, bits_at(&slot.out) == rows[i].neg);
        check_float_canaries(failures, &slot);

        prepare_float_slot(&slot, GFPT_SENTINEL);
        expect_true(failures, gfp_unary(GFP_ABSOLUTE, float_of(rows[i].in), &slot.out) == GFP_OK);
        expect_true(failures, bits_at(&slot.out) == rows[i].abs);
        check_float_canaries(failures, &slot);
    }
}

static void test_compare(uint32_t *failures)
{
    struct Row {
        uint32_t a;
        uint32_t b;
        uint32_t expect[6]; // EQ, NE, LT, LE, GT, GE
    };
    const uint32_t ops[6] = {
        GFP_EQUAL, GFP_NOT_EQUAL, GFP_LESS, GFP_LESS_EQUAL, GFP_GREATER, GFP_GREATER_EQUAL
    };
    const Row rows[] = {
        { 0x3F800000u, 0x40000000u, { 0u, 1u, 1u, 1u, 0u, 0u } }, // 1 vs 2
        { 0x40000000u, 0x40000000u, { 1u, 0u, 0u, 1u, 0u, 1u } }, // 2 vs 2
        { 0x40000000u, 0x3F800000u, { 0u, 1u, 0u, 0u, 1u, 1u } }, // 2 vs 1
        { 0xBFC00000u, 0x3F000000u, { 0u, 1u, 1u, 1u, 0u, 0u } }, // -1.5 vs 0.5
        { 0x00000000u, 0x80000000u, { 1u, 0u, 0u, 1u, 0u, 1u } }, // +0 vs -0
        { 0x80000000u, 0x00000000u, { 1u, 0u, 0u, 1u, 0u, 1u } }, // -0 vs +0
        { 0xFF800000u, 0x7F800000u, { 0u, 1u, 1u, 1u, 0u, 0u } }, // -inf vs +inf
        { 0x7F800000u, 0x7F800000u, { 1u, 0u, 0u, 1u, 0u, 1u } }, // +inf vs +inf
        { 0xFF800000u, 0xFF800000u, { 1u, 0u, 0u, 1u, 0u, 1u } }, // -inf vs -inf
        { 0x7F800000u, 0x3F800000u, { 0u, 1u, 0u, 0u, 1u, 1u } }, // +inf vs 1
        { 0x7FC00000u, 0x3F800000u, { 0u, 1u, 0u, 0u, 0u, 0u } }, // qNaN vs 1
        { 0x3F800000u, 0x7FC00000u, { 0u, 1u, 0u, 0u, 0u, 0u } }, // 1 vs qNaN
        { 0x7FC00000u, 0x7FC00000u, { 0u, 1u, 0u, 0u, 0u, 0u } }, // qNaN vs same qNaN
        { 0xFFC12345u, 0x7F800000u, { 0u, 1u, 0u, 0u, 0u, 0u } }, // -qNaN vs +inf
        { 0x00000000u, 0x7FEABCDEu, { 0u, 1u, 0u, 0u, 0u, 0u } }, // +0 vs qNaN
    };
    for (std::size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
        for (std::size_t k = 0; k < 6; ++k) {
            WordSlot slot;
            prepare_word_slot(&slot, GFPT_WORD_SENTINEL);
            const uint32_t status =
                gfp_compare(ops[k], float_of(rows[i].a), float_of(rows[i].b), &slot.out);
            expect_true(failures, status == GFP_OK);
            expect_true(failures, slot.out == rows[i].expect[k]);
            check_word_canaries(failures, &slot);
        }
    }
}

static void test_binary_errors(uint32_t *failures)
{
    const uint32_t valid[4] = { GFP_ADD, GFP_SUBTRACT, GFP_MULTIPLY, GFP_DIVIDE };
    const uint32_t bad[4] = { 0u, 5u, 0x80000001u, 0xFFFFFFFFu };
    const float a = float_of(0x3F800000u);
    const float b = float_of(0x40000000u);
    for (std::size_t i = 0; i < 4; ++i) {
        expect_true(failures, gfp_binary(valid[i], a, b, nullptr) == GFP_NULL_OUTPUT);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        // Null-first precedence: bad opcode plus null output.
        expect_true(failures, gfp_binary(bad[i], a, b, nullptr) == GFP_NULL_OUTPUT);

        FloatSlot slot;
        FloatSlot before;
        prepare_float_slot(&slot, GFPT_ERROR_CANARY);
        std::memcpy(&before, &slot, sizeof slot);
        expect_true(failures, gfp_binary(bad[i], a, b, &slot.out) == GFP_BAD_OPCODE);
        check_float_unchanged(failures, &before, &slot);
    }
}

static void test_unary_errors(uint32_t *failures)
{
    const uint32_t valid[2] = { GFP_NEGATE, GFP_ABSOLUTE };
    const uint32_t bad[4] = { 0u, 3u, 0x80000001u, 0xFFFFFFFFu };
    const float a = float_of(0xBF800000u);
    for (std::size_t i = 0; i < 2; ++i) {
        expect_true(failures, gfp_unary(valid[i], a, nullptr) == GFP_NULL_OUTPUT);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        expect_true(failures, gfp_unary(bad[i], a, nullptr) == GFP_NULL_OUTPUT);

        FloatSlot slot;
        FloatSlot before;
        prepare_float_slot(&slot, GFPT_ERROR_CANARY);
        std::memcpy(&before, &slot, sizeof slot);
        expect_true(failures, gfp_unary(bad[i], a, &slot.out) == GFP_BAD_OPCODE);
        check_float_unchanged(failures, &before, &slot);
    }
}

static void test_compare_errors(uint32_t *failures)
{
    const uint32_t valid[6] = {
        GFP_EQUAL, GFP_NOT_EQUAL, GFP_LESS, GFP_LESS_EQUAL, GFP_GREATER, GFP_GREATER_EQUAL
    };
    const uint32_t bad[4] = { 0u, 7u, 0x80000001u, 0xFFFFFFFFu };
    const float a = float_of(0x3F800000u);
    const float b = float_of(0x40000000u);
    for (std::size_t i = 0; i < 6; ++i) {
        expect_true(failures, gfp_compare(valid[i], a, b, nullptr) == GFP_NULL_OUTPUT);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        expect_true(failures, gfp_compare(bad[i], a, b, nullptr) == GFP_NULL_OUTPUT);

        WordSlot slot;
        WordSlot before;
        prepare_word_slot(&slot, GFPT_ERROR_CANARY);
        std::memcpy(&before, &slot, sizeof slot);
        expect_true(failures, gfp_compare(bad[i], a, b, &slot.out) == GFP_BAD_OPCODE);
        check_word_unchanged(failures, &before, &slot);
    }
}

extern "C" uint32_t gfp_run_tests(void) noexcept
{
    uint32_t failures = 0u;
    test_constants(&failures);
    test_binary_exact(&failures);
    test_binary_nan(&failures);
    test_unary(&failures);
    test_compare(&failures);
    test_binary_errors(&failures);
    test_unary_errors(&failures);
    test_compare_errors(&failures);
    return failures;
}

#if defined(GAMEPLAY_GRAPH_FLOAT_STANDALONE_TEST_MAIN)
int main(void)
{
    return gfp_run_tests() != 0u ? 1 : 0;
}
#endif
