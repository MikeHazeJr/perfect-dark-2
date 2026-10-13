/*
 * gameplay_graph_effect1.h - anonymous synchronous fixed-arity-one typed
 * callback gateway. C99-compatible header; C++11 implementation; C linkage;
 * C++ declarations are noexcept.
 *
 * This is an independent arity-one boundary. Its names, uint32_t status and
 * tag domains, and value/signature layouts belong to this header alone. They
 * are not aliases of any other arity.
 *
 * Status (e1_status, uint32_t):
 *   E1_OK 0, E1_NULL 1, E1_BAD_TYPE 2, E1_TYPE_MISMATCH 3.
 * Type tags (uint32_t):
 *   E1_TYPE_VOID 0 (valid only as a result tag), E1_TYPE_I32 1,
 *   E1_TYPE_U32 2, E1_TYPE_F32 3, E1_TYPE_BOOL32 4, E1_TYPE_REF 5.
 *   Every other numeric tag is unsupported.
 *
 * Payload semantics:
 *   - REF is a borrowed pointer and may be null. The gateway never
 *     dereferences it and never treats it as owned or serialized identity.
 *   - BOOL32 carries every int32_t pattern. There is no normalization.
 *   - F32 receives no arithmetic, coercion or canonicalization.
 *
 * Precedence:
 *   e1_validate_signature:
 *     null signature, then args[0] tag, then result_type.
 *   e1_validate_arguments:
 *     null signature or args, then the whole signature (as above), then the
 *     actual args[0].type (unsupported tag -> BAD_TYPE), then
 *     declared/actual mismatch (-> TYPE_MISMATCH).
 *   e1_invoke:
 *     null signature, args or callback; then e1_validate_arguments order;
 *     then a non-VOID result with null out -> E1_NULL. Context may be null.
 *
 * e1_invoke effects:
 *   - Failure: the gateway writes nothing and calls the callback zero times.
 *   - Success: the callback is called exactly once, synchronously, with the
 *     ORIGINAL context and args pointers. The declared result tag is cached
 *     before the call. The gateway never prewrites out.
 *   - Non-VOID result: only after the callback returns, the gateway stores
 *     the cached tag in out->type. It then copies the returned union into
 *     out->payload with memcpy of sizeof(e1_payload). No other out bytes
 *     are assigned.
 *     Copied inactive or padding union bytes are opaque and carry no meaning.
 *   - VOID result: the returned payload is ignored and out (null or not) is
 *     not written by the gateway.
 *   - Nothing happens after the callback returns: no check, refusal, retry,
 *     fallback or rollback.
 *
 * Unchecked caller preconditions (not validated; the gateway does not try
 * to prove pointer validity or aliasing):
 *   - Every nonnull pointer designates a live, initialized object. args
 *     designates one e1_value.
 *   - signature and args stay unmodified for the whole call, including while
 *     the callback runs.
 *   - out does not overlap signature or args.
 *   - callback is a valid C-compatible function that is total and returns
 *     normally. It must not throw or unwind through the gateway.
 *   - callback's returned active payload belongs to the declared result
 *     domain. Any payload is acceptable for VOID.
 *
 * Old-output observation is expressly permitted:
 *   - context may be out itself, or may contain a pointer to out.
 *   - The callback may read the prior initialized output (old tag and active
 *     payload) while it runs. This does not violate the out/signature/args
 *     nonoverlap precondition.
 *   - Writes the callback makes through its own context or borrowed
 *     references are caller-owned effects. The "untouched" guarantees for
 *     refusal and VOID describe gateway writes only.
 *
 * Handoff: authored source only. Build, ABI (including returning the union
 * by value across C/C++), layout, NaN transport and runtime behavior are
 * unqualified. No integration or gameplay behavior is provided.
 */
#ifndef GAMEPLAY_GRAPH_EFFECT1_H
#define GAMEPLAY_GRAPH_EFFECT1_H

#include <stdint.h>

#if defined(__cplusplus)
#define E1_NOEXCEPT noexcept
extern "C" {
#else
#define E1_NOEXCEPT
#endif

typedef uint32_t e1_status;

#define E1_OK ((e1_status)0u)
#define E1_NULL ((e1_status)1u)
#define E1_BAD_TYPE ((e1_status)2u)
#define E1_TYPE_MISMATCH ((e1_status)3u)

#define E1_TYPE_VOID ((uint32_t)0u)
#define E1_TYPE_I32 ((uint32_t)1u)
#define E1_TYPE_U32 ((uint32_t)2u)
#define E1_TYPE_F32 ((uint32_t)3u)
#define E1_TYPE_BOOL32 ((uint32_t)4u)
#define E1_TYPE_REF ((uint32_t)5u)

typedef union e1_payload {
    int32_t i32;
    uint32_t u32;
    float f32;
    int32_t b32;
    void *ref;
} e1_payload;

typedef struct e1_value {
    uint32_t type;
    e1_payload payload;
} e1_value;

typedef struct e1_signature {
    uint32_t args[1];
    uint32_t result_type;
} e1_signature;

/* Plain C-compatible callback pointer. Total and nonthrowing by caller
 * obligation. */
typedef e1_payload (*e1_callback)(void *context, const e1_value args[1]);

e1_status e1_validate_signature(const e1_signature *signature) E1_NOEXCEPT;

e1_status e1_validate_arguments(const e1_signature *signature,
                                const e1_value args[1]) E1_NOEXCEPT;

e1_status e1_invoke(const e1_signature *signature,
                    const e1_value args[1],
                    e1_callback callback,
                    void *context,
                    e1_value *out) E1_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#endif /* GAMEPLAY_GRAPH_EFFECT1_H */
