#ifndef _IN_PDGUI_FONTMGR_H
#define _IN_PDGUI_FONTMGR_H

/**
 * pdgui_fontmgr.h -- TTF font manager with glow/shadow for PD2 UI (P4)
 *
 * Manages font loading from mod packages and provides text rendering
 * effects (glow, shadow) that composite correctly with the theme system.
 *
 * Font lifecycle:
 *   1. pdguiFontMgrInit() — registers default font, sets up config vars
 *   2. pdguiFontMgrLoadFont() — loads a TTF from a mod, adds to ImGui atlas
 *   3. pdguiFontMgrSetActive() — switches the active font for UI rendering
 *   4. pdguiFontMgrDrawText*() — draw text with effects
 *
 * The default font (Handel Gothic) is always available. Mod fonts are
 * loaded additively — they don't replace the default, they extend the atlas.
 *
 * Text effects:
 *   - Shadow: offset copy behind text, configurable color/offset/alpha
 *   - Glow: blurred halo behind text, configurable color/radius/intensity
 *   Both rendered as ImGui draw list primitives (no shaders needed).
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase P4: UI Texture Mod.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Font slot: up to 16 fonts loaded simultaneously
 * --------------------------------------------------------------------- */

#define FONTMGR_MAX_FONTS    16
#define FONTMGR_NAME_LEN     64
#define FONTMGR_PATH_LEN    256

/* -----------------------------------------------------------------------
 * Text shadow definition
 * --------------------------------------------------------------------- */

typedef struct font_shadow_def {
    f32  offset_x;      /* horizontal offset in pixels (positive = right) */
    f32  offset_y;      /* vertical offset in pixels (positive = down) */
    u32  color;         /* 0xRRGGBBAA */
} font_shadow_def_t;

/* -----------------------------------------------------------------------
 * Text glow definition
 * --------------------------------------------------------------------- */

typedef struct font_glow_def {
    f32  radius;        /* glow spread in pixels (0 = off) */
    f32  intensity;     /* 0.0 = off, 1.0 = full brightness */
    u32  color;         /* 0xRRGGBBAA */
    s32  passes;        /* number of glow passes (1-4, more = smoother) */
} font_glow_def_t;

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/** Initialize font manager. Call after pdguiInit() has set up ImGui context.
 *  Registers the default Handel Gothic as slot 0. */
void pdguiFontMgrInit(void);

/** Shutdown: font memory is owned by ImGui atlas, just clear our metadata. */
void pdguiFontMgrShutdown(void);

/* -----------------------------------------------------------------------
 * Font loading
 * --------------------------------------------------------------------- */

/** Load a TTF font from a file path and register it with a display name.
 *  size_px: rasterization size in pixels (24.0 recommended).
 *  Returns font slot index (1+) on success, 0 on failure.
 *  NOTE: Loading a new font requires rebuilding the ImGui font atlas,
 *  which invalidates the current frame. Call between frames only. */
s32 pdguiFontMgrLoadFont(const char *name, const char *ttf_path, f32 size_px);

/** Get the number of loaded fonts (including default). */
s32 pdguiFontMgrGetCount(void);

/** Get the display name of font at slot index. NULL if out of range. */
const char *pdguiFontMgrGetName(s32 slot);

/** Get the catalog ID for a font slot. NULL if out of range. */
const char *pdguiFontMgrGetCatalogId(s32 slot);

/* -----------------------------------------------------------------------
 * Active font selection
 * --------------------------------------------------------------------- */

/** Set the active font by slot index. 0 = default (Handel Gothic). */
void pdguiFontMgrSetActive(s32 slot);

/** Get the current active font slot. */
s32 pdguiFontMgrGetActive(void);

/** Push the font at the given slot onto ImGui's font stack.
 *  Must be matched with pdguiFontMgrPopFont(). */
void pdguiFontMgrPushFont(s32 slot);

/** Pop the font stack. */
void pdguiFontMgrPopFont(void);

/* -----------------------------------------------------------------------
 * Text effect configuration
 * --------------------------------------------------------------------- */

/** Set the global text shadow parameters. Applies to all DrawText* calls. */
void pdguiFontMgrSetShadow(const font_shadow_def_t *def);

/** Get the current shadow definition. */
const font_shadow_def_t *pdguiFontMgrGetShadow(void);

/** Set the global text glow parameters. Applies to all DrawText* calls. */
void pdguiFontMgrSetGlow(const font_glow_def_t *def);

/** Get the current glow definition. */
const font_glow_def_t *pdguiFontMgrGetGlow(void);

/* -----------------------------------------------------------------------
 * Text rendering with effects
 *
 * These draw text with shadow and/or glow effects applied.
 * Text is always the topmost compositing layer.
 * --------------------------------------------------------------------- */

/** Draw text with shadow + glow at the given position.
 *  color: 0xRRGGBBAA text color. */
void pdguiFontMgrDrawText(float x, float y, u32 color, const char *text);

/** Draw text centered horizontally within a rectangle. */
void pdguiFontMgrDrawTextCentered(float x, float y, float w, float h,
                                  u32 color, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_FONTMGR_H */
