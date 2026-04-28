/*
 * actionmap_pure.c -- pure-C mirror of action-map flush + classification.
 * See actionmap_pure.h header for the contract.
 */

#include "actionmap_pure.h"
#include <string.h>

static AmpActionState s_State[AMP_MAX_PLAYERS][AMP_ACTION_COUNT];

static const AmpInputAction s_CutsceneActionSet[] = {
    AMP_ACTION_SKIP_CUTSCENE,
    AMP_ACTION_USE,
    AMP_ACTION_CANCEL_USE,
    AMP_ACTION_FIRE_PRIMARY,
    AMP_ACTION_FIRE_SECONDARY,
    AMP_ACTION_PAUSE,
    AMP_ACTION_FIRE_MODE,
    AMP_ACTION_RELOAD,
    AMP_ACTION_WEAPON_NEXT,
};

#define AMP_ARRAYCOUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void ampReset(void)
{
    memset(s_State, 0, sizeof(s_State));
}

void ampSetHeld(int player, AmpInputAction a)
{
    if (player < 0 || player >= AMP_MAX_PLAYERS) return;
    if (a < 0 || a >= AMP_ACTION_COUNT) return;
    AmpActionState *st = &s_State[player][a];
    st->held    = 1;
    st->pressed = 1;
    st->value   = 1.0f;
    st->down_time_ms = 1;
}

void ampSetReleased(int player, AmpInputAction a)
{
    if (player < 0 || player >= AMP_MAX_PLAYERS) return;
    if (a < 0 || a >= AMP_ACTION_COUNT) return;
    AmpActionState *st = &s_State[player][a];
    st->held     = 0;
    st->released = 1;
    st->up_time_ms = 2;
}

void ampSetPressed(int player, AmpInputAction a)
{
    if (player < 0 || player >= AMP_MAX_PLAYERS) return;
    if (a < 0 || a >= AMP_ACTION_COUNT) return;
    AmpActionState *st = &s_State[player][a];
    st->pressed = 1;
    st->held    = 1;
    st->value   = 1.0f;
    st->down_time_ms = 1;
}

const AmpActionState *ampGetState(int player, AmpInputAction a)
{
    if (player < 0 || player >= AMP_MAX_PLAYERS) return 0;
    if (a < 0 || a >= AMP_ACTION_COUNT) return 0;
    return &s_State[player][a];
}

int ampIsGameplayOnly(AmpInputAction a)
{
    if (a < 0 || a >= AMP_ACTION_COUNT) {
        return 0;
    }

    /* Menu navigation: explicitly owned by the menu layer. */
    if (a >= AMP_ACTION_MENU_UP && a <= AMP_ACTION_MENU_TAB_NEXT) {
        return 0;
    }

    /* Shared / system actions: menus legitimately consume these too. */
    switch (a) {
    case AMP_ACTION_USE:
    case AMP_ACTION_CANCEL_USE:
    case AMP_ACTION_PAUSE:
    case AMP_ACTION_SCORECARD:
    case AMP_ACTION_SCORECARD_HOLD:
    case AMP_ACTION_SOCIAL_TOGGLE:
    case AMP_ACTION_SCREENSHOT:
    case AMP_ACTION_CONSOLE_TOGGLE:
    case AMP_ACTION_DEBUG_TOGGLE:
    case AMP_ACTION_CHEAT_ENTER:
    case AMP_ACTION_TEXT_PASTE:
    case AMP_ACTION_SKIP_CUTSCENE:
    case AMP_ACTION_MENU_SECONDARY:
    case AMP_ACTION_MENU_TERTIARY:
    case AMP_ACTION_MENU_DELETE:
    case AMP_ACTION_VOICE_PTT:
    case AMP_ACTION_FORGE_PLACE_CANCEL:
    case AMP_ACTION_FORGE_BOT_ADD:
    case AMP_ACTION_FORGE_BOT_REMOVE_ALL:
    case AMP_ACTION_FORGE_BOT_FREEZE_TOGGLE:
    case AMP_ACTION_FORGE_BOT_SPAWN_CYCLE:
    case AMP_ACTION_SKIN_BRUSH_DECREASE:
    case AMP_ACTION_SKIN_BRUSH_INCREASE:
    case AMP_ACTION_SKIN_TOOL_DRAW:
    case AMP_ACTION_SKIN_TOOL_ERASE:
    case AMP_ACTION_SKIN_TOOL_FILL:
    case AMP_ACTION_SKIN_TOOL_EYEDROPPER:
    case AMP_ACTION_SKIN_TOOL_LINE:
    case AMP_ACTION_SKIN_GRID_TOGGLE:
    case AMP_ACTION_SKIN_UV_TOGGLE:
    case AMP_ACTION_SKIN_UNDO:
    case AMP_ACTION_SKIN_REDO:
    case AMP_ACTION_SKIN_SAVE:
        return 0;
    default:
        return 1;
    }
}

static void ampFlushStateSlot(AmpActionState *st)
{
    int wasHeld = st->held;
    st->held    = 0;
    st->pressed = 0;
    st->released      = wasHeld ? 1 : st->released;
    st->value         = 0.0f;
    st->hold_consumed = 0;
}

void ampFlushGameplayState(void)
{
    /* Mirrors actionmapFlushGameplayState: zero gameplay-only state, leave
     * shared/menu/system state intact. Synthesizes a released edge for any
     * action that was held so latched consumers see a release. */
    for (int p = 0; p < AMP_MAX_PLAYERS; p++) {
        for (int a = 0; a < AMP_ACTION_COUNT; a++) {
            if (!ampIsGameplayOnly((AmpInputAction)a)) {
                continue;
            }
            ampFlushStateSlot(&s_State[p][a]);
        }
    }
}

void ampFlushActionSet(const AmpInputAction *actions, int action_count)
{
    if (!actions || action_count <= 0) {
        return;
    }

    for (int p = 0; p < AMP_MAX_PLAYERS; p++) {
        for (int i = 0; i < action_count; i++) {
            AmpInputAction a = actions[i];
            if (a < 0 || a >= AMP_ACTION_COUNT) {
                continue;
            }
            ampFlushStateSlot(&s_State[p][a]);
        }
    }
}

const AmpInputAction *ampCutsceneActionSet(int *out_count)
{
    if (out_count) {
        *out_count = AMP_ARRAYCOUNT(s_CutsceneActionSet);
    }
    return s_CutsceneActionSet;
}
