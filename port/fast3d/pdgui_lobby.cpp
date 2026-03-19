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
    float overlayW = 240.0f * scale;
    float overlayH = 80.0f * scale;

    ImGui::SetNextWindowPos(ImVec2(10.0f * scale, 10.0f * scale));
    ImGui::SetNextWindowSize(ImVec2(overlayW, overlayH));
    ImGui::SetNextWindowBgAlpha(0.8f);

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
            ImGui::Text("IP: %s:%u", publicIP, port);
        } else {
            ImGui::Text("Port: %u", port);
        }

        ImGui::Text("Players: %d / %d", clientCount, netGetMaxClients());
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

    /* Player entries */
    ImDrawList *dl = ImGui::GetWindowDrawList();

    for (s32 i = 0; i < clientCount; i++) {
        s32 state = netLobbyGetClientState(i);
        if (state == CLSTATE_DISCONNECTED) continue;

        const char *name = netLobbyGetClientName(i);
        u8 team = netLobbyGetClientTeam(i);
        bool isLocal = netLobbyIsLocalClient(i) != 0;

        /* State indicator color */
        ImU32 stateColor;
        const char *stateText;
        switch (state) {
            case CLSTATE_CONNECTING:
                stateColor = IM_COL32(255, 200, 0, 255);
                stateText = "...";
                break;
            case CLSTATE_AUTH:
                stateColor = IM_COL32(255, 200, 0, 255);
                stateText = "auth";
                break;
            case CLSTATE_LOBBY:
                stateColor = IM_COL32(100, 255, 100, 255);
                stateText = "ready";
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

        /* Small colored dot for team/state */
        float dotR = 4.0f * scale;
        dl->AddCircleFilled(
            ImVec2(cursor.x + dotR + 2.0f, cursor.y + 10.0f * scale),
            dotR, stateColor, 12);

        /* Name */
        char displayName[32];
        if (isLocal) {
            snprintf(displayName, sizeof(displayName), "%s (you)", name);
        } else {
            snprintf(displayName, sizeof(displayName), "%s", name);
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotR * 2 + 8.0f);
        ImGui::Text("%s", displayName);

        /* State text (smaller, dimmed) */
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), " [%s]", stateText);
    }

    ImGui::End();
}

} /* extern "C" */
