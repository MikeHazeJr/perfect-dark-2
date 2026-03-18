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

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_opengl3.h"

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */

static bool g_PdguiInitialized = false;
static bool g_PdguiActive = false;  /* overlay visible? */
static SDL_Window *g_PdguiWindow = nullptr;

/* ---------------------------------------------------------------------------
 * PD-themed style (inline for now; will move to pdgui_style.c later)
 * --------------------------------------------------------------------------- */

static void pdguiApplyStyle(void)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    /* Dark background with blue/cyan accent — PD's signature palette */
    colors[ImGuiCol_WindowBg]           = ImVec4(0.06f, 0.06f, 0.12f, 0.94f);
    colors[ImGuiCol_PopupBg]            = ImVec4(0.06f, 0.06f, 0.12f, 0.94f);
    colors[ImGuiCol_Border]             = ImVec4(0.20f, 0.40f, 0.60f, 0.50f);
    colors[ImGuiCol_FrameBg]            = ImVec4(0.10f, 0.15f, 0.25f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.15f, 0.30f, 0.50f, 0.40f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.20f, 0.40f, 0.65f, 0.67f);
    colors[ImGuiCol_TitleBg]            = ImVec4(0.04f, 0.04f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.10f, 0.20f, 0.40f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_CheckMark]          = ImVec4(0.30f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.25f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]             = ImVec4(0.15f, 0.30f, 0.55f, 0.40f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.10f, 0.30f, 0.60f, 1.00f);
    colors[ImGuiCol_Header]             = ImVec4(0.15f, 0.30f, 0.55f, 0.31f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.20f, 0.40f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.25f, 0.50f, 0.80f, 1.00f);
    colors[ImGuiCol_Separator]          = ImVec4(0.20f, 0.40f, 0.60f, 0.50f);
    colors[ImGuiCol_Tab]                = ImVec4(0.10f, 0.20f, 0.40f, 0.86f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.20f, 0.40f, 0.70f, 0.80f);
    colors[ImGuiCol_TabActive]          = ImVec4(0.15f, 0.35f, 0.60f, 1.00f);
    colors[ImGuiCol_Text]              = ImVec4(0.85f, 0.90f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]      = ImVec4(0.40f, 0.45f, 0.55f, 1.00f);

    /* Rounded corners, subtle borders */
    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
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

    /* Apply PD-themed style */
    pdguiApplyStyle();

    /* Initialize SDL2 + OpenGL3 backends.
     * We use "#version 130" (GLSL 1.30 / OpenGL 3.0) which is compatible with
     * the port's OpenGL 2.1 compat / 3.x core setup. The ImGui OpenGL3 backend
     * falls back gracefully for older GL versions. */
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

    /* GL state is reset by gfx_opengl_reset_for_overlay() in gfx_pc.cpp
     * before this function is called — viewport, framebuffer, scissor, depth
     * are all set to full-window defaults using glad (the same GL loader the
     * rest of the renderer uses). */

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void pdguiRender(void)
{
    if (!g_PdguiInitialized || !g_PdguiActive) {
        return;
    }

    /* Demo window for initial integration testing — will be replaced by
     * the mod manager screen and other pdgui screens in later commits. */
    ImGui::ShowDemoWindow();

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

    /* Always let ImGui see events so it can track mouse/keyboard state,
     * even when the overlay isn't visible (needed for toggle detection). */
    ImGui_ImplSDL2_ProcessEvent((const SDL_Event *)sdlEvent);

    /* Only consume events when the overlay is active and ImGui wants them. */
    if (!g_PdguiActive) {
        return 0;
    }

    ImGuiIO &io = ImGui::GetIO();
    const SDL_Event *ev = (const SDL_Event *)sdlEvent;

    switch (ev->type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
            return io.WantCaptureMouse ? 1 : 0;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
            return io.WantCaptureKeyboard ? 1 : 0;

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
