/**
 * server_gui.cpp -- Dedicated server ImGui GUI.
 *
 * Completely self-contained: owns ImGui context, GLAD loading, and
 * all rendering. Independent of the game's pdgui_backend / gfx pipeline.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "glad/glad.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_opengl3.h"

extern "C" {

#include "system.h"

/* Net state */
extern s32 g_NetMode;
extern s32 g_NetDedicated;
extern s32 g_NetNumClients;
extern s32 g_NetMaxClients;
extern u32 g_NetServerPort;
extern const char *netUpnpGetExternalIP(void);
extern s32 netUpnpIsActive(void);
extern s32 netUpnpGetStatus(void);

/* Log ring buffer */
extern s32 sysLogRingGetCount(void);
extern const char *sysLogRingGetLine(s32 idx);

/* Lobby state */
extern void lobbyUpdate(void);

#define NETMODE_NONE   0
#define NETMODE_SERVER 1

#define UPNP_STATUS_IDLE    0
#define UPNP_STATUS_WORKING 1
#define UPNP_STATUS_SUCCESS 2
#define UPNP_STATUS_FAILED  3

} /* extern "C" */

static bool s_Initialized = false;

extern "C" s32 serverGuiInit(SDL_Window *window, void *glContext)
{
    /* Load OpenGL functions via GLAD */
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        printf("SERVER GUI: GLAD failed to load\n");
        return -1;
    }

    sysLogPrintf(LOG_NOTE, "SERVER GUI: OpenGL %s", glGetString(GL_VERSION));

    /* Create ImGui context */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    /* Dark theme */
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;

    /* Init SDL2 + OpenGL3 backends */
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    s_Initialized = true;
    sysLogPrintf(LOG_NOTE, "SERVER GUI: Initialized");
    return 0;
}

extern "C" void serverGuiProcessEvent(SDL_Event *ev)
{
    if (s_Initialized) {
        ImGui_ImplSDL2_ProcessEvent(ev);
    }
}

extern "C" void serverGuiFrame(SDL_Window *window)
{
    if (!s_Initialized) return;

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);

    /* GL state reset + clear */
    glViewport(0, 0, winW, winH);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* ImGui frame */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    /* === Server Status Panel (top) === */
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2((float)winW - 20, 80));
    if (ImGui::Begin("Server Status", nullptr,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::Columns(3, nullptr, false);

        /* Column 1: Server info */
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "PD2 DEDICATED SERVER");
        const char *ip = netUpnpIsActive() ? netUpnpGetExternalIP() : "";
        if (ip && ip[0]) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s:%u", ip, g_NetServerPort);
        } else {
            s32 upnpStatus = netUpnpGetStatus();
            if (upnpStatus == UPNP_STATUS_WORKING) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Port %u (UPnP discovering...)", g_NetServerPort);
            } else {
                ImGui::Text("Port %u", g_NetServerPort);
            }
        }

        /* Column 2: Player count */
        ImGui::NextColumn();
        ImGui::Text("Players: %d / %d", g_NetNumClients, g_NetMaxClients);

        /* Column 3: Status */
        ImGui::NextColumn();
        if (g_NetMode == NETMODE_SERVER) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "ONLINE");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "OFFLINE");
        }

        ImGui::Columns(1);
    }
    ImGui::End();

    /* === Log Panel (bottom, takes remaining space) === */
    float logTop = 100;
    float logH = (float)winH - logTop - 10;
    ImGui::SetNextWindowPos(ImVec2(10, logTop));
    ImGui::SetNextWindowSize(ImVec2((float)winW - 20, logH));
    if (ImGui::Begin("Server Log", nullptr,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false)) {
            s32 lineCount = sysLogRingGetCount();
            for (s32 i = 0; i < lineCount; i++) {
                const char *line = sysLogRingGetLine(i);
                if (strncmp(line, "ERROR:", 6) == 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", line);
                } else if (strncmp(line, "WARNING:", 8) == 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", line);
                } else if (strncmp(line, "CHAT:", 5) == 0) {
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", line);
                } else if (strncmp(line, "NET:", 4) == 0) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", line);
                } else if (strncmp(line, "UPNP:", 5) == 0) {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", line);
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 0.9f), "%s", line);
                }
            }
            /* Auto-scroll */
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    /* Finalize */
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
}

extern "C" void serverGuiShutdown(void)
{
    if (!s_Initialized) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    s_Initialized = false;
}
