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
 * WEAPON:    id1 = weapon catalog id (e.g. "base:weapon_falcon2"), id2 unused
 * VEHICLE:   id1 = vehicle catalog id, id2 unused
 * PROP:      id1 = prop catalog id, id2 unused
 *
 * For non-character types the function resolves the catalog entry and uses
 * its source_filenum for MENUMODELPARAMS_SET_FILENUM().  If the entry cannot
 * be resolved or has no filenum, the request is silently dropped (preview
 * falls back to the placeholder silhouette rendered by pdgui_model_preview). */
void pdguiCharPreviewRequestEx(PdguiPreviewType type,
                                const char *id1,
                                const char *id2);

/* Low-level filenum-based request.  Use when the caller already has a
 * resolved N64 file index and does not need catalog resolution. */
void pdguiCharPreviewRequestFilenum(PdguiPreviewType type, u32 filenum);

/* Set Y rotation angle (radians) applied on the next pdguiCharPreviewRequest.
 * Call each frame before pdguiCharPreviewRequest to animate rotation. */
void pdguiCharPreviewSetRotY(f32 rotY);

/* Get the GL texture ID of the rendered preview (0 if not ready).
 * Cast to ImTextureID for use with ImGui::Image. */
u32 pdguiCharPreviewGetTextureId(void);

/* Returns non-zero if the preview texture has valid content. */
s32 pdguiCharPreviewIsReady(void);

/* Get preview dimensions. */
void pdguiCharPreviewGetSize(s32 *w, s32 *h);

/* Bake the current preview to a new standalone GL texture.
 * Returns new texture ID (caller owns it; call pdguiCharPreviewFreeTexture
 * to release).  Returns 0 if not ready.  Clears the ready state. */
u32 pdguiCharPreviewBakeToTexture(void);

/* Delete a texture previously returned by pdguiCharPreviewBakeToTexture. */
void pdguiCharPreviewFreeTexture(u32 texId);

/* GBI-phase hook: renders the model to the preview FBO.
 * Called from menuRenderDialog when hotswap is active but preview needed.
 * Returns updated display list pointer. */
struct menu;
Gfx *pdguiCharPreviewRenderGBI(Gfx *gdl, struct menu *menu);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_CHARPREVIEW_H */
