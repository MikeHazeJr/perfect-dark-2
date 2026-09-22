#include "modasset_json.h"
#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"
#include <cstring>

extern "C" s32 modAssetJsonValidateObject(const char *json, size_t json_size)
{
    crude_json::value value;
    return modAssetJsonReadValue(json, json_size, value) && value.is_object();
}

extern "C" s32 modAssetJsonDecodeString(const char *start, const char *end,
    char *out, size_t out_cap)
{
    if (out && out_cap) out[0] = '\0';
    if (!start || !end || end <= start || !out || !out_cap) return 0;
    try {
        crude_json::value value;
        if (!modAssetJsonReadValue(start, static_cast<size_t>(end - start), value) ||
                !value.is_string()) return 0;
        const auto &text = value.get<crude_json::string>();
        if (text.size() >= out_cap) return 0;
        std::memcpy(out, text.data(), text.size());
        out[text.size()] = '\0';
        return 1;
    } catch (...) { return 0; }
}

extern "C" s32 modAssetJsonStringEquals(const char *quoted_start,
    const char *quoted_end, const char *expected)
{
    if (!quoted_start || !quoted_end || quoted_end <= quoted_start || !expected)
        return -1;
    try {
        crude_json::value value;
        if (!modAssetJsonReadValue(quoted_start,
                static_cast<size_t>(quoted_end - quoted_start), value) || !value.is_string())
            return -1;
        return value.get<crude_json::string>() == expected ? 1 : 0;
    } catch (...) {
        return -1;
    }
}
