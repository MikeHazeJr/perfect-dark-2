#ifndef GAMEPLAY_GRAPH_EFFECT4_H
#define GAMEPLAY_GRAPH_EFFECT4_H

/*
 * Synchronous four-argument typed effect gateway.
 *
 * The caller-supplied callback is the only effect. This component performs
 * pointer and type-tag validation only; it never decodes functions or
 * addresses from data, never converts payload values and never allocates.
 *
 * Caller preconditions (not checked):
 *  - all non-null objects are initialized, live for the call and do not
 *    overlap the output object;
 *  - the callback is total, does not throw, returns a payload within the
 *    declared result domain and does not mutate the input objects.
 *
 * BOOL32 carries any signed 32-bit pattern (truth is nonzero, interpreted by
 * the caller). F32 payloads are copied by object representation. REF is a
 * borrowed pointer (null allowed), not an owned or serializable identity.
 * Success defines output fields only, never padding bytes.
 */

#include <stdint.h>

#ifdef __cplusplus
#define E4_NOEXCEPT noexcept
#else
#define E4_NOEXCEPT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t e4_status;

#define E4_OK ((e4_status)0u)
#define E4_NULL ((e4_status)1u)
#define E4_BAD_TYPE ((e4_status)2u)
#define E4_TYPE_MISMATCH ((e4_status)3u)

/* VOID is valid only as a result type; tags 1..5 are argument kinds. */
#define E4_TYPE_VOID ((uint32_t)0u)
#define E4_TYPE_I32 ((uint32_t)1u)
#define E4_TYPE_U32 ((uint32_t)2u)
#define E4_TYPE_F32 ((uint32_t)3u)
#define E4_TYPE_BOOL32 ((uint32_t)4u)
#define E4_TYPE_REF ((uint32_t)5u)

typedef union e4_payload {
    int32_t i32;
    uint32_t u32;
    float f32;
    int32_t b32;
    void *ref;
} e4_payload;

typedef struct e4_value {
    uint32_t type;
    e4_payload payload;
} e4_value;

typedef struct e4_signature {
    uint32_t args[4];
    uint32_t result_type;
} e4_signature;

typedef e4_payload (*e4_callback)(void *context, const e4_value args[4]);

/* Null, then argument tags in index order, then the result tag. */
e4_status e4_validate_signature(const e4_signature *signature) E4_NOEXCEPT;

/*
 * Null signature/args, then signature validation, then for each index in
 * increasing order: tag validity (BAD_TYPE) then exact match (TYPE_MISMATCH).
 */
e4_status e4_validate_arguments(const e4_signature *signature,
                                const e4_value args[4]) E4_NOEXCEPT;

/*
 * Null signature/args/callback, then signature and argument validation, then
 * out != null only for a non-VOID result. Context may be null. Every refusal
 * leaves *out untouched and does not call the callback. On success the
 * callback is called exactly once with the same context and args pointer; a
 * non-VOID result stores the declared type and the returned payload. A VOID
 * result leaves out (null or not) untouched.
 */
e4_status e4_invoke(const e4_signature *signature,
                    const e4_value args[4],
                    e4_callback callback,
                    void *context,
                    e4_value *out) E4_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif
