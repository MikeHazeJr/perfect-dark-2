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
 *   Footer      — Room request status + Create / Disconnect action bar
 *
 * Create and Join use the same authoritative operation for the local listen
 * host and remote peers. Only an accepted assignment opens the room interior.
 *
 * Architecture: In-client listen hosting with remote peers.
 * Called from pdguiLobbyRender() in pdgui_lobby.cpp.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include "net/lobby_view.h"
#include "assetcatalog.h"
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui_nav.h"
#include "system.h"
#include "hub.h"
#include "room.h"
#include "net/room_ui.h"
#include "inputctx.h"
#include "menupool.h"
#include "menugraph.h"

extern "C" {

/* Network state */
s32 netGetMode(void);
s32 netGetMaxClients(void);
u32 netGetServerPort(void);
const char *netGetPublicIP(void);

/* Connect codes (connectcode.c) */
s32 connectCodeEncode(u32 ip, char *buf, s32 bufsize);
s32 connectCodeEncodeWithPort(u32 ip, u16 port, char *buf, s32 bufsize);
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


/* Character accessor */
char *mpGetBodyName(u8 mpbodynum);
u32 mpGetNumBodies(void);
/* Phase 5: catalog ID accessors for lobby players */
const char *lobbyGetPlayerBodyId(s32 idx);
const char *lobbyGetPlayerHeadId(s32 idx);

/* Check if local client is in lobby state */
s32 netLocalClientInLobby(void);

/* Agent name */
const char *mpPlayerConfigGetName(s32 playernum);

/* Routing: transition into room interior (pdgui_lobby.cpp) */
void pdguiSetInRoom(s32 inRoom);

} /* extern "C" */

/* S391 M-5-A: Disconnect-from-server confirm modal tracker.
 * Initialized to -1; set to ImGui::GetFrameCount() when the popup opens.
 * pdguiRenderConfirmModal consults it for the 5-frame force-focus latch
 * and 3-frame input debounce, and clears it back to -1 on dismiss. */
static s32 s_LobbyDisconnectOpenFrame = -1;

static char s_CreateRoomName[ROOM_NAME_MAX];
static char s_CreateRoomPassword[32];
static int s_CreateRoomAccess = ROOM_ACCESS_OPEN;
static int s_CreateRoomCapacity = 4;
static u8 s_JoinRoomId = 0xff;
static char s_JoinRoomPassword[32];

static s32 lobbyGraphCreateRoom(void * /*userdata*/)
{
    return netRequestRoomCreate(s_CreateRoomName, (u8)s_CreateRoomAccess,
        s_CreateRoomPassword, (u8)s_CreateRoomCapacity);
}

static s32 lobbyGraphDisconnect(void * /*userdata*/)
{
    return netDisconnect();
}

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

    float pdTitleH = pdguiScale(39.0f);

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##social_lobby", nullptr, wflags)) {
        menupoolRelease(MENU_TYPE_SOCIAL_LOBBY);
        ImGui::End();
        return;
    }

    menupoolAcquire(MENU_TYPE_SOCIAL_LOBBY, NULL, &g_CtxImGuiMenu);

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: social lobby OPEN");
    }

    /* Opaque backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          pdguiPalImU32(PDPAL_BODYBG, 255));
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
                    pdguiPalImU32(PDPAL_TITLEFG, 255), title);
    }

    pdguiSetCursorBelowTitle(pdTitleH);

    /* Connection status + player count */
    {
        s32 numPlayers = lobbyGetPlayerCount();
        s32 maxPlayers = netGetMaxClients();
        ImGui::TextColored(pdguiVec4TitleGlow(),
                           netGetMode() == NETMODE_SERVER ? "Hosting multiplayer" : "Connected to host");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f),
                           "  Players: %d / %d", numPlayers, maxPlayers);
    }

    /* Connect code (server host view only) */
    if (netGetMode() == NETMODE_SERVER) {
        static char s_ConnectCode[128] = "";
        static u32 s_CodeIp = 0;
        static u16 s_CodePort = 0;
        {
            const char *ip = netGetPublicIP();
            u32 ipAddr = 0;
            if (ip) {
                u32 a=0,b=0,c=0,d=0;
                if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4
                        && a <= 255 && b <= 255 && c <= 255 && d <= 255) {
                    ipAddr = a | (b << 8) | (c << 16) | (d << 24);
                }
            }
            u32 port = netGetServerPort();
            if (port < 1 || port > 65535) port = CONNECT_DEFAULT_PORT;
            if (ipAddr != s_CodeIp || port != s_CodePort) {
                s_CodeIp = ipAddr;
                s_CodePort = (u16)port;
                s_ConnectCode[0] = 0;
                if (ipAddr) connectCodeEncodeWithPort(ipAddr, (u16)port,
                    s_ConnectCode, sizeof(s_ConnectCode));
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

    /* Two-column layout — sized to leave bottom space for the docked
     * action bar (C1) that hosts Create Room + Disconnect. */
    float pad = 8.0f * scale;
    float colW = (dialogW - pad * 3.0f) * 0.5f;
    float bodyAvail = ImGui::GetContentRegionAvail().y;
    float contentH  = pdguiBodyHeightForActionBar(bodyAvail)
                    - 60.0f * scale; /* reserve the room request status row */

    /* ================================================================
     * Left column — Connected Players
     * ================================================================ */
    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##social_players", ImVec2(colW, contentH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    ImGui::TextColored(pdguiVec4TitleGlow(), "Connected Players");
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

        /* Resolve presentation from the player's public catalog identity. */
        const char *bodyId = lobbyGetPlayerBodyId(i);
        const char *headId = lobbyGetPlayerHeadId(i);
        const asset_entry_t *character = assetCatalogFindCharacterByBodyHead(bodyId, headId);
        const asset_entry_t *body = assetCatalogResolve(bodyId);
        const char *bodyName = character && character->ext.character.display_name[0]
            ? character->ext.character.display_name
            : body && body->type == ASSET_BODY && body->ext.body.display_name[0]
                ? body->ext.body.display_name : bodyId;
        if (bodyName && bodyName[0]) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), " [%s]",
                               bodyName);
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
                stateColor = pdguiVec4TitleGlow();
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
    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##social_rooms", ImVec2(colW, contentH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    ImGui::TextColored(pdguiVec4TitleGlow(), "Active Rooms");
    ImGui::Separator();

    float innerW = colW - ImGui::GetStyle().WindowPadding.x * 2 - 4.0f;
    (void)innerW;
    /* Create Room button moved to the docked action bar (C1) so it never
     * scrolls out of reach when the room list fills the column. */

    /* Room list — populated from SVC_ROOM_LIST cache */
    s32 roomsShown = 0;
    for (s32 ri = 0; ri < g_RoomCacheCount; ri++) {
        room_cache_entry_t *entry = &g_RoomCache[ri];
        if (entry->client_count == 0 && entry->id == 0) continue; /* skip empty Lounge */
        roomsShown++;

        ImGui::PushID(ri);

        /* State color */
        const char *stateName = roomStateName((room_state_t)entry->state);
        ImVec4 stateColor;
        switch (entry->state) {
            case ROOM_STATE_LOBBY:    stateColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
            case ROOM_STATE_LOADING:  stateColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
            case ROOM_STATE_MATCH:    stateColor = pdguiVec4TitleGlow();              break;
            case ROOM_STATE_POSTGAME: stateColor = ImVec4(0.8f, 0.5f, 1.0f, 1.0f); break;
            default:                  stateColor = ImVec4(0.4f, 0.4f, 0.4f, 0.6f); break;
        }

        /* Room name row */
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "%s",
                           entry->name[0] ? entry->name : "Unnamed Room");
        ImGui::SameLine();
        ImGui::TextColored(stateColor, "[%s]", stateName);

        /* Player count + Join button on same row */
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f),
                           "  %u / %u players%s",
                           entry->client_count, entry->max_players,
                           entry->access == ROOM_ACCESS_PASSWORD ? " (password)" :
                           entry->access == ROOM_ACCESS_INVITE ? " (invite only)" : "");

        if (!g_NetDedicated && entry->state == ROOM_STATE_LOBBY) {
            float joinW = pdguiScale(84.0f);
            char joinId[32];
            snprintf(joinId, sizeof(joinId), "Join##r%d", ri);
            ImGui::SameLine(innerW - joinW);
            ImGui::BeginDisabled(netRoomRequestPending() || entry->client_count >= entry->max_players);
            if (ImGui::Button(joinId, ImVec2(joinW, 0))) {
                pdguiPlaySound(PDGUI_SND_SELECT);
                if (entry->access == ROOM_ACCESS_PASSWORD) {
                    s_JoinRoomId = entry->id;
                    s_JoinRoomPassword[0] = 0;
                } else {
                    netRequestRoomJoin(entry->id, "");
                }
            }
            ImGui::EndDisabled();
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
     * Footer — authoritative room request status
     * ================================================================ */
    ImGui::Separator();
    ImGui::TextWrapped("%s", netRoomRequestMessage());

    /* ================================================================
     * Docked action bar (C1) — Create Room (clients only) + Disconnect
     * ================================================================ */
    bool wantCreate = false;
    bool wantDisconnect = false;

    if (pdguiBeginActionBar("##lobby_ab")) {
        float availW = ImGui::GetContentRegionAvail().x;
        if (!g_NetDedicated) {
            if (pdguiActionBarButton("+ Create Room", 0, availW * 0.5f)) {
                wantCreate = true;
            }
            ImGui::SameLine();
            if (pdguiActionBarButton("Disconnect", 1,
                                     ImGui::GetContentRegionAvail().x)) {
                wantDisconnect = true;
            }
        } else {
            if (pdguiActionBarButton("Disconnect", 1, availW)) {
                wantDisconnect = true;
            }
        }
    }
    pdguiEndActionBar();

    const char *createPopupId = "Create Room##social_create";
    const char *joinPopupId = "Room Password##social_join";
    if (wantCreate && !netRoomRequestPending()) {
        if (s_CreateRoomCapacity > netGetMaxClients()) s_CreateRoomCapacity = netGetMaxClients();
        if (s_CreateRoomCapacity < 1) s_CreateRoomCapacity = 1;
        ImGui::OpenPopup(createPopupId);
    }
    if (s_JoinRoomId != 0xff && !ImGui::IsPopupOpen(joinPopupId)) ImGui::OpenPopup(joinPopupId);
    const bool roomModalOwnedInput = ImGui::IsPopupOpen(createPopupId) || ImGui::IsPopupOpen(joinPopupId);
    if (ImGui::BeginPopupModal(createPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool cancel = pdguiMenuCancelPressed();
        ImGui::InputText("Name", s_CreateRoomName, sizeof(s_CreateRoomName));
        ImGui::Combo("Access", &s_CreateRoomAccess, "Open\0Password\0");
        if (s_CreateRoomAccess == ROOM_ACCESS_PASSWORD)
            ImGui::InputText("Password", s_CreateRoomPassword, sizeof(s_CreateRoomPassword), ImGuiInputTextFlags_Password);
        ImGui::SliderInt("Players", &s_CreateRoomCapacity, 1, netGetMaxClients());
        ImGui::BeginDisabled(netRoomRequestPending()
            || (s_CreateRoomAccess == ROOM_ACCESS_PASSWORD && !s_CreateRoomPassword[0]));
        if (ImGui::Button("Create") && !cancel) {
            menuGraphFireNetworkOp(MENU_TYPE_SOCIAL_LOBBY, "create_room", lobbyGraphCreateRoom, nullptr);
            s_CreateRoomPassword[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || cancel) {
            s_CreateRoomPassword[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal(joinPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool cancel = pdguiMenuCancelPressed();
        ImGui::InputText("Password", s_JoinRoomPassword, sizeof(s_JoinRoomPassword), ImGuiInputTextFlags_Password);
        ImGui::BeginDisabled(netRoomRequestPending() || !s_JoinRoomPassword[0]);
        if (ImGui::Button("Join") && !cancel) {
            netRequestRoomJoin(s_JoinRoomId, s_JoinRoomPassword);
            s_JoinRoomId = 0xff;
            s_JoinRoomPassword[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || cancel) {
            s_JoinRoomId = 0xff;
            s_JoinRoomPassword[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    /* S391 M-5-A: route Disconnect button + Esc through a confirm modal
     * (canonical S385 pattern) instead of tearing down the session on a
     * single click / stray keystroke.  The modal fires the graph disconnect
     * edge only when the user confirms. */
    const char *discPopupId = "Disconnect from Server?##lobby_disconnect";
    if (!roomModalOwnedInput && (wantDisconnect || pdguiMenuCancelPressed()) &&
            !ImGui::IsPopupOpen(discPopupId)
            && !ImGui::IsPopupOpen(createPopupId) && !ImGui::IsPopupOpen(joinPopupId)) {
        sysLogPrintf(LOG_NOTE,
            "MENU_IMGUI: social lobby DISCONNECT confirm OPEN via button/ESC");
        ImGui::OpenPopup(discPopupId);
        s_LobbyDisconnectOpenFrame = (s32)ImGui::GetFrameCount();
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    s32 discRes = pdguiRenderConfirmModal(
        discPopupId,
        "Disconnect from Server?",
        "Disconnect from the server and return to the main menu?",
        "Disconnect",
        &s_LobbyDisconnectOpenFrame);
    if (discRes == PDGUI_CONFIRM_OK) {
        sysLogPrintf(LOG_NOTE,
            "MENU_IMGUI: social lobby DISCONNECT confirmed");
        ImGui::End();
        menuGraphFireNetworkOp(MENU_TYPE_SOCIAL_LOBBY, "disconnect",
                               lobbyGraphDisconnect, NULL);
        return;
    }

    ImGui::End();
}
