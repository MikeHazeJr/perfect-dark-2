#ifndef _IN_PDGUI_H
#define _IN_PDGUI_H

/**
 * pdgui.h — Public API for the Dear ImGui integration layer.
 *
 * This provides the C-callable interface used by the game loop (video.c, main.c)
 * to initialize, drive, and shut down the ImGui overlay. The actual ImGui calls
 * happen in pdgui_backend.cpp; this header exposes only the C interface.
 *
 * Part of Sub-Phase D3.4: Menu System Modernization.
 */

#include <PR/ultratypes.h>

/* Initialize ImGui with PD's SDL2 window and OpenGL context.
 * Called once after videoInit() in main.c.
 * sdlWindow is a void* to avoid pulling SDL headers into game code. */
void pdguiInit(void *sdlWindow);

/* Begin a new ImGui frame. Called once per frame after GBI rendering
 * (videoSubmitCommands) but before pdguiRender/videoEndFrame. */
void pdguiNewFrame(void);

/* Render all active ImGui content and finalize the frame.
 * Calls ImGui::Render() and draws the result via OpenGL. */
void pdguiRender(void);

/* Clean up ImGui resources. Called during shutdown (cleanup in main.c). */
void pdguiShutdown(void);

/* Pass an SDL event to ImGui for processing.
 * Called from gfx_sdl_handle_events() for each event BEFORE PD's own handling.
 * Returns true (non-zero) if ImGui consumed the event (suppress PD input).
 * sdlEvent is a void* to SDL_Event. */
s32 pdguiProcessEvent(void *sdlEvent);

/* Query whether ImGui currently wants keyboard or mouse input.
 * When true, PD's input system should be suppressed to avoid conflicts. */
s32 pdguiWantsInput(void);

/* Query whether the ImGui overlay is currently active/visible. */
s32 pdguiIsActive(void);

/* Toggle the ImGui overlay on/off (e.g., bound to a key). */
void pdguiToggle(void);

/* Reset the main menu to the top-level view (s_MenuView = 0).
 * Call on disconnect so the menu re-opens at the root, not "Online Play". */
void pdguiMainMenuReset(void);

/* D5.0: Return an ImTextureID (GLuint cast to void*) for a named UI texture.
 * Delegates to pdguiThemeGetTexture() in pdgui_theme.cpp.
 * Textures are decoded from ROM (N64 RGBA16/IA16/IA8/CI4/CI8 → RGBA32 → GL)
 * during pdguiInit() and registered in the asset catalog as ASSET_UI.
 * NULL return = pipeline bug (unknown id or GBI decode not yet done):
 *   LOG_ERROR + assert, no fallback. */
void* pdguiGetUiTexture(const char *id);

/* Null-safe langGet wrapper. Returns langGet(textid) or "" if NULL.
 * Use this everywhere a langGet result goes to ImGui to prevent 0xc0000005. */
const char *langSafe(s32 textid);

#endif /* _IN_PDGUI_H */
