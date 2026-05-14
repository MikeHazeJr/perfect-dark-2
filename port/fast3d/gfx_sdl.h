#ifndef GFX_SDL_H
#define GFX_SDL_H

#include "gfx_window_manager_api.h"

/* SDL_Window is an opaque struct in SDL2's public ABI; the smoke harness
 * and any other C consumer only needs a pointer-typedef. Forward-declare
 * here so gfxGetSdlWindow can be reached without pulling all of <SDL.h>
 * into headers that just want the handle. */
struct SDL_Window;
typedef struct SDL_Window SDL_Window;

extern struct GfxWindowManagerAPI gfx_sdl;

#ifdef __cplusplus
extern "C" {
#endif

/* s036-03 (c036, 2026-05-12): public C-callable fullscreen toggle.
 * Drives the same set_fullscreen path that the legacy Alt+Enter raw
 * handler in gfx_sdl2.cpp's event loop used; now dispatched by
 * actionPressed(ACTION_TOGGLE_FULLSCREEN) from port/src/pdsched.c. */
void gfxFullscreenToggle(void);

/* c115 (2026-05-14): public C accessor for the SDL window handle.
 * Returns the live SDL_Window* owned by gfx_sdl2.cpp, or NULL if the
 * window has not been created yet. Used by the smoke harness to stamp
 * synthesised SDL events with the correct windowID -- ImGui's SDL2
 * backend filters out events whose windowID does not match the one
 * captured at ImGui_ImplSDL2_Init time. */
SDL_Window *gfxGetSdlWindow(void);

#ifdef __cplusplus
}
#endif

#endif
