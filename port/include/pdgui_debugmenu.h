#ifndef _IN_PDGUI_DEBUGMENU_H
#define _IN_PDGUI_DEBUGMENU_H

/**
 * pdgui_debugmenu.h -- F12 debug menu for Perfect Dark 2.
 *
 * Provides a PD-styled ImGui debug overlay with:
 *   - Network state display (mode, clients, tick)
 *   - Match lifecycle controls (host, start, end, disconnect)
 *   - Memory diagnostics (persistent pool stats, heap usage)
 *   - Frame timing and FPS
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Render the F12 debug menu. Call once per frame between NewFrame and Render
 * when the overlay is active. winW/winH are the current window dimensions
 * for game-relative scaling calculations. */
void pdguiDebugMenuRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_DEBUGMENU_H */
