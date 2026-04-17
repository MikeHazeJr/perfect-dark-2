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

#include "imgui/imgui.h"
#include "pdgui_layout.h"
#include "pdgui_scaling.h"
#include "pdgui_style.h"
#include "pdgui_audio.h"

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
     * inherits whatever the parent draws with pdguiDrawPdDialog(). */
    ImGuiWindowFlags f = ImGuiWindowFlags_NoScrollbar
                       | ImGuiWindowFlags_NoScrollWithMouse;

    bool vis = ImGui::BeginChild(id ? id : "##pdgui_action_bar",
                                  childSize, false, f);
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
        ImGui::IsKeyPressed(ImGuiKey_Enter, false);

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

void pdguiPopupDarkenBehind(f32 alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

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

} /* extern "C" */
