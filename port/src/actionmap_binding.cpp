#include "actionmap_binding.h"

/* input.h owns the VK domains. os_cont.h needs OSThread declared first. */
#include <PR/os_thread.h>
#include "input.h"
#include "input_vk.h"

static s32 boundedTriggerCount(const InputMapping *mapping)
{
    if (mapping->num_triggers < 0) return 0;
    if (mapping->num_triggers > ACTIONMAP_MAX_TRIGGERS) {
        return ACTIONMAP_MAX_TRIGGERS;
    }
    return mapping->num_triggers;
}

ActionBindingMatch actionBindingResolveVk(const ActionBindingContext *contexts,
    s32 count, u32 vk)
{
    return actionBindingResolveVkPair(contexts, count, vk, 0);
}

ActionBindingMatch actionBindingResolveVkPair(const ActionBindingContext *contexts,
    s32 count, u32 precise_vk, u32 legacy_alias)
{
    ActionBindingMatch result = {NULL, ACTION_COUNT, 0};
    if (!contexts || count <= 0 || count > ACTIONMAP_MAX_CONTEXTS ||
        precise_vk == 0 || precise_vk >= (u32)VK_TOTAL_COUNT ||
        legacy_alias >= (u32)VK_TOTAL_COUNT) {
        return result;
    }

    for (s32 ci = 0; ci < count; ci++) {
        const InputMappingContext *ctx = contexts[ci].context;
        if (!contexts[ci].eligible || !ctx || !ctx->active) continue;

        for (s32 candidate = 0; candidate < (legacy_alias && legacy_alias != precise_vk ? 2 : 1); candidate++) {
            const u32 vk = candidate == 0 ? precise_vk : legacy_alias;
            s32 best_action = ACTION_COUNT;
            s32 best_slot = ACTIONMAP_MAX_TRIGGERS;
            for (s32 action = 0; action < ACTION_COUNT; action++) {
                if (!ctx->has_mapping[action]) continue;
                const InputMapping *mapping = &ctx->mappings[action];
                for (s32 slot = 0; slot < boundedTriggerCount(mapping); slot++) {
                    if (mapping->triggers[slot].vk == vk &&
                        (slot < best_slot ||
                         (slot == best_slot && action < best_action))) {
                        best_slot = slot;
                        best_action = action;
                    }
                }
            }
            if (best_action < ACTION_COUNT) {
                result.context = ctx;
                result.action = (InputAction)best_action;
                result.vk = vk;
                return result;
            }
        }
    }
    return result;
}

s32 actionBindingVkMatchesDevice(u32 vk, s32 player, s32 device)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS ||
        vk == 0 || vk >= (u32)VK_TOTAL_COUNT) return 0;

    if (device == ACTIONMAP_DEVICE_KBM) {
        return player == 0 && vk < (u32)VK_JOY_BEGIN;
    }
    if (device != ACTIONMAP_DEVICE_GAMEPAD || vk < (u32)VK_JOY_BEGIN) return 0;
    input_vk_control_t control;
    return inputVkDecodeController(vk, &control) && control.player == player;
}

u32 actionBindingFindVk(const ActionBindingContext *contexts, s32 count,
    s32 player, InputAction action, s32 device, s32 action_allowed)
{
    return actionBindingFindVkMatching(contexts, count, player, action, device,
        action_allowed, nullptr, nullptr);
}

u32 actionBindingFindVkMatching(const ActionBindingContext *contexts, s32 count,
    s32 player, InputAction action, s32 device, s32 action_allowed,
    ActionBindingVkPredicate matches, void *userdata)
{
    if (!contexts || count <= 0 || count > ACTIONMAP_MAX_CONTEXTS ||
        action < 0 || action >= ACTION_COUNT || !action_allowed) return 0;

    for (s32 ci = 0; ci < count; ci++) {
        const InputMappingContext *ctx = contexts[ci].context;
        if (!contexts[ci].eligible || !ctx || !ctx->active ||
            !ctx->has_mapping[action]) continue;

        const InputMapping *mapping = &ctx->mappings[action];
        for (s32 slot = 0; slot < boundedTriggerCount(mapping); slot++) {
            const u32 vk = mapping->triggers[slot].vk;
            if (!actionBindingVkMatchesDevice(vk, player, device)) continue;
            u32 physical[2] = {vk, 0};
            s32 physical_count = 1;
            if (device == ACTIONMAP_DEVICE_GAMEPAD &&
                vk < (u32)VK_JOY_LEGACY_END &&
                (vk - (u32)VK_JOY_BEGIN) % INPUT_MAX_CONTROLLER_BUTTONS >= 22) {
                const s32 ordinal = (vk - (u32)VK_JOY_BEGIN) % INPUT_MAX_CONTROLLER_BUTTONS - 22;
                physical[0] = inputVkRawButton(player, 22 + ordinal);
                physical[1] = inputVkAxisByOrdinal(player, ordinal);
                physical_count = 2;
            }
            for (s32 candidate = 0; candidate < physical_count; candidate++) {
                const u32 source_vk = physical[candidate];
                if (matches && !matches(source_vk, userdata)) continue;
                const ActionBindingMatch winner = actionBindingResolveVkPair(
                    contexts, count, source_vk, inputVkLegacyAlias(source_vk));
                if (winner.action == action && winner.vk == vk) return vk;
            }
        }
    }
    return 0;
}

f32 actionBindingMenuScrollY(s32 menu_authority, s32 focus_lost,
    s32 focus_settling, s32 raw_y)
{
    if (!menu_authority || focus_lost || focus_settling) return 0.0f;
    if (raw_y <= -32768) return -1.0f;
    if (raw_y >= 32767) return 1.0f;
    return raw_y < 0 ? raw_y / 32768.0f : raw_y / 32767.0f;
}
