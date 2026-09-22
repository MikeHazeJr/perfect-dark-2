#ifndef PD_ACTIONMAP_BINDING_H
#define PD_ACTIONMAP_BINDING_H

#include "actionmap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Caller supplies the actual active registry in dispatch priority order.
 * Eligibility carries the production context-suppression decision. */
typedef struct ActionBindingContext {
    const InputMappingContext *context;
    s32 eligible;
} ActionBindingContext;

typedef struct ActionBindingMatch {
    const InputMappingContext *context;
    InputAction action; /* ACTION_COUNT when no context maps the VK. */
    u32 vk; /* The precise key or retained legacy alias that won. */
} ActionBindingMatch;

/* Resolve one physical key exactly once: first context, then lowest trigger
 * slot, then lowest action ID. A denied winning action must not fall through
 * to a different context; the caller applies its layer gate to this result. */
ActionBindingMatch actionBindingResolveVk(const ActionBindingContext *contexts,
    s32 count, u32 vk);
/* One physical controller edge may have a precise key and an old shared
 * alias. Context priority wins first; within one context the precise key
 * beats the alias. The selected key/action is retained by the source owner. */
ActionBindingMatch actionBindingResolveVkPair(const ActionBindingContext *contexts,
    s32 count, u32 precise_vk, u32 legacy_alias);

/* Find a requested action's first binding that actually wins dispatch. This
 * does not invent another-device fallback. action_allowed is the caller's
 * production layer/freefly decision for the requested action. */
u32 actionBindingFindVk(const ActionBindingContext *contexts, s32 count,
    s32 player, InputAction action, s32 device, s32 action_allowed);

/* Capability filtering receives physical candidate VKs. A legacy shared
 * alias is advertised only if a present button or axis actually resolves to
 * it after precise-key precedence. A null predicate permits both sources. */
typedef s32 (*ActionBindingVkPredicate)(u32 vk, void *userdata);
u32 actionBindingFindVkMatching(const ActionBindingContext *contexts, s32 count,
    s32 player, InputAction action, s32 device, s32 action_allowed,
    ActionBindingVkPredicate matches, void *userdata);

/* Canonical virtkey domains, including synthesized keyboard chords. */
s32 actionBindingVkMatchesDevice(u32 vk, s32 player, s32 device);

/* Menu scroll is independent of gameplay look sensitivity, inversion and
 * suppression. Raw right-stick Y is SDL signed 16-bit, positive downward. */
f32 actionBindingMenuScrollY(s32 menu_authority, s32 focus_lost,
    s32 focus_settling, s32 raw_y);

#ifdef __cplusplus
}
#endif

#endif
