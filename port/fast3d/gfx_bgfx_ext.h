#ifndef GFX_BGFX_EXT_H
#define GFX_BGFX_EXT_H

/*
 * Shared between the game's bgfx backend (port/fast3d/gfx_bgfx.cpp) and the
 * patched copy of bgfx's renderer_gl.cpp (third_party/bgfx-pd/CMakeLists.txt).
 *
 * bgfx has no depth bias or polygon offset state, but fast3d's decal draws
 * (ZMODE_DEC) use glPolygonOffset(-2, -2) in the classic GL renderer. The
 * patch maps one unused bgfx state bit to pdBgfxGlPolygonOffset(), which the
 * game implements with the same GL calls, so decals rasterise exactly as they
 * do today. Bits 59 and 60 sit between BGFX_STATE_CONSERVATIVE_RASTER and
 * BGFX_STATE_RESERVED_MASK and are unused by bgfx 651c203.
 */

#include <stdint.h>

#define PD_BGFX_STATE_POLYGON_OFFSET UINT64_C(0x0800000000000000)

#ifdef __cplusplus
extern "C" {
#endif

/* Enable (nonzero) or disable the decal polygon offset on the current GL
 * context: GL_POLYGON_OFFSET_FILL with glPolygonOffset(-2, -2), or disabled
 * with glPolygonOffset(0, 0), matching gfx_opengl_set_depth_mode. */
void pdBgfxGlPolygonOffset(int enable);

#ifdef __cplusplus
}
#endif

#endif
