#include "input_device_identity.h"
#include <PR/os_thread.h>
#include "input.h"
#include "input_vk.h"

#include <stdio.h>
#include <string.h>

InputDeviceIdentity inputDeviceKeyboardIdentity(void)
{
    InputDeviceIdentity result = {};
    result.kind = INPUT_DEVICE_MKB;
    result.instance_id = -1;
    result.connected = 1;
    result.nintendo_button_labels = 1;
    return result;
}

void inputDeviceIdentityReset(InputDeviceIdentityState *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->last = inputDeviceKeyboardIdentity();
    for (int player = 0; player < INPUT_DEVICE_IDENTITY_PLAYERS; player++) {
        state->controller[player].kind = INPUT_DEVICE_GAMEPAD;
        state->controller[player].instance_id = -1;
        state->controller[player].player = player;
    }
}

int inputDeviceActivityMeaningful(const InputDeviceActivity *activity)
{
    if (!activity) return 0;
    switch (activity->kind) {
    case INPUT_ACTIVITY_PRESS:
    case INPUT_ACTIVITY_TEXT:
    case INPUT_ACTIVITY_WHEEL:
        return activity->value != 0;
    case INPUT_ACTIVITY_POINTER:
        return activity->value > 1 || activity->value < -1;
    case INPUT_ACTIVITY_AXIS: {
        /* Neutral drift cannot steal ownership. Accumulate deliberate motion
         * against the last meaningful position rather than every noisy event. */
        const int64_t value = activity->value;
        const int64_t previous = activity->prior_value;
        if (value > 32767 || value < -32768 ||
            previous > 32767 || previous < -32768) return 0;
        if (value < 8192 && value > -8192) return 0;
        const int64_t delta = value - previous;
        return delta >= 2048 || delta <= -2048;
    }
    case INPUT_ACTIVITY_RELEASE:
    default:
        return 0;
    }
}

static int identityEqual(const InputDeviceIdentity &left, const InputDeviceIdentity &right)
{
    return left.kind == right.kind && left.input_class == right.input_class &&
        left.family == right.family && left.standard_controller == right.standard_controller &&
        left.instance_id == right.instance_id && left.player == right.player &&
        left.connected == right.connected &&
        left.button_mask == right.button_mask && left.axis_mask == right.axis_mask &&
        left.nintendo_button_labels == right.nintendo_button_labels &&
        left.joycon_vertical_mode == right.joycon_vertical_mode;
}

int inputDeviceIdentityObserve(InputDeviceIdentityState *state,
    const InputDeviceIdentity *identity, const InputDeviceActivity *activity)
{
    if (!state || !identity || !inputDeviceActivityMeaningful(activity)) return 0;
    if (identity->kind != INPUT_DEVICE_MKB && identity->kind != INPUT_DEVICE_GAMEPAD) return 0;
    if (!identity->connected || identity->player < 0 ||
        identity->player >= INPUT_DEVICE_IDENTITY_PLAYERS) return 0;

    InputDeviceIdentity next = *identity;
    if (next.kind == INPUT_DEVICE_MKB) {
        next = inputDeviceKeyboardIdentity();
    } else {
        if (next.instance_id < 0) return 0;
        state->controller[next.player] = next;
        if (next.player != 0) return 0;
    }
    const int changed = !identityEqual(state->last, next);
    state->last = next;
    return changed;
}

void inputDeviceIdentityDisconnect(InputDeviceIdentityState *state, int32_t instance_id)
{
    if (!state || instance_id < 0) return;
    for (int player = 0; player < INPUT_DEVICE_IDENTITY_PLAYERS; player++) {
        InputDeviceIdentity &identity = state->controller[player];
        if (identity.instance_id == instance_id) {
            identity.connected = 0;
            identity.instance_id = -1;
        }
    }
    if (state->last.kind == INPUT_DEVICE_GAMEPAD && state->last.instance_id == instance_id) {
        state->last = inputDeviceKeyboardIdentity();
    }
}

static int isNintendo(int family)
{
    return family >= INPUT_GLYPH_SWITCH_PRO && family <= INPUT_GLYPH_JOYCON_PAIR;
}

int inputGlyphControllerSlotPresent(const InputDeviceIdentity *identity, int slot)
{
    if (!identity || slot < 0 || slot >= 32) return 0;
    const int axis = slot >= 30 ? slot - 26 : slot >= 22 ? (slot - 22) / 2 : -1;
    if (!(identity->button_mask & (uint32_t(1) << slot)) &&
        (axis < 0 || !(identity->axis_mask & (uint32_t(1) << axis)))) return 0;
    if (!identity->standard_controller) return 1;
    if (slot == 21) return 0; /* SDL_CONTROLLER_BUTTON_MAX is not a button. */
    const int left = identity->family == INPUT_GLYPH_JOYCON_LEFT;
    const int right = identity->family == INPUT_GLYPH_JOYCON_RIGHT;
    if (!left && !right) return 1;
    if (!identity->joycon_vertical_mode) {
        return slot < 4 || slot == 5 || slot == 6 || slot == 7 ||
            slot == 9 || slot == 10 || (slot >= 22 && slot <= 25) ||
            (left && (slot == 17 || slot == 19)) ||
            (right && (slot == 16 || slot == 18));
    }
    if (left) return slot == 4 || slot == 7 || slot == 9 ||
        (slot >= 11 && slot <= 15) || slot == 17 || slot == 19 ||
        (slot >= 22 && slot <= 25) || slot == 30;
    return slot < 4 || slot == 5 || slot == 6 || slot == 8 || slot == 10 ||
        slot == 16 || slot == 18 || (slot >= 26 && slot <= 29) || slot == 31;
}

int inputGlyphControllerVkPresent(const InputDeviceIdentity *identity, uint32_t vk)
{
    input_vk_control_t control;
    if (!identity || !inputVkDecodeController(vk, &control) ||
        control.player != identity->player) return 0;
    if (control.kind == INPUT_VK_LEGACY_OVERLAP || vk < VK_JOY_LEGACY_END) {
        return inputGlyphControllerSlotPresent(identity,
            (vk - VK_JOY_BEGIN) % INPUT_MAX_CONTROLLER_BUTTONS);
    }
    if (control.kind == INPUT_VK_BUTTON) {
        return !identity->standard_controller &&
            (identity->button_mask & (uint32_t(1) << control.index)) != 0;
    }
    const int ordinal = control.index < 4
        ? control.index * 2 + (control.direction > 0) : control.index + 4;
    return (identity->axis_mask & (uint32_t(1) << control.index)) != 0 &&
        inputGlyphControllerSlotPresent(identity, 22 + ordinal);
}

int inputGlyphControllerVkLabel(const InputDeviceIdentity *identity, uint32_t vk,
    char *out, size_t out_size)
{
    if (!out || !out_size) return 0;
    out[0] = '\0';
    if (!inputGlyphControllerVkPresent(identity, vk)) return 0;
    input_vk_control_t control;
    inputVkDecodeController(vk, &control);
    if (vk < VK_JOY_LEGACY_END) {
        return inputGlyphControllerLabel(identity,
            (vk - VK_JOY_BEGIN) % INPUT_MAX_CONTROLLER_BUTTONS, out, out_size);
    }
    if (control.kind == INPUT_VK_BUTTON) {
        snprintf(out, out_size, "Btn%d", control.index + 1);
        return 1;
    }
    const int ordinal = control.index < 4
        ? control.index * 2 + (control.direction > 0) : control.index + 4;
    const int slot = 22 + ordinal;
    InputDeviceIdentity axis_only = *identity;
    axis_only.button_mask &= ~(uint32_t(1) << slot);
    return inputGlyphControllerLabel(&axis_only, slot, out, out_size);
}

int inputGlyphControllerLabel(const InputDeviceIdentity *identity, int slot,
    char *out, size_t out_size)
{
    if (!out || !out_size) return 0;
    out[0] = '\0';
    if (!inputGlyphControllerSlotPresent(identity, slot)) return 0;

    const int family = identity->family;
    const int known_family = family >= INPUT_GLYPH_XBOX360 && family <= INPUT_GLYPH_JOYCON_PAIR;
    if (!identity->standard_controller || !known_family) {
        const int axis = slot >= 30 ? slot - 26 : slot >= 22 ? (slot - 22) / 2 : -1;
        if (axis >= 0 && (identity->axis_mask & (uint32_t(1) << axis))) {
            const char sign = slot >= 30 || (slot - 22) % 2 ? '+' : '-';
            if (identity->button_mask & (uint32_t(1) << slot)) {
                snprintf(out, out_size, "Btn%d/Axis%d%c", slot + 1, axis + 1, sign);
            } else {
                snprintf(out, out_size, "Axis%d%c", axis + 1, sign);
            }
        } else {
            snprintf(out, out_size, "Btn%d", slot + 1);
        }
        return 1;
    }

    const int playstation = family >= INPUT_GLYPH_PS3 && family <= INPUT_GLYPH_PS5;
    const int nintendo = isNintendo(family);
    const int joycon_left = family == INPUT_GLYPH_JOYCON_LEFT;
    const int joycon_right = family == INPUT_GLYPH_JOYCON_RIGHT;
    const int mini_joycon = (joycon_left || joycon_right) && !identity->joycon_vertical_mode;
    const char *label = NULL;
    static const char *const xbox_faces[] = {"A", "B", "X", "Y"};
    static const char *const ps_faces[] = {"Cross", "Circle", "Square", "Triangle"};
    static const char *const nintendo_positions[] = {"B", "A", "Y", "X"};
    /* SDL rotates separate Joy-Cons in horizontal mode before applying its
     * label/position hint. Left face keys have arrow marks, so describe their
     * position in the horizontal grip without requiring special font glyphs. */
    static const char *const joycon_left_faces[] = {"Face-R", "Face-D", "Face-U", "Face-L"};
    static const char *const joycon_right_faces[] = {"X", "A", "Y", "B"};
    static const char *const dpad[] = {"D-Up", "D-Down", "D-Left", "D-Right"};
    static const char *const stick_dirs[] = {
        "LS-L", "LS-R", "LS-U", "LS-D", "RS-L", "RS-R", "RS-U", "RS-D"
    };
    if (slot < 4) {
        if (mini_joycon) {
            const int mapped_slot = identity->nintendo_button_labels ? slot : (slot ^ 1);
            label = joycon_left ? joycon_left_faces[mapped_slot] : joycon_right_faces[mapped_slot];
        } else {
            label = playstation ? ps_faces[slot] :
                nintendo && !identity->nintendo_button_labels ? nintendo_positions[slot] : xbox_faces[slot];
        }
    } else if (slot >= 11 && slot <= 14) {
        label = dpad[slot - 11];
    } else if (slot >= 22 && slot <= 29) {
        label = stick_dirs[slot - 22];
    } else {
        switch (slot) {
        case 4:
            label = nintendo ? "Minus" : family == INPUT_GLYPH_PS3 ? "Select" :
                family == INPUT_GLYPH_PS4 ? "Share" : family == INPUT_GLYPH_PS5 ? "Create" :
                family == INPUT_GLYPH_XBOXONE ? "View" : "Back";
            break;
        case 5: label = mini_joycon && joycon_left ? "Capture" : nintendo ? "Home" : playstation ? "PS" : "Xbox"; break;
        case 6: label = mini_joycon && joycon_left ? "Minus" : nintendo ? "Plus" :
            playstation && family != INPUT_GLYPH_PS3 ? "Options" : family == INPUT_GLYPH_XBOXONE ? "Menu" : "Start"; break;
        case 7: label = mini_joycon ? "Stick" : playstation ? "L3" : "LS"; break;
        case 8: label = playstation ? "R3" : "RS"; break;
        case 9: label = mini_joycon ? "SL" : nintendo ? "L" : playstation ? "L1" : "LB"; break;
        case 10: label = mini_joycon ? "SR" : nintendo ? "R" : playstation ? "R1" : "RB"; break;
        case 15: label = nintendo ? "Capture" : family == INPUT_GLYPH_PS5 ? "Mic" :
            family == INPUT_GLYPH_XBOXONE ? "Share" : "Misc"; break;
        case 16: label = mini_joycon ? "R" : family == INPUT_GLYPH_JOYCON_PAIR || joycon_right ? "SR-R" : "P1"; break;
        case 17: label = mini_joycon ? "L" : family == INPUT_GLYPH_JOYCON_PAIR || joycon_left ? "SL-L" : "P3"; break;
        case 18: label = mini_joycon ? "ZR" : family == INPUT_GLYPH_JOYCON_PAIR || joycon_right ? "SL-R" : "P2"; break;
        case 19: label = mini_joycon ? "ZL" : family == INPUT_GLYPH_JOYCON_PAIR || joycon_left ? "SR-L" : "P4"; break;
        case 20: label = playstation ? "Touchpad" : "Btn21"; break;
        case 30: label = nintendo ? "ZL" : playstation ? "L2" : "LT"; break;
        case 31: label = nintendo ? "ZR" : playstation ? "R2" : "RT"; break;
        default: break;
        }
    }
    if (label) snprintf(out, out_size, "%s", label);
    else snprintf(out, out_size, "Btn%d", slot + 1);
    return 1;
}

InputGlyphBindingChoice inputGlyphChooseBinding(int preferred_device,
    uint32_t keyboard_vk, uint32_t controller_vk, int controller_available)
{
    InputGlyphBindingChoice result = {};
    const int prefer_controller = preferred_device == INPUT_DEVICE_GAMEPAD;
    if (prefer_controller && controller_vk && controller_available) {
        result.vk = controller_vk;
        result.kind = INPUT_DEVICE_GAMEPAD;
    } else if (keyboard_vk) {
        result.vk = keyboard_vk;
        result.kind = INPUT_DEVICE_MKB;
        result.alternate_device = prefer_controller;
    } else if (controller_vk && controller_available) {
        result.vk = controller_vk;
        result.kind = INPUT_DEVICE_GAMEPAD;
        result.alternate_device = !prefer_controller;
    } else {
        result.controller_absent = controller_vk && !controller_available;
    }
    return result;
}

int inputGlyphFormatBindingLabel(const InputGlyphBindingChoice *choice,
    const char *short_label, char *out, size_t out_size)
{
    if (!out || !out_size) return 0;
    out[0] = '\0';
    if (!choice || !choice->vk) {
        snprintf(out, out_size, "%s", choice && choice->controller_absent ? "Controller absent" : "Unbound");
        return 0;
    }
    if (!short_label || !short_label[0]) {
        snprintf(out, out_size, "Unbound");
        return 0;
    }
    const char *prefix = !choice->alternate_device ? "" :
        choice->kind == INPUT_DEVICE_GAMEPAD ? "Controller: " : "Keyboard: ";
    snprintf(out, out_size, "%s%s", prefix, short_label);
    return 1;
}
