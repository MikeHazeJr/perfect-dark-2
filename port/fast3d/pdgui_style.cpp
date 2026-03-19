/**
 * pdgui_style.cpp -- Perfect Dark-authentic ImGui styling.
 *
 * Recreates PD's original menu appearance procedurally using ImGui's custom
 * draw API. The original game (menugfx.c) uses GBI primitives -- fill rects,
 * vertex-colored triangles, and shimmer overlays -- all of which map cleanly
 * to ImGui's draw list.
 *
 * This rewrite is a **faithful port** of the exact math from menugfx.c:
 *
 *   Shimmer:  menugfxDrawShimmer() -- 20-second cycle, 6x travel (3600px
 *             total, modulo 600px), alpha formula:
 *               alpha = clampDist * 255 / width
 *               tailcolour = ((baseAlpha * (255 - alpha)) / 255) | 0xffffff00
 *
 *   Gradient: menugfxRenderGradient() -- 3-color vertical (top/mid/bottom),
 *             6 vertices, 2 strips, mid row at (y1+y2)/2.
 *
 *   Borders:  menugfxDrawDialogBorderLine() -- 1px colored line (DrawLine)
 *             + 10px shimmer overlay. Left/bottom use dialog_border1,
 *             right uses dialog_border2.
 *
 *   Dialog:   menugfxRenderDialogBackground() -- solid fill + 3 border lines.
 *
 * Palette system mirrors the original struct menucolourpalette (15 fields,
 * 0xRRGGBBAA format) with support for swappable themes. Blue is the default;
 * Black & Gold is reserved for campaign-completion reward.
 *
 * Part of Sub-Phase D3.4: Menu System Modernization.
 */

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

/* -----------------------------------------------------------------------
 * PD Color Palette System
 *
 * Mirrors struct menucolourpalette from types.h (15 u32 fields, 0xRRGGBBAA).
 * We duplicate the struct layout here to avoid including types.h (which
 * #defines bool as s32, breaking C++).
 * ----------------------------------------------------------------------- */

struct pdgui_palette {
    unsigned int dialog_border1;        /* 0x00 - bright border color (left, bottom) */
    unsigned int dialog_titlebg;        /* 0x04 - dark title bar background */
    unsigned int dialog_border2;        /* 0x08 - accent border color (right) */
    unsigned int dialog_titlefg;        /* 0x0c - title text color */
    unsigned int dialog_bodybg;         /* 0x10 - body background fill */
    unsigned int unused14;              /* 0x14 - unused in rendering */
    unsigned int item_unfocused;        /* 0x18 - normal menu item text */
    unsigned int item_disabled;         /* 0x1c - greyed out item text */
    unsigned int item_focused_inner;    /* 0x20 - focused/hovered item text */
    unsigned int checkbox_checked;      /* 0x24 - checked checkbox color */
    unsigned int item_focused_outer;    /* 0x28 - focused item background */
    unsigned int listgroup_headerbg;    /* 0x2c - list group header bg */
    unsigned int listgroup_headerfg;    /* 0x30 - list group header fg */
    unsigned int unused34;              /* 0x34 */
    unsigned int unused38;              /* 0x38 */
};

/* ------- Built-in Palettes ------- */

/* Index 0: Greyscale (used for faded/background dialogs) */
static const struct pdgui_palette s_PaletteGrey = {
    0x20202000, 0x20202000, 0x20202000, 0x4f4f4f00, 0x00000000,
    0x00000000, 0x4f4f4f00, 0x4f4f4f00, 0x4f4f4f00, 0x4f4f4f00,
    0x00000000, 0x00000000, 0x4f4f4f00, 0x00000000, 0x00000000
};

/* Index 1: Blue -- PD's signature look (NTSC-final values from menu.c) */
static const struct pdgui_palette s_PaletteBlue = {
    0x0060bf7f, 0x0000507f, 0x00f0ff7f, 0xffffffff, 0x00002f9f,
    0x00006f7f, 0x00ffffff, 0x007f7fff, 0xffffffff, 0x8fffffff,
    0x000044ff, 0x000030ff, 0x7f7fffff, 0xffffffff, 0x6644ff7f
};

/* Index 2: Red (Combat Simulator/enemy) */
static const struct pdgui_palette s_PaletteRed = {
    0xbf00007f, 0x5000007f, 0xff00007f, 0xffff00ff, 0x2f00009f,
    0x6f00007f, 0xff9070ff, 0x7f0000ff, 0xffff00ff, 0xffa090ff,
    0x440000ff, 0x003000ff, 0xffff00ff, 0xffffffff, 0xff44447f
};

/* Index 3: Green */
static const struct pdgui_palette s_PaletteGreen = {
    0x00bf007f, 0x0050007f, 0x00ff007f, 0xffff00ff, 0x002f009f,
    0x00ff0028, 0x55ff55ff, 0x006f00af, 0xffffffff, 0x00000000,
    0x004400ff, 0x003000ff, 0xffff00ff, 0xffffffff, 0x44ff447f
};

/* Index 4: White */
static const struct pdgui_palette s_PaletteWhite = {
    0xffffffff, 0xffffff7f, 0xffffffff, 0xffffffff, 0xffffff9f,
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    0x00000000, 0xffffff5f, 0xffffffff, 0xffffff7f, 0xffffffff
};

/* Index 5: Silver */
static const struct pdgui_palette s_PaletteSilver = {
    0xaaaaaaff, 0xaaaaaa7f, 0xaaaaaaff, 0xffffffff, 0xffffff9f,
    0xffffffff, 0xffffffff, 0xffffffff, 0xff8888ff, 0xffffffff,
    0x00000000, 0xffffff5f, 0xffffffff, 0xffffff7f, 0xffffffff
};

/* Index 6: Black and Gold -- campaign completion reward (custom/new)
 *
 * Design intent: near-black backgrounds, gold borders/text/accents.
 * Buttons should appear dark with gold text, not gold-background.
 *
 *   dialog_border1:  gold border (left/bottom edges, accent)
 *   dialog_titlebg:  very dark with slight warm tint
 *   dialog_border2:  bright gold border (right edge, highlights)
 *   dialog_titlefg:  bright gold title text
 *   dialog_bodybg:   near-black body
 *   item_unfocused:  gold menu text
 *   item_disabled:   dim olive/gold
 *   item_focused_inner: bright white (focused item text)
 *   item_focused_outer: dark gold tint (focused item bg)
 */
static const struct pdgui_palette s_PaletteBlackGold = {
    0xbf8f207f, /* dialog_border1:       gold at 50% alpha */
    0x1408007f, /* dialog_titlebg:        near-black warm tint */
    0xffc8407f, /* dialog_border2:        bright gold accent */
    0xffd060ff, /* dialog_titlefg:        bright gold text */
    0x0a06009f, /* dialog_bodybg:         near-black */
    0x3f2a107f, /* unused14 */
    0xdda830ff, /* item_unfocused:        gold text */
    0x6f5020ff, /* item_disabled:         dim gold */
    0xffffffff, /* item_focused_inner:    white when focused */
    0xffd060ff, /* checkbox_checked:      bright gold */
    0x2a1800ff, /* item_focused_outer:    very dark gold bg */
    0x1a0e00ff, /* listgroup_headerbg:    near-black */
    0xdda830ff, /* listgroup_headerfg:    gold text */
    0xffffffff, /* unused34 */
    0xbf8f207f  /* unused38 */
};

/* Active palette -- defaults to Blue. Game code can switch at runtime. */
static const struct pdgui_palette *s_ActivePalette = &s_PaletteBlue;

/* -----------------------------------------------------------------------
 * Color conversion helpers
 * ----------------------------------------------------------------------- */

/* PD 0xRRGGBBAA -> ImU32 (ImGui's packed ABGR) */
static inline ImU32 PdColor(unsigned int rgba)
{
    unsigned char r = (rgba >> 24) & 0xFF;
    unsigned char g = (rgba >> 16) & 0xFF;
    unsigned char b = (rgba >>  8) & 0xFF;
    unsigned char a = (rgba >>  0) & 0xFF;
    return IM_COL32(r, g, b, a);
}

/* PD color with alpha override */
static inline ImU32 PdColorA(unsigned int rgba, unsigned char alpha)
{
    unsigned char r = (rgba >> 24) & 0xFF;
    unsigned char g = (rgba >> 16) & 0xFF;
    unsigned char b = (rgba >>  8) & 0xFF;
    return IM_COL32(r, g, b, alpha);
}

/* -----------------------------------------------------------------------
 * Shimmer -- faithful port of menugfxDrawShimmer() from menugfx.c
 *
 * Original math (lines 997-1104):
 *   v0 = reverse ? (6 * frac * 600) : ((1 - frac) * 6 * 600)
 *   v0 += (u32)(y1 + x1)
 *   v0 %= 600
 *   shimmerLeft = x1 + v0 - width
 *   alpha = clampDist * 255 / width  (clamped to 255)
 *   tailcolour = ((baseAlpha * (255 - alpha)) / 255) | 0xffffff00
 *
 * The "6x" multiplier means the shimmer cycles 6 times in 20 seconds,
 * traveling at ~180px/sec with a 600px modulo period. Each edge gets
 * a different phase offset from (y1+x1)%600.
 *
 * menugfxDrawTri2 renders a gradient quad: colour1 on one side,
 * colour2 on the other. For horizontal (arg7=0), the gradient goes
 * left-to-right. For vertical (arg7=1), top-to-bottom.
 *
 * The shimmer base colour is 0xffffff00 (white, fully transparent) on
 * the dim end, and the tailcolour (white with computed alpha) on the
 * bright end.
 * ----------------------------------------------------------------------- */

/* Get shimmer phase -- matches g_20SecIntervalFrac (0..1 over 20 seconds) */
static float pdguiGetShimmerFrac(void)
{
    float t = (float)ImGui::GetTime();
    t = t / 20.0f;
    t = t - (float)(int)t;  /* fractional part: 0..1 */
    return t;
}

/**
 * pdguiDrawShimmerExact -- pixel-accurate port of menugfxDrawShimmer.
 *
 * @param dl         ImGui draw list
 * @param x1,y1,x2,y2  Edge bounds (same as original)
 * @param baseAlpha  The alpha byte from the border's base colour (colour & 0xff)
 * @param width      Shimmer width in pixels (arg7 in original: 10 for borders, 40 for title)
 * @param reverse    Direction flag (matches original)
 */
static void pdguiDrawShimmerExact(ImDrawList *dl, float x1, float y1, float x2, float y2,
                                   int baseAlpha, int width, bool reverse)
{
    float frac = pdguiGetShimmerFrac();
    bool horizontal = ((y2 - y1) < (x2 - x1));

    /* v0 calculation -- exact match of original integer math */
    int v0;
    if (reverse) {
        v0 = (int)(6.0f * frac * 600.0f);
    } else {
        v0 = (int)((1.0f - frac) * 6.0f * 600.0f);
    }

    v0 += (int)((int)y1 + (int)x1);
    v0 %= 600;
    if (v0 < 0) v0 += 600;  /* safety for negative modulo */

    if (horizontal) {
        int ix1 = (int)x1;
        int ix2 = (int)x2;

        int shimmerleft = ix1 + v0 - width;
        int shimmerright = shimmerleft + width;

        int alpha = 0;
        int minalpha = 0;

        if (shimmerleft < ix1) {
            alpha = ix1 - shimmerleft;
            shimmerleft = ix1;
        }
        if (shimmerright > ix2) {
            minalpha = shimmerright - ix2;
            shimmerright = ix2;
        }
        if (alpha < minalpha) {
            alpha = minalpha;
        }

        alpha = alpha * 255 / width;
        if (alpha > 255) alpha = 255;

        if (ix1 > shimmerright || ix2 < shimmerleft) return;

        /* tailcolour alpha = (baseAlpha * (255 - alpha)) / 255 */
        int tailA = (baseAlpha * (255 - alpha)) / 255;
        if (tailA < 0) tailA = 0;
        if (tailA > 255) tailA = 255;

        ImU32 bright = IM_COL32(255, 255, 255, (unsigned char)tailA);
        ImU32 dim    = IM_COL32(255, 255, 255, 0);

        if (reverse) {
            /* Left to right: dim on left, bright on right */
            dl->AddRectFilledMultiColor(
                ImVec2((float)shimmerleft, y1), ImVec2((float)shimmerright, y2),
                dim, bright, bright, dim);
        } else {
            /* Right to left: bright on left, dim on right */
            dl->AddRectFilledMultiColor(
                ImVec2((float)shimmerleft, y1), ImVec2((float)shimmerright, y2),
                bright, dim, dim, bright);
        }
    } else {
        /* Vertical */
        int iy1 = (int)y1;
        int iy2 = (int)y2;

        int shimmertop = iy1 + v0 - width;
        int shimmerbottom = shimmertop + width;

        int alpha = 0;
        int minalpha = 0;

        if (shimmertop < iy1) {
            alpha = iy1 - shimmertop;
            shimmertop = iy1;
        }
        if (shimmerbottom > iy2) {
            minalpha = shimmerbottom - iy2;
            shimmerbottom = iy2;
        }
        if (alpha < minalpha) {
            alpha = minalpha;
        }

        alpha = alpha * 255 / width;
        if (alpha > 255) alpha = 255;

        if (iy1 > shimmerbottom || iy2 < shimmertop) return;

        int tailA = (baseAlpha * (255 - alpha)) / 255;
        if (tailA < 0) tailA = 0;
        if (tailA > 255) tailA = 255;

        ImU32 bright = IM_COL32(255, 255, 255, (unsigned char)tailA);
        ImU32 dim    = IM_COL32(255, 255, 255, 0);

        if (reverse) {
            /* Top to bottom: dim on top, bright on bottom */
            dl->AddRectFilledMultiColor(
                ImVec2(x1, (float)shimmertop), ImVec2(x2, (float)shimmerbottom),
                dim, dim, bright, bright);
        } else {
            /* Bottom to top: bright on top, dim on bottom */
            dl->AddRectFilledMultiColor(
                ImVec2(x1, (float)shimmertop), ImVec2(x2, (float)shimmerbottom),
                bright, bright, dim, dim);
        }
    }
}

/* -----------------------------------------------------------------------
 * Border line -- port of menugfxDrawDialogBorderLine (lines 1106-1112)
 *
 * Original: draws a solid colored line (menugfxDrawLine -> menugfxDrawTri2)
 * then overlays a 10px shimmer (menugfxDrawShimmer, arg7=10, reverse=false).
 * The border colour's alpha byte drives the shimmer peak brightness.
 * ----------------------------------------------------------------------- */

static void pdguiDrawBorderLine(ImDrawList *dl, float x1, float y1, float x2, float y2,
                                 unsigned int colour)
{
    ImU32 lineCol = PdColor(colour);
    int baseAlpha = colour & 0xFF;

    /* Solid colored line */
    dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), lineCol);

    /* 10px shimmer overlay (always reverse=false, matching original) */
    pdguiDrawShimmerExact(dl, x1, y1, x2, y2, baseAlpha, 10, false);
}

/* -----------------------------------------------------------------------
 * PD Dialog Renderer -- port of menugfxRenderDialogBackground + title gradient
 *
 * Layout (matches original menu.c dialogRender):
 *   +----------------------------+  <- title: 3-color gradient (titlebg/border1/titlebg)
 *   |  Title Text                |     + 40px shimmer on top/bottom edges
 *   +----------------------------+  <- border line with 10px shimmer
 *   |                            |
 *   |  Body content              |  <- solid fill (dialog_bodybg)
 *   |                            |
 *   +----------------------------+  <- bottom border with 10px shimmer
 *     ^ left border                ^ right border
 *
 * Title gradient: menugfxRenderGradient(titlebg, border1, titlebg)
 *   - 3 colors: top=titlebg, mid=border1, bottom=titlebg
 *   - 6 vertices, ymid = (y1+y2)/2
 *   - top half: titlebg -> border1
 *   - bottom half: border1 -> titlebg
 *
 * Body: gDPFillRectangleScaled(x1, y1, x2, y2) with dialog_bodybg
 *
 * Borders (menugfxRenderDialogBackground, lines 197-200):
 *   - Right:  menugfxDrawDialogBorderLine(x2-1, y1, x2, y2, border2, border2)
 *   - Left:   menugfxDrawDialogBorderLine(x1, y1, x1+1, y2, border1, border1)
 *   - Bottom: menugfxDrawDialogBorderLine(x1, y2-1, x2, y2, border1, border2)
 * ----------------------------------------------------------------------- */

extern "C" void pdguiDrawPdDialog(float x, float y, float w, float h,
                                   const char *title, int focused)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const struct pdgui_palette *pal = s_ActivePalette;

    /* Title bar height -- PD uses LINEHEIGHT (11 at 240p). Scale proportionally. */
    float titleH = h * 0.08f;
    if (titleH < 20.0f) titleH = 20.0f;
    if (titleH > 32.0f) titleH = 32.0f;

    /* === Title bar gradient ===
     * menugfxRenderGradient(gdl, x1, y1, x2, y2, titlebg, border1, titlebg)
     * 3-color vertical: top=titlebg, mid=border1, bottom=titlebg
     * ymid = (y1 + y2) / 2 */
    ImU32 titleTop    = PdColor(pal->dialog_titlebg);
    ImU32 titleMid    = PdColor(pal->dialog_border1);
    ImU32 titleBottom = PdColor(pal->dialog_titlebg);

    float titleMidY = y + titleH * 0.5f;

    /* Top half: titlebg -> border1 */
    dl->AddRectFilledMultiColor(
        ImVec2(x, y), ImVec2(x + w, titleMidY),
        titleTop, titleTop, titleMid, titleMid);
    /* Bottom half: border1 -> titlebg */
    dl->AddRectFilledMultiColor(
        ImVec2(x, titleMidY), ImVec2(x + w, y + titleH),
        titleMid, titleMid, titleBottom, titleBottom);

    /* Title shimmer -- 40px width on top and bottom edges of title bar.
     * The original passes the title bar's border1 alpha for shimmer intensity. */
    int titleShimmerAlpha = pal->dialog_border1 & 0xFF;
    pdguiDrawShimmerExact(dl, x, y, x + w, y + 1, titleShimmerAlpha, 40, false);
    pdguiDrawShimmerExact(dl, x, y + titleH - 1, x + w, y + titleH, titleShimmerAlpha, 40, true);

    /* === Body background ===
     * menugfxRenderDialogBackground: gDPFillRectangleScaled with dialog_bodybg */
    float bodyTop = y + titleH;
    dl->AddRectFilled(
        ImVec2(x + 1, bodyTop),
        ImVec2(x + w - 1, y + h),
        PdColor(pal->dialog_bodybg));

    /* === Border lines with shimmer ===
     * Original order (menugfxRenderDialogBackground lines 197-200):
     *   Right:  (x2-1, y1, x2, y2) using border2
     *   Left:   (x1, y1, x1+1, y2) using border1
     *   Bottom: (x1, y2-1, x2, y2) using border1 (left side) blending to border2 (right)
     *
     * For the bottom border, original passes (border1, border2) to DrawLine then
     * calls DrawShimmer with border1's alpha. We approximate by using border1. */

    /* Right border */
    pdguiDrawBorderLine(dl, x + w - 1, bodyTop, x + w, y + h, pal->dialog_border2);

    /* Left border */
    pdguiDrawBorderLine(dl, x, bodyTop, x + 1, y + h, pal->dialog_border1);

    /* Bottom border */
    pdguiDrawBorderLine(dl, x, y + h - 1, x + w, y + h, pal->dialog_border1);
}

/* -----------------------------------------------------------------------
 * Focus highlight -- PD draws focused items with a dark background
 * Uses item_focused_outer from the active palette.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiDrawItemHighlight(float x, float y, float w, float h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h),
                      PdColor(s_ActivePalette->item_focused_outer));
}

/* -----------------------------------------------------------------------
 * Apply PD-authentic ImGui style
 *
 * Derives all ImGui colors from the active palette so theme changes
 * propagate automatically. Metrics match PD's sharp-cornered, compact layout.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiApplyPdStyle(void)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;
    const struct pdgui_palette *pal = s_ActivePalette;

    /* Helper lambda -- PD RGBA to ImVec4 */
    auto C = [](unsigned int rgba) -> ImVec4 {
        return ImVec4(
            ((rgba >> 24) & 0xFF) / 255.0f,
            ((rgba >> 16) & 0xFF) / 255.0f,
            ((rgba >>  8) & 0xFF) / 255.0f,
            ((rgba >>  0) & 0xFF) / 255.0f
        );
    };

    /* Window/frame backgrounds -- derived from dialog_bodybg */
    colors[ImGuiCol_WindowBg]           = C(pal->dialog_bodybg);
    colors[ImGuiCol_ChildBg]            = C((pal->dialog_bodybg & 0xFFFFFF00) | 0x66);
    colors[ImGuiCol_PopupBg]            = C((pal->dialog_bodybg & 0xFFFFFF00) | 0xD9);

    /* Borders -- from dialog_border1 */
    colors[ImGuiCol_Border]             = C(pal->dialog_border1);
    colors[ImGuiCol_BorderShadow]       = ImVec4(0, 0, 0, 0);

    /* Title bar -- from dialog_titlebg and dialog_border1 */
    colors[ImGuiCol_TitleBg]            = C(pal->dialog_titlebg);
    colors[ImGuiCol_TitleBgActive]      = C(pal->dialog_border1);
    colors[ImGuiCol_TitleBgCollapsed]   = C((pal->dialog_titlebg & 0xFFFFFF00) | 0x66);

    /* Frame backgrounds -- darkened body */
    {
        unsigned int fb = pal->dialog_bodybg;
        colors[ImGuiCol_FrameBg]        = C((fb & 0xFFFFFF00) | 0x8A);
        colors[ImGuiCol_FrameBgHovered] = C((pal->dialog_border1 & 0xFFFFFF00) | 0x66);
        colors[ImGuiCol_FrameBgActive]  = C((pal->dialog_border1 & 0xFFFFFF00) | 0xAA);
    }

    /* Text -- title uses titlefg (white), items use item_unfocused (cyan) */
    colors[ImGuiCol_Text]              = C(pal->item_unfocused);
    colors[ImGuiCol_TextDisabled]      = C(pal->item_disabled);
    colors[ImGuiCol_TextSelectedBg]    = C((pal->item_focused_outer & 0xFFFFFF00) | 0x59);

    /* Buttons -- dark body background, border accent on hover/active.
     * PD's menu items sit on the dark body, not on bright colored backgrounds.
     * This keeps buttons readable across all themes including Black & Gold. */
    colors[ImGuiCol_Button]             = C((pal->dialog_bodybg & 0xFFFFFF00) | 0x99);
    colors[ImGuiCol_ButtonHovered]      = C((pal->dialog_border1 & 0xFFFFFF00) | 0x66);
    colors[ImGuiCol_ButtonActive]       = C((pal->dialog_border1 & 0xFFFFFF00) | 0xAA);

    /* Check marks and sliders -- border2 (bright accent) */
    colors[ImGuiCol_CheckMark]          = C(pal->dialog_border2 | 0xFF);
    colors[ImGuiCol_SliderGrab]         = C(pal->dialog_border1 | 0xFF);
    colors[ImGuiCol_SliderGrabActive]   = C(pal->dialog_border2 | 0xFF);

    /* Headers (collapsing headers, selectable rows) */
    colors[ImGuiCol_Header]             = C((pal->dialog_bodybg & 0xFFFFFF00) | 0x99);
    colors[ImGuiCol_HeaderHovered]      = C((pal->dialog_border1 & 0xFFFFFF00) | 0xCC);
    colors[ImGuiCol_HeaderActive]       = C(pal->dialog_border1 | 0xFF);

    /* Separator -- border1 */
    colors[ImGuiCol_Separator]          = C(pal->dialog_border1);
    colors[ImGuiCol_SeparatorHovered]   = C(pal->dialog_border2 | 0xC7);
    colors[ImGuiCol_SeparatorActive]    = C(pal->dialog_border2 | 0xFF);

    /* Tabs */
    colors[ImGuiCol_Tab]                = C((pal->dialog_titlebg & 0xFFFFFF00) | 0xDB);
    colors[ImGuiCol_TabHovered]         = C((pal->dialog_border1 & 0xFFFFFF00) | 0xCC);
    colors[ImGuiCol_TabActive]          = C((pal->dialog_border1 & 0xFFFFFF00) | 0xFF);

    /* Scrollbar */
    colors[ImGuiCol_ScrollbarBg]        = C((pal->dialog_bodybg & 0xFFFFFF00) | 0x87);
    colors[ImGuiCol_ScrollbarGrab]      = C(pal->dialog_border1);
    colors[ImGuiCol_ScrollbarGrabHovered] = C((pal->dialog_border1 & 0xFFFFFF00) | 0xB3);
    colors[ImGuiCol_ScrollbarGrabActive]  = C(pal->dialog_border2 | 0xFF);

    /* Resize grip */
    colors[ImGuiCol_ResizeGrip]         = C((pal->dialog_border1 & 0xFFFFFF00) | 0x33);
    colors[ImGuiCol_ResizeGripHovered]  = C((pal->dialog_border2 & 0xFFFFFF00) | 0xAA);
    colors[ImGuiCol_ResizeGripActive]   = C(pal->dialog_border2 | 0xF2);

    /* Nav highlight -- accent color */
    colors[ImGuiCol_NavHighlight]       = C(pal->dialog_border2 | 0xFF);

    /* --- Metrics --- */
    /* PD menus: sharp rectangles, no rounding anywhere */
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;

    /* 1px borders everywhere */
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    /* Compact padding -- PD menus are tight */
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(6.0f, 3.0f);
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;

    /* Left-aligned title, like PD */
    style.WindowTitleAlign  = ImVec2(0.02f, 0.50f);
}

/* -----------------------------------------------------------------------
 * Window background callback -- adds shimmer to any active PD-style window
 * ----------------------------------------------------------------------- */

extern "C" void pdguiRenderWindowBg(void)
{
    ImGuiWindow *window = ImGui::GetCurrentWindowRead();
    if (!window) return;

    ImDrawList *dl = window->DrawList;
    ImVec2 pos = window->Pos;
    ImVec2 size = window->Size;
    const struct pdgui_palette *pal = s_ActivePalette;
    int borderAlpha1 = pal->dialog_border1 & 0xFF;
    int borderAlpha2 = pal->dialog_border2 & 0xFF;

    float titleH = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y;
    if (titleH < 20.0f) titleH = 20.0f;

    /* Left border shimmer (border1 alpha) */
    pdguiDrawShimmerExact(dl, pos.x, pos.y + titleH, pos.x + 1, pos.y + size.y,
                          borderAlpha1, 10, false);

    /* Right border shimmer (border2 alpha) */
    pdguiDrawShimmerExact(dl, pos.x + size.x - 1, pos.y + titleH, pos.x + size.x, pos.y + size.y,
                          borderAlpha2, 10, true);

    /* Bottom border shimmer (border1 alpha) */
    pdguiDrawShimmerExact(dl, pos.x, pos.y + size.y - 1, pos.x + size.x, pos.y + size.y,
                          borderAlpha1, 10, false);

    /* Title top edge shimmer (40px, border1 alpha) */
    pdguiDrawShimmerExact(dl, pos.x, pos.y, pos.x + size.x, pos.y + 1,
                          borderAlpha1, 40, false);

    /* Title bottom edge shimmer (40px) */
    pdguiDrawShimmerExact(dl, pos.x, pos.y + titleH - 1, pos.x + size.x, pos.y + titleH,
                          borderAlpha1, 40, true);
}

/* -----------------------------------------------------------------------
 * Iterate all active windows and add PD-style shimmer to their borders.
 * Called once per frame after all windows are submitted, before Render().
 * Uses the foreground draw list so shimmer renders on top of all content.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiRenderAllWindowShimmers(void)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    ImDrawList *fgDl = ImGui::GetForegroundDrawList();
    const struct pdgui_palette *pal = s_ActivePalette;
    int borderAlpha1 = pal->dialog_border1 & 0xFF;
    int borderAlpha2 = pal->dialog_border2 & 0xFF;

    for (int i = 0; i < g.Windows.Size; i++) {
        ImGuiWindow *win = g.Windows[i];
        if (!win->Active || !win->WasActive) continue;
        if (win->Hidden) continue;
        if (win->Flags & ImGuiWindowFlags_Tooltip) continue;
        /* Skip windows using custom PD dialog rendering (NoBackground).
         * Those already include shimmer via pdguiDrawPdDialog(). */
        if (win->Flags & ImGuiWindowFlags_NoBackground) continue;

        ImVec2 pos = win->Pos;
        ImVec2 size = win->Size;

        bool hasTitleBar = !(win->Flags & ImGuiWindowFlags_NoTitleBar);
        float titleH = hasTitleBar ? win->TitleBarHeight : 0.0f;
        float bodyTop = pos.y + titleH;

        /* Left border shimmer */
        pdguiDrawShimmerExact(fgDl, pos.x, bodyTop, pos.x + 1, pos.y + size.y,
                              borderAlpha1, 10, false);

        /* Right border shimmer */
        pdguiDrawShimmerExact(fgDl, pos.x + size.x - 1, bodyTop, pos.x + size.x, pos.y + size.y,
                              borderAlpha2, 10, true);

        /* Bottom border shimmer */
        pdguiDrawShimmerExact(fgDl, pos.x, pos.y + size.y - 1, pos.x + size.x, pos.y + size.y,
                              borderAlpha1, 10, false);

        /* Title bar shimmer (top and bottom edges) */
        if (hasTitleBar) {
            pdguiDrawShimmerExact(fgDl, pos.x, pos.y, pos.x + size.x, pos.y + 1,
                                  borderAlpha1, 40, false);
            pdguiDrawShimmerExact(fgDl, pos.x, bodyTop - 1, pos.x + size.x, bodyTop,
                                  borderAlpha1, 40, true);
        }
    }
}

/* -----------------------------------------------------------------------
 * Palette API -- allows runtime theme switching
 * ----------------------------------------------------------------------- */

extern "C" void pdguiSetPalette(int index)
{
    switch (index) {
        case 0: s_ActivePalette = &s_PaletteGrey;      break;
        case 1: s_ActivePalette = &s_PaletteBlue;      break;
        case 2: s_ActivePalette = &s_PaletteRed;       break;
        case 3: s_ActivePalette = &s_PaletteGreen;     break;
        case 4: s_ActivePalette = &s_PaletteWhite;     break;
        case 5: s_ActivePalette = &s_PaletteSilver;    break;
        case 6: s_ActivePalette = &s_PaletteBlackGold; break;
        default: s_ActivePalette = &s_PaletteBlue;     break;
    }

    /* Re-apply style so ImGui colors update immediately */
    pdguiApplyPdStyle();
}

extern "C" int pdguiGetPalette(void)
{
    if (s_ActivePalette == &s_PaletteGrey)      return 0;
    if (s_ActivePalette == &s_PaletteBlue)      return 1;
    if (s_ActivePalette == &s_PaletteRed)       return 2;
    if (s_ActivePalette == &s_PaletteGreen)     return 3;
    if (s_ActivePalette == &s_PaletteWhite)     return 4;
    if (s_ActivePalette == &s_PaletteSilver)    return 5;
    if (s_ActivePalette == &s_PaletteBlackGold) return 6;
    return 1;
}

/* -----------------------------------------------------------------------
 * Theme System -- extended settings beyond palette selection
 *
 * The theme wraps:
 *   - Palette index (which color set to use)
 *   - Global tint/burn strength (0.0 = pure palette, 1.0 = full original PD colors)
 *   - Text glow color + intensity (the soft bloom behind focused/title text)
 *   - Sound FX override (0 = default PD sounds, future: alternate packs)
 *
 * The tint works as a blend between the palette colors and a "burn" color
 * (typically the window's original PD color type). At tint=0, pure palette.
 * At tint=1.0, the original dialog type color dominates.
 * ----------------------------------------------------------------------- */

static struct pdgui_theme {
    int   paletteIndex;         /* Which palette (0-6) */
    float tintStrength;         /* 0.0 = pure palette, 1.0 = full burn/tint */
    unsigned int tintColor;     /* Tint/burn color in 0xRRGGBBAA */
    float textGlowIntensity;    /* 0.0 = no glow, 1.0 = full glow */
    unsigned int textGlowColor; /* Glow color in 0xRRGGBBAA (usually palette accent) */
    int   soundPack;            /* 0 = default PD sounds */
} s_Theme = {
    1,             /* paletteIndex: Blue */
    0.0f,          /* tintStrength: no tint (pure palette) */
    0x0060bf7f,    /* tintColor: default blue */
    0.6f,          /* textGlowIntensity: moderate glow */
    0x0080ffff,    /* textGlowColor: bright blue glow */
    0              /* soundPack: default */
};

extern "C" void pdguiThemeSetPalette(int index)
{
    s_Theme.paletteIndex = index;
    pdguiSetPalette(index);

    /* Auto-set glow color from the new palette's border2 (bright accent) */
    s_Theme.textGlowColor = s_ActivePalette->dialog_border2 | 0x80;
}

extern "C" void pdguiThemeSetTint(float strength, unsigned int color)
{
    s_Theme.tintStrength = strength;
    s_Theme.tintColor = color;
    /* Re-apply style with tint */
    pdguiApplyPdStyle();
}

extern "C" float pdguiThemeGetTintStrength(void)
{
    return s_Theme.tintStrength;
}

extern "C" void pdguiThemeSetTextGlow(float intensity, unsigned int color)
{
    s_Theme.textGlowIntensity = intensity;
    s_Theme.textGlowColor = color;
}

extern "C" float pdguiThemeGetTextGlowIntensity(void)
{
    return s_Theme.textGlowIntensity;
}

extern "C" unsigned int pdguiThemeGetTextGlowColor(void)
{
    return s_Theme.textGlowColor;
}

extern "C" void pdguiThemeSetSoundPack(int pack)
{
    s_Theme.soundPack = pack;
}

extern "C" int pdguiThemeGetSoundPack(void)
{
    return s_Theme.soundPack;
}

/* -----------------------------------------------------------------------
 * Text Glow Rendering
 *
 * Draws a soft colored glow behind text. Used for title text and
 * focused menu items to replicate PD's characteristic text glow effect.
 *
 * The glow is rendered as a blurred rectangle behind the text bounds,
 * using the theme's glow color and intensity.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiDrawTextGlow(float x, float y, float textW, float textH)
{
    if (s_Theme.textGlowIntensity <= 0.0f) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    unsigned int gc = s_Theme.textGlowColor;
    unsigned char gr = (gc >> 24) & 0xFF;
    unsigned char gg = (gc >> 16) & 0xFF;
    unsigned char gb = (gc >>  8) & 0xFF;
    unsigned char baseA = (unsigned char)(s_Theme.textGlowIntensity * 60.0f);

    /* Multi-layer soft glow -- 3 passes at increasing size, decreasing alpha */
    for (int pass = 0; pass < 3; pass++) {
        float expand = (float)(pass + 1) * 3.0f;
        unsigned char alpha = (unsigned char)(baseA / (pass + 1));

        dl->AddRectFilled(
            ImVec2(x - expand, y - expand),
            ImVec2(x + textW + expand, y + textH + expand),
            IM_COL32(gr, gg, gb, alpha),
            expand * 0.5f);  /* rounded corners for glow softness */
    }
}
