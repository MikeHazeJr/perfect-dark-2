/**
 * pdgui_hud.h -- In-match HUD overlay: top scorers + timer.
 *
 * Rendered during CLSTATE_GAME (normmplayerisrunning active).
 * Shows top 2 players by score and remaining match time.
 */

#pragma once

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void pdguiHudRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif
