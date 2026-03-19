/**
 * pdgui_charpreview.h -- Character model preview for ImGui menus.
 *
 * Renders a 3D character model to an offscreen FBO. The resulting texture
 * is usable with ImGui::Image for displaying character previews in menus.
 */

#ifndef _IN_PDGUI_CHARPREVIEW_H
#define _IN_PDGUI_CHARPREVIEW_H

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the preview system (create FBO). Call after gfx init. */
void pdguiCharPreviewInit(void);

/* Request a character preview render for the given head/body indices.
 * The render happens during the next GBI frame. */
void pdguiCharPreviewRequest(u8 headnum, u8 bodynum);

/* Get the GL texture ID of the rendered preview (0 if not ready).
 * Cast to ImTextureID for use with ImGui::Image. */
u32 pdguiCharPreviewGetTextureId(void);

/* Returns non-zero if the preview texture has valid content. */
s32 pdguiCharPreviewIsReady(void);

/* Get preview dimensions. */
void pdguiCharPreviewGetSize(s32 *w, s32 *h);

/* GBI-phase hook: renders the model to the preview FBO.
 * Called from menuRenderDialog when hotswap is active but preview needed.
 * Returns updated display list pointer. */
struct menu;
Gfx *pdguiCharPreviewRenderGBI(Gfx *gdl, struct menu *menu);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_CHARPREVIEW_H */
