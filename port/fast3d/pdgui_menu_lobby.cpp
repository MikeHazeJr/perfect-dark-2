/**
 * pdgui_menu_lobby.cpp -- Network lobby screen.
 *
 * Replaces the Combat Simulator setup when in a network session.
 * Shows connected players, game mode selection, map selection,
 * and allows the lobby leader to configure and start the match.
 *
 * Supports: Combat Simulator, Co-op Campaign, Counter-Op.
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

/* Dialog we replace when in a network session */
extern struct menudialogdef g_CombatSimulatorMenuDialog;

/* Network state */
s32 netGetMode(void);
s32 netGetMaxClients(void);
u32 netGetServerPort(void);
const char *netGetPublicIP(void);
extern s32 g_NetDedicated;

#define NETMODE_NONE   0
#define NETMODE_SERVER 1
#define NETMODE_CLIENT 2

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
    char name[16];
    s32 isLocal;
    s32 state;
};
s32 lobbyGetPlayerInfo(s32 idx, struct lobbyplayer_view *out);

#define CLSTATE_LOBBY 3
#define CLSTATE_GAME  4

/* Game mode triggers */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* These trigger the actual game mode setup flows */
typedef s32 MenuItemHandlerResult;
#define MENUOP_SET 6
MenuItemHandlerResult menuhandlerMainMenuCombatSimulator(s32 operation, struct menuitem *item, union handlerdata *data);

/* Net menu handlers */
extern struct menudialogdef g_NetHostMenuDialog;

/* Character accessor */
char *mpGetBodyName(u8 mpbodynum);
u32 mpGetNumBodies(void);

/* Video */
s32 viGetWidth(void);
s32 viGetHeight(void);

} /* extern "C" */

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_Registered = false;

/* ========================================================================
 * ImGui Render Callback
 * ======================================================================== */

static s32 renderNetworkLobby(struct menudialog *dialog,
                               struct menu *menu,
                               s32 winW, s32 winH)
{
    /* Only render in a network session */
    s32 mode = netGetMode();
    if (mode == NETMODE_NONE) {
        return 0; /* Fall through to PD native */
    }

    lobbyUpdate();

    float scale = (float)winH / 480.0f;
    float dialogW = 520.0f * scale;
    float dialogH = 420.0f * scale;
    float dialogX = ((float)winW - dialogW) * 0.5f;
    float dialogY = ((float)winH - dialogH) * 0.5f;

    float pdTitleH = 26.0f * scale;

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
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    /* Opaque backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255), 0.0f);
    }

    const char *title = (mode == NETMODE_SERVER) ? "Network Lobby (Host)" : "Network Lobby";
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, title, 1);

    /* Title */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dialogX + (dialogW - titleSize.x) * 0.5f,
                           dialogY + (pdTitleH - titleSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), title);
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    /* Connection info for host */
    if (mode == NETMODE_SERVER) {
        const char *ip = netGetPublicIP();
        u32 port = netGetServerPort();
        if (ip && ip[0]) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Server: %s:%u", ip, port);
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 0.9f), "Server: port %u (UPnP pending...)", port);
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Connected to server");
    }

    ImGui::Separator();

    /* ================================================================
     * Two-column layout: Left = players, Right = settings
     * ================================================================ */
    float pad = 8.0f * scale;
    float leftW = dialogW * 0.45f;
    float rightW = dialogW * 0.45f;
    float contentH = dialogH - pdTitleH - 80.0f * scale;

    /* ---- Left column: Player list ---- */
    ImGui::BeginChild("##lobby_players_col", ImVec2(leftW, contentH), true);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Players");
    ImGui::Separator();

    s32 playerCount = lobbyGetPlayerCount();
    for (s32 i = 0; i < playerCount; i++) {
        struct lobbyplayer_view pv;
        memset(&pv, 0, sizeof(pv));
        if (!lobbyGetPlayerInfo(i, &pv)) continue;

        ImGui::PushID(i);

        /* Player entry */
        char label[64];
        const char *suffix = "";
        if (pv.isLocal && pv.isLeader) suffix = " (you, leader)";
        else if (pv.isLocal) suffix = " (you)";
        else if (pv.isLeader) suffix = " (leader)";
        snprintf(label, sizeof(label), "%s%s", pv.name, suffix);

        /* Color: leader=gold, local=green, other=white */
        if (pv.isLeader) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", label);
        } else if (pv.isLocal) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", label);
        } else {
            ImGui::Text("%s", label);
        }

        /* Character name on second line */
        if (pv.bodynum < (u8)mpGetNumBodies()) {
            const char *bodyName = mpGetBodyName(pv.bodynum);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), " [%s]", bodyName ? bodyName : "?");
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    ImGui::SameLine(0, pad);

    /* ---- Right column: Game settings (leader only) ---- */
    ImGui::BeginChild("##lobby_settings_col", ImVec2(rightW, contentH), true);

    bool isLeader = lobbyIsLocalLeader() != 0;

    if (isLeader) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Game Settings");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 0.9f), "Game Settings (leader controls)");
    }
    ImGui::Separator();

    if (!isLeader) {
        ImGui::BeginDisabled();
    }

    /* Game mode: these push the appropriate PD dialog for full setup */
    ImGui::Spacing();
    ImGui::Text("Start a game mode:");
    ImGui::Spacing();

    float btnW = rightW - ImGui::GetStyle().WindowPadding.x * 2;
    float btnH = 30.0f * scale;

    if (ImGui::Button("Combat Simulator", ImVec2(btnW, btnH))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* Push the Combat Simulator setup dialog.
         * This opens the full PD native setup flow (arena, weapons, etc.)
         * The leader configures everything, then starts the match. */
        menuhandlerMainMenuCombatSimulator(MENUOP_SET, NULL, NULL);
    }

    ImGui::Spacing();

    if (ImGui::Button("Co-op Campaign", ImVec2(btnW, btnH))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* Push co-op host dialog */
        extern struct menudialogdef g_NetCoopHostMenuDialog;
        menuPushDialog(&g_NetCoopHostMenuDialog);
    }

    ImGui::Spacing();

    if (ImGui::Button("Counter-Op", ImVec2(btnW, btnH))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* Counter-op uses the same co-op dialog but with different mode */
        extern struct menudialogdef g_NetCoopHostMenuDialog;
        menuPushDialog(&g_NetCoopHostMenuDialog);
    }

    if (!isLeader) {
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Character selection for the local player */
    ImGui::Text("Your Character:");
    ImGui::TextDisabled("(change in Combat Sim setup)");

    ImGui::EndChild();

    /* ---- Footer ---- */
    ImGui::Separator();
    if (isLeader) {
        ImGui::TextDisabled("You are the lobby leader. Choose a game mode to configure and start.");
    } else {
        ImGui::TextDisabled("Waiting for the lobby leader to start a game...");
    }

    /* B = disconnect */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        extern s32 netDisconnect(void);
        netDisconnect();
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMenuLobbyRegister(void)
{
    if (s_Registered) return;

    /* Register against the Combat Simulator dialog — when in a network
     * session, this replaces the PD native setup with our lobby screen. */
    pdguiHotswapRegister(
        &g_CombatSimulatorMenuDialog,
        renderNetworkLobby,
        "Network Lobby"
    );

    s_Registered = true;
    sysLogPrintf(LOG_NOTE, "pdgui_menu_lobby: Registered");
}

} /* extern "C" */
