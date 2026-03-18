/**
 * pdgui_style.cpp — Perfect Dark-authentic ImGui styling.
 *
 * Recreates PD's original menu appearance procedurally using ImGui's custom
 * draw API.  The original game uses GBI primitives (fill rectangles, vertex-
 * colored triangles, shimmer overlays) — all of which map cleanly to ImGui's
 * draw list.
 *
 * Visual elements recreated:
 *  - Title bar: 3-color vertical gradient (dark → mid → bright blue)
 *  - Body:      Dark blue translucent fill (0x00002f @ ~62% alpha)
 *  - Borders:   Bright blue / cyan 1px lines on left, right, bottom
 *  - Shimmer:   Animated white comet traveling along border edges
 *  - Text:      White with shadow offset (+1,+1 dark behind main text)
 *  - Items:     Cyan text unfocused, white when focused/hovered
 *
 * Color values taken directly from g_MenuColours[1] (blue palette) in menu.c.
 * PD's color format is 0xRRGGBBAA — converted to ImGui's ImVec4(r,g,b,a).
 *
 * Part of Sub-Phase D3.4: Menu System Modernization.
 */

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

/* -----------------------------------------------------------------------
 * PD Blue Palette — from g_MenuColours[1] in src/game/menu.c
 *
 * Original format: 0xRRGGBBAA
 *   dialog_border1:     0x0060bf7f  →  R=0, G=96, B=191, A=127
 *   dialog_titlebg:     0x0000507f  →  R=0, G=0, B=80, A=127
 *   dialog_border2:     0x00f0ff7f  →  R=0, G=240, B=255, A=127
 *   dialog_titlefg:     0xffffffff  →  white
 *   dialog_bodybg:      0x00002f7f  →  R=0, G=0, B=47, A=127
 *   item_unfocused:     0x00ffffff  →  R=0, G=255, B=255, A=255 (cyan)
 *   item_disabled:      0x007f7fff  →  R=0, G=127, B=127, A=255
 *   item_focused_inner: 0xffffffff  →  white
 *   item_focused_outer: 0x000044ff  →  R=0, G=0, B=68, A=255
 *   checkbox_checked:   0x8fffffff  →  light cyan
 * ----------------------------------------------------------------------- */

/* Helper: PD 0xRRGGBBAA → ImU32 (ImGui's packed ABGR) */
static inline ImU32 PdColor(unsigned int rgba)
{
    unsigned char r = (rgba >> 24) & 0xFF;
    unsigned char g = (rgba >> 16) & 0xFF;
    unsigned char b = (rgba >>  8) & 0xFF;
    unsigned char a = (rgba >>  0) & 0xFF;
    return IM_COL32(r, g, b, a);
}

/* Helper: PD color with alpha override */
static inline ImU32 PdColorA(unsigned int rgba, unsigned char alpha)
{
    unsigned char r = (rgba >> 24) & 0xFF;
    unsigned char g = (rgba >> 16) & 0xFF;
    unsigned char b = (rgba >>  8) & 0xFF;
    return IM_COL32(r, g, b, alpha);
}

/* Helper: lerp between two ImU32 colors */
static inline ImU32 LerpColor(ImU32 c1, ImU32 c2, float t)
{
    int r1 = (c1 >> IM_COL32_R_SHIFT) & 0xFF;
    int g1 = (c1 >> IM_COL32_G_SHIFT) & 0xFF;
    int b1 = (c1 >> IM_COL32_B_SHIFT) & 0xFF;
    int a1 = (c1 >> IM_COL32_A_SHIFT) & 0xFF;
    int r2 = (c2 >> IM_COL32_R_SHIFT) & 0xFF;
    int g2 = (c2 >> IM_COL32_G_SHIFT) & 0xFF;
    int b2 = (c2 >> IM_COL32_B_SHIFT) & 0xFF;
    int a2 = (c2 >> IM_COL32_A_SHIFT) & 0xFF;
    return IM_COL32(
        r1 + (int)((r2 - r1) * t),
        g1 + (int)((g2 - g1) * t),
        b1 + (int)((b2 - b1) * t),
        a1 + (int)((a2 - a1) * t)
    );
}

/* PD palette constants (ImU32 packed) */
static const ImU32 PD_BORDER1     = PdColor(0x0060bf7f);  /* bright blue border */
static const ImU32 PD_BORDER2     = PdColor(0x00f0ff7f);  /* cyan accent border */
static const ImU32 PD_TITLEBG     = PdColor(0x0000507f);  /* dark blue title bg */
static const ImU32 PD_TITLEFG     = PdColor(0xffffffff);  /* white title text */
static const ImU32 PD_BODYBG      = PdColor(0x00002f9f);  /* dark blue body */
static const ImU32 PD_ITEM_NORMAL = PdColor(0x00ffffff);  /* cyan item text */
static const ImU32 PD_ITEM_DIM    = PdColor(0x007f7fff);  /* dimmed item */
static const ImU32 PD_FOCUS_INNER = PdColor(0xffffffff);  /* white focused */
static const ImU32 PD_FOCUS_OUTER = PdColor(0x000044ff);  /* dark blue focus bg */
static const ImU32 PD_SHIMMER     = IM_COL32(255, 255, 255, 0);  /* shimmer base */

/* Shimmer animation speed — PD uses g_20SecIntervalFrac cycling 0..1 over 20s.
 * We approximate with ImGui's time. */
static float pdguiGetShimmerPhase(void)
{
    float t = (float)ImGui::GetTime();
    /* 20-second cycle, same as PD */
    t = t / 20.0f;
    t = t - (float)(int)t;  /* frac */
    return t;
}

/* -----------------------------------------------------------------------
 * Shimmer effect — animated white highlight traveling along an edge
 *
 * PD's menugfxDrawShimmer() renders a gradient quad that slides along
 * border edges.  We recreate it with ImGui's AddRectFilledMultiColor.
 * ----------------------------------------------------------------------- */

static void pdguiDrawShimmerH(ImDrawList *dl, float x1, float y1, float x2, float y2,
                               float phase, int shimmerWidth, bool reverse)
{
    float length = x2 - x1;
    if (length <= 0.0f) return;

    /* Wrap shimmer position along total travel (600 px cycle in PD) */
    float travel = 600.0f;
    float pos;
    if (reverse) {
        pos = phase * travel;
    } else {
        pos = (1.0f - phase) * travel;
    }
    /* Offset by edge position for per-edge variation (like PD) */
    pos += (float)((int)(y1 + x1) % (int)travel);
    pos = pos - (float)((int)(pos / travel)) * travel;

    float left  = x1 + pos - shimmerWidth;
    float right = left + shimmerWidth;

    /* Clamp to edge bounds */
    if (left < x1) left = x1;
    if (right > x2) right = x2;
    if (left >= right) return;

    /* Alpha fades at edges */
    float fadeL = (left - (x1 + pos - shimmerWidth)) / (float)shimmerWidth;
    float fadeR = ((x1 + pos) - right) / (float)shimmerWidth;
    float fade = fadeL > fadeR ? fadeL : fadeR;
    if (fade < 0.0f) fade = 0.0f;
    if (fade > 1.0f) fade = 1.0f;

    unsigned char peakAlpha = (unsigned char)(180 * (1.0f - fade));
    ImU32 bright = IM_COL32(255, 255, 255, peakAlpha);
    ImU32 dim    = IM_COL32(255, 255, 255, 0);

    if (reverse) {
        dl->AddRectFilledMultiColor(ImVec2(left, y1), ImVec2(right, y2),
                                     dim, bright, bright, dim);
    } else {
        dl->AddRectFilledMultiColor(ImVec2(left, y1), ImVec2(right, y2),
                                     bright, dim, dim, bright);
    }
}

static void pdguiDrawShimmerV(ImDrawList *dl, float x1, float y1, float x2, float y2,
                               float phase, int shimmerWidth, bool reverse)
{
    float length = y2 - y1;
    if (length <= 0.0f) return;

    float travel = 600.0f;
    float pos;
    if (reverse) {
        pos = phase * travel;
    } else {
        pos = (1.0f - phase) * travel;
    }
    pos += (float)((int)(y1 + x1) % (int)travel);
    pos = pos - (float)((int)(pos / travel)) * travel;

    float top    = y1 + pos - shimmerWidth;
    float bottom = top + shimmerWidth;

    if (top < y1) top = y1;
    if (bottom > y2) bottom = y2;
    if (top >= bottom) return;

    float fadeT = (top - (y1 + pos - shimmerWidth)) / (float)shimmerWidth;
    float fadeB = ((y1 + pos) - bottom) / (float)shimmerWidth;
    float fade = fadeT > fadeB ? fadeT : fadeB;
    if (fade < 0.0f) fade = 0.0f;
    if (fade > 1.0f) fade = 1.0f;

    unsigned char peakAlpha = (unsigned char)(180 * (1.0f - fade));
    ImU32 bright = IM_COL32(255, 255, 255, peakAlpha);
    ImU32 dim    = IM_COL32(255, 255, 255, 0);

    if (reverse) {
        dl->AddRectFilledMultiColor(ImVec2(x1, top), ImVec2(x2, bottom),
                                     dim, dim, bright, bright);
    } else {
        dl->AddRectFilledMultiColor(ImVec2(x1, top), ImVec2(x2, bottom),
                                     bright, bright, dim, dim);
    }
}

/* -----------------------------------------------------------------------
 * PD Dialog Renderer — draws a complete PD-style dialog frame
 *
 * Layout (matches original menu.c dialogRender):
 *   ┌──────────────────────────┐  ← title gradient (11px high in PD)
 *   │  Title Text              │     3-color vertical gradient
 *   ├──────────────────────────┤  ← border line with shimmer
 *   │                          │
 *   │  Body content            │  ← dark blue translucent fill
 *   │                          │
 *   └──────────────────────────┘  ← bottom border with shimmer
 *     ↑ left border              ↑ right border
 * ----------------------------------------------------------------------- */

extern "C" void pdguiDrawPdDialog(float x, float y, float w, float h,
                                   const char *title, int focused)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float phase = pdguiGetShimmerPhase();

    /* Title bar height — PD uses LINEHEIGHT which is 11 at 240p.
     * Scale proportionally: ~4.6% of dialog height, minimum 20px */
    float titleH = h * 0.08f;
    if (titleH < 20.0f) titleH = 20.0f;
    if (titleH > 32.0f) titleH = 32.0f;

    /* === Title bar gradient ===
     * PD uses menugfxRenderGradient with 3 colors:
     *   top = dialog_titlebg (dark blue)
     *   mid = dialog_border1 (bright blue)
     *   bottom = dialog_titlebg (dark blue)
     * Rendered as two halves. */
    ImU32 titleTop    = PD_TITLEBG;
    ImU32 titleMid    = focused ? PD_BORDER1 : PdColor(0x003060af);
    ImU32 titleBottom = PD_TITLEBG;

    float titleMidY = y + titleH * 0.5f;

    /* Top half of gradient */
    dl->AddRectFilledMultiColor(
        ImVec2(x - 2, y), ImVec2(x + w + 2, titleMidY),
        titleTop, titleTop, titleMid, titleMid);
    /* Bottom half of gradient */
    dl->AddRectFilledMultiColor(
        ImVec2(x - 2, titleMidY), ImVec2(x + w + 2, y + titleH),
        titleMid, titleMid, titleBottom, titleBottom);

    /* Title bar shimmer — top edge and bottom edge, like PD */
    pdguiDrawShimmerH(dl, x - 2, y, x + w + 2, y + 1, phase, 40, false);
    pdguiDrawShimmerH(dl, x - 2, y + titleH - 1, x + w + 2, y + titleH, phase, 40, true);

    /* === Body background ===
     * PD: menugfxRenderDialogBackground with dialog_bodybg color
     * Inset by 1px from borders */
    dl->AddRectFilled(
        ImVec2(x + 1, y + titleH),
        ImVec2(x + w - 1, y + h),
        PD_BODYBG);

    /* === Border lines with shimmer ===
     * PD draws: left (1px), right (1px), bottom (1px) borders
     * Each border is a colored line + shimmer overlay */

    /* Left border */
    dl->AddRectFilled(ImVec2(x, y + titleH), ImVec2(x + 1, y + h), PD_BORDER1);
    pdguiDrawShimmerV(dl, x, y + titleH, x + 1, y + h, phase, 10, false);

    /* Right border */
    dl->AddRectFilled(ImVec2(x + w - 1, y + titleH), ImVec2(x + w, y + h), PD_BORDER2);
    pdguiDrawShimmerV(dl, x + w - 1, y + titleH, x + w, y + h, phase, 10, true);

    /* Bottom border */
    dl->AddRectFilled(ImVec2(x, y + h - 1), ImVec2(x + w, y + h), PD_BORDER1);
    pdguiDrawShimmerH(dl, x, y + h - 1, x + w, y + h, phase, 10, false);
}

/* -----------------------------------------------------------------------
 * PD-style focus highlight for items
 * PD draws focused items with a dark blue background and white text
 * ----------------------------------------------------------------------- */

extern "C" void pdguiDrawItemHighlight(float x, float y, float w, float h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), PD_FOCUS_OUTER);
}

/* -----------------------------------------------------------------------
 * Apply PD-authentic ImGui style
 *
 * Sets ImGui colors and metrics to match PD's menu appearance as closely
 * as possible through the standard style system.  For elements that need
 * full custom rendering (window backgrounds, title bars, shimmer), use
 * pdguiDrawPdDialog() via window draw callbacks.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiApplyPdStyle(void)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    /* --- Colors from PD blue palette --- */

    /* Window/frame backgrounds — very dark blue, translucent */
    colors[ImGuiCol_WindowBg]           = ImVec4(0.00f, 0.00f, 0.18f, 0.62f);
    colors[ImGuiCol_ChildBg]            = ImVec4(0.00f, 0.00f, 0.12f, 0.40f);
    colors[ImGuiCol_PopupBg]            = ImVec4(0.00f, 0.00f, 0.18f, 0.85f);

    /* Borders — bright blue (PD_BORDER1: 0x0060bf7f) */
    colors[ImGuiCol_Border]             = ImVec4(0.00f, 0.376f, 0.749f, 0.50f);
    colors[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    /* Title bar gradient center (PD_TITLEBG: 0x0000507f mixed with border1) */
    colors[ImGuiCol_TitleBg]            = ImVec4(0.00f, 0.00f, 0.31f, 0.50f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.00f, 0.188f, 0.50f, 0.60f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.00f, 0.00f, 0.20f, 0.40f);

    /* Frame backgrounds (input fields, checkboxes) — darkened body */
    colors[ImGuiCol_FrameBg]            = ImVec4(0.00f, 0.00f, 0.27f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.00f, 0.20f, 0.50f, 0.40f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.00f, 0.30f, 0.60f, 0.67f);

    /* Text — PD uses white for title, cyan for items */
    colors[ImGuiCol_Text]              = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);  /* cyan */
    colors[ImGuiCol_TextDisabled]      = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);  /* dim cyan */
    colors[ImGuiCol_TextSelectedBg]    = ImVec4(0.00f, 0.27f, 0.55f, 0.35f);

    /* Buttons — PD blue tones */
    colors[ImGuiCol_Button]             = ImVec4(0.00f, 0.15f, 0.40f, 0.50f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.00f, 0.376f, 0.749f, 0.70f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.00f, 0.50f, 1.00f, 0.70f);

    /* Check marks and sliders — bright cyan (PD_BORDER2: 0x00f0ff7f) */
    colors[ImGuiCol_CheckMark]          = ImVec4(0.00f, 0.94f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.00f, 0.376f, 0.749f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.00f, 0.94f, 1.00f, 1.00f);

    /* Headers (collapsing headers, selectable rows) */
    colors[ImGuiCol_Header]             = ImVec4(0.00f, 0.00f, 0.27f, 0.60f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.00f, 0.20f, 0.50f, 0.80f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.00f, 0.376f, 0.749f, 1.00f);

    /* Separator — border1 color */
    colors[ImGuiCol_Separator]          = ImVec4(0.00f, 0.376f, 0.749f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]   = ImVec4(0.00f, 0.94f, 1.00f, 0.78f);
    colors[ImGuiCol_SeparatorActive]    = ImVec4(0.00f, 0.94f, 1.00f, 1.00f);

    /* Tabs */
    colors[ImGuiCol_Tab]                = ImVec4(0.00f, 0.10f, 0.30f, 0.86f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.00f, 0.376f, 0.749f, 0.80f);
    colors[ImGuiCol_TabActive]          = ImVec4(0.00f, 0.25f, 0.55f, 1.00f);

    /* Scrollbar */
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.00f, 0.00f, 0.12f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.00f, 0.376f, 0.749f, 0.50f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 0.50f, 0.80f, 0.70f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.00f, 0.94f, 1.00f, 1.00f);

    /* Resize grip */
    colors[ImGuiCol_ResizeGrip]         = ImVec4(0.00f, 0.376f, 0.749f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]  = ImVec4(0.00f, 0.94f, 1.00f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]   = ImVec4(0.00f, 0.94f, 1.00f, 0.95f);

    /* Nav highlight — cyan */
    colors[ImGuiCol_NavHighlight]       = ImVec4(0.00f, 0.94f, 1.00f, 1.00f);

    /* --- Metrics --- */
    /* PD menus have no rounded corners — they are sharp rectangles */
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;

    /* Border sizes — PD uses 1px borders */
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    /* Padding — PD menus are relatively compact */
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(6.0f, 3.0f);
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;

    /* Title bar — we handle this with custom rendering but set a base */
    style.WindowTitleAlign  = ImVec2(0.02f, 0.50f);  /* left-aligned, like PD */
}

/* -----------------------------------------------------------------------
 * Custom window background callback
 *
 * This replaces ImGui's default window background with PD's authentic look.
 * Called from pdguiRender() before each PD-style window.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiRenderWindowBg(void)
{
    ImGuiWindow *window = ImGui::GetCurrentWindowRead();
    if (!window) return;

    ImDrawList *dl = window->DrawList;
    ImVec2 pos = window->Pos;
    ImVec2 size = window->Size;
    float phase = pdguiGetShimmerPhase();

    /* Title bar region */
    float titleH = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y;
    if (titleH < 20.0f) titleH = 20.0f;

    /* We use ImGui's built-in title rendering but add shimmer to borders */

    /* Shimmer on left border */
    pdguiDrawShimmerV(dl, pos.x, pos.y + titleH, pos.x + 1, pos.y + size.y,
                      phase, 10, false);

    /* Shimmer on right border */
    pdguiDrawShimmerV(dl, pos.x + size.x - 1, pos.y + titleH, pos.x + size.x, pos.y + size.y,
                      phase, 10, true);

    /* Shimmer on bottom border */
    pdguiDrawShimmerH(dl, pos.x, pos.y + size.y - 1, pos.x + size.x, pos.y + size.y,
                      phase, 10, false);

    /* Shimmer on title top edge */
    pdguiDrawShimmerH(dl, pos.x, pos.y, pos.x + size.x, pos.y + 1,
                      phase, 40, false);

    /* Shimmer on title bottom edge */
    pdguiDrawShimmerH(dl, pos.x, pos.y + titleH - 1, pos.x + size.x, pos.y + titleH,
                      phase, 40, true);
}

/* -----------------------------------------------------------------------
 * Iterate all active ImGui windows and add PD-style shimmer to borders.
 * Called once per frame after all windows are submitted, before Render().
 * Uses the foreground draw list so shimmer renders on top of all content.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiRenderAllWindowShimmers(void)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    ImDrawList *fgDl = ImGui::GetForegroundDrawList();
    float phase = pdguiGetShimmerPhase();

    for (int i = 0; i < g.Windows.Size; i++) {
        ImGuiWindow *win = g.Windows[i];
        if (!win->Active || !win->WasActive) continue;
        if (win->Hidden) continue;
        if (win->Flags & ImGuiWindowFlags_Tooltip) continue;

        ImVec2 pos = win->Pos;
        ImVec2 size = win->Size;

        bool hasTitleBar = !(win->Flags & ImGuiWindowFlags_NoTitleBar);
        float titleH = hasTitleBar ? win->TitleBarHeight : 0.0f;

        /* Body border shimmer (left, right, bottom) */
        float bodyTop = pos.y + titleH;

        /* Left border shimmer */
        pdguiDrawShimmerV(fgDl, pos.x, bodyTop, pos.x + 1, pos.y + size.y,
                          phase, 10, false);

        /* Right border shimmer */
        pdguiDrawShimmerV(fgDl, pos.x + size.x - 1, bodyTop, pos.x + size.x, pos.y + size.y,
                          phase, 10, true);

        /* Bottom border shimmer */
        pdguiDrawShimmerH(fgDl, pos.x, pos.y + size.y - 1, pos.x + size.x, pos.y + size.y,
                          phase, 10, false);

        /* Title bar shimmer (top and bottom edges) */
        if (hasTitleBar) {
            pdguiDrawShimmerH(fgDl, pos.x, pos.y, pos.x + size.x, pos.y + 1,
                              phase, 40, false);
            pdguiDrawShimmerH(fgDl, pos.x, bodyTop - 1, pos.x + size.x, bodyTop,
                              phase, 40, true);
        }
    }
}
