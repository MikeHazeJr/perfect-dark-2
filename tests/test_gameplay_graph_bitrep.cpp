// test_gameplay_graph_bitrep.cpp - standalone tests for the b32 boundary.
// Authored only: not compiled, not run. No pass result is claimed.
//
// Float objects exist only as guard fields; they are set and observed solely
// by memcpy from/to uint32_t literal objects and byte buffers. No float is
// assigned, compared, passed or returned by value.
//
// Known gaps: transient writes that are later restored, and reads beyond the
// four active bytes that have no observable effect, are not detectable.
// Expected bytes come from native literal objects, so no serialized byte
// order is proven.

#include "gameplay_graph_bitrep.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#define B32_TEST_OBJECT_BYTES 4u
#define B32_TEST_CANARY_BYTES 8u

extern "C" uint32_t b32_run_tests(void) noexcept;

namespace {

struct GuardI32 {
    typedef int32_t value_type;
    unsigned char pre[B32_TEST_CANARY_BYTES];
    int32_t value;
    unsigned char post[B32_TEST_CANARY_BYTES];
};

struct GuardU32 {
    typedef uint32_t value_type;
    unsigned char pre[B32_TEST_CANARY_BYTES];
    uint32_t value;
    unsigned char post[B32_TEST_CANARY_BYTES];
};

struct GuardF32 {
    typedef float value_type;
    unsigned char pre[B32_TEST_CANARY_BYTES];
    float value;
    unsigned char post[B32_TEST_CANARY_BYTES];
};

void check(uint32_t *failures, int cond)
{
    if (!cond) {
        *failures += 1u;
    }
}

// Whole guard (including any padding) is memset first; canaries and the
// value are then written only by memcpy, never by member assignment.
template <typename G>
void guard_init(G *g, unsigned char fill, unsigned char seed, const void *value4)
{
    unsigned char pattern[B32_TEST_CANARY_BYTES];
    std::size_t i;

    std::memset(g, fill, sizeof(G));
    for (i = 0; i < B32_TEST_CANARY_BYTES; ++i) {
        pattern[i] = (unsigned char)(seed + i);
    }
    std::memcpy(g->pre, pattern, B32_TEST_CANARY_BYTES);
    for (i = 0; i < B32_TEST_CANARY_BYTES; ++i) {
        pattern[i] = (unsigned char)((seed ^ 0xffu) - i);
    }
    std::memcpy(g->post, pattern, B32_TEST_CANARY_BYTES);
    std::memcpy(&g->value, value4, B32_TEST_OBJECT_BYTES);
}

// One call with fresh guarded source/output objects.
// expected_status is the literal 0u (success) or 1u (null refusal).
template <typename GI, typename GO, typename Fn>
void run_case(uint32_t *failures, Fn fn, int input_nonnull, int output_nonnull,
              b32_status expected_status, const void *source4,
              const void *expected4, const void *sentinel4)
{
    GI gi;
    GO go;
    unsigned char gi_snap[sizeof(GI)];
    unsigned char go_snap[sizeof(GO)];
    unsigned char gi_after[sizeof(GI)];
    unsigned char go_after[sizeof(GO)];
    const std::size_t window = offsetof(GO, value);
    std::size_t i;
    b32_status status;

    guard_init(&gi, (unsigned char)0x5au, (unsigned char)0x11u, source4);
    guard_init(&go, (unsigned char)0xc3u, (unsigned char)0x91u, sentinel4);
    std::memcpy(gi_snap, &gi, sizeof(GI));
    std::memcpy(go_snap, &go, sizeof(GO));

    // Overwrite witness precondition: prior output bytes differ from expected.
    check(failures, std::memcmp(sentinel4, expected4, B32_TEST_OBJECT_BYTES) != 0);

    const typename GI::value_type *in = input_nonnull ? &gi.value : nullptr;
    typename GO::value_type *out = output_nonnull ? &go.value : nullptr;
    status = fn(in, out);

    check(failures, status == expected_status);
    if (expected_status == 0u) {
        check(failures, status == B32_OK);
    } else {
        check(failures, status == B32_NULL);
    }

    std::memcpy(gi_after, &gi, sizeof(GI));
    std::memcpy(go_after, &go, sizeof(GO));

    // Source guard is read only in every case.
    check(failures, std::memcmp(gi_snap, gi_after, sizeof(GI)) == 0);

    if (expected_status == 0u) {
        for (i = 0; i < sizeof(GO); ++i) {
            if (i < window || i >= window + B32_TEST_OBJECT_BYTES) {
                check(failures, go_after[i] == go_snap[i]);
            }
        }
        check(failures, std::memcmp(go_after + window, expected4, B32_TEST_OBJECT_BYTES) == 0);
        check(failures, std::memcmp(go_after + window, sentinel4, B32_TEST_OBJECT_BYTES) != 0);
    } else {
        check(failures, std::memcmp(go_snap, go_after, sizeof(GO)) == 0);
    }
}

void test_i32_to_u32_table(uint32_t *failures)
{
    struct Case {
        int32_t source;
        uint32_t expected;
    };
    Case cases[] = {
        { (-2147483647 - 1), 0x80000000u },
        { 2147483647, 0x7fffffffu },
        { 0, 0x00000000u },
        { 1, 0x00000001u },
        { -1, 0xffffffffu },
        { -2, 0xfffffffeu },
        { 305419896, 0x12345678u },
        { -19088744, 0xfedcba98u },
        { -1431655766, 0xaaaaaaaau },
        { 1431655765, 0x55555555u },
    };
    std::size_t k;

    for (k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k) {
        uint32_t sentinel = ~cases[k].expected;
        run_case<GuardI32, GuardU32>(failures, b32_i32_to_u32, 1, 1, 0u,
                                     &cases[k].source, &cases[k].expected, &sentinel);
    }
}

void test_u32_to_i32_table(uint32_t *failures)
{
    struct Case {
        uint32_t source;
        int32_t expected;
    };
    Case cases[] = {
        { 0x80000000u, (-2147483647 - 1) },
        { 0x7fffffffu, 2147483647 },
        { 0x00000000u, 0 },
        { 0x00000001u, 1 },
        { 0xffffffffu, -1 },
        { 0xfffffffeu, -2 },
        { 0x12345678u, 305419896 },
        { 0xfedcba98u, -19088744 },
        { 0xaaaaaaaau, -1431655766 },
        { 0x55555555u, 1431655765 },
    };
    std::size_t k;

    for (k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k) {
        int32_t sentinel = ~cases[k].expected;
        run_case<GuardU32, GuardI32>(failures, b32_u32_to_i32, 1, 1, 0u,
                                     &cases[k].source, &cases[k].expected, &sentinel);
    }
}

void test_f32_to_u32_table(uint32_t *failures)
{
    uint32_t source_bits[20] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
        0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
        0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
        0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u,
        0x7fc00001u, 0xffc00001u, 0x7fffffffu, 0xffffffffu,
    };
    uint32_t expected_bits[20] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
        0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
        0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
        0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u,
        0x7fc00001u, 0xffc00001u, 0x7fffffffu, 0xffffffffu,
    };
    std::size_t k;

    for (k = 0; k < 20u; ++k) {
        uint32_t sentinel = ~expected_bits[k];
        run_case<GuardF32, GuardU32>(failures, b32_f32_to_u32, 1, 1, 0u,
                                     &source_bits[k], &expected_bits[k], &sentinel);
    }
}

void test_u32_to_f32_table(uint32_t *failures)
{
    uint32_t source_bits[20] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
        0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
        0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
        0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u,
        0x7fc00001u, 0xffc00001u, 0x7fffffffu, 0xffffffffu,
    };
    uint32_t expected_bits[20] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
        0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
        0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
        0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u,
        0x7fc00001u, 0xffc00001u, 0x7fffffffu, 0xffffffffu,
    };
    std::size_t k;

    for (k = 0; k < 20u; ++k) {
        uint32_t sentinel = ~expected_bits[k];
        run_case<GuardU32, GuardF32>(failures, b32_u32_to_f32, 1, 1, 0u,
                                     &source_bits[k], &expected_bits[k], &sentinel);
    }
}

// All four nullness combinations for each of the four APIs.
void test_null_matrix(uint32_t *failures)
{
    int32_t i_source = -2;
    uint32_t i_expected = 0xfffffffeu;
    uint32_t i_sentinel = 0x00000001u;

    uint32_t u_source = 0x80000000u;
    int32_t u_expected = (-2147483647 - 1);
    int32_t u_sentinel = 2147483647;

    uint32_t f_source_bits = 0x3f800000u;
    uint32_t f_expected_bits = 0x3f800000u;
    uint32_t f_sentinel_bits = 0xc07fffffu;

    uint32_t g_source_bits = 0xbf800000u;
    uint32_t g_expected_bits = 0xbf800000u;
    uint32_t g_sentinel_bits = 0x407fffffu;

    run_case<GuardI32, GuardU32>(failures, b32_i32_to_u32, 1, 1, 0u, &i_source, &i_expected, &i_sentinel);
    run_case<GuardI32, GuardU32>(failures, b32_i32_to_u32, 0, 1, 1u, &i_source, &i_expected, &i_sentinel);
    run_case<GuardI32, GuardU32>(failures, b32_i32_to_u32, 1, 0, 1u, &i_source, &i_expected, &i_sentinel);
    run_case<GuardI32, GuardU32>(failures, b32_i32_to_u32, 0, 0, 1u, &i_source, &i_expected, &i_sentinel);

    run_case<GuardU32, GuardI32>(failures, b32_u32_to_i32, 1, 1, 0u, &u_source, &u_expected, &u_sentinel);
    run_case<GuardU32, GuardI32>(failures, b32_u32_to_i32, 0, 1, 1u, &u_source, &u_expected, &u_sentinel);
    run_case<GuardU32, GuardI32>(failures, b32_u32_to_i32, 1, 0, 1u, &u_source, &u_expected, &u_sentinel);
    run_case<GuardU32, GuardI32>(failures, b32_u32_to_i32, 0, 0, 1u, &u_source, &u_expected, &u_sentinel);

    run_case<GuardF32, GuardU32>(failures, b32_f32_to_u32, 1, 1, 0u, &f_source_bits, &f_expected_bits, &f_sentinel_bits);
    run_case<GuardF32, GuardU32>(failures, b32_f32_to_u32, 0, 1, 1u, &f_source_bits, &f_expected_bits, &f_sentinel_bits);
    run_case<GuardF32, GuardU32>(failures, b32_f32_to_u32, 1, 0, 1u, &f_source_bits, &f_expected_bits, &f_sentinel_bits);
    run_case<GuardF32, GuardU32>(failures, b32_f32_to_u32, 0, 0, 1u, &f_source_bits, &f_expected_bits, &f_sentinel_bits);

    run_case<GuardU32, GuardF32>(failures, b32_u32_to_f32, 1, 1, 0u, &g_source_bits, &g_expected_bits, &g_sentinel_bits);
    run_case<GuardU32, GuardF32>(failures, b32_u32_to_f32, 0, 1, 1u, &g_source_bits, &g_expected_bits, &g_sentinel_bits);
    run_case<GuardU32, GuardF32>(failures, b32_u32_to_f32, 1, 0, 1u, &g_source_bits, &g_expected_bits, &g_sentinel_bits);
    run_case<GuardU32, GuardF32>(failures, b32_u32_to_f32, 0, 0, 1u, &g_source_bits, &g_expected_bits, &g_sentinel_bits);
}

} // namespace

extern "C" uint32_t b32_run_tests(void) noexcept
{
    uint32_t failures = 0u;

    check(&failures, B32_OK == 0u);
    check(&failures, B32_NULL == 1u);

    test_i32_to_u32_table(&failures);
    test_u32_to_i32_table(&failures);
    test_f32_to_u32_table(&failures);
    test_u32_to_f32_table(&failures);
    test_null_matrix(&failures);

    return failures;
}

#ifdef GAMEPLAY_GRAPH_BITREP_STANDALONE_TEST_MAIN
int main(void)
{
    return b32_run_tests() != 0u ? 1 : 0;
}
#endif
