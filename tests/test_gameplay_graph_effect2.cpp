// Standalone authored tests for the fixed-arity-two typed callback gateway.
// Expected statuses, tags and values are independent literals below; they are
// never derived from production validation, helpers or macros. Production
// constants are only used to drive the API and are checked against the
// independent literals. Authoring only: no compilation, execution, callback
// runtime or ABI qualification is claimed.

#include "gameplay_graph_effect2.h"

#include <cstring>
#include <limits>
#include <stdint.h>
#include <type_traits>

#define E2T_ST_OK 0u
#define E2T_ST_NULL 1u
#define E2T_ST_BAD_TYPE 2u
#define E2T_ST_MISMATCH 3u

#define E2T_VOID 0u
#define E2T_I32 1u
#define E2T_U32 2u
#define E2T_F32 3u
#define E2T_BOOL32 4u
#define E2T_REF 5u

#define E2T_OLD_U32 0xC0FFEE01u
#define E2T_FINAL_I32 0x5A5A1234u
#define E2T_CANARY_PRE 0xA11CE5EDu
#define E2T_CANARY_POST 0x5EED0B0Eu

#define E2T_NSIG 1u
#define E2T_NARGS 2u
#define E2T_NCB 4u
#define E2T_NOUT 8u

#define E2T_CHECK(t, cond) e2t_check((t), (cond) ? true : false, static_cast<uint32_t>(__LINE__))

static_assert(sizeof(uint32_t) == 4u, "test requires 32-bit uint32_t");
static_assert(sizeof(int32_t) == 4u, "test requires 32-bit int32_t");
static_assert(sizeof(float) == 4u, "test requires 32-bit float");
static_assert(std::numeric_limits<float>::is_iec559, "test requires binary32 float");
static_assert(std::is_same<e2_status, uint32_t>::value, "e2_status must be uint32_t");
static_assert(std::is_same<decltype(e2_value::type), uint32_t>::value, "value tag type");
static_assert(std::is_same<decltype(e2_value::payload), e2_payload>::value, "value payload type");
static_assert(std::is_same<decltype(e2_signature::args), uint32_t[2]>::value, "signature args type");
static_assert(std::is_same<decltype(e2_signature::result_type), uint32_t>::value, "signature result type");
static_assert(std::is_same<decltype(e2_payload::i32), int32_t>::value, "payload i32 type");
static_assert(std::is_same<decltype(e2_payload::u32), uint32_t>::value, "payload u32 type");
static_assert(std::is_same<decltype(e2_payload::f32), float>::value, "payload f32 type");
static_assert(std::is_same<decltype(e2_payload::b32), int32_t>::value, "payload b32 type");
static_assert(std::is_same<decltype(e2_payload::ref), void *>::value, "payload ref type");
static_assert(std::is_same<e2_callback, e2_payload (*)(void *, const e2_value *)>::value, "callback type");

extern "C" uint32_t e2_run_tests(void) noexcept;

namespace {

struct Tally {
    uint32_t failures;
    uint32_t first_failure_line;
};

void e2t_check(Tally *t, bool ok, uint32_t line) noexcept
{
    if (!ok) {
        if (t->failures == 0u) {
            t->first_failure_line = line;
        }
        ++t->failures;
    }
}

// Fixture description: tag plus raw 32-bit pattern or borrowed pointer.
struct ValSpec {
    uint32_t tag;
    uint32_t bits;
    void *ref;
};

ValSpec vs(uint32_t tag, uint32_t bits) noexcept
{
    ValSpec s;
    s.tag = tag;
    s.bits = bits;
    s.ref = nullptr;
    return s;
}

ValSpec vr(void *ref) noexcept
{
    ValSpec s;
    s.tag = E2T_REF;
    s.bits = 0u;
    s.ref = ref;
    return s;
}

// Sets the member that is active for the given tag. VOID and invalid tags get
// a safe u32 initialization that the gateway must never interpret.
void set_payload(e2_payload *p, uint32_t tag, uint32_t bits, void *ref) noexcept
{
    switch (tag) {
    case E2T_I32: {
        int32_t v;
        std::memcpy(&v, &bits, sizeof v);
        p->i32 = v;
        break;
    }
    case E2T_F32:
        p->f32 = 0.0f;
        std::memcpy(&p->f32, &bits, sizeof bits);
        break;
    case E2T_BOOL32: {
        int32_t v;
        std::memcpy(&v, &bits, sizeof v);
        p->b32 = v;
        break;
    }
    case E2T_REF:
        p->ref = ref;
        break;
    default:
        p->u32 = bits;
        break;
    }
}

void set_value(e2_value *v, const ValSpec &s) noexcept
{
    std::memset(v, 0xA5, sizeof *v);
    v->type = s.tag;
    set_payload(&v->payload, s.tag, s.bits, s.ref);
}

e2_payload make_payload(const ValSpec &s) noexcept
{
    e2_payload p;
    std::memset(&p, 0x5C, sizeof p);
    set_payload(&p, s.tag, s.bits, s.ref);
    return p;
}

// Compares only the tag and the active typed representation.
bool active_matches(const e2_value *v, const ValSpec &s) noexcept
{
    if (v->type != s.tag) {
        return false;
    }
    switch (s.tag) {
    case E2T_I32: {
        int32_t x = v->payload.i32;
        uint32_t b;
        std::memcpy(&b, &x, sizeof b);
        return b == s.bits;
    }
    case E2T_U32:
        return v->payload.u32 == s.bits;
    case E2T_F32: {
        uint32_t b;
        std::memcpy(&b, &v->payload.f32, sizeof b);
        return b == s.bits;
    }
    case E2T_BOOL32: {
        int32_t x = v->payload.b32;
        uint32_t b;
        std::memcpy(&b, &x, sizeof b);
        return b == s.bits;
    }
    case E2T_REF:
        return v->payload.ref == s.ref;
    default:
        return false;
    }
}

struct SigBox {
    uint32_t pre;
    e2_signature sig;
    uint32_t post;
};

struct ArgBox {
    uint32_t pre;
    e2_value args[2];
    uint32_t post;
};

struct OutBox {
    uint32_t pre;
    e2_value out;
    uint32_t post;
};

void init_sig_box(SigBox *b, uint32_t a0, uint32_t a1, uint32_t r) noexcept
{
    std::memset(b, 0x3C, sizeof *b);
    b->pre = E2T_CANARY_PRE;
    b->sig.args[0] = a0;
    b->sig.args[1] = a1;
    b->sig.result_type = r;
    b->post = E2T_CANARY_POST;
}

void init_arg_box(ArgBox *b, const ValSpec &v0, const ValSpec &v1) noexcept
{
    std::memset(b, 0x4D, sizeof *b);
    b->pre = E2T_CANARY_PRE;
    set_value(&b->args[0], v0);
    set_value(&b->args[1], v1);
    b->post = E2T_CANARY_POST;
}

void init_out_box(OutBox *b) noexcept
{
    std::memset(b, 0x6E, sizeof *b);
    b->pre = E2T_CANARY_PRE;
    b->out.type = E2T_U32;
    b->out.payload.u32 = E2T_OLD_U32;
    b->post = E2T_CANARY_POST;
}

bool out_canaries_ok(const OutBox &b) noexcept
{
    return b.pre == E2T_CANARY_PRE && b.post == E2T_CANARY_POST;
}

template <typename T>
void snap(const T &obj, unsigned char *dst) noexcept
{
    std::memcpy(dst, &obj, sizeof obj);
}

template <typename T>
bool same_bytes(const T &obj, const unsigned char *src) noexcept
{
    return std::memcmp(&obj, src, sizeof obj) == 0;
}

struct Recorder {
    uint32_t calls;
    void *context;
    const e2_value *args;
    e2_value seen[2];
    e2_payload ret;
    uint32_t observed;
    uint32_t old_type;
    uint32_t old_u32;
};

void init_recorder(Recorder *r, const ValSpec &ret) noexcept
{
    std::memset(r, 0, sizeof *r);
    r->context = r;
    r->args = nullptr;
    r->ret = make_payload(ret);
}

void record(Recorder *r, void *context, const e2_value *args) noexcept
{
    ++r->calls;
    r->context = context;
    r->args = args;
    std::memcpy(r->seen, args, sizeof r->seen);
}

e2_payload cb_context_recorder(void *context, const e2_value args[2]) noexcept
{
    Recorder *r = static_cast<Recorder *>(context);
    record(r, context, args);
    return r->ret;
}

e2_payload cb_arg1_recorder(void *context, const e2_value args[2]) noexcept
{
    Recorder *r = static_cast<Recorder *>(args[1].payload.ref);
    record(r, context, args);
    return r->ret;
}

struct OutProbe {
    Recorder *rec;
    e2_value *out;
};

// Context contains a pointer to out; reads the prior initialized output.
e2_payload cb_probe(void *context, const e2_value args[2]) noexcept
{
    OutProbe *p = static_cast<OutProbe *>(context);
    Recorder *r = p->rec;
    record(r, context, args);
    r->old_type = p->out->type;
    if (r->old_type == E2T_U32) {
        r->old_u32 = p->out->payload.u32;
    }
    ++r->observed;
    return r->ret;
}

// Context is out itself; recorder reached through the second REF argument.
e2_payload cb_context_is_out(void *context, const e2_value args[2]) noexcept
{
    Recorder *r = static_cast<Recorder *>(args[1].payload.ref);
    const e2_value *old = static_cast<const e2_value *>(context);
    record(r, context, args);
    r->old_type = old->type;
    if (r->old_type == E2T_U32) {
        r->old_u32 = old->payload.u32;
    }
    ++r->observed;
    return r->ret;
}

void test_schema(Tally *t) noexcept
{
    E2T_CHECK(t, static_cast<uint32_t>(E2_OK) == 0u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_NULL) == 1u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_BAD_TYPE) == 2u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_TYPE_MISMATCH) == 3u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_TYPE_VOID) == 0u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_TYPE_I32) == 1u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_TYPE_U32) == 2u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_TYPE_F32) == 3u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_TYPE_BOOL32) == 4u);
    E2T_CHECK(t, static_cast<uint32_t>(E2_TYPE_REF) == 5u);
}

struct SigCase {
    uint32_t a0;
    uint32_t a1;
    uint32_t r;
    uint32_t expected;
};

void test_validate_signature(Tally *t) noexcept
{
    SigCase cases[] = {
        {1u, 1u, 0u, 0u},
        {2u, 1u, 1u, 0u},
        {3u, 1u, 2u, 0u},
        {4u, 1u, 3u, 0u},
        {5u, 1u, 4u, 0u},
        {1u, 2u, 5u, 0u},
        {1u, 3u, 0u, 0u},
        {1u, 4u, 0u, 0u},
        {1u, 5u, 0u, 0u},
        {5u, 2u, 3u, 0u},
        {3u, 4u, 5u, 0u},
        {0u, 1u, 1u, 2u},
        {6u, 1u, 1u, 2u},
        {0xFFFFFFFFu, 1u, 1u, 2u},
        {1u, 0u, 1u, 2u},
        {1u, 6u, 1u, 2u},
        {1u, 0x80000000u, 1u, 2u},
        {1u, 1u, 6u, 2u},
        {1u, 1u, 0xFFFFFFFFu, 2u},
        {0u, 0u, 0u, 2u},
        {7u, 7u, 7u, 2u},
        {0u, 1u, 6u, 2u},
    };
    for (uint32_t i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        SigBox sb;
        init_sig_box(&sb, cases[i].a0, cases[i].a1, cases[i].r);
        unsigned char ss[sizeof(SigBox)];
        snap(sb, ss);
        e2_status st = e2_validate_signature(&sb.sig);
        E2T_CHECK(t, st == cases[i].expected);
        E2T_CHECK(t, same_bytes(sb, ss));
    }
    E2T_CHECK(t, e2_validate_signature(nullptr) == E2T_ST_NULL);
}

struct ArgCase {
    uint32_t a0;
    uint32_t a1;
    uint32_t r;
    ValSpec v0;
    ValSpec v1;
    uint32_t expected;
};

void test_validate_arguments(Tally *t) noexcept
{
    int32_t live_target = 7;
    ArgCase cases[] = {
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_I32, 0x80000000u), vs(E2T_U32, 0xFFFFFFFFu), E2T_ST_OK},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_VOID, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {E2T_I32, E2T_U32, E2T_VOID, vs(6u, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {E2T_I32, E2T_U32, E2T_VOID, vs(0xFFFFFFFFu, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_I32, 1u), vs(E2T_VOID, 2u), E2T_ST_BAD_TYPE},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_I32, 1u), vs(6u, 2u), E2T_ST_BAD_TYPE},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_I32, 1u), vs(0x80000000u, 2u), E2T_ST_BAD_TYPE},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_U32, 1u), vs(E2T_U32, 2u), E2T_ST_MISMATCH},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_I32, 1u), vs(E2T_I32, 2u), E2T_ST_MISMATCH},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_F32, 0x3F800000u), vs(E2T_U32, 2u), E2T_ST_MISMATCH},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_I32, 1u), vr(nullptr), E2T_ST_MISMATCH},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_U32, 1u), vs(9u, 2u), E2T_ST_MISMATCH},
        {E2T_I32, E2T_U32, E2T_VOID, vs(9u, 1u), vs(E2T_I32, 2u), E2T_ST_BAD_TYPE},
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_BOOL32, 1u), vs(E2T_F32, 0u), E2T_ST_MISMATCH},
        {E2T_F32, E2T_BOOL32, E2T_F32, vs(E2T_F32, 0x7FC00000u), vs(E2T_BOOL32, 0xFFFFFFFFu), E2T_ST_OK},
        {E2T_F32, E2T_BOOL32, E2T_F32, vs(E2T_BOOL32, 1u), vs(E2T_F32, 0u), E2T_ST_MISMATCH},
        {E2T_F32, E2T_BOOL32, E2T_F32, vs(E2T_F32, 0u), vs(E2T_I32, 1u), E2T_ST_MISMATCH},
        {E2T_REF, E2T_REF, E2T_REF, vr(nullptr), vr(&live_target), E2T_ST_OK},
        {E2T_REF, E2T_REF, E2T_REF, vr(&live_target), vs(E2T_U32, 3u), E2T_ST_MISMATCH},
        {E2T_BOOL32, E2T_I32, E2T_U32, vs(E2T_BOOL32, 0x100u), vs(E2T_I32, 0x7FFFFFFFu), E2T_ST_OK},
        {E2T_I32, E2T_U32, 6u, vs(E2T_U32, 1u), vs(E2T_I32, 2u), E2T_ST_BAD_TYPE},
        {E2T_VOID, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {E2T_I32, 0x10u, E2T_I32, vs(9u, 1u), vs(9u, 2u), E2T_ST_BAD_TYPE},
    };
    for (uint32_t i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        SigBox sb;
        ArgBox ab;
        init_sig_box(&sb, cases[i].a0, cases[i].a1, cases[i].r);
        init_arg_box(&ab, cases[i].v0, cases[i].v1);
        unsigned char ss[sizeof(SigBox)];
        unsigned char as[sizeof(ArgBox)];
        snap(sb, ss);
        snap(ab, as);
        e2_status st = e2_validate_arguments(&sb.sig, ab.args);
        E2T_CHECK(t, st == cases[i].expected);
        E2T_CHECK(t, same_bytes(sb, ss));
        E2T_CHECK(t, same_bytes(ab, as));
    }

    SigBox sb;
    ArgBox ab;
    init_sig_box(&sb, E2T_I32, E2T_U32, 6u);
    init_arg_box(&ab, vs(E2T_U32, 1u), vs(E2T_I32, 2u));
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    snap(sb, ss);
    snap(ab, as);
    E2T_CHECK(t, e2_validate_arguments(nullptr, ab.args) == E2T_ST_NULL);
    E2T_CHECK(t, e2_validate_arguments(&sb.sig, nullptr) == E2T_ST_NULL);
    E2T_CHECK(t, e2_validate_arguments(nullptr, nullptr) == E2T_ST_NULL);
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
}

struct RefusalCase {
    uint32_t nulls;
    uint32_t a0;
    uint32_t a1;
    uint32_t r;
    ValSpec v0;
    ValSpec v1;
    uint32_t expected;
};

void run_refusal(Tally *t, const RefusalCase &c) noexcept
{
    Recorder rec;
    init_recorder(&rec, vs(E2T_U32, 0xDEADBEEFu));
    SigBox sb;
    ArgBox ab;
    OutBox ob;
    init_sig_box(&sb, c.a0, c.a1, c.r);
    init_arg_box(&ab, c.v0, c.v1);
    init_out_box(&ob);
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    unsigned char os[sizeof(OutBox)];
    snap(sb, ss);
    snap(ab, as);
    snap(ob, os);
    const e2_signature *sig = (c.nulls & E2T_NSIG) != 0u ? nullptr : &sb.sig;
    const e2_value *args = (c.nulls & E2T_NARGS) != 0u ? nullptr : ab.args;
    e2_callback cb = (c.nulls & E2T_NCB) != 0u ? nullptr : cb_context_recorder;
    e2_value *out = (c.nulls & E2T_NOUT) != 0u ? nullptr : &ob.out;
    e2_status st = e2_invoke(sig, args, cb, &rec, out);
    E2T_CHECK(t, st == c.expected);
    E2T_CHECK(t, rec.calls == 0u);
    E2T_CHECK(t, rec.observed == 0u);
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
    E2T_CHECK(t, same_bytes(ob, os));
}

void test_invoke_refusals(Tally *t) noexcept
{
    int32_t live_target = 13;
    RefusalCase cases[] = {
{E2T_NSIG | E2T_NARGS, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
{E2T_NSIG | E2T_NCB, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
{E2T_NARGS | E2T_NCB, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
{E2T_NSIG | E2T_NARGS | E2T_NCB, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {E2T_NSIG, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {E2T_NARGS, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {E2T_NCB, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {E2T_NSIG | E2T_NARGS | E2T_NCB | E2T_NOUT, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {E2T_NCB, E2T_I32, E2T_U32, 6u, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {E2T_NARGS, E2T_I32, E2T_U32, 6u, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {E2T_NCB, E2T_I32, E2T_U32, E2T_I32, vs(E2T_U32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {0u, E2T_I32, E2T_U32, 6u, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {0u, E2T_VOID, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {0u, E2T_I32, 0xFFFFFFFFu, E2T_I32, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {E2T_NOUT, E2T_I32, E2T_U32, 6u, vs(E2T_I32, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {0u, E2T_I32, E2T_U32, 0x7FFFFFFFu, vs(E2T_U32, 1u), vs(E2T_I32, 2u), E2T_ST_BAD_TYPE},
        {0u, E2T_I32, E2T_U32, E2T_I32, vs(7u, 1u), vs(E2T_U32, 2u), E2T_ST_BAD_TYPE},
        {0u, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(E2T_VOID, 2u), E2T_ST_BAD_TYPE},
        {0u, E2T_I32, E2T_U32, E2T_I32, vs(E2T_BOOL32, 1u), vs(E2T_U32, 2u), E2T_ST_MISMATCH},
        {0u, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vr(&live_target), E2T_ST_MISMATCH},
        {0u, E2T_I32, E2T_U32, E2T_I32, vs(E2T_U32, 1u), vs(8u, 2u), E2T_ST_MISMATCH},
        {0u, E2T_I32, E2T_U32, E2T_I32, vs(8u, 1u), vs(E2T_I32, 2u), E2T_ST_BAD_TYPE},
        {E2T_NOUT, E2T_I32, E2T_U32, E2T_I32, vs(E2T_U32, 1u), vs(E2T_U32, 2u), E2T_ST_MISMATCH},
        {E2T_NOUT, E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, 1u), vs(6u, 2u), E2T_ST_BAD_TYPE},
        {E2T_NOUT, E2T_U32, E2T_I32, E2T_I32, vs(E2T_U32, 1u), vs(E2T_I32, 2u), E2T_ST_NULL},
        {E2T_NOUT, E2T_I32, E2T_BOOL32, E2T_U32, vs(E2T_I32, 1u), vs(E2T_BOOL32, 2u), E2T_ST_NULL},
        {E2T_NOUT, E2T_BOOL32, E2T_U32, E2T_F32, vs(E2T_BOOL32, 1u), vs(E2T_U32, 2u), E2T_ST_NULL},
        {E2T_NOUT, E2T_F32, E2T_I32, E2T_BOOL32, vs(E2T_F32, 0x7F800000u), vs(E2T_I32, 2u), E2T_ST_NULL},
        {E2T_NOUT, E2T_REF, E2T_F32, E2T_REF, vr(&live_target), vs(E2T_F32, 0xFF800000u), E2T_ST_NULL},
    };
    for (uint32_t i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        run_refusal(t, cases[i]);
    }

    // Null-context refusals: recorder reached only through the second REF.
    Recorder rec;
    init_recorder(&rec, vs(E2T_U32, 0xDEADBEEFu));
    SigBox sb;
    ArgBox ab;
    OutBox ob;
    init_sig_box(&sb, E2T_I32, E2T_REF, E2T_I32);
    init_arg_box(&ab, vs(E2T_U32, 1u), vr(&rec));
    init_out_box(&ob);
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    unsigned char os[sizeof(OutBox)];
    snap(sb, ss);
    snap(ab, as);
    snap(ob, os);
    E2T_CHECK(t, e2_invoke(&sb.sig, ab.args, cb_arg1_recorder, nullptr, &ob.out) == E2T_ST_MISMATCH);
    E2T_CHECK(t, rec.calls == 0u);
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
    E2T_CHECK(t, same_bytes(ob, os));

    SigBox sb2;
    ArgBox ab2;
    init_sig_box(&sb2, E2T_I32, E2T_REF, E2T_I32);
    init_arg_box(&ab2, vs(E2T_I32, 1u), vr(&rec));
    unsigned char ss2[sizeof(SigBox)];
    unsigned char as2[sizeof(ArgBox)];
    snap(sb2, ss2);
    snap(ab2, as2);
    E2T_CHECK(t, e2_invoke(&sb2.sig, ab2.args, cb_arg1_recorder, nullptr, nullptr) == E2T_ST_NULL);
    E2T_CHECK(t, rec.calls == 0u);
    E2T_CHECK(t, same_bytes(sb2, ss2));
    E2T_CHECK(t, same_bytes(ab2, as2));
    E2T_CHECK(t, same_bytes(ob, os));
}

struct SuccessCase {
    uint32_t a0;
    uint32_t a1;
    uint32_t r;
    ValSpec v0;
    ValSpec v1;
    ValSpec ret;
};

void run_success(Tally *t, const SuccessCase &c, bool pass_out) noexcept
{
    Recorder rec;
    init_recorder(&rec, c.ret);
    SigBox sb;
    ArgBox ab;
    OutBox ob;
    init_sig_box(&sb, c.a0, c.a1, c.r);
    init_arg_box(&ab, c.v0, c.v1);
    init_out_box(&ob);
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    unsigned char os[sizeof(OutBox)];
    snap(sb, ss);
    snap(ab, as);
    snap(ob, os);
    e2_status st = e2_invoke(&sb.sig, ab.args, cb_context_recorder, &rec, pass_out ? &ob.out : nullptr);
    E2T_CHECK(t, st == E2T_ST_OK);
    E2T_CHECK(t, rec.calls == 1u);
    E2T_CHECK(t, rec.context == &rec);
    E2T_CHECK(t, rec.args == ab.args);
    E2T_CHECK(t, active_matches(&rec.seen[0], c.v0));
    E2T_CHECK(t, active_matches(&rec.seen[1], c.v1));
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
    if (c.r == E2T_VOID || !pass_out) {
        E2T_CHECK(t, same_bytes(ob, os));
    } else {
        E2T_CHECK(t, out_canaries_ok(ob));
        E2T_CHECK(t, active_matches(&ob.out, c.ret));
    }
}

void test_invoke_success(Tally *t) noexcept
{
    int32_t target_a = 11;
    int32_t target_b = 22;
    SuccessCase cases[] = {
        {E2T_I32, E2T_U32, E2T_VOID, vs(E2T_I32, 0x80000000u), vs(E2T_U32, 0xFFFFFFFFu), vs(E2T_U32, 1u)},
        {E2T_U32, E2T_I32, E2T_I32, vs(E2T_U32, 0u), vs(E2T_I32, 0x7FFFFFFFu), vs(E2T_I32, 0x80000000u)},
        {E2T_U32, E2T_I32, E2T_I32, vs(E2T_U32, 0xFFFFFFFFu), vs(E2T_I32, 0x80000000u), vs(E2T_I32, 0xFFFFFFFFu)},
        {E2T_BOOL32, E2T_REF, E2T_U32, vs(E2T_BOOL32, 0x80000000u), vr(&target_a), vs(E2T_U32, 0xFFFFFFFFu)},
        {E2T_REF, E2T_I32, E2T_F32, vr(nullptr), vs(E2T_I32, 0u), vs(E2T_F32, 0xFF800000u)},
        {E2T_F32, E2T_U32, E2T_BOOL32, vs(E2T_F32, 0x80000000u), vs(E2T_U32, 0x80000000u), vs(E2T_BOOL32, 0x100u)},
        {E2T_I32, E2T_F32, E2T_REF, vs(E2T_I32, 0xFFFFFFFFu), vs(E2T_F32, 0x00000001u), vr(&target_b)},
        {E2T_I32, E2T_F32, E2T_REF, vs(E2T_I32, 1u), vs(E2T_F32, 0x7F7FFFFFu), vr(nullptr)},
        {E2T_REF, E2T_REF, E2T_REF, vr(&target_a), vr(nullptr), vr(&target_b)},
        {E2T_I32, E2T_I32, E2T_I32, vs(E2T_I32, 0x80000000u), vs(E2T_I32, 0x7FFFFFFFu), vs(E2T_I32, 0x7FFFFFFFu)},
        {E2T_BOOL32, E2T_F32, E2T_VOID, vs(E2T_BOOL32, 0xFFFFFFFFu), vs(E2T_F32, 0xFFC00000u), vs(E2T_I32, 5u)},
    };
    for (uint32_t i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        run_success(t, cases[i], true);
    }
    run_success(t, cases[0], false);
    run_success(t, cases[10], false);

    uint32_t bools[] = {0u, 1u, 0xFFFFFFFFu, 0x100u, 0x7FFFFFFFu, 0x80000000u};
    uint32_t nb = static_cast<uint32_t>(sizeof bools / sizeof bools[0]);
    for (uint32_t i = 0u; i < nb; ++i) {
        SuccessCase a = {E2T_BOOL32, E2T_I32, E2T_BOOL32, vs(E2T_BOOL32, bools[i]), vs(E2T_I32, bools[i]),
                         vs(E2T_BOOL32, bools[nb - 1u - i])};
        run_success(t, a, true);
        SuccessCase b = {E2T_U32, E2T_BOOL32, E2T_BOOL32, vs(E2T_U32, bools[i]), vs(E2T_BOOL32, bools[i]),
                         vs(E2T_BOOL32, bools[i])};
        run_success(t, b, true);
    }

    uint32_t ints[] = {0x80000000u, 0x7FFFFFFFu, 0u, 0xFFFFFFFFu, 1u};
    uint32_t uints[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
    uint32_t ni = static_cast<uint32_t>(sizeof ints / sizeof ints[0]);
    for (uint32_t i = 0u; i < ni; ++i) {
        SuccessCase a = {E2T_I32, E2T_U32, E2T_I32, vs(E2T_I32, ints[i]), vs(E2T_U32, uints[i]),
                         vs(E2T_I32, ints[ni - 1u - i])};
        run_success(t, a, true);
        SuccessCase b = {E2T_U32, E2T_I32, E2T_U32, vs(E2T_U32, uints[i]), vs(E2T_I32, ints[i]),
                         vs(E2T_U32, uints[ni - 1u - i])};
        run_success(t, b, true);
    }

    uint32_t floats[] = {
        0x00000000u, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu,
        0x00800000u, 0x80800000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x7F800000u,
        0xFF800000u, 0x7FC00000u, 0xFFC00000u, 0x7FC00001u, 0x7FD5A5A5u, 0xFFFFFFFFu,
    };
    uint32_t nf = static_cast<uint32_t>(sizeof floats / sizeof floats[0]);
    for (uint32_t i = 0u; i < nf; ++i) {
        SuccessCase a = {E2T_F32, E2T_BOOL32, E2T_F32, vs(E2T_F32, floats[i]), vs(E2T_BOOL32, i),
                         vs(E2T_F32, floats[nf - 1u - i])};
        run_success(t, a, true);
        SuccessCase b = {E2T_REF, E2T_F32, E2T_F32, vr((i & 1u) != 0u ? &target_a : nullptr),
                         vs(E2T_F32, floats[i]), vs(E2T_F32, floats[i])};
        run_success(t, b, true);
    }
}

void run_null_context_success(Tally *t, uint32_t r, bool pass_out) noexcept
{
    Recorder rec;
    ValSpec ret = r == E2T_VOID ? vs(E2T_I32, 0x01010101u) : vs(E2T_U32, 0xFFFFFFFFu);
    init_recorder(&rec, ret);
    SigBox sb;
    ArgBox ab;
    OutBox ob;
    ValSpec v0 = vs(E2T_F32, 0x7FC00001u);
    ValSpec v1 = vr(&rec);
    init_sig_box(&sb, E2T_F32, E2T_REF, r);
    init_arg_box(&ab, v0, v1);
    init_out_box(&ob);
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    unsigned char os[sizeof(OutBox)];
    snap(sb, ss);
    snap(ab, as);
    snap(ob, os);
    e2_status st = e2_invoke(&sb.sig, ab.args, cb_arg1_recorder, nullptr, pass_out ? &ob.out : nullptr);
    E2T_CHECK(t, st == E2T_ST_OK);
    E2T_CHECK(t, rec.calls == 1u);
    E2T_CHECK(t, rec.context == nullptr);
    E2T_CHECK(t, rec.args == ab.args);
    E2T_CHECK(t, active_matches(&rec.seen[0], v0));
    E2T_CHECK(t, active_matches(&rec.seen[1], v1));
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
    if (r == E2T_VOID || !pass_out) {
        E2T_CHECK(t, same_bytes(ob, os));
    } else {
        E2T_CHECK(t, out_canaries_ok(ob));
        E2T_CHECK(t, active_matches(&ob.out, ret));
    }
}

void test_null_context_success(Tally *t) noexcept
{
    run_null_context_success(t, E2T_U32, true);
    run_null_context_success(t, E2T_VOID, true);
    run_null_context_success(t, E2T_VOID, false);
}

// Witness 1: context contains a pointer to out; distinct I32 final.
void test_old_output_probe_i32(Tally *t) noexcept
{
    Recorder rec;
    ValSpec ret = vs(E2T_I32, E2T_FINAL_I32);
    init_recorder(&rec, ret);
    SigBox sb;
    ArgBox ab;
    OutBox ob;
    ValSpec v0 = vs(E2T_U32, 0x01020304u);
    ValSpec v1 = vs(E2T_BOOL32, 0xFFFFFFFFu);
    init_sig_box(&sb, E2T_U32, E2T_BOOL32, E2T_I32);
    init_arg_box(&ab, v0, v1);
    init_out_box(&ob);
    OutProbe probe;
    probe.rec = &rec;
    probe.out = &ob.out;
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    snap(sb, ss);
    snap(ab, as);
    e2_status st = e2_invoke(&sb.sig, ab.args, cb_probe, &probe, &ob.out);
    E2T_CHECK(t, st == E2T_ST_OK);
    E2T_CHECK(t, rec.calls == 1u);
    E2T_CHECK(t, rec.context == &probe);
    E2T_CHECK(t, rec.args == ab.args);
    E2T_CHECK(t, active_matches(&rec.seen[0], v0));
    E2T_CHECK(t, active_matches(&rec.seen[1], v1));
    E2T_CHECK(t, rec.observed == 1u);
    E2T_CHECK(t, rec.old_type == E2T_U32);
    E2T_CHECK(t, rec.old_u32 == E2T_OLD_U32);
    E2T_CHECK(t, probe.rec == &rec);
    E2T_CHECK(t, probe.out == &ob.out);
    E2T_CHECK(t, out_canaries_ok(ob));
    E2T_CHECK(t, active_matches(&ob.out, ret));
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
}

// Witness 2: context IS out; recorder reached through a valid second REF.
void test_old_output_context_is_out(Tally *t) noexcept
{
    Recorder rec;
    ValSpec ret = vs(E2T_I32, E2T_FINAL_I32);
    init_recorder(&rec, ret);
    SigBox sb;
    ArgBox ab;
    OutBox ob;
    ValSpec v0 = vs(E2T_I32, 0x7FFFFFFFu);
    ValSpec v1 = vr(&rec);
    init_sig_box(&sb, E2T_I32, E2T_REF, E2T_I32);
    init_arg_box(&ab, v0, v1);
    init_out_box(&ob);
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    snap(sb, ss);
    snap(ab, as);
    e2_status st = e2_invoke(&sb.sig, ab.args, cb_context_is_out, &ob.out, &ob.out);
    E2T_CHECK(t, st == E2T_ST_OK);
    E2T_CHECK(t, rec.calls == 1u);
    E2T_CHECK(t, rec.context == &ob.out);
    E2T_CHECK(t, rec.args == ab.args);
    E2T_CHECK(t, active_matches(&rec.seen[0], v0));
    E2T_CHECK(t, active_matches(&rec.seen[1], v1));
    E2T_CHECK(t, rec.observed == 1u);
    E2T_CHECK(t, rec.old_type == E2T_U32);
    E2T_CHECK(t, rec.old_u32 == E2T_OLD_U32);
    E2T_CHECK(t, out_canaries_ok(ob));
    E2T_CHECK(t, active_matches(&ob.out, ret));
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
}

// Witness 3: context contains a pointer to out; distinct REF final.
void test_old_output_probe_ref(Tally *t) noexcept
{
    int32_t marker = 33;
    Recorder rec;
    ValSpec ret = vr(&marker);
    init_recorder(&rec, ret);
    SigBox sb;
    ArgBox ab;
    OutBox ob;
    ValSpec v0 = vr(nullptr);
    ValSpec v1 = vs(E2T_F32, 0x7FC00000u);
    init_sig_box(&sb, E2T_REF, E2T_F32, E2T_REF);
    init_arg_box(&ab, v0, v1);
    init_out_box(&ob);
    OutProbe probe;
    probe.rec = &rec;
    probe.out = &ob.out;
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    snap(sb, ss);
    snap(ab, as);
    e2_status st = e2_invoke(&sb.sig, ab.args, cb_probe, &probe, &ob.out);
    E2T_CHECK(t, st == E2T_ST_OK);
    E2T_CHECK(t, rec.calls == 1u);
    E2T_CHECK(t, rec.context == &probe);
    E2T_CHECK(t, rec.args == ab.args);
    E2T_CHECK(t, active_matches(&rec.seen[0], v0));
    E2T_CHECK(t, active_matches(&rec.seen[1], v1));
    E2T_CHECK(t, rec.observed == 1u);
    E2T_CHECK(t, rec.old_type == E2T_U32);
    E2T_CHECK(t, rec.old_u32 == E2T_OLD_U32);
    E2T_CHECK(t, probe.rec == &rec);
    E2T_CHECK(t, probe.out == &ob.out);
    E2T_CHECK(t, out_canaries_ok(ob));
    E2T_CHECK(t, active_matches(&ob.out, ret));
    E2T_CHECK(t, marker == 33);
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
}

// Missing non-VOID output refusal with a valid live probing context.
void test_missing_output_refusal(Tally *t) noexcept
{
    Recorder rec;
    init_recorder(&rec, vs(E2T_I32, E2T_FINAL_I32));
    SigBox sb;
    ArgBox ab;
    OutBox ob;
    init_sig_box(&sb, E2T_U32, E2T_BOOL32, E2T_I32);
    init_arg_box(&ab, vs(E2T_U32, 0x01020304u), vs(E2T_BOOL32, 0u));
    init_out_box(&ob);
    OutProbe probe;
    probe.rec = &rec;
    probe.out = &ob.out;
    unsigned char ss[sizeof(SigBox)];
    unsigned char as[sizeof(ArgBox)];
    unsigned char os[sizeof(OutBox)];
    snap(sb, ss);
    snap(ab, as);
    snap(ob, os);
    e2_status st = e2_invoke(&sb.sig, ab.args, cb_probe, &probe, nullptr);
    E2T_CHECK(t, st == E2T_ST_NULL);
    E2T_CHECK(t, rec.calls == 0u);
    E2T_CHECK(t, rec.observed == 0u);
    E2T_CHECK(t, probe.rec == &rec);
    E2T_CHECK(t, probe.out == &ob.out);
    E2T_CHECK(t, same_bytes(sb, ss));
    E2T_CHECK(t, same_bytes(ab, as));
    E2T_CHECK(t, same_bytes(ob, os));
}

} // namespace

extern "C" uint32_t e2_run_tests(void) noexcept
{
    Tally t;
    t.failures = 0u;
    t.first_failure_line = 0u;
    test_schema(&t);
    test_validate_signature(&t);
    test_validate_arguments(&t);
    test_invoke_refusals(&t);
    test_invoke_success(&t);
    test_null_context_success(&t);
    test_old_output_probe_i32(&t);
    test_old_output_context_is_out(&t);
    test_old_output_probe_ref(&t);
    test_missing_output_refusal(&t);
    return t.failures;
}

#if defined(GAMEPLAY_GRAPH_EFFECT2_STANDALONE_TEST_MAIN)
int main(void)
{
    return e2_run_tests() != 0u ? 1 : 0;
}
#endif
