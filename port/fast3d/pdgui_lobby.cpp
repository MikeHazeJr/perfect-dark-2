/**
 * pdgui_lobby.cpp -- Network lobby player list sidebar overlay.
 *
 * Renders a small sidebar window showing connected players when in a
 * networked session. Displays each player's agent name, character thumbnail,
 * and connection state.
 *
 * This overlay renders independently of the hotswap system — it appears
 * whenever a network session is active (host or client) and the player
 * is in the lobby state (Combat Simulator setup).
 *
 * Called from pdguiRender() in the ImGui overlay phase.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "system.h"

/* ========================================================================
 * Forward declarations for game symbols
 * ======================================================================== */

extern "C" {

/* Network state */
#define NETMODE_NONE   0
#define NETMODE_SERVER 1
#define NETMODE_CLIENT 2

#define CLSTATE_DISCONNECTED 0
#define CLSTATE_CONNECTING   1
#define CLSTATE_AUTH         2
#define CLSTATE_LOBBY        3
#define CLSTATE_GAME         4

#define NET_MAX_CLIENTS 8
#define NET_MAX_NAME    16

/* We access the netclient array opaquely via bridge functions */
s32 netGetMode(void);
s32 netGetNumClients(void);
s32 netGetMaxClients(void);
u32 netGetServerPort(void);
const char *netGetPublicIP(void);

/* Bridge functions for reading client info from C (net.c) */
s32 netLobbyGetClientCount(void);
s32 netLobbyGetClientState(s32 idx);
const char *netLobbyGetClientName(s32 idx);
u8 netLobbyGetClientHead(s32 idx);
u8 netLobbyGetClientBody(s32 idx);
u8 netLobbyGetClientTeam(s32 idx);
s32 netLobbyIsLocalClient(s32 idx);

/* Dedicated server flag */
extern s32 g_NetDedicated;

/* Window title */
void videoSetWindowTitle(const char *title);

/* Log ring buffer for on-screen display */
s32 sysLogRingGetCount(void);
const char *sysLogRingGetLine(s32 idx);

/* Lobby screen (from pdgui_menu_lobby.cpp) */
void pdguiLobbyScreenRender(s32 winW, s32 winH);

/* Check if local client is in lobby state */
s32 netLocalClientInLobby(void);

/* Lobby state management */
void lobbyUpdate(void);

/* Lobby player data (from netlobby.h, simplified for C++) */
struct lobbyplayer_view {
    u8 active;
    u8 isLeader;
    u8 isReady;
    u8 headnum;
    u8 bodynum;
    u8 team;
    char name[16];
    s32 isLocal;
    s32 state; /* CLSTATE_* */
};

s32 lobbyGetPlayerCount(void);
s32 lobbyGetPlayerInfo(s32 idx, struct lobbyplayer_view *out);
s32 lobbyIsLocalLeader(void);

/* Video info */
s32 viGetWidth(void);
s32 viGetHeight(void);

} /* extern "C" */

/* ========================================================================
 * Public API
 * ======================================================================== */

extern "C" {

/**
 * Render the dedicated server info overlay (top-left).
 * Shows server status, IP, port, and connected player count.
 */
static void renderDedicatedServerOverlay(s32 winW, s32 winH, s32 clientCount)
{
    float scale = (float)winH / 480.0f;

    /* ---- Server info panel (top-left) ---- */
    float infoW = 260.0f * scale;
    float infoH = 80.0f * scale;

    ImGui::SetNextWindowPos(ImVec2(10.0f * scale, 10.0f * scale));
    ImGui::SetNextWindowSize(ImVec2(infoW, infoH));
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin("##dedicated_server_info", nullptr, flags)) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "DEDICATED SERVER");
        ImGui::Separator();

        u32 port = netGetServerPort();
        const char *publicIP = netGetPublicIP();

        if (publicIP && publicIP[0]) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s:%u", publicIP, port);
        } else {
            ImGui::Text("Port: %u (UPnP inactive)", port);
        }

        ImGui::Text("Players: %d / %d", clientCount, netGetMaxClients());
    }
    ImGui::End();

    /* ---- Live log window (bottom-left) ---- */
    float logW = (float)winW * 0.5f;
    float logH = (float)winH * 0.35f;
    float logX = 10.0f * scale;
    float logY = (float)winH - logH - 10.0f * scale;

    ImGui::SetNextWindowPos(ImVec2(logX, logY));
    ImGui::SetNextWindowSize(ImVec2(logW, logH));
    ImGui::SetNextWindowBgAlpha(0.75f);

    if (ImGui::Begin("##server_log", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Server Log");
        ImGui::Separator();

        if (ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false)) {
            s32 lineCount = sysLogRingGetCount();
            for (s32 i = 0; i < lineCount; i++) {
                const char *line = sysLogRingGetLine(i);
                /* Color-code by prefix */
                if (strncmp(line, "ERROR:", 6) == 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", line);
                } else if (strncmp(line, "WARNING:", 8) == 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", line);
                } else if (strncmp(line, "CHAT:", 5) == 0) {
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", line);
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 0.9f), "%s", line);
                }
            }
            /* Auto-scroll to bottom */
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

/**
 * Render the lobby player list sidebar.
 * Called from pdguiRender() every frame. Only draws if a network session
 * is active. Safe to call when not networked (will no-op).
 */
void pdguiLobbyRender(s32 winW, s32 winH)
{
    s32 mode = netGetMode();

    /* Update window title at most once per second */
    {
        static u32 s_TitleFrameCounter = 0;
        if (++s_TitleFrameCounter >= 60) {
            s_TitleFrameCounter = 0;
            char titleBuf[256];

            if (g_NetDedicated && mode == NETMODE_SERVER) {
                const char *ip = netGetPublicIP();
                u32 port = netGetServerPort();
                s32 count = netLobbyGetClientCount();
                s32 max = netGetMaxClients();
                if (ip && ip[0]) {
                    snprintf(titleBuf, sizeof(titleBuf),
                             "PD2 Dedicated Server - %s:%u - %d/%d connected",
                             ip, port, count, max);
                } else {
                    snprintf(titleBuf, sizeof(titleBuf),
                             "PD2 Dedicated Server - port %u - %d/%d connected",
                             port, count, max);
                }
            } else if (mode != NETMODE_NONE) {
                snprintf(titleBuf, sizeof(titleBuf), "Perfect Dark 2.0 (v0.0.2)");
            } else {
                snprintf(titleBuf, sizeof(titleBuf), "Perfect Dark 2.0 (v0.0.2)");
            }

            videoSetWindowTitle(titleBuf);
        }
    }

    if (mode == NETMODE_NONE) {
        return;  /* Not in a network session */
    }

    s32 clientCount = netLobbyGetClientCount();
    if (clientCount <= 0 && !g_NetDedicated) {
        return;
    }

    /* Dedicated server: show server info overlay in top-left */
    if (g_NetDedicated && mode == NETMODE_SERVER) {
        renderDedicatedServerOverlay(winW, winH, clientCount);
    }

    /* Unified lobby screen — shown when local client is in lobby state.
     * This is the main multiplayer menu, not tied to any specific PD dialog. */
    if (netLocalClientInLobby() || (g_NetDedicated && mode == NETMODE_SERVER)) {
        pdguiLobbyScreenRender(winW, winH);
    }

    /* ---- Layout: small sidebar on the right ---- */
    float scale = (float)winH / 480.0f;
    float sideW = 200.0f * scale;
    float extraH = (mode == NETMODE_SERVER) ? 36.0f : 0.0f;
    float sideH = (40.0f * clientCount + 34.0f + extraH) * scale;
    float sideX = (float)winW - sideW - 10.0f * scale;
    float sideY = 10.0f * scale;

    /* Clamp height */
    if (sideH > (float)winH * 0.5f) sideH = (float)winH * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(sideX, sideY));
    ImGui::SetNextWindowSize(ImVec2(sideW, sideH));
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoTitleBar;

    if (!ImGui::Begin("##lobby_players", nullptr, flags)) {
        ImGui::End();
        return;
    }

    /* Header */
    const char *header = (mode == NETMODE_SERVER) ? "Lobby (Host)" : "Lobby";
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", header);

    /* Show server connection info for the host */
    if (mode == NETMODE_SERVER) {
        u32 port = netGetServerPort();
        const char *publicIP = netGetPublicIP();
        if (publicIP && publicIP[0]) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s:%u", publicIP, port);
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 0.9f), "Port: %u", port);
        }
    }

    ImGui::Separator();

    /* Player entries — using lobby state system */
    ImDrawList *dl = ImGui::GetWindowDrawList();

    s32 playerCount = lobbyGetPlayerCount();
    for (s32 i = 0; i < playerCount; i++) {
        struct lobbyplayer_view pv;
        memset(&pv, 0, sizeof(pv));
        if (!lobbyGetPlayerInfo(i, &pv)) continue;
        if (!pv.active) continue;

        /* State indicator color */
        ImU32 stateColor;
        const char *stateText;
        switch (pv.state) {
            case CLSTATE_CONNECTING:
                stateColor = IM_COL32(255, 200, 0, 255);
                stateText = "connecting";
                break;
            case CLSTATE_AUTH:
                stateColor = IM_COL32(255, 200, 0, 255);
                stateText = "auth";
                break;
            case CLSTATE_LOBBY:
                stateColor = IM_COL32(100, 255, 100, 255);
                stateText = "lobby";
                break;
            case CLSTATE_GAME:
                stateColor = IM_COL32(100, 200, 255, 255);
                stateText = "in-game";
                break;
            default:
                stateColor = IM_COL32(128, 128, 128, 255);
                stateText = "?";
                break;
        }

        /* Player row */
        ImVec2 cursor = ImGui::GetCursorScreenPos();

        /* Small colored dot for state */
        float dotR = 4.0f * scale;
        dl->AddCircleFilled(
            ImVec2(cursor.x + dotR + 2.0f, cursor.y + 10.0f * scale),
            dotR, stateColor, 12);

        /* Name with leader/local indicators */
        char displayName[48];
        const char *suffix = "";
        if (pv.isLocal && pv.isLeader) {
            suffix = " (you, leader)";
        } else if (pv.isLocal) {
            suffix = " (you)";
        } else if (pv.isLeader) {
            suffix = " (leader)";
        }
        snprintf(displayName, sizeof(displayName), "%s%s", pv.name, suffix);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotR * 2 + 8.0f);

        /* Leader name in gold, others in white */
        if (pv.isLeader) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", displayName);
        } else {
            ImGui::Text("%s", displayName);
        }

        /* State text */
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), " [%s]", stateText);
    }

    ImGui::End();
}

} /* extern "C" */
