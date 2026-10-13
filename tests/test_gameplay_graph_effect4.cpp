// Authored tests for the synchronous four-argument typed effect gateway.
//
// No namespace-scope, static or thread_local variables are used, including
// immutable tables. Every table is an automatic function-local const array.
// Every probe record is a caller-owned local passed through the callback
// context pointer.
//
// Representation assumptions (test-only, asserted below): float is IEEE-754
// binary32 and has the same size as uint32_t. F32 witnesses are written as
// uint32_t bit patterns and moved into and out of payloads with memcpy, never
// through float arithmetic. Successful results are compared by declared
// fields or by the active payload representation, not by whole-object bytes.
// Whole-object bytes are compared only for initialized canaries that must
// stay untouched and for caller-owned inputs that must not be mutated.

#include "gameplay_graph_effect4.h"

#include <limits>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static_assert(sizeof(float) == sizeof(uint32_t),
              "f32 witnesses assume a 32-bit float object representation");
static_assert(std::numeric_limits<float>::is_iec559,
              "f32 witnesses assume IEEE-754 binary32 floats");

#define E4T_ARGC 4u
#define E4T_PATTERN_COUNT 11u
#define E4T_BAD_ARG_COUNT 5u
#define E4T_BAD_RESULT_COUNT 4u
#define E4T_MAX_WITNESSES 16u
#define E4T_CANARY_BYTE 0xA5
#define E4T_NULL_CONTEXT_MARK 0xC0DE0E40u
#define E4T_LIVE_CONTEXT_MARK 0x11FEC0DEu
#define E4T_VOID_REPLY_BITS 0x5A5A5A5Au
#define E4T_CHECK(failures, condition) e4t_check((failures), (condition) ? true : false)

// Records one callback observation. Owned by the calling test function.
struct e4t_probe {
    uint32_t calls;
    void *context_seen;
    e4_value seen[4];
    e4_payload reply;
};

extern "C" {
e4_payload e4t_probe_callback(void *context, const e4_value args[4]);
e4_payload e4t_null_context_callback(void *context, const e4_value args[4]);
uint32_t e4_run_tests(void);
}

// Counts the call, records the context and argument values, and returns the
// caller-chosen reply. Never writes through args.
extern "C" e4_payload e4t_probe_callback(void *context, const e4_value args[4])
{
    e4_payload reply;
    memset(&reply, 0, sizeof(reply));
    if (context == NULL) {
        return reply;
    }
    e4t_probe *probe = static_cast<e4t_probe *>(context);
    probe->calls += 1u;
    probe->context_seen = context;
    if (args != NULL) {
        memcpy(probe->seen, args, sizeof(probe->seen));
    }
    reply = probe->reply;
    return reply;
}

// Reports through its result whether it observed a null context.
extern "C" e4_payload e4t_null_context_callback(void *context, const e4_value args[4])
{
    e4_payload reply;
    memset(&reply, 0, sizeof(reply));
    (void)args;
    reply.u32 = (context == NULL) ? E4T_NULL_CONTEXT_MARK : E4T_LIVE_CONTEXT_MARK;
    return reply;
}

namespace {

void e4t_check(uint32_t *failures, bool condition)
{
    if (!condition) {
        *failures += 1u;
    }
}

bool e4t_bytes_equal(const void *a, const void *b, size_t size)
{
    return memcmp(a, b, size) == 0;
}

uint32_t e4t_f32_bits(const e4_payload *payload)
{
    uint32_t bits = 0u;
    memcpy(&bits, &payload->f32, sizeof(bits));
    return bits;
}

e4_payload e4t_zero_payload(void)
{
    e4_payload payload;
    memset(&payload, 0, sizeof(payload));
    return payload;
}

e4_value e4t_blank(uint32_t tag)
{
    e4_value value;
    memset(&value, 0, sizeof(value));
    value.type = tag;
    return value;
}

e4_value e4t_i32(int32_t x)
{
    e4_value value = e4t_blank(E4_TYPE_I32);
    value.payload.i32 = x;
    return value;
}

e4_value e4t_u32(uint32_t x)
{
    e4_value value = e4t_blank(E4_TYPE_U32);
    value.payload.u32 = x;
    return value;
}

e4_value e4t_f32_from_bits(uint32_t bits)
{
    e4_value value = e4t_blank(E4_TYPE_F32);
    memcpy(&value.payload.f32, &bits, sizeof(bits));
    return value;
}

e4_value e4t_b32(int32_t x)
{
    e4_value value = e4t_blank(E4_TYPE_BOOL32);
    value.payload.b32 = x;
    return value;
}

e4_value e4t_ref(void *pointer)
{
    e4_value value = e4t_blank(E4_TYPE_REF);
    value.payload.ref = pointer;
    return value;
}

// Representative argument value for a tag. Unsupported tags keep a zero payload.
e4_value e4t_sample(uint32_t tag, void *ref_target)
{
    switch (tag) {
    case E4_TYPE_I32:
        return e4t_i32(-123456789);
    case E4_TYPE_U32:
        return e4t_u32(0xDEADBEEFu);
    case E4_TYPE_F32:
        return e4t_f32_from_bits(0x3FC00000u);
    case E4_TYPE_BOOL32:
        return e4t_b32(256);
    case E4_TYPE_REF:
        return e4t_ref(ref_target);
    default:
        return e4t_blank(tag);
    }
}

// Reply value for a result tag, distinct from every argument sample.
e4_value e4t_reply_sample(uint32_t tag, void *ref_target)
{
    switch (tag) {
    case E4_TYPE_I32:
        return e4t_i32(324508639);
    case E4_TYPE_U32:
        return e4t_u32(0x2468ACE0u);
    case E4_TYPE_F32:
        return e4t_f32_from_bits(0xC0490FDBu);
    case E4_TYPE_BOOL32:
        return e4t_b32(-1);
    case E4_TYPE_REF:
        return e4t_ref(ref_target);
    default: {
        e4_value value = e4t_blank(tag);
        value.payload.u32 = E4T_VOID_REPLY_BITS;
        return value;
    }
    }
}

bool e4t_payload_equal(uint32_t tag, const e4_payload *a, const e4_payload *b)
{
    switch (tag) {
    case E4_TYPE_I32:
        return a->i32 == b->i32;
    case E4_TYPE_U32:
        return a->u32 == b->u32;
    case E4_TYPE_F32:
        return e4t_f32_bits(a) == e4t_f32_bits(b);
    case E4_TYPE_BOOL32:
        return a->b32 == b->b32;
    case E4_TYPE_REF:
        return a->ref == b->ref;
    default:
        return false;
    }
}

bool e4t_value_equal(const e4_value *a, const e4_value *b)
{
    return a->type == b->type && e4t_payload_equal(a->type, &a->payload, &b->payload);
}

uint32_t e4t_other_tag(uint32_t tag)
{
    return (tag >= E4_TYPE_REF) ? E4_TYPE_I32 : tag + 1u;
}

// Patterns 0..4 are uniform (I32..REF); 5..10 are mixed. Across the first
// five mixed patterns every slot sees every valid argument tag.
void e4t_pattern(uint32_t index, uint32_t pattern[4])
{
    const uint32_t mixed[6][4] = {
        {E4_TYPE_I32, E4_TYPE_U32, E4_TYPE_F32, E4_TYPE_BOOL32},
        {E4_TYPE_U32, E4_TYPE_F32, E4_TYPE_BOOL32, E4_TYPE_REF},
        {E4_TYPE_F32, E4_TYPE_BOOL32, E4_TYPE_REF, E4_TYPE_I32},
        {E4_TYPE_BOOL32, E4_TYPE_REF, E4_TYPE_I32, E4_TYPE_U32},
        {E4_TYPE_REF, E4_TYPE_I32, E4_TYPE_U32, E4_TYPE_F32},
        {E4_TYPE_I32, E4_TYPE_I32, E4_TYPE_REF, E4_TYPE_BOOL32}};
    for (uint32_t i = 0u; i < E4T_ARGC; ++i) {
        pattern[i] = (index < 5u) ? (E4_TYPE_I32 + index) : mixed[(index - 5u) % 6u][i];
    }
}

uint32_t e4t_bad_arg_tag(uint32_t index)
{
    const uint32_t tags[E4T_BAD_ARG_COUNT] = {E4_TYPE_VOID, 6u, 7u, 0x80000000u, 0xFFFFFFFFu};
    return tags[index % E4T_BAD_ARG_COUNT];
}

uint32_t e4t_bad_result_tag(uint32_t index)
{
    const uint32_t tags[E4T_BAD_RESULT_COUNT] = {6u, 7u, 0x80000000u, 0xFFFFFFFFu};
    return tags[index % E4T_BAD_RESULT_COUNT];
}

e4_signature e4t_signature_from(const uint32_t pattern[4], uint32_t result)
{
    e4_signature signature;
    memset(&signature, 0, sizeof(signature));
    for (uint32_t i = 0u; i < E4T_ARGC; ++i) {
        signature.args[i] = pattern[i];
    }
    signature.result_type = result;
    return signature;
}

void e4t_fill_args(const e4_signature *signature, e4_value args[4], void *ref_target)
{
    for (uint32_t i = 0u; i < E4T_ARGC; ++i) {
        args[i] = e4t_sample(signature->args[i], ref_target);
    }
}

void e4t_probe_reset(e4t_probe *probe, e4_payload reply)
{
    memset(probe, 0, sizeof(*probe));
    probe->reply = reply;
}

void e4t_canary(e4_value *out)
{
    memset(out, E4T_CANARY_BYTE, sizeof(*out));
}

// A pre-output refusal must return the same status with a nonnull canary
// output and with a null output, call the callback zero times, and leave the
// canary, the signature and the arguments byte-identical.
void e4t_expect_refusal(uint32_t *failures, const e4_signature *signature,
                        const e4_value args[4], uint32_t expected)
{
    unsigned char signature_before[sizeof(e4_signature)];
    unsigned char args_before[sizeof(e4_value) * E4T_ARGC];
    if (signature != NULL) {
        memcpy(signature_before, signature, sizeof(signature_before));
    }
    if (args != NULL) {
        memcpy(args_before, args, sizeof(args_before));
    }

    e4t_probe probe;
    e4t_probe_reset(&probe, e4t_zero_payload());
    e4_value out;
    e4t_canary(&out);
    unsigned char out_before[sizeof(e4_value)];
    memcpy(out_before, &out, sizeof(out_before));

    E4T_CHECK(failures, e4_invoke(signature, args, e4t_probe_callback, &probe, &out) == expected);
    E4T_CHECK(failures, probe.calls == 0u);
    E4T_CHECK(failures, e4t_bytes_equal(&out, out_before, sizeof(out_before)));

    E4T_CHECK(failures, e4_invoke(signature, args, e4t_probe_callback, &probe, NULL) == expected);
    E4T_CHECK(failures, probe.calls == 0u);

    if (signature != NULL) {
        E4T_CHECK(failures, e4t_bytes_equal(signature, signature_before, sizeof(signature_before)));
    }
    if (args != NULL) {
        E4T_CHECK(failures, e4t_bytes_equal(args, args_before, sizeof(args_before)));
    }
}

uint32_t e4t_witnesses(uint32_t tag, e4_value witnesses[E4T_MAX_WITNESSES],
                       void *ref_a, void *ref_b)
{
    const int32_t i32_min = std::numeric_limits<int32_t>::min();
    const int32_t i32_max = std::numeric_limits<int32_t>::max();
    uint32_t count = 0u;
    if (tag == E4_TYPE_I32) {
        const int32_t values[5] = {i32_min, i32_max, -1, 0, 1};
        for (uint32_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
            witnesses[count++] = e4t_i32(values[i]);
        }
    } else if (tag == E4_TYPE_U32) {
        const uint32_t values[5] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u,
                                    std::numeric_limits<uint32_t>::max()};
        for (uint32_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
            witnesses[count++] = e4t_u32(values[i]);
        }
    } else if (tag == E4_TYPE_F32) {
        // binary32 bit patterns: +0, -0, canonical qNaN, qNaN with payloads,
        // sNaN with payloads, infinities, subnormals and extreme normals.
        const uint32_t bits[15] = {
            0x00000000u, 0x80000000u,
            0x7FC00000u, 0x7FC12345u, 0xFFC00001u,
            0x7F800001u, 0x7FA5A5A5u, 0xFF800001u,
            0x7F800000u, 0xFF800000u,
            0x00000001u, 0x807FFFFFu,
            0x00800000u, 0x7F7FFFFFu, 0x3F800000u};
        for (uint32_t i = 0u; i < sizeof(bits) / sizeof(bits[0]); ++i) {
            witnesses[count++] = e4t_f32_from_bits(bits[i]);
        }
    } else if (tag == E4_TYPE_BOOL32) {
        const int32_t values[6] = {-1, 256, i32_min, i32_max, 0, 1};
        for (uint32_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
            witnesses[count++] = e4t_b32(values[i]);
        }
    } else if (tag == E4_TYPE_REF) {
        witnesses[count++] = e4t_ref(NULL);
        witnesses[count++] = e4t_ref(ref_a);
        witnesses[count++] = e4t_ref(ref_b);
    }
    return count;
}

void e4t_test_signature_validation(uint32_t *failures)
{
    E4T_CHECK(failures, e4_validate_signature(NULL) == E4_NULL);

    for (uint32_t p = 0u; p < E4T_PATTERN_COUNT; ++p) {
        uint32_t pattern[4];
        e4t_pattern(p, pattern);

        // Every valid argument pattern accepts every result tag, including VOID.
        for (uint32_t r = E4_TYPE_VOID; r <= E4_TYPE_REF; ++r) {
            e4_signature sig = e4t_signature_from(pattern, r);
            const e4_signature before = sig;
            E4T_CHECK(failures, e4_validate_signature(&sig) == E4_OK);
            E4T_CHECK(failures, e4t_bytes_equal(&sig, &before, sizeof(sig)));
        }

        for (uint32_t b = 0u; b < E4T_BAD_RESULT_COUNT; ++b) {
            e4_signature sig = e4t_signature_from(pattern, e4t_bad_result_tag(b));
            E4T_CHECK(failures, e4_validate_signature(&sig) == E4_BAD_TYPE);
        }

        // VOID and unknown tags are refused at every argument slot, with a
        // valid or an invalid result type.
        for (uint32_t slot = 0u; slot < E4T_ARGC; ++slot) {
            for (uint32_t b = 0u; b < E4T_BAD_ARG_COUNT; ++b) {
                e4_signature sig = e4t_signature_from(pattern, (slot + b) % (E4_TYPE_REF + 1u));
                sig.args[slot] = e4t_bad_arg_tag(b);
                E4T_CHECK(failures, e4_validate_signature(&sig) == E4_BAD_TYPE);
                sig.result_type = e4t_bad_result_tag(b);
                E4T_CHECK(failures, e4_validate_signature(&sig) == E4_BAD_TYPE);
            }
        }
    }

    const uint32_t all_bad[4] = {6u, 7u, 0x80000000u, 0xFFFFFFFFu};
    e4_signature sig = e4t_signature_from(all_bad, E4_TYPE_VOID);
    E4T_CHECK(failures, e4_validate_signature(&sig) == E4_BAD_TYPE);
    const uint32_t all_void[4] = {E4_TYPE_VOID, E4_TYPE_VOID, E4_TYPE_VOID, E4_TYPE_VOID};
    sig = e4t_signature_from(all_void, E4_TYPE_VOID);
    E4T_CHECK(failures, e4_validate_signature(&sig) == E4_BAD_TYPE);
}

void e4t_test_argument_validation(uint32_t *failures)
{
    int ref_target = 41;
    uint32_t base[4];
    e4t_pattern(5u, base);
    e4_signature sig = e4t_signature_from(base, E4_TYPE_U32);
    e4_value args[4];
    e4t_fill_args(&sig, args, &ref_target);

    e4_signature bad_arg_sig = sig;
    bad_arg_sig.args[3] = 6u;
    e4_signature bad_result_sig = sig;
    bad_result_sig.result_type = 0xFFFFFFFFu;

    // Null pointers are reported before any signature validation.
    E4T_CHECK(failures, e4_validate_arguments(NULL, NULL) == E4_NULL);
    E4T_CHECK(failures, e4_validate_arguments(&sig, NULL) == E4_NULL);
    E4T_CHECK(failures, e4_validate_arguments(NULL, args) == E4_NULL);
    E4T_CHECK(failures, e4_validate_arguments(&bad_arg_sig, NULL) == E4_NULL);
    E4T_CHECK(failures, e4_validate_arguments(&bad_result_sig, NULL) == E4_NULL);
    E4T_CHECK(failures, e4_validate_arguments(&sig, args) == E4_OK);

    // Signature errors are reported before argument errors.
    e4_value mismatched[4];
    memcpy(mismatched, args, sizeof(mismatched));
    mismatched[0] = e4t_sample(e4t_other_tag(base[0]), &ref_target);
    E4T_CHECK(failures, e4_validate_arguments(&sig, mismatched) == E4_TYPE_MISMATCH);
    E4T_CHECK(failures, e4_validate_arguments(&bad_arg_sig, mismatched) == E4_BAD_TYPE);
    E4T_CHECK(failures, e4_validate_arguments(&bad_result_sig, mismatched) == E4_BAD_TYPE);
    e4t_expect_refusal(failures, &bad_arg_sig, mismatched, E4_BAD_TYPE);
    e4t_expect_refusal(failures, &bad_result_sig, mismatched, E4_BAD_TYPE);
    e4t_expect_refusal(failures, &sig, mismatched, E4_TYPE_MISMATCH);

    for (uint32_t p = 0u; p < E4T_PATTERN_COUNT; ++p) {
        uint32_t pattern[4];
        e4t_pattern(p, pattern);
        for (uint32_t slot = 0u; slot < E4T_ARGC; ++slot) {
            const uint32_t result = (p + slot) % (E4_TYPE_REF + 1u);
            e4_signature s = e4t_signature_from(pattern, result);
            e4_value valid[4];
            e4t_fill_args(&s, valid, &ref_target);
            E4T_CHECK(failures, e4_validate_arguments(&s, valid) == E4_OK);

            // Every other valid tag at this slot is a mismatch.
            for (uint32_t t = E4_TYPE_I32; t <= E4_TYPE_REF; ++t) {
                if (t == pattern[slot]) {
                    continue;
                }
                e4_value wrong[4];
                memcpy(wrong, valid, sizeof(wrong));
                wrong[slot] = e4t_sample(t, &ref_target);
                E4T_CHECK(failures, e4_validate_arguments(&s, wrong) == E4_TYPE_MISMATCH);
                e4t_expect_refusal(failures, &s, wrong, E4_TYPE_MISMATCH);
            }

            // VOID and unknown argument tags are unsupported at this slot.
            for (uint32_t b = 0u; b < E4T_BAD_ARG_COUNT; ++b) {
                e4_value unsupported[4];
                memcpy(unsupported, valid, sizeof(unsupported));
                unsupported[slot] = e4t_blank(e4t_bad_arg_tag(b));
                E4T_CHECK(failures, e4_validate_arguments(&s, unsupported) == E4_BAD_TYPE);
                e4t_expect_refusal(failures, &s, unsupported, E4_BAD_TYPE);
            }

            // The lowest failing index decides the status.
            for (uint32_t later = slot + 1u; later < E4T_ARGC; ++later) {
                const uint32_t bad_tag = e4t_bad_arg_tag(slot + later);
                e4_value mixed[4];

                memcpy(mixed, valid, sizeof(mixed));
                mixed[slot] = e4t_sample(e4t_other_tag(pattern[slot]), &ref_target);
                mixed[later] = e4t_blank(bad_tag);
                E4T_CHECK(failures, e4_validate_arguments(&s, mixed) == E4_TYPE_MISMATCH);
                e4t_expect_refusal(failures, &s, mixed, E4_TYPE_MISMATCH);

                memcpy(mixed, valid, sizeof(mixed));
                mixed[slot] = e4t_blank(bad_tag);
                mixed[later] = e4t_sample(e4t_other_tag(pattern[later]), &ref_target);
                E4T_CHECK(failures, e4_validate_arguments(&s, mixed) == E4_BAD_TYPE);
                e4t_expect_refusal(failures, &s, mixed, E4_BAD_TYPE);
            }
        }
    }

    E4T_CHECK(failures, ref_target == 41);
}

void e4t_test_null_and_output(uint32_t *failures)
{
    int ref_target = 7;
    uint32_t pattern[4];
    e4t_pattern(6u, pattern);

    // Every null subset of signature/args/callback wins over a valid call, a
    // bad signature, mismatched arguments and a VOID result, with or without
    // an output object.
    for (uint32_t variant = 0u; variant < 4u; ++variant) {
        e4_signature sig = e4t_signature_from(pattern, (variant == 3u) ? E4_TYPE_VOID : E4_TYPE_U32);
        e4_value args[4];
        e4t_fill_args(&sig, args, &ref_target);
        if (variant == 1u) {
            sig.args[2] = 0xFFFFFFFFu;
        }
        if (variant == 2u) {
            args[3] = e4t_sample(e4t_other_tag(pattern[3]), &ref_target);
        }
        unsigned char args_before[sizeof(args)];
        memcpy(args_before, args, sizeof(args_before));

        for (uint32_t mask = 1u; mask < 8u; ++mask) {
            for (uint32_t with_out = 0u; with_out < 2u; ++with_out) {
                const e4_signature *s = &sig;
                const e4_value *a = args;
                e4_callback cb = e4t_probe_callback;
                if ((mask & 1u) != 0u) {
                    s = NULL;
                }
                if ((mask & 2u) != 0u) {
                    a = NULL;
                }
                if ((mask & 4u) != 0u) {
                    cb = NULL;
                }

                e4t_probe probe;
                e4t_probe_reset(&probe, e4t_zero_payload());
                e4_value out;
                e4t_canary(&out);
                unsigned char out_before[sizeof(out)];
                memcpy(out_before, &out, sizeof(out_before));

                E4T_CHECK(failures, e4_invoke(s, a, cb, &probe, (with_out != 0u) ? &out : NULL) == E4_NULL);
                E4T_CHECK(failures, probe.calls == 0u);
                E4T_CHECK(failures, e4t_bytes_equal(&out, out_before, sizeof(out_before)));
                E4T_CHECK(failures, e4t_bytes_equal(args, args_before, sizeof(args_before)));
            }
        }
    }

    // Signature errors, then argument errors, precede the output requirement.
    e4_signature sig = e4t_signature_from(pattern, E4_TYPE_U32);
    e4_value args[4];
    e4t_fill_args(&sig, args, &ref_target);
    e4_signature bad_sig = sig;
    bad_sig.args[0] = E4_TYPE_VOID;
    e4_value mismatched[4];
    memcpy(mismatched, args, sizeof(mismatched));
    mismatched[1] = e4t_sample(e4t_other_tag(pattern[1]), &ref_target);
    e4_value unsupported[4];
    memcpy(unsupported, args, sizeof(unsupported));
    unsupported[2] = e4t_blank(6u);
    e4t_expect_refusal(failures, &bad_sig, mismatched, E4_BAD_TYPE);
    e4t_expect_refusal(failures, &bad_sig, args, E4_BAD_TYPE);
    e4t_expect_refusal(failures, &sig, mismatched, E4_TYPE_MISMATCH);
    e4t_expect_refusal(failures, &sig, unsupported, E4_BAD_TYPE);

    // A null output is refused last, and only for non-VOID results.
    for (uint32_t r = E4_TYPE_I32; r <= E4_TYPE_REF; ++r) {
        e4_signature sig_r = e4t_signature_from(pattern, r);
        e4_value args_r[4];
        e4t_fill_args(&sig_r, args_r, &ref_target);
        unsigned char args_before[sizeof(args_r)];
        memcpy(args_before, args_r, sizeof(args_before));
        const e4_value reply = e4t_reply_sample(r, &ref_target);

        e4t_probe probe;
        e4t_probe_reset(&probe, reply.payload);
        E4T_CHECK(failures, e4_invoke(&sig_r, args_r, e4t_probe_callback, &probe, NULL) == E4_NULL);
        E4T_CHECK(failures, probe.calls == 0u);
        E4T_CHECK(failures, e4t_bytes_equal(args_r, args_before, sizeof(args_before)));

        e4_value out;
        e4t_canary(&out);
        E4T_CHECK(failures, e4_invoke(&sig_r, args_r, e4t_probe_callback, &probe, &out) == E4_OK);
        E4T_CHECK(failures, probe.calls == 1u);
        E4T_CHECK(failures, out.type == r);
        E4T_CHECK(failures, e4t_payload_equal(r, &out.payload, &reply.payload));
    }

    // VOID result: null output is accepted and a nonnull output is untouched.
    e4_signature void_sig = e4t_signature_from(pattern, E4_TYPE_VOID);
    e4_value void_args[4];
    e4t_fill_args(&void_sig, void_args, &ref_target);
    {
        e4t_probe probe;
        e4t_probe_reset(&probe, e4t_reply_sample(E4_TYPE_VOID, &ref_target).payload);
        E4T_CHECK(failures, e4_invoke(&void_sig, void_args, e4t_probe_callback, &probe, NULL) == E4_OK);
        E4T_CHECK(failures, probe.calls == 1u);
        E4T_CHECK(failures, probe.context_seen == static_cast<void *>(&probe));
        for (uint32_t i = 0u; i < E4T_ARGC; ++i) {
            E4T_CHECK(failures, e4t_value_equal(&probe.seen[i], &void_args[i]));
        }

        e4t_probe_reset(&probe, e4t_reply_sample(E4_TYPE_U32, &ref_target).payload);
        e4_value out;
        e4t_canary(&out);
        unsigned char out_before[sizeof(out)];
        memcpy(out_before, &out, sizeof(out_before));
        E4T_CHECK(failures, e4_invoke(&void_sig, void_args, e4t_probe_callback, &probe, &out) == E4_OK);
        E4T_CHECK(failures, probe.calls == 1u);
        E4T_CHECK(failures, e4t_bytes_equal(&out, out_before, sizeof(out_before)));
    }

    // A null context is forwarded as null; a live context is forwarded as-is.
    {
        unsigned char args_before[sizeof(args)];
        memcpy(args_before, args, sizeof(args_before));
        e4_value out;
        e4t_canary(&out);
        E4T_CHECK(failures, e4_invoke(&sig, args, e4t_null_context_callback, NULL, &out) == E4_OK);
        E4T_CHECK(failures, out.type == E4_TYPE_U32);
        E4T_CHECK(failures, out.payload.u32 == E4T_NULL_CONTEXT_MARK);
        E4T_CHECK(failures, e4t_bytes_equal(args, args_before, sizeof(args_before)));

        int marker = 3;
        e4t_canary(&out);
        E4T_CHECK(failures, e4_invoke(&sig, args, e4t_null_context_callback, &marker, &out) == E4_OK);
        E4T_CHECK(failures, out.type == E4_TYPE_U32);
        E4T_CHECK(failures, out.payload.u32 == E4T_LIVE_CONTEXT_MARK);
        E4T_CHECK(failures, marker == 3);

        e4t_canary(&out);
        unsigned char out_before[sizeof(out)];
        memcpy(out_before, &out, sizeof(out_before));
        E4T_CHECK(failures, e4_invoke(&void_sig, void_args, e4t_null_context_callback, NULL, &out) == E4_OK);
        E4T_CHECK(failures, e4t_bytes_equal(&out, out_before, sizeof(out_before)));
        E4T_CHECK(failures, e4_invoke(&void_sig, void_args, e4t_null_context_callback, NULL, NULL) == E4_OK);
    }

    E4T_CHECK(failures, ref_target == 7);
}

void e4t_test_result_matrix(uint32_t *failures)
{
    int arg_target = 101;
    int reply_target = 202;

    for (uint32_t p = 0u; p < E4T_PATTERN_COUNT; ++p) {
        uint32_t pattern[4];
        e4t_pattern(p, pattern);
        for (uint32_t r = E4_TYPE_VOID; r <= E4_TYPE_REF; ++r) {
            e4_signature sig = e4t_signature_from(pattern, r);
            e4_value args[4];
            e4t_fill_args(&sig, args, &arg_target);
            unsigned char args_before[sizeof(args)];
            memcpy(args_before, args, sizeof(args_before));
            const e4_value reply = e4t_reply_sample(r, &reply_target);

            e4t_probe probe;
            e4t_probe_reset(&probe, reply.payload);
            e4_value out;
            e4t_canary(&out);
            unsigned char out_before[sizeof(out)];
            memcpy(out_before, &out, sizeof(out_before));

            E4T_CHECK(failures, e4_validate_arguments(&sig, args) == E4_OK);
            E4T_CHECK(failures, e4_invoke(&sig, args, e4t_probe_callback, &probe, &out) == E4_OK);
            E4T_CHECK(failures, probe.calls == 1u);
            E4T_CHECK(failures, probe.context_seen == static_cast<void *>(&probe));
            for (uint32_t i = 0u; i < E4T_ARGC; ++i) {
                E4T_CHECK(failures, e4t_value_equal(&probe.seen[i], &args[i]));
            }
            E4T_CHECK(failures, e4t_bytes_equal(args, args_before, sizeof(args_before)));

            if (r == E4_TYPE_VOID) {
                E4T_CHECK(failures, e4t_bytes_equal(&out, out_before, sizeof(out_before)));
            } else {
                E4T_CHECK(failures, out.type == r);
                E4T_CHECK(failures, e4t_payload_equal(r, &out.payload, &reply.payload));
            }
        }
    }

    E4T_CHECK(failures, arg_target == 101);
    E4T_CHECK(failures, reply_target == 202);
}

void e4t_test_witnesses(uint32_t *failures)
{
    int ref_a = 11;
    int ref_b = 22;

    for (uint32_t tag = E4_TYPE_I32; tag <= E4_TYPE_REF; ++tag) {
        e4_value witnesses[E4T_MAX_WITNESSES];
        const uint32_t count = e4t_witnesses(tag, witnesses, &ref_a, &ref_b);
        E4T_CHECK(failures, count > 0u);
        const uint32_t pattern[4] = {tag, tag, tag, tag};
        const e4_signature sig = e4t_signature_from(pattern, tag);

        for (uint32_t w = 0u; w < count; ++w) {
            for (uint32_t slot = 0u; slot < E4T_ARGC; ++slot) {
                e4_value args[4];
                for (uint32_t j = 0u; j < E4T_ARGC; ++j) {
                    args[j] = witnesses[(w + 1u + j) % count];
                }
                args[slot] = witnesses[w];
                unsigned char args_before[sizeof(args)];
                memcpy(args_before, args, sizeof(args_before));

                e4t_probe probe;
                e4t_probe_reset(&probe, witnesses[w].payload);
                e4_value out;
                e4t_canary(&out);

                E4T_CHECK(failures, e4_validate_arguments(&sig, args) == E4_OK);
                E4T_CHECK(failures, e4_invoke(&sig, args, e4t_probe_callback, &probe, &out) == E4_OK);
                E4T_CHECK(failures, probe.calls == 1u);
                E4T_CHECK(failures, probe.context_seen == static_cast<void *>(&probe));
                for (uint32_t j = 0u; j < E4T_ARGC; ++j) {
                    E4T_CHECK(failures, e4t_value_equal(&probe.seen[j], &args[j]));
                }
                E4T_CHECK(failures, e4t_value_equal(&probe.seen[slot], &witnesses[w]));
                E4T_CHECK(failures, out.type == tag);
                E4T_CHECK(failures, e4t_payload_equal(tag, &out.payload, &witnesses[w].payload));
                if (tag == E4_TYPE_F32) {
                    E4T_CHECK(failures, e4t_f32_bits(&out.payload) == e4t_f32_bits(&witnesses[w].payload));
                    E4T_CHECK(failures, e4t_f32_bits(&probe.seen[slot].payload) ==
                                            e4t_f32_bits(&witnesses[w].payload));
                }
                E4T_CHECK(failures, e4t_bytes_equal(args, args_before, sizeof(args_before)));
            }
        }
    }

    // Literal spot checks of exact extrema, non-normalized BOOL32 and f32 bits.
    {
        const uint32_t i32_pattern[4] = {E4_TYPE_I32, E4_TYPE_U32, E4_TYPE_BOOL32, E4_TYPE_REF};
        const e4_signature sig = e4t_signature_from(i32_pattern, E4_TYPE_F32);
        e4_value args[4];
        args[0] = e4t_i32(std::numeric_limits<int32_t>::min());
        args[1] = e4t_u32(std::numeric_limits<uint32_t>::max());
        args[2] = e4t_b32(256);
        args[3] = e4t_ref(&ref_a);

        const uint32_t f32_bits[4] = {0x80000000u, 0x7FC12345u, 0x7FA5A5A5u, 0x00000001u};
        for (uint32_t i = 0u; i < 4u; ++i) {
            const e4_value reply = e4t_f32_from_bits(f32_bits[i]);
            e4t_probe probe;
            e4t_probe_reset(&probe, reply.payload);
            e4_value out;
            e4t_canary(&out);
            E4T_CHECK(failures, e4_invoke(&sig, args, e4t_probe_callback, &probe, &out) == E4_OK);
            E4T_CHECK(failures, probe.calls == 1u);
            E4T_CHECK(failures, out.type == E4_TYPE_F32);
            E4T_CHECK(failures, e4t_f32_bits(&out.payload) == f32_bits[i]);
            E4T_CHECK(failures, probe.seen[0].payload.i32 == std::numeric_limits<int32_t>::min());
            E4T_CHECK(failures, probe.seen[1].payload.u32 == 0xFFFFFFFFu);
            E4T_CHECK(failures, probe.seen[2].payload.b32 == 256);
            E4T_CHECK(failures, probe.seen[3].payload.ref == static_cast<void *>(&ref_a));
        }

        const int32_t bool_values[6] = {-1, 256, std::numeric_limits<int32_t>::min(),
                                        std::numeric_limits<int32_t>::max(), 0, 1};
        e4_signature bool_sig = sig;
        bool_sig.result_type = E4_TYPE_BOOL32;
        for (uint32_t i = 0u; i < 6u; ++i) {
            e4t_probe probe;
            e4t_probe_reset(&probe, e4t_b32(bool_values[i]).payload);
            e4_value out;
            e4t_canary(&out);
            E4T_CHECK(failures, e4_invoke(&bool_sig, args, e4t_probe_callback, &probe, &out) == E4_OK);
            E4T_CHECK(failures, out.type == E4_TYPE_BOOL32);
            E4T_CHECK(failures, out.payload.b32 == bool_values[i]);
        }

        e4_signature ref_sig = sig;
        ref_sig.result_type = E4_TYPE_REF;
        e4t_probe probe;
        e4t_probe_reset(&probe, e4t_ref(&ref_b).payload);
        e4_value out;
        e4t_canary(&out);
        E4T_CHECK(failures, e4_invoke(&ref_sig, args, e4t_probe_callback, &probe, &out) == E4_OK);
        E4T_CHECK(failures, out.type == E4_TYPE_REF);
        E4T_CHECK(failures, out.payload.ref == static_cast<void *>(&ref_b));
        E4T_CHECK(failures, *static_cast<int *>(out.payload.ref) == 22);

        e4t_probe_reset(&probe, e4t_ref(NULL).payload);
        e4t_canary(&out);
        E4T_CHECK(failures, e4_invoke(&ref_sig, args, e4t_probe_callback, &probe, &out) == E4_OK);
        E4T_CHECK(failures, out.type == E4_TYPE_REF);
        E4T_CHECK(failures, out.payload.ref == NULL);
    }

    E4T_CHECK(failures, ref_a == 11);
    E4T_CHECK(failures, ref_b == 22);
}

} // namespace

extern "C" uint32_t e4_run_tests(void)
{
    uint32_t failures = 0u;
    e4t_test_signature_validation(&failures);
    e4t_test_argument_validation(&failures);
    e4t_test_null_and_output(&failures);
    e4t_test_result_matrix(&failures);
    e4t_test_witnesses(&failures);
    return failures;
}

#ifdef GAMEPLAY_GRAPH_EFFECT4_STANDALONE_TEST_MAIN
int main(void)
{
    return (e4_run_tests() == 0u) ? 0 : 1;
}
#endif
