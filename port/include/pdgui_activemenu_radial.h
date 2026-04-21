/**
 * pdgui_activemenu_radial.h -- PC ImGui renderer for the in-game active menu
 * (weapon / function / bot orders diamond).  Game logic stays in activemenu.c;
 * labels come from amGetSlotDetails (inv/lang/bot names) with no extra catalog
 * wire IDs at this layer.
 */

#ifndef PDGUI_ACTIVEMENU_RADIAL_H
#define PDGUI_ACTIVEMENU_RADIAL_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

s32 pdguiActiveMenuShouldSkipLegacyWheel(void);
void pdguiActiveMenuRadialRender(s32 winW, s32 winH);

void pdguiActiveMenuRadialMapGameToScreen(s16 gx, s16 gy, s32 winW, s32 winH, float *sx, float *sy);
void pdguiActiveMenuRadialQuerySlot(s32 slot, s32 winW, s32 winH,
		float *out_cx, float *out_cy, u32 *flags, char *label, s32 *mode);
f32 pdguiActiveMenuRadialGetAlphaFrac(void);
s32 pdguiActiveMenuRadialGetSlotWidthPx(s32 winW);
s32 pdguiActiveMenuRadialIsEditMode(void);
s32 pdguiActiveMenuRadialIsCramped(void);
void pdguiActiveMenuRadialGetSelectionPulseRGBA(u8 *r, u8 *g, u8 *b, u8 *a);
void pdguiActiveMenuRadialGetOuterDiamondScreen(float *out_x, float *out_y, s32 winW, s32 winH);
void pdguiActiveMenuRadialGetSelectionCenterScreen(float *sx, float *sy, s32 winW, s32 winH);
s32 pdguiActiveMenuRadialGetSlotNum(void);
s32 pdguiActiveMenuRadialGetLocalPlayerCount(void);

#ifdef __cplusplus
}
#endif

#endif /* PDGUI_ACTIVEMENU_RADIAL_H */
