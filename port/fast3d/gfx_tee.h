#ifndef GFX_TEE_H
#define GFX_TEE_H

/* Renderer parity mode (--renderer-parity, port/fast3d/gfx_tee.cpp): every
 * fast3d call goes to the classic GL backend, which presents, and to the bgfx
 * backend, which renders the same frame offscreen; smoke screenshots compare
 * the two images (gfx_parity.cpp). */

#include "gfx_rendering_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct GfxRenderingAPI gfx_tee_api;

/* Nonzero when the command line asks for parity mode. */
int gfxTeeRequested(void);

#ifdef __cplusplus
}
#endif

#endif
