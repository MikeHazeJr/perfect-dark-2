/**
 * pdgui_charpreview.h -- Model preview for ImGui menus (character / weapon /
 *                        vehicle / prop).
 *
 * Renders a 3D model to an offscreen FBO.  The resulting texture is usable
 * with ImGui::Image for displaying previews in menus.
 *
 * Historical name: "charpreview" -- it started as character-only (head+body)
 * and has since been generalized to accept a model type + catalog ID so that
 * weapon/vehicle/prop previews use the same FBO + GBI render hook.  The
 * existing character API is retained as the CHARACTER case, so every prior
 * caller (agent select, agent create, modding hub, room lobby) continues to
 * work without churn.
 *
 * Batch 0 (D5 Phase 3 foundation): added preview-type enum and filenum-based
 * request path.  Used by Batch 10 training screens (weapon + vehicle
 * holograms) and future character creator.
 */

#ifndef _IN_PDGUI_CHARPREVIEW_H
#define _IN_PDGUI_CHARPREVIEW_H

#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "assetprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * Preview type
 * ------------------------------------------------------------------------
 * Determines the GBI render mode (MENUMODELTYPE_*) and the encoded
 * newparams format passed to menuRenderModel().
 */
typedef enum {
    PDGUI_PREVIEW_CHARACTER = 0,  /* head+body pair; MENUMODELTYPE_DEFAULT  */
    PDGUI_PREVIEW_WEAPON    = 1,  /* single filenum; MENUMODELTYPE_HUDPIECE */
    PDGUI_PREVIEW_VEHICLE   = 2,  /* single filenum; MENUMODELTYPE_DEFAULT  */
    PDGUI_PREVIEW_PROP      = 3   /* single filenum; MENUMODELTYPE_DEFAULT  */
} PdguiPreviewType;

/* Initialize the preview system (create FBO). Call after gfx init. */
void pdguiCharPreviewInit(void);

/* Request a character preview render for the given head/body catalog IDs.
 * Resolves catalog IDs to runtime indices internally.
 * The render happens during the next GBI frame.
 *
 * Equivalent to pdguiCharPreviewRequestEx(PDGUI_PREVIEW_CHARACTER,
 * head_id, body_id) -- existing callers do not need to change. */
void pdguiCharPreviewRequest(const char *head_id, const char *body_id);

/* Request a preview render for any asset type.
 *
 * CHARACTER: id1 = head catalog id, id2 = body catalog id
 * WEAPON:    id1 = weapon catalog id (e.g. "base:falcon2"), id2 unused
 * VEHICLE:   id1 = vehicle catalog id, id2 unused
 * PROP:      id1 = prop catalog id, id2 unused
 *
 * For non-character types the function resolves the catalog entry and uses
 * either its source_filenum or a direct catalog provider handle for custom
 * source-backed rows that do not occupy a ROM file slot. If the entry cannot
 * be resolved, the request is silently dropped (preview falls back to the
 * placeholder silhouette rendered by pdgui_model_preview). */
void pdguiCharPreviewRequestEx(PdguiPreviewType type,
                                const char *id1,
                                const char *id2);

/* Low-level filenum-based request.  Use when the caller already has a
 * resolved N64 file index and does not need catalog resolution. */
void pdguiCharPreviewRequestFilenum(PdguiPreviewType type, u32 filenum);

/* Low-level provider-backed request. Use when the caller already has a
 * catalog source handle but no real legacy file index. `request_key` is only
 * the menu-model request key and must match the key stored with the handle. */
void pdguiCharPreviewRequestHandle(PdguiPreviewType type,
                                   asset_data_handle_t handle,
                                   u32 request_key);

/* Set Y rotation angle (radians) applied on the next pdguiCharPreviewRequest.
 * Call each frame before pdguiCharPreviewRequest to animate rotation. */
void pdguiCharPreviewSetRotY(f32 rotY);

/* B-253 follow-up: set the projection aspect ratio used when rendering the
 * model into the (square) FBO.  When the displayed pane is non-square, set
 * aspect = pane_w / pane_h so the model is rendered "squished" in the FBO
 * and unstretches back to correct proportions when ImGui::Image stretches
 * the FBO texture to fill the pane.  Default 1.0 (square pane). */
void pdguiCharPreviewSetAspect(f32 aspect);

/* Get the GL texture ID of the rendered preview (0 if not ready).
 * Cast to ImTextureID for use with ImGui::Image. */
u32 pdguiCharPreviewGetTextureId(void);

/* Returns non-zero if the preview texture has valid content. */
s32 pdguiCharPreviewIsReady(void);

/* Non-zero if menuRenderModel must run for the preview/capture pipeline this
 * frame even when g_MenuData.unk5d5_01 would normally skip (no legacy dialog
 * open). Used from menu.c — B-213 standalone ImGui + Skin Editor. */
s32 pdguiCharPreviewNeedsMenuModel(void);

/* Get preview dimensions. */
void pdguiCharPreviewGetSize(s32 *w, s32 *h);

/* Bake the current preview to a new standalone GL texture.
 * Returns new texture ID (caller owns it; call pdguiCharPreviewFreeTexture
 * to release).  Returns 0 if not ready.  Clears the ready state. */
u32 pdguiCharPreviewBakeToTexture(void);

/* Delete a texture previously returned by pdguiCharPreviewBakeToTexture. */
void pdguiCharPreviewFreeTexture(u32 texId);

/* ---- Skin Override (Batch S-2) ----
 * When active, the preview FBO render substitutes the given GL texture
 * for the body's texture.  Used by the skin editor to show live edits
 * on the 3D model.  Only affects the preview FBO render, never the
 * main game render.
 *
 * Call SetSkinOverride before each frame's pdguiCharPreviewRequest.
 * Call ClearSkinOverride when the skin editor closes. */
void pdguiCharPreviewSetSkinOverride(u32 glTexId, s32 texWidth, s32 texHeight);
void pdguiCharPreviewClearSkinOverride(void);

/* Query: is a skin override currently active? */
s32  pdguiCharPreviewHasSkinOverride(void);

/* Get the override GL texture ID (0 if none). For gfx_pc to check. */
u32  pdguiCharPreviewGetSkinOverrideTexId(void);

/* GBI-phase hook: renders the model to the preview FBO.
 * Called from menuRenderDialog when hotswap is active but preview needed.
 * Returns updated display list pointer. */
struct menu;
Gfx *pdguiCharPreviewRenderGBI(Gfx *gdl, struct menu *menu);

/* Direct entry point that does NOT depend on menuRenderDialog (MASTER-C6).
 *
 * Call once per frame from a point in the main GBI display list that is
 * guaranteed to execute for every stage and menu state — typically the tail
 * of lvRender.  Binds the render hook to player 0's menu struct.
 *
 * Standalone ImGui screens (modding hub, pause menu, skin editor, scorecard
 * overlay) never reach menuRenderDialog, so without this direct path their
 * preview FBO stays black.  pdguiCharPreviewRenderGBI is idempotent within a
 * frame, so adding this call does not double-render when the legacy dialog
 * path also fires.
 *
 * Returns the updated display list pointer. */
Gfx *pdguiCharPreviewRenderDirect(Gfx *gdl);

/* ---- Skin Texture Capture (Batch S-9) ----
 * One-shot capture of the original body texture from the GBI pipeline.
 * Flow: request -> wait for model load -> render without override ->
 * read back captured source texture pixels.
 *
 * Call RequestSkinCapture to start.
 * Call SkinCapturePoll each frame during ImGui phase.
 * When SkinCaptureReady returns non-zero, call SkinCaptureGetPixels.
 * Call SkinCaptureConsume when done to free the pixel buffer. */

void  pdguiCharPreviewRequestSkinCapture(void);
void  pdguiCharPreviewSkinCapturePoll(void);
s32   pdguiCharPreviewSkinCaptureReady(void);
u8   *pdguiCharPreviewSkinCaptureGetPixels(s32 *outW, s32 *outH);
void  pdguiCharPreviewSkinCaptureConsume(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_CHARPREVIEW_H */
