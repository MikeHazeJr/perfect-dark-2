#include "actionmap_profile.h"
#include "save_atomic.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <utility>

namespace {
s32 collectDeviceSlots(const InputMapping *mapping, u32 controllerBegin,
                      u32 keyEnd, s32 controller, s32 *slots)
{
    if (!mapping) return 0;
    const s32 count = mapping->num_triggers < 0 ? 0
        : mapping->num_triggers > ACTIONMAP_MAX_TRIGGERS ? ACTIONMAP_MAX_TRIGGERS
        : mapping->num_triggers;
    s32 found = 0;
    for (s32 slot = 0; slot < count; ++slot) {
        const u32 vk = mapping->triggers[slot].vk;
        if (!vk || vk >= keyEnd || (vk >= controllerBegin) != (controller != 0)) continue;
        if (slots) slots[found] = slot;
        ++found;
    }
    return found;
}
}

s32 actionmapProfileDeviceBindingCount(const InputMapping *mapping,
    u32 controllerBegin, u32 keyEnd, s32 controller)
{
    return collectDeviceSlots(mapping, controllerBegin, keyEnd, controller, nullptr);
}

s32 actionmapProfileFindDeviceSlot(const InputMapping *mapping,
    u32 controllerBegin, u32 keyEnd, s32 controller, s32 filteredIndex)
{
    if (!mapping || filteredIndex < 0) return -1;
    s32 slots[ACTIONMAP_MAX_TRIGGERS];
    const s32 found = collectDeviceSlots(mapping, controllerBegin, keyEnd, controller, slots);
    if (filteredIndex < found) return slots[filteredIndex];
    if (found != 0 || filteredIndex != 0) return -1;
    for (s32 slot = 0; slot < ACTIONMAP_MAX_TRIGGERS; ++slot)
        if (slot >= mapping->num_triggers || !mapping->triggers[slot].vk) return slot;
    return -1;
}

namespace actionmap_profile {
namespace {
constexpr const char *kFormat = "format=PD2-bindings-v1";
constexpr size_t kMaxDocumentBytes = 4 * 1024 * 1024;

bool fail(std::string &error, const char *message)
{
    error = message;
    return false;
}

std::string trim(const std::string &s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

void normalize(InputMapping &mapping, InputAction action)
{
    mapping.action = action;
    mapping.num_triggers = 0;
    for (int slot = 0; slot < ACTIONMAP_MAX_TRIGGERS; ++slot) {
        if (mapping.triggers[slot].vk) mapping.num_triggers = slot + 1;
    }
}

bool valid(const Registry &registry, const Snapshot &snapshot, std::string &error)
{
    if (snapshot.contexts.size() != registry.count
        || snapshot.activeProfile < -1 || snapshot.activeProfile >= 6)
        return fail(error, "The binding snapshot has an invalid shape or profile slot.");
    for (size_t ci = 0; ci < registry.count; ++ci) {
        const auto &context = snapshot.contexts[ci];
        if (context.name != registry.contexts[ci]->name)
            return fail(error, "The binding snapshot uses an unknown context.");
        for (int a = 0; a < ACTION_COUNT; ++a) {
            const auto &mapping = context.mappings[a];
            if ((context.present[a] != 0 && context.present[a] != 1)
                || mapping.num_triggers < 0 || mapping.num_triggers > ACTIONMAP_MAX_TRIGGERS)
                return fail(error, "The binding snapshot has an invalid mapping.");
            for (int slot = 0; slot < ACTIONMAP_MAX_TRIGGERS; ++slot) {
                const u32 vk = mapping.triggers[slot].vk;
                if (!vk) continue;
                if (!context.present[a] || slot >= mapping.num_triggers)
                    return fail(error, "The binding snapshot contains stale trigger slots.");
                const std::string name = registry.vkName(vk);
                if (registry.vkByName(name.c_str()) != static_cast<s32>(vk))
                    return fail(error, "The binding snapshot contains an unsupported key.");
            }
        }
    }
    return true;
}

bool parseKeys(const Registry &registry, const std::string &value, bool exact,
               InputMapping &mapping, std::string &error)
{
    mapping = {};
    if (value.empty()) return !exact || fail(error, "A complete mapping needs four explicit slots.");
    size_t start = 0;
    int count = 0;
    while (true) {
        if (count == ACTIONMAP_MAX_TRIGGERS)
            return fail(error, "A binding has more than four trigger slots.");
        const auto end = value.find(',', start);
        const auto token = trim(value.substr(start, end == std::string::npos ? end : end - start));
        if (token.empty()) return fail(error, "A binding contains an empty key token.");
        if (!exact && token == "0")
            return fail(error, "A legacy empty binding must use an empty value.");
        if (token != "0") {
            // Bound names before calling the legacy resolver (UNKNOWN uses atoi).
            if (token.size() > 63) return fail(error, "A binding key name is too long.");
            if (token.compare(0, 7, "UNKNOWN") == 0) {
                const auto digits = token.substr(7);
                if (digits.empty() || digits.size() > 6
                    || digits.find_first_not_of("0123456789") != std::string::npos)
                    return fail(error, "A raw binding key is malformed.");
            }
            const s32 vk = registry.vkByName(token.c_str());
            if (vk <= 0 || token != registry.vkName(static_cast<u32>(vk)))
                return fail(error, "A binding contains an unknown key name.");
            mapping.triggers[count].vk = static_cast<u32>(vk);
        }
        ++count;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (exact && count != ACTIONMAP_MAX_TRIGGERS)
        return fail(error, "A complete mapping needs four explicit slots.");
    return true;
}
} // namespace

Snapshot capture(const Registry &registry, int activeProfile)
{
    Snapshot result;
    result.activeProfile = activeProfile;
    for (size_t ci = 0; ci < registry.count; ++ci) {
        Context context;
        const auto *source = registry.contexts[ci];
        context.name = source->name;
        for (int a = 0; a < ACTION_COUNT; ++a) {
            context.present[a] = source->has_mapping[a] ? 1 : 0;
            if (context.present[a]) {
                context.mappings[a] = source->mappings[a];
                // Ignore slots outside the live length; never serialize stale tails.
                const int count = source->mappings[a].num_triggers;
                for (int slot = 0; slot < ACTIONMAP_MAX_TRIGGERS; ++slot)
                    if (slot >= count) context.mappings[a].triggers[slot].vk = 0;
            }
            normalize(context.mappings[a], static_cast<InputAction>(a));
        }
        result.contexts.push_back(std::move(context));
    }
    return result;
}

void apply(const Registry &registry, const Snapshot &snapshot)
{
    for (size_t ci = 0; ci < registry.count; ++ci) {
        auto *destination = registry.contexts[ci];
        const auto &source = snapshot.contexts[ci];
        for (int a = 0; a < ACTION_COUNT; ++a) {
            destination->has_mapping[a] = source.present[a];
            destination->mappings[a] = source.mappings[a];
        }
    }
}

bool resetDevice(const Registry &registry, const Snapshot &defaults, const Snapshot &before,
                 InputMappingContext *const *contexts, size_t count, bool controller,
                 Snapshot &out, std::string &error)
{
    if (!valid(registry, defaults, error) || !valid(registry, before, error)) return false;
    Snapshot candidate = before;
    for (size_t selected = 0; selected < count; ++selected) {
        size_t ci = 0;
        for (; ci < registry.count; ++ci) if (registry.contexts[ci] == contexts[selected]) break;
        if (ci == registry.count) return fail(error, "The reset names an unknown input context.");
        auto &context = candidate.contexts[ci];
        for (int a = 0; a < ACTION_COUNT; ++a) {
            auto &mapping = context.mappings[a];
            for (auto &trigger : mapping.triggers)
                if (trigger.vk && (trigger.vk >= registry.controllerBegin) == controller)
                    trigger.vk = 0;
            if (defaults.contexts[ci].present[a]) {
                for (const auto &trigger : defaults.contexts[ci].mappings[a].triggers) {
                    if (!trigger.vk || (trigger.vk >= registry.controllerBegin) != controller) continue;
                    int slot = 0;
                    while (slot < ACTIONMAP_MAX_TRIGGERS && mapping.triggers[slot].vk) ++slot;
                    if (slot == ACTIONMAP_MAX_TRIGGERS)
                        return fail(error, "Reset needs more binding slots. Clear a slot on the other input device first.");
                    mapping.triggers[slot] = trigger;
                    context.present[a] = 1;
                }
            }
            normalize(mapping, static_cast<InputAction>(a));
            if (!mapping.num_triggers && !defaults.contexts[ci].present[a]) context.present[a] = 0;
        }
    }
    out = std::move(candidate);
    error.clear();
    return true;
}

bool encode(const Registry &registry, const Snapshot &snapshot,
            std::string &text, std::string &error)
{
    if (!valid(registry, snapshot, error)) return false;
    std::ostringstream out;
    out << "# Perfect Dark 2 complete bindings; 0 is an empty slot, - is unmapped.\n"
        << kFormat << "\nactive_profile=" << snapshot.activeProfile << '\n';
    for (const auto &context : snapshot.contexts) {
        for (int a = 0; a < ACTION_COUNT; ++a) {
            out << context.name << '.' << registry.actionNames[a] << '=';
            if (!context.present[a]) {
                out << '-';
            } else {
                for (int slot = 0; slot < ACTIONMAP_MAX_TRIGGERS; ++slot) {
                    if (slot) out << ',';
                    const u32 vk = context.mappings[a].triggers[slot].vk;
                    if (vk) out << registry.vkName(vk);
                    else out << '0';
                }
            }
            out << '\n';
        }
    }
    text = out.str();
    error.clear();
    return true;
}

bool decode(const Registry &registry, const Snapshot &defaults,
            const std::string &text, bool allowLegacy,
            Snapshot &out, std::string &error)
{
    if (text.size() > kMaxDocumentBytes || text.find('\0') != std::string::npos)
        return fail(error, "The binding file is too large or contains binary data.");
    std::istringstream lines(text);
    std::string line;
    std::vector<std::string> rows;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (!line.empty() && line.front() != '#') rows.push_back(line);
    }
    if (rows.empty()) return fail(error, "The binding file is empty.");
    const bool versioned = rows.front() == kFormat;
    if (!versioned && (!allowLegacy || rows.front().compare(0, 7, "format=") == 0))
        return fail(error, "The binding file format is unsupported.");
    if (!valid(registry, defaults, error)) return false;
    Snapshot candidate = defaults;
    candidate.activeProfile = -1;
    std::vector<std::array<bool, ACTION_COUNT>> seen(registry.count);
    bool sawActive = false;
    size_t mappingRows = 0;
    for (size_t ri = versioned ? 1 : 0; ri < rows.size(); ++ri) {
        const auto eq = rows[ri].find('=');
        if (eq == std::string::npos || rows[ri].find('=', eq + 1) != std::string::npos)
            return fail(error, "A binding file row is malformed.");
        const auto key = trim(rows[ri].substr(0, eq));
        const auto value = trim(rows[ri].substr(eq + 1));
        if (versioned && key == "active_profile") {
            if (sawActive || (value != "-1" && (value.size() != 1 || value[0] < '0' || value[0] > '5')))
                return fail(error, "The binding file profile slot is invalid or repeated.");
            sawActive = true;
            candidate.activeProfile = value == "-1" ? -1 : value[0] - '0';
            continue;
        }
        const auto dot = key.find('.');
        if (dot == std::string::npos) return fail(error, "A binding row is missing its context.");
        const auto contextName = key.substr(0, dot);
        auto actionName = key.substr(dot + 1);
        if (!versioned) {
            if (actionName.compare(0, 3, "P0.") != 0)
                return fail(error, "A legacy profile must contain player-zero binding rows.");
            actionName.erase(0, 3);
        }
        size_t ci = 0;
        for (; ci < registry.count; ++ci) if (contextName == registry.contexts[ci]->name) break;
        int a = 0;
        for (; a < ACTION_COUNT; ++a) if (actionName == registry.actionNames[a]) break;
        if (ci == registry.count || a == ACTION_COUNT)
            return fail(error, "The binding file names an unknown context or action.");
        if (seen[ci][a]) return fail(error, "The binding file repeats a context/action pair.");
        seen[ci][a] = true;
        ++mappingRows;
        auto &context = candidate.contexts[ci];
        InputMapping mapping{};
        if (versioned && value == "-") {
            context.present[a] = 0;
        } else {
            if (!parseKeys(registry, value, versioned, mapping, error)) return false;
            if (!versioned) {
                // Legacy profiles only replaced P0, retaining any other-player defaults.
                for (const auto &trigger : mapping.triggers)
                    if (trigger.vk && registry.playerForKey(trigger.vk) != 0)
                        return fail(error, "A legacy player-zero row contains another player's key.");
                int slot = 0;
                while (slot < ACTIONMAP_MAX_TRIGGERS && mapping.triggers[slot].vk) ++slot;
                for (const auto &trigger : context.mappings[a].triggers) {
                    if (!trigger.vk || registry.playerForKey(trigger.vk) == 0) continue;
                    if (slot == ACTIONMAP_MAX_TRIGGERS)
                        return fail(error, "A legacy binding exceeds the four-slot capacity.");
                    mapping.triggers[slot++] = trigger;
                }
            }
            context.present[a] = 1;
        }
        normalize(mapping, static_cast<InputAction>(a));
        context.mappings[a] = mapping;
    }
    if (!mappingRows || (versioned && (!sawActive || mappingRows != registry.count * ACTION_COUNT)))
        return fail(error, "The complete binding file is missing required rows.");
    if (!valid(registry, candidate, error)) return false;
    out = std::move(candidate);
    error.clear();
    return true;
}

ReadResult read(const Registry &registry, const Snapshot &defaults,
                const char *absolutePath, bool allowLegacy,
                Snapshot &out, std::string &error)
{
    if (!absolutePath || !*absolutePath) {
        fail(error, "The binding file path is empty.");
        return ReadResult::Invalid;
    }
    FILE *file = std::fopen(absolutePath, "rb");
    if (!file) {
        const bool missing = errno == ENOENT;
        fail(error, missing ? "The binding file does not exist." : "The binding file could not be opened.");
        return missing ? ReadResult::Missing : ReadResult::Invalid;
    }
    std::string text;
    char buffer[4096];
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0) {
        text.append(buffer, count);
        if (text.size() > kMaxDocumentBytes) break;
    }
    const bool readError = std::ferror(file) != 0;
    const bool closeError = std::fclose(file) != 0;
    if (readError || closeError) {
        fail(error, "The binding file could not be read completely.");
        return ReadResult::Invalid;
    }
    return decode(registry, defaults, text, allowLegacy, out, error)
        ? ReadResult::Ok : ReadResult::Invalid;
}

bool write(const Registry &registry, const Snapshot &snapshot,
           const char *absolutePath, std::string &error)
{
    std::string text;
    if (!encode(registry, snapshot, text, error)) return false;
    save_atomic_file_t transaction{};
    if (!absolutePath || !*absolutePath || saveAtomicBegin(&transaction, absolutePath) != 0)
        return fail(error, "Could not create the binding save. Existing bindings were kept.");
    if (std::fwrite(text.data(), 1, text.size(), saveAtomicStream(&transaction)) != text.size()) {
        saveAtomicAbort(&transaction);
        return fail(error, "Could not write the binding save. Existing bindings were kept.");
    }
    if (saveAtomicCommit(&transaction) != 0)
        return fail(error, "Could not commit the binding save. Existing bindings were kept.");
    error.clear();
    return true;
}

bool Store::initialize(const Registry &registry, const Snapshot &defaults,
                       const char *currentPath, std::string &error)
{
    if (initialized_) return !blocked_;
    // Caller has applied legacy pd.ini to live state once. Never infer a named
    // profile from Input.ActiveProfile: older UI used that as a selection only.
    accepted_ = capture(registry, -1);
    Snapshot candidate;
    const auto result = read(registry, defaults, currentPath, false, candidate, error);
    initialized_ = true;
    if (result == ReadResult::Invalid) {
        blocked_ = true;
        return false;
    }
    if (result == ReadResult::Missing) return write(registry, accepted_, currentPath, error);
    apply(registry, candidate);
    accepted_ = std::move(candidate);
    error.clear();
    return true;
}

bool Store::commit(const Registry &registry, const Snapshot &candidate,
                   const char *currentPath, std::string &error)
{
    if (!write(registry, candidate, currentPath, error)) {
        apply(registry, accepted_);
        return false;
    }
    apply(registry, candidate);
    accepted_ = candidate;
    blocked_ = false;
    return true;
}

bool Store::saveCurrent(const Registry &registry, const char *currentPath, std::string &error)
{
    if (!initialized_) return fail(error, "Bindings have not been initialized.");
    if (blocked_) {
        apply(registry, accepted_);
        return fail(error, "The current binding file is invalid. Load a valid profile to replace it.");
    }
    return commit(registry, capture(registry, accepted_.activeProfile), currentPath, error);
}

bool Store::loadProfile(const Registry &registry, const Snapshot &defaults,
                        const char *profilePath, const char *currentPath,
                        int profile, std::string &error)
{
    if (!initialized_) return fail(error, "Bindings have not been initialized.");
    if (profile < -1 || profile >= 6) return fail(error, "The selected profile slot is invalid.");
    Snapshot candidate;
    if (read(registry, defaults, profilePath, true, candidate, error) != ReadResult::Ok) return false;
    candidate.activeProfile = profile;
    return commit(registry, candidate, currentPath, error);
}
} // namespace actionmap_profile
