#ifndef GFX_SDL_H
#define GFX_SDL_H

#include "gfx_window_manager_api.h"

extern struct GfxWindowManagerAPI gfx_sdl;

#ifdef __cplusplus
extern "C" {
#endif

/* s036-03 (c036, 2026-05-12): public C-callable fullscreen toggle.
 * Drives the same set_fullscreen path that the legacy Alt+Enter raw
 * handler in gfx_sdl2.cpp's event loop used; now dispatched by
 * actionPressed(ACTION_TOGGLE_FULLSCREEN) from port/src/pdsched.c. */
void gfxFullscreenToggle(void);

#ifdef __cplusplus
}
#endif

#endif
