/**
 * pdgui_menu_network.cpp -- ImGui replacement for the Multiplayer menu.
 *
 * Replaces g_NetMenuDialog with a clean dedicated-server-only join screen.
 * Players connect to a dedicated server via Server Browser or Direct IP.
 * No host capability in the client.
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
#include "connectcode.h"

extern "C" {

/* Dialog we replace */
extern struct menudialogdef g_NetMenuDialog;

/* Network functions */
s32 netStartClient(const char *addr);

/* Net menu state — shared with netmenu.c */
extern char g_NetJoinAddr[];
extern s32 g_NetServerPort;

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
    float pdTitleH = pdguiScale(26.0f);

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
        /* Restore last used address */
        extern char g_NetLastJoinAddr[];
        if (s_JoinAddress[0] == '\0') {
            strncpy(s_JoinAddress, g_NetLastJoinAddr, NET_MAX_ADDR);
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
                          IM_COL32(8, 8, 16, 255));
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
                    IM_COL32(255, 255, 255, 255), titleBuf);
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    float itemW = dialogW - ImGui::GetStyle().WindowPadding.x * 4;
    float sectionH = dialogH - pdTitleH - 100.0f * scale;

    /* ---- Server Browser section ---- */
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Server Browser");
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

            /* Server row: clickable to select address */
            char label[128];
            snprintf(label, sizeof(label), "%-30s", addr);

            if (ImGui::Selectable(label, false)) {
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                strncpy(s_JoinAddress, addr, NET_MAX_ADDR);
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
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Direct Connect");
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Enter IP:port or connect code");

    ImGui::PushItemWidth(itemW - 120.0f * scale);
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    bool enterPressed = ImGui::InputText("##address", s_JoinAddress,
                                          sizeof(s_JoinAddress), inputFlags);
    ImGui::PopItemWidth();

    ImGui::SameLine();

    bool canConnect = (s_JoinAddress[0] != '\0');
    if (!canConnect) ImGui::BeginDisabled();

    float connectBtnW = 100.0f * scale;
    bool doConnect = ImGui::Button("Connect", ImVec2(connectBtnW, 0));
    if (enterPressed && canConnect) doConnect = true;

    if (doConnect) {
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
                if (netStartClient(g_NetJoinAddr) == 0) {
                    menuPushDialog(&g_NetJoiningDialog);
                }
            }
            /* Invalid code -- field stays, user can retry */
        }
    }

    if (!canConnect) ImGui::EndDisabled();

    /* ---- Footer ---- */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btnW = 120.0f * scale;
    float btnH = 28.0f * scale;
    ImGui::SetCursorPosX((dialogW - btnW) * 0.5f);

    if (ImGui::Button("Back", ImVec2(btnW, btnH)) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
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
    }
    sysLogPrintf(LOG_NOTE, "pdgui_menu_network: Registered Multiplayer menu");
}

} /* extern "C" */
