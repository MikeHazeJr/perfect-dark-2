#ifndef PD_ACTIONMAP_PROFILE_H
#define PD_ACTIONMAP_PROFILE_H

#include "actionmap.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Legacy dropdown adapters use the same bounded physical-slot semantics as
 * snapshots. A missing first device binding may use a free slot, never evict
 * another device; out-of-range dropdown selections return -1. */
s32 actionmapProfileDeviceBindingCount(const InputMapping *mapping,
    u32 controllerBegin, u32 keyEnd, s32 controller);
s32 actionmapProfileFindDeviceSlot(const InputMapping *mapping,
    u32 controllerBegin, u32 keyEnd, s32 controller, s32 filteredIndex);
#ifdef __cplusplus
}

#include <array>
#include <string>
#include <vector>

/* Complete mapping snapshots. Registry size is the known-context count, not
 * ACTIONMAP_MAX_CONTEXTS (which only limits concurrently active contexts).
 * Names/priorities/activation remain owned by actionmap, never by a file. */
namespace actionmap_profile {
struct Registry {
    InputMappingContext *const *contexts;
    size_t count;
    const char *const *actionNames;
    const char *(*vkName)(u32);
    s32 (*vkByName)(const char *);
    s32 (*playerForKey)(u32);
    u32 controllerBegin;
};
struct Context {
    std::string name;
    std::array<InputMapping, ACTION_COUNT> mappings{};
    std::array<s32, ACTION_COUNT> present{};
};
struct Snapshot {
    std::vector<Context> contexts;
    int activeProfile = -1; // -1 = defaults/imported/custom; 0..5 = loaded base
};
enum class ReadResult { Ok, Missing, Invalid };

Snapshot capture(const Registry &registry, int activeProfile);
void apply(const Registry &registry, const Snapshot &snapshot);
bool resetDevice(const Registry &, const Snapshot &defaults, const Snapshot &before,
                 InputMappingContext *const *contexts, size_t count, bool controller,
                 Snapshot &out, std::string &error);
bool encode(const Registry &registry, const Snapshot &snapshot,
            std::string &text, std::string &error);
/* A versioned document lists every context/action pair, with one exception:
 * a complete document from a build with a shorter action list, a shorter
 * context list, or both. Saved names are positional, and new actions and new
 * contexts are both appended (to the action list, and to the registry's
 * context list), so such a document lists the same leading run of the action
 * list for each of the leading contexts and nothing else. decode accepts it,
 * keeps its bindings and leaves the actions and contexts it could not know at
 * their defaults; *olderActions (optional) then receives how many actions per
 * context it lists and *olderContexts (optional) how many contexts, and both
 * receive 0 for a current document. Any other missing row is damage and is
 * refused. Without this, every added action or context turned each player's
 * saved bindings into a file that could not load. */
bool decode(const Registry &registry, const Snapshot &defaults,
            const std::string &text, bool allowLegacy,
            Snapshot &out, std::string &error, size_t *olderActions = nullptr,
            size_t *olderContexts = nullptr);
ReadResult read(const Registry &registry, const Snapshot &defaults,
                const char *absolutePath, bool allowLegacy,
                Snapshot &out, std::string &error, size_t *olderActions = nullptr,
                size_t *olderContexts = nullptr);
bool write(const Registry &registry, const Snapshot &snapshot,
           const char *absolutePath, std::string &error);

/* B-1184: true for a current-bindings file cut off at its first unnamed
 * action (a bare "<context>." last row). Startup rewrites such a file from
 * the live bindings instead of blocking saves. */
bool isUnnamedActionTruncation(const Registry &registry, const char *absolutePath);

/* Used by the real actionmap entrypoints and directly by tests. Candidates are
 * parsed and committed before live mutation. Failed UI edits are rolled back
 * to accepted mappings. A corrupt current file is never silently overwritten. */
class Store {
public:
    bool initialized() const { return initialized_; }
    int activeProfile() const { return accepted_.activeProfile; }
    /* How many actions per context, and how many contexts, the current file
     * listed when initialize read it as a complete file from an older build
     * (and brought it up to date); both 0 when it was current, missing or
     * refused. */
    size_t upgradedFromActions() const { return upgradedFromActions_; }
    size_t upgradedFromContexts() const { return upgradedFromContexts_; }
    bool initialize(const Registry &, const Snapshot &defaults,
                    const char *currentPath, std::string &error);
    bool saveCurrent(const Registry &, const char *currentPath, std::string &error);
    bool loadProfile(const Registry &, const Snapshot &defaults,
                     const char *profilePath, const char *currentPath,
                     int profile, std::string &error);
private:
    bool commit(const Registry &, const Snapshot &, const char *, std::string &);
    Snapshot accepted_;
    bool initialized_ = false;
    bool blocked_ = false;
    size_t upgradedFromActions_ = 0;
    size_t upgradedFromContexts_ = 0;
};
} // namespace actionmap_profile
#endif // __cplusplus
#endif
