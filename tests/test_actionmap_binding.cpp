#include "catch.hpp"
#include "actionmap_binding.h"
#include "actionmap_digital_owner.h"
#include <PR/os_thread.h>
#include "input.h"
#include "input_vk.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {
void bind(InputMappingContext &context, InputAction action, s32 slot, u32 vk)
{
    context.active = 1;
    context.has_mapping[action] = 1;
    InputMapping &mapping = context.mappings[action];
    mapping.action = action;
    mapping.triggers[slot].vk = vk;
    if (mapping.num_triggers <= slot) mapping.num_triggers = slot + 1;
}

std::string source(const char *path)
{
    std::ifstream input(std::string(PD_SOURCE_DIR) + "/" + path);
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}
}

TEST_CASE("active binding uses every action from the supplied live registry",
    "[input][actionmap][binding][glyphs]")
{
    InputMappingContext custom = {};
    custom.name = "dynamic context outside any built-in glyph roster";
    ActionBindingContext contexts[] = {{&custom, 1}};

    for (s32 action = 0; action < ACTION_COUNT; action++) {
        custom = {};
        const InputAction requested = (InputAction)action;
        bind(custom, requested, 0, VK_RETURN);
        bind(custom, requested, 1, VK_JOY1_BEGIN);
        CAPTURE(action);
        REQUIRE(actionBindingFindVk(contexts, 1, 0, requested, ACTIONMAP_DEVICE_KBM, 1)
            == (u32)VK_RETURN);
        REQUIRE(actionBindingFindVk(contexts, 1, 0, requested, ACTIONMAP_DEVICE_GAMEPAD, 1)
            == (u32)VK_JOY1_BEGIN);
        REQUIRE(actionBindingResolveVk(contexts, 1, VK_RETURN).action == requested);
    }
}

TEST_CASE("physical sources retain the selected winner and aggregate one action",
    "[input][actionmap][ownership]")
{
    InputMappingContext menu = {}, replacement = {};
    bind(menu, ACTION_MENU_ACCEPT, 0, VK_RETURN);
    bind(menu, ACTION_MENU_ACCEPT, 1, VK_SPACE);
    bind(replacement, ACTION_MENU_CANCEL, 0, VK_RETURN);
    ActionBindingContext contexts[] = {{&menu, 1}};
    ActionDigitalOwners owners;
    const auto enter = actionSourceKey(ActionSourceKind::Keyboard, 0, VK_RETURN);
    const auto space = actionSourceKey(ActionSourceKind::Keyboard, 0, VK_SPACE);

    const auto first = actionBindingResolveVk(contexts, 1, VK_RETURN);
    REQUIRE(owners.press(enter, 0, first.action, first.context).first);
    REQUIRE_FALSE(owners.press(enter, 0, first.action, first.context).accepted);
    const auto second = actionBindingResolveVk(contexts, 1, VK_SPACE);
    REQUIRE(owners.press(space, 0, second.action, second.context).accepted);
    REQUIRE(owners.count(0, ACTION_MENU_ACCEPT) == 2);

    contexts[0].context = &replacement; /* Binding changes before key-up. */
    const auto releaseEnter = owners.release(enter);
    REQUIRE(releaseEnter.valid);
    REQUIRE_FALSE(releaseEnter.last);
    REQUIRE(releaseEnter.owner.action == ACTION_MENU_ACCEPT);
    REQUIRE(owners.count(0, ACTION_MENU_ACCEPT) == 1);
    REQUIRE(owners.release(space).last);
    REQUIRE_FALSE(owners.release(space).valid);
}

TEST_CASE("precise joystick keys retain all old player ranges and separate physical sources",
    "[input][actionmap][binding][raw-vk]")
{
    REQUIRE(VK_JOY_BEGIN == 519);
    REQUIRE(VK_JOY_LEGACY_END == 647);
    REQUIRE(VK_JOY_RAW_BUTTON_BEGIN == 647);
    REQUIRE(VK_JOY_AXIS_BEGIN == 687);
    REQUIRE(VK_TOTAL_COUNT == 727);
    for (int player = 0; player < 4; player++) {
        for (int slot = 0; slot < 32; slot++) {
            const u32 old = VK_JOY_BEGIN + player * 32 + slot;
            input_vk_control_t control = {};
            REQUIRE(inputVkDecodeController(old, &control));
            REQUIRE(control.player == player);
            char name[32] = {};
            REQUIRE(inputVkControllerName(old, name, sizeof(name)));
            REQUIRE(inputVkControllerByName(name) == old);
        }
        for (int ordinal = 0; ordinal < 10; ordinal++) {
            const u32 button = inputVkRawButton(player, 22 + ordinal);
            const u32 axis = inputVkAxisByOrdinal(player, ordinal);
            REQUIRE(button != axis);
            REQUIRE(inputVkLegacyAlias(button) == VK_JOY_BEGIN + player * 32 + 22 + ordinal);
            REQUIRE(inputVkLegacyAlias(axis) == inputVkLegacyAlias(button));
            char name[32] = {};
            REQUIRE(inputVkControllerName(button, name, sizeof(name)));
            REQUIRE(inputVkControllerByName(name) == button);
            REQUIRE(inputVkControllerName(axis, name, sizeof(name)));
            REQUIRE(inputVkControllerByName(name) == axis);
        }
    }
    REQUIRE(inputVkRawButton(0, 21) == 540);
    REQUIRE(inputVkRawButton(0, 22) == 647);
    REQUIRE(inputVkRawButton(0, 31) == 656);
    REQUIRE(inputVkRawButton(0, 32) == 0);
    REQUIRE(inputVkDigitalAxis(0, 0, -1) == 687);
    REQUIRE(inputVkDigitalAxis(0, 4, 1) == 695);
    REQUIRE(inputVkDigitalAxis(0, 5, 1) == 696);
    REQUIRE(inputVkDigitalAxis(0, 5, -1) == 0);
    REQUIRE(inputVkControllerByName("JOY2_A") == 551);
    REQUIRE(inputVkControllerByName("JOY1_BUTTON_22") == 647);
    REQUIRE(inputVkControllerByName("JOY1_AXIS0_NEG") == 687);
    REQUIRE(inputVkControllerByName("JOY5_AXIS0_NEG") == 0);
    REQUIRE(inputVkControllerByName("JOY1_AXIS0_NEG_suffix") == 0);
    REQUIRE(actionBindingVkMatchesDevice(726, 3, ACTIONMAP_DEVICE_GAMEPAD));
    REQUIRE_FALSE(actionBindingVkMatchesDevice(726, 0, ACTIONMAP_DEVICE_GAMEPAD));
}

TEST_CASE("precise bindings override old shared aliases only within their context",
    "[input][actionmap][binding][raw-vk]")
{
    InputMappingContext menu = {}, higher = {};
    const u32 button = inputVkRawButton(0, 22);
    const u32 axis = inputVkDigitalAxis(0, 0, -1);
    const u32 alias = VK_JOY1_LSTICK_LEFT;
    bind(menu, ACTION_MENU_ACCEPT, 0, alias);
    bind(menu, ACTION_MENU_SECONDARY, 0, button);
    bind(menu, ACTION_MENU_TERTIARY, 0, axis);
    ActionBindingContext contexts[] = {{&menu, 1}, {&higher, 1}};
    REQUIRE(actionBindingResolveVkPair(contexts, 2, button, alias).action == ACTION_MENU_SECONDARY);
    REQUIRE(actionBindingResolveVkPair(contexts, 2, axis, alias).action == ACTION_MENU_TERTIARY);
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_GAMEPAD, 1) == 0);
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_SECONDARY, ACTIONMAP_DEVICE_GAMEPAD, 1) == button);
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_TERTIARY, ACTIONMAP_DEVICE_GAMEPAD, 1) == axis);
    menu.has_mapping[ACTION_MENU_TERTIARY] = 0;
    REQUIRE(actionBindingResolveVkPair(contexts, 2, axis, alias).vk == alias);
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_GAMEPAD, 1) == alias);
    bind(higher, ACTION_MENU_CANCEL, 0, alias);
    contexts[0].context = &higher;
    contexts[1].context = &menu;
    REQUIRE(actionBindingResolveVkPair(contexts, 2, button, alias).action == ACTION_MENU_CANCEL);
    REQUIRE(actionBindingResolveVkPair(contexts, 2, axis, alias).action == ACTION_MENU_CANCEL);
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_SECONDARY, ACTIONMAP_DEVICE_GAMEPAD, 1) == 0);
}

TEST_CASE("legacy glyph lookup carries the physical source that still wins",
    "[input][actionmap][binding][glyphs][raw-vk]")
{
    InputMappingContext menu = {};
    const u32 button = inputVkRawButton(0, 22);
    const u32 axis = inputVkDigitalAxis(0, 0, -1);
    const u32 alias = VK_JOY1_LSTICK_LEFT;
    bind(menu, ACTION_MENU_ACCEPT, 0, alias);
    bind(menu, ACTION_MENU_SECONDARY, 0, button);
    ActionBindingContext contexts[] = {{&menu, 1}};
    struct Probe { u32 button, axis, selected; bool button_present, axis_present; };
    Probe probe = {button, axis, 0, true, true};
    auto present = [](u32 vk, void *userdata) -> s32 {
        auto &probe = *static_cast<Probe *>(userdata);
        if ((vk == probe.button && probe.button_present) ||
            (vk == probe.axis && probe.axis_present)) {
            probe.selected = vk;
            return 1;
        }
        return 0;
    };
    REQUIRE(actionBindingFindVkMatching(contexts, 1, 0, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 1, present, &probe) == alias);
    REQUIRE(probe.selected == axis); /* The precise button owns its own action. */
    probe.axis_present = false;
    probe.selected = 0;
    REQUIRE(actionBindingFindVkMatching(contexts, 1, 0, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 1, present, &probe) == 0);
    menu.has_mapping[ACTION_MENU_SECONDARY] = 0;
    REQUIRE(actionBindingFindVkMatching(contexts, 1, 0, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 1, present, &probe) == alias);
    REQUIRE(probe.selected == button);
}

TEST_CASE("wheel retirement leaves an independently held keyboard owner",
    "[input][actionmap][ownership]")
{
    InputMappingContext menu = {};
    ActionDigitalOwners owners;
    const auto key = actionSourceKey(ActionSourceKind::Keyboard, 0, VK_RETURN);
    const auto wheel = actionSourceKey(ActionSourceKind::Wheel, 0, 1);
    REQUIRE(owners.press(key, 0, ACTION_MENU_ACCEPT, &menu).first);
    owners.retireWhere([](uint64_t source, const ActionDigitalOwner &) {
        return (source >> 56) == uint8_t(ActionSourceKind::Wheel);
    }, [](const ActionDigitalRelease &) { FAIL("no wheel event should retire a key"); });
    REQUIRE(owners.count(0, ACTION_MENU_ACCEPT) == 1);

    REQUIRE_FALSE(owners.press(wheel, 0, ACTION_MENU_ACCEPT, &menu).first);
    bool releasedLast = true;
    owners.retireWhere([](uint64_t source, const ActionDigitalOwner &) {
        return (source >> 56) == uint8_t(ActionSourceKind::Wheel);
    }, [&](const ActionDigitalRelease &release) { releasedLast = release.last; });
    REQUIRE_FALSE(releasedLast);
    REQUIRE(owners.count(0, ACTION_MENU_ACCEPT) == 1);
    REQUIRE(owners.release(key).last);
}

TEST_CASE("device and context retirement remove only matching physical owners",
    "[input][actionmap][ownership]")
{
    InputMappingContext menu = {}, gameplay = {};
    ActionDigitalOwners owners;
    const auto rawButton = actionSourceKey(ActionSourceKind::RawButton, 42, 22);
    const auto rawAxis = actionSourceKey(ActionSourceKind::RawAxis, 42, 0);
    const auto otherPad = actionSourceKey(ActionSourceKind::ControllerButton, 43, 22);
    REQUIRE(rawButton != rawAxis);
    REQUIRE(rawButton != otherPad);
    REQUIRE(owners.press(rawButton, 0, ACTION_MENU_ACCEPT, &menu).first);
    REQUIRE_FALSE(owners.press(rawAxis, 0, ACTION_MENU_ACCEPT, &menu).first);
    REQUIRE_FALSE(owners.press(otherPad, 0, ACTION_MENU_ACCEPT, &gameplay).first);

    owners.retireWhere([](uint64_t source, const ActionDigitalOwner &) {
        return uint32_t(source >> 16) == 42;
    }, [](const ActionDigitalRelease &release) { REQUIRE_FALSE(release.last); });
    REQUIRE(owners.count(0, ACTION_MENU_ACCEPT) == 1);
    owners.retireWhere([&](uint64_t, const ActionDigitalOwner &owner) {
        return owner.context == &gameplay;
    }, [](const ActionDigitalRelease &release) { REQUIRE(release.last); });
    REQUIRE(owners.count(0, ACTION_MENU_ACCEPT) == 0);
}

TEST_CASE("active binding skips primary keys captured by another context",
    "[input][actionmap][binding][glyphs]")
{
    InputMappingContext menu = {}, lower = {};
    bind(menu, ACTION_MENU_SECONDARY, 0, VK_RETURN);
    bind(lower, ACTION_MENU_ACCEPT, 0, VK_RETURN);
    bind(lower, ACTION_MENU_ACCEPT, 1, VK_SPACE);
    ActionBindingContext contexts[] = {{&menu, 1}, {&lower, 1}};

    REQUIRE(actionBindingResolveVk(contexts, 2, VK_RETURN).context == &menu);
    REQUIRE(actionBindingResolveVk(contexts, 2, VK_RETURN).action == ACTION_MENU_SECONDARY);
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_KBM, 1)
        == (u32)VK_SPACE);
    lower.mappings[ACTION_MENU_ACCEPT].triggers[1].vk = 0;
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_KBM, 1) == 0);

    SECTION("a suppressed context cannot capture a visible lower key") {
        contexts[0].eligible = 0;
        REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_KBM, 1)
            == (u32)VK_RETURN);
    }
    SECTION("an inactive context cannot capture a visible lower key") {
        menu.active = 0;
        REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_KBM, 1)
            == (u32)VK_RETURN);
    }
}

TEST_CASE("active binding preserves trigger-slot and action-ID collision winners",
    "[input][actionmap][binding][glyphs]")
{
    InputMappingContext context = {};
    bind(context, ACTION_MENU_ACCEPT, 1, VK_RETURN);
    bind(context, ACTION_MENU_SECONDARY, 0, VK_RETURN);
    ActionBindingContext contexts[] = {{&context, 1}};

    REQUIRE(actionBindingResolveVk(contexts, 1, VK_RETURN).action == ACTION_MENU_SECONDARY);
    REQUIRE(actionBindingFindVk(contexts, 1, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_KBM, 1) == 0);
    bind(context, ACTION_MENU_ACCEPT, 0, VK_RETURN);
    REQUIRE(actionBindingResolveVk(contexts, 1, VK_RETURN).action == ACTION_MENU_ACCEPT);
    REQUIRE(actionBindingFindVk(contexts, 1, 0, ACTION_MENU_SECONDARY, ACTIONMAP_DEVICE_KBM, 1) == 0);
}

TEST_CASE("active binding follows live registry order and rebind changes immediately",
    "[input][actionmap][binding][glyphs]")
{
    InputMappingContext first = {}, second = {};
    bind(first, ACTION_FORGE_TOGGLE, 0, VK_F1);
    bind(second, ACTION_FORGE_TOGGLE, 0, VK_RETURN);
    ActionBindingContext contexts[] = {{&first, 1}, {&second, 1}};
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_FORGE_TOGGLE, ACTIONMAP_DEVICE_KBM, 1)
        == (u32)VK_F1);
    contexts[0].context = &second;
    contexts[1].context = &first;
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_FORGE_TOGGLE, ACTIONMAP_DEVICE_KBM, 1)
        == (u32)VK_RETURN);
    second.mappings[ACTION_FORGE_TOGGLE].triggers[0].vk = VK_SPACE;
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_FORGE_TOGGLE, ACTIONMAP_DEVICE_KBM, 1)
        == (u32)VK_SPACE);
    second.has_mapping[ACTION_FORGE_TOGGLE] = 0;
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_FORGE_TOGGLE, ACTIONMAP_DEVICE_KBM, 1)
        == (u32)VK_F1);
}

TEST_CASE("active binding layer denial cannot advertise a lower fallback",
    "[input][actionmap][binding][glyphs]")
{
    InputMappingContext top = {}, lower = {};
    bind(top, ACTION_FIRE_PRIMARY, 0, VK_SPACE);
    bind(lower, ACTION_MENU_ACCEPT, 0, VK_SPACE);
    ActionBindingContext contexts[] = {{&top, 1}, {&lower, 1}};

    REQUIRE(actionBindingResolveVk(contexts, 2, VK_SPACE).action == ACTION_FIRE_PRIMARY);
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_FIRE_PRIMARY, ACTIONMAP_DEVICE_KBM, 0) == 0);
    REQUIRE(actionBindingFindVk(contexts, 2, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_KBM, 1) == 0);
}

TEST_CASE("active binding respects player device and full keyboard chord domains",
    "[input][actionmap][binding][glyphs]")
{
    InputMappingContext context = {};
    bind(context, ACTION_MENU_ACCEPT, 0, VK_RETURN);
    bind(context, ACTION_MENU_ACCEPT, 1, VK_JOY1_BEGIN);
    bind(context, ACTION_MENU_ACCEPT, 2, VK_JOY2_BEGIN);
    ActionBindingContext contexts[] = {{&context, 1}};

    REQUIRE(actionBindingFindVk(contexts, 1, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_GAMEPAD, 1)
        == (u32)VK_JOY1_BEGIN);
    REQUIRE(actionBindingFindVk(contexts, 1, 1, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_GAMEPAD, 1)
        == (u32)VK_JOY2_BEGIN);
    REQUIRE(actionBindingFindVk(contexts, 1, 1, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_KBM, 1) == 0);
    REQUIRE(actionBindingFindVk(contexts, 1, 2, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_GAMEPAD, 1) == 0);
    context.mappings[ACTION_MENU_ACCEPT].triggers[1].vk = 0;
    REQUIRE(actionBindingFindVk(contexts, 1, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_GAMEPAD, 1) == 0);

    for (u32 chord = VK_CHORD_CTRL_TAB; chord <= VK_CHORD_ALT_RETURN; chord++) {
        CAPTURE(chord);
        context = {};
        bind(context, ACTION_SKIN_UNDO, 0, chord);
        REQUIRE(actionBindingVkMatchesDevice(chord, 0, ACTIONMAP_DEVICE_KBM));
        REQUIRE_FALSE(actionBindingVkMatchesDevice(chord, 0, ACTIONMAP_DEVICE_GAMEPAD));
        REQUIRE(actionBindingFindVk(contexts, 1, 0, ACTION_SKIN_UNDO, ACTIONMAP_DEVICE_KBM, 1)
            == chord);
    }
    REQUIRE(actionBindingVkMatchesDevice(VK_TOTAL_COUNT - 1, 3, ACTIONMAP_DEVICE_GAMEPAD));
    REQUIRE_FALSE(actionBindingVkMatchesDevice(VK_TOTAL_COUNT, 3, ACTIONMAP_DEVICE_GAMEPAD));
    REQUIRE_FALSE(actionBindingVkMatchesDevice(0, 0, ACTIONMAP_DEVICE_KBM));
    REQUIRE_FALSE(actionBindingVkMatchesDevice(VK_RETURN, -1, ACTIONMAP_DEVICE_KBM));
    REQUIRE_FALSE(actionBindingVkMatchesDevice(VK_RETURN, 0, 99));
}

TEST_CASE("active binding rejects invalid ranges and bounds malformed trigger counts",
    "[input][actionmap][binding][glyphs]")
{
    InputMappingContext context = {};
    bind(context, ACTION_MENU_ACCEPT, 3, VK_SPACE);
    ActionBindingContext contexts[] = {{&context, 1}};
    context.mappings[ACTION_MENU_ACCEPT].num_triggers = 999;
    REQUIRE(actionBindingFindVk(contexts, 1, 0, ACTION_MENU_ACCEPT, ACTIONMAP_DEVICE_KBM, 1)
        == (u32)VK_SPACE);
    context.mappings[ACTION_MENU_ACCEPT].num_triggers = -1;
    REQUIRE(actionBindingResolveVk(contexts, 1, VK_SPACE).action == ACTION_COUNT);
    REQUIRE(actionBindingResolveVk(nullptr, 1, VK_SPACE).context == nullptr);
    REQUIRE(actionBindingResolveVk(contexts, -1, VK_SPACE).action == ACTION_COUNT);
    REQUIRE(actionBindingResolveVk(contexts, ACTIONMAP_MAX_CONTEXTS + 1, VK_SPACE).action == ACTION_COUNT);
    REQUIRE(actionBindingResolveVk(contexts, 1, VK_TOTAL_COUNT).action == ACTION_COUNT);
    REQUIRE(actionBindingFindVk(contexts, 1, 0, (InputAction)-1, ACTIONMAP_DEVICE_KBM, 1) == 0);
    REQUIRE(actionBindingFindVk(contexts, 1, 0, ACTION_COUNT, ACTIONMAP_DEVICE_KBM, 1) == 0);
}

TEST_CASE("menu scroll preserves raw stick direction only under focused menu authority",
    "[input][actionmap][menu-scroll][binding]")
{
    REQUIRE(actionBindingMenuScrollY(1, 0, 0, 32767) == 1.0f);
    REQUIRE(actionBindingMenuScrollY(1, 0, 0, -32768) == -1.0f);
    REQUIRE(actionBindingMenuScrollY(1, 0, 0, 0) == 0.0f);
    REQUIRE(actionBindingMenuScrollY(1, 0, 0, 16384) == Approx(0.5f).margin(0.0001));
    REQUIRE(actionBindingMenuScrollY(1, 0, 0, -16384) == -0.5f);
    REQUIRE(actionBindingMenuScrollY(0, 0, 0, 32767) == 0.0f);
    REQUIRE(actionBindingMenuScrollY(1, 1, 0, 32767) == 0.0f);
    REQUIRE(actionBindingMenuScrollY(1, 0, 1, 32767) == 0.0f);
    REQUIRE(actionBindingMenuScrollY(1, 0, 0, 50000) == 1.0f);
    REQUIRE(actionBindingMenuScrollY(1, 0, 0, -50000) == -1.0f);
}

TEST_CASE("production dispatch glyph and menu scroll use shared authoritative seams",
    "[input][actionmap][binding][static]")
{
    const std::string actionmap = source("port/src/actionmap.cpp");
    const std::string glyphs = source("port/fast3d/pdgui_glyphs.cpp");
    REQUIRE(actionmap.find("const ActionBindingMatch winner = actionBindingResolveVkPair(")
        != std::string::npos);
    REQUIRE(actionmap.find("return actionBindingFindVk(contexts, s_NumActive, player, action, device, allowed)")
        != std::string::npos);
    REQUIRE(glyphs.find("kAllImcs") == std::string::npos);
    REQUIRE(glyphs.find("actionmapGetActiveBindingVkMatching(0, action, ACTIONMAP_DEVICE_GAMEPAD") != std::string::npos);
    REQUIRE(glyphs.find("actionmapGetVkName(vk)") != std::string::npos);
    REQUIRE(actionmap.find("SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY)")
        != std::string::npos);
    REQUIRE(actionmap.find("authority.focus_settle_remaining_ms > 0") != std::string::npos);
}


TEST_CASE("binding capability lookup tries later triggers without changing dispatch ownership",
    "[input][actionmap][binding][glyphs]")
{
    InputMappingContext top = {}, lower = {};
    bind(top, ACTION_MENU_SECONDARY, 0, VK_JOY1_BEGIN + 1);
    bind(lower, ACTION_MENU_ACCEPT, 0, VK_JOY1_BEGIN);
    bind(lower, ACTION_MENU_ACCEPT, 1, VK_JOY1_BEGIN + 1);
    bind(lower, ACTION_MENU_ACCEPT, 2, VK_JOY1_BEGIN + 2);
    bind(lower, ACTION_MENU_ACCEPT, 3, VK_JOY2_BEGIN + 2);
    ActionBindingContext contexts[] = {{&top, 1}, {&lower, 1}};
    u32 missing = VK_JOY1_BEGIN;
    auto present = [](u32 vk, void *data) -> s32 { return vk != *static_cast<u32 *>(data); };
    REQUIRE(actionBindingFindVkMatching(contexts, 2, 0, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 1, present, &missing) == (u32)VK_JOY1_BEGIN + 2);
    REQUIRE(actionBindingResolveVk(contexts, 2, VK_JOY1_BEGIN + 1).action == ACTION_MENU_SECONDARY);
    REQUIRE(actionBindingFindVkMatching(contexts, 2, 0, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 0, present, &missing) == 0);
    REQUIRE(actionBindingFindVkMatching(contexts, 2, 1, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 1, present, &missing) == (u32)VK_JOY2_BEGIN + 2);
    contexts[0].eligible = 0;
    REQUIRE(actionBindingFindVkMatching(contexts, 2, 0, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 1, present, &missing) == (u32)VK_JOY1_BEGIN + 1);
    auto absent = [](u32, void *) -> s32 { return 0; };
    REQUIRE(actionBindingFindVkMatching(contexts, 2, 0, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 1, absent, nullptr) == 0);
    REQUIRE(actionBindingFindVkMatching(contexts, 2, 0, ACTION_MENU_ACCEPT,
        ACTIONMAP_DEVICE_GAMEPAD, 1, nullptr, nullptr) == (u32)VK_JOY1_BEGIN);
}
