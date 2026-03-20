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

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_STYLE_H */
