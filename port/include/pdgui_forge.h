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

/** B-254 (2026-04-25): begin a forge session on a SP campaign stage as a
 *  build canvas. Setup-time chr / AI / script paths suppress mission
 *  state via forgeIsCanvasMode().  Entry pre-conditions match
 *  pdguiForgeStartSessionOn (no pending stage transition; stagenum is
 *  Grid-eligible).  Returns 1 on success, 0 on rejection. */
s32 pdguiForgeStartSessionOnCanvas(s32 stagenum);

/** HUD overlay (F0 shell + F1+ live readouts). */
void pdguiForgeHudRender(s32 winW, s32 winH);

/** The open level file's player starts and objects, marked in the world by
 *  the HUD while the editor camera flies (Forge F3, first piece). On by
 *  default; the editor's Level File section has the switch and tells the HUD
 *  which start and which object are selected so it can pick them out ("" or
 *  NULL for none). */
s32  pdguiForgeLevelMarkersGet(void);
void pdguiForgeLevelMarkersSet(s32 on);
void pdguiForgeLevelMarkersSelect(const char *start_id, const char *object_id);

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
