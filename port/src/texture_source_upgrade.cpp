#include "texture_source_upgrade.h"
#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"
extern "C" {
#include "modarchive.h"
#include "sha256.h"
}
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fstream>
#include <iterator>

namespace {
using Json = crude_json::value;
using Bytes = std::unique_ptr<void, decltype(&std::free)>;
using Archive = std::unique_ptr<mod_archive_t, decltype(&modArchiveClose)>;
using Writer = std::unique_ptr<mod_archive_writer_t, decltype(&modArchiveAbort)>;
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int failure(char *error, size_t cap, const char *message) {
    if (error && cap) std::snprintf(error, cap, "%s", message);
    return -1;
}
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
std::string hash(const std::string &bytes) {
    u8 digest[SHA256_DIGEST_SIZE]; char hex[SHA256_HEX_SIZE];
    sha256Hash(bytes.data(), bytes.size(), digest); sha256ToHex(digest, hex);
    return hex;
}
struct Span { size_t start, end; };
/* Locate tokens only after the shared strict JSON parser has accepted the whole
 * document. Preserve unknown numbers/strings and formatting byte-for-byte. */
size_t tokenEnd(const std::string &text, size_t at) {
    bool quoted = false, escape = false; int depth = 0;
    for (size_t i = at; i < text.size(); ++i) {
        char c = text[i];
        if (quoted) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') { quoted = false; if (!depth) return i + 1; }
        } else {
            if (!depth && (space(c) || c == ',' || c == ']' || c == '}')) return i;
            if (c == '"') quoted = true;
            else if (c == '{' || c == '[') ++depth;
            else if ((c == '}' || c == ']') && !--depth) return i + 1;
        }
    }
    return text.size();
}
std::string stringAt(const std::string &text, Span span) {
    Json value;
    require(modAssetJsonReadValue(text.data() + span.start, span.end - span.start, value)
        && value.is_string(), "invalid metadata string");
    return value.get<crude_json::string>();
}
std::map<std::string, Span> fields(const std::string &text, Span object) {
    require(text[object.start] == '{', "invalid metadata object");
    std::map<std::string, Span> result;
    size_t at = object.start + 1;
    while (at < object.end - 1) {
        while (at < object.end && (space(text[at]) || text[at] == ',')) ++at;
        if (at == object.end - 1) break;
        const size_t key_end = tokenEnd(text, at);
        const auto key = stringAt(text, {at, key_end}); at = key_end;
        while (space(text[at])) ++at;
        require(text[at++] == ':', "invalid metadata field");
        while (space(text[at])) ++at;
        const size_t end = tokenEnd(text, at);
        require(result.emplace(key, Span{at, end}).second, "duplicate metadata field");
        at = end;
    }
    return result;
}
struct Edit { Span span; std::string replacement; };
std::string updateRecord(const std::string &text, const char *schema,
    const std::string &descriptor, const char *member = "texture.ini") {
    Json document;
    require(modAssetJsonReadValue(text.data(), text.size(), document) && document.is_object(),
        "invalid texture archive metadata JSON");
    size_t start = 0; while (start < text.size() && space(text[start])) ++start;
    const auto root = fields(text, {start, tokenEnd(text, start)});
    require(root.count("schema") && stringAt(text, root.at("schema")) == schema,
        "unsupported texture archive metadata schema");
    require(root.count("entries"), "missing texture metadata entries");
    const Span entries = root.at("entries");
    require(text[entries.start] == '[', "invalid texture metadata entries");
    std::set<std::string> paths;
    std::vector<Edit> edits;
    for (size_t at = entries.start + 1; at < entries.end - 1;) {
        while (at < entries.end && (space(text[at]) || text[at] == ',')) ++at;
        if (at == entries.end - 1) break;
        const size_t end = tokenEnd(text, at);
        const auto row = fields(text, {at, end}); at = end;
        require(row.count("path"), "missing texture metadata entry path");
        const auto path = stringAt(text, row.at("path"));
        require(!path.empty() && paths.insert(path).second, "duplicate texture metadata entry");
        if (path != member) continue;
        require(row.count("sha256") && row.count("size"), "missing texture descriptor hash record");
        require(!stringAt(text, row.at("sha256")).empty(), "invalid texture descriptor hash record");
        Json size;
        const auto size_span = row.at("size");
        require(modAssetJsonReadValue(text.data() + size_span.start,
            size_span.end - size_span.start, size) && size.is_number(), "invalid texture descriptor size");
        edits.push_back({row.at("sha256"), "\"" + hash(descriptor) + "\""});
        edits.push_back({size_span, std::to_string(descriptor.size())});
    }
    require(edits.size() == 2, "missing texture descriptor inventory row");
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.span.start > b.span.start; });
    std::string output = text;
    for (const auto &edit : edits) output.replace(edit.span.start, edit.span.end - edit.span.start, edit.replacement);
    return output;
}
void replaceSource(Archive &archive, const char *path, const char *member,
    const std::string &payload) {
    std::map<std::string, std::string> replacements{{member, payload}};
    const char *metadata[] = {"_meta/hashes.json", "_meta/inventory.json"};
    const char *schemas[] = {"pd2.asset.hashes.v1", "pd2.asset.inventory.v1"};
    for (unsigned i = 0; i < 2; ++i) {
        const s32 index = modArchiveFindEntry(archive.get(), metadata[i]);
        require(index >= 0, "missing archive metadata");
        u32 bytes = 0;
        Bytes data(modArchiveExtractAlloc(archive.get(), index, &bytes), &std::free);
        require(bool(data), "cannot read archive metadata");
        replacements[metadata[i]] = updateRecord(std::string(static_cast<const char *>(data.get()), bytes),
            schemas[i], payload, member);
    }
    const std::string sidecar = std::string("_meta/") + member + ".sha256";
    require(modArchiveFindEntry(archive.get(), sidecar.c_str()) >= 0, "missing source hash sidecar");
    replacements[sidecar] = hash(payload) + "\n";
    Writer writer(modArchiveBegin(path), &modArchiveAbort);
    require(bool(writer), "cannot begin atomic source upgrade");
    modArchiveSetComment(writer.get(), modArchiveGetComment(archive.get()));
    std::set<std::string> copied;
    for (s32 i = 0; i < modArchiveGetEntryCount(archive.get()); ++i) {
        const char *name = modArchiveGetEntryName(archive.get(), i);
        require(name && copied.insert(name).second, "duplicate archive member");
        u32 bytes = 0;
        Bytes data(modArchiveExtractAlloc(archive.get(), i, &bytes), &std::free);
        require(bool(data), "cannot read archive member");
        const auto replacement = replacements.find(name);
        const void *content = data.get();
        if (replacement != replacements.end()) {
            require(replacement->second.size() <= std::numeric_limits<u32>::max(), "source member too large");
            content = replacement->second.data(); bytes = static_cast<u32>(replacement->second.size());
        }
        require(modArchiveAddFileMem(writer.get(), name, content, bytes) == MODARCHIVE_OK,
            "cannot write archive member");
    }
    archive.reset();
    require(modArchiveFinish(writer.release()) == MODARCHIVE_OK, "cannot commit source archive upgrade");
}
struct TempArchive {
    char path[MAX_PATH]{};
    TempArchive() {
        char dir[MAX_PATH];
        const DWORD length = GetTempPathA(MAX_PATH, dir);
        require(length && length < MAX_PATH && GetTempFileNameA(dir, "pdt", 0, path),
            "cannot reserve temporary nested archive");
    }
    ~TempArchive() { if (path[0]) DeleteFileA(path); }
};

}

extern "C" int textureSourceUpgradeDescriptor(const char *text, size_t size, const char *id,
    const texture_source_properties_t *defaults, char **output, size_t *output_size,
    char *error, size_t error_cap) {
    if (output) *output = nullptr;
    if (output_size) *output_size = 0;
    if (error && error_cap) error[0] = 0;
    try {
        require(output && output_size && id && defaults, "invalid texture upgrade inputs");
        texture_source_properties_t existing{};
        if (!textureSourceReadProperties(text, size, id, &existing, error, error_cap)) return -1;
        if (existing.properties_version == 1) return 0;
        const char *names[] = {"surface_type", "sound_surface_type", "tile_column_offset",
            "tile_row_offset", "mask_s_reduction", "mask_t_reduction"};
        const unsigned values[] = {defaults->surface_type, defaults->sound_surface_type,
            defaults->tile_column_offset, defaults->tile_row_offset,
            defaults->mask_s_reduction, defaults->mask_t_reduction};
        require(textureSourceSurfaceName(values[0]) && textureSourceSurfaceName(values[1])
            && values[2] <= 15 && values[3] <= 15 && values[4] <= 15 && values[5] <= 15,
            "invalid original texture properties");
        std::string source(text, size);
        const std::string newline = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        size_t insertion = std::string::npos;
        for (size_t start = 0; start < size;) {
            const size_t lf = source.find('\n', start), end = lf == std::string::npos ? size : lf;
            size_t a = start, b = end;
            while (a < b && space(source[a])) ++a;
            while (b > a && space(source[b - 1])) --b;
            if (source.compare(a, b - a, "[texture]") == 0) { insertion = end; break; }
            start = end + 1;
        }
        require(insertion != std::string::npos, "missing texture section");
        /* Insert after the header's existing newline; preserve every byte of
         * original source, including extension sections and an absent final LF. */
        const bool has_lf = insertion < size;
        if (has_lf) ++insertion;
        std::string added = has_lf ? "" : newline;
        added += "properties_version = 1" + newline;
        for (unsigned i = 0; i < 6; ++i) if (!(existing.present_mask & (1u << i)))
            added += std::string(names[i]) + " = " + (i < 2 ? std::string(textureSourceSurfaceName(values[i]))
                : std::to_string(values[i])) + newline;
        require(source.size() <= std::numeric_limits<u32>::max() - added.size(), "texture descriptor too large");
        source.insert(insertion, added);
        texture_source_properties_t verified{};
        if (!textureSourceReadProperties(source.data(), source.size(), id, &verified, error, error_cap)) return -1;
        auto result = Bytes(std::malloc(source.size() + 1), &std::free);
        require(bool(result), "texture upgrade allocation failed");
        std::memcpy(result.get(), source.c_str(), source.size() + 1);
        *output_size = source.size(); *output = static_cast<char *>(result.release()); return 1;
    } catch (const std::exception &e) { return failure(error, error_cap, e.what()); }
    catch (...) { return failure(error, error_cap, "texture descriptor upgrade failed"); }
}

extern "C" int textureSourceUpgradeArchive(const char *path, const char *id,
    const texture_source_properties_t *defaults, char *error, size_t error_cap) {
    if (error && error_cap) error[0] = 0;
    try {
        require(path && path[0], "invalid texture archive path");
        Archive archive(modArchiveOpen(path), &modArchiveClose);
        require(bool(archive), "cannot open existing texture archive");
        require(modArchiveCanRewriteSources(archive.get()), "partial texture archive cannot be upgraded");
        const s32 descriptor_index = modArchiveFindEntry(archive.get(), "texture.ini");
        require(descriptor_index >= 0, "missing texture descriptor");
        u32 size = 0;
        Bytes descriptor(modArchiveExtractAlloc(archive.get(), descriptor_index, &size), &std::free);
        require(bool(descriptor), "cannot read existing texture descriptor");
        char *updated = nullptr; size_t updated_size = 0;
        const int changed = textureSourceUpgradeDescriptor(static_cast<const char *>(descriptor.get()),
            size, id, defaults, &updated, &updated_size, error, error_cap);
        Bytes owned(updated, &std::free);
        if (changed <= 0) return changed;
        replaceSource(archive, path, "texture.ini", std::string(updated, updated_size));
        return 1;
    } catch (const std::exception &e) { return failure(error, error_cap, e.what()); }
    catch (...) { return failure(error, error_cap, "texture archive upgrade failed"); }
}

extern "C" int textureSourceUpgradeStageArchive(const char *path, const char *member,
    const char *properties_json, char *error, size_t error_cap) {
    if (error && error_cap) error[0] = 0;
    try {
        require(path && path[0] && properties_json, "invalid stage texture upgrade input");
        Archive archive(modArchiveOpen(path), &modArchiveClose);
        require(bool(archive), "cannot read existing stage archive");
        require(modArchiveCanRewriteSources(archive.get()), "partial stage archive cannot be upgraded");
        if (member && member[0]) {
            const s32 index = modArchiveFindEntry(archive.get(), member);
            require(index >= 0, "missing nested scenario archive");
            u32 size = 0;
            Bytes data(modArchiveExtractAlloc(archive.get(), index, &size), &std::free);
            require(bool(data), "cannot read nested scenario archive");
            u32 graph_size = 0;
            Bytes graph_bytes(modArchiveExtractMemAlloc(data.get(), size, "level.graph.json", &graph_size), &std::free);
            Json current_graph;
            require(graph_bytes && modAssetJsonReadValue(static_cast<const char *>(graph_bytes.get()),
                graph_size, current_graph) && current_graph.is_object(), "invalid embedded level graph");
            if (current_graph.contains("texture_properties_version")) {
                const auto &version = current_graph["texture_properties_version"];
                require(version.is_number() && version.get<crude_json::number>() == 1,
                    "unsupported embedded stage material version");
                return 0;
            }
            TempArchive temporary;
            std::ofstream output(temporary.path, std::ios::binary | std::ios::trunc);
            output.write(static_cast<const char *>(data.get()), size); output.close();
            require(bool(output), "cannot prepare nested scenario archive");
            const int result = textureSourceUpgradeStageArchive(temporary.path, nullptr,
                properties_json, error, error_cap);
            if (result <= 0) return result;
            std::ifstream input(temporary.path, std::ios::binary);
            std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            require(!input.bad() && !bytes.empty(), "cannot read upgraded nested scenario");
            replaceSource(archive, path, member, bytes);
            return 1;
        }
        const s32 index = modArchiveFindEntry(archive.get(), "level.graph.json");
        require(index >= 0, "missing public level graph");
        u32 size = 0;
        Bytes bytes(modArchiveExtractAlloc(archive.get(), index, &size), &std::free);
        require(bool(bytes), "cannot read public level graph");
        std::string text(static_cast<const char *>(bytes.get()), size);
        Json graph;
        require(modAssetJsonReadValue(text.data(), text.size(), graph) && graph.is_object(),
            "invalid public level graph");
        if (graph.contains("texture_properties_version")) {
            const auto &version = graph["texture_properties_version"];
            require(version.is_number() && version.get<crude_json::number>() == 1,
                "unsupported stage texture properties version");
            return 0;
        }
        Json properties;
        require(modAssetJsonReadValue(properties_json, std::strlen(properties_json), properties)
            && properties.is_object(), "invalid extracted stage texture properties");
        size_t start = 0; while (start < text.size() && space(text[start])) ++start;
        std::string added = "\n  \"texture_properties_version\": 1";
        if (!graph.contains("texture_properties"))
            added += std::string(",\n  \"texture_properties\": ") + properties_json;
        if (!graph.get<crude_json::object>().empty()) added += ',';
        text.insert(start + 1, added);
        replaceSource(archive, path, "level.graph.json", text);
        return 1;
    } catch (const std::exception &e) { return failure(error, error_cap, e.what()); }
    catch (...) { return failure(error, error_cap, "stage texture upgrade failed"); }
}
