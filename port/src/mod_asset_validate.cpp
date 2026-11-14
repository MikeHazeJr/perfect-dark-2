/**
 * mod_asset_validate.cpp -- one in-game Validate for typed asset archives.
 *
 * See mod_asset_validate.h for the contract.
 */

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../external/imgui-node-editor/crude_json.h"

extern "C" {
#include "asset_archive_policy.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "body_head_source.h"
#include "character_head_policy.h"
#include "modarchive.h"
}

#include "mod_asset_validate.h"
#include "modasset_gltf_document.h"

namespace {

/* Largest single text member that is read for checking. */
const u32 kMaxTextBytes = 256u * 1024u * 1024u;

struct Failure {
    s32 code = MOD_ASSET_VALIDATE_OK;
    std::string text;
};

bool fail(Failure &out, s32 code, const std::string &text)
{
    if (out.code == MOD_ASSET_VALIDATE_OK) {
        out.code = code;
        out.text = text;
    }
    return false;
}

std::string lowerExtension(const std::string &name)
{
    const size_t slash = name.find_last_of("/\\");
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string ext = name.substr(dot);
    for (char &c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return ext;
}

bool isMeta(const std::string &entry)
{
    return entry.compare(0, 6, ASSET_ARCHIVE_META_DIR "/") == 0;
}

bool utf8Valid(const std::string &text)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(text.data());
    const size_t size = text.size();
    size_t i = 0;

    while (i < size) {
        unsigned code = p[i++];
        unsigned minimum;
        unsigned remaining;

        if (code < 0x80) continue;
        if (code >= 0xc2 && code <= 0xdf) { code &= 0x1f; remaining = 1; minimum = 0x80; }
        else if (code >= 0xe0 && code <= 0xef) { code &= 0x0f; remaining = 2; minimum = 0x800; }
        else if (code >= 0xf0 && code <= 0xf4) { code &= 0x07; remaining = 3; minimum = 0x10000; }
        else return false;
        if (remaining > size - i) return false;
        while (remaining--) {
            if ((p[i] & 0xc0) != 0x80) return false;
            code = (code << 6) | (p[i++] & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

/*
 * The descriptor readers split on "=" and take keys from sections. A line
 * inside a section that is none of comment, section header or `key = value`
 * has no meaning to any of them, so it is refused here.
 */
bool iniWellFormed(const std::string &text, std::string &why)
{
    if (text.find('\0') != std::string::npos) {
        why = "holds a NUL byte";
        return false;
    }
    if (!utf8Valid(text)) {
        why = "is not valid UTF-8";
        return false;
    }

    bool in_section = false;
    size_t line_no = 0;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = at;
        while (end < text.size() && text[end] != '\n' && text[end] != '\r') end++;
        size_t next = end;
        if (next < text.size()) {
            if (text[next] == '\r' && next + 1 < text.size() && text[next + 1] == '\n') next++;
            next++;
        }
        line_no++;

        size_t a = at;
        size_t b = end;
        while (a < b && (text[a] == ' ' || text[a] == '\t')) a++;
        while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) b--;
        at = next;
        if (a == b || text[a] == ';' || text[a] == '#') continue;

        if (text[a] == '[') {
            if (text.find(']', a) >= b) {
                why = "line " + std::to_string(line_no) + ": section header has no closing bracket";
                return false;
            }
            in_section = true;
            continue;
        }
        if (!in_section) continue;

        const size_t eq = text.find('=', a);
        if (eq == std::string::npos || eq >= b) {
            why = "line " + std::to_string(line_no) +
                " is not a comment, a section header or key = value";
            return false;
        }
        size_t key_end = eq;
        while (key_end > a && (text[key_end - 1] == ' ' || text[key_end - 1] == '\t')) key_end--;
        if (key_end == a) {
            why = "line " + std::to_string(line_no) + " has no key before its equals sign";
            return false;
        }
    }
    return true;
}

s32 collectNameCb(const char *entry_name, u32, void *user)
{
    static_cast<std::vector<std::string> *>(user)->push_back(entry_name ? entry_name : "");
    return 0;
}

bool extract(const void *bytes, u32 size, const std::string &entry, std::string &out)
{
    u32 member_size = 0;
    void *data = modArchiveExtractMemAlloc(bytes, size, entry.c_str(), &member_size);
    if (!data) return false;
    out.assign(static_cast<const char *>(data), member_size);
    free(data);
    return true;
}

/* `catalog_id = value` as the first assignment of that key in a descriptor. */
std::string descriptorCatalogId(const std::string &text)
{
    static const char key[] = "catalog_id";
    const size_t key_len = sizeof(key) - 1;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find_first_of("\r\n", at);
        if (end == std::string::npos) end = text.size();
        size_t a = at;
        while (a < end && (text[a] == ' ' || text[a] == '\t')) a++;
        at = end + 1;
        if (end - a <= key_len || text.compare(a, key_len, key) != 0) continue;
        a += key_len;
        while (a < end && (text[a] == ' ' || text[a] == '\t')) a++;
        if (a >= end || text[a] != '=') continue;
        a++;
        while (a < end && (text[a] == ' ' || text[a] == '\t')) a++;
        size_t b = end;
        while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) b--;
        return text.substr(a, b - a);
    }
    return "";
}

/* The family's own reader, where it runs without game state. */
bool validateFamily(const void *bytes, u32 size, const std::string &name,
    const std::string &label, Failure &out)
{
    const asset_type_e type = assetArchiveTypeForPath(name.c_str());
    if (type != ASSET_BODY && type != ASSET_HEAD && type != ASSET_CHARACTER) return true;

    const char *descriptor_name = assetArchiveDescriptorForPath(name.c_str());
    std::string descriptor;
    if (!descriptor_name || !extract(bytes, size, descriptor_name, descriptor)) {
        return fail(out, MOD_ASSET_VALIDATE_ERR_FAMILY, label + " has no readable descriptor");
    }
    const std::string id = descriptorCatalogId(descriptor);
    if (id.empty() || id.size() >= CATALOG_ID_LEN) {
        return fail(out, MOD_ASSET_VALIDATE_ERR_FAMILY,
            label + "::" + descriptor_name + " declares no usable catalog_id");
    }

    char reason[192];
    reason[0] = '\0';
    if (type == ASSET_BODY || type == ASSET_HEAD) {
        std::unique_ptr<body_head_source_t> source(new body_head_source_t());
        if (!bodyHeadSourceParseArchiveBytes(bytes, size, id.c_str(), type == ASSET_HEAD ? 1 : 0,
                source.get(), reason, sizeof(reason))) {
            return fail(out, MOD_ASSET_VALIDATE_ERR_FAMILY, label + "::" + descriptor_name +
                " is refused by the " + (type == ASSET_HEAD ? "head" : "body") + " reader: " +
                (reason[0] ? reason : "no detail"));
        }
        return true;
    }

    std::unique_ptr<ini_section_t> ini(new ini_section_t());
    if (!bodyHeadSourceReadIniBytes(descriptor.data(), static_cast<u32>(descriptor.size()),
            "character", ini.get(), reason, sizeof(reason))) {
        return fail(out, MOD_ASSET_VALIDATE_ERR_FAMILY, label + "::" + descriptor_name +
            " is refused by the character reader: " + (reason[0] ? reason : "no detail"));
    }
    std::unique_ptr<asset_entry_t> entry(new asset_entry_t());
    std::memset(entry.get(), 0, sizeof(asset_entry_t));
    entry->type = ASSET_CHARACTER;
    std::memcpy(entry->id, id.c_str(), id.size() + 1);
    if (!characterSourceApplyIni(entry.get(), ini.get())) {
        return fail(out, MOD_ASSET_VALIDATE_ERR_FAMILY, label + "::" + descriptor_name +
            " is refused by the character reader: a field is missing, duplicated, too long or malformed");
    }
    return true;
}

bool validateTree(const void *bytes, u32 size, const std::string &name,
    const std::string &label, int depth, Failure &out)
{
    std::vector<std::string> names;
    if (modArchiveMemForEachEntry(bytes, size, collectNameCb, &names) != MODARCHIVE_OK) {
        return fail(out, MOD_ASSET_VALIDATE_ERR_POLICY, label + " could not be listed");
    }

    for (const std::string &entry : names) {
        if (entry.empty() || isMeta(entry)) continue;
        const std::string ext = lowerExtension(entry);
        const std::string where = label + "::" + entry;
        const bool typed = assetArchivePathIsTyped(entry.c_str()) != 0;
        const bool json = ext == ".json" || ext == ".gltf";
        const bool ini = ext == ".ini";
        if (!typed && !json && !ini) continue;

        std::string member;
        if (!extract(bytes, size, entry, member)) {
            return fail(out, MOD_ASSET_VALIDATE_ERR_TEXT, where + " could not be read");
        }
        if (typed) {
            if (depth + 1 > MOD_ASSET_VALIDATE_MAX_DEPTH) {
                return fail(out, MOD_ASSET_VALIDATE_ERR_POLICY,
                    where + " is nested deeper than typed archives may go");
            }
            if (!validateTree(member.data(), static_cast<u32>(member.size()), entry, where,
                    depth + 1, out)) {
                return false;
            }
            continue;
        }
        if (member.size() > kMaxTextBytes) {
            return fail(out, MOD_ASSET_VALIDATE_ERR_TEXT, where + " is too large to check");
        }
        if (json) {
            crude_json::value value;
            if (!modAssetJsonReadValue(member.data(), member.size(), value)) {
                return fail(out, MOD_ASSET_VALIDATE_ERR_TEXT, where +
                    " is not one strict JSON value (UTF-8, no byte order mark, no duplicate keys, "
                    "no trailing text)");
            }
        } else {
            std::string why;
            if (!iniWellFormed(member, why)) {
                return fail(out, MOD_ASSET_VALIDATE_ERR_TEXT, where + " " + why);
            }
        }
    }
    return validateFamily(bytes, size, name, label, out);
}

std::string leafName(const char *path)
{
    const std::string text(path);
    const size_t slash = text.find_last_of("/\\");
    return slash == std::string::npos ? text : text.substr(slash + 1);
}

void publish(const Failure &failure, char *err, size_t err_cap)
{
    if (!err || err_cap == 0) return;
    std::snprintf(err, err_cap, "%s", failure.text.c_str());
}

} // namespace

extern "C" s32 modAssetValidateArchiveBytes(const void *bytes, u32 size,
    const char *archive_name, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    Failure failure;
    try {
        if (!bytes || size == 0 || !archive_name || !archive_name[0] ||
                !assetArchivePathIsTyped(archive_name)) {
            fail(failure, MOD_ASSET_VALIDATE_ERR_ARGS, "not a typed asset archive");
        } else {
            char policy[256];
            policy[0] = '\0';
            if (assetArchiveValidateBytes(bytes, size, archive_name,
                    ASSET_ARCHIVE_VALIDATE_RELEASE, policy, sizeof(policy)) != 0) {
                fail(failure, MOD_ASSET_VALIDATE_ERR_POLICY,
                    policy[0] ? policy : "release validation refused the archive");
            } else {
                validateTree(bytes, size, archive_name, leafName(archive_name), 0, failure);
            }
        }
    } catch (...) {
        failure.code = MOD_ASSET_VALIDATE_ERR_TEXT;
        failure.text = "out of memory while validating";
    }
    if (failure.code != MOD_ASSET_VALIDATE_OK) publish(failure, err, err_cap);
    return failure.code;
}

extern "C" s32 modAssetValidateArchiveFile(const char *path, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    if (!path || !path[0]) {
        if (err && err_cap) std::snprintf(err, err_cap, "no archive path");
        return MOD_ASSET_VALIDATE_ERR_ARGS;
    }
    try {
        std::string bytes;
        FILE *f = std::fopen(path, "rb");
        bool ok = false;
        if (f) {
            if (std::fseek(f, 0, SEEK_END) == 0) {
                const long size = std::ftell(f);
                if (size > 0 && static_cast<unsigned long>(size) <= 0x7fffffffUL &&
                        std::fseek(f, 0, SEEK_SET) == 0) {
                    bytes.resize(static_cast<size_t>(size));
                    ok = std::fread(&bytes[0], 1, bytes.size(), f) == bytes.size();
                }
            }
            std::fclose(f);
        }
        if (!ok) {
            if (err && err_cap) std::snprintf(err, err_cap, "the archive could not be read");
            return MOD_ASSET_VALIDATE_ERR_ARGS;
        }
        return modAssetValidateArchiveBytes(bytes.data(), static_cast<u32>(bytes.size()), path,
            err, err_cap);
    } catch (...) {
        if (err && err_cap) std::snprintf(err, err_cap, "out of memory while validating");
        return MOD_ASSET_VALIDATE_ERR_TEXT;
    }
}
