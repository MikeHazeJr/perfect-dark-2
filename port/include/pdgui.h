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

#ifdef __cplusplus
extern "C" {
#endif

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

/* Pass an SDL event to ImGui and the input-context dispatcher.
 * Called from gfx_sdl_handle_events() for each event before ordinary PD input.
 * Main-window focus lifecycle is non-consumable core state and is routed first.
 * Returns true (non-zero) when UI/input context consumes the event.
 * sdlEvent is a void* to SDL_Event. */
s32 pdguiProcessEvent(void *sdlEvent);

/* Query whether ImGui currently wants keyboard or mouse input.
 * When true, PD's input system should be suppressed to avoid conflicts. */
s32 pdguiWantsInput(void);

/* Query whether the ImGui overlay is currently active/visible. */
s32 pdguiIsActive(void);

/* Toggle the ImGui overlay on/off (e.g., bound to a key). */
void pdguiToggle(void);

/* Toggle the in-game console (backquote key). Driven by
 * ACTION_CONSOLE_TOGGLE after s036-03 (c036, 2026-05-12); previously a
 * raw SDL handler in gfx_sdl2.cpp's event loop. */
void pdguiConsoleToggle(void);

/* Reset the main menu to the top-level view (s_MenuView = 0).
 * Call on disconnect so the menu re-opens at the root. */
void pdguiMainMenuReset(void);

/* B-303 (2026-05-01): open the canonical Main Menu dialog over CI and switch
 * its inline view to the requested page. Used by the post-exit auto-pop
 * mechanism in menutick: after a campaign Mission "Exit to Main Menu" we
 * auto-pop with view=1 (Play / Mission Select); after a Forge "End
 * Match" exit we auto-pop with view=0 (top-level Main Menu). View indices
 * match s_MenuView in pdgui_menu_mainmenu.cpp:
 *   0 = top-level Main Menu (Play / Social / Settings / etc.)
 *   1 = Play (Mission Select)
 *   2 = Settings, 3 = Modding, 5 = Player Stats, 6 = The Grid.
 *   4 is reserved/deprecated and is clamped back to the top level.
 * Negative values are clamped to 0. The push uses g_CiMenuViaPauseMenuDialog
 * (the same dialog that the in-game Pause press opens) so the menu pool
 * dedup, input context attachment, and animated chrome all match the manual
 * Pause-press path. */
void pdguiMainMenuOpenAtView(s32 view, const char *reason);

/* B-195: Clear ImGui's nav/focus/active-id state. Call when the last imgui
 * menu closes so ImGui's WantCaptureKeyboard drops back to false. Without
 * this, NavWindow / ActiveId references from the just-closed menu can
 * stick until the next menu opens, and pdguiProcessEvent's
 * WantCaptureKeyboard gate (pdgui_backend.cpp:851) keeps routing WASD /
 * gameplay keys into ImGui instead of the action map. Symptom: after
 * closing a menu, WASD / Space are dead while Escape still works (that
 * key is explicitly whitelisted), and the menu "doubles up" when the
 * user tries to re-open it. */
void pdguiClearImGuiFocusAndNav(void);

/* D5.0: Return an ImTextureID (GLuint cast to void*) for a named UI texture.
 * Delegates to pdguiThemeGetTexture() in pdgui_theme.cpp.
 * Textures are decoded from ROM (N64 RGBA16/IA16/IA8/CI4/CI8 → RGBA32 → GL)
 * during pdguiInit() and registered in the asset catalog as ASSET_UI.
 * NULL return = pipeline bug (unknown id or GBI decode not yet done):
 *   LOG_ERROR + assert, no fallback. */
void* pdguiGetUiTexture(const char *id);

/* Request an immediate font atlas rebuild at the start of the next frame.
 * Call after changing the active font (e.g., pdguiFontModSetActiveId).
 * The rebuild happens between frames — safe to call from UI code. */
void pdguiRequestFontAtlasRebuild(void);

/* Carrington Institute opening fly-in: suppress gameplay-only overlays. */
s32 pdguiCiIntroBlocksInteractPrompt(void);
s32 pdguiCutsceneSkipPromptShouldRender(void);
s32 pdguiCutsceneSkipPromptPlayer(void);

/* Null-safe langGet wrapper. Returns langGet(textid) or "" if NULL.
 * Use this everywhere a langGet result goes to ImGui to prevent 0xc0000005. */
const char *langSafe(s32 textid);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_H */
