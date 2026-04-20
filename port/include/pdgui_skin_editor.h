/**
 * pdgui_skin_editor.h -- In-game character skin editor.
 *
 * Layer-based 2D pixel editor with live 3D preview, controller-friendly
 * painting, and save-to-catalog-as-mod.  Renders as a tab in the Modding Hub.
 *
 * Design doc: context/designs/skin-editor-design.md
 * Batch S-1: Canvas + 2D editor + Draw/Erase + color picker
 * Batch S-2: Live 3D preview override
 * Batch S-3: Fill + Line + Brush Size + Undo/Redo
 */

#ifndef _IN_PDGUI_SKIN_EDITOR_H
#define _IN_PDGUI_SKIN_EDITOR_H

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Canvas constants ---- */
#define SKIN_MAX_LAYERS       8
#define SKIN_DEFAULT_WIDTH  256
#define SKIN_DEFAULT_HEIGHT 256
#define SKIN_MAX_WIDTH      256
#define SKIN_MAX_HEIGHT     256

/* ---- Blend modes ---- */
#define SKIN_BLEND_NORMAL      0
#define SKIN_BLEND_MULTIPLY    1
#define SKIN_BLEND_SCREEN      2
#define SKIN_BLEND_HUE         3
#define SKIN_BLEND_BURN        4
#define SKIN_BLEND_SATURATION  5
#define SKIN_BLEND_COUNT       6

/* ---- Tools ---- */
#define SKIN_TOOL_DRAW        0
#define SKIN_TOOL_ERASE       1
#define SKIN_TOOL_FILL        2
#define SKIN_TOOL_EYEDROPPER  3
#define SKIN_TOOL_LINE        4
#define SKIN_TOOL_COUNT       5

/* ---- Canvas API (pdgui_skin_canvas.cpp) ---- */

/** Create a new blank canvas. Returns 0 on success. */
s32  skinCanvasCreate(s32 width, s32 height);

/** Destroy the canvas and free all resources. */
void skinCanvasDestroy(void);

/** Get canvas dimensions. */
s32  skinCanvasGetWidth(void);
s32  skinCanvasGetHeight(void);

/** Get number of layers. */
s32  skinCanvasGetNumLayers(void);

/** Get/set active layer index. */
s32  skinCanvasGetActiveLayer(void);
void skinCanvasSetActiveLayer(s32 idx);

/** Add a new layer above current. Returns layer index or -1. */
s32  skinCanvasAddLayer(void);

/** Remove layer at index. Cannot remove last layer. Returns 0 on success. */
s32  skinCanvasRemoveLayer(s32 idx);

/** Get/set layer visibility. */
s32  skinCanvasGetLayerVisible(s32 idx);
void skinCanvasSetLayerVisible(s32 idx, s32 visible);

/** Get/set layer opacity (0.0-1.0). */
f32  skinCanvasGetLayerOpacity(s32 idx);
void skinCanvasSetLayerOpacity(s32 idx, f32 opacity);

/** Get/set layer blend mode. */
s32  skinCanvasGetLayerBlendMode(s32 idx);
void skinCanvasSetLayerBlendMode(s32 idx, s32 mode);

/** Get layer name. */
const char *skinCanvasGetLayerName(s32 idx);

/** Set a single pixel on the active layer. */
void skinCanvasSetPixel(s32 x, s32 y, u8 r, u8 g, u8 b, u8 a);

/** Get a single pixel from the active layer. */
void skinCanvasGetPixel(s32 x, s32 y, u8 *r, u8 *g, u8 *b, u8 *a);

/** Get a pixel from the composited result. */
void skinCanvasGetCompositePixel(s32 x, s32 y, u8 *r, u8 *g, u8 *b, u8 *a);

/** Load RGBA pixel data into a specific layer.
 *  Scales with nearest-neighbor if srcW/srcH differ from canvas size. */
void skinCanvasSetLayerPixels(s32 idx, const u8 *rgba, s32 srcW, s32 srcH);

/** Mark canvas dirty — triggers recomposite + GL upload next frame. */
void skinCanvasMarkDirty(void);

/** Recomposite all visible layers and upload to GL texture if dirty.
 *  Call once per frame from the editor render function. */
void skinCanvasUpdate(void);

/** Get the GL texture ID of the composited canvas (for ImGui::Image / preview). */
u32  skinCanvasGetGlTexture(void);

/** Get raw pointer to active layer pixel data (w*h*4 RGBA). */
u8  *skinCanvasGetActivePixels(void);

/** Get raw pointer to composite pixel data. */
const u8 *skinCanvasGetCompositePixels(void);

/* ---- Undo/Redo (S-3) ---- */

/** Push current active layer state onto undo stack. Call before a stroke. */
void skinCanvasUndoPush(void);

/** Undo last stroke. Returns 0 on success, -1 if nothing to undo. */
s32  skinCanvasUndo(void);

/** Redo last undone stroke. Returns 0 on success, -1 if nothing to redo. */
s32  skinCanvasRedo(void);

/* ---- UV Wireframe (pdgui_skin_uv.cpp, S-8) ---- */

/** Extract UV coordinates from a body model for wireframe overlay.
 *  texW/texH are the canvas dimensions for normalization. */
void skinUvExtract(const char *body_id, s32 texW, s32 texH);

/** Clear extracted UV data. */
void skinUvClear(void);

/** Get number of UV wireframe lines. */
s32  skinUvGetNumLines(void);

/** Get a UV line segment (normalized 0..1 coordinates). */
void skinUvGetLine(s32 idx, f32 *u0, f32 *v0, f32 *u1, f32 *v1);

/* ---- Editor UI (pdgui_skin_editor.cpp) ---- */

/** Called from Modding Hub when Skin Editor tab is selected.
 *  Refreshes character list from catalog. */
void pdguiSkinEditorRefresh(void);

/** Render the skin editor content area.
 *  Called each frame from the Modding Hub when the Skin Editor tab is active. */
void pdguiSkinEditorRender(float contentW, float contentH, float scale);

/** Hub-level Escape: if the paint session is active, exit to character select
 *  and return non-zero so the hub does not close. B-213. */
s32 pdguiSkinEditorTryConsumeHubEscape(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_SKIN_EDITOR_H */
