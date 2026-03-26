/**
 * menumgr.h -- Unified Menu State Manager
 *
 * Single authority for all menu state in the game. Prevents double-press
 * issues, manages hierarchy, and ensures only one menu has input focus
 * at a time.
 *
 * Architecture:
 *   - Stack-based navigation (push to open, pop to go back)
 *   - One-frame input cooldown after any transition
 *   - Legacy PD menus and ImGui menus both go through this system
 *   - The console overlay (backtick) is independent and always available
 *
 * Usage from C:
 *   menuPush(MENU_PAUSE);     // open pause menu
 *   menuPop();                // close current, return to previous
 *   menuGetCurrent();         // what's active right now
 *   menuIsInCooldown();       // true for 1 frame after any transition
 */

#ifndef MENUMGR_H
#define MENUMGR_H

#include <ultra64.h>

typedef enum {
    MENU_NONE = 0,       /* no menu -- gameplay */
    MENU_MAIN,           /* main menu / title */
    MENU_PAUSE,          /* in-game pause menu */
    MENU_LOBBY,          /* multiplayer lobby */
    MENU_JOIN,           /* join server screen */
    MENU_SETTINGS,       /* settings / options */
    MENU_BOT_CONFIG,     /* bot customizer */
    MENU_ENDGAME,        /* post-match scorecard */
    MENU_PROFILE,        /* player profile view */
    MENU_MODDING,        /* modding hub */
    MENU_MATCHSETUP,     /* match configuration (map, mode, bots) */
    MENU_COUNT
} menu_state_e;

#define MENU_STACK_MAX 8

/**
 * Initialize the menu manager. Call once at startup.
 */
void menuMgrInit(void);

/**
 * Push a menu onto the stack. The new menu becomes active and gets input.
 * Triggers a one-frame input cooldown to prevent double-press.
 * Returns 1 on success, 0 if stack is full.
 */
s32 menuPush(menu_state_e menu);

/**
 * Pop the current menu off the stack, returning to the previous one.
 * If the stack is empty, returns to MENU_NONE (gameplay).
 * Triggers a one-frame input cooldown.
 * Returns the menu that was popped.
 */
menu_state_e menuPop(void);

/**
 * Pop all menus, returning directly to MENU_NONE (gameplay).
 * Triggers a one-frame input cooldown.
 */
void menuPopAll(void);

/**
 * Get the currently active menu (top of stack).
 * Returns MENU_NONE if no menu is open.
 */
menu_state_e menuGetCurrent(void);

/**
 * Check if any menu is currently open.
 */
s32 menuIsOpen(void);

/**
 * Check if a specific menu is anywhere in the stack (not just on top).
 */
s32 menuIsInStack(menu_state_e menu);

/**
 * Check if the menu manager is in its one-frame input cooldown period.
 * When true, all menu-related input should be ignored.
 * Call menuMgrTick() each frame to advance the cooldown.
 */
s32 menuIsInCooldown(void);

/**
 * Advance the cooldown timer. Call once per game frame (tick).
 * This clears the cooldown flag that was set by push/pop operations.
 */
void menuMgrTick(void);

/**
 * Get the name of a menu state (for logging/debug).
 */
const char *menuGetName(menu_state_e menu);

#endif /* MENUMGR_H */
