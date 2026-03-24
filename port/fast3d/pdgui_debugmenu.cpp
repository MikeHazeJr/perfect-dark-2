/**
 * pdgui_debugmenu.cpp -- F12 debug menu for Perfect Dark 2.
 *
 * Renders a PD-styled debug overlay using ImGui with game-relative scaling.
 * All UI proportions are calculated relative to the game viewport so the menu
 * looks the same whether the window is 640x480 or 2560x1440.
 *
 * IMPORTANT: This file is C++ and must NOT include types.h (which #defines
 * bool as s32, breaking C++ bool). Instead, we forward-declare the specific
 * C symbols we need with extern "C" linkage.
 *
 * This file is auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

/* PD-authentic drawing functions */
#include "pdgui_style.h"

/* Logging */
#include "system.h"

/* -----------------------------------------------------------------------
 * Forward declarations for game/port symbols.
 *
 * We declare these manually instead of including the game headers because
 * types.h redefines bool as s32, which is incompatible with C++.
 * ----------------------------------------------------------------------- */

extern "C" {

/* --- net/net.h symbols --- */
#define PDGUI_NETMODE_NONE     0
#define PDGUI_NETMODE_SERVER   1
#define PDGUI_NETMODE_CLIENT   2

#define PDGUI_CLSTATE_DISCONNECTED 0
#define PDGUI_CLSTATE_CONNECTING   1
#define PDGUI_CLSTATE_AUTH         2
#define PDGUI_CLSTATE_LOBBY        3
#define PDGUI_CLSTATE_GAME         4

#define PDGUI_NETGAMEMODE_MP    0
#define PDGUI_NETGAMEMODE_COOP  1
#define PDGUI_NETGAMEMODE_ANTI  2

#define PDGUI_NET_DEFAULT_PORT  27100
#define PDGUI_NET_MAX_CLIENTS   8
#define PDGUI_NET_MAX_NAME      32

/* --- constants.h symbols --- */
#define PDGUI_STAGE_MP_COMPLEX  0x1f

/* Minimal netclient struct -- just enough to read state and name.
 * The full struct has many more fields, but we only peek at these. */
struct pdgui_netclient_peek {
    void *peer;
    u32 id;
    u32 state;
    u32 flags;
    struct {
        char name[PDGUI_NET_MAX_NAME];
    } settings;
};

extern s32 g_NetMode;
extern s32 g_NetDedicated;
extern u8 g_NetGameMode;
extern u32 g_NetTick;
extern u32 g_NetServerPort;
extern s32 g_NetMaxClients;
extern s32 g_NetNumClients;

/* We read g_NetLocalClient through a peek struct -- this is safe because
 * we only access the initial fields which have identical layout to the
 * real struct netclient. The void* declaration links to the same symbol
 * (C linkage is name-only, no type mangling). */
extern void *g_NetLocalClient; /* actually struct netclient* */

extern s32 g_StageNum;
extern s32 g_OsMemSizeMb;

/* Function declarations */
s32 netStartServer(u16 port, s32 maxclients);
s32 netDisconnect(void);
void netServerStageStart(void);
void netServerStageEnd(void);
void mainChangeToStage(s32 stagenum);
void mpEndMatch(void);

/* Persistent memory diagnostics */
void *mempPCAlloc(u32 size, const char *tag);
s32 mempPCValidate(const char *context);
void mempPCFreeAll(void);
u32 mempPCGetTotalAllocated(void);
u32 mempPCGetNumAllocations(void);

} /* extern "C" */

/* -----------------------------------------------------------------------
 * Safe accessors for netclient data.
 * We need the actual struct size to index into the array properly.
 * Rather than hardcoding the struct size (which would be fragile),
 * we provide a C helper function that the game code compiles.
 * For now, we read state from g_NetLocalClient directly.
 * ----------------------------------------------------------------------- */

static u32 pdguiGetLocalClientState(void)
{
    if (!g_NetLocalClient) return PDGUI_CLSTATE_DISCONNECTED;
    /* The state field is at offset 8 (after peer pointer + id u32).
     * On 64-bit: peer=8 bytes, id=4 bytes, so state is at offset 12.
     * On 32-bit: peer=4 bytes, id=4 bytes, so state is at offset 8.
     * Use the peek struct which has matching layout. */
    const struct pdgui_netclient_peek *cl =
        (const struct pdgui_netclient_peek *)g_NetLocalClient;
    return cl->state;
}

/* -----------------------------------------------------------------------
 * Scaling
 *
 * PD's native resolution is 320x220 (NTSC). We want ImGui elements to
 * occupy the same *proportion* of the screen regardless of window size.
 * Scale factor = windowHeight / referenceHeight (480 = comfortable base
 * for the 16px font loaded in pdguiInit).
 * ----------------------------------------------------------------------- */

static float s_DebugScale = 1.0f;

static void pdguiDebugUpdateScale(s32 winW, s32 winH)
{
    (void)winW;
    const float refH = 480.0f;
    s_DebugScale = (float)winH / refH;
    if (s_DebugScale < 0.5f) s_DebugScale = 0.5f;
    if (s_DebugScale > 4.0f) s_DebugScale = 4.0f;
}

/* Scaled pixel value */
static inline float S(float px) { return px * s_DebugScale; }

/* -----------------------------------------------------------------------
 * State label helpers
 * ----------------------------------------------------------------------- */

static const char *netModeStr(s32 mode)
{
    switch (mode) {
        case PDGUI_NETMODE_NONE:   return "Disconnected";
        case PDGUI_NETMODE_SERVER: return "Server";
        case PDGUI_NETMODE_CLIENT: return "Client";
        default:                   return "Unknown";
    }
}

static const char *clientStateStr(u32 state)
{
    switch (state) {
        case PDGUI_CLSTATE_DISCONNECTED: return "Disconnected";
        case PDGUI_CLSTATE_CONNECTING:   return "Connecting";
        case PDGUI_CLSTATE_AUTH:         return "Authenticating";
        case PDGUI_CLSTATE_LOBBY:        return "In Lobby";
        case PDGUI_CLSTATE_GAME:         return "In Game";
        default:                         return "Unknown";
    }
}

static const char *gameModeStr(u8 mode)
{
    switch (mode) {
        case PDGUI_NETGAMEMODE_MP:   return "Combat Simulator";
        case PDGUI_NETGAMEMODE_COOP: return "Co-op";
        case PDGUI_NETGAMEMODE_ANTI: return "Counter-op";
        default:                     return "Unknown";
    }
}

/* -----------------------------------------------------------------------
 * Network section
 * ----------------------------------------------------------------------- */

static void pdguiDebugNetworkSection(void)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("NETWORK");
    ImGui::PopStyleColor();

    ImGui::Separator();

    /* Status info */
    ImGui::Text("Mode:    %s", netModeStr(g_NetMode));

    if (g_NetMode != PDGUI_NETMODE_NONE) {
        ImGui::Text("Clients: %d / %d", g_NetNumClients, g_NetMaxClients);
        ImGui::Text("Tick:    %u", g_NetTick);
        ImGui::Text("Game:    %s", gameModeStr(g_NetGameMode));
        ImGui::Text("Port:    %u", g_NetServerPort);

        u32 localState = pdguiGetLocalClientState();
        ImGui::Text("State:   %s", clientStateStr(localState));
    }

    ImGui::Spacing();

    /* Controls */
    bool isServer = (g_NetMode == PDGUI_NETMODE_SERVER);
    bool isConnected = (g_NetMode != PDGUI_NETMODE_NONE);
    u32 localState2 = pdguiGetLocalClientState();
    bool inLobby = isConnected && (localState2 == PDGUI_CLSTATE_LOBBY);
    bool inGame = isConnected && (localState2 == PDGUI_CLSTATE_GAME);

    if (!isConnected) {
        /* Debug-only: start a local server inside the game client.
         * In the dedicated-server-only model, the game client normally
         * never hosts. This is kept for development/testing only. */
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 0.8f), "(dev only)");
        if (ImGui::Button("Debug: Local Server", ImVec2(S(160), S(24)))) {
            s32 result = netStartServer(PDGUI_NET_DEFAULT_PORT, PDGUI_NET_MAX_CLIENTS);
            if (result == 0) {
                sysLogPrintf(LOG_NOTE, "DEBUG_MENU: started local server on port %u",
                    PDGUI_NET_DEFAULT_PORT);
            } else {
                sysLogPrintf(LOG_WARNING, "DEBUG_MENU: netStartServer failed (%d)", result);
            }
        }
    }

    /* Start/End Match — only available when running as server
     * (either dedicated server process or debug local server) */
    if (isServer && inLobby) {
        if (ImGui::Button("Start Match", ImVec2(S(160), S(24)))) {
            sysLogPrintf(LOG_NOTE, "DEBUG_MENU: starting match on Complex");
            mainChangeToStage(PDGUI_STAGE_MP_COMPLEX);
            netServerStageStart();
        }
    }

    if (isServer && inGame) {
        if (ImGui::Button("End Match", ImVec2(S(160), S(24)))) {
            sysLogPrintf(LOG_NOTE, "DEBUG_MENU: ending match");
            netServerStageEnd();
            mpEndMatch();
        }
    }

    /* Show warning when debug local server is active */
    if (isServer && !g_NetDedicated) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "DEBUG SERVER ACTIVE");
    }

    if (isConnected) {
        ImGui::Spacing();
        /* Disconnect -- red-tinted button */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.05f, 0.05f, 0.50f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.1f, 0.1f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.15f, 0.15f, 0.70f));
        if (ImGui::Button("Disconnect", ImVec2(S(160), S(24)))) {
            sysLogPrintf(LOG_NOTE, "DEBUG_MENU: disconnecting");
            netDisconnect();
        }
        ImGui::PopStyleColor(3);
    }
}

/* -----------------------------------------------------------------------
 * Memory section
 * ----------------------------------------------------------------------- */

static void pdguiDebugMemorySection(void)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("MEMORY");
    ImGui::PopStyleColor();

    ImGui::Separator();

    u32 pcTotal = mempPCGetTotalAllocated();
    u32 pcCount = mempPCGetNumAllocations();

    ImGui::Text("Persistent: %u bytes (%u allocs)", pcTotal, pcCount);
    ImGui::Text("Heap size:  %d MB", g_OsMemSizeMb);

    ImGui::Spacing();
    if (ImGui::Button("Validate Memory", ImVec2(S(160), S(24)))) {
        s32 ok = mempPCValidate("debug_menu");
        sysLogPrintf(LOG_NOTE, "DEBUG_MENU: mempPCValidate = %s", ok ? "OK" : "CORRUPTED");
    }
}

/* -----------------------------------------------------------------------
 * Theme section -- runtime palette switcher
 * ----------------------------------------------------------------------- */

static const char *s_ThemeNames[] = {
    "Grey", "Blue", "Red", "Green", "White", "Silver", "Black & Gold"
};
static const int s_ThemeIndices[] = {
    0, 1, 2, 3, 4, 5, 6
};
#define PDGUI_NUM_THEMES 7

static void pdguiDebugThemeSection(void)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("THEME");
    ImGui::PopStyleColor();

    ImGui::Separator();

    s32 currentPal = pdguiGetPalette();

    for (int i = 0; i < PDGUI_NUM_THEMES; i++) {
        bool selected = (s_ThemeIndices[i] == currentPal);
        if (selected) {
            /* Use the current theme's hover color to highlight the active palette button
             * instead of hardcoding blue, so it looks right on every theme */
            ImVec4 activeCol = ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered];
            activeCol.w = 0.85f;
            ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
        }

        if (ImGui::Button(s_ThemeNames[i], ImVec2(S(100), S(20)))) {
            pdguiSetPalette(s_ThemeIndices[i]);
            sysLogPrintf(LOG_NOTE, "DEBUG_MENU: palette -> %s (%d)",
                s_ThemeNames[i], s_ThemeIndices[i]);
        }

        if (selected) {
            ImGui::PopStyleColor();
        }

        /* 2 buttons per row */
        if (i % 2 == 0 && i + 1 < PDGUI_NUM_THEMES) {
            ImGui::SameLine();
        }
    }
}

/* -----------------------------------------------------------------------
 * Log Filters section -- toggle log channels on/off
 * ----------------------------------------------------------------------- */

static void pdguiDebugLogSection(void)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("LOG FILTERS");
    ImGui::PopStyleColor();

    ImGui::Separator();

    u32 mask = sysLogGetChannelMask();

    /* Preset buttons: All / None */
    bool isAll = (mask == LOG_CH_ALL);
    bool isNone = (mask == LOG_CH_NONE);

    if (isAll) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
    }
    if (ImGui::Button("All", ImVec2(S(60), S(20)))) {
        sysLogSetChannelMask(LOG_CH_ALL);
        mask = LOG_CH_ALL;
    }
    if (isAll) ImGui::PopStyleColor();

    ImGui::SameLine();

    if (isNone) {
        ImVec4 redBg = ImVec4(0.6f, 0.1f, 0.1f, 0.85f);
        ImGui::PushStyleColor(ImGuiCol_Button, redBg);
    }
    if (ImGui::Button("None", ImVec2(S(60), S(20)))) {
        sysLogSetChannelMask(LOG_CH_NONE);
        mask = LOG_CH_NONE;
    }
    if (isNone) ImGui::PopStyleColor();

    ImGui::Spacing();

    /* Individual channel toggles */
    bool changed = false;
    for (int i = 0; i < LOG_CH_COUNT; i++) {
        bool enabled = (mask & sysLogChannelBits[i]) != 0;
        if (ImGui::Checkbox(sysLogChannelNames[i], &enabled)) {
            if (enabled) {
                mask |= sysLogChannelBits[i];
            } else {
                mask &= ~sysLogChannelBits[i];
            }
            changed = true;
        }
    }

    if (changed) {
        sysLogSetChannelMask(mask);
    }

    ImGui::Spacing();

    /* Verbose toggle */
    bool verbose = sysLogGetVerbose() != 0;
    if (ImGui::Checkbox("Verbose", &verbose)) {
        sysLogSetVerbose(verbose ? 1 : 0);
    }

    /* Show current mask value for reference */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::Text("Mask: 0x%04X%s", mask, verbose ? " +V" : "");
    ImGui::PopStyleColor();
}

/* -----------------------------------------------------------------------
 * Frame/performance section
 * ----------------------------------------------------------------------- */

static void pdguiDebugPerfSection(void)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("PERFORMANCE");
    ImGui::PopStyleColor();

    ImGui::Separator();

    ImGuiIO &io = ImGui::GetIO();
    ImGui::Text("FPS:   %.1f", io.Framerate);
    ImGui::Text("Frame: %.2f ms", 1000.0f / io.Framerate);
    ImGui::Text("Stage: 0x%02x", g_StageNum);
}

/* -----------------------------------------------------------------------
 * Main debug menu render entry point
 * ----------------------------------------------------------------------- */

extern "C" void pdguiDebugMenuRender(s32 winW, s32 winH)
{
    pdguiDebugUpdateScale(winW, winH);

    /* Apply scaling to font and metrics for this frame */
    ImGuiIO &io = ImGui::GetIO();
    io.FontGlobalScale = s_DebugScale;

    ImGuiStyle &style = ImGui::GetStyle();

    /* Scale metrics proportionally. We save/restore the originals so
     * pdguiApplyPdStyle's base values remain canonical. */
    ImVec2 origWinPad = style.WindowPadding;
    ImVec2 origFrmPad = style.FramePadding;
    ImVec2 origItmSpc = style.ItemSpacing;

    style.WindowPadding  = ImVec2(S(8), S(8));
    style.FramePadding   = ImVec2(S(6), S(3));
    style.ItemSpacing    = ImVec2(S(8), S(4));

    /* Position: right side of screen, slight inset from edge */
    float menuW = S(220);
    float margin = S(12);
    ImGui::SetNextWindowPos(ImVec2((float)winW - menuW - margin, margin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(menuW, S(100)),
                                        ImVec2(menuW, (float)winH - margin * 2));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize;

    if (ImGui::Begin("Perfect Dark 2 Debug", nullptr, flags)) {
        pdguiDebugPerfSection();
        ImGui::Spacing();
        ImGui::Spacing();
        pdguiDebugNetworkSection();
        ImGui::Spacing();
        ImGui::Spacing();
        pdguiDebugMemorySection();
        ImGui::Spacing();
        ImGui::Spacing();
        pdguiDebugThemeSection();
        ImGui::Spacing();
        ImGui::Spacing();
        pdguiDebugLogSection();
    }
    ImGui::End();

    /* Restore original metrics */
    style.WindowPadding = origWinPad;
    style.FramePadding  = origFrmPad;
    style.ItemSpacing   = origItmSpc;

    /* Reset font scale for any other ImGui rendering that might follow */
    io.FontGlobalScale = 1.0f;
}
