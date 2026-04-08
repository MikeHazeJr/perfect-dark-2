#ifndef _IN_PDGUI_MODEL_PREVIEW_H
#define _IN_PDGUI_MODEL_PREVIEW_H

/**
 * pdgui_model_preview.h -- High-level model preview panel for ImGui (P6)
 *
 * Wraps pdgui_charpreview (FBO render) with a self-contained ImGui panel
 * that handles:
 *   - Background frame with palette-derived colors
 *   - Automatic re-render on body/head change (event-driven)
 *   - Slow idle rotation when selection is static
 *   - Fallback placeholder when FBO not ready
 *   - Optional body/head name labels
 *
 * Usage:
 *   pdguiModelPreviewBegin("preview1");       // begin tracking
 *   pdguiModelPreviewSetModel(headId, bodyId); // set current selection
 *   pdguiModelPreviewDraw(x, y, w, h);        // render the panel
 *   pdguiModelPreviewEnd();                    // end tracking
 *
 * Multiple independent preview panels are supported via the id parameter.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------- */

/** Preview panel options. */
typedef struct {
    s32  showBodyName;     /* 1 = show body name label below preview */
    s32  showHeadName;     /* 1 = show head name label below body name */
    s32  idleRotation;     /* 1 = slowly rotate model when static */
    f32  idleRotSpeed;     /* radians/sec for idle rotation (default 0.3) */
    f32  cornerRadius;     /* border corner rounding in pixels (default 4) */
    u32  bgColor;          /* background color 0xRRGGBBAA (0 = use palette) */
    u32  borderColor;      /* border color 0xRRGGBBAA (0 = use palette) */
} ModelPreviewOpts;

/** Return default options. */
ModelPreviewOpts pdguiModelPreviewDefaultOpts(void);

/* -----------------------------------------------------------------------
 * Panel API
 * --------------------------------------------------------------------- */

/** Draw a self-contained model preview panel.
 *  @param head_id  catalog ID for head (e.g., "base:head_dark_combat")
 *  @param body_id  catalog ID for body (e.g., "base:dark_combat")
 *  @param x,y      screen position (top-left)
 *  @param w,h      panel size in pixels
 *  @param opts     display options (NULL for defaults)
 *
 *  Internally tracks the last head/body and only re-requests a render
 *  from pdgui_charpreview when the selection changes. */
void pdguiModelPreviewDraw(const char *head_id, const char *body_id,
                            f32 x, f32 y, f32 w, f32 h,
                            const ModelPreviewOpts *opts);

/** Reset preview state (force re-render on next draw). */
void pdguiModelPreviewInvalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_MODEL_PREVIEW_H */
