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
#include "pdgui_layout.h"
#include "pdgui_nav.h"
#include "system.h"
#include "hub.h"
#include "room.h"
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
    u8 clientId;
};
s32 lobbyGetPlayerInfo(s32 idx, struct lobbyplayer_view *out);

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

/* R-3: Room networking — send create/join/leave to server.
 * SEC-14 (v38): CLC_ROOM_CREATE now carries access mode + password + max_players;
 * CLC_ROOM_JOIN carries an optional password. */
struct netbuf;
u32 netmsgClcRoomCreateWrite(struct netbuf *dst, const char *name, u8 access,
                              const char *password, u8 maxPlayers);
u32 netmsgClcRoomJoinWrite(struct netbuf *dst, u8 room_id, const char *password);
u32 netmsgClcRoomLeaveWrite(struct netbuf *dst);
u32 netSend(struct netclient *dstcl, struct netbuf *buf, const s32 reliable, const s32 chan);
extern struct netbuf g_NetMsgRel;
void netbufStartWrite(struct netbuf *buf);

} /* extern "C" */

/* S391 M-5-A: Disconnect-from-server confirm modal tracker.
 * Initialized to -1; set to ImGui::GetFrameCount() when the popup opens.
 * pdguiRenderConfirmModal consults it for the 5-frame force-focus latch
 * and 3-frame input debounce, and clears it back to -1 on dismiss. */
static s32 s_LobbyDisconnectOpenFrame = -1;

static s32 lobbyGraphCreateRoom(void * /*userdata*/)
{
    sysLogPrintf(LOG_NOTE, "LOBBY: sending CLC_ROOM_CREATE to server");
    /* SEC-14: default create goes out as an open room with the hub-wide
     * max_players.  A follow-up UI pass should expose access mode +
     * password + max_players fields in the "Create Room" dialog. */
    netbufStartWrite(&g_NetMsgRel);
    netmsgClcRoomCreateWrite(&g_NetMsgRel, "", /*access=*/0,
                              /*password=*/"", /*maxPlayers=*/0);
    netSend(NULL, &g_NetMsgRel, 1, 0);
    return 0;
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
                if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4
                        && a <= 255 && b <= 255 && c <= 255 && d <= 255) {
                    ipAddr = a | (b << 8) | (c << 16) | (d << 24);
                }
            }
            if (ipAddr) {
                u32 port = netGetServerPort();
                if (port < 1 || port > 65535) port = CONNECT_DEFAULT_PORT;
                connectCodeEncodeWithPort(ipAddr, (u16)port, s_ConnectCode, sizeof(s_ConnectCode));
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

    /* Two-column layout — sized to leave bottom space for the docked
     * action bar (C1) that hosts Create Room + Disconnect. */
    float pad = 8.0f * scale;
    float colW = (dialogW - pad * 3.0f) * 0.5f;
    float bodyAvail = ImGui::GetContentRegionAvail().y;
    float contentH  = pdguiBodyHeightForActionBar(bodyAvail)
                    - 60.0f * scale; /* reserve the footer chat-stub row */

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
                           "  %d player%s",
                           entry->client_count,
                           entry->client_count == 1 ? "" : "s");

        if (!g_NetDedicated && entry->state == ROOM_STATE_LOBBY) {
            float joinW = pdguiScale(84.0f);
            char joinId[32];
            snprintf(joinId, sizeof(joinId), "Join##r%d", ri);
            ImGui::SameLine(innerW - joinW);
            if (ImGui::Button(joinId, ImVec2(joinW, 0))) {
                pdguiPlaySound(PDGUI_SND_SELECT);
                sysLogPrintf(LOG_NOTE, "LOBBY: sending CLC_ROOM_JOIN for room %u", (unsigned)entry->id);
                /* SEC-14: the UI does not yet expose a password prompt — join
                 * attempts on password-protected rooms will be rejected
                 * server-side.  A follow-up UI pass should prompt for the
                 * password here (open a confirm modal with a text input). */
                netbufStartWrite(&g_NetMsgRel);
                netmsgClcRoomJoinWrite(&g_NetMsgRel, entry->id, "");
                netSend(NULL, &g_NetMsgRel, 1, 0);
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
     * Footer — server chat stub (above the docked action bar)
     * ================================================================ */
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f, 0.4f, 0.5f, 0.6f), "Server Chat  (coming soon)");

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

    if (wantCreate) {
        menuGraphFireNetworkOp(MENU_TYPE_SOCIAL_LOBBY, "create_room",
                               lobbyGraphCreateRoom, NULL);
    }

    /* S391 M-5-A: route Disconnect button + Esc through a confirm modal
     * (canonical S385 pattern) instead of tearing down the session on a
     * single click / stray keystroke.  The modal fires the graph disconnect
     * edge only when the user confirms. */
    const char *discPopupId = "Disconnect from Server?##lobby_disconnect";
    if ((wantDisconnect || pdguiMenuCancelPressed()) &&
            !ImGui::IsPopupOpen(discPopupId)) {
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
