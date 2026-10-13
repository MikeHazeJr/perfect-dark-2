// test_gameplay_graph_effect3.cpp
//
// Standalone authored tests for the fixed-arity-three callback gateway.
//
// Exports and build:
//   - Exports C-linkage, noexcept uint32_t e3_run_tests(void), which returns
//     the failure count.
//   - main exists only under GAMEPLAY_GRAPH_EFFECT3_STANDALONE_TEST_MAIN and
//     returns nonzero iff there are failures.
//   - There is no Catch dependency or registration.
//
// Handoff:
//   - Authored only. Not compiled or executed, so no pass is claimed.
//   - Still untested: the actual ABI, struct/union layout, union-by-value
//     return, NaN transport and calling convention.
//
// Test rules:
//   - All tables are function-local automatic objects. There are no
//     global, static or thread-local objects.
//   - No allocation, I/O, clock, RNG or environment changes.
//   - Expected tags and statuses are literal E3T_* constants. They are
//     independent of production validation; the production macros are
//     checked against them separately.
//
// Test-only assumptions:
//   - float is IEEE-754 binary32 and uint32_t is 4 bytes (asserted below).
//   - Only quiet NaNs are used. Float bits are built and observed through
//     memcpy only, with no float arithmetic or coercion.
//
// Probe convention (test bookkeeping only, not gateway behavior):
//   - Recorders normally reach their probe through a nonnull context.
//   - The dedicated null-context recorder reaches its live local probe
//     through the VALID third REF argument, used directly as a borrowed
//     pointer.
//   - Refusal and null-combination tests always use a nonnull context and
//     initialized arguments.

#include "gameplay_graph_effect3.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

static_assert(sizeof(uint32_t) == 4u, "test assumes 4-byte uint32_t");
static_assert(sizeof(float) == 4u, "test assumes 4-byte float");
static_assert(std::numeric_limits<float>::is_iec559, "test assumes IEEE-754 binary32 float");
static_assert(std::is_same<e3_status, uint32_t>::value, "e3_status must be uint32_t");
static_assert(std::is_same<decltype(E3_OK), uint32_t>::value, "E3_OK must be uint32_t");
static_assert(std::is_same<decltype(E3_NULL), uint32_t>::value, "E3_NULL must be uint32_t");
static_assert(std::is_same<decltype(E3_BAD_TYPE), uint32_t>::value, "E3_BAD_TYPE must be uint32_t");
static_assert(std::is_same<decltype(E3_TYPE_MISMATCH), uint32_t>::value, "E3_TYPE_MISMATCH must be uint32_t");
static_assert(std::is_same<decltype(E3_TYPE_VOID), uint32_t>::value, "E3_TYPE_VOID must be uint32_t");
static_assert(std::is_same<decltype(E3_TYPE_I32), uint32_t>::value, "E3_TYPE_I32 must be uint32_t");
static_assert(std::is_same<decltype(E3_TYPE_U32), uint32_t>::value, "E3_TYPE_U32 must be uint32_t");
static_assert(std::is_same<decltype(E3_TYPE_F32), uint32_t>::value, "E3_TYPE_F32 must be uint32_t");
static_assert(std::is_same<decltype(E3_TYPE_BOOL32), uint32_t>::value, "E3_TYPE_BOOL32 must be uint32_t");
static_assert(std::is_same<decltype(E3_TYPE_REF), uint32_t>::value, "E3_TYPE_REF must be uint32_t");

// Independent literal expectations.
#define E3T_OK       0u
#define E3T_NULL     1u
#define E3T_BAD      2u
#define E3T_MISMATCH 3u

#define E3T_VOID   0u
#define E3T_I32    1u
#define E3T_U32    2u
#define E3T_F32    3u
#define E3T_BOOL32 4u
#define E3T_REF    5u
#define E3T_BAD6   6u
#define E3T_BADHI  0x80000000u
#define E3T_BADMAX 0xFFFFFFFFu

// Null-selection mask bits for refusal rows.
#define E3T_NULL_SIG  1u
#define E3T_NULL_ARGS 2u
#define E3T_NULL_CB   4u
#define E3T_NULL_OUT  8u

#define E3T_CHECK(failures, condition) \
    do { if (!(condition)) { (failures) += 1u; } } while (0)

extern "C" uint32_t e3_run_tests(void) noexcept;

struct e3t_probe {
    uint32_t calls;
    unsigned char marker;
    void *seen_context;
    const e3_value *seen_args;
    e3_value seen_copy[3];
    e3_payload ret;
};

struct e3t_guarded {
    unsigned char pre[16];
    e3_value out;
    unsigned char post[16];
};

struct e3t_observed {
    e3_value received[3];
    e3_value stored;
};

struct e3t_refusal_row {
    uint32_t s0, s1, s2, sr;
    uint32_t t0, t1, t2;
    uint32_t null_mask;
    uint32_t expected;
};

static void e3t_probe_init(e3t_probe *probe)
{
    std::memset(probe, 0, sizeof *probe);
    probe->calls = 0u;
    probe->marker = 0u;
    // Sentinel distinct from null and from the probe address itself.
    probe->seen_context = &probe->marker;
    probe->seen_args = nullptr;
    probe->ret.u32 = 0u;
}

static e3_payload e3t_record(e3t_probe *probe, void *context, const e3_value *args)
{
    probe->calls += 1u;
    probe->seen_context = context;
    probe->seen_args = args;
    std::memcpy(probe->seen_copy, args, sizeof probe->seen_copy);
    return probe->ret;
}

extern "C" {

// Normal recorder: reaches the probe through the nonnull context.
static e3_payload e3t_context_callback(void *context, const e3_value args[3])
{
    return e3t_record(static_cast<e3t_probe *>(context), context, args);
}

// Null-context recorder: reaches the live local probe through the VALID
// third REF argument, used directly as a borrowed pointer.
static e3_payload e3t_ref_probe_callback(void *context, const e3_value args[3])
{
    return e3t_record(static_cast<e3t_probe *>(args[2].payload.ref), context, args);
}

}

static void e3t_value_init(e3_value *value)
{
    std::memset(value, 0x5A, sizeof *value);
}

static void e3t_set_i32(e3_value *value, int32_t x)
{
    e3t_value_init(value);
    value->type = E3T_I32;
    value->payload.i32 = x;
}

static void e3t_set_u32(e3_value *value, uint32_t x)
{
    e3t_value_init(value);
    value->type = E3T_U32;
    value->payload.u32 = x;
}

static void e3t_set_bool32(e3_value *value, int32_t x)
{
    e3t_value_init(value);
    value->type = E3T_BOOL32;
    value->payload.b32 = x;
}

static void e3t_set_f32_bits(e3_value *value, uint32_t bits)
{
    e3t_value_init(value);
    value->type = E3T_F32;
    std::memcpy(&value->payload.f32, &bits, sizeof bits);
}

static void e3t_set_ref(e3_value *value, void *ref)
{
    e3t_value_init(value);
    value->type = E3T_REF;
    value->payload.ref = ref;
}

// Unsupported tag with an initialized payload. Never used as a probe carrier.
static void e3t_set_raw_tag(e3_value *value, uint32_t tag)
{
    e3t_value_init(value);
    value->type = tag;
    value->payload.u32 = 0xC0DEC0DEu;
}

static void e3t_set_default(e3_value *value, uint32_t tag)
{
    switch (tag) {
    case E3T_I32: e3t_set_i32(value, -7); break;
    case E3T_U32: e3t_set_u32(value, 7u); break;
    case E3T_F32: e3t_set_f32_bits(value, 0x3F800000u); break;
    case E3T_BOOL32: e3t_set_bool32(value, 1); break;
    case E3T_REF: e3t_set_ref(value, nullptr); break;
    default: e3t_set_raw_tag(value, tag); break;
    }
}

static void e3t_signature(e3_signature *signature, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t result)
{
    std::memset(signature, 0x5A, sizeof *signature);
    signature->args[0] = a0;
    signature->args[1] = a1;
    signature->args[2] = a2;
    signature->result_type = result;
}

static e3_payload e3t_payload_i32(int32_t x)
{
    e3_payload payload;
    std::memset(&payload, 0x3C, sizeof payload);
    payload.i32 = x;
    return payload;
}

static e3_payload e3t_payload_u32(uint32_t x)
{
    e3_payload payload;
    std::memset(&payload, 0x3C, sizeof payload);
    payload.u32 = x;
    return payload;
}

static e3_payload e3t_payload_bool32(int32_t x)
{
    e3_payload payload;
    std::memset(&payload, 0x3C, sizeof payload);
    payload.b32 = x;
    return payload;
}

static e3_payload e3t_payload_f32_bits(uint32_t bits)
{
    e3_payload payload;
    std::memset(&payload, 0x3C, sizeof payload);
    std::memcpy(&payload.f32, &bits, sizeof bits);
    return payload;
}

static e3_payload e3t_payload_ref(void *ref)
{
    e3_payload payload;
    std::memset(&payload, 0x3C, sizeof payload);
    payload.ref = ref;
    return payload;
}

static uint32_t e3t_f32_bits(const e3_payload &payload)
{
    uint32_t bits;
    std::memcpy(&bits, &payload.f32, sizeof bits);
    return bits;
}

// Compares only the active member selected by tag; never padding.
static bool e3t_payload_active_equal(uint32_t tag, const e3_payload &a, const e3_payload &b)
{
    switch (tag) {
    case E3T_I32: return std::memcmp(&a.i32, &b.i32, sizeof a.i32) == 0;
    case E3T_U32: return std::memcmp(&a.u32, &b.u32, sizeof a.u32) == 0;
    case E3T_F32: return std::memcmp(&a.f32, &b.f32, sizeof a.f32) == 0;
    case E3T_BOOL32: return std::memcmp(&a.b32, &b.b32, sizeof a.b32) == 0;
    case E3T_REF: return a.ref == b.ref;
    default: return false;
    }
}

static bool e3t_value_active_equal(const e3_value &a, const e3_value &b)
{
    return a.type == b.type && e3t_payload_active_equal(a.type, a.payload, b.payload);
}

// Refusal witness. Requirements and checks:
//   - Context is always the nonnull probe and arguments are initialized.
//   - Expected: the literal status, zero callback calls, untouched probe
//     record, and unchanged guarded output, signature and argument bytes.
static void e3t_expect_refusal(uint32_t &failures, const e3t_refusal_row &row)
{
    e3_signature signature;
    e3t_signature(&signature, row.s0, row.s1, row.s2, row.sr);
    e3_value args[3];
    e3t_set_default(&args[0], row.t0);
    e3t_set_default(&args[1], row.t1);
    e3t_set_default(&args[2], row.t2);
    e3t_guarded guarded;
    std::memset(&guarded, 0xA5, sizeof guarded);
    e3t_probe probe;
    e3t_probe_init(&probe);
    probe.ret = e3t_payload_u32(0xDEADBEEFu);

    e3_signature signature_before;
    std::memcpy(&signature_before, &signature, sizeof signature);
    e3_value args_before[3];
    std::memcpy(args_before, args, sizeof args);
    e3t_guarded guarded_before;
    std::memcpy(&guarded_before, &guarded, sizeof guarded);

    const e3_signature *signature_arg = (row.null_mask & E3T_NULL_SIG) != 0u ? nullptr : &signature;
    const e3_value *args_arg = (row.null_mask & E3T_NULL_ARGS) != 0u ? nullptr : args;
    e3_callback callback_arg = (row.null_mask & E3T_NULL_CB) != 0u ? nullptr : &e3t_context_callback;
    e3_value *out_arg = (row.null_mask & E3T_NULL_OUT) != 0u ? nullptr : &guarded.out;

    const e3_status status = e3_invoke(signature_arg, args_arg, callback_arg, &probe, out_arg);

    E3T_CHECK(failures, status == row.expected);
    E3T_CHECK(failures, status != E3T_OK);
    E3T_CHECK(failures, probe.calls == 0u);
    E3T_CHECK(failures, probe.seen_args == nullptr);
    E3T_CHECK(failures, probe.seen_context == static_cast<void *>(&probe.marker));
    E3T_CHECK(failures, std::memcmp(&guarded, &guarded_before, sizeof guarded) == 0);
    E3T_CHECK(failures, std::memcmp(&signature, &signature_before, sizeof signature) == 0);
    E3T_CHECK(failures, std::memcmp(args, args_before, sizeof args) == 0);
}

// Success witness for the context-reached recorder. Requirements:
//   - null_out may be set only for a VOID expected tag.
// Checks:
//   - E3_OK and exactly one call.
//   - The ORIGINAL args pointer and context identity were passed.
//   - Every active received field matches the input.
//   - Input bytes are untouched and the adjacent canaries are intact.
//   - Non-VOID: the declared tag and the active returned bytes were stored.
//   - VOID: the whole guarded output is untouched.
static void e3t_expect_success(uint32_t &failures,
                               const e3_signature &signature,
                               const e3_value (&args)[3],
                               const e3_payload &ret,
                               uint32_t expected_tag,
                               bool null_out,
                               e3t_observed &observed)
{
    std::memset(&observed, 0, sizeof observed);
    e3t_guarded guarded;
    std::memset(&guarded, 0xA5, sizeof guarded);
    e3t_probe probe;
    e3t_probe_init(&probe);
    probe.ret = ret;

    e3_signature signature_before;
    std::memcpy(&signature_before, &signature, sizeof signature);
    e3_value args_before[3];
    std::memcpy(args_before, args, sizeof args_before);
    e3t_guarded guarded_before;
    std::memcpy(&guarded_before, &guarded, sizeof guarded);

    const e3_status status = e3_invoke(&signature, args, &e3t_context_callback, &probe,
                                       null_out ? nullptr : &guarded.out);

    E3T_CHECK(failures, status == E3T_OK);
    E3T_CHECK(failures, probe.calls == 1u);
    E3T_CHECK(failures, probe.seen_args == &args[0]);
    E3T_CHECK(failures, probe.seen_context == static_cast<void *>(&probe));
    for (std::size_t index = 0u; index < 3u; ++index) {
        E3T_CHECK(failures, e3t_value_active_equal(probe.seen_copy[index], args_before[index]));
    }
    E3T_CHECK(failures, std::memcmp(&signature, &signature_before, sizeof signature) == 0);
    E3T_CHECK(failures, std::memcmp(args, args_before, sizeof args_before) == 0);
    E3T_CHECK(failures, std::memcmp(guarded.pre, guarded_before.pre, sizeof guarded.pre) == 0);
    E3T_CHECK(failures, std::memcmp(guarded.post, guarded_before.post, sizeof guarded.post) == 0);
    if (expected_tag == E3T_VOID) {
        E3T_CHECK(failures, std::memcmp(&guarded, &guarded_before, sizeof guarded) == 0);
    } else {
        E3T_CHECK(failures, !null_out);
        E3T_CHECK(failures, guarded.out.type == expected_tag);
        E3T_CHECK(failures, e3t_payload_active_equal(expected_tag, guarded.out.payload, ret));
    }

    std::memcpy(observed.received, probe.seen_copy, sizeof observed.received);
    std::memcpy(&observed.stored, &guarded.out, sizeof observed.stored);
}

static void e3t_test_schema_constants(uint32_t &failures)
{
    E3T_CHECK(failures, E3_OK == E3T_OK);
    E3T_CHECK(failures, E3_NULL == E3T_NULL);
    E3T_CHECK(failures, E3_BAD_TYPE == E3T_BAD);
    E3T_CHECK(failures, E3_TYPE_MISMATCH == E3T_MISMATCH);
    E3T_CHECK(failures, E3_TYPE_VOID == E3T_VOID);
    E3T_CHECK(failures, E3_TYPE_I32 == E3T_I32);
    E3T_CHECK(failures, E3_TYPE_U32 == E3T_U32);
    E3T_CHECK(failures, E3_TYPE_F32 == E3T_F32);
    E3T_CHECK(failures, E3_TYPE_BOOL32 == E3T_BOOL32);
    E3T_CHECK(failures, E3_TYPE_REF == E3T_REF);
}

static void e3t_test_validate_signature(uint32_t &failures)
{
    struct row { uint32_t a0, a1, a2, result, expected; };
    const row rows[] = {
        // Index 0 sweep.
        { E3T_VOID,   E3T_F32, E3T_REF, E3T_U32, E3T_BAD },
        { E3T_I32,    E3T_F32, E3T_REF, E3T_U32, E3T_OK },
        { E3T_U32,    E3T_F32, E3T_REF, E3T_U32, E3T_OK },
        { E3T_F32,    E3T_F32, E3T_REF, E3T_U32, E3T_OK },
        { E3T_BOOL32, E3T_F32, E3T_REF, E3T_U32, E3T_OK },
        { E3T_REF,    E3T_F32, E3T_REF, E3T_U32, E3T_OK },
        { E3T_BAD6,   E3T_F32, E3T_REF, E3T_U32, E3T_BAD },
        { E3T_BADHI,  E3T_F32, E3T_REF, E3T_U32, E3T_BAD },
        { E3T_BADMAX, E3T_F32, E3T_REF, E3T_U32, E3T_BAD },
        // Index 1 sweep.
        { E3T_I32, E3T_VOID,   E3T_REF, E3T_U32, E3T_BAD },
        { E3T_I32, E3T_I32,    E3T_REF, E3T_U32, E3T_OK },
        { E3T_I32, E3T_U32,    E3T_REF, E3T_U32, E3T_OK },
        { E3T_I32, E3T_F32,    E3T_REF, E3T_U32, E3T_OK },
        { E3T_I32, E3T_BOOL32, E3T_REF, E3T_U32, E3T_OK },
        { E3T_I32, E3T_REF,    E3T_REF, E3T_U32, E3T_OK },
        { E3T_I32, E3T_BAD6,   E3T_REF, E3T_U32, E3T_BAD },
        { E3T_I32, E3T_BADHI,  E3T_REF, E3T_U32, E3T_BAD },
        { E3T_I32, E3T_BADMAX, E3T_REF, E3T_U32, E3T_BAD },
        // Index 2 sweep.
        { E3T_I32, E3T_F32, E3T_VOID,   E3T_U32, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_I32,    E3T_U32, E3T_OK },
        { E3T_I32, E3T_F32, E3T_U32,    E3T_U32, E3T_OK },
        { E3T_I32, E3T_F32, E3T_F32,    E3T_U32, E3T_OK },
        { E3T_I32, E3T_F32, E3T_BOOL32, E3T_U32, E3T_OK },
        { E3T_I32, E3T_F32, E3T_REF,    E3T_U32, E3T_OK },
        { E3T_I32, E3T_F32, E3T_BAD6,   E3T_U32, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_BADHI,  E3T_U32, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_BADMAX, E3T_U32, E3T_BAD },
        // Result sweep.
        { E3T_I32, E3T_F32, E3T_REF, E3T_VOID,   E3T_OK },
        { E3T_I32, E3T_F32, E3T_REF, E3T_I32,    E3T_OK },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32,    E3T_OK },
        { E3T_I32, E3T_F32, E3T_REF, E3T_F32,    E3T_OK },
        { E3T_I32, E3T_F32, E3T_REF, E3T_BOOL32, E3T_OK },
        { E3T_I32, E3T_F32, E3T_REF, E3T_REF,    E3T_OK },
        { E3T_I32, E3T_F32, E3T_REF, E3T_BAD6,   E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_BADHI,  E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_BADMAX, E3T_BAD },
        // Mixed defects and mixed legal signatures.
        { E3T_VOID,   E3T_VOID,   E3T_VOID,  E3T_VOID,   E3T_BAD },
        { E3T_BAD6,   E3T_F32,    E3T_REF,   E3T_BADMAX, E3T_BAD },
        { E3T_I32,    E3T_F32,    E3T_BADHI, E3T_BAD6,   E3T_BAD },
        { E3T_REF,    E3T_BOOL32, E3T_U32,   E3T_F32,    E3T_OK },
        { E3T_BOOL32, E3T_REF,    E3T_F32,   E3T_VOID,   E3T_OK },
    };
    for (std::size_t i = 0u; i < sizeof rows / sizeof rows[0]; ++i) {
        e3_signature signature;
        e3t_signature(&signature, rows[i].a0, rows[i].a1, rows[i].a2, rows[i].result);
        e3_signature before;
        std::memcpy(&before, &signature, sizeof signature);
        E3T_CHECK(failures, e3_validate_signature(&signature) == rows[i].expected);
        E3T_CHECK(failures, std::memcmp(&signature, &before, sizeof signature) == 0);
    }
    E3T_CHECK(failures, e3_validate_signature(nullptr) == E3T_NULL);
}

static void e3t_test_validate_arguments(uint32_t &failures)
{
    struct row { uint32_t s0, s1, s2, sr, t0, t1, t2, expected; };
    const row rows[] = {
        // Exact matches, mixed types, including a VOID result.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_OK },
        { E3T_I32, E3T_F32, E3T_REF, E3T_VOID, E3T_I32, E3T_F32, E3T_REF, E3T_OK },
        { E3T_BOOL32, E3T_U32, E3T_I32, E3T_REF, E3T_BOOL32, E3T_U32, E3T_I32, E3T_OK },
        { E3T_F32, E3T_REF, E3T_BOOL32, E3T_F32, E3T_F32, E3T_REF, E3T_BOOL32, E3T_OK },
        // Invalid actual tag at index 0.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_VOID, E3T_F32, E3T_REF, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_BAD6, E3T_F32, E3T_REF, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_BADMAX, E3T_F32, E3T_REF, E3T_BAD },
        // Invalid actual tag at index 1.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_VOID, E3T_REF, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_BADHI, E3T_REF, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_BADMAX, E3T_REF, E3T_BAD },
        // Invalid actual tag at index 2.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_VOID, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_BAD6, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_BADHI, E3T_BAD },
        // Legal but mismatched actual tag at each index.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_BOOL32, E3T_F32, E3T_REF, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_U32, E3T_F32, E3T_REF, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_I32, E3T_REF, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_U32, E3T_REF, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_U32, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_I32, E3T_MISMATCH },
        // Lowest defective index wins.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_BOOL32, E3T_BAD6, E3T_REF, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_VOID, E3T_I32, E3T_REF, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_U32, E3T_BADMAX, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_BADHI, E3T_U32, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_U32, E3T_F32, E3T_I32, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_BAD6, E3T_F32, E3T_VOID, E3T_BAD },
        // Whole signature is validated before any actual tag.
        { E3T_I32, E3T_F32, E3T_BAD6, E3T_U32, E3T_U32, E3T_F32, E3T_REF, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_BADMAX, E3T_BOOL32, E3T_F32, E3T_REF, E3T_BAD },
        { E3T_VOID, E3T_F32, E3T_REF, E3T_U32, E3T_VOID, E3T_F32, E3T_REF, E3T_BAD },
    };
    for (std::size_t i = 0u; i < sizeof rows / sizeof rows[0]; ++i) {
        e3_signature signature;
        e3t_signature(&signature, rows[i].s0, rows[i].s1, rows[i].s2, rows[i].sr);
        e3_value args[3];
        e3t_set_default(&args[0], rows[i].t0);
        e3t_set_default(&args[1], rows[i].t1);
        e3t_set_default(&args[2], rows[i].t2);
        e3_signature signature_before;
        std::memcpy(&signature_before, &signature, sizeof signature);
        e3_value args_before[3];
        std::memcpy(args_before, args, sizeof args);
        E3T_CHECK(failures, e3_validate_arguments(&signature, args) == rows[i].expected);
        E3T_CHECK(failures, std::memcmp(&signature, &signature_before, sizeof signature) == 0);
        E3T_CHECK(failures, std::memcmp(args, args_before, sizeof args) == 0);
    }

    e3_signature signature;
    e3t_signature(&signature, E3T_I32, E3T_F32, E3T_REF, E3T_U32);
    e3_signature bad_signature;
    e3t_signature(&bad_signature, E3T_I32, E3T_F32, E3T_REF, E3T_BAD6);
    e3_value args[3];
    e3t_set_default(&args[0], E3T_I32);
    e3t_set_default(&args[1], E3T_F32);
    e3t_set_default(&args[2], E3T_REF);
    E3T_CHECK(failures, e3_validate_arguments(nullptr, args) == E3T_NULL);
    E3T_CHECK(failures, e3_validate_arguments(&signature, nullptr) == E3T_NULL);
    E3T_CHECK(failures, e3_validate_arguments(nullptr, nullptr) == E3T_NULL);
    E3T_CHECK(failures, e3_validate_arguments(&bad_signature, nullptr) == E3T_NULL);
}

static void e3t_test_invoke_refusals(uint32_t &failures)
{
    const e3t_refusal_row rows[] = {
        // Signature defects.
        { E3T_I32, E3T_BAD6, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, 0u, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_BADMAX, E3T_I32, E3T_F32, E3T_REF, 0u, E3T_BAD },
        { E3T_VOID, E3T_F32, E3T_REF, E3T_VOID, E3T_VOID, E3T_F32, E3T_REF, 0u, E3T_BAD },
        // Invalid actual tag at each index.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_VOID, E3T_F32, E3T_REF, 0u, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_BAD6, E3T_REF, 0u, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_BADHI, 0u, E3T_BAD },
        // Mismatched actual tag at each index.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_BOOL32, E3T_F32, E3T_REF, 0u, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_U32, E3T_REF, 0u, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_U32, 0u, E3T_MISMATCH },
        // Lowest defective index wins.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_U32, E3T_F32, E3T_BADMAX, 0u, E3T_MISMATCH },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_VOID, E3T_I32, 0u, E3T_BAD },
        // Signature validated before actual tags.
        { E3T_I32, E3T_F32, E3T_BAD6, E3T_U32, E3T_BOOL32, E3T_F32, E3T_REF, 0u, E3T_BAD },
        // Output-required precedence: earlier defects beat null out.
        { E3T_I32, E3T_F32, E3T_REF, E3T_BAD6, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_OUT, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_BADMAX, E3T_NULL_OUT, E3T_BAD },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_BOOL32, E3T_REF, E3T_NULL_OUT, E3T_MISMATCH },
        // Null out with a valid non-VOID result of each kind.
        { E3T_I32, E3T_F32, E3T_REF, E3T_I32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_OUT, E3T_NULL },
        { E3T_REF, E3T_BOOL32, E3T_U32, E3T_U32, E3T_REF, E3T_BOOL32, E3T_U32, E3T_NULL_OUT, E3T_NULL },
        { E3T_F32, E3T_U32, E3T_I32, E3T_F32, E3T_F32, E3T_U32, E3T_I32, E3T_NULL_OUT, E3T_NULL },
        { E3T_BOOL32, E3T_REF, E3T_F32, E3T_BOOL32, E3T_BOOL32, E3T_REF, E3T_F32, E3T_NULL_OUT, E3T_NULL },
        { E3T_U32, E3T_I32, E3T_BOOL32, E3T_REF, E3T_U32, E3T_I32, E3T_BOOL32, E3T_NULL_OUT, E3T_NULL },
    };
    for (std::size_t i = 0u; i < sizeof rows / sizeof rows[0]; ++i) {
        e3t_expect_refusal(failures, rows[i]);
    }
}

static void e3t_test_invoke_null_combinations(uint32_t &failures)
{
    const e3t_refusal_row rows[] = {
        // All seven nonzero (signature, args, callback) masks, out nonnull.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_SIG, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_ARGS, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_SIG | E3T_NULL_ARGS, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_CB, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_SIG | E3T_NULL_CB, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_ARGS | E3T_NULL_CB, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF,
          E3T_NULL_SIG | E3T_NULL_ARGS | E3T_NULL_CB, E3T_NULL },
        // The same seven masks with out also null.
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_SIG | E3T_NULL_OUT, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_ARGS | E3T_NULL_OUT, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF,
          E3T_NULL_SIG | E3T_NULL_ARGS | E3T_NULL_OUT, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_CB | E3T_NULL_OUT, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF,
          E3T_NULL_SIG | E3T_NULL_CB | E3T_NULL_OUT, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF,
          E3T_NULL_ARGS | E3T_NULL_CB | E3T_NULL_OUT, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_REF,
          E3T_NULL_SIG | E3T_NULL_ARGS | E3T_NULL_CB | E3T_NULL_OUT, E3T_NULL },
        // Null wins over every type defect in the remaining nonnull inputs.
        { E3T_I32, E3T_F32, E3T_REF, E3T_BAD6, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_ARGS, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_BAD6, E3T_I32, E3T_F32, E3T_REF, E3T_NULL_CB, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_BOOL32, E3T_F32, E3T_REF, E3T_NULL_CB, E3T_NULL },
        { E3T_I32, E3T_F32, E3T_REF, E3T_U32, E3T_I32, E3T_F32, E3T_BADMAX, E3T_NULL_CB | E3T_NULL_OUT, E3T_NULL },
    };
    for (std::size_t i = 0u; i < sizeof rows / sizeof rows[0]; ++i) {
        e3t_expect_refusal(failures, rows[i]);
    }
}

static void e3t_test_invoke_result_kinds(uint32_t &failures)
{
    unsigned char target = 0x11u;
    void *const target_ref = static_cast<void *>(&target);

    {   // VOID, nonnull out: returned payload ignored, output untouched.
        e3_signature signature;
        e3t_signature(&signature, E3T_I32, E3T_F32, E3T_REF, E3T_VOID);
        e3_value args[3];
        e3t_set_i32(&args[0], -1);
        e3t_set_f32_bits(&args[1], 0xBF800000u);
        e3t_set_ref(&args[2], target_ref);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_u32(0xCAFEBABEu), E3T_VOID, false, observed);
        E3T_CHECK(failures, observed.received[0].type == E3T_I32 && observed.received[0].payload.i32 == -1);
        E3T_CHECK(failures, e3t_f32_bits(observed.received[1].payload) == 0xBF800000u);
        E3T_CHECK(failures, observed.received[2].payload.ref == target_ref);
    }
    {   // VOID, null out.
        e3_signature signature;
        e3t_signature(&signature, E3T_BOOL32, E3T_U32, E3T_REF, E3T_VOID);
        e3_value args[3];
        e3t_set_bool32(&args[0], 256);
        e3t_set_u32(&args[1], 0xFFFFFFFFu);
        e3t_set_ref(&args[2], nullptr);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_i32(42), E3T_VOID, true, observed);
        E3T_CHECK(failures, observed.received[0].type == E3T_BOOL32 && observed.received[0].payload.b32 == 256);
        E3T_CHECK(failures, observed.received[1].type == E3T_U32 && observed.received[1].payload.u32 == 0xFFFFFFFFu);
        E3T_CHECK(failures, observed.received[2].type == E3T_REF && observed.received[2].payload.ref == nullptr);
    }
    {   // I32 result from (BOOL32, REF, F32).
        e3_signature signature;
        e3t_signature(&signature, E3T_BOOL32, E3T_REF, E3T_F32, E3T_I32);
        e3_value args[3];
        e3t_set_bool32(&args[0], -1);
        e3t_set_ref(&args[1], target_ref);
        e3t_set_f32_bits(&args[2], 0x7F800000u);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_i32(INT32_MIN), E3T_I32, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.b32 == -1);
        E3T_CHECK(failures, observed.received[1].payload.ref == target_ref);
        E3T_CHECK(failures, e3t_f32_bits(observed.received[2].payload) == 0x7F800000u);
        E3T_CHECK(failures, observed.stored.type == E3T_I32 && observed.stored.payload.i32 == INT32_MIN);
    }
    {   // U32 result from (U32, I32, BOOL32).
        e3_signature signature;
        e3t_signature(&signature, E3T_U32, E3T_I32, E3T_BOOL32, E3T_U32);
        e3_value args[3];
        e3t_set_u32(&args[0], 0u);
        e3t_set_i32(&args[1], INT32_MAX);
        e3t_set_bool32(&args[2], 0);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_u32(UINT32_MAX), E3T_U32, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.u32 == 0u);
        E3T_CHECK(failures, observed.received[1].payload.i32 == INT32_MAX);
        E3T_CHECK(failures, observed.received[2].payload.b32 == 0);
        E3T_CHECK(failures, observed.stored.type == E3T_U32 && observed.stored.payload.u32 == UINT32_MAX);
    }
    {   // F32 result from (REF, BOOL32, U32).
        e3_signature signature;
        e3t_signature(&signature, E3T_REF, E3T_BOOL32, E3T_U32, E3T_F32);
        e3_value args[3];
        e3t_set_ref(&args[0], nullptr);
        e3t_set_bool32(&args[1], 1);
        e3t_set_u32(&args[2], 0x80000000u);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_f32_bits(0x3F800000u), E3T_F32, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.ref == nullptr);
        E3T_CHECK(failures, observed.received[1].payload.b32 == 1);
        E3T_CHECK(failures, observed.received[2].payload.u32 == 0x80000000u);
        E3T_CHECK(failures, observed.stored.type == E3T_F32);
        E3T_CHECK(failures, e3t_f32_bits(observed.stored.payload) == 0x3F800000u);
    }
    {   // BOOL32 result from (I32, F32, REF).
        e3_signature signature;
        e3t_signature(&signature, E3T_I32, E3T_F32, E3T_REF, E3T_BOOL32);
        e3_value args[3];
        e3t_set_i32(&args[0], 0);
        e3t_set_f32_bits(&args[1], 0x80000000u);
        e3t_set_ref(&args[2], target_ref);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_bool32(-1), E3T_BOOL32, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.i32 == 0);
        E3T_CHECK(failures, e3t_f32_bits(observed.received[1].payload) == 0x80000000u);
        E3T_CHECK(failures, observed.received[2].payload.ref == target_ref);
        E3T_CHECK(failures, observed.stored.type == E3T_BOOL32 && observed.stored.payload.b32 == -1);
    }
    {   // REF result from (F32, U32, I32).
        e3_signature signature;
        e3t_signature(&signature, E3T_F32, E3T_U32, E3T_I32, E3T_REF);
        e3_value args[3];
        e3t_set_f32_bits(&args[0], 0x7FC00000u);
        e3t_set_u32(&args[1], 1u);
        e3t_set_i32(&args[2], -2);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_ref(target_ref), E3T_REF, false, observed);
        E3T_CHECK(failures, e3t_f32_bits(observed.received[0].payload) == 0x7FC00000u);
        E3T_CHECK(failures, observed.received[1].payload.u32 == 1u);
        E3T_CHECK(failures, observed.received[2].payload.i32 == -2);
        E3T_CHECK(failures, observed.stored.type == E3T_REF && observed.stored.payload.ref == target_ref);
    }
}

static void e3t_test_invoke_integer_extrema(uint32_t &failures)
{
    {
        e3_signature signature;
        e3t_signature(&signature, E3T_I32, E3T_I32, E3T_U32, E3T_I32);
        e3_value args[3];
        e3t_set_i32(&args[0], INT32_MIN);
        e3t_set_i32(&args[1], INT32_MAX);
        e3t_set_u32(&args[2], UINT32_MAX);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_i32(INT32_MAX), E3T_I32, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.i32 == INT32_MIN);
        E3T_CHECK(failures, observed.received[1].payload.i32 == INT32_MAX);
        E3T_CHECK(failures, observed.received[2].payload.u32 == UINT32_MAX);
        E3T_CHECK(failures, observed.stored.type == E3T_I32 && observed.stored.payload.i32 == INT32_MAX);
    }
    {
        e3_signature signature;
        e3t_signature(&signature, E3T_U32, E3T_I32, E3T_U32, E3T_U32);
        e3_value args[3];
        e3t_set_u32(&args[0], 0u);
        e3t_set_i32(&args[1], -1);
        e3t_set_u32(&args[2], 0x80000000u);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_u32(0u), E3T_U32, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.u32 == 0u);
        E3T_CHECK(failures, observed.received[1].payload.i32 == -1);
        E3T_CHECK(failures, observed.received[2].payload.u32 == 0x80000000u);
        E3T_CHECK(failures, observed.stored.type == E3T_U32 && observed.stored.payload.u32 == 0u);
    }
    {
        e3_signature signature;
        e3t_signature(&signature, E3T_I32, E3T_U32, E3T_I32, E3T_I32);
        e3_value args[3];
        e3t_set_i32(&args[0], 0);
        e3t_set_u32(&args[1], 0x7FFFFFFFu);
        e3t_set_i32(&args[2], INT32_MIN);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_i32(INT32_MIN), E3T_I32, false, observed);
        E3T_CHECK(failures, observed.received[1].payload.u32 == 0x7FFFFFFFu);
        E3T_CHECK(failures, observed.received[2].payload.i32 == INT32_MIN);
        E3T_CHECK(failures, observed.stored.type == E3T_I32 && observed.stored.payload.i32 == INT32_MIN);
    }
}

static void e3t_test_invoke_bool32(uint32_t &failures)
{
    struct row { int32_t value, other; };
    const row rows[] = {
        { 0, INT32_MAX },
        { 1, INT32_MIN },
        { -1, 256 },
        { 256, -1 },
        { INT32_MIN, 1 },
        { INT32_MAX, 0 },
    };
    for (std::size_t i = 0u; i < sizeof rows / sizeof rows[0]; ++i) {
        e3_signature signature;
        e3t_signature(&signature, E3T_BOOL32, E3T_I32, E3T_BOOL32, E3T_BOOL32);
        e3_value args[3];
        e3t_set_bool32(&args[0], rows[i].value);
        e3t_set_i32(&args[1], rows[i].value);
        e3t_set_bool32(&args[2], rows[i].other);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_bool32(rows[i].value), E3T_BOOL32, false, observed);
        E3T_CHECK(failures, observed.received[0].type == E3T_BOOL32);
        E3T_CHECK(failures, observed.received[0].payload.b32 == rows[i].value);
        E3T_CHECK(failures, observed.received[1].type == E3T_I32);
        E3T_CHECK(failures, observed.received[1].payload.i32 == rows[i].value);
        E3T_CHECK(failures, observed.received[2].payload.b32 == rows[i].other);
        E3T_CHECK(failures, observed.stored.type == E3T_BOOL32);
        E3T_CHECK(failures, observed.stored.payload.b32 == rows[i].value);
    }
}

static void e3t_test_invoke_refs(uint32_t &failures)
{
    unsigned char first = 0x21u;
    unsigned char second = 0x42u;
    void *const first_ref = static_cast<void *>(&first);
    void *const second_ref = static_cast<void *>(&second);

    {   // Null and nonnull borrowed refs in, nonnull ref out.
        e3_signature signature;
        e3t_signature(&signature, E3T_REF, E3T_REF, E3T_U32, E3T_REF);
        e3_value args[3];
        e3t_set_ref(&args[0], nullptr);
        e3t_set_ref(&args[1], first_ref);
        e3t_set_u32(&args[2], 3u);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_ref(second_ref), E3T_REF, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.ref == nullptr);
        E3T_CHECK(failures, observed.received[1].payload.ref == first_ref);
        E3T_CHECK(failures, observed.stored.type == E3T_REF && observed.stored.payload.ref == second_ref);
    }
    {   // Null ref out.
        e3_signature signature;
        e3t_signature(&signature, E3T_REF, E3T_REF, E3T_U32, E3T_REF);
        e3_value args[3];
        e3t_set_ref(&args[0], second_ref);
        e3t_set_ref(&args[1], nullptr);
        e3t_set_u32(&args[2], 4u);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_ref(nullptr), E3T_REF, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.ref == second_ref);
        E3T_CHECK(failures, observed.received[1].payload.ref == nullptr);
        E3T_CHECK(failures, observed.stored.type == E3T_REF && observed.stored.payload.ref == nullptr);
    }
    {   // Refs into VOID.
        e3_signature signature;
        e3t_signature(&signature, E3T_REF, E3T_I32, E3T_REF, E3T_VOID);
        e3_value args[3];
        e3t_set_ref(&args[0], first_ref);
        e3t_set_i32(&args[1], 5);
        e3t_set_ref(&args[2], second_ref);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_ref(first_ref), E3T_VOID, false, observed);
        E3T_CHECK(failures, observed.received[0].payload.ref == first_ref);
        E3T_CHECK(failures, observed.received[2].payload.ref == second_ref);
    }
    E3T_CHECK(failures, first == 0x21u && second == 0x42u);
}

static void e3t_test_invoke_f32(uint32_t &failures)
{
    struct row { uint32_t bits, other; };
    const row rows[] = {
        { 0x00000000u, 0xFFFFFFFFu }, // +0
        { 0x80000000u, 0x7FD55555u }, // -0
        { 0x3F800000u, 0x7FC00001u }, // +1
        { 0xBF800000u, 0xFFC00000u }, // -1
        { 0x7F7FFFFFu, 0x7FC00000u }, // +max finite
        { 0xFF7FFFFFu, 0xFF800000u }, // -max finite
        { 0x00800000u, 0x7F800000u }, // +min normal
        { 0x80800000u, 0x007FFFFFu }, // -min normal
        { 0x00000001u, 0x80000001u }, // +min subnormal
        { 0x80000001u, 0x00000001u }, // -min subnormal
        { 0x007FFFFFu, 0x80800000u }, // +max subnormal
        { 0x7F800000u, 0x00800000u }, // +infinity
        { 0xFF800000u, 0xFF7FFFFFu }, // -infinity
        { 0x7FC00000u, 0x7F7FFFFFu }, // +quiet NaN
        { 0xFFC00000u, 0xBF800000u }, // -quiet NaN
        { 0x7FC00001u, 0x3F800000u }, // +quiet NaN, payload 1
        { 0x7FD55555u, 0x80000000u }, // +quiet NaN, alternating payload
        { 0xFFFFFFFFu, 0x00000000u }, // -quiet NaN, all payload bits
    };
    for (std::size_t i = 0u; i < sizeof rows / sizeof rows[0]; ++i) {
        e3_signature signature;
        e3t_signature(&signature, E3T_F32, E3T_I32, E3T_F32, E3T_F32);
        e3_value args[3];
        e3t_set_f32_bits(&args[0], rows[i].bits);
        e3t_set_i32(&args[1], 0x12345678);
        e3t_set_f32_bits(&args[2], rows[i].other);
        e3t_observed observed;
        e3t_expect_success(failures, signature, args, e3t_payload_f32_bits(rows[i].bits), E3T_F32, false, observed);
        E3T_CHECK(failures, observed.received[0].type == E3T_F32);
        E3T_CHECK(failures, e3t_f32_bits(observed.received[0].payload) == rows[i].bits);
        E3T_CHECK(failures, observed.received[1].payload.i32 == 0x12345678);
        E3T_CHECK(failures, e3t_f32_bits(observed.received[2].payload) == rows[i].other);
        E3T_CHECK(failures, observed.stored.type == E3T_F32);
        E3T_CHECK(failures, e3t_f32_bits(observed.stored.payload) == rows[i].bits);
    }
}

// Null-context successes. The probe is reached only through the VALID third
// REF argument, as a borrowed pointer (test bookkeeping only).
static void e3t_test_invoke_null_context(uint32_t &failures)
{
    {   // Non-VOID result.
        e3t_probe probe;
        e3t_probe_init(&probe);
        probe.ret = e3t_payload_u32(0xA5A55A5Au);
        e3_signature signature;
        e3t_signature(&signature, E3T_I32, E3T_F32, E3T_REF, E3T_U32);
        e3_value args[3];
        e3t_set_i32(&args[0], INT32_MIN);
        e3t_set_f32_bits(&args[1], 0x80000000u);
        e3t_set_ref(&args[2], static_cast<void *>(&probe));
        e3t_guarded guarded;
        std::memset(&guarded, 0xA5, sizeof guarded);

        e3_signature signature_before;
        std::memcpy(&signature_before, &signature, sizeof signature);
        e3_value args_before[3];
        std::memcpy(args_before, args, sizeof args);
        e3t_guarded guarded_before;
        std::memcpy(&guarded_before, &guarded, sizeof guarded);

        const e3_status status = e3_invoke(&signature, args, &e3t_ref_probe_callback, nullptr, &guarded.out);

        E3T_CHECK(failures, status == E3T_OK);
        E3T_CHECK(failures, probe.calls == 1u);
        E3T_CHECK(failures, probe.seen_context == nullptr);
        E3T_CHECK(failures, probe.seen_args == &args[0]);
        for (std::size_t index = 0u; index < 3u; ++index) {
            E3T_CHECK(failures, e3t_value_active_equal(probe.seen_copy[index], args_before[index]));
        }
        E3T_CHECK(failures, probe.seen_copy[0].payload.i32 == INT32_MIN);
        E3T_CHECK(failures, e3t_f32_bits(probe.seen_copy[1].payload) == 0x80000000u);
        E3T_CHECK(failures, probe.seen_copy[2].payload.ref == static_cast<void *>(&probe));
        E3T_CHECK(failures, std::memcmp(&signature, &signature_before, sizeof signature) == 0);
        E3T_CHECK(failures, std::memcmp(args, args_before, sizeof args) == 0);
        E3T_CHECK(failures, std::memcmp(guarded.pre, guarded_before.pre, sizeof guarded.pre) == 0);
        E3T_CHECK(failures, std::memcmp(guarded.post, guarded_before.post, sizeof guarded.post) == 0);
        E3T_CHECK(failures, guarded.out.type == E3T_U32);
        E3T_CHECK(failures, guarded.out.payload.u32 == 0xA5A55A5Au);
    }
    {   // VOID result, null out.
        e3t_probe probe;
        e3t_probe_init(&probe);
        probe.ret = e3t_payload_i32(-5);
        e3_signature signature;
        e3t_signature(&signature, E3T_BOOL32, E3T_U32, E3T_REF, E3T_VOID);
        e3_value args[3];
        e3t_set_bool32(&args[0], INT32_MAX);
        e3t_set_u32(&args[1], 9u);
        e3t_set_ref(&args[2], static_cast<void *>(&probe));
        e3_value args_before[3];
        std::memcpy(args_before, args, sizeof args);

        const e3_status status = e3_invoke(&signature, args, &e3t_ref_probe_callback, nullptr, nullptr);

        E3T_CHECK(failures, status == E3T_OK);
        E3T_CHECK(failures, probe.calls == 1u);
        E3T_CHECK(failures, probe.seen_context == nullptr);
        E3T_CHECK(failures, probe.seen_args == &args[0]);
        for (std::size_t index = 0u; index < 3u; ++index) {
            E3T_CHECK(failures, e3t_value_active_equal(probe.seen_copy[index], args_before[index]));
        }
        E3T_CHECK(failures, std::memcmp(args, args_before, sizeof args) == 0);
    }
}

extern "C" uint32_t e3_run_tests(void) noexcept
{
    uint32_t failures = 0u;
    e3t_test_schema_constants(failures);
    e3t_test_validate_signature(failures);
    e3t_test_validate_arguments(failures);
    e3t_test_invoke_refusals(failures);
    e3t_test_invoke_null_combinations(failures);
    e3t_test_invoke_result_kinds(failures);
    e3t_test_invoke_integer_extrema(failures);
    e3t_test_invoke_bool32(failures);
    e3t_test_invoke_refs(failures);
    e3t_test_invoke_f32(failures);
    e3t_test_invoke_null_context(failures);
    return failures;
}

#if defined(GAMEPLAY_GRAPH_EFFECT3_STANDALONE_TEST_MAIN)
int main()
{
    return e3_run_tests() != 0u ? 1 : 0;
}
#endif
