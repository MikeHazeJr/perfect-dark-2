#include "lang_source.h"
#include "langmanifest.h"
#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
using Json = crude_json::value;
constexpr size_t sourceLimit = 2147483647u;

bool reject(const char **error, const char *reason)
{
    if (error) *error = reason;
    return false;
}

bool countValue(const Json &value, u32 &out)
{
    if (!value.is_number()) return false;
    const double number = value.get<crude_json::number>();
    if (!std::isfinite(number) || number < 0 || number > LANG_SOURCE_MAX_STRINGS ||
            std::floor(number) != number) return false;
    out = static_cast<u32>(number);
    return true;
}

bool nativeText(const std::string &utf8, std::string &out, const char **error)
{
    /* The shared JSON reader already validates Unicode and surrogate pairs.
     * Decode its canonical UTF-8 once, independent of the author's spelling. */
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i++]);
        unsigned cp = c;
        unsigned continuation = 0;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; continuation = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; continuation = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; continuation = 3; }
        else if (c >= 0x80) return reject(error, "language_utf8_invalid");
        if (continuation > utf8.size() - i) return reject(error, "language_utf8_invalid");
        while (continuation--) {
            const unsigned char next = static_cast<unsigned char>(utf8[i++]);
            if ((next & 0xc0) != 0x80) return reject(error, "language_utf8_invalid");
            cp = (cp << 6) | (next & 63);
        }
        if (cp == 0) return reject(error, "language_embedded_nul_unsupported");
        if (cp > 0xff) return reject(error, "language_codepoint_unsupported_by_latin1");
        out += static_cast<char>(cp);
    }
    return true;
}

u32 be32(const u8 *p)
{
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
        (static_cast<u32>(p[2]) << 8) | p[3];
}

bool zeroBytes(const u8 *data, u32 size)
{
    for (u32 i = 0; i < size; i++) if (data[i]) return false;
    return true;
}

void appendText(std::string &out, const u8 *text, size_t size)
{
    static const char hex[] = "0123456789abcdef";
    out += '"';
    for (size_t i = 0; i < size; i++) {
        const u8 c = text[i];
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c >= 0x20 && c < 0x7f) out += static_cast<char>(c);
        else {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    out += '"';
}

int localeIndex(const char *tag)
{
    if (!tag || !tag[0]) return 0;
    std::string value(tag);
    if (value.size() > 15) return -1;
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    if (value == "en" || value == "en-us" || value == "en_us") return 0;
    if (value == "en-gb" || value == "en_gb" || value == "gb") return 1;
    if (value == "fr") return 2;
    if (value == "de") return 3;
    if (value == "it") return 4;
    if (value == "es") return 5;
    if (value == "ja" || value == "jp" || value == "ja-jp" || value == "ja_jp") return 6;
    return -1;
}
}

extern "C" s32 langSourceLocaleRank(const char *candidate, const char *requested)
{
    const int source = localeIndex(candidate);
    const int target = localeIndex(requested);
    if (source < 0 || target < 0) return 0;
    if (source == target) return 2;
    return source == 0 ? 1 : 0;
}

extern "C" lang_source_encoding_t langSourceEncodingForLocale(
    const char *rom_id, const char *locale)
{
    if (rom_id && std::strcmp(rom_id, "jpn-final") == 0 &&
            localeIndex(locale) == 6) return LANG_SOURCE_JAPANESE_UNSUPPORTED;
    return LANG_SOURCE_LATIN1;
}

extern "C" s32 langSourceParseCount(const char *text, u32 *out_count)
{
    if (out_count) *out_count = 0;
    if (!text || !out_count) return 0;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    if (*text < '0' || *text > '9') return 0;
    u32 count = 0;
    while (*text >= '0' && *text <= '9') {
        count = count * 10 + static_cast<unsigned>(*text++ - '0');
        if (count > LANG_SOURCE_MAX_STRINGS) return 0;
    }
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    if (*text) return 0;
    *out_count = count;
    return 1;
}

extern "C" s32 langSourceParseManifestFields(const char *json, size_t size,
    s32 *out_bank, u32 *out_count, s32 *out_count_declared)
{
    if (out_bank) *out_bank = -1;
    if (out_count) *out_count = 0;
    if (out_count_declared) *out_count_declared = 0;
    if (!json || !size || size > sourceLimit || !out_bank || !out_count || !out_count_declared) return 0;
    try {
        if (size >= 3 && std::memcmp(json, "\xef\xbb\xbf", 3) == 0) { json += 3; size -= 3; }
        Json root;
        if (!modAssetJsonReadValue(json, size, root) || !root.is_object()) return 0;
        u32 bank, count = 0;
        if (!root.contains("source_bank") || !countValue(root["source_bank"], bank) ||
                bank == 0 || bank >= LANG_MANIFEST_MAX_BANKS) return 0;
        const bool declared = root.contains("string_count");
        if (declared && !countValue(root["string_count"], count)) return 0;
        *out_bank = static_cast<s32>(bank);
        *out_count = count;
        *out_count_declared = declared ? 1 : 0;
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" s32 langSourceParseJson(const char *json, size_t size,
    s32 expected_count, lang_source_encoding_t encoding,
    lang_source_bank_t *out, const char **out_error)
{
    if (out) *out = {};
    if (out_error) *out_error = nullptr;
    if (!out || !json || !size || size > sourceLimit || expected_count < -1 ||
            expected_count > static_cast<s32>(LANG_SOURCE_MAX_STRINGS))
        return reject(out_error, "language_source_arguments_invalid");
    if (encoding != LANG_SOURCE_LATIN1)
        return reject(out_error, "language_native_encoding_unsupported");
    try {
        /* Keep support for existing UTF-8 BOM source files. */
        if (size >= 3 && std::memcmp(json, "\xef\xbb\xbf", 3) == 0) { json += 3; size -= 3; }
        Json root;
        if (!modAssetJsonReadValue(json, size, root) || !root.is_object())
            return reject(out_error, "language_json_invalid");
        if (!root.contains("pd_kind") || !root["pd_kind"].is_string() ||
                root["pd_kind"].get<crude_json::string>() != "language_strings")
            return reject(out_error, "language_source_kind_invalid");
        if (root.contains("pd_schema_version")) {
            u32 version;
            if (!countValue(root["pd_schema_version"], version) || version != 1)
                return reject(out_error, "language_source_version_unsupported");
        }
        if (!root.contains("strings") || !root["strings"].is_array())
            return reject(out_error, "language_strings_array_missing");
        const auto &rows = root["strings"].get<crude_json::array>();
        if (rows.size() > LANG_SOURCE_MAX_STRINGS ||
                (expected_count >= 0 && rows.size() != static_cast<size_t>(expected_count)))
            return reject(out_error, "language_string_count_mismatch");
        std::array<bool, LANG_SOURCE_MAX_STRINGS> seen{};
        std::array<bool, LANG_SOURCE_MAX_STRINGS> present{};
        std::array<std::string, LANG_SOURCE_MAX_STRINGS> strings;
        size_t extent = sizeof(uintptr_t) * LANG_SOURCE_MAX_STRINGS;
        for (const Json &row : rows) {
            u32 index;
            if (!row.is_object() || !row.contains("index") || !row.contains("text") ||
                    !countValue(row["index"], index) || index >= rows.size())
                return reject(out_error, "language_row_or_index_invalid");
            if (seen[index]) return reject(out_error, "language_index_duplicate");
            seen[index] = true;
            if (row["text"].is_null()) continue;
            if (!row["text"].is_string()) return reject(out_error, "language_text_type_invalid");
            if (!nativeText(row["text"].get<crude_json::string>(), strings[index], out_error)) return 0;
            present[index] = true;
            if (strings[index].size() >= sourceLimit - extent)
                return reject(out_error, "language_runtime_extent_overflow");
            extent += strings[index].size() + 1;
        }
        auto *bank = static_cast<uintptr_t *>(std::calloc(1, extent));
        if (!bank) return reject(out_error, "language_allocation_failed");
        size_t cursor = sizeof(uintptr_t) * LANG_SOURCE_MAX_STRINGS;
        for (size_t i = 0; i < rows.size(); i++) {
            if (!present[i]) continue;
            bank[i] = cursor;
            std::memcpy(reinterpret_cast<char *>(bank) + cursor, strings[i].c_str(), strings[i].size() + 1);
            cursor += strings[i].size() + 1;
        }
        out->data = bank;
        out->string_count = static_cast<u32>(rows.size());
        out->data_size = static_cast<u32>(extent);
        return 1;
    } catch (...) {
        return reject(out_error, "language_allocation_failed");
    }
}

extern "C" s32 langSourceExportNative(const u8 *data, u32 size,
    s32 expected_count, lang_source_encoding_t encoding, char **out_json,
    u32 *out_size, u32 *out_count, const char **out_error)
{
    if (out_json) *out_json = nullptr;
    if (out_size) *out_size = 0;
    if (out_count) *out_count = 0;
    if (out_error) *out_error = nullptr;
    if (!data || size < 4 || size > sourceLimit || !out_json || !out_size || !out_count ||
            expected_count < -1 || expected_count > static_cast<s32>(LANG_SOURCE_MAX_STRINGS))
        return reject(out_error, "language_native_arguments_invalid");
    if (encoding != LANG_SOURCE_LATIN1)
        return reject(out_error, "language_native_encoding_unsupported");
    try {
        u32 count = 0;
        if (expected_count >= 0) count = static_cast<u32>(expected_count);
        else {
            for (u32 pos = 0; pos + 4 <= size && pos < LANG_SOURCE_MAX_STRINGS * 4; pos += 4) {
                const u32 offset = be32(data + pos);
                if (!offset) continue;
                if (offset % 4 || offset > size || offset <= pos || offset / 4 > LANG_SOURCE_MAX_STRINGS)
                    return reject(out_error, "language_native_table_invalid");
                count = offset / 4;
                break;
            }
            if (!count && (size != 16 || !zeroBytes(data, size)))
                return reject(out_error, "language_native_table_extent_ambiguous");
        }
        if (count * 4 > size || (!count && !zeroBytes(data, size)))
            return reject(out_error, "language_native_table_invalid");
        std::string json = "{\n  \"pd_kind\": \"language_strings\",\n  \"pd_schema_version\": 1,\n  \"strings\": [";
        for (u32 i = 0; i < count; i++) {
            if (json.size() > sourceLimit - 64)
                return reject(out_error, "language_source_extent_overflow");
            const u32 offset = be32(data + i * 4);
            json += i ? ",\n    {\"index\": " : "\n    {\"index\": ";
            json += std::to_string(i);
            json += ", \"text\": ";
            if (!offset) json += "null";
            else {
                if (offset < count * 4 || offset >= size)
                    return reject(out_error, "language_native_string_offset_invalid");
                const auto *end = static_cast<const u8 *>(std::memchr(data + offset, 0, size - offset));
                if (!end) return reject(out_error, "language_native_string_unterminated");
                const size_t length = static_cast<size_t>(end - data - offset);
                if (length > (sourceLimit - json.size() - 32) / 6)
                    return reject(out_error, "language_source_extent_overflow");
                appendText(json, data + offset, length);
            }
            json += '}';
        }
        json += "\n  ]\n}\n";
        char *output = static_cast<char *>(std::malloc(json.size() + 1));
        if (!output) return reject(out_error, "language_allocation_failed");
        std::memcpy(output, json.c_str(), json.size() + 1);
        *out_json = output;
        *out_size = static_cast<u32>(json.size());
        *out_count = count;
        return 1;
    } catch (...) {
        return reject(out_error, "language_allocation_failed");
    }
}
