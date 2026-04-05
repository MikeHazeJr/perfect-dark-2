#ifndef _IN_PDMAIN_H
#define _IN_PDMAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Input ownership boundary (D5.1).
 *
 * INPUTMODE_MENU: ImGui owns all SDL keyboard and mouse events.
 *   - SDL keyboard/mouse routed to ImGui only
 *   - Tab suppressed (eliminates CK_START double-trigger)
 *   - Esc handled by ImGui only (one code path)
 *   - Mouse in absolute mode, cursor visible
 *
 * INPUTMODE_GAMEPLAY: Game input owns everything.
 *   - ImGui does not receive keyboard events (HUD renders but does not consume input)
 *   - Mouse captured (SDL relative mode) if the game requested it
 */
typedef enum {
    INPUTMODE_MENU,     /* ImGui owns SDL events */
    INPUTMODE_GAMEPLAY  /* game input owns, ImGui HUD only */
} InputOwnerMode;

extern InputOwnerMode g_InputMode;

/**
 * Canonical input mode transition function.
 * No-op if already in the requested mode.
 *
 * GAMEPLAY->MENU: SDL_SetRelativeMouseMode(FALSE), cursor shown.
 * MENU->GAMEPLAY: SDL_SetRelativeMouseMode(TRUE), cursor hidden
 *                 only if inputMouseIsLocked() is true (i.e. a mission is running).
 */
void pdmainSetInputMode(InputOwnerMode mode);

/**
 * Returns g_Vars.lvframe60 — the in-stage tick counter (0 on the first frame).
 * Exposed as a C function so C++ port code can read it without including
 * types.h (which #defines bool as s32 and breaks C++ bool).
 */
s32 pdmainGetLvFrame60(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDMAIN_H */
