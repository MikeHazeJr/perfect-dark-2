#include "modmgr_enabled_state.h"
#include "modasset_gltf_document.h"
#include "save_atomic.h"
#include "../external/imgui-node-editor/crude_json.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
#include <memory>
#include <cerrno>
#include <sys/stat.h>

namespace {
int reject(char *error, size_t capacity, const char *message)
{
    if (error && capacity) std::snprintf(error, capacity, "%s", message);
    return 0;
}

bool validRegistry(const modinfo_t *registry, int count)
{
    if (count < 0 || count > MODMGR_MAX_MODS || (count && !registry)) return false;
    std::set<std::string> ids;
    for (int i = 0; i < count; ++i) {
        if (!std::memchr(registry[i].id, '\0', sizeof(registry[i].id)) ||
                !registry[i].id[0] || !ids.insert(registry[i].id).second) return false;
    }
    return true;
}
}

extern "C" int modmgrBuildEnabledCsv(const modinfo_t *registry, int count,
    char *output, size_t capacity, char *error, size_t error_cap)
{
    if (error && error_cap) error[0] = '\0';
    try {
        if (!output || !capacity || !validRegistry(registry, count))
            return reject(error, error_cap, "Invalid enabled-mod registry or output buffer.");
        std::string candidate;
        for (int i = 0; i < count; ++i) {
            if (!registry[i].enabled || registry[i].session_only) continue;
            if (!candidate.empty()) candidate += ',';
            candidate += registry[i].id;
        }
        if (candidate.size() >= capacity)
            return reject(error, error_cap, "Enabled mods exceed the legacy config capacity.");
        std::memcpy(output, candidate.c_str(), candidate.size() + 1);
        return 1;
    } catch (...) {
        return reject(error, error_cap, "Could not prepare the legacy enabled-mod list.");
    }
}

extern "C" int modmgrLoadEnabledDocument(const char *json, size_t json_size,
    modinfo_t *registry, int count, char *error, size_t error_cap)
{
    static_assert(std::is_trivially_copyable<modinfo_t>::value,
        "Registry publication requires value-copyable metadata");
    if (error && error_cap) error[0] = '\0';
    if (!json || count < 0 || count > MODMGR_MAX_MODS || (count && !registry))
        return reject(error, error_cap, "Invalid enabled-mod document or registry.");
    try {
        crude_json::value document;
        if (!modAssetJsonReadValue(json, json_size, document) || !document.is_array())
            return reject(error, error_cap, "Enabled mods must be a complete JSON array.");

        std::vector<std::string> saved_order;
        std::set<std::string> enabled;
        for (const auto &value : document.get<crude_json::array>()) {
            if (!value.is_string())
                return reject(error, error_cap, "Every enabled-mod entry must be a string.");
            const auto &id = value.get<crude_json::string>();
            if (id.empty() || id.size() >= MODMGR_ID_LEN ||
                    id.find('\0') != std::string::npos)
                return reject(error, error_cap, "An enabled-mod ID is empty or too long, or contains a null byte.");
            if (!enabled.insert(id).second)
                return reject(error, error_cap, "Enabled mods contain a duplicate ID.");
            saved_order.push_back(id);
        }

        if (!validRegistry(registry, count))
            return reject(error, error_cap, "Installed mods contain an invalid or duplicate ID.");

        std::vector<int> order;
        for (const auto &id : saved_order) {
            for (int i = 0; i < count; ++i) {
                if (!registry[i].session_only && id == registry[i].id) {
                    order.push_back(i);
                    break;
                }
            }
        }
        for (int i = 0; i < count; ++i) {
            if (!registry[i].session_only && enabled.count(registry[i].id) == 0)
                order.push_back(i);
        }

        std::vector<modinfo_t> staged;
        if (count) staged.assign(registry, registry + count);
        size_t next = 0;
        for (int i = 0; i < count; ++i) {
            if (registry[i].session_only) continue;
            staged[i] = registry[order[next++]];
            staged[i].enabled = enabled.count(staged[i].id) ? 1 : 0;
        }
        if (count) std::memcpy(registry, staged.data(), sizeof(modinfo_t) * count);
        return 1;
    } catch (...) {
        return reject(error, error_cap, "Could not prepare the enabled-mod selection.");
    }
}

extern "C" int modmgrLoadEnabledFile(const char *path, modinfo_t *registry,
    int count, char *error, size_t error_cap)
{
    if (error && error_cap) error[0] = '\0';
    if (!path || !path[0]) {
        reject(error, error_cap, "Missing enabled-mod source path.");
        return -1;
    }
    struct stat info;
    if (stat(path, &info) != 0) {
        if (errno == ENOENT) return 0;
        reject(error, error_cap, "Could not inspect enabled-mod source.");
        return -1;
    }
    if (!S_ISREG(info.st_mode)) {
        reject(error, error_cap, "Enabled-mod source is not a regular file.");
        return -1;
    }
    try {
        std::unique_ptr<FILE, int(*)(FILE*)> file(std::fopen(path, "rb"), std::fclose);
        if (!file) {
            reject(error, error_cap, "Could not open enabled-mod source.");
            return -1;
        }
        std::string json;
        char buffer[4096];
        size_t bytes;
        while ((bytes = std::fread(buffer, 1, sizeof(buffer), file.get())) != 0)
            json.append(buffer, bytes);
        if (std::ferror(file.get())) {
            reject(error, error_cap, "Could not read complete enabled-mod source.");
            return -1;
        }
        if (std::fclose(file.release()) != 0) {
            reject(error, error_cap, "Could not close enabled-mod source.");
            return -1;
        }
        return modmgrLoadEnabledDocument(json.data(), json.size(), registry, count,
            error, error_cap) ? 1 : -1;
    } catch (...) {
        reject(error, error_cap, "Could not prepare enabled-mod source.");
        return -1;
    }
}

extern "C" int modmgrSaveEnabledFile(const char *path, const modinfo_t *registry,
    int count, char *error, size_t error_cap)
{
    if (error && error_cap) error[0] = '\0';
    if (!path || !path[0]) return reject(error, error_cap, "Missing enabled-mod destination.");
    try {
        if (!validRegistry(registry, count))
            return reject(error, error_cap, "Installed mods contain an invalid or duplicate ID.");
        std::string json = "[";
        bool first = true;
        const char hex[] = "0123456789abcdef";
        for (int i = 0; i < count; ++i) {
            if (!registry[i].enabled || registry[i].session_only) continue;
            json += first ? "\n  \"" : ",\n  \"";
            first = false;
            for (const unsigned char *c = reinterpret_cast<const unsigned char *>(registry[i].id); *c; ++c) {
                if (*c == '"' || *c == '\\') { json += '\\'; json += static_cast<char>(*c); }
                else if (*c < 0x20) {
                    json += "\\u00"; json += hex[*c >> 4]; json += hex[*c & 15];
                } else json += static_cast<char>(*c);
            }
            json += '"';
        }
        json += first ? "]\n" : "\n]\n";
        // The strict decoder also rejects malformed UTF-8 before touching disk.
        crude_json::value decoded;
        if (!modAssetJsonReadValue(json.data(), json.size(), decoded))
            return reject(error, error_cap, "Enabled-mod IDs are not valid JSON text.");
        save_atomic_file_t transaction{};
        if (saveAtomicBegin(&transaction, path) != 0)
            return reject(error, error_cap, "Could not open enabled-mod save candidate.");
        if (std::fwrite(json.data(), 1, json.size(), saveAtomicStream(&transaction)) != json.size()) {
            saveAtomicAbort(&transaction);
            return reject(error, error_cap, "Could not write enabled-mod save candidate.");
        }
        if (saveAtomicCommit(&transaction) != 0)
            return reject(error, error_cap, "Could not commit enabled-mod save candidate.");
        return 1;
    } catch (...) {
        return reject(error, error_cap, "Could not prepare the enabled-mod save.");
    }
}
