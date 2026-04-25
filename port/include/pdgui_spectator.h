/**
 * pdgui_spectator.h -- Phase 3 spectator overlay UI.
 *
 * Two surfaces share this module:
 *
 *   - HUD overlay during a live stream / Theater playback. Shows the
 *     focused participant's name + score + subset label, plus the
 *     camera + control hints (D-pad subset / member, R3 first-person
 *     toggle, hold-Y free-fly).
 *   - Stop button + Theater placeholder.
 *
 * The actual camera transform application (game-side) plugs into the
 * existing first-person view path in a follow-up wiring commit; this
 * module owns the on-screen UI.
 */

#ifndef _IN_PDGUI_SPECTATOR_H
#define _IN_PDGUI_SPECTATOR_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void pdguiSpectatorRender(s32 winW, s32 winH);
s32  pdguiSpectatorOverlayActive(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_SPECTATOR_H */
