#include "texture_stage_source.h"
#include "modasset_gltf_document.h"
#include "body_head_source.h"
#include "../external/imgui-node-editor/crude_json.h"
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
struct Row { std::string id; texture_source_properties_t properties{}; };
}
extern "C" int textureStageSourceRead(const char *json, size_t size, int multiplayer,
    int require_version, texture_stage_property_visitor visitor, void *context,
    char *error, size_t error_cap) {
    if (error && error_cap) error[0] = 0;
    try {
        crude_json::value graph;
        require(modAssetJsonReadValue(json, size, graph) && graph.is_object(), "invalid stage material JSON");
        if (graph.contains("texture_properties_version")) {
            const auto &version = graph["texture_properties_version"];
            require(version.is_number() && version.get<crude_json::number>() == 1,
                "unsupported stage material version");
        } else require(!require_version, "base stage material extraction upgrade required");
        if (!graph.contains("texture_properties")) return 1;
        const auto &source = graph["texture_properties"];
        require(source.is_object(), "invalid stage material properties");
        std::string mode = "all";
        if (source.contains("mode")) {
            require(source["mode"].is_string(), "invalid stage material mode");
            mode = source["mode"].get<crude_json::string>();
        }
        require(mode == "all" || mode == "solo" || mode == "multiplayer", "unknown stage material mode");
        if (!source.contains("entries")) return 1;
        require(source["entries"].is_array(), "invalid stage material entries");
        const char *keys[] = {"surface_type", "sound_surface_type", "tile_column_offset",
            "tile_row_offset", "mask_s_reduction", "mask_t_reduction"};
        std::set<std::string> ids;
        std::vector<Row> rows;
        for (const auto &entry : source["entries"].get<crude_json::array>()) {
            require(entry.is_object() && entry.contains("texture") && entry["texture"].is_string(),
                "stage material entry needs texture catalog ID");
            Row row; row.id = entry["texture"].get<crude_json::string>();
            require(bodyHeadSourceCatalogIdValid(row.id.c_str()) && ids.insert(row.id).second,
                "invalid or duplicate stage texture identity");
            std::string ini = "[texture]\ncatalog_id = " + row.id + "\n";
            for (unsigned i = 0; i < 6; ++i) if (entry.contains(keys[i])) {
                const auto &value = entry[keys[i]];
                std::string text;
                if (i < 2) {
                    require(value.is_string(), "stage surface type must be a name");
                    text = value.get<crude_json::string>();
                    require(text.find_first_of("\r\n") == std::string::npos, "invalid stage surface name");
                } else {
                    require(value.is_number(), "stage tile property must be an integer");
                    const double number = value.get<crude_json::number>();
                    require(std::isfinite(number) && number >= 0 && number <= 15 && number == std::floor(number),
                        "stage tile property out of range");
                    text = std::to_string(static_cast<unsigned>(number));
                }
                ini += std::string(keys[i]) + " = " + text + "\n";
            }
            if (!textureSourceReadProperties(ini.data(), ini.size(), row.id.c_str(),
                    &row.properties, error, error_cap)) return 0;
            rows.push_back(std::move(row));
        }
        const bool active = mode == "all" || (multiplayer ? mode == "multiplayer" : mode == "solo");
        if (active && visitor) for (const auto &row : rows)
            require(visitor(row.id.c_str(), &row.properties, context) == 0, "stage texture dependency rejected");
        return 1;
    } catch (const std::exception &e) {
        if (error && error_cap) std::snprintf(error, error_cap, "%s", e.what());
        return 0;
    } catch (...) {
        if (error && error_cap) std::snprintf(error, error_cap, "%s", "stage material parsing failed");
        return 0;
    }
}
