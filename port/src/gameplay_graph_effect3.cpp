// gameplay_graph_effect3.cpp
//
// Fixed-arity-three callback gateway implementation (C++11, C linkage
// exports).
// Authored source only; it has not been compiled or executed. The unchecked
// caller preconditions are documented in gameplay_graph_effect3.h.
// This file uses no global, static or thread-local objects, and no
// allocation, exceptions, I/O, clock, RNG or floating-point environment
// changes.

#include "gameplay_graph_effect3.h"

#include <cstring>

static bool e3_is_argument_tag(uint32_t tag) noexcept
{
    return tag >= E3_TYPE_I32 && tag <= E3_TYPE_REF;
}

static bool e3_is_result_tag(uint32_t tag) noexcept
{
    return tag == E3_TYPE_VOID || e3_is_argument_tag(tag);
}

extern "C" e3_status e3_validate_signature(const e3_signature *signature) noexcept
{
    if (signature == nullptr) {
        return E3_NULL;
    }
    for (unsigned index = 0u; index < 3u; ++index) {
        if (!e3_is_argument_tag(signature->args[index])) {
            return E3_BAD_TYPE;
        }
    }
    if (!e3_is_result_tag(signature->result_type)) {
        return E3_BAD_TYPE;
    }
    return E3_OK;
}

extern "C" e3_status e3_validate_arguments(const e3_signature *signature,
                                           const e3_value args[3]) noexcept
{
    if (signature == nullptr || args == nullptr) {
        return E3_NULL;
    }
    // Validate the whole signature before reading any actual tag.
    const e3_status signature_status = e3_validate_signature(signature);
    if (signature_status != E3_OK) {
        return signature_status;
    }
    for (unsigned index = 0u; index < 3u; ++index) {
        const uint32_t actual = args[index].type;
        if (!e3_is_argument_tag(actual)) {
            return E3_BAD_TYPE;
        }
        if (actual != signature->args[index]) {
            return E3_TYPE_MISMATCH;
        }
    }
    return E3_OK;
}

extern "C" e3_status e3_invoke(const e3_signature *signature,
                               const e3_value args[3],
                               e3_callback callback,
                               void *context,
                               e3_value *out) noexcept
{
    if (signature == nullptr || args == nullptr || callback == nullptr) {
        return E3_NULL;
    }
    const e3_status status = e3_validate_arguments(signature, args);
    if (status != E3_OK) {
        return status;
    }
    // Cache the declared result tag before the callback. Nothing after the
    // callback reads the signature again.
    const uint32_t result_type = signature->result_type;
    if (result_type != E3_TYPE_VOID && out == nullptr) {
        return E3_NULL;
    }

    // Sole effect: exactly one synchronous call with the original pointers.
    const e3_payload returned = callback(context, args);

    // From here on, only the cached VOID/non-VOID output selection remains.
    if (result_type != E3_TYPE_VOID) {
        // Write only the tag and the payload union, never the whole
        // e3_value, so padding is not written. Union bytes beyond the
        // active member are copied as-is and are not defined by this
        // gateway.
        std::memcpy(&out->type, &result_type, sizeof result_type);
        std::memcpy(&out->payload, &returned, sizeof returned);
    }
    return E3_OK;
}
