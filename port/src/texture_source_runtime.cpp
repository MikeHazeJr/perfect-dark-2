#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <stdexcept>
#include "texture_source_runtime.h"
#include "texture_stage_source.h"
#include "assetcatalog_load.h"
#include "fs.h"
#include "system.h"
#include "types.h"
#undef bool

extern "C" int g_NotLoadMod;
namespace {
struct Cached { bool loaded = false; bool suppress_overlays = false; struct texture definition{}; };
Cached cache[TEXTURE_CUSTOM_END];
std::map<int, texture_source_properties_t> stage_properties;
void apply(struct texture &out, const texture_source_properties_t &p, unsigned mask) {
    if (mask & 1) out.surfacetype = p.surface_type;
    if (mask & 2) out.soundsurfacetype = p.sound_surface_type;
    if (mask & 4) out.unk04_00 = p.tile_column_offset;
    if (mask & 8) out.unk04_04 = p.tile_row_offset;
    if (mask & 16) out.unk04_08 = p.mask_s_reduction;
    if (mask & 32) out.unk04_0c = p.mask_t_reduction;
}
int indexForNumber(int number) {
    const bool base_only = g_NotLoadMod && number < TEXTURE_CUSTOM_START;
    const CatalogResolveResult result = catalogResolveTexture(number);
    const asset_entry_t *indexed = assetCatalogGetByIndex(result.catalog_id);
    if (indexed && indexed->occupied && indexed->type == ASSET_TEXTURE
            && (indexed->bundled || (indexed->enabled && !base_only))) return result.catalog_id;
    int selected = -1;
    for (int i = 0; i < assetCatalogGetPoolSize(); ++i) {
        const auto *entry = assetCatalogGetByIndex(i);
        if (entry && entry->occupied && (entry->bundled || (entry->enabled && !base_only))
                && entry->type == ASSET_TEXTURE && (entry->source_texnum == number
                    || entry->ext.texture.texture_id == number)) selected = i;
    }
    if (selected < 0 && indexed && base_only)
        sysFatalError("ASSET.SOURCE_ONLY: texture %d overlay suppressed but no base public source exists", number);
    return selected;
}
int collectStage(const char *id, const texture_source_properties_t *properties, void *context) {
    auto &candidate = *static_cast<std::map<int, texture_source_properties_t> *>(context);
    for (int i = 0; i < assetCatalogGetPoolSize(); ++i) {
        const auto *entry = assetCatalogGetByIndex(i);
        if (!entry || !entry->occupied || std::strcmp(entry->id, id)) continue;
        if (entry->type != ASSET_TEXTURE || (!entry->enabled && !entry->bundled)) return 1;
        const int number = entry->source_texnum >= 0 ? entry->source_texnum : entry->ext.texture.texture_id;
        if (number < 0 || number >= TEXTURE_CUSTOM_END || candidate.count(number)) return 1;
        candidate.emplace(number, *properties); return 0;
    }
    return 1;
}
}
extern "C" int textureSourceRuntimeCatalogIndex(int number) {
    return number >= 0 && number < TEXTURE_CUSTOM_END ? indexForNumber(number) : -1;
}
extern "C" void textureSourceRuntimeInvalidate(void) {
    for (auto &entry : cache) entry = Cached{};
}
extern "C" void textureSourceRuntimeResetStage(void) {
    stage_properties.clear(); textureSourceRuntimeInvalidate();
}
extern "C" int textureSourceRuntimeReadSelected(const asset_entry_t *entry, const char *image_path,
    texture_source_properties_t *properties, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (properties) *properties = texture_source_properties_t{};
    try {
        if (!entry || !image_path || !properties) throw std::runtime_error("invalid texture source selection");
        std::string descriptor;
        if (!entry->source.override.provider && entry->descriptor_path[0]) descriptor = entry->descriptor_path;
        else {
            descriptor = image_path;
            const auto archive = descriptor.rfind("::");
            if (archive != std::string::npos) descriptor.resize(archive + 2);
            else {
                const auto slash = descriptor.find_last_of("/\\");
                descriptor.resize(slash == std::string::npos ? 0 : slash + 1);
            }
            descriptor += "texture.ini";
        }
        u32 size = 0; char *text = static_cast<char *>(fsFileLoad(descriptor.c_str(), &size));
        if (!text) {
            /* A loose mod image without an authored texture descriptor has
             * explicit standard defaults. Bundled/declared descriptors never do. */
            if (!entry->bundled && !entry->descriptor_path[0] && !std::strstr(image_path, "::")) return 1;
            throw std::runtime_error("selected public texture descriptor unavailable");
        }
        const bool ok = textureSourceReadProperties(text, size, entry->id, properties, error, cap) != 0;
        std::free(text);
        if (!ok) return 0;
        if (entry->bundled && !properties->properties_version
                && properties->present_mask != TEXTURE_SOURCE_ALL_PROPERTIES)
            throw std::runtime_error("base texture material extraction upgrade required");
        return 1;
    } catch (const std::exception &e) {
        if (error && cap) std::snprintf(error, cap, "%s", e.what()); return 0;
    } catch (...) {
        if (error && cap) std::snprintf(error, cap, "%s", "texture material source load failed"); return 0;
    }
}
extern "C" const struct texture *textureSourceRuntimeDefinition(int number) {
    static const struct texture empty{};
    if (number < 0 || number >= TEXTURE_CUSTOM_END) return &empty;
    auto &item = cache[number];
    const bool suppress = g_NotLoadMod && number < TEXTURE_CUSTOM_START;
    if (!item.loaded || item.suppress_overlays != suppress) {
        const auto *entry = assetCatalogGetByIndex(textureSourceRuntimeCatalogIndex(number));
        if (!entry) return &empty; // untextured/native sentinel has no catalog source
        const auto handle = catalogEffectiveHandle(entry);
        const char *path = handle.provider == fileProvider() ? fileProviderPath(handle) : nullptr;
        texture_source_properties_t properties{}; char error[192];
        if (!path || !textureSourceRuntimeReadSelected(entry, path, &properties, error, sizeof(error))) {
            sysFatalError("ASSET.SOURCE_ONLY: texture material '%s': %s", entry->id,
                path ? error : "no selected FileProvider source");
            return &empty;
        }
        item.definition = {};
        apply(item.definition, properties, TEXTURE_SOURCE_ALL_PROPERTIES);
        const auto overrides = stage_properties.find(number);
        if (overrides != stage_properties.end()) apply(item.definition, overrides->second, overrides->second.present_mask);
        item.suppress_overlays = suppress; item.loaded = true;
    }
    return &item.definition;
}
extern "C" int textureSourceRuntimeStageGraph(const char *json, size_t size, int multiplayer,
    int bundled, char *error, size_t error_cap) {
    try {
        std::map<int, texture_source_properties_t> candidate;
        if (!textureStageSourceRead(json, size, multiplayer, bundled, collectStage, &candidate, error, error_cap)) return 0;
        stage_properties.swap(candidate); textureSourceRuntimeInvalidate(); return 1;
    } catch (...) {
        if (error && error_cap) std::snprintf(error, error_cap, "%s", "stage material allocation failed");
        return 0;
    }
}
