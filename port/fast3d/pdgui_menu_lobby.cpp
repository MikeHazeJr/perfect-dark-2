/**
 * pdgui_menu_lobby.cpp -- Social Lobby screen.
 *
 * First screen shown after a client successfully connects to a server
 * (CLSTATE_LOBBY, !s_InRoom). The player sees all connected players and
 * all active rooms. From here they can create a new room or join one.
 *
 * NOT the room interior — that is pdgui_menu_room.cpp (shown when s_InRoom=true).
 * NOT shown during gameplay.
 *
 * Layout:
 *   Left panel  — Connected Players (agent name, character, status)
 *   Right panel — Active Rooms (name, state badge, player count, Join button)
 *                 "Create Room" button at top of room panel
 *   Footer      — Server chat stub (UI frame only) + Disconnect button
 *
 * On "Create Room": sets s_InRoom=true via pdguiSetInRoom(1), showing
 * pdgui_menu_room.cpp until the player presses "Leave Room".
 * "Join" buttons are disabled until R-3 (SVC_ROOM_JOIN protocol).
 *
 * Architecture: Dedicated-server-only model.
 * Called from pdguiLobbyRender() in pdgui_lobby.cpp.
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
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "system.h"
#include "menumgr.h"
#include "hub.h"
#include "room.h"

extern "C" {

/* Network state */
s32 netGetMode(void);
s32 netGetMaxClients(void);
u32 netGetServerPort(void);
const char *netGetPublicIP(void);

/* Connect codes (connectcode.c) */
s32 connectCodeEncode(u32 ip, char *buf, s32 bufsize);
s32 connectCodeDecode(const char *code, u32 *outIp);
#define CONNECT_DEFAULT_PORT 27100
extern s32 g_NetDedicated;
s32 netDisconnect(void);

#define NETMODE_NONE   0
#define NETMODE_SERVER 1
#define NETMODE_CLIENT 2

#define CLSTATE_DISCONNECTED 0
#define CLSTATE_CONNECTING   1
#define CLSTATE_AUTH         2
#define CLSTATE_LOBBY        3
#define CLSTATE_GAME         4

/* Lobby state */
void lobbyUpdate(void);
s32 lobbyGetPlayerCount(void);

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
s32 lobbyGetPlayerInfo(s32 idx, struct lobbyplayer_view *out);

/* Character accessor */
char *mpGetBodyName(u8 mpbodynum);
u32 mpGetNumBodies(void);

/* Check if local client is in lobby state */
s32 netLocalClientInLobby(void);

/* Agent name */
const char *mpPlayerConfigGetName(s32 playernum);

/* Routing: transition into room interior (pdgui_lobby.cpp) */
void pdguiSetInRoom(s32 inRoom);

} /* extern "C" */

/* ========================================================================
 * Render — Social Lobby
 * Called when client is in CLSTATE_LOBBY and has not yet entered a room.
 * ======================================================================== */

extern "C" void pdguiLobbyScreenRender(s32 winW, s32 winH)
{
    lobbyUpdate();

    float scale = pdguiScaleFactor();
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;

    float pdTitleH = pdguiScale(26.0f);

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##social_lobby", nullptr, wflags)) {
        ImGui::End();
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    /* Opaque backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255));
    }

    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, "Social Lobby", 1);

    /* Title bar text */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        const char *title = "Social Lobby";
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dialogX + (dialogW - titleSize.x) * 0.5f,
                           dialogY + (pdTitleH - titleSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), title);
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    /* Connection status + player count */
    {
        s32 numPlayers = lobbyGetPlayerCount();
        s32 maxPlayers = netGetMaxClients();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                           "Connected to dedicated server");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f),
                           "  Players: %d / %d", numPlayers, maxPlayers);
    }

    /* Connect code (server host view only) */
    if (netGetMode() == NETMODE_SERVER) {
        static char s_ConnectCode[128] = "";
        static bool s_CodeGenerated = false;
        if (!s_CodeGenerated) {
            const char *ip = netGetPublicIP();
            u32 ipAddr = 0;
            if (ip) {
                u32 a=0,b=0,c=0,d=0;
                if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                    ipAddr = (a << 24) | (b << 16) | (c << 8) | d;
                }
            }
            if (ipAddr) {
                connectCodeEncode(ipAddr, s_ConnectCode, sizeof(s_ConnectCode));
                s_CodeGenerated = true;
                sysLogPrintf(LOG_NOTE, "LOBBY: connect code: %s", s_ConnectCode);
            }
        }
        if (s_ConnectCode[0]) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Code: %s", s_ConnectCode);
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                SDL_SetClipboardText(s_ConnectCode);
                sysLogPrintf(LOG_NOTE, "LOBBY: connect code copied to clipboard");
            }
        }
    }

    ImGui::Separator();

    /* Two-column layout */
    float pad = 8.0f * scale;
    float colW = (dialogW - pad * 3.0f) * 0.5f;
    float contentH = dialogH - pdTitleH - 90.0f * scale;

    /* ================================================================
     * Left column — Connected Players
     * ================================================================ */
    ImGui::BeginChild("##social_players", ImVec2(colW, contentH), true);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Connected Players");
    ImGui::Separator();

    s32 playerCount = lobbyGetPlayerCount();
    for (s32 i = 0; i < playerCount; i++) {
        struct lobbyplayer_view pv;
        memset(&pv, 0, sizeof(pv));
        if (!lobbyGetPlayerInfo(i, &pv)) continue;

        ImGui::PushID(i);

        /* Agent name (you = green, others = white) */
        char label[64];
        snprintf(label, sizeof(label), "%s%s", pv.name, pv.isLocal ? " (you)" : "");

        if (pv.isLocal) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", label);
        } else {
            ImGui::Text("%s", label);
        }

        /* Character name */
        if (pv.bodynum < (u8)mpGetNumBodies()) {
            const char *bodyName = mpGetBodyName(pv.bodynum);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), " [%s]",
                               bodyName ? bodyName : "?");
        }

        /* Status string */
        const char *stateStr = "";
        ImVec4 stateColor = ImVec4(0.5f, 0.5f, 0.5f, 0.6f);
        switch (pv.state) {
            case CLSTATE_CONNECTING:
                stateStr = "connecting...";
                stateColor = ImVec4(1.0f, 0.8f, 0.2f, 0.8f);
                break;
            case CLSTATE_AUTH:
                stateStr = "authenticating...";
                stateColor = ImVec4(1.0f, 0.8f, 0.2f, 0.8f);
                break;
            case CLSTATE_LOBBY:
                stateStr = "In Lobby";
                stateColor = ImVec4(0.3f, 1.0f, 0.3f, 0.8f);
                break;
            case CLSTATE_GAME:
                stateStr = "In Match";
                stateColor = ImVec4(0.3f, 0.7f, 1.0f, 0.8f);
                break;
        }
        if (stateStr[0]) {
            ImGui::SameLine();
            ImGui::TextColored(stateColor, "  %s", stateStr);
        }

        ImGui::PopID();
    }

    if (playerCount == 0) {
        ImGui::TextDisabled("Waiting for players...");
    }

    ImGui::EndChild();
    ImGui::SameLine(0, pad);

    /* ================================================================
     * Right column — Active Rooms
     * ================================================================ */
    ImGui::BeginChild("##social_rooms", ImVec2(colW, contentH), true);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Active Rooms");
    ImGui::Separator();

    float innerW = colW - ImGui::GetStyle().WindowPadding.x * 2 - 4.0f;
    float btnH   = pdguiScale(28.0f);

    /* Create Room — only shown to game clients, not the server operator */
    if (!g_NetDedicated) {
        ImGui::Spacing();
        if (ImGui::Button("+ Create Room", ImVec2(innerW, btnH))) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            sysLogPrintf(LOG_NOTE, "LOBBY: transitioning to room interior");
            pdguiSetInRoom(1);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    /* Room list */
    s32 roomsShown = 0;
    for (s32 ri = 0; ri < HUB_MAX_ROOMS; ri++) {
        hub_room_t *room = roomGetByIndex(ri);
        if (!room) continue;
        if (room->client_count == 0) continue;
        roomsShown++;

        ImGui::PushID(ri);

        /* State color */
        const char *stateName = roomStateName(room->state);
        ImVec4 stateColor;
        switch (room->state) {
            case ROOM_STATE_LOBBY:    stateColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
            case ROOM_STATE_LOADING:  stateColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
            case ROOM_STATE_MATCH:    stateColor = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); break;
            case ROOM_STATE_POSTGAME: stateColor = ImVec4(0.8f, 0.5f, 1.0f, 1.0f); break;
            default:                  stateColor = ImVec4(0.4f, 0.4f, 0.4f, 0.6f); break;
        }

        /* Room name row */
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "%s",
                           room->name[0] ? room->name : "Unnamed Room");
        ImGui::SameLine();
        ImGui::TextColored(stateColor, "[%s]", stateName);

        /* Player count + greyed-out Join button on same row */
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f),
                           "  %d player%s",
                           room->client_count,
                           room->client_count == 1 ? "" : "s");

        if (!g_NetDedicated) {
            float joinW = pdguiScale(56.0f);
            char joinId[32];
            snprintf(joinId, sizeof(joinId), "Join##r%d", ri);
            ImGui::SameLine(innerW - joinW);
            ImGui::BeginDisabled();
            ImGui::Button(joinId, ImVec2(joinW, 0));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Coming soon (R-3)");
            }
        }

        ImGui::Spacing();
        ImGui::PopID();
    }

    if (roomsShown == 0) {
        ImGui::TextDisabled("No active rooms.");
        if (!g_NetDedicated) {
            ImGui::Spacing();
            ImGui::TextDisabled("Create a room to get started.");
        }
    }

    ImGui::EndChild();

    /* ================================================================
     * Footer — server chat stub + Disconnect
     * ================================================================ */
    ImGui::Separator();

    /* Server chat stub (UI frame — protocol in a future session) */
    ImGui::TextColored(ImVec4(0.3f, 0.4f, 0.5f, 0.6f), "Server Chat  (coming soon)");
    ImGui::Spacing();

    /* Disconnect */
    float discBtnW = 120.0f * scale;
    ImGui::SetCursorPosX((dialogW - discBtnW) * 0.5f);
    if (ImGui::Button("Disconnect", ImVec2(discBtnW, 26.0f * scale)) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        netDisconnect();
    }

    ImGui::End();
}
