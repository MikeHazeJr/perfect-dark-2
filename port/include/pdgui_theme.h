#ifndef _IN_PDGUI_THEME_H
#define _IN_PDGUI_THEME_H

/**
 * pdgui_theme.h -- PD menu visual theme layer (D5.0)
 *
 * Two-stage lifecycle:
 *   1. pdguiThemeInit()      -- early init, called from pdguiInit()
 *   2. pdguiThemeLateInit()  -- loads textures from base-ui mod or procedural
 *                               fallbacks, called after texInit()/texReset()
 *
 * Texture pipeline: mod TGA files → GL upload → s_ThemeTexCache → draw funcs
 * Procedural fallback: generate noise/solid textures when mod files missing
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

/** Early init: marks theme as initialized. No texture loading.
 *  Called from pdguiInit() after OpenGL context is ready. */
void pdguiThemeInit(void);

/** Late init: loads UI textures from base-ui mod TGA files, or generates
 *  procedural fallbacks. Called after texInit()/texReset() have run.
 *  Safe to call more than once (no-op after first call). */
void pdguiThemeLateInit(void);

/** Shutdown: delete GL textures, clear theme cache. */
void pdguiThemeShutdown(void);

/* -----------------------------------------------------------------------
 * Texture bridge
 * --------------------------------------------------------------------- */

/** Return ImTextureID for a named UI texture.
 *  Returns NULL gracefully if late init hasn't run or texture not found. */
void *pdguiThemeGetTexture(const char *catalog_id);

/** Look up the source-texture dimensions (width/height in pixels) for a
 *  cached theme texture.  Returns 1 on success with *out_w/*out_h set,
 *  0 if the texture is not cached or the id is unknown.  Either pointer
 *  may be NULL if the caller only cares about the other dimension. */
s32 pdguiThemeGetTextureSize(const char *catalog_id, u32 *out_w, u32 *out_h);

/* -----------------------------------------------------------------------
 * Background texture config
 * --------------------------------------------------------------------- */

/** Get/set the catalog ID for dialog background haze overlay.
 *  NULL = no overlay (solid fill only). Default: "base:ui_bg_haze". */
const char *pdguiThemeGetBgTexId(void);
void pdguiThemeSetBgTexId(const char *catalog_id);

/* -----------------------------------------------------------------------
 * Scanline config
 * --------------------------------------------------------------------- */

/** Enable/disable CRT scanline overlay. Default: enabled. */
void pdguiThemeSetScanlineEnabled(s32 enabled);
s32  pdguiThemeGetScanlineEnabled(void);

/** Scanline opacity: 0.0 = invisible, 1.0 = max darkening (~16%). Default: 0.8 */
void pdguiThemeSetScanlineAlpha(f32 alpha);
f32  pdguiThemeGetScanlineAlpha(void);

/** Scanline vertical stride multiplier. 1.0 = default pattern. Range 0.25..4.0.
 * Lower = denser lines, higher = wider spacing. Saved to pd.ini as
 * Video.ScanlineVerticalScale. 2026-04-23. */
void pdguiThemeSetScanlineVerticalScale(f32 scale);
f32  pdguiThemeGetScanlineVerticalScale(void);

/* -----------------------------------------------------------------------
 * Palette bridge (theme → style layer)
 * --------------------------------------------------------------------- */

/** Return the active palette as a flat array of 15 u32 values (0xRRGGBBAA).
 *  Delegates to pdguiGetPalette() in pdgui_style.cpp. */
const u32 *pdguiThemeGetActivePaletteColors(void);

/* -----------------------------------------------------------------------
 * Draw functions  (ImGui draw-list based)
 *
 * All coordinates are in ImGui SCREEN-SPACE pixels.
 * --------------------------------------------------------------------- */

void pdguiThemeDrawPanel(float x, float y, float w, float h,
                         const char *bg_tex_id);

void pdguiThemeDrawBorder(float x, float y, float w, float h,
                          s32 palette_idx);

void pdguiThemeDrawHeader(float x, float y, float w, float h,
                          const char *title, s32 palette_idx);

void pdguiThemeDrawButton(float x, float y, float w, float h, s32 focused);

void pdguiThemeDrawStars(float x, float y, s32 filled, s32 total);

void pdguiThemeDrawScanline(float x, float y, float w, float h, float alpha);

/** Scanline on foreground draw list (renders on top of all content). */
void pdguiThemeDrawScanlineFg(float x, float y, float w, float h);

/* -----------------------------------------------------------------------
 * ROM Texture Extraction Tool
 * --------------------------------------------------------------------- */

/** Extract ROM UI textures to mods/base-ui/textures/ as TGA files.
 *  Must be called after texReset() has run. Triggered by --extract-ui-textures. */
void pdguiThemeExtractRomTextures(void);

/** Frame check: run extraction once if --extract-ui-textures is set and
 *  g_TexGeneralConfigs is available. Called from pdguiRender(). */
void pdguiThemeCheckExtract(void);

/** Initialize the base-game UI chrome template mod (S196).
 *  Idempotent; safe to call multiple times.  Generates programmatic test
 *  chrome assets in mods/base-game/ui-chrome/, writes the template-flagged
 *  mod.json + README.md, loads the texture into the theme cache, and
 *  registers the nineslice under catalog id "base:ui_chrome_frame".
 *  Called from pdguiThemeCheckExtract(). */
void pdguiChromeInitializeBaseMod(void);

/** Get/set the persisted UI chrome toggle.  Backed by "Video.UiChromeEnabled"
 *  in pd.ini.  The Settings -> Video dropdown calls the setter; setting this
 *  directly does NOT hot-apply — pair with pdguiChromeSetEnabled() from
 *  pdgui_style.h to drive the render branch. */
void pdguiThemeSetUiChromeEnabled(s32 enabled);
s32  pdguiThemeGetUiChromeEnabled(void);

/** Persisted selected chrome style catalog ID (for nineslice-based chrome).
 *  Defaults to "base:ui_chrome_frame". */
void pdguiThemeSetUiChromeStyleId(const char *catalog_id);
const char *pdguiThemeGetUiChromeStyleId(void);

/** Enumerate discovered chrome styles (nineslice + texture registrations).
 *  These are populated from the base template and user chrome mods that
 *  declare components.textures + components.nineslice in mod.json. */
s32 pdguiThemeGetChromeStyleCount(void);
const char *pdguiThemeGetChromeStyleId(s32 index);
const char *pdguiThemeGetChromeStyleName(s32 index);

/** Runtime chrome mod hooks (for importer/save flows).
 * Register + optionally activate a chrome mod directory that contains
 * `mod.json` with `components.textures` + `components.nineslice`.
 * Returns 1 on success, 0 on no valid chrome manifest. */
s32 pdguiThemeRegisterChromeModDir(const char *mod_dir, s32 activate_now);

/** Rescan known mod roots for chrome styles (used after bulk imports). */
void pdguiThemeRescanChromeStyles(void);

/* -----------------------------------------------------------------------
 * UI Texture Mod Override API (D5 Phase 4)
 *
 * Scan a single mod directory for type="ui" component textures in mod.json
 * and register any catalog_id/path pairs as overrides.  Returns the number
 * of textures registered (0 if none or mod.json absent).
 * pdguiThemeApplyEnabledModUiTextures iterates all enabled mods and calls
 * pdguiThemeScanModUiTextures for each one. Called from pdguiThemeLateInit
 * and modmgrApplyChanges.
 * --------------------------------------------------------------------- */
s32  pdguiThemeScanModUiTextures(const char *mod_dir);
void pdguiThemeApplyEnabledModUiTextures(void);

/* -----------------------------------------------------------------------
 * Content inset API (S297)
 *
 * Tells renderers how much to pull their content in from the outer dialog
 * bounds so nothing overlaps the border / frame artwork.  When chrome is
 * off, the procedural dialog has 1–2 px borders so the inset is tiny.
 * When chrome is on, the active nineslice def's destination corners drive
 * the inset.  Callers should add their own title-bar height if they are
 * drawing above the body (most ImGui windows already subtract title bar
 * via client area).
 *
 * Any pointer may be NULL if the caller only cares about a subset.
 * --------------------------------------------------------------------- */
void pdguiThemeGetContentInset(float *out_l, float *out_r,
                               float *out_t, float *out_b);

/** Shrink a rect by the active content inset.  x/y/w/h must be non-NULL. */
void pdguiThemeApplyContentInset(float *x, float *y, float *w, float *h);

/** Resolve padding that clears the nineslice chrome border + 8px breathe.
 *  Returns max(base, inset+breathe) per edge.  Pass title_h=0 if no title.
 *  Any out_* may be NULL. */
void pdguiThemeResolveContentPad(float title_h,
                                 float base_l, float base_t,
                                 float base_r, float base_b,
                                 float *out_l, float *out_t,
                                 float *out_r, float *out_b);

/** Shorthand: set ImGui cursor below title bar at chrome-safe origin.
 *  Replaces `ImGui::SetCursorPosY(title_h + WindowPadding.y)`. */
void pdguiSetCursorBelowTitle(float title_h);

/* -----------------------------------------------------------------------
 * Title-bar style (S297)
 *
 * Procedural title-bar options. Chosen via `Video.UiTitleBarStyle` in
 * pd.ini. Affects pdguiDrawPdDialog() title rendering only; the gradient
 * shimmer and close-button overlay still run on top.
 *
 * 0 = Classic gradient   (PD default: titlebg→border1→titlebg)
 * 1 = Solid               (border1 flat fill)
 * 2 = Vertical bars       (titlebg base + subtle vertical stripes)
 * 3 = Horizontal scanlines(classic gradient + scanline overlay)
 * 4 = Diagonal stripes    (border1 base + 45° gradient bars)
 * --------------------------------------------------------------------- */
enum {
    PDGUI_TITLEBAR_CLASSIC      = 0,
    PDGUI_TITLEBAR_SOLID        = 1,
    PDGUI_TITLEBAR_VERT_BARS    = 2,
    PDGUI_TITLEBAR_SCANLINES    = 3,
    PDGUI_TITLEBAR_DIAG_STRIPES = 4,
    PDGUI_TITLEBAR_STYLE_COUNT  = 5,
};

void pdguiThemeSetTitleBarStyle(s32 style);
s32  pdguiThemeGetTitleBarStyle(void);
const char *pdguiThemeGetTitleBarStyleName(s32 style);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_THEME_H */
