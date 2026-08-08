/**
 * pdgui_layout.cpp -- Docked action bar + popup scrim primitives.
 *
 * Implementation for port/include/pdgui_layout.h.  See that header for
 * the layout contract, rationale, and usage.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking
 * C++.  Use int returns for C-ABI booleans and include only ImGui + the
 * layout/style/scaling/audio public headers.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <PR/ultratypes.h>
#include <stdio.h>

#include "imgui/imgui.h"
#include "pdgui_layout.h"
#include "pdgui_scaling.h"
#include "pdgui_style.h"
#include "pdgui_audio.h"
#include "pdgui_nav.h"

/* Action bar metrics (1080p baseline, matching pdgui_scaling.h reference and
 * the d5-full-menu-overhaul.md UI Scaling table).
 *
 * BASE_HEIGHT   = total bar height containing a centered 64px button +
 *                 ~10px vertical breathing on each side.
 * MIN_HEIGHT    = absolute floor in raw pixels (not scaled) so the bar
 *                 remains a valid touch/click target on very small viewports.
 * BUTTON_HEIGHT = 64px at 1080p, matching the "Button height" tier from
 *                 d5-full-menu-overhaul.md.
 * BODY_GAP      = gap between the scroll body and the action bar.
 * BODY_MIN      = minimum scroll-body height before we refuse to shrink
 *                 further on degenerate viewports.
 */
#define PDGUI_AB_BASE_HEIGHT_PX   84.0f
#define PDGUI_AB_MIN_HEIGHT_PX    48.0f
#define PDGUI_AB_BUTTON_HEIGHT_PX 64.0f
#define PDGUI_AB_BODY_GAP_PX      12.0f
#define PDGUI_AB_BODY_MIN_PX      90.0f

/* Per-frame accumulation: multiple modal paths may call pdguiPopupDarkenBehind
 * in one frame (e.g. main menu + CI settings sibling preloaded). Drawing each
 * full-viewport rect stacks alpha and reads as "double dim". We take the max
 * requested alpha and draw a single rect at flush time (before ImGui::Render). */
static f32 s_PopupDarkenMaxAlpha = -1.0f;

extern "C" {

f32 pdguiActionBarHeight(void)
{
    f32 h = pdguiScale(PDGUI_AB_BASE_HEIGHT_PX);
    if (h < PDGUI_AB_MIN_HEIGHT_PX) h = PDGUI_AB_MIN_HEIGHT_PX;
    return h;
}

f32 pdguiBodyHeightForActionBar(f32 avail)
{
    f32 barH = pdguiActionBarHeight();
    f32 gap  = pdguiScale(PDGUI_AB_BODY_GAP_PX);
    f32 body = avail - barH - gap;
    f32 minB = pdguiScale(PDGUI_AB_BODY_MIN_PX);
    if (body < minB) body = minB;
    return body;
}

s32 pdguiBeginActionBar(const char *id)
{
    /* Visible separator between body and bar -- anchors the dock visually. */
    ImGui::Separator();

    f32 barH = pdguiActionBarHeight();
    ImVec2 childSize(0.0f, barH);

    /* NoScrollbar + NoScrollWithMouse so the action bar never itself scrolls;
     * NoMove/NoResize are inherited from the parent window flags.
     * We deliberately omit NoBackground: the parent window background is
     * transparent (NoBackground is set per-window by callers), so the bar
     * inherits whatever the parent draws with pdguiDrawPdDialog().
     *
     * S368: ImGuiChildFlags_NavFlattened -- merges this child's nav scope into
     * the parent's so D-pad / stick navigation crosses from the scroll body
     * into the action bar and back.  Without it the action bar lives in its
     * own nav scope and buttons like "Back", "Save", "Confirm / Cancel"
     * become unreachable by controller once focus is in the body. */
    ImGuiWindowFlags f = ImGuiWindowFlags_NoScrollbar
                       | ImGuiWindowFlags_NoScrollWithMouse;

    bool vis = ImGui::BeginChild(id ? id : "##pdgui_action_bar",
                                  childSize,
                                  ImGuiChildFlags_NavFlattened,
                                  f);
    return vis ? 1 : 0;
}

void pdguiEndActionBar(void)
{
    ImGui::EndChild();
}

s32 pdguiActionBarButton(const char *label, s32 isFocused, f32 width)
{
    if (width < 1.0f) width = 1.0f;

    /* Button height = bar height minus a small vertical breathing pad. */
    f32 barH = pdguiActionBarHeight();
    f32 btnH = pdguiScale(PDGUI_AB_BUTTON_HEIGHT_PX);
    if (btnH > barH - 4.0f) btnH = barH - 4.0f;
    if (btnH < 24.0f)       btnH = 24.0f;

    /* Vertical center the button within the bar. */
    f32 vPad = (barH - btnH) * 0.5f;
    if (vPad > 0.0f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + vPad);
    }

    ImVec2 cp = ImGui::GetCursorScreenPos();

    /* Focus highlight behind the button (palette-derived ring + fill). */
    if (isFocused) {
        pdguiDrawItemHighlight(cp.x, cp.y, width, btnH);
    }

    bool clicked = ImGui::Button(label, ImVec2(width, btnH));

    /* Enter confirms the focused button. */
    bool doActivate = (isFocused != 0) &&
        pdguiMenuAcceptPressed();

    if (clicked || doActivate) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        return 1;
    }

    /* Mouse hover also claims focus so keyboard/gamepad + mouse play nice. */
    if (ImGui::IsItemHovered()) {
        /* Only play hover sound on edge-trigger to avoid spam; ImGui's
         * IsItemHovered fires every frame. The caller owns focus state
         * and can decide whether to play SND_FOCUS on change. */
    }

    return 0;
}

f32 pdguiHintFooterHeight(const char *text, f32 width)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    f32 wrapW = width - style.WindowPadding.x * 2.0f;
    if (wrapW < 1.0f) wrapW = 1.0f;

    const char *safeText = text ? text : "";
    ImVec2 textSize = ImGui::CalcTextSize(safeText, NULL, false, wrapW);

    /* BeginChild uses AlwaysUseWindowPadding below.  Separator advances by
     * one physical pixel plus ItemSpacing.y; use the live font measurement
     * rather than pdguiScale constants so custom vector/bitmap fonts and the
     * user's UI scale cannot push the last baseline outside the panel. */
    return style.WindowPadding.y * 2.0f
         + style.ItemSpacing.y
         + 1.0f
         + textSize.y;
}

void pdguiDrawHintFooter(const char *id, const char *text, f32 height)
{
    if (height <= 0.0f) return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoScrollWithMouse;
    bool visible = ImGui::BeginChild(id ? id : "##pdgui_hint_footer",
                                     ImVec2(0.0f, height),
                                     ImGuiChildFlags_AlwaysUseWindowPadding,
                                     flags);
    if (visible) {
        ImGui::Separator();
        f32 wrapX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::PushTextWrapPos(wrapX);
        ImGui::TextDisabled("%s", text ? text : "");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

void pdguiPopupDarkenBeginFrame(void)
{
    s_PopupDarkenMaxAlpha = -1.0f;
}

void pdguiPopupDarkenBehind(f32 alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    if (s_PopupDarkenMaxAlpha < 0.0f || alpha > s_PopupDarkenMaxAlpha) {
        s_PopupDarkenMaxAlpha = alpha;
    }
}

void pdguiPopupDarkenFlush(void)
{
    if (s_PopupDarkenMaxAlpha < 0.0f) {
        return;
    }

    f32 alpha = s_PopupDarkenMaxAlpha;
    s_PopupDarkenMaxAlpha = -1.0f;

    ImVec2 disp = ImGui::GetIO().DisplaySize;
    int ai = (int)(alpha * 255.0f);
    if (ai < 0)   ai = 0;
    if (ai > 255) ai = 255;

    ImU32 dimCol = IM_COL32(0, 0, 0, ai);

    /* Background draw list = behind every ImGui window, on top of the
     * game 3D scene.  Single rect spanning the whole viewport. */
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), disp, dimCol);
}

/* ========================================================================
 * Destructive-action confirm modal (S391 M-5/6/Q batch)
 *
 * Canonical S385 pattern: scrim + PD red frame + 5-frame force-focus on
 * Cancel + 3-frame input debounce + red confirm button + footer hint.
 * Callers maintain the s_*OpenFrame tracker and call ImGui::OpenPopup /
 * set the tracker to GetFrameCount() at the site where the destructive
 * action is triggered (button click, Esc on screen, etc.).
 * ======================================================================== */

#define PDGUI_CONFIRM_FRAME_DEBOUNCE       3
#define PDGUI_CONFIRM_FORCE_FOCUS_FRAMES   5

s32 pdguiRenderConfirmModal(const char *popupId,
                            const char *title,
                            const char *body,
                            const char *confirmLabel,
                            s32 *openFrame)
{
    if (!popupId || !title || !body || !confirmLabel || !openFrame) {
        return PDGUI_CONFIRM_PENDING;
    }

    /* No popup requested — keep tracker in -1 and return PENDING so the
     * caller can cheaply call us every frame without branching. */
    if (!ImGui::IsPopupOpen(popupId)) {
        *openFrame = -1;
        return PDGUI_CONFIRM_PENDING;
    }

    /* Scrim behind every ImGui window, over the game 3D scene. */
    pdguiPopupDarkenBehind(0.65f);

    s32 prevPalette = pdguiGetPalette();

    f32 modalW = pdguiScale(560.0f);
    f32 modalH = pdguiScale(240.0f);
    ImVec2 mPos = pdguiCenterPos(modalW, modalH);

    ImGui::SetNextWindowPos(mPos);
    ImGui::SetNextWindowSize(ImVec2(modalW, modalH));

    ImGuiWindowFlags mflags = ImGuiWindowFlags_NoResize
                             | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoCollapse
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoBackground
                             | ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::BeginPopupModal(popupId, nullptr, mflags)) {
        /* Popup was dismissed externally (hotswap, stage change, etc.). */
        *openFrame = -1;
        return PDGUI_CONFIRM_PENDING;
    }

    /* Red / warning palette for the PD frame + title. */
    pdguiSetPalette(2);

    f32 mx = ImGui::GetWindowPos().x;
    f32 my = ImGui::GetWindowPos().y;
    f32 titleH = pdguiScale(36.0f);
    if (titleH < 18.0f) titleH = 18.0f;

    s32 curFrame = (s32)ImGui::GetFrameCount();
    s32 framesOpen = (*openFrame >= 0)
                     ? (curFrame - *openFrame)
                     : PDGUI_CONFIRM_FORCE_FOCUS_FRAMES + 1;
    bool forceFocus = (framesOpen >= 0 &&
                        framesOpen < PDGUI_CONFIRM_FORCE_FOCUS_FRAMES);
    bool inputDebounced = (framesOpen >= 0 &&
                            framesOpen < PDGUI_CONFIRM_FRAME_DEBOUNCE);

    /* Opaque PD-authentic backdrop + frame + title text. */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(mx, my),
                          ImVec2(mx + modalW, my + modalH),
                          pdguiPalImU32(PDPAL_BODYBG, 255));
    }
    pdguiDrawPdDialog(mx, my, modalW, modalH, title, 1);
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(mx + 8.0f, my + 2.0f,
                          modalW - 16.0f, titleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(mx + (modalW - ts.x) * 0.5f,
                           my + (titleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 0, 255), title);
    }

    pdguiSetCursorBelowTitle(titleH);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pdguiScale(8.0f));

    f32 availW = modalW - ImGui::GetStyle().WindowPadding.x * 2.0f;

    /* Body text — centered, wrapped, warning-tinted. */
    {
        ImVec2 bts = ImGui::CalcTextSize(body, nullptr, false, availW);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - bts.x) * 0.5f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availW);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(1.0f, 0.85f, 0.85f, 1.0f));
        ImGui::TextWrapped("%s", body);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* Buttons. */
    f32 btnW = pdguiScale(160.0f);
    f32 btnH = pdguiScale(32.0f);
    f32 gap  = pdguiScale(16.0f);
    f32 totalW = btnW * 2.0f + gap;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - totalW) * 0.5f);

    bool doConfirm = false;
    bool doCancel  = false;

    /* 5-frame force-focus latch on Cancel so controller D-pad starts on
     * the safe default.  SetItemDefaultFocus alone races with popup
     * NavInit; SetKeyboardFocusHere(0) before the next-submitted item
     * bypasses that race. */
    if (forceFocus) {
        ImGui::SetKeyboardFocusHere(0);
    }

    if (ImGui::Button("Cancel##pdgui_confirm_cancel", ImVec2(btnW, btnH))) {
        if (!inputDebounced) doCancel = true;
    }
    ImGui::SetItemDefaultFocus();

    ImGui::SameLine(0.0f, gap);

    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.00f, 0.20f, 0.20f, 1.0f));
    char confirmId[128];
    snprintf(confirmId, sizeof(confirmId), "%s##pdgui_confirm_ok",
             confirmLabel);
    if (ImGui::Button(confirmId, ImVec2(btnW, btnH))) {
        if (!inputDebounced) doConfirm = true;
    }
    ImGui::PopStyleColor(3);

    /* Footer hints. */
    {
        const char *hintL = "[Enter/Space/(A)] Confirm";
        const char *hintR = "[Esc/(B)] Cancel";
        f32 hintY = modalH - pdguiScale(22.0f);
        if (hintY < ImGui::GetCursorPosY() + pdguiScale(4.0f)) {
            hintY = ImGui::GetCursorPosY() + pdguiScale(4.0f);
        }
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x,
                                    hintY));
        ImGui::TextDisabled("%s", hintL);
        ImVec2 rSize = ImGui::CalcTextSize(hintR);
        ImGui::SetCursorPos(ImVec2(modalW - ImGui::GetStyle().WindowPadding.x
                                       - rSize.x,
                                    hintY));
        ImGui::TextDisabled("%s", hintR);
    }

    /* Keyboard / gamepad shortcuts.  Debounced for FRAME_DEBOUNCE frames
     * so the Enter press that opened the popup cannot bleed through. */
    if (!inputDebounced) {
        if (pdguiMenuAcceptPressed()) {
            doConfirm = true;
        }
        if (pdguiMenuCancelPressed()) {
            doCancel = true;
        }
    }

    s32 result = PDGUI_CONFIRM_PENDING;
    if (doConfirm) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        ImGui::CloseCurrentPopup();
        *openFrame = -1;
        result = PDGUI_CONFIRM_OK;
    } else if (doCancel) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        ImGui::CloseCurrentPopup();
        *openFrame = -1;
        result = PDGUI_CONFIRM_CANCEL;
    }

    pdguiSetPalette(prevPalette);
    ImGui::EndPopup();
    return result;
}

} /* extern "C" */
