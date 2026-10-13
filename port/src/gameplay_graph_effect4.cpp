#include "gameplay_graph_effect4.h"

#include <cstring>

namespace {

bool is_argument_tag(uint32_t tag) noexcept
{
    return tag >= E4_TYPE_I32 && tag <= E4_TYPE_REF;
}

bool is_result_tag(uint32_t tag) noexcept
{
    return tag == E4_TYPE_VOID || is_argument_tag(tag);
}

} // namespace

extern "C" {

e4_status e4_validate_signature(const e4_signature *signature) E4_NOEXCEPT
{
    if (signature == nullptr) {
        return E4_NULL;
    }
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (!is_argument_tag(signature->args[i])) {
            return E4_BAD_TYPE;
        }
    }
    if (!is_result_tag(signature->result_type)) {
        return E4_BAD_TYPE;
    }
    return E4_OK;
}

e4_status e4_validate_arguments(const e4_signature *signature,
                                const e4_value args[4]) E4_NOEXCEPT
{
    if (signature == nullptr || args == nullptr) {
        return E4_NULL;
    }
    const e4_status signature_status = e4_validate_signature(signature);
    if (signature_status != E4_OK) {
        return signature_status;
    }
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (!is_argument_tag(args[i].type)) {
            return E4_BAD_TYPE;
        }
        if (args[i].type != signature->args[i]) {
            return E4_TYPE_MISMATCH;
        }
    }
    return E4_OK;
}

e4_status e4_invoke(const e4_signature *signature,
                    const e4_value args[4],
                    e4_callback callback,
                    void *context,
                    e4_value *out) E4_NOEXCEPT
{
    if (signature == nullptr || args == nullptr || callback == nullptr) {
        return E4_NULL;
    }
    const e4_status argument_status = e4_validate_arguments(signature, args);
    if (argument_status != E4_OK) {
        return argument_status;
    }
    const uint32_t result_type = signature->result_type;
    if (result_type != E4_TYPE_VOID && out == nullptr) {
        return E4_NULL;
    }

    // The effect point: no refusal, fallback, retry or rollback after this.
    const e4_payload returned = callback(context, args);

    if (result_type != E4_TYPE_VOID) {
        out->type = result_type;
        std::memcpy(&out->payload, &returned, sizeof returned);
    }
    return E4_OK;
}

} // extern "C"
