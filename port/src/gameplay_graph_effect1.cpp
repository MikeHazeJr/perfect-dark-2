// gameplay_graph_effect1.cpp - arity-one typed callback gateway (C++11).
//
// Handoff: authored only, not compiled or run. ABI, layout and runtime
// behavior are unqualified. There are no globals, allocation, exceptions,
// I/O or platform guards.

#include "gameplay_graph_effect1.h"

#include <cstring>

namespace {

bool e1_arg_tag_supported(uint32_t tag) noexcept
{
    return tag >= E1_TYPE_I32 && tag <= E1_TYPE_REF;
}

bool e1_result_tag_supported(uint32_t tag) noexcept
{
    return tag <= E1_TYPE_REF;
}

} // namespace

extern "C" e1_status e1_validate_signature(const e1_signature *signature) noexcept
{
    if (signature == nullptr) {
        return E1_NULL;
    }
    if (!e1_arg_tag_supported(signature->args[0])) {
        return E1_BAD_TYPE;
    }
    if (!e1_result_tag_supported(signature->result_type)) {
        return E1_BAD_TYPE;
    }
    return E1_OK;
}

extern "C" e1_status e1_validate_arguments(const e1_signature *signature,
                                           const e1_value args[1]) noexcept
{
    if (signature == nullptr || args == nullptr) {
        return E1_NULL;
    }
    const e1_status signature_status = e1_validate_signature(signature);
    if (signature_status != E1_OK) {
        return signature_status;
    }
    const uint32_t actual = args[0].type;
    if (!e1_arg_tag_supported(actual)) {
        return E1_BAD_TYPE;
    }
    if (actual != signature->args[0]) {
        return E1_TYPE_MISMATCH;
    }
    return E1_OK;
}

extern "C" e1_status e1_invoke(const e1_signature *signature,
                               const e1_value args[1],
                               e1_callback callback,
                               void *context,
                               e1_value *out) noexcept
{
    if (signature == nullptr || args == nullptr || callback == nullptr) {
        return E1_NULL;
    }
    const e1_status argument_status = e1_validate_arguments(signature, args);
    if (argument_status != E1_OK) {
        return argument_status;
    }
    // Cached before the callback; never reread afterward.
    const uint32_t result_type = signature->result_type;
    if (result_type != E1_TYPE_VOID && out == nullptr) {
        return E1_NULL;
    }

    // Sole effect: one synchronous call with the original pointers. Out is
    // not prewritten, so the callback may observe the old output.
    const e1_payload returned = callback(context, args);

    if (result_type != E1_TYPE_VOID) {
        out->type = result_type;
        std::memcpy(&out->payload, &returned, sizeof(e1_payload));
    }
    return E1_OK;
}
