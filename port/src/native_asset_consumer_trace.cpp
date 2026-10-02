#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "native_asset_consumer_trace.h"
#include "native_asset_consumer_trace_contract.h"
#include "asset_archive_policy.h"
#include "modarchive.h"
#include "fs.h"
#include "sha256.h"
#include "system.h"
#undef bool

namespace {
struct State {
    bool initialized = false, enabled = false;
    pdtrace::Manifest manifest;
    std::string output;
    HANDLE file = INVALID_HANDLE_VALUE;
    size_t written = 0;
    unsigned attempts = 0;
    std::map<std::string, bool> expected_textures;
    ~State() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};
State state;
std::mutex guard;
std::string digest(const void *data, size_t size) {
    u8 raw[SHA256_DIGEST_SIZE]; char text[SHA256_HEX_SIZE];
    sha256Hash(data, size, raw); sha256ToHex(raw, text);
    return text;
}
void initialize() {
    if (state.initialized) return;
    state.initialized = true;
    const char *path = std::getenv("PD_ASSET_CONSUMER_MANIFEST");
    if (!path || !path[0]) return;
    if (std::strlen(path) > 1024) return;
    std::ifstream input(path, std::ios::binary);
    char bytes[pdtrace::manifest_limit + 1];
    input.read(bytes, sizeof(bytes));
    const auto count = input.gcount();
    if (!input.is_open() || count <= 0 || static_cast<size_t>(count) > pdtrace::manifest_limit
        || !pdtrace::parse(std::string(bytes, static_cast<size_t>(count)), state.manifest)) {
        sysLogPrintf(LOG_WARNING, "ASSET.CONSUMER.TRACE: invalid manifest; evidence pending"); return;
    }
    char executable[32768];
    const DWORD length = GetModuleFileNameA(nullptr, executable, sizeof(executable));
    u8 raw[SHA256_DIGEST_SIZE]; char hash[SHA256_HEX_SIZE];
    if (!length || length >= sizeof(executable) || sha256HashFile(executable, raw) != 0) return;
    sha256ToHex(raw, hash);
    if (state.manifest.binary != hash) {
        sysLogPrintf(LOG_WARNING, "ASSET.CONSUMER.TRACE: executable identity mismatch; evidence pending"); return;
    }
    char full[32768];
    const DWORD full_length = GetFullPathNameA(path, sizeof(full), full, nullptr);
    if (!full_length || full_length >= sizeof(full)) return;
    std::string parent(full);
    const auto slash = parent.find_last_of("/\\");
    if (slash == std::string::npos) return;
    parent.resize(slash + 1);
    const DWORD attributes = GetFileAttributesA(parent.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)
        || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return;
    state.output = parent + "native-asset-events.jsonl";
    state.enabled = true;
}
struct EntrySize { const char *name; size_t size; unsigned matches = 0; bool valid = true; };
s32 entrySize(const char *name, u32 size, void *context) {
    auto &entry = *static_cast<EntrySize *>(context);
    if (!std::strcmp(name, entry.name)) {
        ++entry.matches;
        if (size != entry.size || size > pdtrace::read_limit) entry.valid = false;
    }
    return 0;
}
bool sameMember(const void *archive, u32 archive_size, const std::string &member,
        const native_asset_consumer_model_read &read) {
    EntrySize expected{member.c_str(), read.size};
    if (modArchiveMemForEachEntry(archive, archive_size, entrySize, &expected) != MODARCHIVE_OK
        || expected.matches != 1 || !expected.valid) return false;
    u32 size = 0;
    void *bytes = modArchiveExtractMemAlloc(archive, archive_size, member.c_str(), &size);
    const bool same = bytes && size == read.size && !std::memcmp(bytes, read.bytes, size);
    std::free(bytes);
    return same;
}
std::string role(const std::string &member) {
    const auto dot = member.find_last_of('.');
    auto suffix = dot == std::string::npos ? std::string() : member.substr(dot);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
    });
    if (suffix == ".obj" || suffix == ".gltf" || suffix == ".glb") return "mesh_source";
    if (suffix == ".png" || suffix == ".tga" || suffix == ".jpg" || suffix == ".jpeg") return "image_source";
    if (suffix == ".mtl") return "material_source";
    return "supporting_source";
}
std::string timestamp() {
    SYSTEMTIME now; GetSystemTime(&now);
    char out[32];
    std::snprintf(out, sizeof(out), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth), static_cast<unsigned>(now.wDay),
        static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
        static_cast<unsigned>(now.wMilliseconds));
    return out;
}
std::string common(const std::string &time, const std::string &attempt,
        const std::string &catalog, const std::string &origin, const std::string &archive,
        const std::string &operation) {
    const auto &m = state.manifest;
    return "\"schema\":\"pd2.native-asset-event.v1\",\"run_id\":" + pdtrace::quote(m.run)
        + ",\"attempt_id\":" + pdtrace::quote(attempt) + ",\"catalog_id\":" + pdtrace::quote(catalog)
        + ",\"archive_origin\":" + pdtrace::quote(origin) + ",\"archive_sha256\":" + pdtrace::quote(archive)
        + ",\"generation\":{\"binary_sha256\":" + pdtrace::quote(m.binary) + ",\"source_sha256\":" + pdtrace::quote(m.source)
        + "},\"operation\":" + pdtrace::quote(operation) + ",\"timestamp\":" + pdtrace::quote(time);
}
bool writePayload(const std::string &payload) {
    if (payload.size() > state.manifest.max_output - state.written) return false;
    if (state.file == INVALID_HANDLE_VALUE) {
        state.file = CreateFileA(state.output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (state.file == INVALID_HANDLE_VALUE) {
            state.enabled = false;
            sysLogPrintf(LOG_WARNING, "ASSET.CONSUMER.TRACE: exclusive output unavailable; evidence pending"); return false;
        }
    }
    DWORD written = 0;
    if (!WriteFile(state.file, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)
        || written != payload.size() || !FlushFileBuffers(state.file)) {
        state.enabled = false;
        sysLogPrintf(LOG_WARNING, "ASSET.CONSUMER.TRACE: incomplete output retained; evidence pending"); return false;
    }
    state.written += written;
    return true;
}
} // namespace

int nativeAssetConsumerModelRequested(const char *id, const char *source) noexcept {
    try {
        std::lock_guard<std::mutex> lock(guard);
        initialize();
        return state.enabled && id && source && state.manifest.catalog == id
            && state.manifest.archive_origin == pdtrace::modelOrigin(source);
    } catch (...) { return false; }
}

void nativeAssetConsumerExpectTexture(const char *parent, const char *source, const char *id) noexcept {
    try {
        std::lock_guard<std::mutex> lock(guard);
        initialize();
        if (state.enabled && parent && source && id && pdtrace::catalog(id)
            && state.manifest.catalog == parent && state.manifest.archive_origin == pdtrace::modelOrigin(source)
            && state.expected_textures.size() < pdtrace::attempt_limit)
            state.expected_textures.emplace(id, true);
    } catch (...) { /* Evidence failure never changes native texture acquisition. */ }
}
int nativeAssetConsumerTextureRequested(const char *id) noexcept {
    try {
        std::lock_guard<std::mutex> lock(guard);
        initialize();
        return state.enabled && id && state.expected_textures.count(id);
    } catch (...) { return false; }
}
void nativeAssetConsumerEmitTexture(const char *id, const char *image_path,
        const void *image, size_t image_size, const char *descriptor_path,
        const void *descriptor, size_t descriptor_size, const char *source_hash,
        int slot, unsigned width, unsigned height, const void *pixels, size_t pixel_size) noexcept {
    try {
        std::lock_guard<std::mutex> lock(guard);
        initialize();
        if (!state.enabled || !id || !state.expected_textures.count(id) || !image_path || !descriptor_path
            || !source_hash || !pdtrace::textureBinding(id, source_hash, slot) || !image || !image_size
            || !descriptor || !descriptor_size || image_size > pdtrace::read_limit
            || descriptor_size > pdtrace::read_limit - image_size || !pixels || !width || !height
            || width > 255 || height > 255 || pixel_size != static_cast<size_t>(width) * height * 4
            || state.attempts >= pdtrace::attempt_limit) return;
        const auto owner = pdtrace::sourceOrigin(image_path, ".pdtexture");
        if (owner.empty() || owner != pdtrace::sourceOrigin(descriptor_path, ".pdtexture")
            || assetArchiveTypeForPath(owner.c_str()) != ASSET_TEXTURE) return;
        const s32 expected_size = fsFileSize(owner.c_str());
        if (expected_size <= 0 || static_cast<size_t>(expected_size) > pdtrace::archive_limit) return;
        u32 archive_size = 0;
        std::unique_ptr<void, decltype(&std::free)> archive(fsFileLoad(owner.c_str(), &archive_size), std::free);
        if (!archive || !archive_size || archive_size > pdtrace::archive_limit) return;
        const std::string prefix = owner + "::";
        native_asset_consumer_model_read reads[] = {{image_path, image, image_size},
            {descriptor_path, descriptor, descriptor_size}};
        std::map<std::string, const native_asset_consumer_model_read *> members;
        for (const auto &read : reads) {
            const std::string path(read.path);
            if (path.compare(0, prefix.size(), prefix)) return;
            const auto member = path.substr(prefix.size());
            if (!pdtrace::relative(member) || member.compare(0, 6, "_meta/") == 0
                || !sameMember(archive.get(), archive_size, member, read)
                || !members.emplace(member, &read).second) return;
        }
        const auto image_member = std::string(image_path).substr(prefix.size());
        if (role(image_member) != "image_source") return;
        const auto fields = common(timestamp(), "texture-" + std::to_string(++state.attempts),
            id, owner, digest(archive.get(), archive_size), "texture_load");
        std::string payload = "{" + fields + ",\"event\":\"begin\",\"provider\":\"FileProvider\"}\n";
        for (const auto &pair : members) {
            const auto &read = *pair.second;
            payload += "{" + fields + ",\"event\":\"member\",\"provider\":\"FileProvider\",\"member\":"
                + pdtrace::quote(pair.first) + ",\"sha256\":" + pdtrace::quote(digest(read.bytes, read.size))
                + ",\"bytes\":" + std::to_string(read.size) + ",\"source_role\":" + pdtrace::quote(role(pair.first)) + "}\n";
        }
        payload += "{" + fields + ",\"event\":\"end\",\"native_completed\":true,\"rom_fallback\":false,"
            "\"load_verdict\":\"pass\",\"checks\":{},\"native_source_closure_sha256\":" + pdtrace::quote(source_hash)
            + ",\"native_texture\":{\"native_slot\":" + std::to_string(slot)
            + ",\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height)
            + ",\"rgba_bytes\":" + std::to_string(pixel_size) + ",\"rgba_sha256\":" + pdtrace::quote(digest(pixels, pixel_size))
            + "},\"pending_dependencies\":[]}\n";
        if (writePayload(payload)) sysLogPrintf(LOG_NOTE,
            "ASSET.CONSUMER.TRACE: texture=%s members=%u result=PASS draw=pending",
            id, static_cast<unsigned>(members.size()));
    } catch (...) {
        sysLogPrintf(LOG_WARNING, "ASSET.CONSUMER.TRACE: texture diagnostic failed; evidence pending");
    }
}

void nativeAssetConsumerEmitModel(const char *catalog_id, const char *source,
        const void *archive, size_t archive_size, const native_asset_consumer_model_read *reads,
        size_t count, const char *closure, const char *const *dependencies, size_t dependency_count,
        const native_asset_consumer_texture_binding *bindings, size_t binding_count) noexcept {
    try {
        std::lock_guard<std::mutex> lock(guard);
        initialize();
        if (!state.enabled || !catalog_id || !source || state.manifest.catalog != catalog_id
            || state.manifest.archive_origin != source || !archive || !archive_size
            || archive_size > pdtrace::archive_limit || !reads || !count || count > pdtrace::member_limit
            || !closure || !pdtrace::hex(closure) || state.attempts >= pdtrace::attempt_limit) return;
        if (assetArchiveTypeForPath(source) != ASSET_MODEL || digest(archive, archive_size) != state.manifest.archive_hash) return;
        const std::string prefix = std::string(source) + "::";
        std::map<std::string, const native_asset_consumer_model_read *> members;
        size_t total = 0; bool activating = false;
        for (size_t i = 0; i < count; ++i) {
            const auto &read = reads[i];
            if (!read.path || !read.bytes || !read.size || read.size > pdtrace::read_limit - total) return;
            total += read.size;
            const std::string path(read.path);
            if (path.compare(0, prefix.size(), prefix)) return; // Other archive/loose inputs need their own receipt.
            const auto member = path.substr(prefix.size());
            if (!pdtrace::relative(member) || !sameMember(archive, static_cast<u32>(archive_size), member, read)) return;
            if (member.compare(0, 6, "_meta/") == 0) continue;
            if (!members.emplace(member, &read).second) return;
            activating = activating || role(member) == "mesh_source";
        }
        if (!activating) return;
        if (dependency_count > pdtrace::member_limit || (dependency_count && !dependencies)) return;
        std::map<std::string, bool> pending;
        for (size_t i = 0; i < dependency_count; ++i) {
            if (!dependencies[i] || !pdtrace::catalog(dependencies[i])) return;
            pending.emplace(dependencies[i], true);
        }
        std::string dependency_json = "[";
        for (const auto &pair : pending) {
            if (dependency_json.size() > 1) dependency_json += ',';
            dependency_json += pdtrace::quote(pair.first);
        }
        dependency_json += ']';
        if (binding_count > pdtrace::member_limit || (binding_count && !bindings)) return;
        std::map<std::string, native_asset_consumer_texture_binding> bound;
        for (size_t i = 0; i < binding_count; ++i) {
            const auto &binding = bindings[i];
            if (!binding.catalog_id || !binding.source_hash
                || !pdtrace::textureBinding(binding.catalog_id, binding.source_hash, binding.native_slot)
                || !pending.count(binding.catalog_id)) return;
            const auto previous = bound.find(binding.catalog_id);
            if (previous != bound.end() && (std::strcmp(previous->second.source_hash, binding.source_hash)
                || previous->second.native_slot != binding.native_slot)) return;
            bound.emplace(binding.catalog_id, binding);
        }
        std::string binding_json = "[";
        for (const auto &pair : bound) {
            if (binding_json.size() > 1) binding_json += ',';
            binding_json += "{\"catalog_id\":" + pdtrace::quote(pair.first)
                + ",\"native_source_closure_sha256\":" + pdtrace::quote(pair.second.source_hash)
                + ",\"native_slot\":" + std::to_string(pair.second.native_slot) + "}";
        }
        binding_json += ']';
        const auto fields = common(timestamp(), "model-" + std::to_string(++state.attempts),
            catalog_id, source, state.manifest.archive_hash, "modeldef_compile");
        std::string payload = "{" + fields + ",\"event\":\"begin\",\"provider\":\"FileProvider\"}\n";
        for (const auto &pair : members) {
            const auto &read = *pair.second;
            payload += "{" + fields + ",\"event\":\"member\",\"provider\":\"FileProvider\",\"member\":"
                + pdtrace::quote(pair.first) + ",\"sha256\":" + pdtrace::quote(digest(read.bytes, read.size))
                + ",\"bytes\":" + std::to_string(read.size) + ",\"source_role\":" + pdtrace::quote(role(pair.first)) + "}\n";
        }
        payload += "{" + fields + ",\"event\":\"end\",\"native_completed\":true,\"rom_fallback\":false,"
            "\"load_verdict\":\"pass\",\"checks\":{},\"native_source_closure_sha256\":" + pdtrace::quote(closure)
            + ",\"pending_dependencies\":" + dependency_json + ",\"dependency_bindings\":" + binding_json + "}\n";
        if (!writePayload(payload)) return;
        sysLogPrintf(LOG_NOTE, "ASSET.CONSUMER.TRACE: model=%s members=%u result=PASS draw=pending dependencies_pending=%u",
            catalog_id, static_cast<unsigned>(members.size()), static_cast<unsigned>(pending.size()));
    } catch (...) {
        sysLogPrintf(LOG_WARNING, "ASSET.CONSUMER.TRACE: diagnostic allocation failed; evidence pending");
    }
}
