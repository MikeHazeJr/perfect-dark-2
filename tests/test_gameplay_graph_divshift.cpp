#include "gameplay_graph_divshift.h"

#include <stdint.h>

/*
 * Authored checks only: not compiled or executed. No hashes, byte counts,
 * compiler, test or runtime results have been produced for these files.
 *
 * gds_run_tests is silent (no I/O) and returns 0 only when every check
 * passes, otherwise a positive failure count bounded at 1000000.
 * Every expected value is a hand-written literal; no expected value is
 * computed with '/', '>>' or other arithmetic. Each output object sits
 * between adjacent canaries in a three-element array, and those canaries are
 * checked after every call.
 *
 * Groups: status schema, null gates, division literals, division aliasing,
 * division matrix, shift literals, shift aliasing, repeated/interleaved state.
 */

namespace {

void check(int &fails, bool ok)
{
    if (!ok && fails < 1000000) {
        ++fails;
    }
}

int32_t i32_lo() { return 0x13579BDF; }
int32_t i32_hi() { return 0x2468ACE0; }
int32_t i32_sentinel() { return 0x5A5A5A5A; }
uint32_t u32_lo() { return 0xC3A5C3A5u; }
uint32_t u32_hi() { return 0x3C5A3C5Au; }
uint32_t u32_sentinel() { return 0xDEADBEEFu; }

bool i32_cells(const int32_t *cell, int32_t lo, int32_t mid, int32_t hi)
{
    return cell[0] == lo && cell[1] == mid && cell[2] == hi;
}

bool u32_cells(const uint32_t *cell, uint32_t lo, uint32_t mid, uint32_t hi)
{
    return cell[0] == lo && cell[1] == mid && cell[2] == hi;
}

/* ---- division helpers ---- */

void div_ok(int &fails, int32_t n, int32_t d, int32_t q)
{
    int32_t num[3] = {i32_lo(), n, i32_hi()};
    int32_t den[3] = {i32_lo(), d, i32_hi()};
    int32_t out[3] = {i32_lo(), i32_sentinel(), i32_hi()};
    gds_status st = gds_i32_div_trunc(&num[1], &den[1], &out[1]);
    check(fails, st == GDS_OK);
    check(fails, i32_cells(num, i32_lo(), n, i32_hi()));
    check(fails, i32_cells(den, i32_lo(), d, i32_hi()));
    check(fails, i32_cells(out, i32_lo(), q, i32_hi()));
}

void div_refused(int &fails, int32_t n, int32_t d, gds_status want)
{
    int32_t num[3] = {i32_lo(), n, i32_hi()};
    int32_t den[3] = {i32_lo(), d, i32_hi()};
    int32_t out[3] = {i32_lo(), i32_sentinel(), i32_hi()};
    gds_status st = gds_i32_div_trunc(&num[1], &den[1], &out[1]);
    check(fails, st == want);
    check(fails, i32_cells(num, i32_lo(), n, i32_hi()));
    check(fails, i32_cells(den, i32_lo(), d, i32_hi()));
    check(fails, i32_cells(out, i32_lo(), i32_sentinel(), i32_hi()));
}

void div_null_case(int &fails, bool null_n, bool null_d, bool null_out,
                   int32_t n, int32_t d, gds_status want)
{
    int32_t num[3] = {i32_lo(), n, i32_hi()};
    int32_t den[3] = {i32_lo(), d, i32_hi()};
    int32_t out[3] = {i32_lo(), i32_sentinel(), i32_hi()};
    const int32_t *pn = null_n ? nullptr : &num[1];
    const int32_t *pd = null_d ? nullptr : &den[1];
    int32_t *po = null_out ? nullptr : &out[1];
    gds_status st = gds_i32_div_trunc(pn, pd, po);
    check(fails, st == want);
    check(fails, i32_cells(num, i32_lo(), n, i32_hi()));
    check(fails, i32_cells(den, i32_lo(), d, i32_hi()));
    check(fails, i32_cells(out, i32_lo(), i32_sentinel(), i32_hi()));
}

/* Numerator and denominator share one object; q is ignored on refusal. */
void div_shared(int &fails, int32_t x, gds_status want, int32_t q)
{
    {
        int32_t in[3] = {i32_lo(), x, i32_hi()};
        int32_t out[3] = {i32_lo(), i32_sentinel(), i32_hi()};
        gds_status st = gds_i32_div_trunc(&in[1], &in[1], &out[1]);
        check(fails, st == want);
        check(fails, i32_cells(in, i32_lo(), x, i32_hi()));
        if (want == GDS_OK) {
            check(fails, i32_cells(out, i32_lo(), q, i32_hi()));
        } else {
            check(fails, i32_cells(out, i32_lo(), i32_sentinel(), i32_hi()));
        }
    }
    {
        int32_t in[3] = {i32_lo(), x, i32_hi()};
        gds_status st = gds_i32_div_trunc(&in[1], &in[1], &in[1]);
        check(fails, st == want);
        if (want == GDS_OK) {
            check(fails, i32_cells(in, i32_lo(), q, i32_hi()));
        } else {
            check(fails, i32_cells(in, i32_lo(), x, i32_hi()));
        }
    }
}

/* ---- shift helpers ---- */

void shr_ok(int &fails, uint32_t v, uint32_t c, uint32_t r)
{
    uint32_t val[3] = {u32_lo(), v, u32_hi()};
    uint32_t cnt[3] = {u32_lo(), c, u32_hi()};
    uint32_t out[3] = {u32_lo(), u32_sentinel(), u32_hi()};
    gds_status st = gds_u32_shr(&val[1], &cnt[1], &out[1]);
    check(fails, st == GDS_OK);
    check(fails, u32_cells(val, u32_lo(), v, u32_hi()));
    check(fails, u32_cells(cnt, u32_lo(), c, u32_hi()));
    check(fails, u32_cells(out, u32_lo(), r, u32_hi()));
}

void shr_refused(int &fails, uint32_t v, uint32_t c)
{
    uint32_t val[3] = {u32_lo(), v, u32_hi()};
    uint32_t cnt[3] = {u32_lo(), c, u32_hi()};
    uint32_t out[3] = {u32_lo(), u32_sentinel(), u32_hi()};
    gds_status st = gds_u32_shr(&val[1], &cnt[1], &out[1]);
    check(fails, st == GDS_SHIFT_RANGE);
    check(fails, u32_cells(val, u32_lo(), v, u32_hi()));
    check(fails, u32_cells(cnt, u32_lo(), c, u32_hi()));
    check(fails, u32_cells(out, u32_lo(), u32_sentinel(), u32_hi()));
}

void shr_null_case(int &fails, bool null_v, bool null_c, bool null_out,
                   uint32_t v, uint32_t c, gds_status want)
{
    uint32_t val[3] = {u32_lo(), v, u32_hi()};
    uint32_t cnt[3] = {u32_lo(), c, u32_hi()};
    uint32_t out[3] = {u32_lo(), u32_sentinel(), u32_hi()};
    const uint32_t *pv = null_v ? nullptr : &val[1];
    const uint32_t *pc = null_c ? nullptr : &cnt[1];
    uint32_t *po = null_out ? nullptr : &out[1];
    gds_status st = gds_u32_shr(pv, pc, po);
    check(fails, st == want);
    check(fails, u32_cells(val, u32_lo(), v, u32_hi()));
    check(fails, u32_cells(cnt, u32_lo(), c, u32_hi()));
    check(fails, u32_cells(out, u32_lo(), u32_sentinel(), u32_hi()));
}

/* Value and count share one object; r is ignored on refusal. */
void shr_shared(int &fails, uint32_t x, gds_status want, uint32_t r)
{
    {
        uint32_t in[3] = {u32_lo(), x, u32_hi()};
        uint32_t out[3] = {u32_lo(), u32_sentinel(), u32_hi()};
        gds_status st = gds_u32_shr(&in[1], &in[1], &out[1]);
        check(fails, st == want);
        check(fails, u32_cells(in, u32_lo(), x, u32_hi()));
        if (want == GDS_OK) {
            check(fails, u32_cells(out, u32_lo(), r, u32_hi()));
        } else {
            check(fails, u32_cells(out, u32_lo(), u32_sentinel(), u32_hi()));
        }
    }
    {
        uint32_t in[3] = {u32_lo(), x, u32_hi()};
        gds_status st = gds_u32_shr(&in[1], &in[1], &in[1]);
        check(fails, st == want);
        if (want == GDS_OK) {
            check(fails, u32_cells(in, u32_lo(), r, u32_hi()));
        } else {
            check(fails, u32_cells(in, u32_lo(), x, u32_hi()));
        }
    }
}

/* ---- groups ---- */

void group_status_values(int &fails)
{
    check(fails, static_cast<int>(GDS_OK) == 0);
    check(fails, static_cast<int>(GDS_NULL_INPUT) == 1);
    check(fails, static_cast<int>(GDS_NULL_OUTPUT) == 2);
    check(fails, static_cast<int>(GDS_DIV_ZERO) == 3);
    check(fails, static_cast<int>(GDS_DIV_OVERFLOW) == 4);
    check(fails, static_cast<int>(GDS_SHIFT_RANGE) == 5);
}

void group_null_gates(int &fails)
{
    /* Any null input wins, including over a null output. */
    div_null_case(fails, true, false, false, 7, 3, GDS_NULL_INPUT);
    div_null_case(fails, false, true, false, 7, 3, GDS_NULL_INPUT);
    div_null_case(fails, true, true, false, 7, 3, GDS_NULL_INPUT);
    div_null_case(fails, true, false, true, 7, 3, GDS_NULL_INPUT);
    div_null_case(fails, false, true, true, 7, 3, GDS_NULL_INPUT);
    div_null_case(fails, true, true, true, 7, 3, GDS_NULL_INPUT);
    div_null_case(fails, false, false, true, 7, 3, GDS_NULL_OUTPUT);
    /* Pointer gates precede operand guards. */
    div_null_case(fails, false, false, true, 7, 0, GDS_NULL_OUTPUT);
    div_null_case(fails, false, false, true, INT32_MIN, -1, GDS_NULL_OUTPUT);
    div_null_case(fails, true, false, false, 7, 0, GDS_NULL_INPUT);
    div_null_case(fails, false, true, false, INT32_MIN, -1, GDS_NULL_INPUT);
    div_null_case(fails, true, true, true, INT32_MIN, 0, GDS_NULL_INPUT);

    shr_null_case(fails, true, false, false, 0xF0u, 4u, GDS_NULL_INPUT);
    shr_null_case(fails, false, true, false, 0xF0u, 4u, GDS_NULL_INPUT);
    shr_null_case(fails, true, true, false, 0xF0u, 4u, GDS_NULL_INPUT);
    shr_null_case(fails, true, false, true, 0xF0u, 4u, GDS_NULL_INPUT);
    shr_null_case(fails, false, true, true, 0xF0u, 4u, GDS_NULL_INPUT);
    shr_null_case(fails, true, true, true, 0xF0u, 4u, GDS_NULL_INPUT);
    shr_null_case(fails, false, false, true, 0xF0u, 4u, GDS_NULL_OUTPUT);
    shr_null_case(fails, false, false, true, 0xF0u, 32u, GDS_NULL_OUTPUT);
    shr_null_case(fails, false, false, true, 0xF0u, UINT32_MAX, GDS_NULL_OUTPUT);
    shr_null_case(fails, true, false, false, 0xF0u, 32u, GDS_NULL_INPUT);
    shr_null_case(fails, false, true, true, 0xF0u, UINT32_MAX, GDS_NULL_INPUT);
}

void group_div_literals(int &fails)
{
    /* Sign combinations of 7 and 3: truncation toward zero. */
    div_ok(fails, 7, 3, 2);
    div_ok(fails, -7, 3, -2);
    div_ok(fails, 7, -3, -2);
    div_ok(fails, -7, -3, 2);
    /* Exact multiples. */
    div_ok(fails, 12, 4, 3);
    div_ok(fails, -12, 4, -3);
    div_ok(fails, 12, -4, -3);
    div_ok(fails, -12, -4, 3);
    /* Zero numerator. */
    div_ok(fails, 0, 5, 0);
    div_ok(fails, 0, -5, 0);
    /* Denominators 1 and -1. */
    div_ok(fails, 123, 1, 123);
    div_ok(fails, 123, -1, -123);
    div_ok(fails, -123, -1, 123);
    /* INT32_MAX boundary. */
    div_ok(fails, INT32_MAX, 1, 2147483647);
    div_ok(fails, INT32_MAX, -1, -2147483647);
    div_ok(fails, INT32_MAX, 2, 1073741823);
    div_ok(fails, INT32_MAX, INT32_MAX, 1);
    div_ok(fails, INT32_MAX, INT32_MIN, 0);
    /* INT32_MIN boundary. */
    div_ok(fails, INT32_MIN, 1, INT32_MIN);
    div_ok(fails, INT32_MIN, 2, -1073741824);
    div_ok(fails, INT32_MIN, -2, 1073741824);
    div_ok(fails, INT32_MIN, INT32_MIN, 1);
    div_ok(fails, INT32_MIN, INT32_MAX, -1);
    /* Refusals: zero denominator is checked before overflow. */
    div_refused(fails, 5, 0, GDS_DIV_ZERO);
    div_refused(fails, 0, 0, GDS_DIV_ZERO);
    div_refused(fails, INT32_MIN, 0, GDS_DIV_ZERO);
    div_refused(fails, INT32_MAX, 0, GDS_DIV_ZERO);
    div_refused(fails, INT32_MIN, -1, GDS_DIV_OVERFLOW);
}

void group_div_alias(int &fails)
{
    gds_status st;

    /* Output aliases numerator: 21 / -4 -> -5. */
    {
        int32_t num[3] = {i32_lo(), 21, i32_hi()};
        int32_t den[3] = {i32_lo(), -4, i32_hi()};
        st = gds_i32_div_trunc(&num[1], &den[1], &num[1]);
        check(fails, st == GDS_OK);
        check(fails, i32_cells(num, i32_lo(), -5, i32_hi()));
        check(fails, i32_cells(den, i32_lo(), -4, i32_hi()));
    }
    /* Output aliases denominator: -20 / 6 -> -3. */
    {
        int32_t num[3] = {i32_lo(), -20, i32_hi()};
        int32_t den[3] = {i32_lo(), 6, i32_hi()};
        st = gds_i32_div_trunc(&num[1], &den[1], &den[1]);
        check(fails, st == GDS_OK);
        check(fails, i32_cells(num, i32_lo(), -20, i32_hi()));
        check(fails, i32_cells(den, i32_lo(), -3, i32_hi()));
    }
    /* Shared input, with separate and with aliased output. */
    div_shared(fails, 7, GDS_OK, 1);
    div_shared(fails, -9, GDS_OK, 1);
    div_shared(fails, INT32_MIN, GDS_OK, 1);
    div_shared(fails, -1, GDS_OK, 1);
    div_shared(fails, 0, GDS_DIV_ZERO, 0);

    /* Refusal, output aliases zero denominator: object stays 0. */
    {
        int32_t num[3] = {i32_lo(), 5, i32_hi()};
        int32_t den[3] = {i32_lo(), 0, i32_hi()};
        st = gds_i32_div_trunc(&num[1], &den[1], &den[1]);
        check(fails, st == GDS_DIV_ZERO);
        check(fails, i32_cells(num, i32_lo(), 5, i32_hi()));
        check(fails, i32_cells(den, i32_lo(), 0, i32_hi()));
    }
    /* Refusal, output aliases numerator INT32_MIN with -1: stays INT32_MIN. */
    {
        int32_t num[3] = {i32_lo(), INT32_MIN, i32_hi()};
        int32_t den[3] = {i32_lo(), -1, i32_hi()};
        st = gds_i32_div_trunc(&num[1], &den[1], &num[1]);
        check(fails, st == GDS_DIV_OVERFLOW);
        check(fails, i32_cells(num, i32_lo(), INT32_MIN, i32_hi()));
        check(fails, i32_cells(den, i32_lo(), -1, i32_hi()));
    }
    /* Refusal, output aliases denominator -1: stays -1. */
    {
        int32_t num[3] = {i32_lo(), INT32_MIN, i32_hi()};
        int32_t den[3] = {i32_lo(), -1, i32_hi()};
        st = gds_i32_div_trunc(&num[1], &den[1], &den[1]);
        check(fails, st == GDS_DIV_OVERFLOW);
        check(fails, i32_cells(num, i32_lo(), INT32_MIN, i32_hi()));
        check(fails, i32_cells(den, i32_lo(), -1, i32_hi()));
    }
}

void group_div_matrix(int &fails)
{
    const int32_t ns[7] = {-6, -5, -1, 0, 1, 5, 6};
    const int32_t ds[6] = {-3, -2, -1, 1, 2, 3};
    /* Hand-authored truncated quotients; rows follow ns, columns follow ds. */
    const int32_t qs[7][6] = {
        { 2,  3,  6, -6, -3, -2},
        { 1,  2,  5, -5, -2, -1},
        { 0,  0,  1, -1,  0,  0},
        { 0,  0,  0,  0,  0,  0},
        { 0,  0, -1,  1,  0,  0},
        {-1, -2, -5,  5,  2,  1},
        {-2, -3, -6,  6,  3,  2}
    };
    int i;
    int j;

    for (i = 0; i < 7; ++i) {
        for (j = 0; j < 6; ++j) {
            div_ok(fails, ns[i], ds[j], qs[i][j]);
        }
    }
}

void group_shr_literals(int &fails)
{
    const uint32_t vs[5] = {0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0xA5C3F00Fu};
    const uint32_t cs[5] = {0u, 1u, 15u, 16u, 31u};
    /* Hand-authored results; rows follow vs, columns follow cs. */
    const uint32_t rs[5][5] = {
        {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
        {0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
        {0xFFFFFFFFu, 0x7FFFFFFFu, 0x0001FFFFu, 0x0000FFFFu, 0x00000001u},
        {0x80000000u, 0x40000000u, 0x00010000u, 0x00008000u, 0x00000001u},
        {0xA5C3F00Fu, 0x52E1F807u, 0x00014B87u, 0x0000A5C3u, 0x00000001u}
    };
    int i;
    int j;

    for (i = 0; i < 5; ++i) {
        for (j = 0; j < 5; ++j) {
            shr_ok(fails, vs[i], cs[j], rs[i][j]);
        }
    }

    /* Full-width counts >= 32 are refused, never masked. */
    for (i = 0; i < 5; ++i) {
        shr_refused(fails, vs[i], 32u);
        shr_refused(fails, vs[i], 33u);
        shr_refused(fails, vs[i], 0x80000000u);
        shr_refused(fails, vs[i], UINT32_MAX);
    }
}

void group_shr_alias(int &fails)
{
    gds_status st;

    /* Output aliases value: 0xF0 >> 4 -> 0xF. */
    {
        uint32_t val[3] = {u32_lo(), 0xF0u, u32_hi()};
        uint32_t cnt[3] = {u32_lo(), 4u, u32_hi()};
        st = gds_u32_shr(&val[1], &cnt[1], &val[1]);
        check(fails, st == GDS_OK);
        check(fails, u32_cells(val, u32_lo(), 0xFu, u32_hi()));
        check(fails, u32_cells(cnt, u32_lo(), 4u, u32_hi()));
    }
    /* Output aliases value: 0x80000000 >> 31 -> 1. */
    {
        uint32_t val[3] = {u32_lo(), 0x80000000u, u32_hi()};
        uint32_t cnt[3] = {u32_lo(), 31u, u32_hi()};
        st = gds_u32_shr(&val[1], &cnt[1], &val[1]);
        check(fails, st == GDS_OK);
        check(fails, u32_cells(val, u32_lo(), 1u, u32_hi()));
        check(fails, u32_cells(cnt, u32_lo(), 31u, u32_hi()));
    }
    /* Output aliases count: 0xF0 >> 4 -> 0xF. */
    {
        uint32_t val[3] = {u32_lo(), 0xF0u, u32_hi()};
        uint32_t cnt[3] = {u32_lo(), 4u, u32_hi()};
        st = gds_u32_shr(&val[1], &cnt[1], &cnt[1]);
        check(fails, st == GDS_OK);
        check(fails, u32_cells(val, u32_lo(), 0xF0u, u32_hi()));
        check(fails, u32_cells(cnt, u32_lo(), 0xFu, u32_hi()));
    }
    /* Shared value == count: 0>>0, 1>>1, 31>>31 are 0; 32 is refused. */
    shr_shared(fails, 0u, GDS_OK, 0u);
    shr_shared(fails, 1u, GDS_OK, 0u);
    shr_shared(fails, 31u, GDS_OK, 0u);
    shr_shared(fails, 32u, GDS_SHIFT_RANGE, 0u);

    /* Refusal, output aliases value: value preserved. */
    {
        uint32_t val[3] = {u32_lo(), 0xA5C3F00Fu, u32_hi()};
        uint32_t cnt[3] = {u32_lo(), 32u, u32_hi()};
        st = gds_u32_shr(&val[1], &cnt[1], &val[1]);
        check(fails, st == GDS_SHIFT_RANGE);
        check(fails, u32_cells(val, u32_lo(), 0xA5C3F00Fu, u32_hi()));
        check(fails, u32_cells(cnt, u32_lo(), 32u, u32_hi()));
    }
    /* Refusal, output aliases count 33: count preserved. */
    {
        uint32_t val[3] = {u32_lo(), 0xFFFFFFFFu, u32_hi()};
        uint32_t cnt[3] = {u32_lo(), 33u, u32_hi()};
        st = gds_u32_shr(&val[1], &cnt[1], &cnt[1]);
        check(fails, st == GDS_SHIFT_RANGE);
        check(fails, u32_cells(val, u32_lo(), 0xFFFFFFFFu, u32_hi()));
        check(fails, u32_cells(cnt, u32_lo(), 33u, u32_hi()));
    }
    /* Refusal, output aliases count UINT32_MAX: count preserved. */
    {
        uint32_t val[3] = {u32_lo(), 1u, u32_hi()};
        uint32_t cnt[3] = {u32_lo(), UINT32_MAX, u32_hi()};
        st = gds_u32_shr(&val[1], &cnt[1], &cnt[1]);
        check(fails, st == GDS_SHIFT_RANGE);
        check(fails, u32_cells(val, u32_lo(), 1u, u32_hi()));
        check(fails, u32_cells(cnt, u32_lo(), UINT32_MAX, u32_hi()));
    }
}

void group_state(int &fails)
{
    /* Separate int32 and uint32 guarded objects; no cross-type storage. */
    int32_t dn[3] = {i32_lo(), 100, i32_hi()};
    int32_t dd[3] = {i32_lo(), 7, i32_hi()};
    int32_t dq[3] = {i32_lo(), i32_sentinel(), i32_hi()};
    uint32_t sv[3] = {u32_lo(), 0xF000000Fu, u32_hi()};
    uint32_t sc[3] = {u32_lo(), 4u, u32_hi()};
    uint32_t sr[3] = {u32_lo(), u32_sentinel(), u32_hi()};
    gds_status st;
    int i;

    /* Repeated identical division: 100 / 7 -> 14. */
    for (i = 0; i < 3; ++i) {
        st = gds_i32_div_trunc(&dn[1], &dd[1], &dq[1]);
        check(fails, st == GDS_OK);
        check(fails, i32_cells(dq, i32_lo(), 14, i32_hi()));
    }
    /* An unrelated prior output value has no influence. */
    dq[1] = -777;
    st = gds_i32_div_trunc(&dn[1], &dd[1], &dq[1]);
    check(fails, st == GDS_OK);
    check(fails, i32_cells(dq, i32_lo(), 14, i32_hi()));

    /* Shift success leaves the division output alone: 0xF000000F >> 4. */
    st = gds_u32_shr(&sv[1], &sc[1], &sr[1]);
    check(fails, st == GDS_OK);
    check(fails, u32_cells(sr, u32_lo(), 0x0F000000u, u32_hi()));
    check(fails, i32_cells(dq, i32_lo(), 14, i32_hi()));

    /* Division refusal retains the last division success. */
    dd[1] = 0;
    st = gds_i32_div_trunc(&dn[1], &dd[1], &dq[1]);
    check(fails, st == GDS_DIV_ZERO);
    check(fails, i32_cells(dq, i32_lo(), 14, i32_hi()));
    check(fails, u32_cells(sr, u32_lo(), 0x0F000000u, u32_hi()));

    /* Shift refusal retains the last shift success. */
    sc[1] = 32u;
    st = gds_u32_shr(&sv[1], &sc[1], &sr[1]);
    check(fails, st == GDS_SHIFT_RANGE);
    check(fails, u32_cells(sr, u32_lo(), 0x0F000000u, u32_hi()));
    check(fails, i32_cells(dq, i32_lo(), 14, i32_hi()));

    /* New division operands: -100 / 7 -> -14. */
    dn[1] = -100;
    dd[1] = 7;
    st = gds_i32_div_trunc(&dn[1], &dd[1], &dq[1]);
    check(fails, st == GDS_OK);
    check(fails, i32_cells(dq, i32_lo(), -14, i32_hi()));
    check(fails, u32_cells(sr, u32_lo(), 0x0F000000u, u32_hi()));

    /* Repeated shift with new count: 0xF000000F >> 31 -> 1. */
    sc[1] = 31u;
    for (i = 0; i < 3; ++i) {
        st = gds_u32_shr(&sv[1], &sc[1], &sr[1]);
        check(fails, st == GDS_OK);
        check(fails, u32_cells(sr, u32_lo(), 1u, u32_hi()));
        check(fails, i32_cells(dq, i32_lo(), -14, i32_hi()));
    }

    /* Overflow refusal retains -14. */
    dn[1] = INT32_MIN;
    dd[1] = -1;
    st = gds_i32_div_trunc(&dn[1], &dd[1], &dq[1]);
    check(fails, st == GDS_DIV_OVERFLOW);
    check(fails, i32_cells(dq, i32_lo(), -14, i32_hi()));
    check(fails, u32_cells(sr, u32_lo(), 1u, u32_hi()));

    /* Full-width count refusal retains 1. */
    sc[1] = UINT32_MAX;
    st = gds_u32_shr(&sv[1], &sc[1], &sr[1]);
    check(fails, st == GDS_SHIFT_RANGE);
    check(fails, u32_cells(sr, u32_lo(), 1u, u32_hi()));
    check(fails, i32_cells(dq, i32_lo(), -14, i32_hi()));

    /* Final observations: inputs and canaries intact. */
    check(fails, i32_cells(dn, i32_lo(), INT32_MIN, i32_hi()));
    check(fails, i32_cells(dd, i32_lo(), -1, i32_hi()));
    check(fails, u32_cells(sv, u32_lo(), 0xF000000Fu, u32_hi()));
    check(fails, u32_cells(sc, u32_lo(), UINT32_MAX, u32_hi()));
}

} // namespace

int gds_run_tests(void)
{
    int fails = 0;
    group_status_values(fails);
    group_null_gates(fails);
    group_div_literals(fails);
    group_div_alias(fails);
    group_div_matrix(fails);
    group_shr_literals(fails);
    group_shr_alias(fails);
    group_state(fails);
    return fails;
}
