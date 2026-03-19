/**
 * pdgui_menu_network.cpp -- ImGui replacements for the Network Game menus.
 *
 * Replaces g_NetMenuDialog, g_NetHostMenuDialog, and g_NetJoinMenuDialog
 * with clean ImGui versions that are properly navigable with both
 * mouse/keyboard and controller.
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
#include "pdgui_audio.h"
#include "system.h"

extern "C" {

/* Dialogs we replace */
extern struct menudialogdef g_NetMenuDialog;
extern struct menudialogdef g_NetHostMenuDialog;
extern struct menudialogdef g_NetJoinMenuDialog;

/* Network functions */
s32 netStartServer(u16 port, s32 maxclients);
s32 netStartClient(const char *addr);

/* Net menu state — shared with netmenu.c */
extern s32 g_NetMenuMaxPlayers;
extern s32 g_NetMenuPort;
extern char g_NetJoinAddr[];
extern s32 g_NetServerPort;
extern s32 g_NetMaxClients;

#define NET_MAX_ADDR 256
#define NET_DEFAULT_PORT 27100
#define NET_MAX_CLIENTS 8

#define NETGAMEMODE_MP   0
#define NETGAMEMODE_COOP 1

extern u8 g_NetGameMode;

/* Menu stack */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* Handlers from netmenu.c that we call to trigger game flows */
typedef s32 MenuItemHandlerResult;
#define MENUOP_SET 6

MenuItemHandlerResult menuhandlerHostStart(s32 operation, struct menuitem *item, union handlerdata *data);
MenuItemHandlerResult menuhandlerMainMenuCombatSimulator(s32 operation, struct menuitem *item, union handlerdata *data);

extern struct menudialogdef g_NetCoopHostMenuDialog;
extern struct menudialogdef g_NetJoiningDialog;

/* Video */
s32 viGetWidth(void);
s32 viGetHeight(void);

} /* extern "C" */

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_RegisteredNet = false;
static bool s_RegisteredHost = false;
static bool s_RegisteredJoin = false;

/* Local copies of settings for the ImGui sliders/inputs */
static int s_HostPort = NET_DEFAULT_PORT;
static int s_HostMaxPlayers = NET_MAX_CLIENTS;
static char s_JoinAddress[NET_MAX_ADDR + 1] = {0};

/* ========================================================================
 * Helper: draw a standard network dialog frame
 * ======================================================================== */

static float drawNetFrame(float winW, float winH, float dialogW, float dialogH, const char *title)
{
    float dialogX = (winW - dialogW) * 0.5f;
    float dialogY = (winH - dialogH) * 0.5f;
    float pdTitleH = 24.0f * (winH / 480.0f);

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    /* Opaque backdrop */
    ImDrawList *bgDl = ImGui::GetBackgroundDrawList();
    bgDl->AddRectFilled(ImVec2(dialogX, dialogY),
                        ImVec2(dialogX + dialogW, dialogY + dialogH),
                        IM_COL32(8, 8, 16, 255));

    return pdTitleH;
}

/* ========================================================================
 * Network Game Menu (Host / Join / Back)
 * ======================================================================== */

static s32 renderNetMenu(struct menudialog *dialog,
                          struct menu *menu,
                          s32 winW, s32 winH)
{
    float scale = (float)winH / 480.0f;
    float dialogW = 340.0f * scale;
    float dialogH = 260.0f * scale;
    float dialogX = ((float)winW - dialogW) * 0.5f;
    float dialogY = ((float)winH - dialogH) * 0.5f;
    float pdTitleH = 24.0f * scale;

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    if (!ImGui::Begin("##net_menu", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    /* Opaque + PD frame */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255));
    }
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, "Network Game", 1);

    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize("Network Game");
        dl->AddText(ImVec2(dialogX + (dialogW - ts.x) * 0.5f,
                           dialogY + (pdTitleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), "Network Game");
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y + 8.0f * scale);

    float btnW = dialogW - ImGui::GetStyle().WindowPadding.x * 4;
    float btnH = 34.0f * scale;
    float gap = 8.0f * scale;

    /* Center buttons */
    float startX = (dialogW - btnW) * 0.5f;

    ImGui::SetCursorPosX(startX);
    if (ImGui::Button("Host Game", ImVec2(btnW, btnH))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        s_HostPort = g_NetServerPort;
        s_HostMaxPlayers = g_NetMaxClients;
        menuPushDialog(&g_NetHostMenuDialog);
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX(startX);
    if (ImGui::Button("Join Game", ImVec2(btnW, btnH))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        if (s_JoinAddress[0] == '\0') {
            extern char g_NetLastJoinAddr[];
            strncpy(s_JoinAddress, g_NetLastJoinAddr, NET_MAX_ADDR);
        }
        menuPushDialog(&g_NetJoinMenuDialog);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetCursorPosX(startX);
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
 * Host Game Screen
 * ======================================================================== */

static s32 renderHostMenu(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
    float scale = (float)winH / 480.0f;
    float dialogW = 380.0f * scale;
    float dialogH = 280.0f * scale;
    float dialogX = ((float)winW - dialogW) * 0.5f;
    float dialogY = ((float)winH - dialogH) * 0.5f;
    float pdTitleH = 24.0f * scale;

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    if (!ImGui::Begin("##host_menu", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255));
    }
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, "Host Game", 1);

    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize("Host Game");
        dl->AddText(ImVec2(dialogX + (dialogW - ts.x) * 0.5f,
                           dialogY + (pdTitleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), "Host Game");
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y + 8.0f * scale);

    float itemW = dialogW - ImGui::GetStyle().WindowPadding.x * 4;

    /* Max Players slider */
    ImGui::Text("Max Players");
    ImGui::PushItemWidth(itemW);
    ImGui::SliderInt("##max_players", &s_HostMaxPlayers, 2, NET_MAX_CLIENTS);
    ImGui::PopItemWidth();

    ImGui::Spacing();

    /* Port */
    ImGui::Text("Port");
    ImGui::PushItemWidth(itemW);
    ImGui::InputInt("##port", &s_HostPort, 0, 0);
    ImGui::PopItemWidth();
    if (s_HostPort < 1024) s_HostPort = 1024;
    if (s_HostPort > 65535) s_HostPort = 65535;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btnW = 140.0f * scale;
    float btnH = 30.0f * scale;
    float totalW = btnW * 2 + 10.0f * scale;
    ImGui::SetCursorPosX((dialogW - totalW) * 0.5f);

    if (ImGui::Button("Start Server", ImVec2(btnW, btnH)) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* Write settings back to the net globals */
        g_NetServerPort = (u32)s_HostPort;
        g_NetMaxClients = s_HostMaxPlayers;
        g_NetMenuPort = s_HostPort;
        g_NetMenuMaxPlayers = s_HostMaxPlayers;
        /* Call the original host start handler which does netStartServer
         * and opens the Combat Simulator dialog */
        menuhandlerHostStart(MENUOP_SET, NULL, NULL);
    }
    ImGui::SetItemDefaultFocus();

    ImGui::SameLine(0, 10.0f * scale);

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
 * Join Game Screen
 * ======================================================================== */

static s32 renderJoinMenu(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
    float scale = (float)winH / 480.0f;
    float dialogW = 420.0f * scale;
    float dialogH = 240.0f * scale;
    float dialogX = ((float)winW - dialogW) * 0.5f;
    float dialogY = ((float)winH - dialogH) * 0.5f;
    float pdTitleH = 24.0f * scale;

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    if (!ImGui::Begin("##join_menu", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        ImGui::SetKeyboardFocusHere();
    }

    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255));
    }
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, "Join Game", 1);

    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize("Join Game");
        dl->AddText(ImVec2(dialogX + (dialogW - ts.x) * 0.5f,
                           dialogY + (pdTitleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), "Join Game");
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y + 8.0f * scale);

    float itemW = dialogW - ImGui::GetStyle().WindowPadding.x * 4;

    /* Address input */
    ImGui::Text("Server Address (IP:Port)");
    ImGui::PushItemWidth(itemW);
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    bool enterPressed = ImGui::InputText("##address", s_JoinAddress,
                                          sizeof(s_JoinAddress), inputFlags);
    ImGui::PopItemWidth();

    ImGui::Spacing();

    /* Hint text */
    if (s_JoinAddress[0] == '\0') {
        ImGui::TextDisabled("Enter the host's IP address (e.g., 192.168.1.5:27100)");
    } else {
        ImGui::TextDisabled("Press Connect or Enter to join");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btnW = 140.0f * scale;
    float btnH = 30.0f * scale;
    float totalW = btnW * 2 + 10.0f * scale;
    ImGui::SetCursorPosX((dialogW - totalW) * 0.5f);

    bool canConnect = (s_JoinAddress[0] != '\0');
    if (!canConnect) ImGui::BeginDisabled();

    bool doConnect = ImGui::Button("Connect", ImVec2(btnW, btnH));
    if (enterPressed && canConnect) doConnect = true;

    if (doConnect) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* Copy address to net globals */
        strncpy(g_NetJoinAddr, s_JoinAddress, NET_MAX_ADDR);
        g_NetJoinAddr[NET_MAX_ADDR] = '\0';
        /* Start connection */
        if (netStartClient(g_NetJoinAddr) == 0) {
            menuPushDialog(&g_NetJoiningDialog);
        }
    }

    if (!canConnect) ImGui::EndDisabled();

    ImGui::SameLine(0, 10.0f * scale);

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
    if (!s_RegisteredNet) {
        pdguiHotswapRegister(&g_NetMenuDialog, renderNetMenu, "Network Game");
        s_RegisteredNet = true;
    }
    if (!s_RegisteredHost) {
        pdguiHotswapRegister(&g_NetHostMenuDialog, renderHostMenu, "Host Game");
        s_RegisteredHost = true;
    }
    if (!s_RegisteredJoin) {
        pdguiHotswapRegister(&g_NetJoinMenuDialog, renderJoinMenu, "Join Game");
        s_RegisteredJoin = true;
    }
    sysLogPrintf(LOG_NOTE, "pdgui_menu_network: Registered Network/Host/Join");
}

} /* extern "C" */
