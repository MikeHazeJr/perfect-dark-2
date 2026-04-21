/**
 * pdgui_menu_stack_debug.h -- F9 menu / input-context diagnostics overlay (read-only).
 */

#ifndef _PDGUI_MENU_STACK_DEBUG_H
#define _PDGUI_MENU_STACK_DEBUG_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void pdguiMenuStackOverlayToggle(void);
s32 pdguiMenuStackOverlayGetOpen(void);
void pdguiMenuStackOverlayRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif
