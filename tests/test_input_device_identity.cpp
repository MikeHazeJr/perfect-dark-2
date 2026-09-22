#include "catch.hpp"
#include "input_device_identity.h"
#include <PR/os_thread.h>
#include "input.h"
#include "input_vk.h"

#include <climits>
#include <cstring>
#include <string>

namespace {
InputDeviceIdentity pad(int family = INPUT_GLYPH_XBOXONE, int id = 7, int player = 0)
{
    InputDeviceIdentity result = {};
    result.kind = INPUT_DEVICE_GAMEPAD;
    result.input_class = 1;
    result.family = family;
    result.standard_controller = 1;
    result.instance_id = id;
    result.player = player;
    result.connected = 1;
    result.button_mask = (uint32_t(1) << 21) - 1;
    result.axis_mask = 63;
    result.nintendo_button_labels = 1;
    return result;
}
std::string label(const InputDeviceIdentity &identity, int slot)
{
    char text[64] = {};
    inputGlyphControllerLabel(&identity, slot, text, sizeof(text));
    return text;
}
std::string bindingLabel(const InputGlyphBindingChoice &choice, const char *text)
{
    char result[64] = {};
    inputGlyphFormatBindingLabel(&choice, text, result, sizeof(result));
    return result;
}
}

TEST_CASE("device identity changes immediately across controllers in the same class",
    "[input][glyphs][identity]")
{
    InputDeviceIdentityState state;
    inputDeviceIdentityReset(&state);
    REQUIRE(state.last.kind == INPUT_DEVICE_MKB);
    const InputDeviceActivity press = {INPUT_ACTIVITY_PRESS, 1, 0};
    auto xbox = pad();
    REQUIRE(inputDeviceIdentityObserve(&state, &xbox, &press) == 1);
    REQUIRE(inputDeviceIdentityObserve(&state, &xbox, &press) == 0);
    auto ps = pad(INPUT_GLYPH_PS5, 8);
    REQUIRE(inputDeviceIdentityObserve(&state, &ps, &press) == 1);
    REQUIRE(state.last.family == INPUT_GLYPH_PS5);
    REQUIRE(state.last.instance_id == 8);
    ps.instance_id = 9;
    REQUIRE(inputDeviceIdentityObserve(&state, &ps, &press) == 1);
    REQUIRE(state.last.instance_id == 9);
}

TEST_CASE("captured keyboard and text activity updates identity without action dispatch",
    "[input][glyphs][identity]")
{
    for (const int kind : {INPUT_ACTIVITY_PRESS, INPUT_ACTIVITY_TEXT}) {
        InputDeviceIdentityState state;
        inputDeviceIdentityReset(&state);
        auto xbox = pad();
        auto keyboard = inputDeviceKeyboardIdentity();
        const InputDeviceActivity press = {INPUT_ACTIVITY_PRESS, 1, 0};
        inputDeviceIdentityObserve(&state, &xbox, &press);
        const InputDeviceActivity typing = {kind, 1, 0};
        REQUIRE(inputDeviceIdentityObserve(&state, &keyboard, &typing) == 1);
        REQUIRE(state.last.kind == INPUT_DEVICE_MKB);
        REQUIRE(state.controller[0].instance_id == xbox.instance_id);
        const InputDeviceActivity release = {INPUT_ACTIVITY_RELEASE, 1, 0};
        REQUIRE(inputDeviceIdentityObserve(&state, &xbox, &release) == 0);
        REQUIRE(state.last.kind == INPUT_DEVICE_MKB);
    }
}

TEST_CASE("identity ignores noise neutral motion and stale release events",
    "[input][glyphs][identity]")
{
    const InputDeviceActivity ignored[] = {
        {INPUT_ACTIVITY_RELEASE, 1, 0}, {INPUT_ACTIVITY_POINTER, 1, 0},
        {INPUT_ACTIVITY_POINTER, -1, 0}, {INPUT_ACTIVITY_WHEEL, 0, 0},
        {INPUT_ACTIVITY_TEXT, 0, 0}, {INPUT_ACTIVITY_AXIS, 8191, 0},
        {INPUT_ACTIVITY_AXIS, -8191, 0}, {INPUT_ACTIVITY_AXIS, 25001, 25000},
        {INPUT_ACTIVITY_AXIS, 0, 25000}, {INPUT_ACTIVITY_AXIS, INT_MAX, 0},
        {INPUT_ACTIVITY_AXIS, 25000, INT_MIN}
    };
    for (const auto &activity : ignored) REQUIRE_FALSE(inputDeviceActivityMeaningful(&activity));
    const InputDeviceActivity meaningful[] = {
        {INPUT_ACTIVITY_POINTER, -2, 0}, {INPUT_ACTIVITY_WHEEL, -1, 0},
        {INPUT_ACTIVITY_AXIS, 8192, 0}, {INPUT_ACTIVITY_AXIS, -8192, 0},
        {INPUT_ACTIVITY_AXIS, 12048, 10000}, {INPUT_ACTIVITY_AXIS, -32768, 32767}
    };
    for (const auto &activity : meaningful) REQUIRE(inputDeviceActivityMeaningful(&activity));
}

TEST_CASE("other players and disconnected devices cannot steal menu identity",
    "[input][glyphs][identity]")
{
    InputDeviceIdentityState state;
    inputDeviceIdentityReset(&state);
    auto primary = pad(INPUT_GLYPH_PS4, 4);
    auto secondary = pad(INPUT_GLYPH_XBOXONE, 5, 3);
    const InputDeviceActivity press = {INPUT_ACTIVITY_PRESS, 1, 0};
    inputDeviceIdentityObserve(&state, &primary, &press);
    REQUIRE_FALSE(inputDeviceIdentityObserve(&state, &secondary, &press));
    REQUIRE(state.last.instance_id == 4);
    REQUIRE(state.controller[3].instance_id == 5);
    inputDeviceIdentityDisconnect(&state, 5);
    REQUIRE(state.last.instance_id == 4);
    REQUIRE_FALSE(state.controller[3].connected);
    inputDeviceIdentityDisconnect(&state, 4);
    REQUIRE(state.last.kind == INPUT_DEVICE_MKB);
    REQUIRE_FALSE(state.controller[0].connected);
    primary.connected = 0;
    REQUIRE_FALSE(inputDeviceIdentityObserve(&state, &primary, &press));
    primary.connected = 1;
    primary.instance_id = -1;
    REQUIRE_FALSE(inputDeviceIdentityObserve(&state, &primary, &press));
}

TEST_CASE("controller labels use real family button names without special fonts",
    "[input][glyphs][identity]")
{
    struct Example { int family, slot; const char *expected; };
    const Example cases[] = {
        {INPUT_GLYPH_XBOX360, 0, "A"}, {INPUT_GLYPH_XBOXONE, 1, "B"},
        {INPUT_GLYPH_XBOXONE, 9, "LB"}, {INPUT_GLYPH_XBOXONE, 31, "RT"},
        {INPUT_GLYPH_XBOXONE, 4, "View"}, {INPUT_GLYPH_XBOXONE, 6, "Menu"},
        {INPUT_GLYPH_XBOXONE, 17, "P3"}, {INPUT_GLYPH_XBOXONE, 18, "P2"},
        {INPUT_GLYPH_PS3, 0, "Cross"}, {INPUT_GLYPH_PS3, 4, "Select"},
        {INPUT_GLYPH_PS3, 6, "Start"}, {INPUT_GLYPH_PS4, 1, "Circle"},
        {INPUT_GLYPH_PS4, 4, "Share"}, {INPUT_GLYPH_PS5, 2, "Square"},
        {INPUT_GLYPH_PS5, 3, "Triangle"}, {INPUT_GLYPH_PS5, 4, "Create"},
        {INPUT_GLYPH_PS5, 6, "Options"}, {INPUT_GLYPH_PS5, 15, "Mic"},
        {INPUT_GLYPH_PS5, 20, "Touchpad"}, {INPUT_GLYPH_PS5, 30, "L2"},
        {INPUT_GLYPH_SWITCH_PRO, 4, "Minus"}, {INPUT_GLYPH_SWITCH_PRO, 6, "Plus"},
        {INPUT_GLYPH_SWITCH_PRO, 9, "L"}, {INPUT_GLYPH_SWITCH_PRO, 31, "ZR"},
        {INPUT_GLYPH_JOYCON_PAIR, 16, "SR-R"}, {INPUT_GLYPH_JOYCON_PAIR, 17, "SL-L"},
        {INPUT_GLYPH_JOYCON_PAIR, 18, "SL-R"}, {INPUT_GLYPH_JOYCON_PAIR, 19, "SR-L"}
    };
    for (const auto &example : cases) {
        CAPTURE(example.family, example.slot);
        REQUIRE(label(pad(example.family), example.slot) == example.expected);
    }
}

TEST_CASE("Nintendo face labels follow SDL label versus position policy",
    "[input][glyphs][identity]")
{
    const char *printed[] = {"A", "B", "X", "Y"};
    const char *position[] = {"B", "A", "Y", "X"};
    for (const int family : {INPUT_GLYPH_SWITCH_PRO, INPUT_GLYPH_JOYCON_PAIR}) {
        auto identity = pad(family);
        for (int slot = 0; slot < 4; slot++) {
            identity.nintendo_button_labels = 1;
            REQUIRE(label(identity, slot) == printed[slot]);
            identity.nintendo_button_labels = 0;
            REQUIRE(label(identity, slot) == position[slot]);
        }
    }
    auto ps = pad(INPUT_GLYPH_PS5);
    ps.nintendo_button_labels = 0;
    REQUIRE(label(ps, 0) == "Cross");
}

TEST_CASE("single JoyCons label horizontal and vertical SDL controls correctly",
    "[input][glyphs][identity]")
{
    auto left = pad(INPUT_GLYPH_JOYCON_LEFT);
    auto right = pad(INPUT_GLYPH_JOYCON_RIGHT);
    REQUIRE(label(left, 0) == "Face-R");
    REQUIRE(label(right, 0) == "X");
    REQUIRE(label(left, 5) == "Capture");
    REQUIRE(label(left, 6) == "Minus");
    REQUIRE(label(left, 9) == "SL");
    REQUIRE(label(right, 10) == "SR");
    REQUIRE(label(left, 17) == "L");
    REQUIRE(label(left, 19) == "ZL");
    REQUIRE(label(right, 16) == "R");
    REQUIRE(label(right, 18) == "ZR");
    REQUIRE(label(right, 31).empty());
    left.nintendo_button_labels = right.nintendo_button_labels = 0;
    REQUIRE(label(left, 0) == "Face-D");
    REQUIRE(label(right, 0) == "A");
    left.joycon_vertical_mode = right.joycon_vertical_mode = 1;
    REQUIRE(label(left, 0).empty());
    REQUIRE(label(left, 11) == "D-Up");
    REQUIRE(label(left, 30) == "ZL");
    REQUIRE(label(right, 0) == "B");
    REQUIRE(label(right, 31) == "ZR");
    REQUIRE(label(right, 22).empty());
}

TEST_CASE("generic and accessibility controls preserve real high button and axis aliases",
    "[input][glyphs][identity]")
{
    auto custom = pad(INPUT_GLYPH_PS5);
    custom.standard_controller = 0;
    custom.button_mask = uint32_t(1) << 31;
    custom.axis_mask = 0;
    REQUIRE(label(custom, 31) == "Btn32");
    REQUIRE(label(custom, 0).empty());
    custom.axis_mask = uint32_t(1) << 5;
    REQUIRE(label(custom, 31) == "Btn32/Axis6+");
    custom.button_mask = 0;
    REQUIRE(label(custom, 31) == "Axis6+");
    auto generic = pad(INPUT_GLYPH_GENERIC);
    REQUIRE(label(generic, 0) == "Btn1");
    REQUIRE(label(generic, 22) == "Axis1-");
    REQUIRE(label(generic, 32).empty());
    generic.button_mask &= ~(uint32_t(1) << 0);
    REQUIRE_FALSE(inputGlyphControllerSlotPresent(&generic, 0));
}

TEST_CASE("precise joystick prompts advertise only the physical winning control",
    "[input][glyphs][identity][raw-vk]")
{
    auto custom = pad(INPUT_GLYPH_GENERIC);
    custom.standard_controller = 0;
    const u32 button = inputVkRawButton(0, 22);
    const u32 axis = inputVkDigitalAxis(0, 0, -1);
    const u32 legacy = VK_JOY1_LSTICK_LEFT;
    char text[64] = {};
    custom.button_mask = uint32_t(1) << 22;
    custom.axis_mask = 0;
    REQUIRE(inputGlyphControllerVkPresent(&custom, button));
    REQUIRE_FALSE(inputGlyphControllerVkPresent(&custom, axis));
    REQUIRE(inputGlyphControllerVkLabel(&custom, button, text, sizeof(text)));
    REQUIRE(std::string(text) == "Btn23");
    custom.button_mask = 0;
    custom.axis_mask = 1;
    REQUIRE_FALSE(inputGlyphControllerVkPresent(&custom, button));
    REQUIRE(inputGlyphControllerVkPresent(&custom, axis));
    REQUIRE(inputGlyphControllerVkLabel(&custom, axis, text, sizeof(text)));
    REQUIRE(std::string(text) == "Axis1-");
    custom.button_mask = uint32_t(1) << 22;
    REQUIRE(inputGlyphControllerVkLabel(&custom, legacy, text, sizeof(text)));
    REQUIRE(std::string(text) == "Btn23/Axis1-");
    REQUIRE(inputGlyphControllerVkLabel(&custom, axis, text, sizeof(text)));
    REQUIRE(std::string(text) == "Axis1-");
    custom.standard_controller = 1;
    REQUIRE_FALSE(inputGlyphControllerVkPresent(&custom, button));
    REQUIRE(inputGlyphControllerVkPresent(&custom, axis));
}

TEST_CASE("alternate glyph bindings identify device and do not advertise absent controllers",
    "[input][glyphs][identity]")
{
    auto choice = inputGlyphChooseBinding(INPUT_DEVICE_GAMEPAD, 40, 519, 1);
    REQUIRE(choice.vk == 519);
    REQUIRE(bindingLabel(choice, "Cross") == "Cross");
    choice = inputGlyphChooseBinding(INPUT_DEVICE_GAMEPAD, 40, 0, 1);
    REQUIRE(choice.vk == 40);
    REQUIRE(bindingLabel(choice, "Enter") == "Keyboard: Enter");
    choice = inputGlyphChooseBinding(INPUT_DEVICE_MKB, 0, 519, 1);
    REQUIRE(bindingLabel(choice, "A") == "Controller: A");
    choice = inputGlyphChooseBinding(INPUT_DEVICE_MKB, 0, 519, 0);
    REQUIRE(choice.vk == 0);
    REQUIRE(bindingLabel(choice, "A") == "Controller absent");
    choice = inputGlyphChooseBinding(INPUT_DEVICE_GAMEPAD, 40, 519, 0);
    REQUIRE(bindingLabel(choice, "Enter") == "Keyboard: Enter");
    choice = inputGlyphChooseBinding(INPUT_DEVICE_MKB, 0, 0, 1);
    REQUIRE(bindingLabel(choice, "") == "Unbound");
    char tiny[2] = {'!', '!'};
    inputGlyphFormatBindingLabel(&choice, "", tiny, sizeof(tiny));
    REQUIRE(tiny[1] == '\0');
}
