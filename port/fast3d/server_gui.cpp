/**
 * server_gui.cpp -- Dedicated server ImGui GUI.
 *
 * Completely self-contained: owns ImGui context, GLAD loading, and
 * all rendering. Independent of the game's pdgui_backend / gfx pipeline.
 *
 * Layout:
 *   Top:    Server status bar (IP, port, players, game mode, online status)
 *   Middle: Tabbed panel
 *             - "Server" tab: player list (left) + controls (right)
 *             - "Hub" tab: hub state + room list
 *   Bottom: Log panel with color-coded output
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
#include "connectcode.h"
#include "hub.h"
#include "room.h"

/* Net state */
extern s32 g_NetMode;
extern s32 g_NetDedicated;
extern s32 g_NetNumClients;
extern s32 g_NetMaxClients;
extern u32 g_NetServerPort;
extern u8  g_NetGameMode;
extern const char *netUpnpGetExternalIP(void);
extern s32 netUpnpIsActive(void);
extern s32 netUpnpGetStatus(void);

/* Net functions */
extern void netServerStageStart(void);
extern void netServerStageEnd(void);
extern void netServerKickClient(s32 clientId, const char *reason);
extern s32  netDisconnect(void);

/* Log ring buffer */
extern s32 sysLogRingGetCount(void);
extern const char *sysLogRingGetLine(s32 idx);

/* Lobby state */
extern s32 lobbyGetPlayerCount(void);

#define NETMODE_NONE   0
#define NETMODE_SERVER 1

#define CLSTATE_DISCONNECTED 0
#define CLSTATE_CONNECTING   1
#define CLSTATE_AUTH         2
#define CLSTATE_LOBBY        3
#define CLSTATE_GAME         4

#define GAMEMODE_MP   0
#define GAMEMODE_COOP 1
#define GAMEMODE_ANTI 2

#define UPNP_STATUS_IDLE    0
#define UPNP_STATUS_WORKING 1
#define UPNP_STATUS_SUCCESS 2
#define UPNP_STATUS_FAILED  3

#define NET_MAX_CLIENTS 8

/* Lobby player view — matches pdgui_bridge.c definition */
struct lobbyplayer_view {
    u8 active;
    u8 isLeader;
    u8 isReady;
    u8 headnum;
    u8 bodynum;
    u8 team;
    char name[32];  /* matches LOBBY_NAME_LEN */
    s32 isLocal;
    s32 state;
};

extern s32 lobbyGetPlayerInfo(s32 idx, struct lobbyplayer_view *out);

/* Net client ping access (bridge) */
extern u32 netGetClientPing(s32 clientId);

} /* extern "C" */

static bool s_Initialized = false;

/* ========================================================================
 * Helpers
 * ======================================================================== */

static const char *gameModeStr(u8 mode)
{
    switch (mode) {
        case GAMEMODE_MP:   return "Combat Simulator";
        case GAMEMODE_COOP: return "Co-op Campaign";
        case GAMEMODE_ANTI: return "Counter-Operative";
        default:            return "Unknown";
    }
}

static const char *clientStateStr(s32 state)
{
    switch (state) {
        case CLSTATE_DISCONNECTED: return "Disconnected";
        case CLSTATE_CONNECTING:   return "Connecting";
        case CLSTATE_AUTH:         return "Auth";
        case CLSTATE_LOBBY:        return "Lobby";
        case CLSTATE_GAME:         return "In Game";
        default:                   return "?";
    }
}

/* ========================================================================
 * Init / Event / Shutdown
 * ======================================================================== */

extern "C" s32 serverGuiInit(SDL_Window *window, void *glContext)
{
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        printf("SERVER GUI: GLAD failed to load\n");
        return -1;
    }

    sysLogPrintf(LOG_NOTE, "SERVER GUI: OpenGL %s", glGetString(GL_VERSION));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;

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

/* ========================================================================
 * Tab: Server (player list + controls)
 * ======================================================================== */

static void drawTabServer(float playerW, float controlW, float panelH)
{
    /* === Player List (left) === */
    if (ImGui::BeginChild("##players_panel", ImVec2(playerW, panelH), true)) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Players");
        ImGui::Separator();

        s32 playerCount = lobbyGetPlayerCount();

        if (playerCount == 0) {
            ImGui::TextDisabled("No players connected.");
        } else {
            ImGui::Columns(4, "##player_cols", true);
            ImGui::SetColumnWidth(0, playerW * 0.35f);
            ImGui::SetColumnWidth(1, playerW * 0.20f);
            ImGui::SetColumnWidth(2, playerW * 0.15f);
            ImGui::SetColumnWidth(3, playerW * 0.25f);

            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Agent Name");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "State");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Ping");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Actions");
            ImGui::NextColumn();
            ImGui::Separator();

            for (s32 i = 0; i < playerCount; i++) {
                struct lobbyplayer_view pv;
                memset(&pv, 0, sizeof(pv));
                if (!lobbyGetPlayerInfo(i, &pv)) continue;
                if (!pv.active) continue;

                ImGui::PushID(i);

                if (pv.isLeader) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s (L)", pv.name);
                } else {
                    ImGui::Text("%s", pv.name);
                }
                ImGui::NextColumn();

                ImVec4 stateCol = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                if (pv.state == CLSTATE_LOBBY) stateCol = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                else if (pv.state == CLSTATE_GAME) stateCol = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
                else if (pv.state <= CLSTATE_AUTH) stateCol = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                ImGui::TextColored(stateCol, "%s", clientStateStr(pv.state));
                ImGui::NextColumn();

                u32 ping = netGetClientPing(pv.isLocal ? 0 : i);
                if (ping > 0) {
                    ImVec4 pingCol = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
                    if (ping > 100) pingCol = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                    if (ping > 200) pingCol = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                    ImGui::TextColored(pingCol, "%u ms", ping);
                } else {
                    ImGui::TextDisabled("--");
                }
                ImGui::NextColumn();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.15f, 0.7f));
                char kickLabel[32];
                snprintf(kickLabel, sizeof(kickLabel), "Kick##%d", i);
                if (ImGui::SmallButton(kickLabel)) {
                    sysLogPrintf(LOG_NOTE, "SERVER GUI: kicking player %d (%s)", i, pv.name);
                    netServerKickClient(pv.isLocal ? 0 : i, "Kicked by server operator");
                }
                ImGui::PopStyleColor(2);
                ImGui::NextColumn();

                ImGui::PopID();
            }
            ImGui::Columns(1);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* === Server Controls (right) === */
    if (ImGui::BeginChild("##controls_panel", ImVec2(controlW, panelH), true)) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Match Control");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Game Mode:");
        const char *modes[] = { "Combat Simulator", "Co-op Campaign", "Counter-Operative" };
        s32 currentMode = (s32)g_NetGameMode;
        if (ImGui::Combo("##gamemode", &currentMode, modes, 3)) {
            g_NetGameMode = (u8)currentMode;
            sysLogPrintf(LOG_NOTE, "SERVER GUI: game mode changed to %s",
                         gameModeStr(g_NetGameMode));
        }

        ImGui::Spacing();
        ImGui::Spacing();

        bool hasPlayers = (g_NetNumClients > 0);
        if (!hasPlayers) ImGui::BeginDisabled();
        if (ImGui::Button("Force Start Match", ImVec2(-1, 30))) {
            sysLogPrintf(LOG_NOTE, "SERVER GUI: force starting match");
            netServerStageStart();
        }
        if (!hasPlayers) ImGui::EndDisabled();

        ImGui::Spacing();

        if (ImGui::Button("End Match", ImVec2(-1, 30))) {
            sysLogPrintf(LOG_NOTE, "SERVER GUI: ending match");
            netServerStageEnd();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Server");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.15f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.2f, 0.2f, 0.7f));
        if (ImGui::Button("Shutdown Server", ImVec2(-1, 30))) {
            sysLogPrintf(LOG_NOTE, "SERVER GUI: shutdown requested");
            SDL_Event quitEvent;
            quitEvent.type = SDL_QUIT;
            SDL_PushEvent(&quitEvent);
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::EndChild();
}

/* ========================================================================
 * Tab: Hub (hub state + room table)
 * ======================================================================== */

static void drawTabHub(float panelW, float panelH)
{
    if (ImGui::BeginChild("##hub_panel", ImVec2(panelW, panelH), true)) {
        /* Hub state header */
        hub_state_t hubState = hubGetState();
        ImVec4 stateColor = (hubState == HUB_STATE_ACTIVE)
            ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
            : ImVec4(0.6f, 0.8f, 1.0f, 1.0f);

        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Hub State:");
        ImGui::SameLine();
        ImGui::TextColored(stateColor, "%s", hubGetStateName(hubState));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Rooms");
        ImGui::Spacing();

        s32 roomCount = roomGetActiveCount();
        if (roomCount == 0) {
            ImGui::TextDisabled("No active rooms.");
        } else {
            float cw = panelW - ImGui::GetStyle().WindowPadding.x * 2.0f - 8.0f;
            ImGui::Columns(4, "##room_cols", true);
            ImGui::SetColumnWidth(0, cw * 0.08f);
            ImGui::SetColumnWidth(1, cw * 0.34f);
            ImGui::SetColumnWidth(2, cw * 0.30f);
            ImGui::SetColumnWidth(3, cw * 0.22f);

            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "ID");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Name");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "State");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Players");
            ImGui::NextColumn();
            ImGui::Separator();

            for (s32 i = 0; i < roomCount; i++) {
                hub_room_t *r = roomGetByIndex(i);
                if (!r) continue;

                ImGui::PushID(i);

                ImGui::Text("%u", (unsigned)r->id);
                ImGui::NextColumn();
                ImGui::Text("%s", r->name);
                ImGui::NextColumn();

                ImVec4 rStateColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                switch (r->state) {
                    case ROOM_STATE_LOBBY:    rStateColor = ImVec4(0.5f, 0.8f, 0.5f, 1.0f); break;
                    case ROOM_STATE_LOADING:  rStateColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
                    case ROOM_STATE_MATCH:    rStateColor = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); break;
                    case ROOM_STATE_POSTGAME: rStateColor = ImVec4(0.8f, 0.5f, 0.9f, 1.0f); break;
                    default: break;
                }
                ImGui::TextColored(rStateColor, "%s", roomStateName(r->state));
                ImGui::NextColumn();

                ImGui::Text("%u", (unsigned)r->client_count);
                ImGui::NextColumn();

                ImGui::PopID();
            }
            ImGui::Columns(1);
        }
    }
    ImGui::EndChild();
}

/* ========================================================================
 * Frame render
 * ======================================================================== */

extern "C" void serverGuiFrame(SDL_Window *window)
{
    if (!s_Initialized) return;

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);

    glViewport(0, 0, winW, winH);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    float margin    = 10.0f;
    float statusH   = 80.0f;
    float tabBarH   = 28.0f;
    float middleTop = margin + statusH + margin;
    float middleH   = ((float)winH - middleTop - margin) * 0.55f;
    float logTop    = middleTop + middleH + margin;
    float logH      = (float)winH - logTop - margin;
    float fullW     = (float)winW - margin * 2.0f;

    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove
                                | ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_NoCollapse;

    /* === Status Bar (top) === */
    ImGui::SetNextWindowPos(ImVec2(margin, margin));
    ImGui::SetNextWindowSize(ImVec2(fullW, statusH));
    if (ImGui::Begin("Server Status", nullptr, panelFlags)) {
        ImGui::Columns(4, nullptr, false);

        /* Column 1: Server identity + connect code */
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "PD2 DEDICATED SERVER");
        const char *ip = netUpnpIsActive() ? netUpnpGetExternalIP() : "";
        if (ip && ip[0]) {
            char connectCode[256];
            u32 ipAddr = 0;
            {
                u32 a, b, c, d;
                if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                    ipAddr = (a) | (b << 8) | (c << 16) | (d << 24);
                }
            }
            connectCodeEncode(ipAddr, connectCode, sizeof(connectCode));
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", connectCode);
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy Code")) {
                SDL_SetClipboardText(connectCode);
            }
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s:%u", ip, g_NetServerPort);
        } else {
            s32 upnpStatus = netUpnpGetStatus();
            if (upnpStatus == UPNP_STATUS_WORKING) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Port %u (UPnP...)", g_NetServerPort);
            } else {
                ImGui::Text("Port %u (LAN only)", g_NetServerPort);
            }
        }

        /* Column 2: Players */
        ImGui::NextColumn();
        s32 displayClients = g_NetNumClients > 0 ? g_NetNumClients - 1 : 0;
        ImGui::Text("Players: %d / %d", displayClients, g_NetMaxClients);

        /* Column 3: Game mode */
        ImGui::NextColumn();
        ImGui::Text("Mode: %s", gameModeStr(g_NetGameMode));

        /* Column 4: Online status */
        ImGui::NextColumn();
        if (g_NetMode == NETMODE_SERVER) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "ONLINE");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "OFFLINE");
        }

        ImGui::Columns(1);
    }
    ImGui::End();

    /* === Tabbed middle panel === */
    ImGui::SetNextWindowPos(ImVec2(margin, middleTop));
    ImGui::SetNextWindowSize(ImVec2(fullW, middleH));
    if (ImGui::Begin("##middle_tabs", nullptr,
                     panelFlags | ImGuiWindowFlags_NoTitleBar)) {

        if (ImGui::BeginTabBar("##srv_tabs")) {

            /* "Server" tab — player list + match controls */
            if (ImGui::BeginTabItem("Server")) {
                float innerH  = middleH - tabBarH - ImGui::GetStyle().WindowPadding.y * 2.0f;
                float playerW = fullW * 0.60f;
                float ctrlW   = fullW - playerW
                                - ImGui::GetStyle().WindowPadding.x * 2.0f
                                - ImGui::GetStyle().ItemSpacing.x;
                drawTabServer(playerW, ctrlW, innerH);
                ImGui::EndTabItem();
            }

            /* "Hub" tab — hub state + room list */
            if (ImGui::BeginTabItem("Hub")) {
                float innerH = middleH - tabBarH - ImGui::GetStyle().WindowPadding.y * 2.0f;
                float innerW = fullW - ImGui::GetStyle().WindowPadding.x * 2.0f;
                drawTabHub(innerW, innerH);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    /* === Log Panel (bottom) === */
    ImGui::SetNextWindowPos(ImVec2(margin, logTop));
    ImGui::SetNextWindowSize(ImVec2(fullW, logH));
    if (ImGui::Begin("Server Log", nullptr, panelFlags)) {
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
                } else if (strncmp(line, "LOBBY:", 6) == 0) {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "%s", line);
                } else if (strncmp(line, "HUB", 3) == 0) {
                    ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "%s", line);
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 0.9f), "%s", line);
                }
            }
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
