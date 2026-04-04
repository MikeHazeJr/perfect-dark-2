#ifndef _IN_PDGUI_THEME_H
#define _IN_PDGUI_THEME_H

/**
 * pdgui_theme.h -- PD menu visual theme layer (D5.0)
 *
 * ROM texture decode pipeline + ImGui draw-list-based theme functions.
 * Replaces the D5.0a test-pattern placeholder from pdgui_backend.cpp.
 *
 * Architecture:
 *   1. pdguiThemeInit()          -- decode known ROM UI textures → GL,
 *                                   register as ASSET_UI in catalog
 *   2. pdguiThemeGetTexture()    -- catalog ID → cached ImTextureID (GLuint*)
 *   3. pdguiThemeDraw*()         -- ImGui draw-list theme primitives
 *
 * ROM textures are always available (base game, ROM-backed).
 * NULL return from pdguiThemeGetTexture() is a pipeline bug:
 *   LOG_ERROR + assert.  No solid-color fallbacks for base content.
 * Mod textures fall back to base via catalog.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase D5: Menu System Visual Layer.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/**
 * Initialize theme: decode known ROM UI textures → GL, register
 * ASSET_UI entries in the asset catalog.
 * Called from pdguiInit() after OpenGL context is ready.
 * Safe to call more than once (no-op after first call).
 */
void pdguiThemeInit(void);

/**
 * Shutdown: delete GL textures, clear theme cache.
 * Called from pdguiShutdown() before ImGui context is destroyed.
 */
void pdguiThemeShutdown(void);

/* -----------------------------------------------------------------------
 * Texture bridge  (replaces D5.0a pdguiGetUiTexture test-pattern)
 * --------------------------------------------------------------------- */

/**
 * Return ImTextureID (GLuint cast to void*) for a named UI texture.
 * Texture must have been registered by pdguiThemeInit().
 *
 * NULL return means one of:
 *   a) catalog_id is unknown            -- pipeline bug: add to pdguiThemeInit()
 *   b) texture is texnum-based, not yet decoded via GBI pipeline
 *
 * Both cases LOG_ERROR + assert(false).
 * No fallback is provided: base game textures must always be available.
 */
void *pdguiThemeGetTexture(const char *catalog_id);

/* -----------------------------------------------------------------------
 * Draw functions  (ImGui draw-list based)
 *
 * All coordinates are in ImGui SCREEN-SPACE pixels (not window-relative).
 * Call these from within a ImGui::Begin() / ImGui::End() block, using
 * ImGui::GetWindowDrawList() for the draw list.
 * --------------------------------------------------------------------- */

/**
 * Draw a PD-style panel body fill (dark semi-transparent navy).
 * Optionally composites a ROM texture over the fill at low opacity.
 *   bg_tex_id -- catalog ID for background texture, or NULL for solid fill.
 */
void pdguiThemeDrawPanel(float x, float y, float w, float h,
                         const char *bg_tex_id);

/**
 * Draw PD-style left + right + bottom border lines in active palette colors.
 * Mirrors menugfxDrawDialogBorderLine():
 *   left/bottom: dialog_border1, right: dialog_border2.
 *   palette_idx: 0=Grey, 1=Blue (default), 2=Red, 3=Green, 6=BlackGold.
 *                Pass -1 to use the currently active global palette.
 */
void pdguiThemeDrawBorder(float x, float y, float w, float h,
                          s32 palette_idx);

/**
 * Draw the title gradient bar across the top of a panel.
 * Gradient: dialog_titlebg (top) → dialog_bodybg (bottom).
 * Title text is drawn centered, white, with a 1px drop shadow.
 */
void pdguiThemeDrawHeader(float x, float y, float w, float h,
                          const char *title, s32 palette_idx);

/**
 * Draw button background for focused or unfocused state.
 *   focused != 0: item_focused_outer fill + edge glow.
 *   focused == 0: transparent (body fill shows through).
 */
void pdguiThemeDrawButton(float x, float y, float w, float h, s32 focused);

/**
 * Draw PD-style star rating indicators.
 *   filled  -- number of filled (gold) stars
 *   total   -- total number of stars
 * Each star is a 5-pointed polygon; empties are dim outlines.
 * Coordinates are the top-left origin of the star row.
 */
void pdguiThemeDrawStars(float x, float y, s32 filled, s32 total);

/**
 * Draw subtle horizontal scanlines over a region (retro-CRT effect).
 *   alpha: 0.0 = invisible, 1.0 = maximum darkening (~16% at alpha=1).
 * One scanline per 2px row.
 */
void pdguiThemeDrawScanline(float x, float y, float w, float h, float alpha);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_THEME_H */
