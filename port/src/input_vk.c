/* Canonical controller-key conversions. Existing 0..646 values stay fixed. */
#include <PR/os_thread.h>
#include "input.h"
#include "input_vk.h"
#include <stdio.h>
#include <string.h>

static const char *const legacy_names[32] = {
    "A", "B", "X", "Y", "BACK", "GUIDE", "START", "LSTICK", "RSTICK",
    "LSHOULDER", "RSHOULDER", "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
    "BUTTON_15", "BUTTON_16", "BUTTON_17", "BUTTON_18", "BUTTON_19", "TOUCHPAD",
    "BUTTON_21", "LSTICK_LEFT", "LSTICK_RIGHT", "LSTICK_UP", "LSTICK_DOWN",
    "RSTICK_LEFT", "RSTICK_RIGHT", "RSTICK_UP", "RSTICK_DOWN", "LTRIGGER", "RTRIGGER"
};
static const char *const axis_names[10] = {
    "AXIS0_NEG", "AXIS0_POS", "AXIS1_NEG", "AXIS1_POS",
    "AXIS2_NEG", "AXIS2_POS", "AXIS3_NEG", "AXIS3_POS", "AXIS4_POS", "AXIS5_POS"
};

static int valid_player(int player)
{
    return player >= 0 && player < INPUT_MAX_CONTROLLERS;
}

static int axis_ordinal(int axis, int direction)
{
    if (axis >= 0 && axis < 4 && (direction == -1 || direction == 1))
        return axis * 2 + (direction > 0);
    if ((axis == 4 || axis == 5) && direction == 1) return axis + 4;
    return -1;
}

uint32_t inputVkRawButton(int player, int button)
{
    if (!valid_player(player) || button < 0 || button >= INPUT_MAX_CONTROLLER_BUTTONS)
        return 0;
    if (button < 22) return VK_JOY_BEGIN + player * INPUT_MAX_CONTROLLER_BUTTONS + button;
    return VK_JOY_RAW_BUTTON_BEGIN + player * 10 + button - 22;
}

uint32_t inputVkDigitalAxis(int player, int axis, int direction)
{
    int ordinal = axis_ordinal(axis, direction);
    if (!valid_player(player) || ordinal < 0) return 0;
    return VK_JOY_AXIS_BEGIN + player * 10 + ordinal;
}

uint32_t inputVkAxisByOrdinal(int player, int ordinal)
{
    if (!valid_player(player) || ordinal < 0 || ordinal >= 10) return 0;
    return VK_JOY_AXIS_BEGIN + player * 10 + ordinal;
}

int inputVkDecodeController(uint32_t vk, input_vk_control_t *control)
{
    input_vk_control_t result = {0};
    int ordinal = -1;
    if (!control || vk < VK_JOY_BEGIN || vk >= VK_TOTAL_COUNT) return 0;
    if (vk < VK_JOY_LEGACY_END) {
        uint32_t offset = vk - VK_JOY_BEGIN;
        result.player = offset / INPUT_MAX_CONTROLLER_BUTTONS;
        int slot = offset % INPUT_MAX_CONTROLLER_BUTTONS;
        if (slot < 22) {
            result.kind = INPUT_VK_BUTTON;
            result.index = slot;
        } else {
            result.kind = INPUT_VK_LEGACY_OVERLAP;
            ordinal = slot - 22;
        }
    } else if (vk < VK_JOY_AXIS_BEGIN) {
        uint32_t offset = vk - VK_JOY_RAW_BUTTON_BEGIN;
        result.player = offset / 10;
        result.kind = INPUT_VK_BUTTON;
        result.index = 22 + offset % 10;
    } else {
        uint32_t offset = vk - VK_JOY_AXIS_BEGIN;
        result.player = offset / 10;
        result.kind = INPUT_VK_AXIS;
        ordinal = offset % 10;
    }
    if (!valid_player(result.player)) return 0;
    if (ordinal >= 0) {
        result.index = ordinal < 8 ? ordinal / 2 : ordinal - 4;
        result.direction = ordinal < 8 && !(ordinal & 1) ? -1 : 1;
    }
    *control = result;
    return 1;
}

uint32_t inputVkLegacyAlias(uint32_t vk)
{
    input_vk_control_t c;
    if (!inputVkDecodeController(vk, &c)) return 0;
    if (vk < VK_JOY_LEGACY_END) return 0;
    int slot = c.kind == INPUT_VK_BUTTON ? c.index
        : 22 + axis_ordinal(c.index, c.direction);
    return VK_JOY_BEGIN + c.player * INPUT_MAX_CONTROLLER_BUTTONS + slot;
}

int inputVkControllerName(uint32_t vk, char *out, size_t capacity)
{
    input_vk_control_t c;
    char name[32];
    if (!out || !capacity || !inputVkDecodeController(vk, &c)) return 0;
    int n;
    if (vk < VK_JOY_LEGACY_END) {
        int slot = (vk - VK_JOY_BEGIN) % INPUT_MAX_CONTROLLER_BUTTONS;
        n = snprintf(name, sizeof(name), "JOY%d_%s", c.player + 1, legacy_names[slot]);
    } else if (c.kind == INPUT_VK_BUTTON) {
        n = snprintf(name, sizeof(name), "JOY%d_BUTTON_%d", c.player + 1, c.index);
    } else {
        n = snprintf(name, sizeof(name), "JOY%d_%s", c.player + 1,
            axis_names[axis_ordinal(c.index, c.direction)]);
    }
    if (n < 0 || (size_t)n >= sizeof(name) || (size_t)n >= capacity) return 0;
    memcpy(out, name, (size_t)n + 1);
    return 1;
}

uint32_t inputVkControllerByName(const char *name)
{
    if (!name || strlen(name) < 6 || strncmp(name, "JOY", 3) != 0 ||
            name[3] < '1' || name[3] > '4' || name[4] != '_') return 0;
    int player = name[3] - '1';
    const char *suffix = name + 5;
    for (int slot = 0; slot < INPUT_MAX_CONTROLLER_BUTTONS; ++slot) {
        if (strcmp(suffix, legacy_names[slot]) == 0)
            return VK_JOY_BEGIN + player * INPUT_MAX_CONTROLLER_BUTTONS + slot;
    }
    for (int ordinal = 0; ordinal < 10; ++ordinal) {
        if (strcmp(suffix, axis_names[ordinal]) == 0)
            return VK_JOY_AXIS_BEGIN + player * 10 + ordinal;
        char button[16];
        snprintf(button, sizeof(button), "BUTTON_%d", 22 + ordinal);
        if (strcmp(suffix, button) == 0)
            return VK_JOY_RAW_BUTTON_BEGIN + player * 10 + ordinal;
    }
    return 0;
}
