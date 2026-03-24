#ifndef _IN_PDGUI_PAUSEMENU_H
#define _IN_PDGUI_PAUSEMENU_H

/**
 * pdgui_pausemenu.h -- C-callable API for the ImGui combat sim pause menu
 * and hold-to-show scorecard overlay.
 *
 * The pause menu replaces the legacy g_MpPauseControlMenuDialog stack for
 * combat simulator matches. The scorecard overlay is a transparent HUD
 * element shown while a button is held during gameplay (like Tab in a PC FPS).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Pause Menu --- */

/** Open the ImGui pause menu (call instead of mpPushPauseDialog). */
void pdguiPauseMenuOpen(void);

/** Close the ImGui pause menu and resume gameplay. */
void pdguiPauseMenuClose(void);

/** Returns non-zero if the ImGui pause menu is currently open. */
s32 pdguiIsPauseMenuOpen(void);

/** Render the pause menu. Called from pdguiRender() pipeline each frame. */
void pdguiPauseMenuRender(s32 winW, s32 winH);

/* --- Scorecard Overlay --- */

/** Set scorecard visibility (call each frame based on button hold state). */
void pdguiScorecardSetVisible(s32 visible);

/** Returns non-zero if the scorecard overlay is currently showing. */
s32 pdguiIsScorecardVisible(void);

/** Render the scorecard overlay. Called from pdguiRender() pipeline. */
void pdguiScorecardRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_PAUSEMENU_H */
