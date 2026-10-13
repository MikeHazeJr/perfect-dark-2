// test_gameplay_graph_effect1.cpp - authored standalone tests for the
// arity-one gateway.
//
// Exports C-linkage noexcept uint32_t e1_run_tests(void), which returns the
// failure count. main exists only under
// GAMEPLAY_GRAPH_EFFECT1_STANDALONE_TEST_MAIN and returns nonzero iff there
// are failures. No Catch, globals, heap or I/O.
//
// Expected statuses and tags are independent literals: the T1_* macros and
// function-local tables. They are never produced by production validation.
//
// Handoff/gaps: authored only; never compiled or executed, and passing is
// not claimed. Still unqualified:
//   - build and ABI, including union-by-value return across C/C++;
//   - quiet-NaN payload transport (may differ on x87-style targets);
//   - object layout and runtime behavior;
//   - caller integration.
// The binary32 static_assert is a test-only datatype assumption.

#include "gameplay_graph_effect1.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>

#define T1_OK 0u
#define T1_NULL 1u
#define T1_BAD_TYPE 2u
#define T1_TYPE_MISMATCH 3u

#define T1_VOID 0u
#define T1_I32 1u
#define T1_U32 2u
#define T1_F32 3u
#define T1_BOOL32 4u
#define T1_REF 5u

#define T1_IN_PRE 0xC1C1C1C1u
#define T1_IN_MID 0xC2C2C2C2u
#define T1_IN_POST 0xC3C3C3C3u
#define T1_OUT_PRE 0xD1D1D1D1u
#define T1_OUT_POST 0xD2D2D2D2u
#define T1_OLD_TAG 2u
#define T1_OLD_BITS 0x11111111u
#define T1_UNSEEN 0xEEEEEEEEu
#define T1_NO_STATUS 0xFFFFFFFFu

static_assert(sizeof(uint32_t) == 4u, "test-only: uint32_t is four bytes");
static_assert(sizeof(float) == 4u && std::numeric_limits<float>::is_iec559,
              "test-only: float witnesses assume IEEE binary32");
static_assert(sizeof(e1_payload) >= sizeof(uint32_t),
              "test-only: payload holds a 32-bit member");

namespace {

struct T1Rec {
    uint32_t calls;
    void *seen_context;
    const e1_value *seen_args;
    uint32_t seen_arg_type;
    uint32_t seen_arg_bits;
    void *seen_arg_ref;
    e1_payload ret;
    uint32_t observe_via_context;
    const e1_value *observe;
    uint32_t observed;
    uint32_t observed_type;
    uint32_t observed_bits;
};

struct T1InBox {
    uint32_t pre;
    e1_signature sig;
    uint32_t mid;
    e1_value arg[1];
    uint32_t post;
};

struct T1OutBox {
    uint32_t pre;
    e1_value v;
    uint32_t post;
};

struct T1Fix {
    T1InBox in;
    T1OutBox out;
    T1Rec rec;
    unsigned char in_snap[sizeof(T1InBox)];
    unsigned char out_snap[sizeof(T1OutBox)];
};

void t1_expect(uint32_t *fails, bool ok) noexcept
{
    if (!ok) {
        *fails += 1u;
    }
}

// Activates and writes only the member that matches tag. Float and signed
// bits travel by memcpy, never by arithmetic or conversion.
void t1_put_payload(e1_payload *p, uint32_t tag, uint32_t bits, void *ref) noexcept
{
    switch (tag) {
    case T1_I32:
        p->i32 = 0;
        std::memcpy(&p->i32, &bits, sizeof bits);
        break;
    case T1_U32:
        p->u32 = bits;
        break;
    case T1_F32:
        p->f32 = 0.0f;
        std::memcpy(&p->f32, &bits, sizeof bits);
        break;
    case T1_BOOL32:
        p->b32 = 0;
        std::memcpy(&p->b32, &bits, sizeof bits);
        break;
    case T1_REF:
        p->ref = ref;
        break;
    default:
        p->u32 = bits;
        break;
    }
}

// Reads only the member active for tag. A REF value is never dereferenced.
void t1_get_payload(const e1_payload *p, uint32_t tag, uint32_t *bits, void **ref) noexcept
{
    *bits = T1_UNSEEN;
    *ref = NULL;
    switch (tag) {
    case T1_I32:
        std::memcpy(bits, &p->i32, sizeof *bits);
        break;
    case T1_U32:
        *bits = p->u32;
        break;
    case T1_F32:
        std::memcpy(bits, &p->f32, sizeof *bits);
        break;
    case T1_BOOL32:
        std::memcpy(bits, &p->b32, sizeof *bits);
        break;
    case T1_REF:
        *ref = p->ref;
        break;
    default:
        break;
    }
}

e1_payload t1_zero_payload() noexcept
{
    e1_payload p;
    std::memset(&p, 0, sizeof p);
    p.u32 = 0u;
    return p;
}

void t1_note(T1Rec *rec, void *context, const e1_value *args) noexcept
{
    rec->calls += 1u;
    rec->seen_context = context;
    rec->seen_args = args;
    rec->seen_arg_type = args[0].type;
    t1_get_payload(&args[0].payload, args[0].type, &rec->seen_arg_bits, &rec->seen_arg_ref);
    const e1_value *old = rec->observe_via_context != 0u
                              ? static_cast<const e1_value *>(context)
                              : rec->observe;
    if (old != NULL) {
        void *old_ref = NULL;
        rec->observed = 1u;
        rec->observed_type = old->type;
        t1_get_payload(&old->payload, old->type, &rec->observed_bits, &old_ref);
    }
}

} // namespace

// Test callbacks: C language linkage, internal linkage, matching e1_callback.
extern "C" {

static e1_payload t1_record_cb(void *context, const e1_value args[1])
{
    T1Rec *rec = static_cast<T1Rec *>(context);
    if (rec == NULL || args == NULL) {
        return t1_zero_payload();
    }
    t1_note(rec, context, args);
    return rec->ret;
}

// The recorder arrives as the sole valid REF argument. Only the referent is
// mutated; the argument and signature bytes stay unchanged.
static e1_payload t1_ref_record_cb(void *context, const e1_value args[1])
{
    if (args == NULL || args[0].type != T1_REF || args[0].payload.ref == NULL) {
        return t1_zero_payload();
    }
    T1Rec *rec = static_cast<T1Rec *>(args[0].payload.ref);
    t1_note(rec, context, args);
    return rec->ret;
}

} // extern "C"

namespace {

void t1_rec_init(T1Rec *rec) noexcept
{
    std::memset(rec, 0, sizeof *rec);
    rec->calls = 0u;
    rec->seen_context = NULL;
    rec->seen_args = NULL;
    rec->seen_arg_type = T1_UNSEEN;
    rec->seen_arg_bits = T1_UNSEEN;
    rec->seen_arg_ref = NULL;
    t1_put_payload(&rec->ret, T1_U32, 0u, NULL);
    rec->observe_via_context = 0u;
    rec->observe = NULL;
    rec->observed = 0u;
    rec->observed_type = T1_UNSEEN;
    rec->observed_bits = T1_UNSEEN;
}

void t1_snap(T1Fix *f) noexcept
{
    std::memcpy(f->in_snap, &f->in, sizeof f->in);
    std::memcpy(f->out_snap, &f->out, sizeof f->out);
}

// Fully initializes the input/output boxes (memset first), seeds the old
// output as U32 0x11111111, and snapshots both boxes.
void t1_fix_init(T1Fix *f, uint32_t decl, uint32_t res, uint32_t act,
                 uint32_t act_bits, void *act_ref) noexcept
{
    std::memset(&f->in, 0x5A, sizeof f->in);
    f->in.pre = T1_IN_PRE;
    f->in.sig.args[0] = decl;
    f->in.sig.result_type = res;
    f->in.mid = T1_IN_MID;
    f->in.arg[0].type = act;
    t1_put_payload(&f->in.arg[0].payload, act, act_bits, act_ref);
    f->in.post = T1_IN_POST;

    std::memset(&f->out, 0xA5, sizeof f->out);
    f->out.pre = T1_OUT_PRE;
    f->out.v.type = T1_OLD_TAG;
    f->out.v.payload.u32 = T1_OLD_BITS;
    f->out.post = T1_OUT_POST;

    t1_rec_init(&f->rec);
    t1_snap(f);
}

bool t1_in_same(const T1Fix *f) noexcept
{
    return std::memcmp(&f->in, f->in_snap, sizeof f->in) == 0;
}

bool t1_out_same(const T1Fix *f) noexcept
{
    return std::memcmp(&f->out, f->out_snap, sizeof f->out) == 0;
}

bool t1_out_canaries_ok(const T1Fix *f) noexcept
{
    return f->out.pre == T1_OUT_PRE && f->out.post == T1_OUT_POST;
}

void t1_expect_refusal(uint32_t *fails, const T1Fix *f, e1_status st, uint32_t want) noexcept
{
    t1_expect(fails, want != T1_OK);
    t1_expect(fails, st == want);
    t1_expect(fails, f->rec.calls == 0u);
    t1_expect(fails, f->rec.observed == 0u);
    t1_expect(fails, t1_in_same(f));
    t1_expect(fails, t1_out_same(f));
}

// Success checks only the declared tag, active representations, pointers,
// count, canaries and input snapshots. Inactive or padding bytes are never
// compared, except for the untouched-VOID whole-box check.
void t1_expect_success(uint32_t *fails, const T1Fix *f, e1_status st, const void *ctx,
                       uint32_t act, uint32_t act_bits, const void *act_ref,
                       uint32_t res, uint32_t ret_bits, const void *ret_ref) noexcept
{
    t1_expect(fails, st == T1_OK);
    t1_expect(fails, f->rec.calls == 1u);
    t1_expect(fails, f->rec.seen_context == ctx);
    t1_expect(fails, f->rec.seen_args == &f->in.arg[0]);
    t1_expect(fails, f->rec.seen_arg_type == act);
    if (act == T1_REF) {
        t1_expect(fails, f->rec.seen_arg_ref == act_ref);
    } else {
        t1_expect(fails, f->rec.seen_arg_bits == act_bits);
    }
    t1_expect(fails, t1_in_same(f));
    t1_expect(fails, t1_out_canaries_ok(f));
    if (res == T1_VOID) {
        t1_expect(fails, t1_out_same(f));
        return;
    }
    t1_expect(fails, f->out.v.type == res);
    uint32_t bits = T1_UNSEEN;
    void *ref = NULL;
    t1_get_payload(&f->out.v.payload, res, &bits, &ref);
    if (res == T1_REF) {
        t1_expect(fails, ref == ret_ref);
    } else {
        t1_expect(fails, bits == ret_bits);
    }
}

void t1_run_case(uint32_t *fails, uint32_t decl, uint32_t res, uint32_t act,
                 uint32_t act_bits, void *act_ref, uint32_t ret_bits, void *ret_ref,
                 bool out_null, uint32_t want) noexcept
{
    T1Fix f;
    t1_fix_init(&f, decl, res, act, act_bits, act_ref);
    t1_put_payload(&f.rec.ret, res, ret_bits, ret_ref);
    e1_status st = T1_NO_STATUS;
    if (out_null) {
        st = e1_invoke(&f.in.sig, f.in.arg, t1_record_cb, &f.rec, NULL);
    } else {
        st = e1_invoke(&f.in.sig, f.in.arg, t1_record_cb, &f.rec, &f.out.v);
    }
    if (want != T1_OK) {
        t1_expect_refusal(fails, &f, st, want);
        return;
    }
    t1_expect_success(fails, &f, st, &f.rec, act, act_bits, act_ref, res, ret_bits, ret_ref);
}

void t1_test_constants(uint32_t *fails) noexcept
{
    static_assert(std::is_same<e1_status, uint32_t>::value, "e1_status is uint32_t");
    static_assert(std::is_same<decltype(E1_OK), uint32_t>::value, "E1_OK type");
    static_assert(std::is_same<decltype(E1_NULL), uint32_t>::value, "E1_NULL type");
    static_assert(std::is_same<decltype(E1_BAD_TYPE), uint32_t>::value, "E1_BAD_TYPE type");
    static_assert(std::is_same<decltype(E1_TYPE_MISMATCH), uint32_t>::value, "E1_TYPE_MISMATCH type");
    static_assert(std::is_same<decltype(E1_TYPE_VOID), uint32_t>::value, "E1_TYPE_VOID type");
    static_assert(std::is_same<decltype(E1_TYPE_I32), uint32_t>::value, "E1_TYPE_I32 type");
    static_assert(std::is_same<decltype(E1_TYPE_U32), uint32_t>::value, "E1_TYPE_U32 type");
    static_assert(std::is_same<decltype(E1_TYPE_F32), uint32_t>::value, "E1_TYPE_F32 type");
    static_assert(std::is_same<decltype(E1_TYPE_BOOL32), uint32_t>::value, "E1_TYPE_BOOL32 type");
    static_assert(std::is_same<decltype(E1_TYPE_REF), uint32_t>::value, "E1_TYPE_REF type");

    t1_expect(fails, E1_OK == 0u);
    t1_expect(fails, E1_NULL == 1u);
    t1_expect(fails, E1_BAD_TYPE == 2u);
    t1_expect(fails, E1_TYPE_MISMATCH == 3u);
    t1_expect(fails, E1_TYPE_VOID == 0u);
    t1_expect(fails, E1_TYPE_I32 == 1u);
    t1_expect(fails, E1_TYPE_U32 == 2u);
    t1_expect(fails, E1_TYPE_F32 == 3u);
    t1_expect(fails, E1_TYPE_BOOL32 == 4u);
    t1_expect(fails, E1_TYPE_REF == 5u);
}

void t1_test_validate_signature(uint32_t *fails) noexcept
{
    uint32_t arg_tags[10] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 0x80000000u, 0xFFFFFFFFu};
    uint32_t res_tags[8] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 0xFFFFFFFFu};
    uint32_t want[10][8] = {
        /* arg 0          */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
        /* arg 1          */ {0u, 0u, 0u, 0u, 0u, 0u, 2u, 2u},
        /* arg 2          */ {0u, 0u, 0u, 0u, 0u, 0u, 2u, 2u},
        /* arg 3          */ {0u, 0u, 0u, 0u, 0u, 0u, 2u, 2u},
        /* arg 4          */ {0u, 0u, 0u, 0u, 0u, 0u, 2u, 2u},
        /* arg 5          */ {0u, 0u, 0u, 0u, 0u, 0u, 2u, 2u},
        /* arg 6          */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
        /* arg 7          */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
        /* arg 0x80000000 */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
        /* arg 0xFFFFFFFF */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
    };
    for (uint32_t i = 0u; i < 10u; ++i) {
        for (uint32_t j = 0u; j < 8u; ++j) {
            T1Fix f;
            t1_fix_init(&f, arg_tags[i], res_tags[j], 1u, 0x01020304u, NULL);
            t1_expect(fails, e1_validate_signature(&f.in.sig) == want[i][j]);
            t1_expect(fails, t1_in_same(&f));
            t1_expect(fails, t1_out_same(&f));
        }
    }
    t1_expect(fails, e1_validate_signature(NULL) == T1_NULL);
}

void t1_test_validate_arguments(uint32_t *fails) noexcept
{
    uint32_t ref_target = 0x52454631u;
    void *ref_ptr = &ref_target;
    uint32_t decls[8] = {1u, 2u, 3u, 4u, 5u, 0u, 6u, 0xFFFFFFFFu};
    uint32_t acts[8] = {1u, 2u, 3u, 4u, 5u, 0u, 6u, 0xFFFFFFFFu};
    // Result is F32 (3) throughout.
    uint32_t want[8][8] = {
        /* decl 1          */ {0u, 3u, 3u, 3u, 3u, 2u, 2u, 2u},
        /* decl 2          */ {3u, 0u, 3u, 3u, 3u, 2u, 2u, 2u},
        /* decl 3          */ {3u, 3u, 0u, 3u, 3u, 2u, 2u, 2u},
        /* decl 4          */ {3u, 3u, 3u, 0u, 3u, 2u, 2u, 2u},
        /* decl 5          */ {3u, 3u, 3u, 3u, 0u, 2u, 2u, 2u},
        /* decl 0          */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
        /* decl 6          */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
        /* decl 0xFFFFFFFF */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
    };
    uint32_t want_null_out[8][8] = {
        /* decl 1          */ {1u, 3u, 3u, 3u, 3u, 2u, 2u, 2u},
        /* decl 2          */ {3u, 1u, 3u, 3u, 3u, 2u, 2u, 2u},
        /* decl 3          */ {3u, 3u, 1u, 3u, 3u, 2u, 2u, 2u},
        /* decl 4          */ {3u, 3u, 3u, 1u, 3u, 2u, 2u, 2u},
        /* decl 5          */ {3u, 3u, 3u, 3u, 1u, 2u, 2u, 2u},
        /* decl 0          */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
        /* decl 6          */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
        /* decl 0xFFFFFFFF */ {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u},
    };
    for (uint32_t i = 0u; i < 8u; ++i) {
        for (uint32_t j = 0u; j < 8u; ++j) {
            T1Fix f;
            t1_fix_init(&f, decls[i], T1_F32, acts[j], 0x01020304u, ref_ptr);
            t1_expect(fails, e1_validate_arguments(&f.in.sig, f.in.arg) == want[i][j]);
            t1_expect(fails, t1_in_same(&f));
            t1_expect(fails, t1_out_same(&f));
            t1_run_case(fails, decls[i], T1_F32, acts[j], 0x01020304u, ref_ptr,
                        0x3F800000u, NULL, false, want[i][j]);
            t1_run_case(fails, decls[i], T1_F32, acts[j], 0x01020304u, ref_ptr,
                        0x3F800000u, NULL, true, want_null_out[i][j]);
        }
    }

    {
        T1Fix f;
        t1_fix_init(&f, T1_U32, T1_I32, T1_U32, 7u, NULL);
        t1_expect(fails, e1_validate_arguments(NULL, f.in.arg) == T1_NULL);
        t1_expect(fails, e1_validate_arguments(&f.in.sig, NULL) == T1_NULL);
        t1_expect(fails, e1_validate_arguments(NULL, NULL) == T1_NULL);
        t1_expect(fails, t1_in_same(&f));
        t1_expect(fails, t1_out_same(&f));
    }
    {
        // Null args wins over an invalid signature and an invalid actual tag.
        T1Fix f;
        t1_fix_init(&f, 6u, 6u, 0u, 7u, NULL);
        t1_expect(fails, e1_validate_arguments(&f.in.sig, NULL) == T1_NULL);
        t1_expect(fails, e1_validate_arguments(NULL, f.in.arg) == T1_NULL);
        t1_expect(fails, t1_in_same(&f));
    }
}

// Full signature before actual tag: an invalid result tag beats a mismatched
// or invalid actual tag. Output-required is last.
void t1_test_precedence(uint32_t *fails) noexcept
{
    uint32_t ref_target = 0x52454632u;
    void *ref_ptr = &ref_target;
    uint32_t rows[18][7] = {
        /* decl         result       actual       sig  args live nullout */
        {2u,          3u,          2u,          0u, 0u, 0u, 1u},
        {2u,          3u,          1u,          0u, 3u, 3u, 3u},
        {2u,          3u,          0u,          0u, 2u, 2u, 2u},
        {2u,          3u,          6u,          0u, 2u, 2u, 2u},
        {4u,          0u,          4u,          0u, 0u, 0u, 0u},
        {4u,          0u,          5u,          0u, 3u, 3u, 3u},
        {4u,          0u,          0u,          0u, 2u, 2u, 2u},
        {2u,          6u,          2u,          2u, 2u, 2u, 2u},
        {2u,          6u,          1u,          2u, 2u, 2u, 2u},
        {2u,          6u,          0u,          2u, 2u, 2u, 2u},
        {3u,          0xFFFFFFFFu, 4u,          2u, 2u, 2u, 2u},
        {5u,          0xFFFFFFFFu, 0xFFFFFFFFu, 2u, 2u, 2u, 2u},
        {5u,          7u,          5u,          2u, 2u, 2u, 2u},
        {0u,          1u,          1u,          2u, 2u, 2u, 2u},
        {0u,          1u,          0u,          2u, 2u, 2u, 2u},
        {6u,          2u,          6u,          2u, 2u, 2u, 2u},
        {0xFFFFFFFFu, 0xFFFFFFFFu, 3u,          2u, 2u, 2u, 2u},
        {0x80000000u, 6u,          0x80000000u, 2u, 2u, 2u, 2u},
    };
    for (uint32_t i = 0u; i < 18u; ++i) {
        T1Fix f;
        t1_fix_init(&f, rows[i][0], rows[i][1], rows[i][2], 0x01020304u, ref_ptr);
        t1_expect(fails, e1_validate_signature(&f.in.sig) == rows[i][3]);
        t1_expect(fails, e1_validate_arguments(&f.in.sig, f.in.arg) == rows[i][4]);
        t1_expect(fails, t1_in_same(&f));
        t1_expect(fails, t1_out_same(&f));
        t1_run_case(fails, rows[i][0], rows[i][1], rows[i][2], 0x01020304u, ref_ptr,
                    0x3F800000u, NULL, false, rows[i][5]);
        t1_run_case(fails, rows[i][0], rows[i][1], rows[i][2], 0x01020304u, ref_ptr,
                    0x3F800000u, NULL, true, rows[i][6]);
    }
}

// mask bit 1: signature NULL, bit 2: args NULL, bit 4: callback NULL.
e1_status t1_invoke_masked(T1Fix *f, uint32_t mask, bool out_null) noexcept
{
    if (out_null) {
        switch (mask) {
        case 1u: return e1_invoke(NULL, f->in.arg, t1_record_cb, &f->rec, NULL);
        case 2u: return e1_invoke(&f->in.sig, NULL, t1_record_cb, &f->rec, NULL);
        case 3u: return e1_invoke(NULL, NULL, t1_record_cb, &f->rec, NULL);
        case 4u: return e1_invoke(&f->in.sig, f->in.arg, NULL, &f->rec, NULL);
        case 5u: return e1_invoke(NULL, f->in.arg, NULL, &f->rec, NULL);
        case 6u: return e1_invoke(&f->in.sig, NULL, NULL, &f->rec, NULL);
        case 7u: return e1_invoke(NULL, NULL, NULL, &f->rec, NULL);
        default: break;
        }
    } else {
        switch (mask) {
        case 1u: return e1_invoke(NULL, f->in.arg, t1_record_cb, &f->rec, &f->out.v);
        case 2u: return e1_invoke(&f->in.sig, NULL, t1_record_cb, &f->rec, &f->out.v);
        case 3u: return e1_invoke(NULL, NULL, t1_record_cb, &f->rec, &f->out.v);
        case 4u: return e1_invoke(&f->in.sig, f->in.arg, NULL, &f->rec, &f->out.v);
        case 5u: return e1_invoke(NULL, f->in.arg, NULL, &f->rec, &f->out.v);
        case 6u: return e1_invoke(&f->in.sig, NULL, NULL, &f->rec, &f->out.v);
        case 7u: return e1_invoke(NULL, NULL, NULL, &f->rec, &f->out.v);
        default: break;
        }
    }
    return T1_NO_STATUS;
}

void t1_test_invoke_nulls(uint32_t *fails) noexcept
{
    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        for (uint32_t mask = 1u; mask <= 7u; ++mask) {
            for (uint32_t o = 0u; o < 2u; ++o) {
                T1Fix f;
                if (pass == 0u) {
                    t1_fix_init(&f, T1_U32, T1_F32, T1_U32, 0x01020304u, NULL);
                } else {
                    // Invalid tags everywhere: null must still win.
                    t1_fix_init(&f, 6u, 0xFFFFFFFFu, 0u, 0x01020304u, NULL);
                }
                e1_status st = t1_invoke_masked(&f, mask, o != 0u);
                t1_expect_refusal(fails, &f, st, T1_NULL);
            }
        }
    }
}

void t1_test_output_required(uint32_t *fails) noexcept
{
    uint32_t ref_target = 0x52454633u;
    void *ref_ptr = &ref_target;
    uint32_t kinds[5] = {1u, 2u, 3u, 4u, 5u};
    uint32_t want_null_out[5][5] = {
        {1u, 1u, 1u, 1u, 1u},
        {1u, 1u, 1u, 1u, 1u},
        {1u, 1u, 1u, 1u, 1u},
        {1u, 1u, 1u, 1u, 1u},
        {1u, 1u, 1u, 1u, 1u},
    };
    for (uint32_t i = 0u; i < 5u; ++i) {
        for (uint32_t j = 0u; j < 5u; ++j) {
            t1_run_case(fails, kinds[i], kinds[j], kinds[i], 0x01020304u, ref_ptr,
                        0x3F800000u, ref_ptr, true, want_null_out[i][j]);
        }
    }
    {
        // Live probing context, missing output: zero calls.
        T1Fix f;
        t1_fix_init(&f, T1_U32, T1_I32, T1_U32, 2u, NULL);
        e1_status st = e1_invoke(&f.in.sig, f.in.arg, t1_record_cb, &f.rec, NULL);
        t1_expect_refusal(fails, &f, st, T1_NULL);
    }
    {
        // Null context with the recorder as the sole REF; still zero calls.
        T1Fix f;
        t1_fix_init(&f, T1_REF, T1_U32, T1_REF, 0u, &f.rec);
        e1_status st = e1_invoke(&f.in.sig, f.in.arg, t1_ref_record_cb, NULL, NULL);
        t1_expect_refusal(fails, &f, st, T1_NULL);
    }
}

// All five argument kinds x all six result kinds, live and null output.
void t1_test_success_matrix(uint32_t *fails) noexcept
{
    uint32_t ref_a = 0x52454641u;
    uint32_t ref_b = 0x52454642u;
    void *ref_a_ptr = &ref_a;
    void *ref_b_ptr = &ref_b;
    uint32_t rows[30][6] = {
        /* arg arg bits      result ret bits    live nullout */
        {1u, 0x80000000u, 0u, 0x0BADF00Du, 0u, 0u},
        {1u, 0x80000000u, 1u, 0x7FFFFFFFu, 0u, 1u},
        {1u, 0x80000000u, 2u, 0xFFFFFFFEu, 0u, 1u},
        {1u, 0x80000000u, 3u, 0x7FC00001u, 0u, 1u},
        {1u, 0x80000000u, 4u, 0xFFFFFFFFu, 0u, 1u},
        {1u, 0x80000000u, 5u, 0x00000000u, 0u, 1u},
        {2u, 0xDEADBEEFu, 0u, 0x0BADF00Du, 0u, 0u},
        {2u, 0xDEADBEEFu, 1u, 0x7FFFFFFFu, 0u, 1u},
        {2u, 0xDEADBEEFu, 2u, 0xFFFFFFFEu, 0u, 1u},
        {2u, 0xDEADBEEFu, 3u, 0x7FC00001u, 0u, 1u},
        {2u, 0xDEADBEEFu, 4u, 0xFFFFFFFFu, 0u, 1u},
        {2u, 0xDEADBEEFu, 5u, 0x00000000u, 0u, 1u},
        {3u, 0xFF800000u, 0u, 0x0BADF00Du, 0u, 0u},
        {3u, 0xFF800000u, 1u, 0x7FFFFFFFu, 0u, 1u},
        {3u, 0xFF800000u, 2u, 0xFFFFFFFEu, 0u, 1u},
        {3u, 0xFF800000u, 3u, 0x7FC00001u, 0u, 1u},
        {3u, 0xFF800000u, 4u, 0xFFFFFFFFu, 0u, 1u},
        {3u, 0xFF800000u, 5u, 0x00000000u, 0u, 1u},
        {4u, 0x00000100u, 0u, 0x0BADF00Du, 0u, 0u},
        {4u, 0x00000100u, 1u, 0x7FFFFFFFu, 0u, 1u},
        {4u, 0x00000100u, 2u, 0xFFFFFFFEu, 0u, 1u},
        {4u, 0x00000100u, 3u, 0x7FC00001u, 0u, 1u},
        {4u, 0x00000100u, 4u, 0xFFFFFFFFu, 0u, 1u},
        {4u, 0x00000100u, 5u, 0x00000000u, 0u, 1u},
        {5u, 0x00000000u, 0u, 0x0BADF00Du, 0u, 0u},
        {5u, 0x00000000u, 1u, 0x7FFFFFFFu, 0u, 1u},
        {5u, 0x00000000u, 2u, 0xFFFFFFFEu, 0u, 1u},
        {5u, 0x00000000u, 3u, 0x7FC00001u, 0u, 1u},
        {5u, 0x00000000u, 4u, 0xFFFFFFFFu, 0u, 1u},
        {5u, 0x00000000u, 5u, 0x00000000u, 0u, 1u},
    };
    for (uint32_t i = 0u; i < 30u; ++i) {
        void *act_ref = rows[i][0] == T1_REF ? ref_a_ptr : NULL;
        void *ret_ref = rows[i][2] == T1_REF ? ref_b_ptr : NULL;
        t1_run_case(fails, rows[i][0], rows[i][2], rows[i][0], rows[i][1], act_ref,
                    rows[i][3], ret_ref, false, rows[i][4]);
        t1_run_case(fails, rows[i][0], rows[i][2], rows[i][0], rows[i][1], act_ref,
                    rows[i][3], ret_ref, true, rows[i][5]);
    }
}

void t1_test_value_sweeps(uint32_t *fails) noexcept
{
    uint32_t i32s[5] = {0x80000000u, 0xFFFFFFFFu, 0x00000000u, 0x00000001u, 0x7FFFFFFFu};
    uint32_t u32s[5] = {0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
    // BOOL32: 0, 1, -1, 256, INT32_MIN, INT32_MAX.
    uint32_t b32s[6] = {0x00000000u, 0x00000001u, 0xFFFFFFFFu, 0x00000100u,
                        0x80000000u, 0x7FFFFFFFu};
    // Literal binary32 patterns. Every NaN listed is quiet (bit 22 set).
    uint32_t f32s[19] = {
        0x00000000u, /* +0 */
        0x80000000u, /* -0 */
        0x3F800000u, /* +1 */
        0xBF800000u, /* -1 */
        0x7F7FFFFFu, /* +max finite */
        0xFF7FFFFFu, /* -max finite */
        0x00800000u, /* +min normal */
        0x80800000u, /* -min normal */
        0x00000001u, /* +min subnormal */
        0x80000001u, /* -min subnormal */
        0x007FFFFFu, /* +max subnormal */
        0x807FFFFFu, /* -max subnormal */
        0x7F800000u, /* +inf */
        0xFF800000u, /* -inf */
        0x7FC00000u, /* +quiet NaN */
        0xFFC00000u, /* -quiet NaN */
        0x7FC00001u, /* +quiet NaN, payload 1 */
        0xFFFFFFFFu, /* -quiet NaN, full payload */
        0x7FFFFFFFu, /* +quiet NaN, full payload */
    };
    uint32_t ref_target = 0x52454634u;
    void *ref_ptr = &ref_target;

    for (uint32_t i = 0u; i < 5u; ++i) {
        t1_run_case(fails, T1_I32, T1_I32, T1_I32, i32s[i], NULL, i32s[i], NULL, false, T1_OK);
        t1_run_case(fails, T1_U32, T1_U32, T1_U32, u32s[i], NULL, u32s[i], NULL, false, T1_OK);
    }
    for (uint32_t i = 0u; i < 6u; ++i) {
        t1_run_case(fails, T1_BOOL32, T1_BOOL32, T1_BOOL32, b32s[i], NULL, b32s[i], NULL,
                    false, T1_OK);
    }
    for (uint32_t i = 0u; i < 19u; ++i) {
        t1_run_case(fails, T1_F32, T1_F32, T1_F32, f32s[i], NULL, f32s[i], NULL, false, T1_OK);
        // Received-only and stored-only witnesses with differing kinds.
        t1_run_case(fails, T1_F32, T1_U32, T1_F32, f32s[i], NULL, 0x00C0FFEEu, NULL,
                    false, T1_OK);
        t1_run_case(fails, T1_I32, T1_F32, T1_I32, 0x7FFFFFFFu, NULL, f32s[i], NULL,
                    false, T1_OK);
    }
    t1_run_case(fails, T1_REF, T1_REF, T1_REF, 0u, ref_ptr, 0u, ref_ptr, false, T1_OK);
    t1_run_case(fails, T1_REF, T1_REF, T1_REF, 0u, NULL, 0u, NULL, false, T1_OK);
    t1_run_case(fails, T1_REF, T1_REF, T1_REF, 0u, NULL, 0u, ref_ptr, false, T1_OK);
    t1_run_case(fails, T1_REF, T1_REF, T1_REF, 0u, ref_ptr, 0u, NULL, false, T1_OK);
}

// The callback reads the old U32 0x11111111 output during the call. The
// final distinct typed value is then checked.
void t1_run_witness(uint32_t *fails, uint32_t decl, uint32_t act_bits, void *act_ref,
                    uint32_t res, uint32_t ret_bits, void *ret_ref) noexcept
{
    T1Fix f;
    t1_fix_init(&f, decl, res, decl, act_bits, act_ref);
    t1_put_payload(&f.rec.ret, res, ret_bits, ret_ref);
    f.rec.observe = &f.out.v;
    e1_status st = e1_invoke(&f.in.sig, f.in.arg, t1_record_cb, &f.rec, &f.out.v);
    t1_expect_success(fails, &f, st, &f.rec, decl, act_bits, act_ref, res, ret_bits, ret_ref);
    t1_expect(fails, f.rec.observed == 1u);
    t1_expect(fails, f.rec.observed_type == T1_OLD_TAG);
    t1_expect(fails, f.rec.observed_bits == T1_OLD_BITS);
    t1_expect(fails, f.out.v.type != T1_OLD_TAG);
}

void t1_test_old_output(uint32_t *fails) noexcept
{
    uint32_t ref_target = 0x52454635u;
    void *ref_ptr = &ref_target;

    {
        // Output-required refusal happens before any callback observation.
        T1Fix f;
        t1_fix_init(&f, T1_I32, T1_F32, T1_I32, 0x80000000u, NULL);
        t1_put_payload(&f.rec.ret, T1_F32, 0x40490FDBu, NULL);
        f.rec.observe = &f.out.v;
        e1_status st = e1_invoke(&f.in.sig, f.in.arg, t1_record_cb, &f.rec, NULL);
        t1_expect_refusal(fails, &f, st, T1_NULL);
        t1_expect(fails, f.out.v.type == T1_OLD_TAG);
        t1_expect(fails, f.out.v.payload.u32 == T1_OLD_BITS);
    }

    t1_run_witness(fails, T1_I32, 0x80000000u, NULL, T1_F32, 0x40490FDBu, NULL);
    t1_run_witness(fails, T1_BOOL32, 0x00000100u, NULL, T1_I32, 0x80000001u, NULL);
    t1_run_witness(fails, T1_F32, 0x7FC00000u, NULL, T1_REF, 0u, ref_ptr);

    {
        // context == out; the recorder is the sole valid REF argument.
        T1Fix f;
        t1_fix_init(&f, T1_REF, T1_F32, T1_REF, 0u, &f.rec);
        t1_put_payload(&f.rec.ret, T1_F32, 0xC0490FDBu, NULL);
        f.rec.observe_via_context = 1u;
        e1_status st = e1_invoke(&f.in.sig, f.in.arg, t1_ref_record_cb, &f.out.v, &f.out.v);
        t1_expect_success(fails, &f, st, &f.out.v, T1_REF, 0u, &f.rec, T1_F32, 0xC0490FDBu, NULL);
        t1_expect(fails, f.rec.observed == 1u);
        t1_expect(fails, f.rec.observed_type == T1_OLD_TAG);
        t1_expect(fails, f.rec.observed_bits == T1_OLD_BITS);
    }
}

// Null context: the recorder travels as the sole valid REF argument. Only
// the referent changes; the gateway never dereferences it.
void t1_run_null_context(uint32_t *fails, uint32_t res, uint32_t ret_bits, void *ret_ref,
                         bool out_null) noexcept
{
    T1Fix f;
    t1_fix_init(&f, T1_REF, res, T1_REF, 0u, &f.rec);
    t1_put_payload(&f.rec.ret, res, ret_bits, ret_ref);
    e1_status st = T1_NO_STATUS;
    if (out_null) {
        st = e1_invoke(&f.in.sig, f.in.arg, t1_ref_record_cb, NULL, NULL);
    } else {
        st = e1_invoke(&f.in.sig, f.in.arg, t1_ref_record_cb, NULL, &f.out.v);
    }
    t1_expect_success(fails, &f, st, NULL, T1_REF, 0u, &f.rec, res, ret_bits, ret_ref);
}

void t1_test_null_context(uint32_t *fails) noexcept
{
    uint32_t ref_target = 0x52454636u;
    void *ref_ptr = &ref_target;
    t1_run_null_context(fails, T1_VOID, 0x0BADF00Du, NULL, true);
    t1_run_null_context(fails, T1_VOID, 0x0BADF00Du, NULL, false);
    t1_run_null_context(fails, T1_U32, 0x89ABCDEFu, NULL, false);
    t1_run_null_context(fails, T1_REF, 0u, NULL, false);
    t1_run_null_context(fails, T1_REF, 0u, ref_ptr, false);
}

} // namespace

extern "C" uint32_t e1_run_tests(void) noexcept
{
    uint32_t fails = 0u;
    t1_test_constants(&fails);
    t1_test_validate_signature(&fails);
    t1_test_validate_arguments(&fails);
    t1_test_precedence(&fails);
    t1_test_invoke_nulls(&fails);
    t1_test_output_required(&fails);
    t1_test_success_matrix(&fails);
    t1_test_value_sweeps(&fails);
    t1_test_old_output(&fails);
    t1_test_null_context(&fails);
    return fails;
}

#if defined(GAMEPLAY_GRAPH_EFFECT1_STANDALONE_TEST_MAIN)
int main(void)
{
    return e1_run_tests() != 0u ? 1 : 0;
}
#endif
