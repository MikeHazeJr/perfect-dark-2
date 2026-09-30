#include "weapon_graph_v2_ingress.h"
#include "weapon_graph_v2.h"
#include "modarchive.h"
#include "modasset_gltf_document.h"
#include "crude_json.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <cmath>
#include <algorithm>
#include <cctype>

namespace {
using Json = crude_json::value;
void reject(const char *s) { throw std::runtime_error(s); }
std::string member(const void *bytes, uint32_t size, const char *name) {
    std::string path = name ? name : "";
    if (path.empty() || path.front() == '/' || path.back() == '/' || path.find_first_of("\\:") != std::string::npos)
        reject("graph source must name an exact relative archive member");
    size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..") reject("invalid graph source member");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    uint32_t length = 0;
    std::unique_ptr<void, decltype(&std::free)> source(modArchiveExtractMemAlloc(bytes, size, path.c_str(), &length), std::free);
    if (!source || !length) reject("missing graph/settings source member");
    return std::string(static_cast<const char *>(source.get()), length);
}
Json parse(const std::string &text) {
    Json result;
    if (!modAssetJsonReadValue(text.data(), text.size(), result) || !result.is_object()) reject("invalid complete graph/settings JSON");
    return result;
}
std::string text(const Json &json, const char *key) {
    if (!json.contains(key) || !json[key].is_string()) reject("missing graph/settings string");
    const auto &value = json[key].get<crude_json::string>();
    if (value.empty() || value.find('\0') != std::string::npos) reject("invalid graph/settings string");
    return value;
}
}
extern "C" int wgV2ArchiveInspect(const void *bytes, uint32_t size,
        weapon_graph_archive_descriptor_t *out, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (out) *out = {};
    try {
        weapon_graph_archive_descriptor_t descriptor{};
        if (!bytes || !size || weaponGraphArchiveReadDescriptorBytes(bytes, size, ASSET_WEAPON, &descriptor, error, cap)) return -1;
        const char *primary = descriptor.primary_graph[0] ? descriptor.primary_graph : descriptor.behavior_graph;
        const auto source = member(bytes, size, primary);
        const auto root = parse(source);
        const auto schema = text(root, "schema");
        if (schema == "pd.weapon_graph.v1") { if (out) *out = descriptor; return 0; }
        if (schema != "pd.weapon_graph.v2") reject("unsupported public weapon graph schema");
        if (descriptor.variables[0] || descriptor.shared_context[0] || descriptor.presentation[0])
            reject("held_single_shot.v1 does not implement variables/shared-context/presentation bindings");
        if (descriptor.primary_graph[0] && descriptor.behavior_graph[0] && std::strcmp(primary, descriptor.behavior_graph))
            reject("v2 weapon has conflicting primary and behavior graph bindings");
        std::string model = descriptor.model_file;
        std::transform(model.begin(), model.end(), model.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (model.size() < 7 || model.substr(model.size() - 7) != ".pdmesh") reject("v2 held model requires a selected public .pdmesh source");
        // Validate member syntax and presence, without treating model bytes as JSON.
        member(bytes, size, descriptor.model_file);
        const auto settings = parse(member(bytes, size, descriptor.settings));
        if (text(settings, "schema") != "pd.weapon_settings.v2" || text(settings, "asset_id") != descriptor.catalog_id)
            reject("v2 equipped settings identity/schema mismatch");
        if (!settings.contains("equipped") || !settings["equipped"].is_object()
                || !settings["equipped"].contains("modes") || !settings["equipped"]["modes"].is_array())
            reject("production v2 equipment requires explicit idle mode metadata");
        const auto &modes = settings["equipped"]["modes"].get<crude_json::array>();
        if (modes.size() != 2 || !modes[0].is_object() || (descriptor.secondary_graph[0] ? !modes[1].is_object() : !modes[1].is_null()))
            reject("v2 idle metadata must match primary and optional secondary graphs");
        char digest[65]{};
        if (weaponGraphArchiveCanonicalSha256Bytes(bytes, size, digest)) reject("cannot hash captured weapon source");
        const int count = descriptor.secondary_graph[0] ? 2 : 1;
        for (int mode = 0; mode < count; ++mode) {
            const auto &idle = modes[mode];
            if (idle.get<crude_json::object>().size() != 1 || !idle.contains("ammo_slot") || !idle["ammo_slot"].is_number())
                reject("idle metadata requires exactly ammo_slot");
            const double slot = idle["ammo_slot"].get<crude_json::number>();
            if (slot < -1 || slot > 1 || std::floor(slot) != slot) reject("invalid idle ammo slot");
            const auto graph = mode ? member(bytes, size, descriptor.secondary_graph) : source;
            std::unique_ptr<wg_v2_program, decltype(&wgV2ProgramRelease)> program(
                wgV2Compile(graph.data(), graph.size(), 1, digest, error, cap), wgV2ProgramRelease);
            if (!program) return -1;
            if (std::strcmp(wgV2ProgramAssetId(program.get()), descriptor.catalog_id)
                    || std::strcmp(wgV2ProgramMode(program.get()), mode ? "secondary" : "primary"))
                reject("v2 graph identity/mode differs from selected descriptor");
        }
        if (out) *out = descriptor;
        return 1;
    } catch (const std::exception &e) { if (error && cap) std::snprintf(error, cap, "%s", e.what()); }
    catch (...) { if (error && cap) std::snprintf(error, cap, "weapon source inspection allocation failed"); }
    return -1;
}
