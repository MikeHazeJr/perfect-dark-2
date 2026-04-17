/**
 * pdgui_forge.h -- Forge UI shims (Phase F0).
 *
 * F0 surface area is intentionally tiny: one entry-point that the main menu
 * button calls to start a forge session, and the HUD overlay renderer.
 *
 * Future phases (F1 catalog, F2 gizmo, F3 save/load) will add their own
 * pdgui_forge_*.cpp files and grow this header.
 */

#ifndef _IN_PDGUI_FORGE_H
#define _IN_PDGUI_FORGE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Begin a forge session.
 *
 *  Currently loads CI Training as the base stage.  In F3 this will dispatch
 *  to a base-stage browser; for F0 it's a single fixed entry point so the
 *  freefly camera + mode toggle can be exercised end-to-end.
 *
 *  Safe to call from any menu context.  Returns 1 on success (stage change
 *  queued), 0 on rejection (e.g. an existing stage transition is already
 *  pending). */
s32 pdguiForgeStartSession(void);

/** Forge HUD overlay (Phase F0 shell).
 *
 *  Draws the in-session "FORGE -- NORMAL/FREEFLY" indicator plus a freefly
 *  reticle and placeholder catalog/properties panels.  Called from the
 *  ImGui overlay dispatch in pdgui_backend.cpp.  Renders nothing when
 *  forgeSessionIsActive() is false, so it's safe to call every frame. */
void pdguiForgeHudRender(s32 winW, s32 winH);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _IN_PDGUI_FORGE_H */
