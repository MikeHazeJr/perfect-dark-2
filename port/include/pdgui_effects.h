#ifndef _IN_PDGUI_EFFECTS_H
#define _IN_PDGUI_EFFECTS_H

/**
 * pdgui_effects.h -- Animated overlay effects for UI elements (P4)
 *
 * Provides two compositable effect systems:
 *
 * 1. Caustic/Animated Mask Overlay:
 *    A grayscale mask texture that scrolls over time and composites onto
 *    UI elements. Used for water caustics, energy fields, scan distortion.
 *    - Time-based UV scrolling (configurable speed + direction)
 *    - Alpha-blended mask composite
 *    - Configurable speed, scale, tint color
 *
 * 2. Border Effect Mask:
 *    Decorative animated effects applied to UI element edges.
 *    - Glow pulse on focused elements
 *    - Gradient sweep on active/selected
 *    - Configurable per theme via theme.json
 *
 * Compositing order (back to front):
 *   base texture → 9-slice render → border effect → caustic overlay → text
 *
 * All effects use delta time for frame-rate independence (60Hz compatible).
 *
 * Part of Phase P4: UI Texture Mod.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Caustic / Animated Mask Overlay
 * --------------------------------------------------------------------- */

/** Configuration for a caustic mask effect. */
typedef struct PdguiCausticConfig {
    f32 scroll_speed_x;   /* UV scroll speed, texels/sec (default: 0.02) */
    f32 scroll_speed_y;   /* UV scroll speed Y (default: 0.015) */
    f32 scale;            /* UV scale factor (default: 1.0, higher = more tiled) */
    f32 opacity;          /* Base opacity 0..1 (default: 0.15) */
    u32 tint_color;       /* Tint in 0xRRGGBBAA (default: 0xFFFFFFFF = no tint) */
    s32 blend_additive;   /* 1 = additive blend, 0 = alpha blend (default: 0) */
} PdguiCausticConfig;

/** Default caustic config (subtle, slow water-like scroll). */
PdguiCausticConfig pdguiCausticDefaultConfig(void);

/**
 * Draw a caustic mask overlay on a region.
 *
 * @param mask_tex   ImTextureID of the grayscale mask texture
 * @param x,y,w,h   Screen-space region to overlay
 * @param config     Effect parameters
 * @param dt         Delta time in seconds (use ImGui::GetIO().DeltaTime)
 */
void pdguiCausticDraw(void *mask_tex, f32 x, f32 y, f32 w, f32 h,
                      const PdguiCausticConfig *config, f32 dt);

/**
 * Draw caustic using the theme's configured mask texture + settings.
 * Returns 1 if drawn, 0 if no caustic configured in theme.
 */
s32 pdguiCausticDrawThemed(f32 x, f32 y, f32 w, f32 h);

/* -----------------------------------------------------------------------
 * Border Effect Mask
 * --------------------------------------------------------------------- */

/** Border effect types. */
typedef enum PdguiBorderFx {
    PDGUI_BORDERFX_NONE       = 0,  /* No border effect */
    PDGUI_BORDERFX_GLOW_PULSE = 1,  /* Pulsing glow on edges */
    PDGUI_BORDERFX_GRAD_SWEEP = 2,  /* Gradient sweep around perimeter */
    PDGUI_BORDERFX_ENERGY     = 3,  /* Energy field flicker */
} PdguiBorderFx;

/** Configuration for border effects. */
typedef struct PdguiBorderFxConfig {
    PdguiBorderFx type;         /* Effect type */
    f32           intensity;    /* 0..1 effect strength (default: 0.5) */
    f32           speed;        /* Animation speed multiplier (default: 1.0) */
    u32           color;        /* Effect color in 0xRRGGBBAA (0 = use palette) */
    f32           width;        /* Border effect width in pixels (default: 2.0) */
} PdguiBorderFxConfig;

/** Default border effect config. */
PdguiBorderFxConfig pdguiBorderFxDefaultConfig(void);

/**
 * Draw a border effect around a rectangular region.
 *
 * @param x,y,w,h   Screen-space rectangle
 * @param config     Border effect parameters
 * @param focused    1 if the element is focused/active (intensifies effect)
 */
void pdguiBorderFxDraw(f32 x, f32 y, f32 w, f32 h,
                       const PdguiBorderFxConfig *config, s32 focused);

/**
 * Draw border effect using theme-configured settings.
 * Automatically uses palette accent color if config color is 0.
 */
void pdguiBorderFxDrawThemed(f32 x, f32 y, f32 w, f32 h, s32 focused);

/* -----------------------------------------------------------------------
 * Theme Integration
 * --------------------------------------------------------------------- */

/** Set the active caustic config from theme.json parsing. */
void pdguiEffectsSetCausticConfig(const PdguiCausticConfig *config);

/** Set the active border effect config from theme.json parsing. */
void pdguiEffectsSetBorderFxConfig(const PdguiBorderFxConfig *config);

/** Set the caustic mask texture catalog ID. */
void pdguiEffectsSetCausticTexture(const char *catalog_id);

/** Get the current caustic mask texture catalog ID (NULL if none). */
const char *pdguiEffectsGetCausticTexture(void);

/** Get current configs (read-only). */
const PdguiCausticConfig *pdguiEffectsGetCausticConfig(void);
const PdguiBorderFxConfig *pdguiEffectsGetBorderFxConfig(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_EFFECTS_H */
