#ifndef GAMEPLAY_GRAPH_EFFECT3_H
#define GAMEPLAY_GRAPH_EFFECT3_H

/*
 * Anonymous synchronous fixed-arity-three typed callback gateway.
 *
 * C99-compatible header. The implementation is C++11 and exports every
 * function with C linkage. In C++ translation units every function is
 * declared noexcept.
 *
 * This header defines no objects. It depends on no other arity.
 */

#include <stdint.h>

#ifdef __cplusplus
#define E3_NOEXCEPT noexcept
extern "C" {
#else
#define E3_NOEXCEPT
#endif

/* Status codes. */
typedef uint32_t e3_status;

#define E3_OK            ((e3_status)0u)
#define E3_NULL          ((e3_status)1u)
#define E3_BAD_TYPE      ((e3_status)2u)
#define E3_TYPE_MISMATCH ((e3_status)3u)

/*
 * Type tags. E3_TYPE_VOID is valid only as a result tag. Every other
 * numeric tag value is unsupported.
 */
#define E3_TYPE_VOID   ((uint32_t)0u)
#define E3_TYPE_I32    ((uint32_t)1u)
#define E3_TYPE_U32    ((uint32_t)2u)
#define E3_TYPE_F32    ((uint32_t)3u)
#define E3_TYPE_BOOL32 ((uint32_t)4u)
#define E3_TYPE_REF    ((uint32_t)5u)

/*
 * Payload union.
 *
 * - b32 (BOOL32) carries every int32_t bit pattern; no normalization.
 * - f32 (F32) is carried without arithmetic, coercion or
 *   canonicalization.
 * - ref (REF) is a borrowed reference. It may be null. The gateway never
 *   dereferences it and never interprets it as owned or serialized
 *   identity.
 */
typedef union e3_payload {
    int32_t  i32;
    uint32_t u32;
    float    f32;
    int32_t  b32;
    void    *ref;
} e3_payload;

/* Tagged value. */
typedef struct e3_value {
    uint32_t   type;
    e3_payload payload;
} e3_value;

/* Fixed-arity-three signature. */
typedef struct e3_signature {
    uint32_t args[3];
    uint32_t result_type;
} e3_signature;

/*
 * Callback. Receives the ORIGINAL context and args pointer passed to
 * e3_invoke. Returns the payload for the declared result type; the
 * returned payload is ignored when the declared result type is
 * E3_TYPE_VOID.
 */
typedef e3_payload (*e3_callback)(void *context, const e3_value args[3]);

/*
 * Validate a signature.
 *
 * Precedence:
 *   1. signature null                         -> E3_NULL
 *   2. args[0], args[1], args[2] in order:
 *      tag not in I32..REF                    -> E3_BAD_TYPE
 *   3. result_type not in VOID..REF           -> E3_BAD_TYPE
 *   otherwise                                 -> E3_OK
 */
e3_status e3_validate_signature(const e3_signature *signature) E3_NOEXCEPT;

/*
 * Validate actual arguments against a signature.
 *
 * Precedence:
 *   1. signature or args null                 -> E3_NULL
 *   2. whole signature, as e3_validate_signature
 *   3. for index 0, 1, 2 in increasing order:
 *      actual tag not in I32..REF             -> E3_BAD_TYPE
 *      actual tag != declared tag             -> E3_TYPE_MISMATCH
 *      The lowest defective index decides the status.
 *   otherwise                                 -> E3_OK
 */
e3_status e3_validate_arguments(const e3_signature *signature,
                                const e3_value args[3]) E3_NOEXCEPT;

/*
 * Validate, then invoke the callback exactly once.
 *
 * Precedence:
 *   1. signature, args or callback null       -> E3_NULL
 *   2. signature and arguments, as e3_validate_arguments
 *   3. declared result not VOID and out null  -> E3_NULL
 *   otherwise invoke                          -> E3_OK
 *
 * context may be null.
 *
 * On any refusal the callback is invoked zero times and the gateway
 * writes no byte of *out.
 *
 * On success the declared result tag is cached before the callback is
 * invoked, and the callback is invoked exactly once with the ORIGINAL
 * context and args pointer. The callback is the sole effect of the call.
 * After it returns there is no further validation, refusal, fallback,
 * retry or rollback.
 *   - Non-VOID result: the gateway stores the cached declared result tag
 *     in out->type and copies the returned union into out->payload by
 *     memcpy, preserving the active payload representation. Padding is
 *     neither written nor defined.
 *   - VOID result: the returned payload is ignored; the gateway itself
 *     never writes out, whether out is null or nonnull.
 *
 * The gateway's own guarantee of not writing out (on refusal and for
 * VOID) concerns the gateway only. It does not constrain or describe
 * effects of the caller-supplied callback, which may perform arbitrary
 * effects through its context or otherwise. This is a statement of the
 * gateway's behavior, not an alias check, rollback or late validation.
 *
 * Unchecked caller preconditions (not verified by the gateway):
 *   - every nonnull pointer argument designates initialized, live
 *     objects for the duration of the call;
 *   - *signature and args[0..2] are not modified during the call;
 *   - out does not overlap *signature or args[0..2];
 *   - the callback is total and does not throw or otherwise unwind
 *     across the gateway;
 *   - for a non-VOID result, the callback's returned payload has the
 *     active member belonging to the declared result domain.
 * The gateway does not attempt to prove pointer validity or aliasing.
 */
e3_status e3_invoke(const e3_signature *signature,
                    const e3_value args[3],
                    e3_callback callback,
                    void *context,
                    e3_value *out) E3_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* GAMEPLAY_GRAPH_EFFECT3_H */
