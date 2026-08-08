#ifndef _IN_PDGUI_EFFECTS_H
#define _IN_PDGUI_EFFECTS_H

/**
 * pdgui_effects.h -- Caustic mask + border overlay effects for PD2 UI (P4)
 *
 * Two compositable overlay effects for UI panels:
 *
 * 1. Caustic/animated mask: A grayscale animation texture (spritesheet)
 *    multiplied over the base texture. Speed, opacity, blend mode configurable.
 *
 * 2. Border effect mask: A second image used as a mask — only the border
 *    regions (defined by 9-slice edges) receive the effect. Interior untouched.
 *
 * Compositing order: base → 9-slice → border effect → caustic overlay → text.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase P4: UI Texture Mod.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Blend modes for overlay effects
 * --------------------------------------------------------------------- */

#define FX_BLEND_MULTIPLY  0   /* darken: src * overlay */
#define FX_BLEND_ADDITIVE  1   /* brighten: src + overlay */
#define FX_BLEND_SCREEN    2   /* lighten: 1 - (1-src)(1-overlay) */

/* -----------------------------------------------------------------------
 * Caustic effect definition
 *
 * A grayscale spritesheet scrolled/animated over the UI element.
 * The spritesheet is a horizontal strip of equal-sized frames.
 * --------------------------------------------------------------------- */

typedef struct caustic_def {
    char  texture_id[64];    /* catalog ID of the caustic spritesheet */
    s32   frame_count;       /* number of frames in the spritesheet */
    f32   speed;             /* frames per second */
    f32   opacity;           /* 0.0 = invisible, 1.0 = full strength */
    s32   blend_mode;        /* FX_BLEND_MULTIPLY, ADDITIVE, or SCREEN */
    f32   scale;             /* UV scale multiplier (1.0 = 1:1 pixel) */
} caustic_def_t;

/* -----------------------------------------------------------------------
 * Border effect definition
 *
 * Applies a texture mask to only the border regions of a 9-slice panel.
 * The mask texture is stretched over the border area (not the center).
 * --------------------------------------------------------------------- */

typedef struct border_fx_def {
    char  mask_texture_id[64];  /* catalog ID of the border mask texture */
    f32   opacity;              /* 0.0 = invisible, 1.0 = full strength */
    s32   blend_mode;           /* FX_BLEND_* */
    u32   tint_color;           /* 0xRRGGBBAA tint applied to the mask */
    f32   scroll_speed_x;       /* horizontal scroll speed (pixels/sec) */
    f32   scroll_speed_y;       /* vertical scroll speed (pixels/sec) */
} border_fx_def_t;

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

void pdguiEffectsInit(void);
void pdguiEffectsShutdown(void);

/* -----------------------------------------------------------------------
 * Per-element effect binding
 *
 * Effects are bound to catalog texture IDs (same IDs used by nineslice).
 * An element can have a caustic, a border effect, both, or neither.
 * --------------------------------------------------------------------- */

/** Bind a caustic effect to a UI element's catalog ID. */
s32 pdguiEffectsSetCaustic(const char *element_id, const caustic_def_t *def);

/** Bind a border effect to a UI element's catalog ID. */
s32 pdguiEffectsSetBorderFx(const char *element_id, const border_fx_def_t *def);

/** Read-only capacity preflight used by transactional theme activation. */
s32 pdguiEffectsCanSet(const char *element_id);

/** Clear all effects for an element. */
void pdguiEffectsClear(const char *element_id);

/** Get the caustic definition for an element. Returns NULL if none. */
const caustic_def_t *pdguiEffectsGetCaustic(const char *element_id);

/** Get the border effect definition for an element. Returns NULL if none. */
const border_fx_def_t *pdguiEffectsGetBorderFx(const char *element_id);

/* -----------------------------------------------------------------------
 * Rendering
 *
 * These are called by the theme renderer after 9-slice drawing.
 * --------------------------------------------------------------------- */

/** Draw the caustic overlay for an element over the given screen rect.
 *  Uses the current frame time to animate. */
void pdguiEffectsDrawCaustic(const char *element_id,
                             float x, float y, float w, float h);

/** Draw the border effect for an element.
 *  Requires the 9-slice insets to know which regions are "border".
 *  left/right/top/bottom: border insets in screen pixels. */
void pdguiEffectsDrawBorder(const char *element_id,
                            float x, float y, float w, float h,
                            float left, float right, float top, float bottom);

/** Convenience: draw both effects (caustic + border) in compositing order. */
void pdguiEffectsDrawAll(const char *element_id,
                         float x, float y, float w, float h,
                         float border_l, float border_r,
                         float border_t, float border_b);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_EFFECTS_H */
