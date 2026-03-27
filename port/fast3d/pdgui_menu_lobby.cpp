/**
 * pdgui_menu_lobby.cpp -- Unified network lobby screen.
 *
 * Renders as a persistent overlay when in a network session (CLSTATE_LOBBY).
 * NOT tied to any specific PD dialog — works for Combat Sim, Co-op, Counter-Op.
 *
 * Architecture: Dedicated-server-only model.
 * All players are clients connected to a dedicated server. The first player
 * to join becomes the lobby leader and can choose the game mode.
 * Players are identified by their Agent name (from their save profile).
 *
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
s32 lobbyIsLocalLeader(void);

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

/* Game mode constants */
#define GAMEMODE_MP   0
#define GAMEMODE_COOP 1
#define GAMEMODE_ANTI 2

/* Menu stack (for co-op config dialog) */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

extern struct menudialogdef g_NetCoopHostMenuDialog;

/* Bridge functions: send CLC_LOBBY_START to the dedicated server */
s32 netLobbyRequestStart(u8 gamemode, u8 stagenum, u8 difficulty);
s32 netLobbyRequestStartWithSims(u8 gamemode, u8 stagenum, u8 difficulty, u8 numSims, u8 simType);

/* Default MP stage (Complex) for quick-start Combat Sim */
#define STAGE_MP_COMPLEX 0x1f

/* Arena stage list (subset of MP stages for quick selection) */
struct arena_entry { const char *name; u8 stagenum; };
static const arena_entry s_Arenas[] = {
    { "Complex",   0x1f },
    { "Grid",      0x20 },
    { "Sewers",    0x21 },
    { "Basement",  0x22 },
    { "Ravine",    0x23 },
    { "Warehouse", 0x24 },
    { "Villa",     0x25 },
    { "Temple",    0x26 },
    { "Caves",     0x27 },
    { "Base",      0x28 },
    { "G5",        0x29 },
    { "Citadel",   0x2a },
    { "Felicity",  0x2b },
    { "Ruins",     0x2c },
    { "Defection", 0x2d },
    { "Island",    0x2e },
    { "Fortress",  0x2f },
    { "Ruins",     0x30 },
    { "Stadium",   0x31 },
    { "Pipes",     0x32 },
};

/* Character accessor */
char *mpGetBodyName(u8 mpbodynum);
u32 mpGetNumBodies(void);

/* Check if local client is in lobby state */
s32 netLocalClientInLobby(void);

/* Agent name */
const char *mpPlayerConfigGetName(s32 playernum);

} /* extern "C" */

/* ========================================================================
 * Lobby persistent state (Combat Sim settings)
 * ======================================================================== */

static int s_SelectedArena = 0;      /* index into s_Arenas */
static int s_NumSims       = 1;      /* 0..MAX_BOTS simulants */
static int s_SimType       = 2;      /* 0=Meat 1=Easy 2=Normal 3=Hard 4=Perfect 5=Dark */

static const char *s_SimTypeNames[] = {
    "Meat", "Easy", "Normal", "Hard", "Perfect", "Dark"
};
static const int s_NumSimTypes = 6;
static const int s_NumArenas   = (int)(sizeof(s_Arenas) / sizeof(s_Arenas[0]));

/* ========================================================================
 * Render — called from pdguiLobbyRender when in network lobby state
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

    if (!ImGui::Begin("##network_lobby", nullptr, wflags)) {
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

    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, "Lobby", 1);

    /* Title */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        const char *title = "Lobby";
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dialogX + (dialogW - titleSize.x) * 0.5f,
                           dialogY + (pdTitleH - titleSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), title);
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    /* Connection status + phonetic code */
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Connected to dedicated server");

    /* Display connect code for server hosts */
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
    float leftW = dialogW * 0.50f;
    float rightW = dialogW * 0.40f;
    float contentH = dialogH - pdTitleH - 100.0f * scale;

    /* ---- Left column: Player list with Agent names ---- */
    ImGui::BeginChild("##lobby_players_col", ImVec2(leftW, contentH), true);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Players");
    ImGui::Separator();

    s32 playerCount = lobbyGetPlayerCount();
    for (s32 i = 0; i < playerCount; i++) {
        struct lobbyplayer_view pv;
        memset(&pv, 0, sizeof(pv));
        if (!lobbyGetPlayerInfo(i, &pv)) continue;

        ImGui::PushID(i);

        /* Agent name with role indicators */
        char label[64];
        const char *suffix = "";
        if (pv.isLocal && pv.isLeader) suffix = " (you, leader)";
        else if (pv.isLocal) suffix = " (you)";
        else if (pv.isLeader) suffix = " (leader)";
        snprintf(label, sizeof(label), "%s%s", pv.name, suffix);

        /* Color code: leader=gold, local=green, others=white */
        if (pv.isLeader) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", label);
        } else if (pv.isLocal) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", label);
        } else {
            ImGui::Text("%s", label);
        }

        /* Character name in muted text */
        if (pv.bodynum < (u8)mpGetNumBodies()) {
            const char *bodyName = mpGetBodyName(pv.bodynum);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), " [%s]",
                               bodyName ? bodyName : "?");
        }

        /* Connection state indicator */
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
                stateStr = "ready";
                stateColor = ImVec4(0.3f, 1.0f, 0.3f, 0.8f);
                break;
            case CLSTATE_GAME:
                stateStr = "in game";
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

    /* ---- Right column: Rooms + Game mode selection ---- */
    ImGui::BeginChild("##lobby_settings_col", ImVec2(rightW, contentH), true);

    /* Hub state + room list */
    {
        hub_state_t hubState = hubGetState();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Server: %s", hubGetStateName(hubState));
        ImGui::Separator();

        s32 roomCount = roomGetActiveCount();
        s32 roomsShown = 0;
        for (s32 ri = 0; ri < HUB_MAX_ROOMS; ri++) {
            hub_room_t *room = roomGetByIndex(ri);
            if (!room) continue;
            if (room->client_count == 0) continue; /* skip permanent empty placeholder rooms */
            roomsShown++;

            const char *stateName = roomStateName(room->state);
            ImVec4 stateColor;
            switch (room->state) {
                case ROOM_STATE_LOBBY:    stateColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
                case ROOM_STATE_LOADING:  stateColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
                case ROOM_STATE_MATCH:    stateColor = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); break;
                case ROOM_STATE_POSTGAME: stateColor = ImVec4(0.8f, 0.5f, 1.0f, 1.0f); break;
                default:                  stateColor = ImVec4(0.4f, 0.4f, 0.4f, 0.6f); break;
            }

            ImGui::TextColored(stateColor, "Room %d: %s (%s) [%d players]",
                room->id, room->name[0] ? room->name : "Untitled",
                stateName, room->client_count);
        }

        if (roomsShown == 0) {
            ImGui::TextDisabled("No active rooms");
        }

        ImGui::Spacing();
        ImGui::Separator();
    }

    /* Game mode selection (leader controls) */
    bool isLeader = lobbyIsLocalLeader() != 0;

    if (isLeader) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Game Setup");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 0.9f), "Game Setup");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.7f), "(Leader controls)");
    }
    ImGui::Separator();

    if (!isLeader) ImGui::BeginDisabled();

    ImGui::Spacing();
    ImGui::Text("Choose a game mode:");
    ImGui::Spacing();

    float btnW = rightW - ImGui::GetStyle().WindowPadding.x * 2;
    float btnH = 32.0f * scale;

    /* ---- Combat Simulator settings (shown before the button) ---- */
    ImGui::Text("Arena:");
    ImGui::SetNextItemWidth(btnW);
    if (ImGui::BeginCombo("##arena", s_Arenas[s_SelectedArena].name)) {
        for (int ai = 0; ai < s_NumArenas; ai++) {
            bool sel = (ai == s_SelectedArena);
            if (ImGui::Selectable(s_Arenas[ai].name, sel)) {
                s_SelectedArena = ai;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Text("Simulants:");
    ImGui::SetNextItemWidth(btnW);
    ImGui::SliderInt("##numsims", &s_NumSims, 0, 8);

    ImGui::Spacing();
    ImGui::Text("Difficulty:");
    ImGui::SetNextItemWidth(btnW);
    if (ImGui::BeginCombo("##simtype", s_SimTypeNames[s_SimType])) {
        for (int si = 0; si < s_NumSimTypes; si++) {
            bool sel = (si == s_SimType);
            if (ImGui::Selectable(s_SimTypeNames[si], sel)) {
                s_SimType = si;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    if (ImGui::Button("Combat Simulator", ImVec2(btnW, btnH))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* Send CLC_LOBBY_START to the dedicated server with arena and simulant settings.
         * Server configures g_MpSetup, assigns playernums, builds chrslots, then
         * broadcasts SVC_STAGE_START to all clients. */
        netLobbyRequestStartWithSims(GAMEMODE_MP,
            (u8)s_Arenas[s_SelectedArena].stagenum,
            0,
            (u8)s_NumSims,
            (u8)s_SimType);
    }

    ImGui::Spacing();

    if (ImGui::Button("Co-op Campaign", ImVec2(btnW, btnH))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* Push co-op config dialog for mission/difficulty selection.
         * The config dialog's "Start" button sends CLC_LOBBY_START
         * with the chosen stage and difficulty. */
        menuPushDialog(&g_NetCoopHostMenuDialog);
    }

    ImGui::Spacing();

    if (ImGui::Button("Counter-Operative", ImVec2(btnW, btnH))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* Counter-op uses the same config dialog as co-op */
        menuPushDialog(&g_NetCoopHostMenuDialog);
    }

    if (!isLeader) ImGui::EndDisabled();

    ImGui::EndChild();

    /* ---- Footer ---- */
    ImGui::Separator();
    if (isLeader) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 0.9f),
                           "You are the lobby leader. Choose a game mode to start.");
    } else {
        ImGui::TextDisabled("Waiting for the lobby leader to start a game...");
    }

    ImGui::Spacing();

    /* Disconnect button */
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
