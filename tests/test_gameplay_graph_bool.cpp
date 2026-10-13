// test_gameplay_graph_bool.cpp - standalone authored tests for
// gameplay_graph_bool.h.
//
// gbp_run_tests() returns the failure count. main() exists only when
// GAMEPLAY_GRAPH_BOOL_STANDALONE_TEST_MAIN is defined, and returns nonzero
// iff there are failures.
//
// All expected statuses and results are independent literals. Every success
// case runs with two output sentinels (0xA5A5A5A5, 0x3C3C3C3C), and no
// expected value equals either one. Adjacent guard words are checked on every
// call. Refusals compare a byte snapshot of the whole buffer.
//
// Handoff: these tests are authored only; they have not been compiled or run.
// Assumption: the include path resolves "gameplay_graph_bool.h" to
// port/include/gameplay_graph_bool.h.

#include "gameplay_graph_bool.h"

#include <cstring>

extern "C" uint32_t gbp_run_tests(void) noexcept;

#define GBP_TEST_EXPECT(failures, cond) \
    do {                                \
        if (!(cond)) {                  \
            ++(failures);               \
        }                               \
    } while (0)

namespace {

void check_binary_ok(uint32_t &failures, uint32_t opcode, uint32_t a, uint32_t b, uint32_t expected) noexcept
{
    uint32_t sentinels[2] = {0xA5A5A5A5u, 0x3C3C3C3Cu};
    for (int s = 0; s < 2; ++s) {
        uint32_t buf[3] = {0xC0DEC0DEu, sentinels[s], 0x5EEDF00Du};
        gbp_status st = gbp_logical_binary(opcode, a, b, &buf[1]);
        GBP_TEST_EXPECT(failures, st == 0u);
        GBP_TEST_EXPECT(failures, buf[1] == expected);
        GBP_TEST_EXPECT(failures, buf[0] == 0xC0DEC0DEu);
        GBP_TEST_EXPECT(failures, buf[2] == 0x5EEDF00Du);
    }
}

void check_not_ok(uint32_t &failures, uint32_t a, uint32_t expected) noexcept
{
    uint32_t sentinels[2] = {0xA5A5A5A5u, 0x3C3C3C3Cu};
    for (int s = 0; s < 2; ++s) {
        uint32_t buf[3] = {0xC0DEC0DEu, sentinels[s], 0x5EEDF00Du};
        gbp_status st = gbp_logical_not(a, &buf[1]);
        GBP_TEST_EXPECT(failures, st == 0u);
        GBP_TEST_EXPECT(failures, buf[1] == expected);
        GBP_TEST_EXPECT(failures, buf[0] == 0xC0DEC0DEu);
        GBP_TEST_EXPECT(failures, buf[2] == 0x5EEDF00Du);
    }
}

void check_select_ok(uint32_t &failures, uint32_t condition, uint32_t when_true, uint32_t when_false,
                     uint32_t expected) noexcept
{
    uint32_t sentinels[2] = {0xA5A5A5A5u, 0x3C3C3C3Cu};
    for (int s = 0; s < 2; ++s) {
        uint32_t buf[3] = {0xC0DEC0DEu, sentinels[s], 0x5EEDF00Du};
        gbp_status st = gbp_select_u32(condition, when_true, when_false, &buf[1]);
        GBP_TEST_EXPECT(failures, st == 0u);
        GBP_TEST_EXPECT(failures, buf[1] == expected);
        GBP_TEST_EXPECT(failures, buf[0] == 0xC0DEC0DEu);
        GBP_TEST_EXPECT(failures, buf[2] == 0x5EEDF00Du);
    }
}

void check_binary_bad_opcode(uint32_t &failures, uint32_t opcode, uint32_t a, uint32_t b) noexcept
{
    uint32_t sentinels[2] = {0xA5A5A5A5u, 0x3C3C3C3Cu};
    for (int s = 0; s < 2; ++s) {
        uint32_t buf[3] = {0xC0DEC0DEu, sentinels[s], 0x5EEDF00Du};
        unsigned char before[sizeof(buf)];
        std::memcpy(before, buf, sizeof(buf));
        gbp_status st = gbp_logical_binary(opcode, a, b, &buf[1]);
        GBP_TEST_EXPECT(failures, st == 2u);
        GBP_TEST_EXPECT(failures, std::memcmp(before, buf, sizeof(buf)) == 0);
        GBP_TEST_EXPECT(failures, buf[0] == 0xC0DEC0DEu);
        GBP_TEST_EXPECT(failures, buf[1] == sentinels[s]);
        GBP_TEST_EXPECT(failures, buf[2] == 0x5EEDF00Du);
    }
}

void test_constants(uint32_t &failures) noexcept
{
    GBP_TEST_EXPECT(failures, GBP_OK == 0u);
    GBP_TEST_EXPECT(failures, GBP_NULL_OUTPUT == 1u);
    GBP_TEST_EXPECT(failures, GBP_BAD_OPCODE == 2u);
    GBP_TEST_EXPECT(failures, GBP_AND == 1u);
    GBP_TEST_EXPECT(failures, GBP_OR == 2u);
    GBP_TEST_EXPECT(failures, GBP_XOR == 3u);
    GBP_TEST_EXPECT(failures, sizeof(gbp_status) == sizeof(uint32_t));
}

void test_binary_matrices(uint32_t &failures) noexcept
{
    uint32_t inputs[5] = {0x00000000u, 0x00000001u, 0x80000000u, 0xFFFFFFFFu, 0x00000100u};

    uint32_t expected_and[5][5] = {
        {0u, 0u, 0u, 0u, 0u},
        {0u, 1u, 1u, 1u, 1u},
        {0u, 1u, 1u, 1u, 1u},
        {0u, 1u, 1u, 1u, 1u},
        {0u, 1u, 1u, 1u, 1u},
    };
    uint32_t expected_or[5][5] = {
        {0u, 1u, 1u, 1u, 1u},
        {1u, 1u, 1u, 1u, 1u},
        {1u, 1u, 1u, 1u, 1u},
        {1u, 1u, 1u, 1u, 1u},
        {1u, 1u, 1u, 1u, 1u},
    };
    uint32_t expected_xor[5][5] = {
        {0u, 1u, 1u, 1u, 1u},
        {1u, 0u, 0u, 0u, 0u},
        {1u, 0u, 0u, 0u, 0u},
        {1u, 0u, 0u, 0u, 0u},
        {1u, 0u, 0u, 0u, 0u},
    };

    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            check_binary_ok(failures, GBP_AND, inputs[i], inputs[j], expected_and[i][j]);
            check_binary_ok(failures, GBP_OR, inputs[i], inputs[j], expected_or[i][j]);
            check_binary_ok(failures, GBP_XOR, inputs[i], inputs[j], expected_xor[i][j]);
        }
    }
}

void test_not(uint32_t &failures) noexcept
{
    uint32_t inputs[5] = {0x00000000u, 0x00000001u, 0x80000000u, 0xFFFFFFFFu, 0x00000100u};
    uint32_t expected[5] = {1u, 0u, 0u, 0u, 0u};

    for (int i = 0; i < 5; ++i) {
        check_not_ok(failures, inputs[i], expected[i]);
    }
}

struct SelectCase {
    uint32_t when_true;
    uint32_t when_false;
    uint32_t expected[5];
};

void test_select(uint32_t &failures) noexcept
{
    // Condition rows: 0, 1, 0x80000000, 0xFFFFFFFF, 0x100.
    uint32_t conditions[5] = {0x00000000u, 0x00000001u, 0x80000000u, 0xFFFFFFFFu, 0x00000100u};

    SelectCase cases[9] = {
        {0x00000000u, 0x00000001u, {0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
        {0x00000001u, 0x00000000u, {0x00000000u, 0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u}},
        {0x80000000u, 0x7FFFFFFFu, {0x7FFFFFFFu, 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}},
        {0x7FFFFFFFu, 0x80000000u, {0x80000000u, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu}},
        {0xFFFFFFFFu, 0x00000100u, {0x00000100u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}},
        {0x00000100u, 0xFFFFFFFFu, {0xFFFFFFFFu, 0x00000100u, 0x00000100u, 0x00000100u, 0x00000100u}},
        {0x12345678u, 0x9ABCDEF0u, {0x9ABCDEF0u, 0x12345678u, 0x12345678u, 0x12345678u, 0x12345678u}},
        {0xFFFFFFFEu, 0x00000002u, {0x00000002u, 0xFFFFFFFEu, 0xFFFFFFFEu, 0xFFFFFFFEu, 0xFFFFFFFEu}},
        {0x00000000u, 0xFFFFFFFFu, {0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
    };

    for (int c = 0; c < 9; ++c) {
        for (int i = 0; i < 5; ++i) {
            check_select_ok(failures, conditions[i], cases[c].when_true, cases[c].when_false, cases[c].expected[i]);
        }
    }
}

void test_null_output(uint32_t &failures) noexcept
{
    GBP_TEST_EXPECT(failures, gbp_logical_binary(GBP_AND, 0x00000001u, 0x80000000u, nullptr) == 1u);
    GBP_TEST_EXPECT(failures, gbp_logical_binary(GBP_OR, 0x00000000u, 0x00000100u, nullptr) == 1u);
    GBP_TEST_EXPECT(failures, gbp_logical_binary(GBP_XOR, 0xFFFFFFFFu, 0x00000000u, nullptr) == 1u);

    // Null output takes precedence over a bad opcode.
    GBP_TEST_EXPECT(failures, gbp_logical_binary(0x00000000u, 0x00000001u, 0x00000001u, nullptr) == 1u);
    GBP_TEST_EXPECT(failures, gbp_logical_binary(0x00000004u, 0x80000000u, 0x00000000u, nullptr) == 1u);
    GBP_TEST_EXPECT(failures, gbp_logical_binary(0xFFFFFFFFu, 0x00000100u, 0xFFFFFFFFu, nullptr) == 1u);

    GBP_TEST_EXPECT(failures, gbp_logical_not(0x00000000u, nullptr) == 1u);
    GBP_TEST_EXPECT(failures, gbp_logical_not(0x80000000u, nullptr) == 1u);

    GBP_TEST_EXPECT(failures, gbp_select_u32(0x00000001u, 0x12345678u, 0x9ABCDEF0u, nullptr) == 1u);
    GBP_TEST_EXPECT(failures, gbp_select_u32(0x00000000u, 0x00000000u, 0xFFFFFFFFu, nullptr) == 1u);
}

void test_bad_opcode(uint32_t &failures) noexcept
{
    check_binary_bad_opcode(failures, 0x00000000u, 0x00000001u, 0x00000001u);
    check_binary_bad_opcode(failures, 0x00000000u, 0x00000000u, 0x00000000u);
    check_binary_bad_opcode(failures, 0x00000004u, 0x80000000u, 0x00000000u);
    check_binary_bad_opcode(failures, 0x00000004u, 0xFFFFFFFFu, 0x00000100u);
    check_binary_bad_opcode(failures, 0xFFFFFFFFu, 0x00000100u, 0xFFFFFFFFu);
    check_binary_bad_opcode(failures, 0xFFFFFFFFu, 0x00000000u, 0x00000001u);
}

} // namespace

extern "C" uint32_t gbp_run_tests(void) noexcept
{
    uint32_t failures = 0u;

    test_constants(failures);
    test_binary_matrices(failures);
    test_not(failures);
    test_select(failures);
    test_null_output(failures);
    test_bad_opcode(failures);

    return failures;
}

#ifdef GAMEPLAY_GRAPH_BOOL_STANDALONE_TEST_MAIN
int main(void)
{
    return gbp_run_tests() != 0u ? 1 : 0;
}
#endif
