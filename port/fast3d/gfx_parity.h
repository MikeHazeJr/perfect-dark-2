#ifndef GFX_PARITY_H
#define GFX_PARITY_H

/* Renderer parity mode image comparison (port/fast3d/gfx_parity.cpp). */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* glRgb: the GL back buffer as glReadPixels(GL_RGB) gives it; bgfxRgba: the
 * bgfx frame read back as RGBA8. Both bottom row first. Writes
 * <path>.bgfx.bmp, <path>.diff.bmp and <path>.parity.json and logs PARITY:. */
void gfxParityCompare(const char *path, const uint8_t *glRgb, int w, int h,
                      const uint8_t *bgfxRgba, int bw, int bh);

#ifdef __cplusplus
}
#endif

#endif
