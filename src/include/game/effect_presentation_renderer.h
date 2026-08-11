#ifndef _IN_GAME_EFFECT_PRESENTATION_RENDERER_H
#define _IN_GAME_EFFECT_PRESENTATION_RENDERER_H

#include <stddef.h>
#include <ultra64.h>

/* Render committed source-backed effect.particle snapshots in the translucent
 * world pass. The texture is selected by its catalog-owned texnum. */
/* Lighting is applied before room geometry; world primitives and source-backed
 * particles are drawn in the normal translucent world pass. */
void effectPresentationApplyLights(void);
Gfx *effectPresentationRenderWorld(Gfx *gdl);
/* Focused proof seam: counts every committed, renderer-eligible particle
 * snapshot using the same enumeration contract as the world pass. */
size_t effectPresentationRenderableParticleCount(void);

#endif
