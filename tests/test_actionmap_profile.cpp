#include "catch.hpp"
#include "actionmap_profile.h"
#include "save_atomic.h"
#include <PR/os_thread.h>
#include "input.h"
#include "input_vk.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
namespace profile = actionmap_profile;
namespace fs = std::filesystem;

const char *keyName(u32 vk)
{
    static std::string name;
    name = "KEY" + std::to_string(vk);
    return name.c_str();
}
s32 keyByName(const char *text)
{
    const std::string name = text ? text : "";
    if (name.compare(0, 3, "KEY") != 0) return -1;
    char *end = nullptr;
    const long vk = std::strtol(name.c_str() + 3, &end, 10);
    return end && !*end && vk > 0 && vk < 200 ? static_cast<s32>(vk) : -1;
}
s32 keyPlayer(u32 vk) { return vk >= 150 ? 1 : 0; }

const char *controllerName(u32 vk)
{
    static char name[32];
    return inputVkControllerName(vk, name, sizeof(name)) ? name : "UNKNOWN";
}
s32 controllerByName(const char *name)
{
    const u32 vk = inputVkControllerByName(name);
    return vk ? (s32)vk : -1;
}
s32 controllerPlayer(u32 vk)
{
    input_vk_control_t control;
    return inputVkDecodeController(vk, &control) ? control.player : 0;
}

struct Fixture {
    // All twelve production context roles must fit even though only ten may
    // be active concurrently. These are real InputMappingContext objects.
    std::vector<InputMappingContext> contexts{12};
    std::array<std::string, 12> contextNames;
    std::array<InputMappingContext *, 12> pointers{};
    std::array<std::string, ACTION_COUNT> actionStrings;
    std::array<const char *, ACTION_COUNT> actionNames{};

    Fixture()
    {
        for (size_t ci = 0; ci < contexts.size(); ++ci) {
            contextNames[ci] = "context" + std::to_string(ci);
            contexts[ci].name = contextNames[ci].c_str();
            contexts[ci].priority = static_cast<s32>(ci * 3);
            contexts[ci].active = ci % 2;
            pointers[ci] = &contexts[ci];
        }
        for (int a = 0; a < ACTION_COUNT; ++a) {
            actionStrings[a] = "Action" + std::to_string(a);
            actionNames[a] = actionStrings[a].c_str();
        }
        bind(0, ACTION_USE, {1, 100, 2, 101});
        bind(1, ACTION_USE, {3, 102});
        bind(11, ACTION_CANCEL_USE, {4});
    }
    profile::Registry registry()
    {
        return {pointers.data(), pointers.size(), actionNames.data(), keyName, keyByName, keyPlayer, 100};
    }
    void bind(size_t context, InputAction action, std::initializer_list<u32> keys)
    {
        auto &mapping = contexts[context].mappings[action];
        mapping = {};
        mapping.action = action;
        contexts[context].has_mapping[action] = 1;
        for (const u32 key : keys) mapping.triggers[mapping.num_triggers++].vk = key;
    }
    std::string row(size_t ci, InputAction action, bool legacy = false)
    {
        return contextNames[ci] + (legacy ? ".P0." : ".") + actionStrings[action] + '=';
    }
};

struct TempDirectory {
    fs::path path;
    TempDirectory()
    {
        static std::atomic<unsigned> serial{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path()
            / ("pd2-actionmap-profile-" + std::to_string(stamp) + '-' + std::to_string(++serial));
        fs::create_directories(path);
    }
    ~TempDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string bytes(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::string encoded(Fixture &fixture, const profile::Snapshot &snapshot)
{
    std::string text, error;
    REQUIRE(profile::encode(fixture.registry(), snapshot, text, error));
    return text;
}
std::string live(Fixture &fixture, int active = -1)
{
    return encoded(fixture, profile::capture(fixture.registry(), active));
}
void noCandidates(const fs::path &path)
{
    for (const auto &entry : fs::directory_iterator(path))
        REQUIRE(entry.path().filename().string().find(".pd2tmp-") == std::string::npos);
}
} // namespace

TEST_CASE("Complete profiles preserve every context, four exact slots, holes and explicit empty mappings",
          "[input][profile][persistence]")
{
    Fixture fixture;
    const auto defaults = profile::capture(fixture.registry(), -1);
    fixture.bind(1, ACTION_CANCEL_USE, {});
    fixture.bind(11, ACTION_USE, {0, 3, 0, 104});
    fixture.bind(9, ACTION_USE, {150, 151, 152, 153}); // complete snapshots retain every physical player/device domain
    const auto before = profile::capture(fixture.registry(), 4);
    const auto text = encoded(fixture, before);
    REQUIRE(text.find(fixture.row(1, ACTION_CANCEL_USE) + "0,0,0,0") != std::string::npos);
    REQUIRE(text.find(fixture.row(11, ACTION_USE) + "0,KEY3,0,KEY104") != std::string::npos);
    REQUIRE(text.find(fixture.row(10, ACTION_USE) + "-") != std::string::npos);
    REQUIRE(before.contexts.size() > ACTIONMAP_MAX_CONTEXTS);
    profile::Snapshot decoded;
    std::string error;
    REQUIRE(profile::decode(fixture.registry(), defaults, text, false, decoded, error));
    REQUIRE(encoded(fixture, decoded) == text);
    profile::apply(fixture.registry(), defaults);
    profile::apply(fixture.registry(), decoded);
    REQUIRE(live(fixture, 4) == text);
    REQUIRE(fixture.contexts[1].mappings[ACTION_USE].triggers[0].vk == 3);
    REQUIRE(fixture.contexts[0].mappings[ACTION_USE].triggers[0].vk == 1);
    REQUIRE(fixture.contexts[1].has_mapping[ACTION_CANCEL_USE] == 1);
    REQUIRE(fixture.contexts[1].mappings[ACTION_CANCEL_USE].num_triggers == 0);
    REQUIRE(fixture.contexts[11].active == 1);
    REQUIRE(fixture.contexts[11].priority == 33);
    REQUIRE(fixture.contexts[11].name == fixture.contextNames[11].c_str());
    REQUIRE(fixture.contexts[9].mappings[ACTION_USE].triggers[3].vk == 153);
}

TEST_CASE("complete profile codec keeps legacy alias and precise controller keys distinct",
    "[input][profile][persistence][raw-vk]")
{
    Fixture fixture;
    for (auto &context : fixture.contexts) {
        for (int action = 0; action < ACTION_COUNT; action++) {
            context.has_mapping[action] = 0;
            context.mappings[action] = {};
        }
    }
    const u32 alias = VK_JOY1_LSTICK_LEFT;
    const u32 button = inputVkRawButton(0, 22);
    const u32 axis = inputVkDigitalAxis(0, 0, -1);
    const u32 player4 = inputVkRawButton(3, 31);
    fixture.bind(0, ACTION_MENU_ACCEPT, {alias, button, axis, player4});
    auto registry = fixture.registry();
    registry.vkName = controllerName;
    registry.vkByName = controllerByName;
    registry.playerForKey = controllerPlayer;
    registry.controllerBegin = VK_JOY_BEGIN;
    const auto before = profile::capture(registry, 2);
    std::string text, error;
    REQUIRE(profile::encode(registry, before, text, error));
    REQUIRE(text.find("JOY1_LSTICK_LEFT,JOY1_BUTTON_22,JOY1_AXIS0_NEG,JOY4_BUTTON_31") != std::string::npos);
    profile::Snapshot after;
    REQUIRE(profile::decode(registry, before, text, false, after, error));
    const auto &mapping = after.contexts[0].mappings[ACTION_MENU_ACCEPT];
    REQUIRE(mapping.num_triggers == 4);
    REQUIRE(mapping.triggers[0].vk == alias);
    REQUIRE(mapping.triggers[1].vk == button);
    REQUIRE(mapping.triggers[2].vk == axis);
    REQUIRE(mapping.triggers[3].vk == player4);
    std::string repeated;
    REQUIRE(profile::encode(registry, after, repeated, error));
    REQUIRE(repeated == text);
}

TEST_CASE("Invalid profile documents never partially replace the output or live state",
          "[input][profile][persistence]")
{
    Fixture fixture;
    const auto defaults = profile::capture(fixture.registry(), -1);
    const auto good = encoded(fixture, defaults);
    std::vector<std::string> bad = {
        "", "# comments only\n", "format=PD2-bindings-v99\n",
        good.substr(0, good.rfind('\n', good.size() - 2)),
        good + fixture.row(0, ACTION_USE) + "KEY1,0,0,0\n",
        fixture.row(0, ACTION_USE, true) + "KEY1,KEY2,KEY3,KEY4,KEY5\n",
        fixture.row(0, ACTION_USE, true) + "BOGUS\n",
        fixture.row(0, ACTION_USE, true) + "KEY1,,KEY2\n",
        fixture.row(0, ACTION_USE, true) + "KEY150\n",
        "unknown.P0.Action0=KEY1\n", "context0.P0.NoSuchAction=KEY1\n",
        fixture.row(0, ACTION_USE, true) + "UNKNOWN99999999999999999999999999\n",
        good + "active_profile=2\n",
        good + std::string(1, '\0')
    };
    const auto keyPos = good.find("KEY1,");
    REQUIRE(keyPos != std::string::npos);
    auto shortRow = good;
    const auto lineEnd = shortRow.find('\n', keyPos);
    shortRow.replace(keyPos, lineEnd - keyPos, "KEY1");
    bad.push_back(shortRow);
    for (const auto &document : bad) {
        INFO(document.substr(0, 160));
        auto output = defaults;
        std::string error;
        REQUIRE_FALSE(profile::decode(fixture.registry(), defaults, document, true, output, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(encoded(fixture, output) == good);
        REQUIRE(live(fixture) == good);
    }
}

TEST_CASE("Legacy named profiles overlay built-in defaults and replace triggers with zeroed tails",
          "[input][profile][legacy]")
{
    Fixture fixture;
    const auto defaults = profile::capture(fixture.registry(), -1);
    const std::string legacy = "# Perfect Dark 2 input profile\n"
        + fixture.row(0, ACTION_USE, true) + "KEY9\n"
        + fixture.row(1, ACTION_USE, true) + "\n";
    profile::Snapshot output;
    std::string error;
    REQUIRE(profile::decode(fixture.registry(), defaults, legacy, true, output, error));
    const auto &mapping = output.contexts[0].mappings[ACTION_USE];
    REQUIRE(mapping.num_triggers == 1);
    REQUIRE(mapping.triggers[0].vk == 9);
    for (int slot = 1; slot < ACTIONMAP_MAX_TRIGGERS; ++slot) REQUIRE(mapping.triggers[slot].vk == 0);
    REQUIRE(output.contexts[1].present[ACTION_USE] == 1);
    REQUIRE(output.contexts[1].mappings[ACTION_USE].num_triggers == 0);
    REQUIRE(output.contexts[11].mappings[ACTION_CANCEL_USE].triggers[0].vk == 4);
    REQUIRE_FALSE(profile::decode(fixture.registry(), defaults, legacy, false, output, error));
    REQUIRE(live(fixture) == encoded(fixture, defaults));
}

TEST_CASE("Atomic named-profile save failures keep existing bytes and remove the candidate",
          "[input][profile][atomic]")
{
    TempDirectory directory;
    Fixture fixture;
    const auto snapshot = profile::capture(fixture.registry(), 2);
    const auto path = directory.path / "profile2.ini";
    std::ofstream(path, std::ios::binary) << "previous bytes";
    std::string error;
    saveAtomicDebugFailNextCommit();
    REQUIRE_FALSE(profile::write(fixture.registry(), snapshot, path.string().c_str(), error));
    REQUIRE(bytes(path) == "previous bytes");
    noCandidates(directory.path);
    REQUIRE(profile::write(fixture.registry(), snapshot, path.string().c_str(), error));
    REQUIRE(bytes(path) == encoded(fixture, snapshot));
    REQUIRE_FALSE(profile::write(fixture.registry(), snapshot,
        (directory.path / "missing-parent" / "profile.ini").string().c_str(), error));
    noCandidates(directory.path);
}

TEST_CASE("Current profile startup imports legacy once and restores the saved current edits and identity",
          "[input][profile][startup]")
{
    TempDirectory directory;
    Fixture fixture;
    const auto defaults = profile::capture(fixture.registry(), -1);
    const auto current = (directory.path / "current.ini").string();
    const auto named = (directory.path / "profile3.ini").string();
    fixture.bind(0, ACTION_USE, {}); // legacy pd.ini explicit clear already applied
    profile::Store store;
    std::string error;
    REQUIRE(store.initialize(fixture.registry(), defaults, current.c_str(), error));
    REQUIRE(store.activeProfile() == -1); // old INI selection is not a loaded profile
    REQUIRE(fixture.contexts[0].mappings[ACTION_USE].num_triggers == 0);
    REQUIRE(profile::write(fixture.registry(), defaults, named.c_str(), error));
    REQUIRE(store.loadProfile(fixture.registry(), defaults, named.c_str(), current.c_str(), 3, error));
    REQUIRE(store.activeProfile() == 3);
    fixture.bind(0, ACTION_USE, {7, 0, 103, 0});
    fixture.bind(1, ACTION_USE, {});
    REQUIRE(store.saveCurrent(fixture.registry(), current.c_str(), error));
    const auto saved = bytes(current);
    profile::apply(fixture.registry(), defaults); // simulate fresh startup defaults/legacy
    profile::Store restarted;
    REQUIRE(restarted.initialize(fixture.registry(), defaults, current.c_str(), error));
    REQUIRE(restarted.activeProfile() == 3);
    REQUIRE(live(fixture, 3) == saved);
    REQUIRE(fixture.contexts[1].mappings[ACTION_USE].num_triggers == 0);
    fixture.bind(1, ACTION_USE, {8});
    REQUIRE(restarted.initialize(fixture.registry(), defaults, current.c_str(), error));
    REQUIRE(fixture.contexts[1].mappings[ACTION_USE].triggers[0].vk == 8); // reopen is a no-op
}

TEST_CASE("Failed current edits and profile switches retain committed bindings, bytes and active slot",
          "[input][profile][atomic][rollback]")
{
    TempDirectory directory;
    Fixture fixture;
    const auto defaults = profile::capture(fixture.registry(), -1);
    const auto current = (directory.path / "current.ini").string();
    const auto named = (directory.path / "profile4.ini").string();
    profile::Store store;
    std::string error;
    REQUIRE(store.initialize(fixture.registry(), defaults, current.c_str(), error));
    REQUIRE(profile::write(fixture.registry(), defaults, named.c_str(), error));
    REQUIRE(store.loadProfile(fixture.registry(), defaults, named.c_str(), current.c_str(), 1, error));
    const auto accepted = bytes(current);
    fixture.bind(0, ACTION_USE, {9});
    saveAtomicDebugFailNextCommit();
    REQUIRE_FALSE(store.saveCurrent(fixture.registry(), current.c_str(), error));
    REQUIRE(live(fixture, store.activeProfile()) == accepted);
    REQUIRE(bytes(current) == accepted);
    REQUIRE(store.activeProfile() == 1);
    auto candidate = defaults;
    candidate.contexts[1].mappings[ACTION_USE].triggers[0].vk = 8;
    REQUIRE(profile::write(fixture.registry(), candidate, named.c_str(), error));
    const auto profileBytes = bytes(named);
    saveAtomicDebugFailNextCommit();
    REQUIRE_FALSE(store.loadProfile(fixture.registry(), defaults, named.c_str(), current.c_str(), 4, error));
    REQUIRE(bytes(current) == accepted);
    REQUIRE(bytes(named) == profileBytes);
    REQUIRE(live(fixture, store.activeProfile()) == accepted);
    REQUIRE(store.activeProfile() == 1);
    REQUIRE_FALSE(store.loadProfile(fixture.registry(), defaults, "", current.c_str(), 4, error));
    REQUIRE_FALSE(store.loadProfile(fixture.registry(), defaults,
        (directory.path / "absent.ini").string().c_str(), current.c_str(), 4, error));
    REQUIRE(live(fixture, store.activeProfile()) == accepted);
    noCandidates(directory.path);
}

TEST_CASE("Failed first migration keeps live bindings and can retry without inventing an active profile",
          "[input][profile][startup][atomic]")
{
    TempDirectory directory;
    Fixture fixture;
    const auto defaults = profile::capture(fixture.registry(), -1);
    fixture.bind(0, ACTION_USE, {9});
    const auto imported = live(fixture);
    const auto current = (directory.path / "current.ini").string();
    profile::Store store;
    std::string error;
    saveAtomicDebugFailNextCommit();
    REQUIRE_FALSE(store.initialize(fixture.registry(), defaults, current.c_str(), error));
    REQUIRE_FALSE(fs::exists(current));
    REQUIRE(live(fixture) == imported);
    REQUIRE(store.activeProfile() == -1);
    noCandidates(directory.path);
    REQUIRE(store.saveCurrent(fixture.registry(), current.c_str(), error));
    REQUIRE(bytes(current) == imported);
}

TEST_CASE("Corrupt current files block implicit overwrites until an explicit valid profile commits",
          "[input][profile][startup][rollback]")
{
    TempDirectory directory;
    Fixture fixture;
    const auto defaults = profile::capture(fixture.registry(), -1);
    const auto current = (directory.path / "current.ini").string();
    const auto named = (directory.path / "profile.ini").string();
    std::ofstream(current, std::ios::binary) << "format=PD2-bindings-v1\nactive_profile=3\n";
    const auto corrupt = bytes(current);
    profile::Store store;
    std::string error;
    REQUIRE_FALSE(store.initialize(fixture.registry(), defaults, current.c_str(), error));
    REQUIRE(bytes(current) == corrupt);
    fixture.bind(0, ACTION_USE, {9});
    REQUIRE_FALSE(store.saveCurrent(fixture.registry(), current.c_str(), error));
    REQUIRE(bytes(current) == corrupt);
    REQUIRE(live(fixture) == encoded(fixture, defaults));
    REQUIRE_FALSE(store.loadProfile(fixture.registry(), defaults, current.c_str(), current.c_str(), 3, error));
    REQUIRE(store.activeProfile() == -1);
    REQUIRE(profile::write(fixture.registry(), defaults, named.c_str(), error));
    REQUIRE(store.loadProfile(fixture.registry(), defaults, named.c_str(), current.c_str(), 3, error));
    REQUIRE(store.activeProfile() == 3);
    REQUIRE(bytes(current) == live(fixture, 3));
    REQUIRE(store.saveCurrent(fixture.registry(), current.c_str(), error));
}

TEST_CASE("Device reset keeps the other device and rejects capacity overflow as one candidate",
          "[input][profile][defaults]")
{
    Fixture fixture;
    const auto defaults = profile::capture(fixture.registry(), -1);
    fixture.bind(0, ACTION_USE, {9, 105, 8, 106});
    const auto before = profile::capture(fixture.registry(), -1);
    auto reset = before;
    std::string error;
    REQUIRE(profile::resetDevice(fixture.registry(), defaults, before,
        fixture.pointers.data(), 1, true, reset, error));
    const auto &mapping = reset.contexts[0].mappings[ACTION_USE];
    REQUIRE(mapping.triggers[0].vk == 9);
    REQUIRE(mapping.triggers[1].vk == 100);
    REQUIRE(mapping.triggers[2].vk == 8);
    REQUIRE(mapping.triggers[3].vk == 101);
    REQUIRE(live(fixture) == encoded(fixture, before));
    fixture.bind(0, ACTION_USE, {7, 8, 9, 10});
    const auto full = profile::capture(fixture.registry(), -1);
    reset = full;
    REQUIRE_FALSE(profile::resetDevice(fixture.registry(), defaults, full,
        fixture.pointers.data(), 1, true, reset, error));
    REQUIRE(encoded(fixture, reset) == encoded(fixture, full));
    REQUIRE(live(fixture) == encoded(fixture, full));
}

TEST_CASE("Legacy dropdown slot selection respects holes, live lengths and other-device capacity",
          "[input][profile][legacy][slots]")
{
    InputMapping mapping{};
    mapping.num_triggers = 4;
    mapping.triggers[0].vk = 1;
    mapping.triggers[2].vk = 102;
    mapping.triggers[3].vk = 103;
    REQUIRE(actionmapProfileDeviceBindingCount(&mapping, 100, 200, 1) == 2);
    REQUIRE(actionmapProfileFindDeviceSlot(&mapping, 100, 200, 1, 0) == 2);
    REQUIRE(actionmapProfileFindDeviceSlot(&mapping, 100, 200, 1, 1) == 3);
    REQUIRE(actionmapProfileFindDeviceSlot(&mapping, 100, 200, 1, 2) == -1);
    REQUIRE(actionmapProfileFindDeviceSlot(&mapping, 100, 200, 1, -1) == -1);
    for (int slot = 0; slot < ACTIONMAP_MAX_TRIGGERS; ++slot) mapping.triggers[slot].vk = slot + 1;
    REQUIRE(actionmapProfileDeviceBindingCount(&mapping, 100, 200, 1) == 0);
    REQUIRE(actionmapProfileFindDeviceSlot(&mapping, 100, 200, 1, 0) == -1);
    mapping.triggers[2].vk = 0;
    REQUIRE(actionmapProfileFindDeviceSlot(&mapping, 100, 200, 1, 0) == 2);
    mapping.num_triggers = 1;
    mapping.triggers[1].vk = 105; // stale tail: outside num_triggers is not a live binding
    REQUIRE(actionmapProfileDeviceBindingCount(&mapping, 100, 200, 1) == 0);
    REQUIRE(actionmapProfileFindDeviceSlot(&mapping, 100, 200, 1, 0) == 1);
    mapping.num_triggers = 100;
    REQUIRE(actionmapProfileDeviceBindingCount(&mapping, 100, 200, 0) == 2);
    mapping.num_triggers = -1;
    REQUIRE(actionmapProfileDeviceBindingCount(&mapping, 100, 200, 0) == 0);
    REQUIRE(actionmapProfileFindDeviceSlot(&mapping, 100, 200, 1, 0) == 0);
    REQUIRE(actionmapProfileFindDeviceSlot(nullptr, 100, 200, 1, 0) == -1);
}

TEST_CASE("Reachable legacy binding rows and checked persistence stay connected to production helpers",
          "[input][profile][legacy][static]")
{
    const fs::path path = fs::path(PD_SOURCE_DIR) / "port/src/optionsmenu.c";
    const auto text = bytes(path);
    REQUIRE_FALSE(text.empty());
    auto between = [&](const char *start, const char *end) {
        const auto first = text.find(start);
        REQUIRE(first != std::string::npos);
        const auto last = text.find(end, first + 1);
        REQUIRE(last != std::string::npos);
        return text.substr(first, last - first);
    };
    auto occurrences = [](const std::string &text, const std::string &needle) {
        size_t count = 0, at = 0;
        while ((at = text.find(needle, at)) != std::string::npos) { ++count; at += needle.size(); }
        return count;
    };
    const auto rows = between("struct menuitem g_ExtendedBindsMenuItems[]", "static MenuItemHandlerResult menuhandlerDoBind(s32 operation");
    const auto definitions = between("static const struct menubind menuBinds[]", "static const char *menutextBind");
    REQUIRE(occurrences(rows, "DEFINE_MENU_BIND(),") == occurrences(definitions, "{ ACTION_"));
    REQUIRE(text.find("actionmapProfileFindDeviceSlot(&g_ImcGameplay.mappings[action]") != std::string::npos);
    REQUIRE(text.find("actionmapProfileDeviceBindingCount(mapping") != std::string::npos);
    REQUIRE(text.find("if (g_BindIndex < 0)") != std::string::npos);
    const auto captureStart = text.find("static MenuItemHandlerResult menuhandlerDoBind(s32 operation", text.find("struct menuitem g_ExtendedBindsMenuItems[]"));
    REQUIRE(captureStart != std::string::npos);
    const auto captureEnd = text.find("static const char *menutextBind", captureStart);
    REQUIRE(captureEnd != std::string::npos);
    const auto capture = text.substr(captureStart, captureEnd - captureStart);
    REQUIRE(capture.find("if (persistExtendedBinds()) menuPopDialog();") != std::string::npos);
    const auto newKey = capture.find("if (key && key != VK_ESCAPE)");
    REQUIRE(newKey != std::string::npos);
    REQUIRE(capture.find("inputClearLastKey();", newKey) < capture.find("actionmapBind(", newKey));
    REQUIRE(occurrences(text, "(uintptr_t)g_ExtendedBindStatus") == 2);
}
