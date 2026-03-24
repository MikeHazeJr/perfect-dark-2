#pragma once

/**
 * pdgui_scaling.h -- Resolution-independent menu sizing helpers.
 *
 * All ImGui menus use these helpers instead of hardcoded pixel values.
 *
 * Baseline: 720p (1280x720). Scale factor = DisplaySize.y / 720.0f
 *
 * Usage
 * -----
 *   float scale  = pdguiScaleFactor();          // current scale multiplier
 *   float px     = pdguiScale(24.0f);           // 24px at 720p, proportional elsewhere
 *   float mw     = pdguiMenuWidth();            // ultrawide-clamped menu width
 *   float mh     = pdguiMenuHeight();           // 80% of viewport height
 *   ImVec2 mp    = pdguiMenuPos();              // centered pos for mw x mh window
 *   ImVec2 cp    = pdguiCenterPos(w, h);        // centered pos for any w x h window
 *   float fs     = pdguiBaseFontSize();         // 16px at 720p, min 12px
 *
 * Ultrawide clamping
 * ------------------
 * pdguiMenuWidth() = min(DisplaySize.x * 0.70, 1200 * scaleFactor)
 * This prevents menus from stretching edge-to-edge on 21:9 / 32:9 monitors.
 * Players should never need to scan from one edge to the other.
 *
 * Scroll indicators
 * -----------------
 * BeginChild() regions with scrollable content must pass
 * ImGuiWindowFlags_AlwaysVerticalScrollbar when the content can overflow.
 *
 * Part of Sub-Phase D5: UI Scaling Standard.
 */

#include "imgui/imgui.h"

/**
 * Base scale factor relative to 720p.
 * Floor: 0.5 (readability minimum below ~360p).
 */
static inline float pdguiScaleFactor()
{
    float h = ImGui::GetIO().DisplaySize.y;
    float f = (h > 0.0f) ? h / 720.0f : 1.0f;
    if (f < 0.5f) f = 0.5f;
    return f;
}

/**
 * Scale any base pixel value by the current viewport scale.
 * Use for button heights, padding, margins, icon sizes, etc.
 */
static inline float pdguiScale(float base)
{
    return base * pdguiScaleFactor();
}

/**
 * Menu window width: 70% of viewport width, capped at 1200 * scaleFactor.
 * Use for primary content windows (mod manager, match setup, etc.).
 * Ultrawide-safe: prevents edge-to-edge stretch on 21:9 and wider displays.
 */
static inline float pdguiMenuWidth()
{
    ImVec2 disp  = ImGui::GetIO().DisplaySize;
    float sf     = pdguiScaleFactor();
    float byPct  = disp.x * 0.70f;
    float byCap  = 1200.0f * sf;
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
 * Base font size: 16px at 720p, proportional at other resolutions.
 * Floor: 12px for minimum readability.
 * Note: runtime font switching requires font atlas rebuild; use this value
 * to set font scale or for layout sizing rather than direct atlas sizing.
 */
static inline float pdguiBaseFontSize()
{
    float fs = 16.0f * pdguiScaleFactor();
    if (fs < 12.0f) fs = 12.0f;
    return fs;
}
