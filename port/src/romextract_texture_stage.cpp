#include "romextract_texture_stage.h"
#include "texture_source_upgrade.h"
#include "assetcatalog.h"
#include "../external/imgui-node-editor/crude_json.h"
#include <cstring>
#include <cstdio>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
namespace {
/* Original authored stage material assignments are extraction input only.
 * Runtime consumes the exported level.graph.json properties by public ID. */
struct OriginalRule { int group, texture; const char *field, *surface; };
const OriginalRule rules[] = {
    {0, 0x073c, "surface_type", "default"},
    {0, 0x073d, "surface_type", "default"},
    {0, 0x073e, "sound_surface_type", "metal"},
    {0, 0x073f, "sound_surface_type", "metal"},
    {0, 0x0740, "sound_surface_type", "metal"},
    {0, 0x0741, "sound_surface_type", "metal"},
    {0, 0x0745, "surface_type", "default"},
    {0, 0x0746, "sound_surface_type", "shallow_water"},
    {0, 0x0746, "surface_type", "shallow_water"},
    {0, 0x0bde, "surface_type", "glass"},
    {0, 0x0bde, "sound_surface_type", "glass"},
    {0, 0x06ff, "surface_type", "default"},
    {0, 0x0716, "surface_type", "default"},
    {0, 0x0716, "sound_surface_type", "default"},
    {0, 0x0a16, "surface_type", "default"},
    {0, 0x0a16, "sound_surface_type", "default"},
    {0, 0x0a17, "surface_type", "default"},
    {0, 0x0a17, "sound_surface_type", "default"},
    {0, 0x0208, "surface_type", "default"},
    {0, 0x0208, "sound_surface_type", "default"},
    {0, 0x06fc, "surface_type", "default"},
    {0, 0x065a, "surface_type", "metal"},
    {0, 0x065a, "sound_surface_type", "metal"},
    {1, 0x0c31, "sound_surface_type", "dirt"},
    {1, 0x0c3b, "sound_surface_type", "mud"},
    {1, 0x0c3c, "sound_surface_type", "mud"},
    {1, 0x0c3d, "sound_surface_type", "dirt"},
    {1, 0x0c3e, "sound_surface_type", "dirt"},
    {1, 0x0c42, "sound_surface_type", "wood"},
    {1, 0x0c43, "sound_surface_type", "stone"},
    {1, 0x0c45, "sound_surface_type", "stone"},
    {1, 0x0c48, "sound_surface_type", "stone"},
    {1, 0x0c49, "sound_surface_type", "mud"},
    {1, 0x0c4a, "sound_surface_type", "none"},
    {1, 0x0c4b, "sound_surface_type", "shallow_water"},
    {1, 0x0c4c, "sound_surface_type", "deep_water"},
    {1, 0x0c63, "sound_surface_type", "dirt"},
    {1, 0x0c64, "sound_surface_type", "stone"},
    {1, 0x0c65, "sound_surface_type", "stone"},
    {1, 0x0c67, "sound_surface_type", "stone"},
    {1, 0x0c68, "sound_surface_type", "stone"},
    {1, 0x0c69, "sound_surface_type", "stone"},
    {1, 0x0c6a, "sound_surface_type", "dirt"},
    {1, 0x0c6b, "sound_surface_type", "stone"},
    {1, 0x0c6c, "sound_surface_type", "dirt"},
    {1, 0x0c6e, "sound_surface_type", "wood"},
    {1, 0x0c6f, "sound_surface_type", "metal"},
    {1, 0x0c73, "sound_surface_type", "wood"},
    {1, 0x0c74, "sound_surface_type", "stone"},
    {1, 0x0c75, "sound_surface_type", "stone"},
    {1, 0x0c77, "sound_surface_type", "mud"},
    {1, 0x0c78, "sound_surface_type", "stone"},
    {1, 0x0c79, "sound_surface_type", "stone"},
    {1, 0x0c7a, "sound_surface_type", "dirt"},
    {1, 0x0c7b, "sound_surface_type", "dirt"},
    {1, 0x0c7c, "sound_surface_type", "stone"},
    {1, 0x0c7e, "sound_surface_type", "metal"},
    {1, 0x0c7f, "sound_surface_type", "wood"},
    {1, 0x0c81, "sound_surface_type", "wood"},
    {1, 0x0c82, "sound_surface_type", "wood"},
    {1, 0x0c83, "sound_surface_type", "stone"},
    {1, 0x0c84, "sound_surface_type", "dirt"},
    {1, 0x0c86, "sound_surface_type", "dirt"},
    {1, 0x0c8a, "sound_surface_type", "wood"},
    {1, 0x0c8b, "sound_surface_type", "wood"},
    {1, 0x0c8c, "sound_surface_type", "wood"},
    {1, 0x0c8d, "sound_surface_type", "wood"},
    {1, 0x0c8f, "sound_surface_type", "dirt"},
    {1, 0x0c31, "surface_type", "dirt"},
    {1, 0x0c3b, "surface_type", "mud"},
    {1, 0x0c3c, "surface_type", "mud"},
    {1, 0x0c3d, "surface_type", "dirt"},
    {1, 0x0c3e, "surface_type", "none"},
    {1, 0x0c42, "surface_type", "wood"},
    {1, 0x0c43, "surface_type", "stone"},
    {1, 0x0c45, "surface_type", "stone"},
    {1, 0x0c48, "surface_type", "stone"},
    {1, 0x0c49, "surface_type", "mud"},
    {1, 0x0c4a, "surface_type", "none"},
    {1, 0x0c4b, "surface_type", "shallow_water"},
    {1, 0x0c4c, "surface_type", "shallow_water"},
    {1, 0x0c63, "surface_type", "dirt"},
    {1, 0x0c64, "surface_type", "stone"},
    {1, 0x0c65, "surface_type", "stone"},
    {1, 0x0c67, "surface_type", "stone"},
    {1, 0x0c68, "surface_type", "stone"},
    {1, 0x0c69, "surface_type", "stone"},
    {1, 0x0c6a, "surface_type", "none"},
    {1, 0x0c6b, "surface_type", "stone"},
    {1, 0x0c6c, "surface_type", "dirt"},
    {1, 0x0c6e, "surface_type", "wood"},
    {1, 0x0c6f, "surface_type", "metal"},
    {1, 0x0c73, "surface_type", "wood"},
    {1, 0x0c74, "surface_type", "stone"},
    {1, 0x0c75, "surface_type", "stone"},
    {1, 0x0c77, "surface_type", "mud"},
    {1, 0x0c78, "surface_type", "stone"},
    {1, 0x0c79, "surface_type", "stone"},
    {1, 0x0c7a, "surface_type", "dirt"},
    {1, 0x0c7b, "surface_type", "dirt"},
    {1, 0x0c7c, "surface_type", "stone"},
    {1, 0x0c7e, "surface_type", "metal"},
    {1, 0x0c7f, "surface_type", "wood"},
    {1, 0x0c81, "surface_type", "wood"},
    {1, 0x0c82, "surface_type", "wood"},
    {1, 0x0c83, "surface_type", "stone"},
    {1, 0x0c84, "surface_type", "dirt"},
    {1, 0x0c86, "surface_type", "dirt"},
    {1, 0x0c88, "surface_type", "none"},
    {1, 0x0c8a, "surface_type", "wood"},
    {1, 0x0c8b, "surface_type", "wood"},
    {1, 0x0c8c, "surface_type", "wood"},
    {1, 0x0c8d, "surface_type", "wood"},
    {1, 0x0c8e, "surface_type", "none"},
    {1, 0x0c8f, "surface_type", "dirt"},
    {1, 0x0048, "sound_surface_type", "wood"},
    {1, 0x0049, "sound_surface_type", "mud"},
    {1, 0x004A, "sound_surface_type", "mud"},
    {1, 0x004B, "sound_surface_type", "wood"},
    {1, 0x004C, "sound_surface_type", "default"},
    {1, 0x004D, "sound_surface_type", "default"},
    {1, 0x004E, "sound_surface_type", "default"},
    {1, 0x004F, "sound_surface_type", "wood"},
    {1, 0x0050, "sound_surface_type", "stone"},
    {1, 0x0051, "sound_surface_type", "mud"},
    {1, 0x0052, "sound_surface_type", "default"},
    {1, 0x0053, "sound_surface_type", "stone"},
    {1, 0x0054, "sound_surface_type", "mud"},
    {1, 0x0056, "sound_surface_type", "wood"},
    {1, 0x0057, "sound_surface_type", "mud"},
    {1, 0x005C, "sound_surface_type", "metal"},
    {1, 0x005D, "sound_surface_type", "wood"},
    {1, 0x005E, "sound_surface_type", "stone"},
    {1, 0x005F, "sound_surface_type", "stone"},
    {1, 0x0060, "sound_surface_type", "stone"},
    {1, 0x0061, "sound_surface_type", "stone"},
    {1, 0x0062, "sound_surface_type", "dirt"},
    {1, 0x0064, "sound_surface_type", "wood"},
    {1, 0x0065, "sound_surface_type", "wood"},
    {1, 0x0067, "sound_surface_type", "wood"},
    {1, 0x0068, "sound_surface_type", "wood"},
    {1, 0x0048, "surface_type", "wood"},
    {1, 0x0049, "surface_type", "mud"},
    {1, 0x004A, "surface_type", "mud"},
    {1, 0x004B, "surface_type", "wood"},
    {1, 0x004C, "surface_type", "default"},
    {1, 0x004D, "surface_type", "default"},
    {1, 0x004E, "surface_type", "default"},
    {1, 0x004F, "surface_type", "none"},
    {1, 0x0050, "surface_type", "stone"},
    {1, 0x0051, "surface_type", "none"},
    {1, 0x0052, "surface_type", "default"},
    {1, 0x0053, "surface_type", "stone"},
    {1, 0x0054, "surface_type", "none"},
    {1, 0x0056, "surface_type", "wood"},
    {1, 0x0057, "surface_type", "none"},
    {1, 0x005C, "surface_type", "metal"},
    {1, 0x005D, "surface_type", "wood"},
    {1, 0x005E, "surface_type", "stone"},
    {1, 0x005F, "surface_type", "stone"},
    {1, 0x0060, "surface_type", "stone"},
    {1, 0x0061, "surface_type", "metal"},
    {1, 0x0062, "surface_type", "dirt"},
    {1, 0x0064, "surface_type", "wood"},
    {1, 0x0065, "surface_type", "wood"},
    {1, 0x0067, "surface_type", "wood"},
    {1, 0x0068, "surface_type", "wood"},
    {2, 0x0281, "surface_type", "default"},
    {2, 0x0281, "sound_surface_type", "default"},
};
int groupForStage(const char *id) {
    if (!id) return -1;
    if (!std::strcmp(id, "base:test_silo")
            || !std::strcmp(id, "base:test_lam")
            || !std::strcmp(id, "base:test_mp8")
            || !std::strcmp(id, "base:test_mp14")
            || !std::strcmp(id, "base:test_mp16")
            || !std::strcmp(id, "base:test_mp17")
            || !std::strcmp(id, "base:test_mp18")
            || !std::strcmp(id, "base:test_mp19")
            || !std::strcmp(id, "base:test_mp20")
            || !std::strcmp(id, "base:extra1")
            || !std::strcmp(id, "base:extra2")
            || !std::strcmp(id, "base:extra3")
            || !std::strcmp(id, "base:extra4")
            || !std::strcmp(id, "base:extra5")
            || !std::strcmp(id, "base:extra6")
            || !std::strcmp(id, "base:extra7")
            || !std::strcmp(id, "base:extra8")
            || !std::strcmp(id, "base:extra9")
            || !std::strcmp(id, "base:extra10")
            || !std::strcmp(id, "base:extra11")
            || !std::strcmp(id, "base:extra12")
            || !std::strcmp(id, "base:extra13")
            || !std::strcmp(id, "base:extra14")
            || !std::strcmp(id, "base:extra15")
            || !std::strcmp(id, "base:extra16")
            || !std::strcmp(id, "base:extra17")
            || !std::strcmp(id, "base:extra24")
            || !std::strcmp(id, "base:extra25")) return 0;
    if (!std::strcmp(id, "base:stage_24")
            || !std::strcmp(id, "base:extra18")
            || !std::strcmp(id, "base:extra19")
            || !std::strcmp(id, "base:extra26")) return 1;
    if (!std::strcmp(id, "base:extra20")
            || !std::strcmp(id, "base:extra21")
            || !std::strcmp(id, "base:extra22")
            || !std::strcmp(id, "base:extra23")) return 2;
    return -1;
}
}
extern "C" int romExtractTextureStageProperties(const char *path, const char *member,
    const char *stage_id, char *error, size_t error_cap) {
    try {
        const int group = groupForStage(stage_id);
        std::map<int, std::string> ids;
        for (int i = 0; i < assetCatalogGetPoolSize(); ++i) {
            const auto *entry = assetCatalogGetByIndex(i);
            if (entry && entry->occupied && entry->bundled && entry->type == ASSET_TEXTURE)
                ids.emplace(entry->ext.texture.texture_id, entry->id);
        }
        std::map<std::string, crude_json::value> entries;
        for (const auto &rule : rules) if (rule.group == group) {
            const auto id = ids.find(rule.texture);
            if (id == ids.end()) throw std::runtime_error("missing base texture identity for original stage rule");
            auto &row = entries[id->second];
            row["texture"] = id->second;
            row[rule.field] = rule.surface;
        }
        crude_json::value result(crude_json::object{});
        result["mode"] = "multiplayer";
        result["entries"] = crude_json::array{};
        for (const auto &entry : entries) result["entries"].push_back(entry.second);
        const std::string json = result.dump(2);
        return textureSourceUpgradeStageArchive(path, member, json.c_str(), error, error_cap);
    } catch (const std::exception &e) {
        if (error && error_cap) std::snprintf(error, error_cap, "%s", e.what());
        return -1;
    } catch (...) {
        if (error && error_cap) std::snprintf(error, error_cap, "%s", "stage property extraction failed");
        return -1;
    }
}
