/**
 * pdgui_nav.c -- D-pad wrapping utility for ImGui menus.
 *
 * M0.2 Phase C: Device detection and event processing live in actionmap.
 * This file keeps menu-action wrappers plus D-pad wrapping via callback.
 *
 * This is C code with extern "C" linkage for C++ callers.
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#include "pdgui_nav.h"
#include <SDL.h>

#define PDGUI_MENU_REPEAT_DELAY_MS 280u
#define PDGUI_MENU_REPEAT_RATE_MS   80u

/* ---- Wrap callback (set by C++ backend) ---- */

static void (*s_WrapCallback)(void) = NULL;
static u32 s_RepeatNextMs[ACTION_COUNT];

static s32 menuActionValid(InputAction action)
{
    return action >= 0 && action < ACTION_COUNT;
}

static s32 menuActionPressed(InputAction action)
{
    if (!menuActionValid(action)) {
        return 0;
    }
    return actionPressed(0, action);
}

static s32 menuActionHeld(InputAction action)
{
    if (!menuActionValid(action)) {
        return 0;
    }
    return actionHeld(0, action);
}

/* ========================================================================
 * D-pad wrapping
 * ======================================================================== */

void pdguiNavSetWrapCallback(void (*fn)(void))
{
    s_WrapCallback = fn;
}

void pdguiNavTickWrap(void)
{
    if (s_WrapCallback) {
        s_WrapCallback();
    }
}

s32 pdguiMenuActionPressed(InputAction action)
{
    return menuActionPressed(action);
}

s32 pdguiMenuActionHeld(InputAction action)
{
    return menuActionHeld(action);
}

s32 pdguiMenuActionRepeat(InputAction action)
{
    u32 start;
    u32 now;
    u32 next;

    if (!menuActionValid(action)) {
        return 0;
    }
    if (actionPressed(0, action)) {
        start = actionHoldPressStartMs(0, action);
        s_RepeatNextMs[action] = (start ? start : SDL_GetTicks()) +
                                 PDGUI_MENU_REPEAT_DELAY_MS;
        return 1;
    }
    if (!actionHeld(0, action)) {
        s_RepeatNextMs[action] = 0;
        return 0;
    }

    now = SDL_GetTicks();
    next = s_RepeatNextMs[action];
    if (next == 0) {
        start = actionHoldPressStartMs(0, action);
        next = (start ? start : now) + PDGUI_MENU_REPEAT_DELAY_MS;
        s_RepeatNextMs[action] = next;
    }

    if ((s32)(now - next) >= 0) {
        s_RepeatNextMs[action] = now + PDGUI_MENU_REPEAT_RATE_MS;
        return 1;
    }
    return 0;
}

s32 pdguiMenuAcceptPressed(void)
{
    return menuActionPressed(ACTION_MENU_ACCEPT);
}

s32 pdguiMenuCancelPressed(void)
{
    return menuActionPressed(ACTION_MENU_CANCEL);
}

s32 pdguiMenuUpPressed(void)
{
    return menuActionPressed(ACTION_MENU_UP);
}

s32 pdguiMenuDownPressed(void)
{
    return menuActionPressed(ACTION_MENU_DOWN);
}

s32 pdguiMenuLeftPressed(void)
{
    return menuActionPressed(ACTION_MENU_LEFT);
}

s32 pdguiMenuRightPressed(void)
{
    return menuActionPressed(ACTION_MENU_RIGHT);
}

s32 pdguiMenuTabPrevPressed(void)
{
    return menuActionPressed(ACTION_MENU_TAB_PREV);
}

s32 pdguiMenuTabNextPressed(void)
{
    return menuActionPressed(ACTION_MENU_TAB_NEXT);
}

s32 pdguiMenuSecondaryPressed(void)
{
    return menuActionPressed(ACTION_MENU_SECONDARY);
}

s32 pdguiMenuTertiaryPressed(void)
{
    return menuActionPressed(ACTION_MENU_TERTIARY);
}

s32 pdguiMenuStartPressed(void)
{
    return menuActionPressed(ACTION_PAUSE);
}

s32 pdguiMenuDeletePressed(void)
{
    return menuActionPressed(ACTION_MENU_DELETE);
}

s32 pdguiMenuSkipUpPressed(void)
{
    return menuActionPressed(ACTION_MENU_SKIPUP);
}

s32 pdguiMenuSkipDownPressed(void)
{
    return menuActionPressed(ACTION_MENU_SKIPDOWN);
}

s32 pdguiMenuUpRepeat(void)
{
    return pdguiMenuActionRepeat(ACTION_MENU_UP);
}

s32 pdguiMenuDownRepeat(void)
{
    return pdguiMenuActionRepeat(ACTION_MENU_DOWN);
}

s32 pdguiMenuLeftRepeat(void)
{
    return pdguiMenuActionRepeat(ACTION_MENU_LEFT);
}

s32 pdguiMenuRightRepeat(void)
{
    return pdguiMenuActionRepeat(ACTION_MENU_RIGHT);
}

s32 pdguiMenuTabPrevRepeat(void)
{
    return pdguiMenuActionRepeat(ACTION_MENU_TAB_PREV);
}

s32 pdguiMenuTabNextRepeat(void)
{
    return pdguiMenuActionRepeat(ACTION_MENU_TAB_NEXT);
}

s32 pdguiTextPastePressed(void)
{
    return menuActionPressed(ACTION_TEXT_PASTE);
}
