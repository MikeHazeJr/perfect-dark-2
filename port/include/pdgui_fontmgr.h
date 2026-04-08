#ifndef _IN_PDGUI_FONTMGR_H
#define _IN_PDGUI_FONTMGR_H

/**
 * pdgui_fontmgr.h -- TTF font loading and text effects (P4)
 *
 * Manages mod-provided TTF fonts loaded through the ImGui font API.
 * Supports per-font glow radius+color and shadow offset+color.
 * Fonts are registered as catalog assets (base:font_*, mod:font_*).
 * Falls back to built-in Handel Gothic if mod font is missing.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------- */

#define FONTMGR_MAX_FONTS    16
#define FONTMGR_NAME_LEN     64
#define FONTMGR_PATH_LEN     256
#define FONTMGR_CATID_LEN    64

/* -----------------------------------------------------------------------
 * Font entry — describes a loaded or pending font
 * --------------------------------------------------------------------- */

typedef struct {
    char  catalog_id[FONTMGR_CATID_LEN];  /* e.g., "base:font_handelgothic" */
    char  name[FONTMGR_NAME_LEN];         /* display name */
    char  filepath[FONTMGR_PATH_LEN];     /* path to .ttf (empty = built-in) */
    f32   size;                            /* point size (default 24.0) */
    f32   glowRadius;                      /* glow blur radius, 0=off */
    u32   glowColor;                       /* glow color 0xRRGGBBAA */
    f32   shadowOffsetX;                   /* shadow X offset in pixels */
    f32   shadowOffsetY;                   /* shadow Y offset in pixels */
    u32   shadowColor;                     /* shadow color 0xRRGGBBAA */
    s32   loaded;                          /* 1 if ImFont* is valid */
    void *imfont;                          /* ImFont* (opaque from C) */
} pdgui_font_entry_t;

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/** Initialize font manager: register built-in font as catalog asset.
 *  Call after pdguiInit() and catalogInit(). */
void pdguiFontMgrInit(void);

/** Shutdown: clear font registry (ImGui owns actual font memory). */
void pdguiFontMgrShutdown(void);

/* -----------------------------------------------------------------------
 * Font registration & loading
 * --------------------------------------------------------------------- */

/** Register a font from a TTF file path.
 *  Does not load immediately — call pdguiFontMgrBuildAtlas() after all
 *  registrations to rebuild the ImGui font atlas.
 *  Returns the font entry index, or -1 on failure. */
s32 pdguiFontMgrRegister(const char *catalog_id, const char *name,
                          const char *filepath, f32 size);

/** Rebuild the ImGui font atlas with all registered fonts.
 *  Must be called after registration and before rendering.
 *  Internally calls ImGui::GetIO().Fonts->Build(). */
void pdguiFontMgrBuildAtlas(void);

/** Load a font from a catalog ID. Returns the ImFont* or NULL. */
void *pdguiFontMgrGetFont(const char *catalog_id);

/** Get the built-in default font. */
void *pdguiFontMgrGetDefault(void);

/* -----------------------------------------------------------------------
 * Font enumeration
 * --------------------------------------------------------------------- */

s32  pdguiFontMgrGetCount(void);
const pdgui_font_entry_t *pdguiFontMgrGetEntry(s32 index);

/* -----------------------------------------------------------------------
 * Text effect rendering
 * --------------------------------------------------------------------- */

/** Draw text with font-specific glow and shadow effects.
 *  Uses the font's configured glow/shadow settings.
 *  If font_catalog_id is NULL, uses the default font. */
void pdguiFontMgrDrawText(const char *font_catalog_id,
                           f32 x, f32 y, u32 color,
                           const char *text);

/** Set glow parameters for a registered font. */
void pdguiFontMgrSetGlow(const char *catalog_id,
                          f32 radius, u32 color);

/** Set shadow parameters for a registered font. */
void pdguiFontMgrSetShadow(const char *catalog_id,
                            f32 offsetX, f32 offsetY, u32 color);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_FONTMGR_H */
