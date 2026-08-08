#ifndef PDGUI_WEAPON_RETICLE_H
#define PDGUI_WEAPON_RETICLE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Queued by the production sight pass, rendered later in the ImGui overlay
 * after the game framebuffer. Coordinates use the native 320x240 HUD space. */
void pdguiWeaponReticleQueue(const char *catalog_id, f32 x, f32 y);
void pdguiWeaponReticleClear(void);
s32 pdguiWeaponReticleIsQueued(void);
void pdguiWeaponReticleRender(s32 window_width, s32 window_height);

#ifdef __cplusplus
}
#endif

#endif
