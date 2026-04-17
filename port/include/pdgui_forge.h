/**
 * pdgui_forge.h -- The Grid (Forge) UI shims.
 *
 * Entry points for the ImGui overlay components:
 *  - pdguiForgeStartSession (F0) kicks off the in-game editor session
 *  - pdguiForgeHudRender (F0) draws the always-on HUD shell
 *  - pdguiForgeEditorRender (F1+) draws the full editor overlay
 *    (catalog, properties, logic graph, gametype, mission, settings)
 *
 * All entry points are safe to call every frame; they early-out when no
 * forge session is active.
 */

#ifndef _IN_PDGUI_FORGE_H
#define _IN_PDGUI_FORGE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Begin a forge session on a specific base stage.
 *  Pass 0 / negative to use the legacy default (CI Training). */
s32 pdguiForgeStartSession(void);
s32 pdguiForgeStartSessionOn(s32 stagenum);

/** HUD overlay (F0 shell + F1+ live readouts). */
void pdguiForgeHudRender(s32 winW, s32 winH);

/** Full editor overlay -- drawn on top of the HUD when in-session. The
 *  editor is visible regardless of NORMAL vs FREEFLY sub-mode, but the
 *  catalog/placement panels only respond to input in FREEFLY. */
void pdguiForgeEditorRender(s32 winW, s32 winH);

/** Per-frame hook called from the game loop (before render) so the
 *  editor runtime can tick (currently a no-op placeholder; room for
 *  placement reticle integration with the freefly camera). */
void pdguiForgeEditorTick(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _IN_PDGUI_FORGE_H */
