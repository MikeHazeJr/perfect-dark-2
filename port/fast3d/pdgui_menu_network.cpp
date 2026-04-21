/**
 * pdgui_menu_network.cpp -- ImGui replacement for the Multiplayer menu.
 *
 * Replaces g_NetMenuDialog with: (1) **Host game** — in-process listen server
 * (`netStartServer`, `g_NetDedicated == false`, same slot-0 model as `--host`); and
 * (2) **Join** — Server Browser + connect-code direct connect to a dedicated or listen host.
 * Connect codes are the only player-facing address format (see context/constraints.md).
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
#include "screenmfst.h"
#include "net/netmanifest.h"
#include "net/netupnp.h"
#include "net/netstun.h"
#include "system.h"
#include "connectcode.h"

extern "C" {

/* Dialog we replace */
extern struct menudialogdef g_NetMenuDialog;

/* Network functions */
s32 netStartClient(const char *addr);
s32 netStartClientWithHolePunch(const char *addr);
s32 netStartServer(u16 port, s32 maxclients);
s32 netDisconnect(void);

/* Net menu state — shared with netmenu.c */
extern char g_NetJoinAddr[];
extern s32 g_NetServerPort;
extern s32 g_NetMenuPort;
extern s32 g_NetMode;
extern s32 g_NetDedicated;

#define NETMODE_NONE   0
#define NETMODE_SERVER 1
#define NETMODE_CLIENT 2

#define NET_MAX_ADDR 256
#define NET_DEFAULT_PORT 27100
#define NET_MAX_CLIENTS 32  /* must match NET_MAX_CLIENTS in port/include/net/net.h */

/* Server browser data */
struct netrecentserver_view {
    char addr[NET_MAX_ADDR + 1];
    u8 flags;
    u8 numclients;
    u8 maxclients;
    u8 stagenum;
    u8 scenario;
    char hostname[16];
    u32 online;
};

extern s32 g_NetNumRecentServers;
void netQueryRecentServers(void);

/* Bridge for reading recent server data */
s32 netRecentServerGetCount(void);
s32 netRecentServerGetInfo(s32 idx, char *addr, s32 addrSize,
                           u8 *flags, u8 *numclients, u8 *maxclients,
                           u32 *online);

/* Menu stack */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* Join dialog */
extern struct menudialogdef g_NetJoiningDialog;

/* Video */
s32 viGetWidth(void);
s32 viGetHeight(void);

/* Agent name */
const char *mpPlayerConfigGetName(s32 playernum);

} /* extern "C" */

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_Registered = false;
static char s_JoinAddress[NET_MAX_ADDR + 1] = {0};

/* Listen-host UI (P3-A): port/max persist for the session; initialized from g_NetServerPort on open. */
static int  s_HostPort       = (int)NET_DEFAULT_PORT;
static int  s_HostMaxRemotes = 7; /* +1 local host slot -> netStartServer second arg */
static char s_HostErr[160]   = {0};

static const char *fmtUpnpStatus(s32 st)
{
	switch (st) {
	case UPNP_STATUS_IDLE:    return "Idle";
	case UPNP_STATUS_WORKING: return "Working…";
	case UPNP_STATUS_SUCCESS: return "OK";
	case UPNP_STATUS_FAILED:  return "Failed";
	default:                  return "?";
	}
}

static const char *fmtStunStatus(s32 st)
{
	switch (st) {
	case STUN_STATUS_IDLE:    return "Idle";
	case STUN_STATUS_WORKING: return "Working…";
	case STUN_STATUS_SUCCESS: return "OK";
	case STUN_STATUS_FAILED:  return "Failed";
	default:                  return "?";
	}
}

/* F-IP-Browser: Convert a raw "A.B.C.D" or "A.B.C.D:port" address string to a
 * connect code for display. sscanf stops at ':' so port is harmlessly ignored. */
static bool addrStringToConnectCode(const char *addrStr, char *buf, s32 bufsize)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(addrStr, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
    u32 ip = (u32)a | ((u32)b << 8) | ((u32)c << 16) | ((u32)d << 24);
    return connectCodeEncode(ip, buf, bufsize) >= 0;
}

/* ========================================================================
 * Multiplayer Menu — Server Browser + Direct IP
 * ======================================================================== */

static s32 renderMultiplayerMenu(struct menudialog *dialog,
                                  struct menu *menu,
                                  s32 winW, s32 winH)
{
    float scale = pdguiScaleFactor();
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;
    float pdTitleH = pdguiScale(39.0f);

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    if (!ImGui::Begin("##multiplayer_menu", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: network/join menu OPEN");
        s_HostPort = (int)g_NetServerPort;
        if (s_HostPort < 1 || s_HostPort > 65535) {
            s_HostPort = (int)NET_DEFAULT_PORT;
        }
        s_HostErr[0] = '\0';
        /* Restore last used address */
        extern char g_NetLastJoinAddr[];
        if (s_JoinAddress[0] == '\0') {
            char code[CONNECT_CODE_MAX];
            if (addrStringToConnectCode(g_NetLastJoinAddr, code, sizeof(code))) {
                strncpy(s_JoinAddress, code, NET_MAX_ADDR);
            } else {
                strncpy(s_JoinAddress, g_NetLastJoinAddr, NET_MAX_ADDR);
            }
            s_JoinAddress[NET_MAX_ADDR] = '\0';
        }
        /* Query servers on open */
        netQueryRecentServers();
    }

    /* Opaque backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          pdguiPalImU32(PDPAL_BODYBG, 255));
    }

    /* Show agent name in title */
    const char *agentName = mpPlayerConfigGetName(0);
    char titleBuf[64];
    if (agentName && agentName[0]) {
        snprintf(titleBuf, sizeof(titleBuf), "Multiplayer - %s", agentName);
    } else {
        snprintf(titleBuf, sizeof(titleBuf), "Multiplayer");
    }

    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, titleBuf, 1);

    /* Title */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(titleBuf);
        dl->AddText(ImVec2(dialogX + (dialogW - ts.x) * 0.5f,
                           dialogY + (pdTitleH - ts.y) * 0.5f),
                    pdguiPalImU32(PDPAL_TITLEFG, 255), titleBuf);
    }

    pdguiSetCursorBelowTitle(pdTitleH);

    float itemW = dialogW - ImGui::GetStyle().WindowPadding.x * 4;

    /* Reserve bottom space for the docked action bar (C1) so scroll body +
     * inline Connect row never push Back out of reach. */
    float bodyAvail = ImGui::GetContentRegionAvail().y;
    float bodyH     = pdguiBodyHeightForActionBar(bodyAvail);
    float sectionH  = bodyH - 100.0f * scale;

    ImGui::BeginChild("##mp_body", ImVec2(0, bodyH), true);

    /* ---- Host game (listen server, same process as --host / g_NetHostLatch) ---- */
    ImGui::TextColored(pdguiVec4TitleGlow(), "Host game (this PC)");
    ImGui::Separator();

    if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
        ImGui::TextColored(pdguiVec4TintSuccess(), "You are hosting.");
        ImGui::TextDisabled("UPnP: %s  |  STUN: %s",
            fmtUpnpStatus(netUpnpGetStatus()),
            fmtStunStatus(stunGetStatus()));
        if (ImGui::Button("Stop hosting")) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            netDisconnect();
            s_HostErr[0] = '\0';
        }
    } else {
        ImGui::TextWrapped(
            "Start a listen server: you stay in slot 0; friends join with your connect code "
            "(shown in the lobby overlay). Default UDP port is stored in pd.ini as Net.Server.Port.");
        ImGui::PushItemWidth(140.0f * scale);
        ImGui::InputInt("Port##host", &s_HostPort);
        ImGui::PopItemWidth();
        if (s_HostPort < 1) {
            s_HostPort = 1;
        }
        if (s_HostPort > 65535) {
            s_HostPort = 65535;
        }
        ImGui::SliderInt("Max remote players##hostmax", &s_HostMaxRemotes, 1, NET_MAX_CLIENTS - 1);
        ImGui::TextDisabled(
            "NAT discovery runs after the server binds (UPnP %s, STUN %s).",
            fmtUpnpStatus(netUpnpGetStatus()),
            fmtStunStatus(stunGetStatus()));
        if (s_HostErr[0]) {
            ImGui::TextColored(pdguiVec4TintDanger(), "%s", s_HostErr);
        }

        if (ImGui::Button("Host game / Go online", ImVec2(-1.0f, 0.0f))) {
            s_HostErr[0] = '\0';
            pdguiPlaySound(PDGUI_SND_SELECT);
            if (g_NetMode == NETMODE_CLIENT) {
                netDisconnect();
            }
            g_NetServerPort = (u32)s_HostPort;
            g_NetMenuPort   = s_HostPort;
            /* Second arg is total player slots (listen host in slot 0 + remotes). */
            s32 rc = netStartServer((u16)s_HostPort, s_HostMaxRemotes + 1);
            if (rc == 0) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: listen host started port=%d maxclients=%d",
                    s_HostPort, s_HostMaxRemotes + 1);
                menuPopDialog();
            } else if (rc == -1) {
                snprintf(s_HostErr, sizeof(s_HostErr), "Already in a network session.");
            } else if (rc == -2) {
                snprintf(s_HostErr, sizeof(s_HostErr), "Could not listen on that port (in use or denied).");
            } else {
                snprintf(s_HostErr, sizeof(s_HostErr), "Host failed (error %d).", rc);
            }
        }
        if (g_NetMode == NETMODE_CLIENT) {
            ImGui::TextDisabled("Joining: the Host button disconnects you first, then starts hosting.");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* ---- Server Browser section ---- */
    ImGui::TextColored(pdguiVec4TitleGlow(), "Server Browser");
    ImGui::SameLine(itemW - 60.0f * scale);
    if (ImGui::SmallButton("Refresh")) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        netQueryRecentServers();
    }
    ImGui::Separator();

    float browserH = sectionH * 0.55f;
    ImGui::BeginChild("##server_list", ImVec2(itemW, browserH), true);

    s32 serverCount = netRecentServerGetCount();
    if (serverCount == 0) {
        ImGui::TextDisabled("No servers found.");
        ImGui::TextDisabled("Add a server address below, or check your network.");
    } else {
        for (s32 i = 0; i < serverCount; i++) {
            char addr[NET_MAX_ADDR + 1];
            u8 flags = 0, numclients = 0, maxclients = 0;
            u32 online = 0;
            if (!netRecentServerGetInfo(i, addr, sizeof(addr),
                                         &flags, &numclients, &maxclients, &online)) {
                continue;
            }

            ImGui::PushID(i);

            const char *status = "Offline";
            ImVec4 statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            if (online) {
                if (flags & 1) {
                    status = "In Game";
                    statusColor = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
                } else {
                    status = "Lobby";
                    statusColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                }
            }

            /* Server row: clickable to select address. Display as connect code
             * to avoid exposing raw IP in the UI. */
            char addrCode[CONNECT_CODE_MAX];
            if (!addrStringToConnectCode(addr, addrCode, sizeof(addrCode))) {
                strncpy(addrCode, addr, sizeof(addrCode) - 1);
                addrCode[sizeof(addrCode) - 1] = '\0';
            }
            char label[128];
            snprintf(label, sizeof(label), "%-30s", addrCode);

            if (ImGui::Selectable(label, false)) {
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                strncpy(s_JoinAddress, addrCode, NET_MAX_ADDR);
                s_JoinAddress[NET_MAX_ADDR] = '\0';
            }

            ImGui::SameLine();
            ImGui::TextColored(statusColor, "[%s]", status);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 0.9f),
                               "%u/%u", numclients, maxclients);

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    /* ---- Direct IP / Connect Code section ---- */
    ImGui::TextColored(pdguiVec4TitleGlow(), "Direct Connect");
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Enter IP:port or connect code");

    ImGui::PushItemWidth(itemW - 120.0f * scale);
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    bool enterPressed = ImGui::InputText("##address", s_JoinAddress,
                                          sizeof(s_JoinAddress), inputFlags);

    /* Right-click the address field to paste the system clipboard. Connect
     * codes are short 4-word sentences users share via Discord/Slack — the
     * classic paste affordance matters here. SDL owns the clipboard even
     * though ImGui has its own textbox. Strip trailing whitespace/newlines
     * so pastes from chat clients don't carry a stray '\n' that would
     * reject the decode. */
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            strncpy(s_JoinAddress, clip, sizeof(s_JoinAddress) - 1);
            s_JoinAddress[sizeof(s_JoinAddress) - 1] = '\0';
            for (s32 i = (s32)strlen(s_JoinAddress) - 1; i >= 0; i--) {
                char c = s_JoinAddress[i];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                    s_JoinAddress[i] = '\0';
                } else {
                    break;
                }
            }
            SDL_free(clip);
            sysLogPrintf(LOG_NOTE,
                "MENU_IMGUI: network menu PASTE addr=\"%s\"", s_JoinAddress);
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();

    bool canConnect = (s_JoinAddress[0] != '\0');
    if (!canConnect) ImGui::BeginDisabled();

    float connectBtnW = 100.0f * scale;
    bool doConnect = ImGui::Button("Connect", ImVec2(connectBtnW, 0));
    if (enterPressed && canConnect) doConnect = true;

    if (doConnect) {
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: network menu CONNECT pressed addr=\"%s\"", s_JoinAddress);
        pdguiPlaySound(PDGUI_SND_SELECT);

        /* Detect if input is a connect code (contains alpha chars) or raw IP */
        bool isConnectCode = false;
        for (const char *ch = s_JoinAddress; *ch; ch++) {
            if ((*ch >= 'A' && *ch <= 'Z') || (*ch >= 'a' && *ch <= 'z')) {
                isConnectCode = true;
                break;
            }
        }

        /* All join attempts must go through connect code decode.
         * Raw IP addresses are not accepted -- the code is a security
         * layer that prevents exposing public IPs. */
        {
            u32 ip = 0;
            if (connectCodeDecode(s_JoinAddress, &ip) == 0 && ip) {
                snprintf(g_NetJoinAddr, NET_MAX_ADDR, "%u.%u.%u.%u:%u",
                         ip & 0xFF, (ip >> 8) & 0xFF,
                         (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, CONNECT_DEFAULT_PORT);
                if (netStartClientWithHolePunch(g_NetJoinAddr) == 0) {
                    menuPushDialog(&g_NetJoiningDialog);
                }
            }
            /* Invalid code -- field stays, user can retry */
        }
    }

    if (!canConnect) ImGui::EndDisabled();

    ImGui::EndChild(); /* ##mp_body */

    /* ---- Docked action bar (C1): Back always reachable ---- */
    bool backActivated = false;
    if (pdguiBeginActionBar("##mp_net_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            backActivated = true;
        }
    }
    pdguiEndActionBar();

    if (backActivated || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: network/join menu CLOSE via Back/ESC");
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMenuNetworkRegister(void)
{
    if (!s_Registered) {
        pdguiHotswapRegister(&g_NetMenuDialog, renderMultiplayerMenu, "Multiplayer");
        s_Registered = true;

        /* Phase 6: Screen mini-manifest.
         * Multiplayer hub displays mode names and lobby strings — declare
         * the MP language bank it needs. */
        {
            static const char *ids[] = {
                "base:lang_mpmenu",  /* MP menu / lobby strings */
                "base:lang_misc",    /* General UI strings */
            };
            static const u8 types[] = {
                MANIFEST_TYPE_LANG,
                MANIFEST_TYPE_LANG,
            };
            screenManifestRegister(
                (void*)&g_NetMenuDialog,
                ids, types, 2);
        }
    }
    sysLogPrintf(LOG_NOTE, "pdgui_menu_network: Registered Multiplayer menu");
}

} /* extern "C" */
