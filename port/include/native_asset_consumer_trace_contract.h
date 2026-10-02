#ifndef PD_NATIVE_ASSET_CONSUMER_TRACE_CONTRACT_H
#define PD_NATIVE_ASSET_CONSUMER_TRACE_CONTRACT_H

#include <cstddef>
#include <map>
#include <string>

namespace pdtrace {
constexpr size_t manifest_limit = 8192;
constexpr size_t output_limit = 16 * 1024 * 1024;
constexpr size_t archive_limit = 16 * 1024 * 1024;
constexpr size_t read_limit = 32 * 1024 * 1024;
constexpr size_t member_limit = 2048;
constexpr unsigned attempt_limit = 256;

inline bool hex(const std::string &value) {
    if (value.size() != 64) return false;
    for (char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline bool token(const std::string &value, size_t limit = 80) {
    if (value.empty() || value.size() > limit) return false;
    for (char c : value) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    return true;
}
inline bool relative(const std::string &path) {
    if (path.empty() || path.size() > 1024 || path.front() == '/') return false;
    for (unsigned char c : path) if (c < 32 || c == '\\' || c == ':') return false;
    size_t start = 0;
    do {
        size_t end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    return true;
}
inline bool typedOrigin(const std::string &path, const std::string &extension) {
    if (path.empty() || path.size() > 1024) return false;
    size_t start = 0;
    do {
        const size_t end = path.find("::", start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (!relative(part)) return false;
        if (end == std::string::npos) {
            if (part.size() < extension.size()) return false;
            auto suffix = part.substr(part.size() - extension.size());
            for (char &c : suffix) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
            return suffix == extension;
        }
        start = end + 2;
    } while (true);
}
inline bool origin(const std::string &path) { return typedOrigin(path, ".pdmesh"); }
inline bool catalog(const std::string &id) {
    const auto colon = id.find(':');
    if (id.size() >= 128 || colon == std::string::npos || !colon
        || !((id[0] >= 'A' && id[0] <= 'Z') || (id[0] >= 'a' && id[0] <= 'z'))
        || !token(id.substr(0, colon), 127) || !relative(id.substr(colon + 1))) return false;
    for (char c : id.substr(colon + 1)) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return false;
    return true;
}
inline std::string sourceOrigin(const std::string &selected, const std::string &extension) {
    // FileProvider may select the mesh member directly. Keep the typed owner
    // separate from that unchanged compiler input locator.
    std::string owner;
    size_t end = selected.find("::");
    while (true) {
        const auto prefix = selected.substr(0, end);
        if (typedOrigin(prefix, extension)) owner = prefix;
        if (end == std::string::npos) break;
        end = selected.find("::", end + 2);
    }
    return owner;
}
inline std::string modelOrigin(const std::string &selected) { return sourceOrigin(selected, ".pdmesh"); }
inline bool textureBinding(const std::string &id, const std::string &hash, int slot) {
    return catalog(id) && hex(hash) && slot >= 0 && slot < 65535;
}
inline std::string quote(const std::string &value) {
    static const char digits[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += digits[c >> 4]; out += digits[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
struct Manifest {
    std::string binary, source, catalog, archive_origin, archive_hash, run;
    size_t max_output = output_limit;
};
inline bool parse(const std::string &text, Manifest &out) {
    if (text.empty() || text.size() > manifest_limit || text.find('\0') != std::string::npos) return false;
    std::map<std::string, std::string> values;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        auto line = text.substr(start, end == std::string::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            size_t equals = line.find('=');
            if (equals == std::string::npos || !values.emplace(line.substr(0, equals), line.substr(equals + 1)).second) return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (values.size() != 8 || values["schema"] != "pd2.native-asset-consumer-manifest.v1"
        || !hex(values["binary_sha256"]) || !hex(values["source_sha256"])
        || !hex(values["archive_sha256"]) || !origin(values["archive_origin"])
        || !token(values["run_id"])) return false;
    const auto &id = values["catalog_id"];
    if (!catalog(id)) return false;
    const auto &number = values["max_output_bytes"];
    if (number.empty() || number.size() > 8 || number.front() == '0') return false;
    size_t limit = 0;
    for (char c : number) { if (c < '0' || c > '9') return false; limit = limit * 10 + static_cast<size_t>(c - '0'); }
    if (limit < 4096 || limit > output_limit) return false;
    out = {values["binary_sha256"], values["source_sha256"], id, values["archive_origin"],
           values["archive_sha256"], values["run_id"], limit};
    return true;
}
} // namespace pdtrace
#endif
