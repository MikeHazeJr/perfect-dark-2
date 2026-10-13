/*
 * gameplay_graph_effect2.cpp - fixed-arity-two callback gateway (C++11, C linkage).
 *
 * Handoff:
 *  - Authored only. Not compiled, run or ABI-qualified.
 *  - Null checks run left to right; all of them yield E2_NULL.
 *  - A missing non-VOID output refuses with E2_NULL, after full validation.
 *  - On non-VOID success, the tag is stored first and then the payload, and
 *    both stores happen after the callback returns.
 *  - No heap, exceptions, global state, I/O or floating-point environment use.
 */
#include "gameplay_graph_effect2.h"

#include <cstring>

namespace {

bool e2_argument_tag_supported(uint32_t tag) noexcept {
    return tag >= E2_TYPE_I32 && tag <= E2_TYPE_REF;
}

bool e2_result_tag_supported(uint32_t tag) noexcept {
    return tag <= E2_TYPE_REF;
}

}  // namespace

extern "C" e2_status e2_validate_signature(const e2_signature *signature) noexcept {
    if (signature == nullptr) {
        return E2_NULL;
    }
    if (!e2_argument_tag_supported(signature->args[0])) {
        return E2_BAD_TYPE;
    }
    if (!e2_argument_tag_supported(signature->args[1])) {
        return E2_BAD_TYPE;
    }
    if (!e2_result_tag_supported(signature->result_type)) {
        return E2_BAD_TYPE;
    }
    return E2_OK;
}

extern "C" e2_status e2_validate_arguments(const e2_signature *signature,
                                           const e2_value args[2]) noexcept {
    if (signature == nullptr || args == nullptr) {
        return E2_NULL;
    }
    const e2_status signature_status = e2_validate_signature(signature);
    if (signature_status != E2_OK) {
        return signature_status;
    }
    for (int index = 0; index < 2; ++index) {
        const uint32_t actual = args[index].type;
        if (!e2_argument_tag_supported(actual)) {
            return E2_BAD_TYPE;
        }
        if (actual != signature->args[index]) {
            return E2_TYPE_MISMATCH;
        }
    }
    return E2_OK;
}

extern "C" e2_status e2_invoke(const e2_signature *signature, const e2_value args[2],
                               e2_callback callback, void *context,
                               e2_value *out) noexcept {
    if (signature == nullptr || args == nullptr || callback == nullptr) {
        return E2_NULL;
    }
    const e2_status status = e2_validate_arguments(signature, args);
    if (status != E2_OK) {
        return status;
    }
    // Cached before the callback; never reread afterward.
    const uint32_t result_type = signature->result_type;
    if (result_type != E2_TYPE_VOID && out == nullptr) {
        return E2_NULL;
    }
    // Sole effect. No output prewrite: the callback may observe the prior output.
    const e2_payload returned = callback(context, args);
    if (result_type != E2_TYPE_VOID) {
        out->type = result_type;
        std::memcpy(&out->payload, &returned, sizeof(e2_payload));
    }
    return E2_OK;
}
