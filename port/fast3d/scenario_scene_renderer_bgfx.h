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

/* Floats per vertex in the bgfx layout: position, two UV sets, colour, sky,
 * room. bgfx has no 32-bit integer attributes, so the room is a float. */
#define SCENE_BGFX_VERTEX_FLOATS 13

/* One scenarioSceneRendererRenderPasses call, as the GL path draws it. */
typedef struct scene_bgfx_pass {
	int x; /* glViewport(x, y, width, height): x, y from the bottom left */
	int y;
	int width;
	int height;
	const float *view_projection; /* 16 floats, as glUniformMatrix4fv takes them */
	float sky_offset[3];
	int fog_enabled;
	float fog_mul;
	float fog_offset;
	float fog_color[3];
	/* This view's room lighting: rooms x 2 RGBA texels, row 0 then row 1. */
	const float *room_texels;
	int rooms;
} scene_bgfx_pass_t;

int gfxBgfxActive(void);
int gfxBgfxRunning(void);
/* A dynamic buffer: sky flags and animated surfaces change vertices. */
uint32_t sceneBgfxCreateVertexBuffer(const float *vertices, uint32_t vertexCount);
void sceneBgfxUpdateVertices(uint32_t vb, uint32_t firstVertex, const float *vertices, uint32_t vertexCount);
void sceneBgfxDestroyVertexBuffer(uint32_t vb);
uint32_t sceneBgfxCreateTexture(int width, int height, const uint8_t *rgba);
void sceneBgfxDestroyTexture(uint32_t tex);
int sceneBgfxBeginPass(const scene_bgfx_pass_t *pass);
/* cull (B-1267): 0 draws both faces; 1 culls the faces that read clockwise
 * in clip space (x right, y up), 2 those that read counter-clockwise. */
void sceneBgfxDraw(uint32_t vb, uint32_t first, uint32_t count,
                   uint32_t tex, int wrapS, int wrapT,
                   uint32_t tex2, int wrap2S, int wrap2T,
                   int hasTex2, int primaryTexcoord, int secondaryTexcoord,
                   float alphaCutoff, int blendPass, int cull);
void sceneBgfxEndPass(void);
/* --debug-clear-depth-after-scene: clear the whole target's depth, in order. */
void sceneBgfxClearDepth(void);

#ifdef __cplusplus
}
#endif

#endif
