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
#include <string.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_opengl3.h"

/* PD-authentic style — colors, metrics, shimmer effects */
#include "pdgui_style.h"

/* Handel Gothic — PD's original menu font, embedded as a C array */
#include "pdgui_font_handelgothic.h"

/* Logging */
#include "system.h"

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */

static bool g_PdguiInitialized = false;
static bool g_PdguiActive = false;  /* overlay visible? */
static SDL_Window *g_PdguiWindow = nullptr;

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

    /* Load Handel Gothic — PD's original menu font, embedded in the binary.
     * ImGui takes ownership of the copy, so we must allocate + memcpy.
     * AddFontFromMemoryTTF takes ownership and will free the buffer. */
    {
        void *fontCopy = ImGui::MemAlloc(g_HandelGothicFont_size);
        memcpy(fontCopy, g_HandelGothicFont_data, g_HandelGothicFont_size);

        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = true;  /* ImGui will free fontCopy */
        snprintf(cfg.Name, sizeof(cfg.Name), "Handel Gothic Regular");

        ImFont *font = io.Fonts->AddFontFromMemoryTTF(
            fontCopy, (int)g_HandelGothicFont_size, 16.0f, &cfg);

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
}

void pdguiNewFrame(void)
{
    if (!g_PdguiInitialized || !g_PdguiActive) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void pdguiRender(void)
{
    if (!g_PdguiInitialized || !g_PdguiActive) {
        return;
    }

    /* Demo window as placeholder for testing — will be replaced by the mod
     * manager screen (modmenu.c) and other pdgui screens in later commits. */
    bool show = true;
    ImGui::ShowDemoWindow(&show);

    /* Add PD-style shimmer effects to all visible windows via foreground draw list.
     * This adds the animated border highlights that are PD's signature look. */
    pdguiRenderAllWindowShimmers();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void pdguiShutdown(void)
{
    if (!g_PdguiInitialized) {
        return;
    }

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

    const SDL_Event *ev = (const SDL_Event *)sdlEvent;

    /* F12 toggle is handled here (not in gfx_sdl2) so we can consume the
     * event and prevent it from reaching PD's input system. */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F12) {
        g_PdguiActive = !g_PdguiActive;
        return 1;  /* consumed — PD never sees F12 */
    }

    /* Always forward events to ImGui so it can track mouse/keyboard state. */
    ImGui_ImplSDL2_ProcessEvent(ev);

    /* When the overlay is active, consume input events that ImGui wants. */
    if (!g_PdguiActive) {
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
            /* When overlay is active, consume ALL keyboard input so the game
             * doesn't act on keys meant for ImGui (prevents accidental jumps,
             * menu actions, etc. while the overlay is open). */
            return 1;

        default:
            return 0;
    }
}

s32 pdguiWantsInput(void)
{
    if (!g_PdguiInitialized || !g_PdguiActive) {
        return 0;
    }

    ImGuiIO &io = ImGui::GetIO();
    return (io.WantCaptureKeyboard || io.WantCaptureMouse) ? 1 : 0;
}

s32 pdguiIsActive(void)
{
    return g_PdguiActive ? 1 : 0;
}

void pdguiToggle(void)
{
    g_PdguiActive = !g_PdguiActive;
}

} /* extern "C" */
