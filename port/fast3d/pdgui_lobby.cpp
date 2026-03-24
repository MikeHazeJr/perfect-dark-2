/**
 * pdgui_lobby.cpp -- Network lobby overlay controller.
 *
 * Manages two distinct rendering modes:
 *
 * 1. LOBBY STATE (CLSTATE_LOBBY): Renders the full lobby screen via
 *    pdguiLobbyScreenRender() from pdgui_menu_lobby.cpp. The sidebar
 *    does NOT render — the lobby screen already shows the player list.
 *
 * 2. IN-GAME STATE (CLSTATE_GAME): Renders a minimal sidebar overlay
 *    showing "Connected: X players" with a compact player list. This is
 *    the only time the sidebar is visible for game clients.
 *
 * 3. DEDICATED SERVER (g_NetDedicated): Always shows the server info
 *    overlay (IP, port, player count, log). Only the dedicated server
 *    process should ever be in NETMODE_SERVER — game clients never are.
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
#include "connectcode.h"

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

/* D3R-9: Distribution overlay and kill feed (pdgui_lobby_distrib.cpp) */
void pdguiDistribOverlayRender(s32 winW, s32 winH);
void pdguiKillFeedRender(s32 winW, s32 winH);

/* Lobby player data (from netlobby.h, simplified for C++) */
struct lobbyplayer_view {
    u8 active;
    u8 isLeader;
    u8 isReady;
    u8 headnum;
    u8 bodynum;
    u8 team;
    char name[32];  /* matches LOBBY_NAME_LEN */
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
            /* Build connect code from public IP + port */
            char connectCode[256];
            u32 ipAddr = 0;
            {
                u32 a, b, c, d;
                if (sscanf(publicIP, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                    ipAddr = (a) | (b << 8) | (c << 16) | (d << 24);
                }
            }
            connectCodeEncode(ipAddr, (u16)port, connectCode, sizeof(connectCode));
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", connectCode);
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                SDL_SetClipboardText(connectCode);
            }
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s:%u", publicIP, port);
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
 * Render the in-game sidebar — minimal player list overlay.
 * Only shown during CLSTATE_GAME when the full lobby screen is NOT visible.
 */
static void renderInGameSidebar(s32 winW, s32 winH)
{
    float scale = (float)winH / 480.0f;

    s32 playerCount = lobbyGetPlayerCount();
    if (playerCount <= 0) return;

    float sideW = 200.0f * scale;
    float sideH = (28.0f * playerCount + 30.0f) * scale;
    float sideX = (float)winW - sideW - 10.0f * scale;
    float sideY = 10.0f * scale;

    /* Clamp height */
    if (sideH > (float)winH * 0.4f) sideH = (float)winH * 0.4f;

    ImGui::SetNextWindowPos(ImVec2(sideX, sideY));
    ImGui::SetNextWindowSize(ImVec2(sideW, sideH));
    ImGui::SetNextWindowBgAlpha(0.65f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoTitleBar;

    if (!ImGui::Begin("##ingame_players", nullptr, flags)) {
        ImGui::End();
        return;
    }

    /* Compact header */
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Connected: %d", playerCount);
    ImGui::Separator();

    /* Compact player entries */
    ImDrawList *dl = ImGui::GetWindowDrawList();

    for (s32 i = 0; i < playerCount; i++) {
        struct lobbyplayer_view pv;
        memset(&pv, 0, sizeof(pv));
        if (!lobbyGetPlayerInfo(i, &pv)) continue;
        if (!pv.active) continue;

        /* State indicator dot */
        ImU32 stateColor;
        switch (pv.state) {
            case CLSTATE_CONNECTING:
            case CLSTATE_AUTH:
                stateColor = IM_COL32(255, 200, 0, 255);
                break;
            case CLSTATE_LOBBY:
                stateColor = IM_COL32(100, 255, 100, 255);
                break;
            case CLSTATE_GAME:
                stateColor = IM_COL32(100, 200, 255, 255);
                break;
            default:
                stateColor = IM_COL32(128, 128, 128, 255);
                break;
        }

        ImVec2 cursor = ImGui::GetCursorScreenPos();
        float dotR = 3.0f * scale;
        dl->AddCircleFilled(
            ImVec2(cursor.x + dotR + 2.0f, cursor.y + 8.0f * scale),
            dotR, stateColor, 12);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotR * 2 + 6.0f);

        /* Name — gold for leader, green for local, white for others */
        if (pv.isLeader) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", pv.name);
        } else if (pv.isLocal) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", pv.name);
        } else {
            ImGui::Text("%s", pv.name);
        }
    }

    ImGui::End();
}

/**
 * Render the network lobby overlay.
 * Called from pdguiRender() every frame. Only draws if a network session
 * is active. Safe to call when not networked (will no-op).
 *
 * Rendering logic:
 * - Dedicated server process: server info overlay + lobby screen (always)
 * - Client in CLSTATE_LOBBY: full lobby screen only (no sidebar)
 * - Client in CLSTATE_GAME: minimal in-game sidebar only
 * - Client in other states: nothing (connecting/auth handled by join dialog)
 */
void pdguiLobbyRender(s32 winW, s32 winH)
{
    s32 mode = netGetMode();

    if (mode == NETMODE_NONE) {
        return;  /* Not in a network session */
    }

    s32 clientCount = netLobbyGetClientCount();

    /* === Dedicated server process === */
    if (g_NetDedicated && mode == NETMODE_SERVER) {
        renderDedicatedServerOverlay(winW, winH, clientCount);
        pdguiLobbyScreenRender(winW, winH);
        return;
    }

    /* === Game client === */
    if (mode == NETMODE_CLIENT) {
        if (netLocalClientInLobby()) {
            /* In lobby: the full lobby screen handles everything.
             * No sidebar — pdguiLobbyScreenRender shows the player list. */
            pdguiLobbyScreenRender(winW, winH);
            /* D3R-9: download progress overlay on top of lobby screen */
            pdguiDistribOverlayRender(winW, winH);
        } else if (clientCount > 0) {
            /* In game (or transitioning): show minimal sidebar overlay */
            renderInGameSidebar(winW, winH);
            /* D3R-9: kill feed for spectating clients */
            pdguiKillFeedRender(winW, winH);
        }
        return;
    }

    /* NETMODE_SERVER without g_NetDedicated = debug local server.
     * Show a minimal sidebar so the developer knows it's active. */
    if (mode == NETMODE_SERVER && clientCount > 0) {
        renderInGameSidebar(winW, winH);
    }
}

} /* extern "C" */
