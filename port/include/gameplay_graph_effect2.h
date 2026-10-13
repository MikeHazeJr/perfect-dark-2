/*
 * gameplay_graph_effect2.h - fixed-arity-two synchronous typed callback gateway.
 *
 * Independent boundary. The e2 names, the uint32_t status and type-tag domains,
 * and the value/signature/callback schema below are self-contained. They do not
 * alias or depend on any other arity. No gameplay or application behavior is
 * defined here; the gateway validates, invokes one callback and stores a result.
 *
 * This header is C99-compatible. The implementation is C++11 with C linkage,
 * and in C++ every export is declared noexcept. The callback type is a plain
 * C-compatible function pointer.
 *
 * Unchecked caller preconditions. The gateway does not attempt to prove
 * arbitrary pointer validity, lifetime or aliasing:
 *  - Every nonnull pointer argument designates a live, initialized object.
 *    args designates two initialized e2_value objects.
 *  - *signature and both args elements stay unmodified for the whole call,
 *    including while the callback runs.
 *  - *out does not overlap *signature or either args element.
 *  - The callback is total and does not throw. A throw that reaches a
 *    noexcept export terminates the program.
 *  - For non-VOID results, the active member of the returned payload belongs
 *    to the declared result type domain.
 *
 * Observing the old output is permitted. context may itself be out, or may
 * contain a pointer to out, and the callback may read the prior initialized
 * output while it runs. The gateway never prewrites the output tag or payload,
 * even on success, so the callback sees the prior bytes. This does not violate
 * the nonoverlap precondition, which concerns only signature and args.
 *
 * Writes the callback makes through its own context are caller-owned effects.
 * The "output untouched" guarantees for refusals and VOID results describe
 * gateway writes only.
 *
 * Borrowed references (E2_TYPE_REF) may be null. The gateway never
 * dereferences them and never treats them as owned or serializable
 * identities. BOOL32 carries every int32_t pattern without normalization.
 * F32 is transported without arithmetic, coercion or canonicalization.
 */
#ifndef GAMEPLAY_GRAPH_EFFECT2_H
#define GAMEPLAY_GRAPH_EFFECT2_H

#include <stdint.h>

#ifdef __cplusplus
#define E2_NOEXCEPT noexcept
extern "C" {
#else
#define E2_NOEXCEPT
#endif

typedef uint32_t e2_status;

#define E2_OK ((e2_status)0u)
#define E2_NULL ((e2_status)1u)
#define E2_BAD_TYPE ((e2_status)2u)
#define E2_TYPE_MISMATCH ((e2_status)3u)

/* VOID is valid only as a result tag. Numeric tags above REF are unsupported. */
#define E2_TYPE_VOID ((uint32_t)0u)
#define E2_TYPE_I32 ((uint32_t)1u)
#define E2_TYPE_U32 ((uint32_t)2u)
#define E2_TYPE_F32 ((uint32_t)3u)
#define E2_TYPE_BOOL32 ((uint32_t)4u)
#define E2_TYPE_REF ((uint32_t)5u)

typedef union e2_payload {
    int32_t i32;
    uint32_t u32;
    float f32;
    int32_t b32;
    void *ref;
} e2_payload;

typedef struct e2_value {
    uint32_t type;
    e2_payload payload;
} e2_value;

typedef struct e2_signature {
    uint32_t args[2];
    uint32_t result_type;
} e2_signature;

typedef e2_payload (*e2_callback)(void *context, const e2_value args[2]);

/*
 * Checks are made in this order:
 *  1. A null signature returns E2_NULL.
 *  2. args[0], then args[1], then result_type. The first unsupported tag
 *     returns E2_BAD_TYPE.
 * Argument tags must be I32..REF. The result tag may also be VOID.
 */
e2_status e2_validate_signature(const e2_signature *signature) E2_NOEXCEPT;

/*
 * Checks are made in this order:
 *  1. A null signature or null args returns E2_NULL.
 *  2. The whole signature is validated, as in e2_validate_signature.
 *  3. Index 0, then index 1. An unsupported actual tag returns E2_BAD_TYPE.
 *     Otherwise, an actual tag that differs from the declared tag returns
 *     E2_TYPE_MISMATCH.
 * The earliest failing index wins, even if a later index has a different
 * defect.
 */
e2_status e2_validate_arguments(const e2_signature *signature,
                                const e2_value args[2]) E2_NOEXCEPT;

/*
 * Checks are made in this order:
 *  1. A null signature, args or callback returns E2_NULL.
 *  2. e2_validate_arguments precedence applies.
 *  3. A null out for a non-VOID result returns E2_NULL.
 * context may be null.
 *
 * On any failure, the callback is not invoked and *out is not written.
 *
 * On success, the callback is invoked exactly once with the original context
 * and args pointers. The declared result tag is cached before the call and is
 * not reread afterward.
 *
 * For a non-VOID result, after the callback returns, the gateway stores the
 * cached tag in out->type. It then copies the returned union into out->payload
 * by memcpy of sizeof(e2_payload). Inactive or padding bytes copied this way
 * are opaque and carry no defined meaning.
 *
 * A VOID result ignores the returned payload and leaves out untouched, whether
 * out is null or nonnull.
 *
 * Nothing is validated, refused, retried or rolled back after the callback.
 */
e2_status e2_invoke(const e2_signature *signature, const e2_value args[2],
                    e2_callback callback, void *context,
                    e2_value *out) E2_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* GAMEPLAY_GRAPH_EFFECT2_H */
