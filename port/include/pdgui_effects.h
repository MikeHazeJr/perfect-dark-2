#ifndef _IN_PDGUI_EFFECTS_H
#define _IN_PDGUI_EFFECTS_H

/**
 * pdgui_effects.h -- Animated UI overlay effects (P4)
 *
 * Provides compositable visual effects for menu elements:
 *   - Caustic/water mask: time-based UV scrolling overlay
 *   - Border glow pulse: animated glow on focus/selection
 *   - Gradient sweep: directional color sweep on selection
 *
 * All effects are frame-rate independent (use delta time).
 * Configurable speed, scale, tint, and opacity.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Effect types
 * --------------------------------------------------------------------- */

typedef enum {
    PDEFFECT_NONE        = 0,
    PDEFFECT_CAUSTIC     = 1,  /* animated UV-scrolling mask overlay */
    PDEFFECT_GLOW_PULSE  = 2,  /* border glow that pulses on focus */
    PDEFFECT_GRAD_SWEEP  = 3,  /* directional gradient sweep */
} pdeffect_type_e;

/* -----------------------------------------------------------------------
 * Caustic overlay config
 * --------------------------------------------------------------------- */

typedef struct {
    f32 scrollSpeedX;  /* UV scroll speed X (units/sec), default 0.05 */
    f32 scrollSpeedY;  /* UV scroll speed Y (units/sec), default 0.03 */
    f32 scale;         /* UV scale multiplier, default 1.0 */
    f32 opacity;       /* overlay alpha 0.0-1.0, default 0.15 */
    u32 tint;          /* tint color 0xRRGGBBAA, default 0xFFFFFF80 */
    s32 enabled;       /* on/off toggle */
} pdeffect_caustic_t;

/* -----------------------------------------------------------------------
 * Border glow pulse config
 * --------------------------------------------------------------------- */

typedef struct {
    f32 pulseSpeed;    /* cycles per second, default 2.0 */
    f32 minAlpha;      /* minimum glow alpha, default 0.1 */
    f32 maxAlpha;      /* maximum glow alpha, default 0.6 */
    f32 glowSize;      /* glow expansion in pixels, default 3.0 */
    u32 color;         /* glow color 0xRRGGBBAA, default from palette accent */
    s32 enabled;
} pdeffect_glow_pulse_t;

/* -----------------------------------------------------------------------
 * Gradient sweep config
 * --------------------------------------------------------------------- */

typedef struct {
    f32 sweepSpeed;    /* pixels per second, default 200.0 */
    f32 sweepWidth;    /* gradient band width in pixels, default 40.0 */
    f32 opacity;       /* peak alpha, default 0.3 */
    u32 color;         /* sweep color, default 0xFFFFFF40 */
    s32 horizontal;    /* 1=left-to-right, 0=top-to-bottom */
    s32 enabled;
} pdeffect_grad_sweep_t;

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/** Initialize effects system. Call once from pdguiInit(). */
void pdguiEffectsInit(void);

/** Per-frame update. Call once per frame with ImGui delta time. */
void pdguiEffectsUpdate(f32 deltaTime);

/** Shutdown: free resources. */
void pdguiEffectsShutdown(void);

/* -----------------------------------------------------------------------
 * Draw functions
 * --------------------------------------------------------------------- */

/** Draw caustic overlay on a rect using a mask texture.
 *  If maskTex is NULL, uses a procedural noise pattern. */
void pdguiEffectDrawCaustic(f32 x, f32 y, f32 w, f32 h,
                            void *maskTex,
                            const pdeffect_caustic_t *cfg);

/** Draw animated glow pulse around a rect border. */
void pdguiEffectDrawGlowPulse(f32 x, f32 y, f32 w, f32 h,
                              const pdeffect_glow_pulse_t *cfg);

/** Draw gradient sweep across a rect. */
void pdguiEffectDrawGradSweep(f32 x, f32 y, f32 w, f32 h,
                              const pdeffect_grad_sweep_t *cfg);

/* -----------------------------------------------------------------------
 * Default configs
 * --------------------------------------------------------------------- */

pdeffect_caustic_t     pdguiEffectDefaultCaustic(void);
pdeffect_glow_pulse_t  pdguiEffectDefaultGlowPulse(void);
pdeffect_grad_sweep_t  pdguiEffectDefaultGradSweep(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_EFFECTS_H */
