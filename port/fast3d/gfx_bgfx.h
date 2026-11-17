#ifndef GFX_BGFX_H
#define GFX_BGFX_H

/*
 * fast3d rendering backend on bgfx's OpenGL 4.3 renderer (2.0 renderer,
 * phase 2a step 1, decision D5). Chosen at launch with --renderer bgfx; with
 * no flag the classic renderer in gfx_opengl.cpp runs unchanged. See
 * context/designs/rendering/bgfx-classic-gl.md.
 *
 * Everything here is C-callable. The game, pdgui and the scene renderer use
 * these entry points so that the bgfx headers stay inside the renderer.
 */

#include <stdint.h>

#include "gfx_rendering_api.h"

#pragma push_macro("bool")
#undef bool
#ifndef __cplusplus
#define bool _Bool
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern struct GfxRenderingAPI gfx_bgfx_api;

/* Renderer selection ------------------------------------------------------ */

/* Nonzero when the command line asks for the bgfx renderer
 * (--renderer bgfx). Read by videoInit before the window exists. */
int gfxBgfxRequested(void);

/* Called by gfx_init after the window and its GL context exist and before
 * the chosen backend's init. Returns the backend to use: `requested` when it
 * can run, otherwise gfx_opengl_api with the reason logged (bgfx needs an
 * OpenGL 4.3 core context). */
struct GfxRenderingAPI *gfxBgfxSelectRenderingApi(struct GfxRenderingAPI *requested);

/* Nonzero once the bgfx backend is the presenting fast3d backend
 * (--renderer bgfx). Code that has a GL branch and a bgfx branch tests this. */
int gfxBgfxActive(void);

/* Renderer parity mode (--renderer-parity, gfx_tee.cpp) --------------------
 *
 * The classic GL backend presents and bgfx renders every frame alongside on
 * its own GL context. gfxBgfxRunning() is nonzero in bgfx mode and parity
 * mode; gfxBgfxTeeActive() only in parity mode. */
int gfxBgfxRunning(void);
int gfxBgfxTeeActive(void);
void gfxBgfxSetTeeMode(int enable);
/* The GL name of fast3d framebuffer fbId's colour, so ImGui draws through
 * bgfx can use bgfx's copy of that framebuffer. */
void gfxBgfxTeeNoteFramebufferTexture(uint32_t glTexture, int fbId);
/* Read back bgfx's finished frame at the next submit, and take it after:
 * RGBA8 rows, bottom row first like glReadPixels. */
void gfxBgfxRequestParityCapture(void);
int gfxBgfxTakeParityPixels(const uint8_t **rgba, int *width, int *height);

/* Frame hooks used by gfx_pc.cpp and gfx_sdl2.cpp ------------------------- */

/* Replaces gfx_opengl_reset_for_overlay in bgfx mode: copies the game image
 * to the window and opens the view ImGui draws into. */
void gfxBgfxResetForOverlay(int width, int height);

/* Runs bgfx::frame(), which executes the whole frame on the GL context. The
 * window layer calls it right before the buffer swap. */
void gfxBgfxSubmitFrame(void);

/* External passes (scenario scene renderer) ------------------------------- */

/* Opens a view on fast3d's current render target with the GL-convention
 * viewport (x, y from the bottom left), in submission order, and returns its
 * bgfx view id. Also returns the target's height (for scissor conversion).
 * fast3d's next draw opens a fresh view after gfxBgfxEndPass. */
uint16_t gfxBgfxBeginPass(int x, int y, int width, int height, uint32_t *targetHeight);
void gfxBgfxEndPass(void);

/* The view ImGui draws into this frame (valid after gfxBgfxResetForOverlay)
 * and that view's target height. */
uint16_t gfxBgfxOverlayView(uint32_t *targetHeight);

/* Textures that ImGui can draw ----------------------------------------------
 *
 * In bgfx mode every ImTextureID is a tagged integer: (index << 3) | kind.
 * The ImGui renderer resolves it at draw time, so a framebuffer resized or a
 * texture re-uploaded mid-session never leaves ImGui with a stale handle.
 */
#define GFX_BGFX_TEXID_KIND_FAST3D_TEX  1u  /* fast3d texture cache id */
#define GFX_BGFX_TEXID_KIND_FAST3D_FB   2u  /* fast3d framebuffer colour */
#define GFX_BGFX_TEXID_KIND_PDGUI       3u  /* pdgui texture (pdgui_texture.h) */
#define GFX_BGFX_TEXID_KIND_IMGUI_FONT  4u  /* ImGui font atlas */

/* Resolves a tagged ImTextureID to a bgfx texture handle index and its
 * sampler flags. Returns 0 if the id names nothing (the draw is skipped). */
int gfxBgfxResolveTextureId(uint64_t texid, uint16_t *handleIdx, uint32_t *samplerFlags);

/* Reads back a fast3d texture's pixels (RGBA8, as uploaded) into a malloc'd
 * buffer the caller frees. Returns 0 when the id is unknown or has no
 * pixels. Used by the skin capture. */
int gfxBgfxReadFast3dTexture(uint32_t texid, uint8_t **outRgba, int *outW, int *outH);

/* pdgui textures in bgfx mode -----------------------------------------------
 *
 * The one place pdgui makes the textures ImGui draws when --renderer bgfx is
 * active (scope doc section 9, item 16). The GL branches at each call site
 * stay exactly as they were for the classic renderer. All images are RGBA8.
 * `linear` picks GL_LINEAR over GL_NEAREST, `clampEdge` GL_CLAMP_TO_EDGE over
 * GL_REPEAT, mirroring the glTexParameteri calls of the GL branch. */
uint32_t gfxBgfxPdguiTexCreate(int width, int height, const uint8_t *rgba, int linear, int clampEdge);
/* glTexSubImage2D. rowLengthPixels is the source row pitch (0 = width). */
void gfxBgfxPdguiTexUpdate(uint32_t id, int x, int y, int width, int height, const uint8_t *rgba,
                           int rowLengthPixels);
void gfxBgfxPdguiTexSetSampler(uint32_t id, int linear, int clampEdge);
void gfxBgfxPdguiTexDestroy(uint32_t id);
/* A new texture holding fast3d framebuffer fbId's colour as drawn so far
 * this frame (the character preview bake). */
uint32_t gfxBgfxPdguiTexFromFramebuffer(int fbId, int linear, int clampEdge);

#ifdef __cplusplus
}
#endif

#pragma pop_macro("bool")

#endif
