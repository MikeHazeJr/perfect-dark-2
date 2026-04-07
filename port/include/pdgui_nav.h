/**
 * pdgui_nav.h -- Gamepad navigation helpers for ImGui menus.
 *
 * Provides D-pad wrapping, centralized accept/cancel queries, and
 * last-input-device tracking. All functions are safe to call from C or C++.
 *
 * Call pdguiNavOnEvent() from the SDL event dispatch path.
 * Call pdguiNavTickWrap() per-frame AFTER all ImGui widgets in a menu.
 * Query pdguiNavAcceptPressed() / pdguiNavCancelPressed() for A/B.
 * Query pdguiNavGetLastDevice() for device-type switching.
 *
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#ifndef _IN_PDGUI_NAV_H
#define _IN_PDGUI_NAV_H

#include <PR/ultratypes.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Device type constants for pdguiNavGetLastDevice().
 */
#define PDNAV_DEVICE_KBM     0   /* Keyboard + mouse */
#define PDNAV_DEVICE_GAMEPAD 1   /* Gamepad / controller */

/**
 * Call from the SDL event dispatch path (pdguiProcessEvent) for every
 * event to track the last-used input device and buffer accept/cancel.
 */
void pdguiNavOnEvent(const SDL_Event *ev);

/**
 * Call once per frame AFTER all ImGui widgets in a wrappable menu.
 * Uses ImGui's NavMoveRequestTryWrapping to loop D-pad navigation.
 *
 * Implementation note: wrapping requires imgui_internal.h (C++), so the
 * actual ImGui call is made through a callback registered by the backend.
 */
void pdguiNavTickWrap(void);

/**
 * Register the C++ function that performs the actual ImGui wrap call.
 * Called once from pdguiInit() in the backend.
 */
void pdguiNavSetWrapCallback(void (*fn)(void));

/**
 * Reset per-frame accept/cancel state. Call at end of frame.
 */
void pdguiNavEndFrame(void);

/**
 * Returns 1 if gamepad A (or Enter/Space on keyboard) was just pressed
 * this frame. For custom menu screens that need explicit accept detection
 * beyond ImGui's built-in nav activate.
 */
s32 pdguiNavAcceptPressed(void);

/**
 * Returns 1 if gamepad B (or Escape on keyboard) was just pressed this
 * frame. For custom cancel/back actions in menu screens.
 */
s32 pdguiNavCancelPressed(void);

/**
 * Returns the last-used input device type:
 *   PDNAV_DEVICE_KBM     (0) = keyboard/mouse
 *   PDNAV_DEVICE_GAMEPAD (1) = gamepad/controller
 *
 * Uses 500ms debounce to prevent flickering during transitions.
 */
s32 pdguiNavGetLastDevice(void);

/**
 * Returns 1 if currently in gamepad mode (shorthand for
 * pdguiNavGetLastDevice() == PDNAV_DEVICE_GAMEPAD).
 */
s32 pdguiNavIsGamepad(void);

/* ========================================================================
 * Safe area — resolution-independent menu positioning
 * ======================================================================== */

/**
 * Safe area rectangle in viewport pixels. All menus should position
 * content within this rect to avoid edge-of-screen issues on ultrawide
 * monitors, TV overscan, etc.
 */
typedef struct {
    float x, y, w, h;
} PdSafeArea;

/**
 * Get the current safe area. Reads viewport size from ImGui and applies
 * aspect-ratio-dependent margins:
 *   - Ultrawide (>2.0 aspect): 10% horizontal, 5% vertical
 *   - Standard: 5% horizontal, 5% vertical
 *
 * Per-edge overrides can be set via pdguiSetSafeAreaMargins().
 * Implementation lives in pdgui_backend.cpp (needs ImGui context).
 */
PdSafeArea pdguiGetSafeArea(void);

/**
 * Override the default safe area margins (in fractions of viewport, 0.0–0.5).
 * Pass -1.0f for any edge to keep the auto-detected default.
 * Values persist via configRegisterFloat() in pd.ini.
 */
void pdguiSetSafeAreaMargins(float top, float bottom, float left, float right);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_NAV_H */
