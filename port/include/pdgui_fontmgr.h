#ifndef _IN_PDGUI_FONTMGR_H
#define _IN_PDGUI_FONTMGR_H

/**
 * pdgui_fontmgr.h -- TTF font loading with glow/shadow support (P4)
 *
 * Manages custom fonts loaded from mod directories via ImGui's font API.
 * Each font can have per-font glow radius/color and shadow offset/color.
 * Fonts are catalog assets — mods can provide custom fonts.
 * Falls back to the built-in Handel Gothic if a mod font fails to load.
 *
 * Usage:
 *   1. pdguiFontMgrInit() at startup (after ImGui context creation)
 *   2. pdguiFontMgrLoadFromMod() for each mod font
 *   3. pdguiFontMgrApply() to select a font for rendering
 *   4. pdguiFontMgrDrawTextWithEffects() for glow+shadow text
 *
 * Part of Phase P4: UI Texture Mod.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Font entry configuration
 * --------------------------------------------------------------------- */

/** Per-font effect configuration. */
typedef struct PdguiFontConfig {
    f32 glow_radius;      /* Glow blur radius in pixels (0 = no glow) */
    u32 glow_color;       /* Glow color in 0xRRGGBBAA */
    f32 shadow_offset_x;  /* Shadow offset X pixels (0 = no shadow) */
    f32 shadow_offset_y;  /* Shadow offset Y pixels */
    u32 shadow_color;     /* Shadow color in 0xRRGGBBAA */
    f32 size_pt;          /* Font size in points (0 = use default 24pt) */
} PdguiFontConfig;

/** Default font config: moderate glow, 1px drop shadow. */
PdguiFontConfig pdguiFontConfigDefault(void);

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/** Initialize font manager. Call after ImGui context creation.
 *  The built-in Handel Gothic remains io.FontDefault. */
void pdguiFontMgrInit(void);

/** Shutdown: clean up font registry (ImGui owns the font atlas memory). */
void pdguiFontMgrShutdown(void);

/* -----------------------------------------------------------------------
 * Font loading
 * --------------------------------------------------------------------- */

/**
 * Load a TTF font from a file path and register it.
 *
 * @param catalog_id   Catalog ID for this font (e.g., "mod:font_cyberpunk")
 * @param filepath     Path to .ttf file (loaded via fsFileLoad)
 * @param config       Per-font effect settings (glow, shadow)
 * @return 1 on success, 0 on failure (built-in font used as fallback)
 *
 * Note: After loading fonts, ImGui's font atlas must be rebuilt.
 * Call pdguiFontMgrRebuildAtlas() after all fonts are loaded.
 */
s32 pdguiFontMgrLoadFont(const char *catalog_id, const char *filepath,
                         const PdguiFontConfig *config);

/**
 * Rebuild the ImGui font atlas after loading new fonts.
 * Must be called once after all pdguiFontMgrLoadFont() calls.
 * This invalidates the previous atlas texture.
 */
void pdguiFontMgrRebuildAtlas(void);

/* -----------------------------------------------------------------------
 * Font selection
 * --------------------------------------------------------------------- */

/**
 * Push a registered font onto ImGui's font stack.
 * @param catalog_id  Font catalog ID (NULL = use default Handel Gothic)
 * @return 1 if font was pushed, 0 if not found (no push occurs)
 *
 * Must be paired with pdguiFontMgrPopFont().
 */
s32 pdguiFontMgrPushFont(const char *catalog_id);

/** Pop the font pushed by pdguiFontMgrPushFont(). */
void pdguiFontMgrPopFont(void);

/* -----------------------------------------------------------------------
 * Text rendering with effects
 * --------------------------------------------------------------------- */

/**
 * Draw text with glow and shadow effects using the active font's config.
 *
 * @param x, y       Screen-space position
 * @param text       Text string to render
 * @param text_color Text color as 0xRRGGBBAA
 *
 * Draws in order: glow → shadow → text (back to front).
 * Uses the effect settings of whatever font was last pushed.
 */
void pdguiFontMgrDrawTextWithEffects(f32 x, f32 y, const char *text, u32 text_color);

/**
 * Draw text with explicit effect config override.
 */
void pdguiFontMgrDrawTextEx(f32 x, f32 y, const char *text, u32 text_color,
                            const PdguiFontConfig *config);

/* -----------------------------------------------------------------------
 * Registry query
 * --------------------------------------------------------------------- */

/** Get number of registered custom fonts. */
s32 pdguiFontMgrGetCount(void);

/** Get catalog ID of the Nth registered font. NULL if out of range. */
const char *pdguiFontMgrGetId(s32 index);

/** Get display name (from TTF metadata or filename) of the Nth font. */
const char *pdguiFontMgrGetName(s32 index);

/** Get the effect config for a registered font. NULL if not found. */
const PdguiFontConfig *pdguiFontMgrGetConfig(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_FONTMGR_H */
