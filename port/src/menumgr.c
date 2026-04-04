/**
 * menumgr.c -- Unified Menu State Manager
 *
 * Stack-based menu navigation with real-time input cooldown.
 * Prevents double-press issues and provides a single authority
 * for "which menu is active and should receive input."
 *
 * Cooldown is time-based (100ms), NOT frame-based, so it works
 * correctly at any framerate (30fps, 60fps, 400fps).
 */

#include "menumgr.h"
#include "input.h"
#include "system.h"
#include "pdmain.h"
#include <string.h>
#include <SDL.h>

/* Cooldown duration in milliseconds. 100ms is enough to prevent
 * any double-press from a human, but short enough to feel instant. */
#define MENU_COOLDOWN_MS 100

static menu_state_e s_MenuStack[MENU_STACK_MAX];
static s32 s_StackDepth = 0;
static u32 s_CooldownEndMs = 0; /* SDL_GetTicks timestamp when cooldown expires */

static const char *s_MenuNames[] = {
    "NONE",
    "MAIN",
    "PAUSE",
    "LOBBY",
    "JOIN",
    "SETTINGS",
    "BOT_CONFIG",
    "ENDGAME",
    "PROFILE",
    "MODDING",
    "MATCHSETUP",
};

static void startCooldown(void)
{
    s_CooldownEndMs = SDL_GetTicks() + MENU_COOLDOWN_MS;
}

/* Dump the full stack contents to the log as a single MENU_STACK: line.
 * Called after every push/pop so diagnostics show the exact stack state. */
static void logStack(const char *action)
{
    char buf[256];
    s32 pos = 0;
    s32 i;
    for (i = 0; i < s_StackDepth && pos < (s32)(sizeof(buf) - 2); i++) {
        if (i > 0) {
            buf[pos++] = '>';
        }
        const char *name = menuGetName(s_MenuStack[i]);
        while (*name && pos < (s32)(sizeof(buf) - 2)) {
            buf[pos++] = *name++;
        }
    }
    buf[pos] = '\0';
    if (s_StackDepth == 0) {
        sysLogPrintf(LOG_NOTE, "MENU_STACK: %s [] depth=0", action);
    } else {
        sysLogPrintf(LOG_NOTE, "MENU_STACK: %s [%s] depth=%d top=%s",
            action, buf, s_StackDepth, menuGetName(menuGetCurrent()));
    }
}

/* E.2: Called when the menu stack empties to restore gameplay input ownership. */
static void restoreGameplayMouseCapture(void)
{
    pdmainSetInputMode(INPUTMODE_GAMEPLAY);
}

void menuMgrInit(void)
{
    memset(s_MenuStack, 0, sizeof(s_MenuStack));
    s_StackDepth = 0;
    s_CooldownEndMs = 0;
    sysLogPrintf(LOG_NOTE, "MENU: manager initialized (cooldown=%dms)", MENU_COOLDOWN_MS);
}

s32 menuPush(menu_state_e menu)
{
    if (s_StackDepth >= MENU_STACK_MAX) {
        sysLogPrintf(LOG_WARNING, "MENU_STACK: FULL cannot push %s (max=%d)", menuGetName(menu), MENU_STACK_MAX);
        return 0;
    }

    /* E.1: Reject if this menu is already anywhere in the stack (not just on top).
     * Prevents Esc or rapid input from creating duplicate menu instances. */
    if (menuIsInStack(menu)) {
        sysLogPrintf(LOG_NOTE, "MENU_STACK: REJECTED push %s (already in stack, depth=%d)",
            menuGetName(menu), s_StackDepth);
        return 1;
    }

    s_MenuStack[s_StackDepth] = menu;
    s_StackDepth++;
    startCooldown();

    logStack("push");
    return 1;
}

menu_state_e menuPop(void)
{
    if (s_StackDepth <= 0) {
        return MENU_NONE;
    }

    s_StackDepth--;
    menu_state_e popped = s_MenuStack[s_StackDepth];
    s_MenuStack[s_StackDepth] = MENU_NONE;
    startCooldown();

    logStack("pop");
    sysLogPrintf(LOG_NOTE, "MENU_STACK: popped=%s", menuGetName(popped));

    /* E.2: Restore gameplay mouse capture when returning to MENU_NONE. */
    if (s_StackDepth == 0) {
        restoreGameplayMouseCapture();
    }

    return popped;
}

void menuPopAll(void)
{
    if (s_StackDepth > 0) {
        sysLogPrintf(LOG_NOTE, "MENU_STACK: popAll from %s depth=%d",
            menuGetName(menuGetCurrent()), s_StackDepth);
    }
    s_StackDepth = 0;
    memset(s_MenuStack, 0, sizeof(s_MenuStack));
    startCooldown();

    logStack("popAll");

    /* E.2: Restore gameplay mouse capture now that no menus are on the stack. */
    restoreGameplayMouseCapture();
}

menu_state_e menuGetCurrent(void)
{
    if (s_StackDepth <= 0) {
        return MENU_NONE;
    }
    return s_MenuStack[s_StackDepth - 1];
}

s32 menuIsOpen(void)
{
    return s_StackDepth > 0;
}

s32 menuIsInStack(menu_state_e menu)
{
    for (s32 i = 0; i < s_StackDepth; i++) {
        if (s_MenuStack[i] == menu) {
            return 1;
        }
    }
    return 0;
}

s32 menuIsInCooldown(void)
{
    if (s_CooldownEndMs == 0) return 0;
    return SDL_GetTicks() < s_CooldownEndMs;
}

void menuMgrTick(void)
{
    /* No-op: cooldown is time-based via SDL_GetTicks, not frame-based.
     * This function exists for API compatibility and future per-frame work. */
}

const char *menuGetName(menu_state_e menu)
{
    if (menu >= 0 && menu < MENU_COUNT) {
        return s_MenuNames[menu];
    }
    return "UNKNOWN";
}
