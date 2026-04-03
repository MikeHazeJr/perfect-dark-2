/**
 * pdgui_backend.cpp — Dear ImGui integration backend for the PD PC port.
 *
 * This file implements the C-callable pdgui API (declared in pdgui.h) using
 * the ImGui C++ API with SDL2 + OpenGL3 backends. It lives in fast3d/ alongside
 * the other rendering code because it directly interfaces with SDL2 and OpenGL.
 *
 * The GLOB_RECURSE for port/*.cpp in CMakeLists.txt will auto-discover this file.
 *
 * Part of Sub-Phase D3.4: Menu System Modernization.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>

#include "glad/glad.h"
#include <string.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_opengl3.h"

/* PD-authentic style — colors, metrics, shimmer effects */
#include "pdgui_style.h"

/* F12 debug menu */
#include "pdgui_debugmenu.h"

/* F8 in-game menu hot-swap */
#include "pdgui_hotswap.h"
#include "pdgui_menus.h"
#include "pdgui_charpreview.h"

/* Lobby sidebar — declared in pdgui_lobby.cpp */
extern "C" void pdguiLobbyRender(s32 winW, s32 winH);
extern "C" void pdguiUpdateRender(void);
extern "C" s32  pdguiUpdateIsActive(void);

/* D3R-7: Modding Hub standalone window — declared in pdgui_menu_moddinghub.cpp */
extern "C" void pdguiModdingHubRender(s32 winW, s32 winH);
extern "C" s32  pdguiModdingHubIsVisible(void);

/* Log Viewer Dev Window tab — declared in pdgui_menu_logviewer.cpp */
extern "C" void pdguiLogViewerRender(s32 winW, s32 winH);

/* Pause menu + scorecard overlay — declared in pdgui_pausemenu.h */
#include "pdgui_pausemenu.h"

/* In-match HUD overlay (top scorers + timer) */
#include "pdgui_hud.h"

/* MP In-Game overlays: kill ticker + endscreen suppression */
extern "C" void pdguiMpIngameRender(s32 winW, s32 winH);

/* Pre-match countdown popup: 3-2-1-GO + cancel support */
extern "C" void pdguiCountdownRender(s32 winW, s32 winH);

/* Network mode query — declared in pdgui_bridge.c */
extern "C" s32 netGetMode(void);

/* Menu state manager (menumgr.c) */
extern "C" s32 menuIsInCooldown(void);
extern "C" s32 menuIsOpen(void);

/* Input system -- for deferred SDL mouse-lock flush (B-92 solo mission path) */
extern "C" s32 inputMouseIsLocked(void);

/* Handel Gothic — PD's original menu font, embedded as a C array */
#include "pdgui_font_handelgothic.h"

/* Resolution-independent scaling helpers */
#include "pdgui_scaling.h"

/* Logging */
#include "system.h"

/* Forward declaration only — game/lang.h includes data.h which #define bool s32 */
extern "C" char *langGet(s32 textid);

/* ---------------------------------------------------------------------------
 * Exported utilities
 * --------------------------------------------------------------------------- */

extern "C" const char *langSafe(s32 textid)
{
    const char *s = langGet(textid);
    return s ? s : "";
}

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */

static bool g_PdguiInitialized = false;
static bool g_PdguiActive = false;  /* overlay visible? */
static SDL_Window *g_PdguiWindow = nullptr;

/* Mouse grab state saved when overlay opens, restored when it closes */
static SDL_bool g_PdguiSavedRelativeMode = SDL_FALSE;
static int      g_PdguiSavedShowCursor   = 0;

/**
 * When the overlay opens: release the mouse grab so ImGui gets absolute
 * coordinates and the cursor is visible. When it closes: restore whatever
 * grab state the game had before we touched it.
 */
static void pdguiUpdateMouseGrab(bool overlayActive)
{
    if (overlayActive) {
        /* Save current state */
        g_PdguiSavedRelativeMode = SDL_GetRelativeMouseMode();
        g_PdguiSavedShowCursor   = SDL_ShowCursor(SDL_QUERY);

        /* Release grab: give ImGui absolute coordinates and a visible cursor */
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);

        /* Warp the cursor to the center of the window so it doesn't start
         * at some off-screen position from relative mode */
        if (g_PdguiWindow) {
            int w, h;
            SDL_GetWindowSize(g_PdguiWindow, &w, &h);
            SDL_WarpMouseInWindow(g_PdguiWindow, w / 2, h / 2);
        }
    } else {
        /* Restore previous state */
        SDL_SetRelativeMouseMode(g_PdguiSavedRelativeMode);
        SDL_ShowCursor(g_PdguiSavedShowCursor ? SDL_ENABLE : SDL_DISABLE);
    }
}

/* ---------------------------------------------------------------------------
 * C-callable API (extern "C" for linkage with video.c, main.c, etc.)
 * --------------------------------------------------------------------------- */

extern "C" {

void pdguiInit(void *sdlWindow)
{
    if (g_PdguiInitialized) {
        return;
    }

    g_PdguiWindow = (SDL_Window *)sdlWindow;

    /* Create ImGui context */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    /* Load Handel Gothic — PD's original menu font, embedded in the binary.
     * ImGui takes ownership of the copy, so we must allocate + memcpy.
     * AddFontFromMemoryTTF takes ownership and will free the buffer. */
    {
        void *fontCopy = ImGui::MemAlloc(g_HandelGothicFont_size);
        memcpy(fontCopy, g_HandelGothicFont_data, g_HandelGothicFont_size);

        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = true;  /* ImGui will free fontCopy */
        snprintf(cfg.Name, sizeof(cfg.Name), "Handel Gothic Regular");
        cfg.OversampleV = 2;  /* Extra vertical rasterization quality */

        /* Extra atlas padding so descenders (q, y, p, g) aren't clipped
         * at the glyph boundary in the texture. Default is 1. */
        io.Fonts->TexGlyphPadding = 2;

        /* Load at a higher base size (24pt) so the font atlas has enough
         * detail for game-relative scaling at 1080p+. FontGlobalScale is
         * set each frame (pdguiNewFrame) to scale proportionally with
         * display height, keeping text within scaled button/row heights. */
        ImFont *font = io.Fonts->AddFontFromMemoryTTF(
            fontCopy, (int)g_HandelGothicFont_size, 24.0f, &cfg);

        if (font) {
            io.FontDefault = font;
            sysLogPrintf(LOG_NOTE, "pdgui: Loaded embedded Handel Gothic (%u bytes)",
                         g_HandelGothicFont_size);
        } else {
            sysLogPrintf(LOG_NOTE, "pdgui: Failed to load embedded Handel Gothic");
        }
    }

    /* Apply PD-authentic style (colors, sharp corners, compact metrics) */
    pdguiApplyPdStyle();

    /* Initialize SDL2 + OpenGL3 backends.
     * We use "#version 130" (GLSL 1.30 / OpenGL 3.0) which matches the port's
     * default GL 3.0 compatibility profile context. */
    SDL_GLContext glCtx = SDL_GL_GetCurrentContext();
    ImGui_ImplSDL2_InitForOpenGL(g_PdguiWindow, glCtx);
    ImGui_ImplOpenGL3_Init("#version 130");

    g_PdguiInitialized = true;
    g_PdguiActive = false;

    /* Initialize the F8 in-game hot-swap system (D4) */
    pdguiHotswapInit();

    /* Register all ImGui menu replacements */
    pdguiMenusRegisterAll();

    /* Initialize the character preview FBO system */
    pdguiCharPreviewInit();
}

void pdguiNewFrame(void)
{
    bool networkActive = (netGetMode() != 0);
    bool pauseActive = (pdguiIsPauseMenuOpen() || pdguiIsScorecardVisible());
    bool hubActive = (pdguiModdingHubIsVisible() != 0);

    if (!g_PdguiInitialized ||
        (!g_PdguiActive && !pdguiHotswapHasQueued() && !pdguiHotswapWasActive() &&
         !networkActive && !pauseActive && !hubActive)) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    /* Scale font with display height so the 24pt atlas renders proportionally
     * at all resolutions. At 720p scale=1.0 (24pt effective). At 800x600
     * scale≈0.83 → ~20pt effective, fitting within scaled button heights.
     * Subsystems that override this (debug menu, storyboard) must restore
     * to pdguiScaleFactor() — NOT 1.0f — so subsequent renderers stay correct. */
    ImGui::GetIO().FontGlobalScale = pdguiScaleFactor();

    ImGui::NewFrame();
}

/* ---- Live Console (backtick toggle) ---- */
static bool s_ConsoleVisible = false;

void pdguiConsoleToggle(void)
{
    s_ConsoleVisible = !s_ConsoleVisible;
}

static void pdguiConsoleRender(void)
{
    if (!s_ConsoleVisible || !g_PdguiInitialized) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.7f);

    /* NoFocusOnAppearing + NoNavFocus + NoNavInputs = overlay that doesn't steal input.
     * Player can still move, shoot, jump while the console is visible. */
    ImGuiWindowFlags consoleFlags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("Console", &s_ConsoleVisible, consoleFlags)) {
        s32 count = sysLogRingGetCount();
        ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (s32 i = 0; i < count; i++) {
            const char *line = sysLogRingGetLine(i);
            /* Color-code by prefix */
            if (strstr(line, "ERROR:")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            } else if (strstr(line, "WARNING:")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            } else if (strstr(line, "MESHCOL:") || strstr(line, "JUMP:") || strstr(line, "CAPSULE")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            } else if (strstr(line, "LOAD:")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            }
            ImGui::TextUnformatted(line);
            ImGui::PopStyleColor();
        }
        /* Auto-scroll to bottom */
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void pdguiRender(void)
{
    /* Console renders independently of the debug overlay */
    if (s_ConsoleVisible && g_PdguiInitialized) {
        pdguiConsoleRender();
    }

    bool hotswapQueued = pdguiHotswapHasQueued() != 0;
    bool hotswapWasActive = pdguiHotswapWasActive() != 0;

    bool networkActive = (netGetMode() != 0);
    bool updateActive = (pdguiUpdateIsActive() != 0);
    bool pauseActive = (pdguiIsPauseMenuOpen() || pdguiIsScorecardVisible());
    bool hubActive = (pdguiModdingHubIsVisible() != 0);

    /* D13: Also render when update UI is visible (notification banner, version picker) */
    if (!g_PdguiInitialized ||
        (!g_PdguiActive && !s_ConsoleVisible && !hotswapQueued && !hotswapWasActive &&
         !networkActive && !updateActive && !pauseActive && !hubActive)) {
        return;
    }

    /* Get window dimensions for game-relative scaling.
     * The SDL window size drives the coordinate system after
     * gfx_opengl_reset_for_overlay resets the viewport. */
    int winW = 0, winH = 0;
    if (g_PdguiWindow) {
        SDL_GetWindowSize(g_PdguiWindow, &winW, &winH);
    }
    if (winW <= 0 || winH <= 0) {
        winW = 640;
        winH = 480;
    }

    /* F12 debug menu — PD-styled, game-relative scaling */
    if (g_PdguiActive) {
        pdguiDebugMenuRender((s32)winW, (s32)winH);
        /* Log Viewer Dev Window — shown alongside the debug menu */
        pdguiLogViewerRender((s32)winW, (s32)winH);
    }

    /* F8 hot-swap: render any ImGui menu replacements that were queued
     * during the GBI phase by pdguiHotswapCheck() in menuRenderDialog().
     *
     * ALWAYS call pdguiHotswapRenderQueued so the persistent WasActive flag
     * stays in sync. If we skip the call (e.g., during gameplay with only
     * the lobby sidebar active), the flag stays stale from the last menu
     * frame and pdguiIsActive/pdguiWantsInput block all game input. */
    pdguiHotswapRenderQueued((s32)winW, (s32)winH);

    /* B-92 (solo mission ImGui path): pdguiHotswapRenderQueued() just updated
     * s_HotswapMenuWasActive.  When a mission is accepted from the ImGui overview
     * dialog, menuhandlerAcceptMission() calls menuStop() + inputLockMouse(1)
     * from inside the hotswap render callback.  At that point pdguiIsActive()
     * still returns true (WasActive is still set for this frame), so
     * inputLockMouse() sets mouseLocked=true but defers SDL_SetRelativeMouseMode.
     * The next frame the hotswap queue is empty, WasActive drops to false —
     * but nothing applies the SDL state unless we do it here.
     * Covers all hotswap→gameplay transitions: solo mission accept, endscreen
     * retry/next, and any future hotswap dialog that calls menuStop(). */
    {
        bool hotswapNowActive = (pdguiHotswapWasActive() != 0);
        if (hotswapWasActive && !hotswapNowActive &&
                !g_PdguiActive && !pdguiIsPauseMenuOpen()) {
            if (inputMouseIsLocked()) {
                SDL_ShowCursor(SDL_DISABLE);
                SDL_SetRelativeMouseMode(SDL_TRUE);
                sysLogPrintf(LOG_NOTE,
                    "pdgui: hotswap closed, flushed deferred mouse capture (B-92 solo)");
            }
        }
    }

    /* D3R-7: Modding Hub standalone window — renders when opened from main menu */
    pdguiModdingHubRender((s32)winW, (s32)winH);

    /* Network lobby player list sidebar — shows connected players when
     * in a networked session. Renders independently of hotswap state. */
    pdguiLobbyRender((s32)winW, (s32)winH);

    /* Pre-match countdown: 3-2-1-GO popup + cancel banner.
     * Renders over everything during MANIFEST_PHASE_LOADING; no-op otherwise. */
    pdguiCountdownRender((s32)winW, (s32)winH);

    /* D13: Update notification banner, version picker, download progress.
     * Renders as overlay — independent of hotswap and menu state. */
    pdguiUpdateRender();

    /* Combat sim pause menu + hold-to-show scorecard overlay.
     * Rendered independently of hotswap/menu state — active during gameplay. */
    pdguiPauseMenuRender((s32)winW, (s32)winH);
    pdguiScorecardRender((s32)winW, (s32)winH);

    /* In-match HUD: top 2 scorers + remaining time.
     * Only visible during normmplayerisrunning (combat sim active). */
    pdguiHudRender((s32)winW, (s32)winH);

    /* Kill/score ticker overlay: slide-in notifications for score events.
     * Active during normmplayerisrunning; auto-suppressed on game over. */
    pdguiMpIngameRender((s32)winW, (s32)winH);

    /* Post-match overlay (MATCH OVER screen + "Return to Lobby" button).
     * Without this call the game freezes on match end — the N64 menu system
     * drove the post-match transition, but the PC port ImGui doesn't observe
     * prevmenuroot.  pdguiGameOverRender is a no-op unless MPPAUSEMODE_GAMEOVER
     * is active, so it is safe to call every frame. */
    pdguiGameOverRender((s32)winW, (s32)winH);


    /* Add PD-style shimmer effects to all visible windows via foreground draw list.
     * This adds the animated border highlights that are PD's signature look. */
    pdguiRenderAllWindowShimmers();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/**
 * Server-only render frame: clear screen, draw ImGui, swap buffers.
 * Called from the dedicated server's main loop (server_main.c).
 * The game client uses gfx_run() which handles this differently.
 */
void pdguiServerFrame(void)
{
    if (!g_PdguiInitialized || !g_PdguiWindow) {
        return;
    }

    /* One-time GL setup: ensure context is current and GLAD is loaded.
     * gfx_init creates the context but the server doesn't call gfx_run
     * which normally manages GL state each frame. */
    static bool s_GlReady = false;
    if (!s_GlReady) {
        SDL_GLContext ctx = SDL_GL_GetCurrentContext();
        if (!ctx) {
            /* No GL context — can't render */
            return;
        }
        SDL_GL_MakeCurrent(g_PdguiWindow, ctx);
        /* GLAD should already be loaded by gfx_init, but verify */
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            sysLogPrintf(LOG_ERROR, "pdguiServerFrame: GLAD failed to load");
            return;
        }
        s_GlReady = true;
        sysLogPrintf(LOG_NOTE, "pdguiServerFrame: GL context ready");
    }

    int winW = 0, winH = 0;
    SDL_GetWindowSize(g_PdguiWindow, &winW, &winH);
    if (winW <= 0 || winH <= 0) return;

    /* Update window title (server doesn't call videoEndFrame) */
    {
        static u32 s_TitleCounter = 0;
        if (++s_TitleCounter >= 60) {
            s_TitleCounter = 0;
            extern s32 g_NetMode;
            extern s32 g_NetNumClients;
            extern s32 g_NetMaxClients;
            extern u32 g_NetServerPort;
            extern const char *netUpnpGetExternalIP(void);
            extern s32 netUpnpIsActive(void);

            char titleBuf[256];
            const char *ip = netUpnpIsActive() ? netUpnpGetExternalIP() : "";
            if (ip && ip[0]) {
                snprintf(titleBuf, sizeof(titleBuf),
                         "PD2 Dedicated Server - %s:%u - %d/%d connected",
                         ip, g_NetServerPort, g_NetNumClients, g_NetMaxClients);
            } else {
                snprintf(titleBuf, sizeof(titleBuf),
                         "PD2 Dedicated Server - port %u - %d/%d connected",
                         g_NetServerPort, g_NetNumClients, g_NetMaxClients);
            }
            SDL_SetWindowTitle(g_PdguiWindow, titleBuf);
        }
    }

    /* GL clear */
    glViewport(0, 0, winW, winH);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* ImGui frame */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    /* Server GUI: render a simple test panel to confirm ImGui works,
     * plus the lobby overlay (server info, player list, log) */
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(300, 100));
    if (ImGui::Begin("Server Status", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("PD2 Dedicated Server");
        ImGui::Text("Window: %dx%d", winW, winH);
        extern s32 g_NetMode;
        extern s32 g_NetDedicated;
        ImGui::Text("NetMode: %d  Dedicated: %d", g_NetMode, g_NetDedicated);
    }
    ImGui::End();

    pdguiLobbyRender((s32)winW, (s32)winH);

    /* Always call hotswap render to keep the WasActive flag in sync */
    pdguiHotswapRenderQueued((s32)winW, (s32)winH);

    /* Shimmer effects */
    pdguiRenderAllWindowShimmers();

    /* Finalize and draw */
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(g_PdguiWindow);
}

void pdguiShutdown(void)
{
    if (!g_PdguiInitialized) {
        return;
    }

    pdguiHotswapShutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    g_PdguiInitialized = false;
    g_PdguiActive = false;
    g_PdguiWindow = nullptr;
}

s32 pdguiProcessEvent(void *sdlEvent)
{
    if (!g_PdguiInitialized) {
        return 0;
    }

    /* Menu manager cooldown: consume ALL input during transition frames
     * to prevent double-press when opening/closing menus. */
    if (menuIsInCooldown()) {
        const SDL_Event *cooldownEv = (const SDL_Event *)sdlEvent;
        /* Still forward to ImGui for state tracking, but consume from game */
        ImGui_ImplSDL2_ProcessEvent(cooldownEv);
        if (cooldownEv->type == SDL_KEYDOWN || cooldownEv->type == SDL_KEYUP ||
            cooldownEv->type == SDL_CONTROLLERBUTTONDOWN || cooldownEv->type == SDL_CONTROLLERBUTTONUP) {
            return 1;
        }
    }

    const SDL_Event *ev = (const SDL_Event *)sdlEvent;

    /* F8 / RS-click hot-swap toggle — does NOT consume input or open an overlay.
     * Just flips the rendering mode for menus that have ImGui replacements. */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F8) {
        pdguiHotswapToggle();
        return 1;  /* consumed — PD never sees F8 */
    }
    if (ev->type == SDL_CONTROLLERBUTTONDOWN &&
        ev->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
        pdguiHotswapToggle();
        return 1;
    }

    /* F12 debug overlay toggle */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F12) {
        g_PdguiActive = !g_PdguiActive;
        pdguiUpdateMouseGrab(g_PdguiActive);
        return 1;  /* consumed — PD never sees F12 */
    }

    /* Always forward events to ImGui so it can track mouse/keyboard state.
     * This includes gamepad events for ImGuiConfigFlags_NavEnableGamepad. */
    ImGui_ImplSDL2_ProcessEvent(ev);

    /* Determine if any ImGui surface is actively wanting input:
     * - Debug overlay (F12)
     * - Hot-swapped menu (F8)
     * - Pause menu (in-game)
     *
     * For hotswap, use pdguiHotswapWasActive() which persists from the
     * previous frame's render. pdguiHotswapHasQueued() would be 0 here
     * because events are processed BEFORE the GBI phase queues new dialogs. */
    bool overlayActive = g_PdguiActive;
    bool hotswapActive = pdguiHotswapWasActive() != 0 || pdguiHotswapHasQueued() != 0;
    bool pauseActive = pdguiIsPauseMenuOpen() != 0;

    if (!overlayActive && !hotswapActive && !pauseActive) {
        return 0;
    }

    ImGuiIO &io = ImGui::GetIO();

    switch (ev->type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
            return io.WantCaptureMouse ? 1 : 0;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
            /* When overlay, hot-swap, or pause menu is active, consume ALL
             * keyboard input so the game doesn't act on keys meant for ImGui. */
            if (overlayActive || hotswapActive || pauseActive) return 1;
            return 0;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
        case SDL_JOYAXISMOTION:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
        case SDL_JOYHATMOTION:
            /* When ImGui is handling a hot-swapped menu or overlay,
             * consume gamepad input so PD doesn't also process it. */
            return 1;

        default:
            return 0;
    }
}

s32 pdguiWantsInput(void)
{
    if (!g_PdguiInitialized) {
        return 0;
    }

    /* Hot-swapped menus (F8) consume all input while active.
     * Uses pdguiHotswapWasActive() which persists from the previous
     * frame's render — pdguiHotswapHasQueued() would be 0 here
     * because the queue was cleared after last frame's render. */
    if (pdguiHotswapWasActive()) {
        return 1;
    }

    /* ImGui pause menu consumes all input when open */
    if (pdguiIsPauseMenuOpen()) {
        return 1;
    }

    if (!g_PdguiActive) {
        return 0;
    }

    ImGuiIO &io = ImGui::GetIO();
    return (io.WantCaptureKeyboard || io.WantCaptureMouse) ? 1 : 0;
}

s32 pdguiIsActive(void)
{
    /* F12 debug overlay, F8 hot-swapped menus, or ImGui pause menu */
    return (g_PdguiActive || pdguiHotswapWasActive() || pdguiIsPauseMenuOpen()) ? 1 : 0;
}

void pdguiToggle(void)
{
    g_PdguiActive = !g_PdguiActive;
    pdguiUpdateMouseGrab(g_PdguiActive);
}

} /* extern "C" */
