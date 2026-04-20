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

/* ========================================================================
 * Destructive-action confirm modal (S391 M-5/6/Q batch)
 * ======================================================================== */

/** Result returned by pdguiRenderConfirmModal(). */
#define PDGUI_CONFIRM_PENDING  0  /* popup closed, or still awaiting input */
#define PDGUI_CONFIRM_OK       1  /* user confirmed the destructive action */
#define PDGUI_CONFIRM_CANCEL   2  /* user cancelled */

/**
 * Render one frame of a destructive-action confirm modal using the
 * canonical S385 pattern (scrim + PD red frame + force-focus + debounce).
 *
 * Call this every frame from the owning renderer (typically just before
 * ImGui::End() on the parent window).  To open the popup, the caller
 * must:
 *   1. Maintain an s32 *openFrame tracker, initialized to -1.
 *   2. When the destructive action is triggered (button click, Esc, etc.),
 *      call ImGui::OpenPopup(popupId) AND set *openFrame =
 *      (s32)ImGui::GetFrameCount().  Typically also play
 *      PDGUI_SND_OPENDIALOG.
 *
 * The helper handles:
 *   - pdguiPopupDarkenBehind(0.65f) scrim while open.
 *   - BeginPopupModal + PD nine-slice frame with red (palette 2) title.
 *   - Centered, wrapped body text with warning-tinted color.
 *   - Default-focused Cancel + red confirm button sized for 1080p baseline.
 *   - 5-frame SetKeyboardFocusHere(0) force-focus latch on Cancel.
 *   - 3-frame input debounce (rejects Enter/Space/Esc before settle so
 *     the activation that opened the popup cannot bleed through).
 *   - Enter/Space/(A) = Confirm, Esc/(B) = Cancel keyboard shortcuts.
 *   - "[Enter/Space/(A)] Confirm   [Esc/(B)] Cancel" footer hint.
 *   - Plays PDGUI_SND_SELECT on confirm and PDGUI_SND_KBCANCEL on cancel.
 *   - Clears *openFrame to -1 on dismiss.
 *
 * The caller performs the actual destructive action when the return is
 * PDGUI_CONFIRM_OK.
 *
 * @param popupId        ImGui popup id, e.g. "Quit Game?##mainmenu_quit".
 *                       The text before "##" is ignored as an id but the
 *                       title argument supplies the visible title text.
 * @param title          Visible title drawn in the PD frame header.
 * @param body           Body prompt (wrapped, centered, warning-tinted).
 * @param confirmLabel   Label for the red confirm button, e.g. "Quit",
 *                       "Disconnect", "Restart".
 * @param openFrame      Pointer to a caller-owned s32 tracking
 *                       ImGui::GetFrameCount() at open time.  Initialize
 *                       to -1; the helper clears to -1 on dismiss.
 *
 * @return PDGUI_CONFIRM_OK, PDGUI_CONFIRM_CANCEL, or PDGUI_CONFIRM_PENDING.
 */
s32 pdguiRenderConfirmModal(const char *popupId,
                            const char *title,
                            const char *body,
                            const char *confirmLabel,
                            s32 *openFrame);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_LAYOUT_H */
