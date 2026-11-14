/**
 * mod_manifest_validate.cpp -- strict root mod.json admission for packaging.
 *
 * See mod_manifest_validate.h for the contract. The offline rule this mirrors
 * is validate_manifest() in tools/verify_pdxxx_modder_workflow.py.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "../external/imgui-node-editor/crude_json.h"
#include "mod_manifest_validate.h"
#include "modasset_gltf_document.h"

namespace {

/* Exactly the code points Python's str.isspace() accepts, so "non-empty"
 * means here what value.strip() means in the offline validator. Zero-width
 * space (U+200B) and U+FEFF are not whitespace there, and are not here. */
bool isOfflineWhitespace(unsigned cp)
{
    return (cp >= 0x0009 && cp <= 0x000d) ||
        (cp >= 0x001c && cp <= 0x0020) ||
        cp == 0x0085 || cp == 0x00a0 || cp == 0x1680 ||
        (cp >= 0x2000 && cp <= 0x200a) ||
        cp == 0x2028 || cp == 0x2029 ||
        cp == 0x202f || cp == 0x205f || cp == 0x3000;
}

/* `text` was decoded by the strict reader, which only emits well-formed
 * UTF-8. A sequence that is cut short is treated as text, not whitespace, so
 * a malformed value can never be reported as empty by mistake. */
bool hasTextAfterStrip(const std::string &text)
{
    const size_t size = text.size();
    size_t i = 0;

    while (i < size) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        unsigned cp;
        size_t extra;

        if (lead < 0x80) { cp = lead; extra = 0; }
        else if (lead < 0xc2) return true;
        else if (lead < 0xe0) { cp = lead & 0x1fu; extra = 1; }
        else if (lead < 0xf0) { cp = lead & 0x0fu; extra = 2; }
        else if (lead < 0xf5) { cp = lead & 0x07u; extra = 3; }
        else return true;

        if (extra > size - i - 1) return true;
        for (size_t k = 1; k <= extra; k++) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xc0u) != 0x80u) return true;
            cp = (cp << 6) | (next & 0x3fu);
        }
        i += extra + 1;

        if (!isOfflineWhitespace(cp)) return true;
    }
    return false;
}

void setError(char *error, size_t cap, const char *text)
{
    if (error && cap) std::snprintf(error, cap, "%s", text);
}

} // namespace

extern "C" s32 modManifestValidateBytes(const void *json, size_t size,
    char *error, size_t error_cap)
{
    if (error && error_cap) error[0] = '\0';

    if (!json || size == 0) {
        setError(error, error_cap, "unreadable JSON: the file is empty");
        return MOD_MANIFEST_ERR_UNREADABLE;
    }
    if (size > MOD_MANIFEST_MAX_BYTES) {
        setError(error, error_cap, "unreadable JSON: the file is larger than 8 MiB");
        return MOD_MANIFEST_ERR_UNREADABLE;
    }

    try {
        crude_json::value root;

        if (!modAssetJsonReadValue(static_cast<const char *>(json), size, root)) {
            setError(error, error_cap,
                "unreadable JSON: expected one complete UTF-8 JSON value with "
                "no byte order mark, no duplicate keys and no trailing text");
            return MOD_MANIFEST_ERR_UNREADABLE;
        }
        if (!root.is_object()) {
            setError(error, error_cap, "expected a JSON object");
            return MOD_MANIFEST_ERR_NOT_OBJECT;
        }

        static const char *const kRequired[] = { "id", "name", "version" };
        const crude_json::value &manifest = root;
        std::string failing;

        for (const char *field : kRequired) {
            const crude_json::string key(field);
            const crude_json::string *text = nullptr;

            if (manifest.contains(key)) {
                text = manifest[key].get_ptr<crude_json::string>();
            }
            if (!text || !hasTextAfterStrip(*text)) {
                if (!failing.empty()) failing += ", ";
                failing += field;
            }
        }

        if (!failing.empty()) {
            const std::string message = "missing non-empty string " + failing;
            setError(error, error_cap, message.c_str());
            return MOD_MANIFEST_ERR_FIELD;
        }
        return MOD_MANIFEST_OK;
    } catch (...) {
        setError(error, error_cap, "unreadable JSON: the reader ran out of memory");
        return MOD_MANIFEST_ERR_UNREADABLE;
    }
}
