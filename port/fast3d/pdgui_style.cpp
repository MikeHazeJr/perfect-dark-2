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

#include <string.h>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "pdgui_theme.h"
#include "pdgui_nineslice.h"
#include "pdgui_effects.h"
#include "pdgui_fontmgr.h"
#include "assetcatalog.h"
#include "system.h"

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
    unsigned int checkbox_checked;      /* 0x24 - checked checkbox color (S306: now live) */
    unsigned int item_focused_outer;    /* 0x28 - focused item background */
    unsigned int listgroup_headerbg;    /* 0x2c - list group header bg */
    unsigned int listgroup_headerfg;    /* 0x30 - list group header fg */
    unsigned int unused34;              /* 0x34 */
    unsigned int unused38;              /* 0x38 */
    /* ---- S306 extensions ----
     * Zero means "derive from legacy fields at apply time" so the existing
     * 15-field built-ins keep their look unchanged until a theme.json opts in.
     * Read/written via theme.json keys: toolbarTint, textPositive, textWarning,
     * buttonHover, buttonActive. Checkbox checkmark uses `checkbox_checked`. */
    unsigned int toolbar_tint;          /* 0x3c - toolbars / modding-hub nav / segmented rows */
    unsigned int text_positive;         /* 0x40 - success/status-ok text (lime) */
    unsigned int text_warning;          /* 0x44 - section-heading / warning text (amber) */
    unsigned int button_hover;          /* 0x48 - ImGuiCol_ButtonHovered override */
    unsigned int button_active;         /* 0x4c - ImGuiCol_ButtonActive override */
    /* ---- S309 extensions ----
     * Window-state tints + title glow color so themes can author the full
     * semantic identity. Zero = derive at apply time (see pdguiGet*).
     * theme.json keys: titleGlow, tintSuccess, tintDanger, tintInfo. */
    unsigned int title_glow;            /* 0x50 - title text glow color (was hardcoded blue) */
    unsigned int tint_success;          /* 0x54 - success window tint (save OK, etc.) */
    unsigned int tint_danger;           /* 0x58 - danger window tint (delete, abort, end-game) */
    unsigned int tint_info;             /* 0x5c - info / other window tint (notices, prompts) */
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

/* Custom palette for JSON-loaded themes (writable copy) */
static struct pdgui_palette s_PaletteCustom;
static bool s_UsingCustomPalette = false;
static s32 s_CloseClickConsumedFrame = -1;

/* -----------------------------------------------------------------------
 * Chrome render state
 *
 * Two pieces of state drive chrome rendering:
 *   s_ChromeEnabled          — global on/off (Settings toggle)
 *   s_ChromeNineSliceId[]    — catalog ID of the active chrome nineslice
 *
 * pdguiDrawPdDialog checks both at entry.  When chrome is enabled AND the
 * active catalog ID resolves to a registered nineslice + loaded texture,
 * the dialog body is drawn via pdguiNinesliceDrawEx instead of the
 * procedural body fill + border lines.  Otherwise the procedural path
 * runs exactly as before (pixel-for-pixel parity with S195 behaviour).
 * ----------------------------------------------------------------------- */

static bool s_ChromeEnabled = false;
static char s_ChromeNineSliceId[64] = "";

/* S306: direct-signal close channel driven by the title-bar X button.
 * `pdguiDrawPdDialog`'s X-click handler sets `s_TitleCloseFrame` to the
 * current frame number. Renderers call `pdguiConsumeTitleClose()` inside
 * their back/Esc path — it returns 1 if the close was requested on the
 * current or previous frame, resets the flag, and the renderer runs the
 * normal close flow. This replaces the prior-behaviour of injecting an
 * ImGuiKey_Escape press/release pair via AddKeyEvent, which could be
 * eaten by ImGui's own nav-stack-pop handling before the renderer's
 * IsKeyPressed check saw it — surfacing as "X button does nothing the
 * first time, works the second time" on nested dialog/tab layouts. The
 * Escape fallback is kept so renderers that haven't been migrated still
 * respond. */
static s32  s_TitleCloseFrame = -1;

extern "C" {

void pdguiChromeSetEnabled(s32 enabled)
{
    bool next = (enabled != 0);
    if (next != s_ChromeEnabled) {
        s_ChromeEnabled = next;
        sysLogPrintf(LOG_NOTE, "UI.CHROME: enabled=%d", (int)next);
    } else {
        s_ChromeEnabled = next;
    }
}

s32 pdguiChromeIsEnabled(void)
{
    return s_ChromeEnabled ? 1 : 0;
}

void pdguiSetPanelNineSlice(const char *nineslice_catalog_id)
{
    if (!nineslice_catalog_id || !nineslice_catalog_id[0]) {
        if (s_ChromeNineSliceId[0]) {
            sysLogPrintf(LOG_NOTE, "UI.CHROME: cleared active chrome (was '%s')",
                         s_ChromeNineSliceId);
        }
        s_ChromeNineSliceId[0] = '\0';
        return;
    }

    if (strcmp(s_ChromeNineSliceId, nineslice_catalog_id) != 0) {
        strncpy(s_ChromeNineSliceId, nineslice_catalog_id,
                sizeof(s_ChromeNineSliceId) - 1);
        s_ChromeNineSliceId[sizeof(s_ChromeNineSliceId) - 1] = '\0';
        sysLogPrintf(LOG_NOTE, "UI.CHROME: active chrome set to '%s'",
                     s_ChromeNineSliceId);
    }
}

const char *pdguiGetPanelNineSlice(void)
{
    return s_ChromeNineSliceId[0] ? s_ChromeNineSliceId : nullptr;
}

void pdguiClearPanelNineSlice(void)
{
    pdguiSetPanelNineSlice(nullptr);
}

} /* extern "C" */

/* Internal helper: return true and set *out_def/*out_tex/*out_tw/*out_th if
 * a chrome is enabled and fully resolvable (nineslice registered AND texture
 * loaded).  Otherwise return false and the procedural path should be used. */
static bool s_resolveActiveChrome(const nineslice_def_t **out_def,
                                  void **out_tex,
                                  u32 *out_tw, u32 *out_th)
{
    if (!s_ChromeEnabled || !s_ChromeNineSliceId[0]) return false;

    const nineslice_def_t *def = pdguiNinesliceGet(s_ChromeNineSliceId);
    if (!def) return false;

    /* The chrome nineslice draws off a source texture; the nineslice's
     * catalog ID is typically a frame id (e.g. "base:ui_chrome_frame") that
     * is REGISTERED to a texture catalog id (e.g. "base:ui_chrome_center").
     * For the initial implementation we treat the nineslice ID as also
     * being a theme texture ID — manifest authors should register the
     * nineslice under the same id as the center texture for simplicity.
     * Alternatively they can register both ids separately and the lookup
     * below will try both forms.  Keep the fast path simple. */
    void *tex = pdguiThemeGetTexture(s_ChromeNineSliceId);
    u32 tw = 0, th = 0;
    if (tex) {
        pdguiThemeGetTextureSize(s_ChromeNineSliceId, &tw, &th);
    }

    if (!tex || tw == 0 || th == 0) return false;

    if (out_def) *out_def = def;
    if (out_tex) *out_tex = tex;
    if (out_tw)  *out_tw  = tw;
    if (out_th)  *out_th  = th;
    return true;
}

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

    /* Compute edge length and use it as the modulo cycle instead of fixed 600.
     * Original N64 used 600 which was fine at 240p. At PC resolutions the dialog
     * can exceed 600px, so the shimmer would never reach the far end. */
    int edgeLen = horizontal ? (int)(x2 - x1) : (int)(y2 - y1);
    if (edgeLen < 100) edgeLen = 100; /* minimum cycle to avoid degenerate cases */

    /* v0 calculation -- adapted from original, scaled to edge length */
    int v0;
    if (reverse) {
        v0 = (int)(6.0f * frac * (float)edgeLen);
    } else {
        v0 = (int)((1.0f - frac) * 6.0f * (float)edgeLen);
    }

    v0 += (int)((int)y1 + (int)x1);
    v0 %= edgeLen;
    if (v0 < 0) v0 += edgeLen;

    /* Intensity boost: scale width and alpha for PC.
     * Original shimmer was subtle at 240p; at higher res the 10px width
     * is barely visible. Scale width by ~2x and boost alpha by 1.8x. */
    int scaledWidth = width * 2;
    int boostedAlpha = (baseAlpha * 180) / 100;
    if (boostedAlpha > 255) boostedAlpha = 255;

    if (horizontal) {
        int ix1 = (int)x1;
        int ix2 = (int)x2;

        int shimmerleft = ix1 + v0 - scaledWidth;
        int shimmerright = shimmerleft + scaledWidth;

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

        alpha = alpha * 255 / scaledWidth;
        if (alpha > 255) alpha = 255;

        if (ix1 > shimmerright || ix2 < shimmerleft) return;

        /* tailcolour alpha = (boostedAlpha * (255 - alpha)) / 255 */
        int tailA = (boostedAlpha * (255 - alpha)) / 255;
        if (tailA < 0) tailA = 0;
        if (tailA > 255) tailA = 255;

        ImU32 bright = IM_COL32(255, 255, 255, (unsigned char)tailA);
        ImU32 dim    = IM_COL32(255, 255, 255, 0);

        if (reverse) {
            dl->AddRectFilledMultiColor(
                ImVec2((float)shimmerleft, y1), ImVec2((float)shimmerright, y2),
                dim, bright, bright, dim);
        } else {
            dl->AddRectFilledMultiColor(
                ImVec2((float)shimmerleft, y1), ImVec2((float)shimmerright, y2),
                bright, dim, dim, bright);
        }
    } else {
        /* Vertical */
        int iy1 = (int)y1;
        int iy2 = (int)y2;

        int shimmertop = iy1 + v0 - scaledWidth;
        int shimmerbottom = shimmertop + scaledWidth;

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

        alpha = alpha * 255 / scaledWidth;
        if (alpha > 255) alpha = 255;

        if (iy1 > shimmerbottom || iy2 < shimmertop) return;

        int tailA = (boostedAlpha * (255 - alpha)) / 255;
        if (tailA < 0) tailA = 0;
        if (tailA > 255) tailA = 255;

        ImU32 bright = IM_COL32(255, 255, 255, (unsigned char)tailA);
        ImU32 dim    = IM_COL32(255, 255, 255, 0);

        if (reverse) {
            dl->AddRectFilledMultiColor(
                ImVec2(x1, (float)shimmertop), ImVec2(x2, (float)shimmerbottom),
                dim, dim, bright, bright);
        } else {
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
    ImGuiIO &io = ImGui::GetIO();
    const struct pdgui_palette *pal = s_ActivePalette;

    /* Title bar height -- PD uses LINEHEIGHT (11 at 240p). Scale proportionally. */
    float titleH = h * 0.08f;
    if (titleH < 20.0f) titleH = 20.0f;
    if (titleH > 32.0f) titleH = 32.0f;

    /* === Title bar render ===
     * Procedural style chosen via pdguiThemeGetTitleBarStyle() (S297).
     * Classic = PD default (3-color vertical gradient titlebg→border1→titlebg).
     * Others reuse the same palette entries so theme recolour still flows
     * through to every variant. */
    s32 titleStyle = pdguiThemeGetTitleBarStyle();
    ImU32 titleTop    = PdColor(pal->dialog_titlebg);
    ImU32 titleMid    = PdColor(pal->dialog_border1);
    ImU32 titleBottom = PdColor(pal->dialog_titlebg);
    float titleMidY = y + titleH * 0.5f;

    switch (titleStyle) {
    case PDGUI_TITLEBAR_SOLID:
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + titleH), titleMid);
        break;

    case PDGUI_TITLEBAR_VERT_BARS: {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + titleH), titleTop);
        ImU32 barCol = PdColorA(pal->dialog_border1, 96);
        float barStride = 8.0f;
        for (float bx = x; bx < x + w; bx += barStride) {
            dl->AddRectFilled(ImVec2(bx, y),
                              ImVec2(bx + 1.0f, y + titleH), barCol);
        }
        break;
    }

    case PDGUI_TITLEBAR_SCANLINES: {
        /* Classic gradient + 1px scanline every other row */
        dl->AddRectFilledMultiColor(
            ImVec2(x, y), ImVec2(x + w, titleMidY),
            titleTop, titleTop, titleMid, titleMid);
        dl->AddRectFilledMultiColor(
            ImVec2(x, titleMidY), ImVec2(x + w, y + titleH),
            titleMid, titleMid, titleBottom, titleBottom);
        ImU32 scan = IM_COL32(0, 0, 0, 40);
        for (float sy = y + 1.0f; sy < y + titleH; sy += 2.0f) {
            dl->AddRectFilled(ImVec2(x, sy), ImVec2(x + w, sy + 1.0f), scan);
        }
        break;
    }

    case PDGUI_TITLEBAR_DIAG_STRIPES: {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + titleH), titleMid);
        ImU32 stripe = PdColorA(pal->dialog_titlebg, 128);
        float stripeStride = 10.0f;
        float stripeWidth = 4.0f;
        for (float sx = x - titleH; sx < x + w; sx += stripeStride) {
            ImVec2 p0(sx,               y);
            ImVec2 p1(sx + stripeWidth, y);
            ImVec2 p2(sx + stripeWidth + titleH, y + titleH);
            ImVec2 p3(sx + titleH,               y + titleH);
            dl->AddQuadFilled(p0, p1, p2, p3, stripe);
        }
        break;
    }

    case PDGUI_TITLEBAR_CLASSIC:
    default:
        /* Top half: titlebg -> border1 */
        dl->AddRectFilledMultiColor(
            ImVec2(x, y), ImVec2(x + w, titleMidY),
            titleTop, titleTop, titleMid, titleMid);
        /* Bottom half: border1 -> titlebg */
        dl->AddRectFilledMultiColor(
            ImVec2(x, titleMidY), ImVec2(x + w, y + titleH),
            titleMid, titleMid, titleBottom, titleBottom);
        break;
    }

    /* Title shimmer -- 40px width on top and bottom edges of title bar.
     * The original passes the title bar's border1 alpha for shimmer intensity.
     *
     * Strip thickness scaled from N64's 1px to 3px for PC pixel density. At
     * 240p a 1px shimmer line was visible against the title gradient; at
     * 1080p+ the eye does not pick up a single-pixel brightness sweep. 3px
     * preserves the OG visual prominence without crossing into the title
     * body area. Same spirit as pdguiDrawShimmerExact's existing 2x width
     * scale for PC. */
    int titleShimmerAlpha = pal->dialog_border1 & 0xFF;
    const float kTitleShimmerThickness = 3.0f;
    pdguiDrawShimmerExact(dl, x, y, x + w, y + kTitleShimmerThickness,
                          titleShimmerAlpha, 40, false);
    pdguiDrawShimmerExact(dl, x, y + titleH - kTitleShimmerThickness, x + w, y + titleH,
                          titleShimmerAlpha, 40, true);

    /* Universal mouse close affordance ("X") in the title bar.
     * Clicking this emits an Escape key edge so every menu uses its existing
     * back/cancel path (the same path used by keyboard/gamepad cancel). */
    {
        float btnPad = 4.0f;
        float btnSize = titleH - btnPad * 2.0f;
        if (btnSize < 14.0f) btnSize = 14.0f;

        ImVec2 bmin(x + w - btnPad - btnSize, y + btnPad);
        ImVec2 bmax(bmin.x + btnSize, bmin.y + btnSize);
        /* S-4: window-clip the hover rect so hovering a close button drawn
         * on a background dialog (under a modal popup) doesn't light up /
         * fire a click through the popup. */
        bool hovered = ImGui::IsMouseHoveringRect(bmin, bmax, true);

        ImU32 bgCol = hovered
            ? PdColorA(pal->dialog_border2, 224)
            : PdColorA(pal->dialog_border1, 176);
        ImU32 xCol = hovered
            ? PdColorA(pal->dialog_titlebg, 255)
            : PdColorA(pal->dialog_titlefg, 255);

        dl->AddRectFilled(bmin, bmax, bgCol, 0.0f);
        dl->AddRect(bmin, bmax, PdColorA(pal->dialog_border2, 255), 0.0f, 0, 1.0f);

        float inset = btnSize * 0.28f;
        dl->AddLine(ImVec2(bmin.x + inset, bmin.y + inset),
                    ImVec2(bmax.x - inset, bmax.y - inset), xCol, 2.0f);
        dl->AddLine(ImVec2(bmin.x + inset, bmax.y - inset),
                    ImVec2(bmax.x - inset, bmin.y + inset), xCol, 2.0f);

        /* S-4: only accept the click when this window is focused — prevents
         * background dialogs from being dismissed through a modal popup.
         * S306: set the direct title-close flag alongside the Escape key
         * injection. Renderers that call pdguiConsumeTitleClose() will
         * act on the click immediately, even when ImGui's own nav pops
         * the Escape edge before the renderer's IsKeyPressed runs. The
         * Escape AddKeyEvent pair remains as a back-compat fallback for
         * renderers that haven't been migrated to the new API. */
        if (hovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left, false)) {
            s32 frame = ImGui::GetFrameCount();
            if (s_CloseClickConsumedFrame != frame) {
                s_TitleCloseFrame = frame;
                io.AddKeyEvent(ImGuiKey_Escape, true);
                io.AddKeyEvent(ImGuiKey_Escape, false);
                s_CloseClickConsumedFrame = frame;
            }
        }
    }

    /* === Chrome render branch ===
     * When chrome is enabled AND the active chrome catalog id resolves to
     * a registered nineslice + cached texture, draw the chrome nineslice
     * as the body background + border layer.  Otherwise fall through to
     * the procedural render path.  Either way the title bar above is
     * drawn identically, so users see a consistent frame header and only
     * the body artwork swaps when they toggle chrome style. */
    float bodyTop = y + titleH;

    const nineslice_def_t *chromeDef = nullptr;
    void *chromeTex = nullptr;
    u32 chromeTw = 0, chromeTh = 0;
    bool useChrome = s_resolveActiveChrome(&chromeDef, &chromeTex, &chromeTw, &chromeTh);
    const char *activeChromeId = pdguiGetPanelNineSlice();
    bool declaredChromeFailed = pdguiChromeIsEnabled() && activeChromeId &&
        activeChromeId[0] && !useChrome;

    if (useChrome) {
        /* Tint with the palette's bright accent color (dialog_border1) but
         * force full alpha — the chrome asset's own alpha channel is the
         * mask, the palette provides the hue.  This lets theme recolour
         * flow through to the chrome artwork. */
        u32 tint = (pal->dialog_border1 & 0xFFFFFF00u) | 0xFFu;

        pdguiNinesliceDrawEx(chromeTex, (s32)chromeTw, (s32)chromeTh, chromeDef,
                             x, bodyTop, w, (y + h) - bodyTop, tint);

        /* Chrome replaces the procedural body background + haze overlay +
         * border lines + perimeter shimmer.  Fall through to caustic /
         * border effect placeholders at the bottom of the function. */
    } else if (!declaredChromeFailed) {
        /* === Body background ===
         * menugfxRenderDialogBackground: gDPFillRectangleScaled with dialog_bodybg */
        dl->AddRectFilled(
            ImVec2(x + 1, bodyTop),
            ImVec2(x + w - 1, y + h),
            PdColor(pal->dialog_bodybg));

        /* === Animated body haze (BgGreenHaze recipe) ===
         * OG menugfxRenderBgGreenHaze (src/game/menugfx.c:226-341) renders
         * two layers of g_TexGeneralConfigs[6] (the haze IA8 tile) at
         * counter-rotating UVs, with an alpha fade-in/out envelope and a
         * green primitive color. Recipe per layer i in {0, 1}:
         *
         *   phase  = ((t / 20) + (i == 1 ? 0.5 : 0.0)) mod 1
         *   angle  = (i == 1 ? -1 : +1) * 2*pi * phase
         *   alpha  = phase < 0.2  -> phase / 0.2  * 0.5
         *            phase > 0.9  -> (1 - phase) / 0.1 * 0.5
         *            else         -> 0.5
         *
         * The two layers' phases are 0.5 apart so when one is fading in,
         * the other is fading out, keeping total coverage roughly constant.
         * The opposite rotation directions cross-fade into a non-repeating
         * organic haze pattern instead of a static tile.
         *
         * ImGui::AddImage takes axis-aligned UVs only, so we use AddImageQuad
         * with explicit per-vertex UVs computed from the rotation matrix.
         * Each layer samples a 2x2 UV square centered at (0.5, 0.5) so the
         * haze tiles at half the natural texture rate (matching OG's f20
         * "zoom factor" centered around 15.0).
         *
         * Stage 0 of the procedural-theme dual-mode work: this is the "OG-
         * faithful animated haze" mode. The static-tile-tint mode that this
         * replaces is suppressed by the chrome-enabled branch above (when the
         * user picks Classic chrome, the nineslice replaces the body and no
         * haze draws -- the static chrome IS the body). */
        if (pdguiThemeGetBgTexId()) {
            const char *bgTex = pdguiThemeGetBgTexId();
            void *texId = pdguiThemeGetTexture(bgTex);
            if (texId) {
                const float kTwoPi = 6.28318530717958647692f;
                float t = (float)ImGui::GetTime();
                float baseFrac = t / 20.0f;
                baseFrac = baseFrac - (float)(int)baseFrac;

                ImVec2 c0(x + 1.0f, bodyTop);
                ImVec2 c1(x + w - 1.0f, bodyTop);
                ImVec2 c2(x + w - 1.0f, y + h);
                ImVec2 c3(x + 1.0f, y + h);

                /* Sample a 2x2 UV square centered at (0.5, 0.5). The four
                 * corner UVs are then rotated about the center per layer. */
                const float kHalf = 1.0f;
                ImVec2 uvBase[4] = {
                    ImVec2(-kHalf, -kHalf),
                    ImVec2(+kHalf, -kHalf),
                    ImVec2(+kHalf, +kHalf),
                    ImVec2(-kHalf, +kHalf),
                };

                for (int layer = 0; layer < 2; layer++) {
                    float phase = baseFrac + (layer == 1 ? 0.5f : 0.0f);
                    if (phase >= 1.0f) phase -= 1.0f;

                    float dir = (layer == 1) ? -1.0f : +1.0f;
                    float angle = dir * kTwoPi * phase;
                    float ca = cosf(angle);
                    float sa = sinf(angle);

                    float a;
                    if      (phase < 0.2f) a = (phase / 0.2f) * 0.5f;
                    else if (phase > 0.9f) a = ((1.0f - phase) / 0.1f) * 0.5f;
                    else                   a = 0.5f;
                    if (a < 0.0f) a = 0.0f;
                    if (a > 1.0f) a = 1.0f;

                    /* Apply rotation matrix to each base UV, then translate
                     * back to (0.5, 0.5) origin for sampling. */
                    ImVec2 uv[4];
                    for (int i = 0; i < 4; i++) {
                        float u = uvBase[i].x * ca - uvBase[i].y * sa;
                        float v = uvBase[i].x * sa + uvBase[i].y * ca;
                        uv[i] = ImVec2(u + 0.5f, v + 0.5f);
                    }

                    ImU32 col = IM_COL32(0, 175, 0, (int)(a * 96.0f));
                    dl->AddImageQuad(
                        (ImTextureID)texId,
                        c0, c1, c2, c3,
                        uv[0], uv[1], uv[2], uv[3],
                        col);
                }
            }
        }

        /* === Border lines (solid color) === */
        /* Right border */
        dl->AddRectFilled(ImVec2(x + w - 1, bodyTop), ImVec2(x + w, y + h), PdColor(pal->dialog_border2));
        /* Left border */
        dl->AddRectFilled(ImVec2(x, bodyTop), ImVec2(x + 1, y + h), PdColor(pal->dialog_border1));
        /* Bottom border */
        dl->AddRectFilled(ImVec2(x, y + h - 1), ImVec2(x + w, y + h), PdColor(pal->dialog_border1));
    } /* end else (procedural body path) */

    /* === Per-edge body shimmer (four-edge OG cadence) ===
     * Stage 0a (2026-04-30): replaces the prior single perimeter walker.
     * Per Mike's empirical playtest the perimeter variant ran one rectangle
     * every 5s and read as "static and blue, no animation" -- significantly
     * less dense than the OG which ran four independent edge sweeps each
     * cycling ~3.33s with bright alpha. menugfxDrawDialogBorderLine in OG
     * (src/game/menugfx.c:1121-1127) calls menugfxDrawShimmer per edge with
     * width=10, reverse=false. Phase desync comes from (y1+x1) inside the
     * shimmer math itself, so each edge naturally offsets by its position.
     *
     * Runs in BOTH chrome and procedural body paths. When chrome is on, the
     * shimmer overlays the chrome nineslice's outer pixels. When chrome is
     * off, it overlays the 1px border lines drawn above. Either way the
     * sweep is part of PD's visual identity and stays coherent.
     *
     * pdguiDrawShimmerExact above (used by the title bar) already matches
     * menugfxDrawShimmer one-for-one: freq=6 cycles per 20s, edge-length-
     * aware modulo replacing N64's hardcoded 600px, alpha boosted 1.8x and
     * width scaled 2x to compensate for higher PC pixel density. */
    {
        int borderAlpha = pal->dialog_border1 & 0xFF;

        /* Left border edge (vertical sweep, x..x+1, bodyTop..y+h) */
        pdguiDrawShimmerExact(dl, x,         bodyTop,   x + 1, y + h, borderAlpha, 10, false);
        /* Right border edge (vertical sweep, x+w-1..x+w, bodyTop..y+h) */
        pdguiDrawShimmerExact(dl, x + w - 1, bodyTop,   x + w, y + h, borderAlpha, 10, false);
        /* Bottom border edge (horizontal sweep, x..x+w, y+h-1..y+h) */
        pdguiDrawShimmerExact(dl, x,         y + h - 1, x + w, y + h, borderAlpha, 10, false);
    }

    /* Theme effects are scoped to the active chrome element and composite
     * after its nineslice. A declared but unresolved chrome does not fall
     * through to procedural body artwork. */
    if (useChrome && activeChromeId && activeChromeId[0]) {
        pdguiEffectsDrawAll(activeChromeId, x, bodyTop, w, (y + h) - bodyTop,
            (float)chromeDef->dst_left, (float)chromeDef->dst_right,
            (float)chromeDef->dst_top, (float)chromeDef->dst_bottom);
    }
}

/* -----------------------------------------------------------------------
 * Focus highlight -- PD draws focused items with a pulsing background
 *
 * Stage 0b (2026-05-01): blends item_focused_inner with item_focused_outer
 * at 2 Hz, matching the OG menu's selected-row breathing pulse. Previously
 * the port drew a static item_focused_outer rect, so the focused row read
 * as inert. The breathing pulse is one of PD's strongest identity cues.
 *
 * OG reference: menuitem.c:464,873,1157,1228,2317,2474,2498,2505,2736,
 * 2905,3770 all call menuGetSinOscFrac(40) (= sin(40*frac*2pi)/2 + 0.5)
 * with frac = g_20SecIntervalFrac (a 20s linear ramp), giving a 2 Hz
 * oscillator that blends two palette entries on the focused row.
 *
 * Port equivalent: phase from ImGui::GetTime() in seconds; sin period
 * collapses the freq + 20s-period composition into a direct angular freq:
 *   weight = 0.5 + 0.5 * sin(t * 2*pi * 40 / 20)
 *          = 0.5 + 0.5 * sin(t * 4*pi)
 *
 * Per-channel blend: result = outer + weight * (inner - outer). At weight=0
 * we draw the darker item_focused_outer; at weight=1 we draw the brighter
 * item_focused_inner. The pulse breathes the highlight from dim background
 * tint up to bright accent tint twice per second.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiDrawItemHighlight(float x, float y, float w, float h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const float kTwoPi = 6.28318530717958647692f;
    float t = (float)ImGui::GetTime();
    float weight = 0.5f + 0.5f * sinf(t * kTwoPi * 2.0f);  /* 2 Hz */

    unsigned int outer = s_ActivePalette->item_focused_outer;
    unsigned int inner = s_ActivePalette->item_focused_inner;

    auto blendByte = [&](int shift) -> unsigned char {
        int ob = (int)((outer >> shift) & 0xFF);
        int ib = (int)((inner >> shift) & 0xFF);
        int rb = ob + (int)((float)(ib - ob) * weight);
        if (rb < 0)   rb = 0;
        if (rb > 255) rb = 255;
        return (unsigned char)rb;
    };

    unsigned char r = blendByte(24);
    unsigned char g = blendByte(16);
    unsigned char b = blendByte(8);
    unsigned char a = blendByte(0);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h),
                      IM_COL32(r, g, b, a));
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

    /* Window/frame backgrounds -- derived from dialog_bodybg.
     *
     * FIX-C.3: WindowBg is fully transparent (alpha=0).  All PD-styled
     * windows use NoBackground and rely on pdguiDrawPdDialog() or
     * drawPdWindowFrame() for their body fill.  Previously WindowBg had
     * alpha ~0xa0 from dialog_bodybg, which added a SECOND translucent
     * layer on top of the explicit PD dialog fill.  Over repeated menu
     * open/close cycles, the GBI blur overlay (menugfxRenderBgBlur) plus
     * the ImGui WindowBg compounded, making the background progressively
     * more opaque ("menu opacity stacking").
     *
     * ChildBg and PopupBg keep their semi-transparent values because
     * child windows and popups that DON'T use NoBackground still need
     * a visible background for readability (e.g., combo dropdowns). */
    colors[ImGuiCol_WindowBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ChildBg]            = C((pal->dialog_bodybg & 0xFFFFFF00) | 0x66);
    colors[ImGuiCol_PopupBg]            = C((pal->dialog_bodybg & 0xFFFFFF00) | 0xD9);

    /* Modal dim overlay -- heavy so background text is not legible */
    colors[ImGuiCol_ModalWindowDimBg]   = ImVec4(0.0f, 0.0f, 0.0f, 0.75f);

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
    /* Text selection highlight — use border2 (bright accent) instead of the dark
     * item_focused_outer, which is too dim to see against dark frame backgrounds. */
    colors[ImGuiCol_TextSelectedBg]    = C((pal->dialog_border2 & 0xFFFFFF00) | 0x99);

    /* Buttons -- dark body background, border accent on hover/active.
     * PD's menu items sit on the dark body, not on bright colored backgrounds.
     * This keeps buttons readable across all themes including Black & Gold.
     * S306: `button_hover` / `button_active` extension fields override the
     * derived values when non-zero, letting themers tune the highlight. */
    colors[ImGuiCol_Button]             = C((pal->dialog_bodybg & 0xFFFFFF00) | 0x99);
    {
        unsigned int bh = pal->button_hover
            ? pal->button_hover
            : ((pal->dialog_border1 & 0xFFFFFF00) | 0x66);
        unsigned int ba = pal->button_active
            ? pal->button_active
            : ((pal->dialog_border1 & 0xFFFFFF00) | 0xAA);
        colors[ImGuiCol_ButtonHovered]  = C(bh);
        colors[ImGuiCol_ButtonActive]   = C(ba);
    }

    /* Check marks and sliders.
     * S306: `checkbox_checked` palette field (slot 9, previously dead) now
     * drives the checkmark colour when non-zero. Falls back to border2 for
     * compatibility with the 7 built-in palettes (which all leave slot 9
     * equal to border2 or white — both read fine as a checkmark). */
    {
        unsigned int cm = pal->checkbox_checked
            ? (pal->checkbox_checked | 0xFF)
            : (pal->dialog_border2 | 0xFF);
        colors[ImGuiCol_CheckMark]      = C(cm);
    }
    colors[ImGuiCol_SliderGrab]         = C(pal->dialog_border1 | 0xFF);
    colors[ImGuiCol_SliderGrabActive]   = C(pal->dialog_border2 | 0xFF);

    /* Headers (collapsing headers, selectable rows).
     * Issue G: the persistent "selected" tint used to be dialog_bodybg with
     * 60% alpha, which rendered near-invisible on top of the dialog body
     * (it's the body colour itself).  Bot rows, weapon-set rows, and custom
     * weapon-slot combos all rely on Selectable(..., isSel) which draws
     * ImGuiCol_Header when selected -- so the user saw no highlight at all.
     * Switching to dialog_border1 (the accent / border colour) at 40% alpha
     * produces a clearly visible tint that is still dimmer than
     * HeaderHovered (border1 @ 80%) and HeaderActive (border1 @ 100%),
     * preserving the hover/active visual hierarchy. */
    colors[ImGuiCol_Header]             = C((pal->dialog_border1 & 0xFFFFFF00) | 0x66);
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

    /* Scrollbar -- S368: widened + high-contrast grab for controller/mouse
     * visibility across every ImGui menu.  Track is mostly opaque so the
     * scroll region stands out from the body, grab uses the accent color
     * (dialog_border2) at full alpha instead of the dimmer border1, so an
     * idle overflowing list is obviously scrollable without needing hover. */
    colors[ImGuiCol_ScrollbarBg]        = C((pal->dialog_bodybg & 0xFFFFFF00) | 0xCC);
    colors[ImGuiCol_ScrollbarGrab]      = C((pal->dialog_border2 & 0xFFFFFF00) | 0xE0);
    colors[ImGuiCol_ScrollbarGrabHovered] = C((pal->dialog_border2 & 0xFFFFFF00) | 0xF5);
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
    /* S368: ScrollbarSize bumped 12 → 18 so the grab is an easy controller /
     * mouse target and an overflowing list is visibly scrollable without
     * needing hover.  GrabMinSize 10 → 14 keeps the grab proportional. */
    style.ScrollbarSize     = 18.0f;
    style.GrabMinSize       = 14.0f;

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
 * Raw palette access — returns the active palette as a flat u32[15] array.
 * Used by pdgui_theme.cpp for theme draw functions.
 * ----------------------------------------------------------------------- */

extern "C" const void *pdguiGetActivePaletteRaw(void)
{
    return (const void *)s_ActivePalette;
}

extern "C" const void *pdguiGetBuiltinPaletteRaw(int index)
{
    switch (index) {
        case 0: return (const void *)&s_PaletteGrey;
        case 1: return (const void *)&s_PaletteBlue;
        case 2: return (const void *)&s_PaletteRed;
        case 3: return (const void *)&s_PaletteGreen;
        case 4: return (const void *)&s_PaletteWhite;
        case 5: return (const void *)&s_PaletteSilver;
        case 6: return (const void *)&s_PaletteBlackGold;
        default: return nullptr;
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
    s_UsingCustomPalette = false;

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
    if (s_UsingCustomPalette) return -1; /* custom JSON theme */
    return 1;
}

extern "C" void pdguiSetPaletteCustom(const unsigned int *colors15)
{
    if (!colors15) return;
    /* Copy the legacy 15 fields only — the S306 extension tail stays at
     * whatever was last set via pdguiSetPaletteExtensions (or zero for
     * derive-default). Prevents buffer over-read when callers pass a
     * stack-allocated u32[15]. */
    memcpy(&s_PaletteCustom, colors15, 15 * sizeof(unsigned int));
    s_ActivePalette = &s_PaletteCustom;
    s_UsingCustomPalette = true;
    /* S309: title text glow derives from the palette's title_glow slot
     * (see pdguiGetTitleGlow), which falls back to border2. Previously
     * the textGlowColor was a separate s_Theme field that never updated
     * when a custom palette loaded, so custom themes kept the default
     * blue glow. pdguiDrawTextGlow now reads pdguiGetTitleGlow()
     * directly so no manual sync is needed here. */
    pdguiApplyPdStyle();
}

/* S306: write the extension tail of the custom palette. Each argument is
 * 0xRRGGBBAA; pass 0 to keep the derived default at apply time. Theme editor
 * + theme loader call this whenever they pick up the new theme.json fields.
 * Re-applies the ImGui style so live preview updates immediately. */
extern "C" void pdguiSetPaletteExtensions(unsigned int toolbarTint,
                                          unsigned int textPositive,
                                          unsigned int textWarning,
                                          unsigned int buttonHover,
                                          unsigned int buttonActive)
{
    s_PaletteCustom.toolbar_tint  = toolbarTint;
    s_PaletteCustom.text_positive = textPositive;
    s_PaletteCustom.text_warning  = textWarning;
    s_PaletteCustom.button_hover  = buttonHover;
    s_PaletteCustom.button_active = buttonActive;
    if (s_ActivePalette == &s_PaletteCustom) {
        pdguiApplyPdStyle();
    }
}

/* S309: write the S309 tail (title glow + 3 window tints).
 * Called by theme loader + theme editor. Zero = derive at apply time. */
extern "C" void pdguiSetPaletteExtensions2(unsigned int titleGlow,
                                           unsigned int tintSuccess,
                                           unsigned int tintDanger,
                                           unsigned int tintInfo)
{
    s_PaletteCustom.title_glow   = titleGlow;
    s_PaletteCustom.tint_success = tintSuccess;
    s_PaletteCustom.tint_danger  = tintDanger;
    s_PaletteCustom.tint_info    = tintInfo;
    if (s_ActivePalette == &s_PaletteCustom) {
        pdguiApplyPdStyle();
    }
}

/* S306: derive-default helpers — readers that want a sensible color even
 * when the theme doesn't specify one. Fallback rules:
 *   toolbar_tint  -> darker border1 (70% alpha) over body bg
 *   text_positive -> PD-canonical lime (#4cff80)
 *   text_warning  -> PD-canonical amber (#ffd950)
 * All return 0xRRGGBBAA. `asImVec4` variants return ImGui-ready floats. */
extern "C" unsigned int pdguiGetToolbarTint(void)
{
    const struct pdgui_palette *pal = s_ActivePalette;
    if (pal->toolbar_tint) return pal->toolbar_tint;
    /* Derive: blend of dialog_border1 + bodybg at 70% alpha */
    return (pal->dialog_border1 & 0xFFFFFF00u) | 0x70u;
}

extern "C" unsigned int pdguiGetTextPositive(void)
{
    const struct pdgui_palette *pal = s_ActivePalette;
    if (pal->text_positive) return pal->text_positive;
    return 0x4cff80ffu; /* lime */
}

extern "C" unsigned int pdguiGetTextWarning(void)
{
    const struct pdgui_palette *pal = s_ActivePalette;
    if (pal->text_warning) return pal->text_warning;
    return 0xffd950ffu; /* amber */
}

extern "C" unsigned int pdguiGetCheckmarkColor(void)
{
    const struct pdgui_palette *pal = s_ActivePalette;
    return (pal->checkbox_checked ? pal->checkbox_checked : pal->dialog_border2) | 0xFFu;
}

/* S309 extensions: title glow + success/danger/info window tints.
 * Zero in a slot means "derive from existing palette fields at call time",
 * so an older theme.json that doesn't set these still gets a sensible look.
 * Theme Editor exposes each slot with an "auto" reset to clear back to 0. */
extern "C" unsigned int pdguiGetTitleGlow(void)
{
    const struct pdgui_palette *pal = s_ActivePalette;
    if (pal->title_glow) return pal->title_glow;
    /* Derive: bright border2 with full alpha so the title stays legible
     * regardless of palette accent.  Keeps the original "palette follows
     * title glow" behavior that pdguiThemeSetPalette used to enforce. */
    return (pal->dialog_border2 & 0xFFFFFF00u) | 0xFFu;
}

extern "C" unsigned int pdguiGetTintSuccess(void)
{
    const struct pdgui_palette *pal = s_ActivePalette;
    if (pal->tint_success) return pal->tint_success;
    return 0x40c070ffu; /* muted green */
}

extern "C" unsigned int pdguiGetTintDanger(void)
{
    const struct pdgui_palette *pal = s_ActivePalette;
    if (pal->tint_danger) return pal->tint_danger;
    return 0xc04040ffu; /* muted red */
}

extern "C" unsigned int pdguiGetTintInfo(void)
{
    const struct pdgui_palette *pal = s_ActivePalette;
    if (pal->tint_info) return pal->tint_info;
    return (pal->dialog_border1 & 0xFFFFFF00u) | 0xFFu;
}

/* S306: direct title-close consumer. Returns 1 if the X button was clicked
 * on the current or previous frame and resets the flag so the next poll
 * returns 0. Two-frame tolerance covers the case where the X click is
 * processed AFTER the caller's IsKeyPressed-style check fired this frame;
 * the next frame's caller will still see the edge. */
extern "C" s32 pdguiConsumeTitleClose(void)
{
    if (s_TitleCloseFrame < 0) return 0;
    s32 cur = ImGui::GetFrameCount();
    if (s_TitleCloseFrame >= cur - 1) {
        s_TitleCloseFrame = -1;
        return 1;
    }
    /* Stale — clear and return 0. Prevents a click that somehow got
     * stranded (e.g. a renderer that didn't poll last frame) from
     * triggering a later close. */
    s_TitleCloseFrame = -1;
    return 0;
}

extern "C" unsigned int pdguiGetPaletteColor(int index)
{
    if (index < 0 || index >= 15) return 0;
    const unsigned int *pal = (const unsigned int *)s_ActivePalette;
    return pal[index];
}

extern "C" unsigned int pdguiPalImU32(int index, int alpha)
{
    if (index < 0 || index >= 15) return IM_COL32(0, 0, 0, 255);
    const unsigned int *pal = (const unsigned int *)s_ActivePalette;
    unsigned int rgba = pal[index];
    unsigned char r = (rgba >> 24) & 0xFF;
    unsigned char g = (rgba >> 16) & 0xFF;
    unsigned char b = (rgba >>  8) & 0xFF;
    unsigned char a = (alpha >= 0) ? (unsigned char)alpha : (rgba & 0xFF);
    return IM_COL32(r, g, b, a);
}

/* S311: convert a 0xRRGGBBAA palette color into an ImU32 (IM_COL32 packing).
 * Pass alpha >= 0 to override the RGBA alpha byte.  Centralises the shift/mask
 * dance that was previously duplicated across menu renderers. */
extern "C" unsigned int pdguiRgbaToImU32(unsigned int rgba, int alpha)
{
    unsigned char r = (rgba >> 24) & 0xFF;
    unsigned char g = (rgba >> 16) & 0xFF;
    unsigned char b = (rgba >>  8) & 0xFF;
    unsigned char a = (alpha >= 0) ? (unsigned char)alpha : (rgba & 0xFF);
    return IM_COL32(r, g, b, a);
}

/* S311: semantic ImU32 accessors.  Returns the palette's title-glow / success /
 * danger / info tint packed as ImU32 for direct AddText/AddRectFilled use.
 * alpha >= 0 overrides the palette alpha; -1 preserves it. */
extern "C" unsigned int pdguiImU32TitleGlow(int alpha)
{
    return pdguiRgbaToImU32(pdguiGetTitleGlow(), alpha);
}

extern "C" unsigned int pdguiImU32TintSuccess(int alpha)
{
    return pdguiRgbaToImU32(pdguiGetTintSuccess(), alpha);
}

extern "C" unsigned int pdguiImU32TintDanger(int alpha)
{
    return pdguiRgbaToImU32(pdguiGetTintDanger(), alpha);
}

extern "C" unsigned int pdguiImU32TintInfo(int alpha)
{
    return pdguiRgbaToImU32(pdguiGetTintInfo(), alpha);
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
    /* S309: pull the glow color from the palette's title_glow slot so
     * custom themes can override or disable the blue tint.  Falls back
     * to the s_Theme setter if nothing opted in at the palette level. */
    unsigned int gc = pdguiGetTitleGlow();
    if (!(gc & 0xFFFFFF00u)) {
        gc = s_Theme.textGlowColor;
    }
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

/* -----------------------------------------------------------------------
 * Button Edge Glow -- animated shimmer on hovered/active button edges
 *
 * Draws a pulsing glow along each edge of the button rect, using the
 * palette's border accent color. The pulse breathes via sin(time) and
 * individual edge shimmers travel along the border for that classic PD feel.
 * ----------------------------------------------------------------------- */

extern "C" void pdguiDrawButtonEdgeGlow(float x, float y, float w, float h, int isActive)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const struct pdgui_palette *pal = s_ActivePalette;

    /* Extract accent color from border2 (bright accent) */
    unsigned int gc = pal->dialog_border2;
    unsigned char gr = (gc >> 24) & 0xFF;
    unsigned char gg = (gc >> 16) & 0xFF;
    unsigned char gb = (gc >>  8) & 0xFF;

    /* Pulsing alpha: breathing effect via sin wave */
    float t = (float)ImGui::GetTime();
    float pulse = 0.5f + 0.5f * sinf(t * 4.0f);  /* 0..1, ~4 Hz breathing */
    float baseAlphaF = isActive ? (0.6f + 0.4f * pulse) : (0.3f + 0.4f * pulse);
    unsigned char baseAlpha = (unsigned char)(baseAlphaF * 255.0f);

    /* Outer glow: 2 passes at increasing expansion, decreasing alpha */
    for (int pass = 0; pass < 2; pass++) {
        float expand = (float)(pass + 1) * 1.5f;
        unsigned char alpha = (unsigned char)(baseAlpha / (pass + 1));

        /* Top edge */
        dl->AddRectFilled(
            ImVec2(x - expand, y - expand),
            ImVec2(x + w + expand, y),
            IM_COL32(gr, gg, gb, alpha));
        /* Bottom edge */
        dl->AddRectFilled(
            ImVec2(x - expand, y + h),
            ImVec2(x + w + expand, y + h + expand),
            IM_COL32(gr, gg, gb, alpha));
        /* Left edge */
        dl->AddRectFilled(
            ImVec2(x - expand, y),
            ImVec2(x, y + h),
            IM_COL32(gr, gg, gb, alpha));
        /* Right edge */
        dl->AddRectFilled(
            ImVec2(x + w, y),
            ImVec2(x + w + expand, y + h),
            IM_COL32(gr, gg, gb, alpha));
    }

    /* Traveling shimmer along each edge */
    int shimAlpha = (int)(baseAlphaF * 200.0f);
    if (shimAlpha > 255) shimAlpha = 255;

    /* Top edge shimmer (horizontal) */
    pdguiDrawShimmerExact(dl, x, y - 1, x + w, y, shimAlpha, 20, false);
    /* Bottom edge shimmer (horizontal, reverse) */
    pdguiDrawShimmerExact(dl, x, y + h, x + w, y + h + 1, shimAlpha, 20, true);
    /* Left edge shimmer (vertical) */
    pdguiDrawShimmerExact(dl, x - 1, y, x, y + h, shimAlpha, 20, false);
    /* Right edge shimmer (vertical, reverse) */
    pdguiDrawShimmerExact(dl, x + w, y, x + w + 1, y + h, shimAlpha, 20, true);
}
