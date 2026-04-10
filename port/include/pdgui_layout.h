#ifndef _IN_PDGUI_LAYOUT_H
#define _IN_PDGUI_LAYOUT_H

/**
 * pdgui_layout.h -- Docked action bar and popup scrim primitives.
 *
 * Part of Batch 0 (D5 Phase 3 foundation).  Every ImGui menu uses these
 * helpers to enforce a single, authoritative layout contract:
 *
 *   +-------------------------------------------+
 *   | Title                                     |
 *   +-------------------------------------------+
 *   |                                           |
 *   | Scrollable body                           |
 *   | (objectives, briefing, settings list,     |
 *   |  characters grid, ...)                    |
 *   |                                           |
 *   +-------------------------------------------+
 *   | [ Start Mission ]  [ Cancel ]             |   <- docked action bar
 *   +-------------------------------------------+
 *
 * Rule: primary CTAs (Start / Accept / Confirm / Apply / Begin) NEVER
 * live inside the scrollable body.  They always live in the docked
 * action bar, which is a fixed-height child at the bottom of the window.
 * This guarantees the user can see and reach the action at any scroll
 * position, at any resolution, on any aspect ratio.
 *
 * Popup scrim: modal popups (confirmation dialogs, text input, bot
 * settings, etc.) dim the entire viewport behind them so the user
 * visually focuses on the modal.  Before this primitive, every popup
 * rolled its own "GetBackgroundDrawList()->AddRectFilled" -- many of
 * them dimmed the wrong region, wrong alpha, or not at all.
 *
 * Audio: pdguiActionBarButton() plays PDGUI_SND_SELECT on activation
 * so every docked primary action fires the same confirmation cue.
 *
 * Usage (typical menu):
 *
 *   ImGui::Begin("##mywindow", ...);
 *   pdguiDrawPdDialog(...);
 *   ImGui::SetCursorPosY(titleH + padding);
 *
 *   // Body -- scrollable.  Explicitly reserve space for the action bar.
 *   float avail = ImGui::GetContentRegionAvail().y;
 *   float bodyH = pdguiBodyHeightForActionBar(avail);
 *   if (ImGui::BeginChild("##body", ImVec2(0, bodyH), false,
 *                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
 *       // ... scrollable items ...
 *   }
 *   ImGui::EndChild();
 *
 *   // Docked action bar -- always visible.
 *   if (pdguiBeginActionBar("##ab")) {
 *       if (pdguiActionBarButton("Start Mission", startFocus,
 *                                 ImGui::GetContentRegionAvail().x * 0.5f)) {
 *           // launch
 *       }
 *       ImGui::SameLine();
 *       if (pdguiActionBarButton("Cancel", cancelFocus,
 *                                 ImGui::GetContentRegionAvail().x)) {
 *           // pop dialog
 *       }
 *   }
 *   pdguiEndActionBar();
 *   ImGui::End();
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Action bar
 * ======================================================================== */

/**
 * Scaled action-bar height in pixels.  Base 84 @ 1080p (holds a 64px
 * button tier + ~10px vertical breathing above and below), scaled by
 * pdguiScaleFactor().  Floored at 48px so the button remains a valid
 * touch/click target on very small viewports.
 *
 * The bar height is a stable constant: the same value is returned for
 * every call in a given frame on a given viewport size.
 */
f32 pdguiActionBarHeight(void);

/**
 * Compute the scrollable body height for a layout that has a docked
 * action bar.  Pass ImGui::GetContentRegionAvail().y measured after
 * the title (and any fixed header) has been drawn.
 *
 * Returns: avail - actionBarHeight - gap, clamped to a reasonable minimum
 * so degenerate small viewports still have SOME scrollable body.
 *
 * Use this to size BeginChild(..., ImVec2(0, bodyH), ...) for the body
 * scroll region.
 */
f32 pdguiBodyHeightForActionBar(f32 avail);

/**
 * Begin a docked action bar at the current cursor position.  Draws a
 * separator above the bar, then opens an ImGui child region of
 * pdguiActionBarHeight() that does not scroll.
 *
 * Always pair with pdguiEndActionBar().  Returns non-zero if the child
 * is visible (follow ImGui child visibility rules).
 *
 * @param id  Unique child id (e.g., "##ms_actionbar").  May be NULL for
 *            a generic id; prefer uniqueness.
 */
s32 pdguiBeginActionBar(const char *id);

/** End the docked action bar.  Always call after pdguiBeginActionBar(). */
void pdguiEndActionBar(void);

/**
 * Draw a primary action button inside the action bar.
 *
 * Handles consistent sizing (fills bar height), focus ring (via
 * pdguiDrawItemHighlight when isFocused is set), and audio cue
 * (plays PDGUI_SND_SELECT on activation).
 *
 * Returns non-zero when the button is activated by mouse click OR
 * gamepad A / keyboard Enter while focused.
 *
 * @param label     Button text (visible).
 * @param isFocused Non-zero if the controller focus is on this button.
 * @param width     Button width in scaled pixels.  Use
 *                  ImGui::GetContentRegionAvail().x for a full-bar
 *                  single-button layout.
 */
s32 pdguiActionBarButton(const char *label, s32 isFocused, f32 width);

/* ========================================================================
 * Popup scrim (darken background behind modal popups)
 * ======================================================================== */

/**
 * Draw a full-viewport dim rectangle on the BACKGROUND draw list.
 *
 * The dim sits behind every ImGui window but over the game's 3D scene.
 * Call this from inside a popup renderer (or BeginPopupModal body) to
 * guarantee the user visually focuses on the modal.
 *
 * alpha is clamped to [0, 1].  Recommended values:
 *   0.50  = default modal darken
 *   0.65  = emphatic (danger confirmations)
 *   0.35  = subtle (non-blocking overlays like tooltips)
 *
 * Replaces ad-hoc "GetBackgroundDrawList()->AddRectFilled" calls that
 * were scattered across endscreen, pausemenu, solomission, training,
 * and agentcreate.  Central primitive = single place to tune.
 */
void pdguiPopupDarkenBehind(f32 alpha);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_LAYOUT_H */
