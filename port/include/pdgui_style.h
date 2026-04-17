#ifndef _IN_PDGUI_STYLE_H
#define _IN_PDGUI_STYLE_H

/**
 * pdgui_style.h -- PD-authentic ImGui style API.
 *
 * Exposes the PD-style rendering functions and palette system to
 * pdgui_backend.cpp, pdgui_debugmenu.cpp, and future menu code.
 *
 * Palette indices match the original g_MenuColours[] array:
 *   0 = Grey (background/faded dialogs)
 *   1 = Blue (default PD look)
 *   2 = Red  (Combat Simulator / enemy)
 *   3 = Green
 *   6 = Black & Gold (campaign completion reward -- new for PD2)
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Apply the active palette's colors and PD metrics to ImGui's style.
 * Called once at init and again whenever the palette changes. */
void pdguiApplyPdStyle(void);

/* Draw a complete PD-style dialog frame (title gradient, body, borders, shimmer).
 * Uses the active palette. Call for windows that need the full PD look. */
void pdguiDrawPdDialog(float x, float y, float w, float h,
                        const char *title, s32 focused);

/* Draw PD-style focus highlight behind a menu item. */
void pdguiDrawItemHighlight(float x, float y, float w, float h);

/* Add shimmer overlay to the current ImGui window's borders.
 * Call after ImGui::Begin() to add animated shimmer to any window. */
void pdguiRenderWindowBg(void);

/* Iterate all active ImGui windows and add PD-style shimmer to their borders.
 * Call once per frame after all windows have been submitted, before Render(). */
void pdguiRenderAllWindowShimmers(void);

/* --- Palette API --- */

/* Switch the active color palette. Re-applies ImGui style automatically.
 * Index values: 0=Grey, 1=Blue, 2=Red, 3=Green, 6=BlackGold */
void pdguiSetPalette(s32 index);

/* Get the current palette index. */
s32 pdguiGetPalette(void);

/* --- Theme API --- */

/* Set palette and auto-configure glow color from palette accent. */
void pdguiThemeSetPalette(s32 index);

/* Set tint/burn: strength 0.0=pure palette, 1.0=full tint. Color is 0xRRGGBBAA. */
void pdguiThemeSetTint(f32 strength, u32 color);
f32  pdguiThemeGetTintStrength(void);

/* Text glow: intensity 0.0=off, 1.0=full. Color is 0xRRGGBBAA. */
void pdguiThemeSetTextGlow(f32 intensity, u32 color);
f32  pdguiThemeGetTextGlowIntensity(void);
u32  pdguiThemeGetTextGlowColor(void);

/* Sound FX pack: 0=default PD sounds. Future: alternate packs. */
void pdguiThemeSetSoundPack(s32 pack);
s32  pdguiThemeGetSoundPack(void);

/* Draw a soft colored glow behind text at the given position/size. */
void pdguiDrawTextGlow(f32 x, f32 y, f32 textW, f32 textH);

/* Draw animated edge glow on a hovered/active button.
 * Call after the button is drawn. Uses palette accent colors. */
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

/* Return the active palette as a raw pointer to u32[15] (0xRRGGBBAA format).
 * Used by pdgui_theme.cpp for theme draw functions. */
const void *pdguiGetActivePaletteRaw(void);

/* --- Palette field indices (matches struct pdgui_palette layout) --- */
#define PDPAL_BORDER1        0   /* bright border (left, bottom) */
#define PDPAL_TITLEBG        1   /* dark title bar background */
#define PDPAL_BORDER2        2   /* accent border (right) */
#define PDPAL_TITLEFG        3   /* title text */
#define PDPAL_BODYBG         4   /* body background fill */
#define PDPAL_ITEM_UNFOCUSED 6   /* normal menu item text */
#define PDPAL_ITEM_DISABLED  7   /* greyed out item text */
#define PDPAL_ITEM_FOCUSED   8   /* focused/hovered item text */
#define PDPAL_CHECKBOX       9   /* checked checkbox */
#define PDPAL_FOCUS_BG      10   /* focused item background */
#define PDPAL_LISTHDR_BG    11   /* list group header bg */
#define PDPAL_LISTHDR_FG    12   /* list group header fg */

/* Set a custom palette from raw u32[15] (0xRRGGBBAA).
 * Used by pdgui_theme_loader.cpp for JSON-loaded themes.
 * Re-applies ImGui style automatically. */
void pdguiSetPaletteCustom(const u32 *colors15);

/* S306: write the extension tail (toolbar tint, positive/warning text,
 * button hover/active). Pass 0 for any field to keep the derive-default
 * (see pdguiGetToolbarTint / pdguiGetTextPositive / pdguiGetTextWarning
 * for the fallback rules). No-op unless the custom palette is active. */
void pdguiSetPaletteExtensions(u32 toolbarTint, u32 textPositive,
                               u32 textWarning, u32 buttonHover,
                               u32 buttonActive);

/* S306: themable semantic color accessors. Return 0xRRGGBBAA. The first
 * three fall back to PD-canonical defaults if the theme didn't specify. */
u32 pdguiGetToolbarTint(void);
u32 pdguiGetTextPositive(void);
u32 pdguiGetTextWarning(void);
u32 pdguiGetCheckmarkColor(void);

/* S306: direct-signal close channel driven by the title-bar X button.
 * Returns 1 if the X button was clicked on this or the previous frame
 * (and resets the flag). Callers use this in addition to IsKeyPressed(
 * Escape) so the X click works even when ImGui's nav pops the Escape
 * edge before the renderer sees it. See pdgui_style.cpp for rationale. */
s32 pdguiConsumeTitleClose(void);

/* Return a palette color by index (0-14) from the active palette.
 * Returns 0 if index is out of range. Format: 0xRRGGBBAA. */
u32 pdguiGetPaletteColor(s32 index);

/* Return a palette color as ImU32 (ImGui packed ABGR) with optional alpha override.
 * If alpha < 0, uses the palette color's own alpha. */
u32 pdguiPalImU32(s32 index, s32 alpha);

/* --- P4: 9-slice panel texture API (chrome rendering) ---
 *
 * The chrome system lets pdguiDrawPdDialog swap its procedural body fill
 * + border lines for a nineslice texture driven by a catalog ID.  Two
 * pieces of state drive it:
 *   1. An enabled flag (global on/off).
 *   2. The active chrome catalog ID (which nineslice to draw).
 *
 * Both must be set for chrome to render.  With enabled=false the original
 * procedural render path runs exactly as before (pixel-for-pixel parity). */

/* Enable or disable chrome rendering globally.
 * Persisted to pd.ini by the Settings -> Video dropdown. */
void pdguiChromeSetEnabled(s32 enabled);
s32  pdguiChromeIsEnabled(void);

/* Set the active chrome nineslice by catalog ID.  The ID must reference
 * a nineslice previously registered via pdguiNinesliceRegister() and a
 * texture registered via pdguiThemeRegisterTexture().  Pass NULL to
 * clear the active selection.  The string is copied. */
void pdguiSetPanelNineSlice(const char *nineslice_catalog_id);
const char *pdguiGetPanelNineSlice(void);

/* Convenience wrapper: clear the active chrome selection. */
void pdguiClearPanelNineSlice(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_STYLE_H */
