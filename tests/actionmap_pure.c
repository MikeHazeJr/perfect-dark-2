/*
 * actionmap_pure.c -- pure-C mirror of action-map flush + classification.
 * See actionmap_pure.h header for the contract.
 */

#include "actionmap_pure.h"
#include <string.h>

static AmpActionState s_State[AMP_MAX_PLAYERS][AMP_ACTION_COUNT];

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
    case AMP_ACTION_SCREENSHOT:
    case AMP_ACTION_CONSOLE_TOGGLE:
    case AMP_ACTION_DEBUG_TOGGLE:
    case AMP_ACTION_CHEAT_ENTER:
    case AMP_ACTION_TEXT_PASTE:
        return 0;
    default:
        return 1;
    }
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
            AmpActionState *st = &s_State[p][a];
            int wasHeld = st->held;
            st->held    = 0;
            st->pressed = 0;
            st->released      = wasHeld ? 1 : st->released;
            st->value         = 0.0f;
            st->hold_consumed = 0;
        }
    }
}
