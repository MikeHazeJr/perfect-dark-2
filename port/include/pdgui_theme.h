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

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_THEME_H */
