#ifndef _IN_PDGUI_MODEL_PREVIEW_H
#define _IN_PDGUI_MODEL_PREVIEW_H

/**
 * pdgui_model_preview.h -- High-level model preview panel for ImGui.
 *
 * Wraps pdgui_charpreview (FBO render) with a self-contained ImGui panel
 * that handles:
 *   - Background frame with palette-derived colors
 *   - Automatic re-render on selection change (event-driven)
 *   - Slow idle rotation when selection is static
 *   - Fallback placeholder when FBO not ready
 *   - Optional name labels
 *
 * Batch 0 (D5 Phase 3 foundation): added type-parameterized draw API so
 * the same panel can render characters, weapons, vehicles, and props.
 * Character callers (agent select, agent create, room lobby, modding hub)
 * continue to use pdguiModelPreviewDraw() unchanged; weapon/vehicle/prop
 * callers (Batch 10 training screens, future character creator) use
 * pdguiModelPreviewDrawEx() with an explicit ModelPreviewKind.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <PR/ultratypes.h>
#include "pdgui_charpreview.h"   /* PdguiPreviewType */

#ifdef __cplusplus
extern "C" {
#endif

/** Model kind passed to pdguiModelPreviewDrawEx().  Mirrors PdguiPreviewType
 *  so callers see a single symbolic space; kept as a distinct typedef so the
 *  high-level panel layer can evolve independently of the low-level FBO. */
typedef enum {
    PDGUI_MP_CHARACTER = 0,
    PDGUI_MP_WEAPON    = 1,
    PDGUI_MP_VEHICLE   = 2,
    PDGUI_MP_PROP      = 3
} ModelPreviewKind;

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

/** Draw a self-contained model preview panel (CHARACTER shortcut).
 *
 *  Equivalent to pdguiModelPreviewDrawEx(PDGUI_MP_CHARACTER, head_id,
 *  body_id, x, y, w, h, opts).  Kept as a convenience wrapper so the
 *  existing character callers do not need to churn.
 *
 *  @param head_id  catalog ID for head (e.g., "base:head_dark_combat")
 *  @param body_id  catalog ID for body (e.g., "base:dark_combat")
 *  @param x,y      screen position (top-left)
 *  @param w,h      panel size in pixels
 *  @param opts     display options (NULL for defaults) */
void pdguiModelPreviewDraw(const char *head_id, const char *body_id,
                            f32 x, f32 y, f32 w, f32 h,
                            const ModelPreviewOpts *opts);

/** Draw a self-contained model preview panel for any supported model kind.
 *
 *  CHARACTER: id1 = head catalog id,   id2 = body catalog id
 *  WEAPON:    id1 = weapon catalog id, id2 = NULL
 *  VEHICLE:   id1 = vehicle catalog id,id2 = NULL
 *  PROP:      id1 = prop catalog id,   id2 = NULL
 *
 *  For CHARACTER the panel shows the body display name in the label slot
 *  (when opts->showBodyName != 0).  For WEAPON / VEHICLE / PROP the single
 *  catalog id1 name is shown.
 *
 *  Internally tracks the last (kind, id1, id2) tuple and only re-requests
 *  a render from pdgui_charpreview when the selection changes. */
void pdguiModelPreviewDrawEx(ModelPreviewKind kind,
                              const char *id1, const char *id2,
                              f32 x, f32 y, f32 w, f32 h,
                              const ModelPreviewOpts *opts);

/** Reset preview state (force re-render on next draw). */
void pdguiModelPreviewInvalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_MODEL_PREVIEW_H */
