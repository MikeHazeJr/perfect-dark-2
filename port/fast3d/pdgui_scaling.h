#pragma once

/**
 * pdgui_scaling.h -- Resolution-independent menu sizing helpers.
 *
 * All ImGui menus use these helpers instead of hardcoded pixel values.
 *
 * Baseline: **1080p (1920x1080)**.  Scale factor = (DisplaySize.y / 1080.0f)
 *  * uiScaleMult.  uiScaleMult is user-configurable (0.5-2.0, default 1.0)
 * via Video settings.
 *
 * The 1080p baseline matches the authoritative design reference in
 * context/designs/d5-full-menu-overhaul.md (Menu UX Guidelines > UI Scaling).
 * Every pixel constant written in menu code should be the value that should
 * appear at 1920x1080, and the scaling helpers produce the correct value at
 * other resolutions.  See also context/designs/scaling-baseline-1080.md for
 * the S193 migration note.
 *
 * Reference table (from d5-full-menu-overhaul.md, 1080p column):
 *   Menu heading           36 px
 *   Body text / items      24 px
 *   Small UI labels        16 px
 *   Button height          64 px
 *   Min focus/click target 48 px
 *   Window padding         16 px
 *   Item spacing            8 px
 *   Scrollbar width        14 px
 *
 * Usage
 * -----
 *   float scale  = pdguiScaleFactor();          // current scale multiplier
 *   float px     = pdguiScale(24.0f);           // 24px at 1080p, proportional elsewhere
 *   float mw     = pdguiMenuWidth();            // ultrawide-clamped menu width
 *   float mh     = pdguiMenuHeight();           // 80% of viewport height
 *   ImVec2 mp    = pdguiMenuPos();              // centered pos for mw x mh window
 *   ImVec2 cp    = pdguiCenterPos(w, h);        // centered pos for any w x h window
 *   float fs     = pdguiBaseFontSize();         // 24px at 1080p (body text), min 12px
 *
 * Ultrawide clamping
 * ------------------
 * pdguiMenuWidth() = min(DisplaySize.x * 0.70, 1800 * scaleFactor)
 * This prevents menus from stretching edge-to-edge on 21:9 / 32:9 monitors.
 * Players should never need to scan from one edge to the other.
 *
 * Scroll indicators
 * -----------------
 * BeginChild() regions with scrollable content must pass
 * ImGuiWindowFlags_AlwaysVerticalScrollbar when the content can overflow.
 *
 * Part of Sub-Phase D5: UI Scaling Standard.  Baseline flipped from 720p
 * to 1080p in S193 (2026-04-10).
 */

#include "imgui/imgui.h"

/* Forward declaration so pdguiScaleFactor() can apply the user multiplier
 * without pulling in all of video.h (which uses C types incompatible with
 * some C++ translation units). */
extern "C" float videoGetUiScaleMult(void);

/* Reference resolution (1080p).  Every unscaled pixel constant in menu code
 * should be the value that renders at exactly this resolution. */
#define PDGUI_REF_WIDTH   1920.0f
#define PDGUI_REF_HEIGHT  1080.0f

/**
 * Base scale factor relative to 1080p, multiplied by the user's UI scale setting.
 * Auto-scale floor: 0.5 (readability minimum below ~540p).
 * Combined result floor: 0.25 (allows user to shrink UI on very small displays).
 */
static inline float pdguiScaleFactor()
{
    float h = ImGui::GetIO().DisplaySize.y;
    float f = (h > 0.0f) ? h / PDGUI_REF_HEIGHT : 1.0f;
    if (f < 0.5f) f = 0.5f;
    f *= videoGetUiScaleMult();
    if (f < 0.25f) f = 0.25f;
    return f;
}

/**
 * Scale any base pixel value by the current viewport scale.
 * Use for button heights, padding, margins, icon sizes, etc.
 *
 * The base value is what you want at 1080p.  For a 64px button:
 *   float btnH = pdguiScale(64.0f);  // 64 at 1080p, 85 at 1440p, 128 at 4K
 */
static inline float pdguiScale(float base)
{
    return base * pdguiScaleFactor();
}

/**
 * Menu window width: 70% of viewport width, capped at 1800 * scaleFactor.
 * Use for primary content windows (mod manager, match setup, etc.).
 * Ultrawide-safe: prevents edge-to-edge stretch on 21:9 and wider displays.
 *
 * Cap rationale: 1800px at 1080p == 93.75% of a 1920px viewport.  At other
 * resolutions the cap scales with scaleFactor, preserving the same visual
 * proportion.  On ultrawide, the 0.70 * display.x term takes over and
 * clamps the menu to 70% of the viewport width.
 */
static inline float pdguiMenuWidth()
{
    ImVec2 disp  = ImGui::GetIO().DisplaySize;
    float sf     = pdguiScaleFactor();
    float byPct  = disp.x * 0.70f;
    float byCap  = 1800.0f * sf;
    return (byPct < byCap) ? byPct : byCap;
}

/**
 * Menu window height: 80% of viewport height.
 * Use alongside pdguiMenuWidth() for primary content windows.
 */
static inline float pdguiMenuHeight()
{
    return ImGui::GetIO().DisplaySize.y * 0.80f;
}

/**
 * Centered screen position for a window of pdguiMenuWidth() x pdguiMenuHeight().
 * Pass directly to ImGui::SetNextWindowPos().
 */
static inline ImVec2 pdguiMenuPos()
{
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    return ImVec2(
        (disp.x - pdguiMenuWidth())  * 0.5f,
        (disp.y - pdguiMenuHeight()) * 0.5f
    );
}

/**
 * Centered screen position for an arbitrary window size w x h.
 * Use when a menu has its own width/height (e.g. smaller dialogs).
 */
static inline ImVec2 pdguiCenterPos(float w, float h)
{
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    return ImVec2((disp.x - w) * 0.5f, (disp.y - h) * 0.5f);
}

/**
 * Base font size: 24px at 1080p (body text tier from d5-full-menu-overhaul.md),
 * proportional at other resolutions.
 *
 * Floor: 12px for minimum readability.
 *
 * Note: runtime font switching requires font atlas rebuild; use this value
 * to set font scale or for layout sizing rather than direct atlas sizing.
 *
 * For explicit font tiers matching the d5 spec, prefer pdguiScale(36.0f) for
 * headings, pdguiScale(24.0f) for body, pdguiScale(16.0f) for small labels.
 */
static inline float pdguiBaseFontSize()
{
    float fs = 24.0f * pdguiScaleFactor();
    if (fs < 12.0f) fs = 12.0f;
    return fs;
}
