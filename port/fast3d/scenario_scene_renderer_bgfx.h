#ifndef SCENARIO_SCENE_RENDERER_BGFX_H
#define SCENARIO_SCENE_RENDERER_BGFX_H

/* The scenario scene renderer's bgfx draw path (--renderer bgfx), kept in its
 * own file so the CPU-only scenario-scene-probe tool, which compiles
 * scenario_scene_renderer.cpp, needs only no-op stubs
 * (tools/scenario_scene_probe_stubs.cpp). Handles are bgfx index + 1. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gfxBgfxActive(void);
int gfxBgfxRunning(void);
uint32_t sceneBgfxCreateVertexBuffer(const void *vertices, uint32_t vertexCount, uint32_t vertexSize);
void sceneBgfxDestroyVertexBuffer(uint32_t vb);
uint32_t sceneBgfxCreateTexture(int width, int height, const uint8_t *rgba);
void sceneBgfxDestroyTexture(uint32_t tex);
int sceneBgfxBeginPass(int width, int height, const float viewProjection[16]);
void sceneBgfxDraw(uint32_t vb, uint32_t first, uint32_t count,
                   uint32_t tex, int wrapS, int wrapT,
                   uint32_t tex2, int wrap2S, int wrap2T,
                   int hasTex2, int primaryTexcoord, int secondaryTexcoord,
                   float alphaCutoff, int blendPass);
void sceneBgfxEndPass(void);
/* --debug-clear-depth-after-scene: clear the whole target's depth, in order. */
void sceneBgfxClearDepth(void);

#ifdef __cplusplus
}
#endif

#endif
