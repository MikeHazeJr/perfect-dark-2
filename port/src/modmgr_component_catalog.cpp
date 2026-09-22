#include "modmgr_component_catalog.h"
#include "modmgr_component_state.h"
#include "assetcatalog_mutation.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <utility>

namespace {
const asset_type_e manageable[] = {
    ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
    ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY,
    ASSET_ARENA, ASSET_BODY, ASSET_HEAD, ASSET_MODEL, ASSET_ANIMATION,
    ASSET_TEXTURES, ASSET_TEXTURE, ASSET_MATERIAL, ASSET_EFFECT,
    ASSET_SFX, ASSET_MUSIC, ASSET_AUDIO,
    ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_GAMEMODE,
    ASSET_BOT_PROFILE, ASSET_SCENARIO, ASSET_HUD, ASSET_UI,
    ASSET_FONT, ASSET_LANG, ASSET_THEME, ASSET_TOOL
};
int reject(modmgr_component_result_t &result, const char *message)
{
    std::snprintf(result.error, sizeof(result.error), "%s", message);
    return 0;
}
std::string normalized(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}
bool within(const std::string &source, const std::string &directory)
{
    return source.compare(0, directory.size(), directory) == 0 &&
        (source.size() == directory.size() || source[directory.size()] == '/');
}
struct Snapshot {
    std::vector<modmgr_component_choice_t> choices;
    std::vector<std::string> session_dirs;
    u32 generation = 0;
    bool failed = false;
};
void capture(const asset_entry_t *entry, void *userdata)
{
    auto &snapshot = *static_cast<Snapshot *>(userdata);
    if (snapshot.failed) return;
    try {
        if (!std::memchr(entry->id, '\0', sizeof(entry->id)) ||
                !std::memchr(entry->dirpath, '\0', sizeof(entry->dirpath)) ||
                !std::memchr(entry->descriptor_path, '\0', sizeof(entry->descriptor_path))) {
            snapshot.failed = true;
            return;
        }
        modmgr_component_choice_t choice{};
        std::memcpy(choice.id, entry->id, sizeof(choice.id));
        choice.enabled = entry->enabled;
        choice.persistent = !entry->bundled && !entry->temporary;
        if (choice.persistent) {
            const auto source = normalized(entry->descriptor_path);
            const auto directory = normalized(entry->dirpath);
            for (const auto &session : snapshot.session_dirs) {
                if (within(source, session) || within(directory, session)) {
                    choice.persistent = 0;
                    break;
                }
            }
        }
        snapshot.choices.push_back(choice);
    } catch (...) {
        snapshot.failed = true;
    }
}
bool snapshotCatalog(Snapshot &snapshot, const char *const *session_dirs,
    size_t session_count, modmgr_component_result_t &result)
{
    if (session_count && !session_dirs) {
        reject(result, "Missing session package ownership.");
        return false;
    }
    for (size_t i = 0; i < session_count; ++i) {
        if (!session_dirs[i] || !session_dirs[i][0]) {
            reject(result, "Invalid session package source directory.");
            return false;
        }
        snapshot.session_dirs.push_back(normalized(session_dirs[i]));
    }
    snapshot.generation = assetCatalogGetGeneration();
    for (auto type : manageable)
        assetCatalogIterateByTypeIncludingDisabled(type, capture, &snapshot);
    if (snapshot.failed || snapshot.generation != assetCatalogGetGeneration()) {
        reject(result, "Catalog changed or could not be copied while preparing component state.");
        return false;
    }
    std::set<std::string> ids;
    for (const auto &choice : snapshot.choices) {
        if (!choice.id[0] || !ids.insert(choice.id).second) {
            reject(result, "Catalog component identities are invalid or duplicated.");
            return false;
        }
    }
    return true;
}
}

struct modmgr_component_plan {
    std::vector<modmgr_component_choice_t> choices;
    std::string path;
};

extern "C" int modmgrPrepareCatalogComponentPlan(const char *path,
    const char *const *session_dirs, size_t session_count,
    const modmgr_component_override_t *overrides, size_t override_count,
    modmgr_component_plan_t **plan, modmgr_component_result_t *result)
{
    if (plan) *plan = nullptr;
    if (!result) return 0;
    *result = {};
    if (!plan || (override_count && !overrides))
        return reject(*result, "Missing component selection or output.");
    try {
        Snapshot snapshot;
        if (!snapshotCatalog(snapshot, session_dirs, session_count, *result)) return 0;
        std::set<std::string> seen;
        for (size_t i = 0; i < override_count; ++i) {
            const auto &change = overrides[i];
            if (!std::memchr(change.id, '\0', sizeof(change.id)) || !change.id[0] ||
                    !seen.insert(change.id).second || (change.enabled != 0 && change.enabled != 1))
                return reject(*result, "Component selection is invalid or duplicated.");
            auto row = std::find_if(snapshot.choices.begin(), snapshot.choices.end(),
                [&change](const modmgr_component_choice_t &choice) {
                    return std::strcmp(choice.id, change.id) == 0;
                });
            if (row == snapshot.choices.end() || !row->persistent) {
                std::snprintf(result->id, sizeof(result->id), "%s", change.id);
                return reject(*result, "A selected component is missing or belongs to the base game/session.");
            }
            row->enabled = change.enabled;
        }
        for (const auto &choice : snapshot.choices) {
            ++result->examined;
            if (!choice.persistent) ++result->excluded;
        }
        if ((!path || !path[0]) && result->examined != result->excluded)
            return reject(*result, "No mods directory for persistent component preferences.");
        auto candidate = std::make_unique<modmgr_component_plan>();
        candidate->choices = std::move(snapshot.choices);
        candidate->path = path ? path : "";
        *plan = candidate.release();
        return 1;
    } catch (...) {
        return reject(*result, "Could not prepare component selection.");
    }
}

extern "C" int modmgrSaveCatalogComponentPlan(const modmgr_component_plan_t *plan,
    modmgr_component_result_t *result)
{
    if (!result) return 0;
    *result = {};
    if (!plan) return reject(*result, "Missing prepared component selection.");
    for (const auto &choice : plan->choices) {
        ++result->examined;
        if (!choice.persistent) ++result->excluded;
    }
    if (plan->path.empty()) {
        result->source_missing = 1;
        return 1;
    }
    if (!modmgrSaveComponentStateFile(plan->path.c_str(), plan->choices.data(),
            plan->choices.size(), result->error, sizeof(result->error))) return 0;
    result->saved = 1;
    return 1;
}

extern "C" void modmgrFreeCatalogComponentPlan(modmgr_component_plan_t *plan)
{
    delete plan;
}

extern "C" int modmgrSaveCatalogComponentState(const char *path,
    const char *const *session_dirs, size_t session_count,
    modmgr_component_result_t *result)
{
    if (!result) return 0;
    *result = {};
    try {
        Snapshot snapshot;
        if (!snapshotCatalog(snapshot, session_dirs, session_count, *result)) return 0;
        for (const auto &choice : snapshot.choices) {
            ++result->examined;
            if (!choice.persistent) ++result->excluded;
        }
        if (!path || !path[0]) {
            if (result->examined != result->excluded)
                return reject(*result, "No mods directory for persistent component preferences.");
            result->source_missing = 1;
            return 1;
        }
        if (!modmgrSaveComponentStateFile(path, snapshot.choices.data(), snapshot.choices.size(),
                result->error, sizeof(result->error))) return 0;
        result->saved = 1;
        return 1;
    } catch (...) {
        return reject(*result, "Could not prepare catalog component preferences.");
    }
}

extern "C" int modmgrReplayCatalogComponentState(const char *path,
    const char *const *session_dirs, size_t session_count,
    modmgr_component_result_t *result)
{
    if (!result) return 0;
    *result = {};
    if (!path || !path[0]) {
        result->source_missing = 1;
        return 1;
    }
    try {
        modmgr_component_state_t *raw = nullptr;
        const int loaded = modmgrReadComponentState(path, &raw, result->error, sizeof(result->error));
        std::unique_ptr<modmgr_component_state_t, decltype(&modmgrFreeComponentState)>
            state(raw, modmgrFreeComponentState);
        if (loaded < 0) return 0;
        if (!loaded) {
            result->source_missing = 1;
            return 1;
        }
        Snapshot snapshot;
        if (!snapshotCatalog(snapshot, session_dirs, session_count, *result)) return 0;
        for (size_t i = 0; i < modmgrComponentStateCount(raw); ++i) {
            const char *id = modmgrComponentStateId(raw, i);
            ++result->examined;
            const auto found = std::find_if(snapshot.choices.begin(), snapshot.choices.end(),
                [id](const modmgr_component_choice_t &choice) { return std::strcmp(choice.id, id) == 0; });
            if (found == snapshot.choices.end()) {
                ++result->unresolved;
                continue;
            }
            if (!found->persistent) {
                ++result->excluded;
                continue;
            }
            if (assetCatalogGetGeneration() != snapshot.generation) {
                result->catalog_result = CATALOG_CHANGE_CONFLICT;
                std::snprintf(result->id, sizeof(result->id), "%s", id);
                return reject(*result, "Catalog changed during component replay; earlier changes may remain.");
            }
            const auto changed = assetCatalogSetEnabledChecked(id, 0);
            if (changed < 0) {
                result->catalog_result = changed;
                std::snprintf(result->id, sizeof(result->id), "%s", id);
                std::snprintf(result->error, sizeof(result->error),
                    "Could not restore component '%s' (catalog result %d); earlier changes may remain.",
                    id, static_cast<int>(changed));
                return 0;
            }
            if (changed == CATALOG_CHANGE_APPLIED) ++result->applied;
            else ++result->unchanged;
        }
        return 1;
    } catch (...) {
        return reject(*result, "Could not replay catalog component preferences; earlier changes may remain.");
    }
}
