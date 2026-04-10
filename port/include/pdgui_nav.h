/**
 * pdgui_nav.h -- D-pad wrapping utility and safe area for ImGui menus.
 *
 * M0.2 Phase C: Accept/cancel queries, device detection, and event processing
 * have been removed. Those are now handled by actionmap.h:
 *   - actionPressed(0, ACTION_USE)           replaces pdguiNavAcceptPressed()
 *   - actionPressed(0, ACTION_CANCEL_USE)   replaces pdguiNavCancelPressed()
 *   - actionmapGetLastDevice()              replaces pdguiNavGetLastDevice()
 *   - ACTIONMAP_DEVICE_GAMEPAD              replaces PDNAV_DEVICE_GAMEPAD
 *
 * Only D-pad wrapping and safe area remain in this header.
 *
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#ifndef _IN_PDGUI_NAV_H
#define _IN_PDGUI_NAV_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

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
