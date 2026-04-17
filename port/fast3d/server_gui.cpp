/**
 * server_gui.cpp -- R-5: Dedicated server ImGui GUI.
 *
 * Layout:
 *   Top:    Status bar — uptime, player count, tick rate, memory, connect code
 *   Middle: Tabbed panel
 *             [Players]  — name, ping, state, team, kick/ban
 *             [Rooms]    — player count, map, mode, state
 *             [Operator] — start/end match, change map/mode, restart/shutdown
 *             [Updates*] — version list + download
 *   Bottom: Log panel — severity coloring + category filter
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#include "glad/glad.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_opengl3.h"

extern "C" {

#include "system.h"
#include "connectcode.h"
#include "hub.h"
#include "room.h"
#include "updater.h"
#include "updateversion.h"

/* Net state */
extern s32  g_NetMode;
extern s32  g_NetDedicated;
extern s32  g_NetNumClients;
extern s32  g_NetMaxClients;
extern u32  g_NetServerPort;
extern u8   g_NetGameMode;
extern u32  g_NetTick;
extern const char *netUpnpGetExternalIP(void);
extern s32  netUpnpIsActive(void);
extern s32  netUpnpGetStatus(void);
extern s32  stunGetStatus(void);
extern const char *stunGetExternalIP(void);

/* Net functions */
extern void netServerStageStart(void);
extern void netServerStageEnd(void);
extern void netServerKickClient(s32 clientId, const char *reason);
extern void netServerBanClient(s32 clientId, const char *reason);
extern s32  netDisconnect(void);

/* Log ring buffer */
extern s32 sysLogRingGetCount(void);
extern const char *sysLogRingGetLine(s32 idx);

/* Lobby state */
extern s32 lobbyGetPlayerCount(void);

/* Match setup (server-side control) */
extern const char *serverGetStageId(void);
extern void        serverSetStageId(const char *stage_id);
extern u8          serverGetScenario(void);
extern void        serverSetScenario(u8 scenario);
extern s32         serverGetMemoryMB(void);

#define NETMODE_NONE   0
#define NETMODE_SERVER 1

#define CLSTATE_DISCONNECTED 0
#define CLSTATE_CONNECTING   1
#define CLSTATE_AUTH         2
#define CLSTATE_LOBBY        3
#define CLSTATE_GAME         4

#define GAMEMODE_MP   0
#define GAMEMODE_COOP 1
#define GAMEMODE_ANTI 2

#define UPNP_STATUS_IDLE    0
#define UPNP_STATUS_WORKING 1
#define UPNP_STATUS_SUCCESS 2
#define UPNP_STATUS_FAILED  3

#define STUN_STATUS_IDLE    0
#define STUN_STATUS_WORKING 1
#define STUN_STATUS_SUCCESS 2
#define STUN_STATUS_FAILED  3

/* Lobby player view — layout must match server_bridge.c lobbyGetPlayerInfo writes */
struct lobbyplayer_view {
    u8   active;     /* offset 0 */
    u8   isLeader;   /* offset 1 */
    u8   isReady;    /* offset 2 */
    u8   headnum;    /* offset 3 */
    u8   bodynum;    /* offset 4 */
    u8   team;       /* offset 5 */
    char name[32];   /* offset 6, matches LOBBY_NAME_LEN */
    s32  isLocal;    /* offset 40 (aligned) */
    s32  state;      /* offset 44 */
    u8   clientId;   /* offset 48 */
};

extern s32 lobbyGetPlayerInfo(s32 idx, struct lobbyplayer_view *out);
extern u32 netGetClientPing(s32 clientId);

} /* extern "C" */

/* ========================================================================
 * Static state
 * ======================================================================== */

static bool   s_Initialized = false;
static Uint32 s_SrvStartMs  = 0;

/* Update tab */
static bool s_SrvDownloadActive     = false;
static bool s_SrvRestartPending     = false;
static bool s_SrvDownloadFailed     = false;
static int  s_SrvDownloadingIndex   = -1;
static int  s_SrvStagedReleaseIndex = -1;

/* Log panel */
enum {
    LOG_FILTER_ALL = 0,
    LOG_FILTER_NET,
    LOG_FILTER_ERROR,
    LOG_FILTER_WARN,
    LOG_FILTER_CHAT,
    LOG_FILTER_HUB,
    LOG_FILTER_COUNT
};
static int  s_LogFilter     = LOG_FILTER_ALL;
static bool s_LogAutoScroll = true;

/* Operator tab — stage ID edit buffer */
static char s_StageIdBuf[64] = {};
static bool s_StageIdDirty   = false;

/* ========================================================================
 * Helpers
 * ======================================================================== */

static const char *gameModeStr(u8 mode)
{
    switch (mode) {
        case GAMEMODE_MP:   return "Combat Simulator";
        case GAMEMODE_COOP: return "Co-op Campaign";
        case GAMEMODE_ANTI: return "Counter-Operative";
        default:            return "Unknown";
    }
}

static const char *clientStateStr(s32 state)
{
    switch (state) {
        case CLSTATE_DISCONNECTED: return "Disconnected";
        case CLSTATE_CONNECTING:   return "Connecting";
        case CLSTATE_AUTH:         return "Auth";
        case CLSTATE_LOBBY:        return "Lobby";
        case CLSTATE_GAME:         return "In Game";
        default:                   return "?";
    }
}

static ImVec4 clientStateColor(s32 state)
{
    if (state == CLSTATE_LOBBY) return ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
    if (state == CLSTATE_GAME)  return ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
    if (state <= CLSTATE_AUTH)  return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
}

static ImVec4 pingColor(u32 ping)
{
    if (ping > 200) return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    if (ping > 100) return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    return ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
}

static ImVec4 teamColor(u8 team)
{
    switch (team) {
        case 1:  return ImVec4(1.0f, 0.40f, 0.40f, 1.0f);
        case 2:  return ImVec4(0.30f, 0.55f, 1.0f,  1.0f);
        case 3:  return ImVec4(0.30f, 1.0f,  0.45f, 1.0f);
        case 4:  return ImVec4(1.0f, 0.90f,  0.30f, 1.0f);
        default: return ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
    }
}

static const char *teamName(u8 team)
{
    switch (team) {
        case 1:  return "Skedar";
        case 2:  return "dataDyne";
        case 3:  return "Carrington";
        case 4:  return "NSA";
        default: return "--";
    }
}

static bool logLineMatchesFilter(const char *line, int filter)
{
    switch (filter) {
        case LOG_FILTER_ALL:   return true;
        case LOG_FILTER_NET:   return strncmp(line, "NET:", 4) == 0;
        case LOG_FILTER_ERROR: return strncmp(line, "ERROR:", 6) == 0;
        case LOG_FILTER_WARN:  return strncmp(line, "WARNING:", 8) == 0;
        case LOG_FILTER_CHAT:  return strncmp(line, "CHAT:", 5) == 0;
        case LOG_FILTER_HUB:   return strncmp(line, "HUB", 3) == 0;
        default:               return true;
    }
}

static ImVec4 logLineColor(const char *line)
{
    if (strncmp(line, "ERROR:", 6) == 0)   return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    if (strncmp(line, "WARNING:", 8) == 0) return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    if (strncmp(line, "CHAT:", 5) == 0)    return ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
    if (strncmp(line, "NET:", 4) == 0)     return ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
    if (strncmp(line, "UPNP:", 5) == 0)    return ImVec4(0.6f, 0.9f, 0.6f, 1.0f);
    if (strncmp(line, "LOBBY:", 6) == 0)   return ImVec4(0.8f, 0.8f, 0.4f, 1.0f);
    if (strncmp(line, "HUB", 3) == 0)      return ImVec4(0.7f, 0.5f, 1.0f, 1.0f);
    return ImVec4(0.7f, 0.7f, 0.8f, 0.9f);
}

/* ========================================================================
 * Init / Event / Shutdown
 * ======================================================================== */

extern "C" s32 serverGuiInit(SDL_Window *window, void *glContext)
{
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        printf("SERVER GUI: GLAD failed to load\n");
        return -1;
    }

    sysLogPrintf(LOG_NOTE, "SERVER GUI: OpenGL %s", glGetString(GL_VERSION));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding  = 2.0f;
    style.GrabRounding   = 2.0f;

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    s_SrvStartMs  = SDL_GetTicks();
    s_Initialized = true;
    sysLogPrintf(LOG_NOTE, "SERVER GUI: Initialized");
    return 0;
}

extern "C" void serverGuiProcessEvent(SDL_Event *ev)
{
    if (s_Initialized) {
        ImGui_ImplSDL2_ProcessEvent(ev);
    }
}

/* ========================================================================
 * Tab: Updates (unchanged from prior design)
 * ======================================================================== */

static void drawTabUpdate(float panelW, float panelH)
{
    if (ImGui::BeginChild("##update_panel", ImVec2(panelW, panelH), false)) {
        const pdversion_t *cur = updaterGetCurrentVersion();
        char curstr[64];
        if (cur) {
            versionFormat(cur, curstr, sizeof(curstr));
        } else {
            snprintf(curstr, sizeof(curstr), "(unknown)");
        }
        ImGui::Text("Server version: %s", curstr);
        ImGui::SameLine(0, 16);

        update_channel_t channel = updaterGetChannel();
        const char *channelLabels[] = { "Stable", "Dev / Test" };
        ImGui::SetNextItemWidth(120);
        int channelInt = (int)channel;
        if (ImGui::Combo("Channel##srv_upd", &channelInt, channelLabels, 2)) {
            updaterSetChannel((update_channel_t)channelInt);
            updaterCheckAsync();
        }
        ImGui::Separator();

        updater_status_t status = updaterGetStatus();

        if (s_SrvDownloadActive) {
            if (status == UPDATER_DOWNLOAD_DONE) {
                s_SrvDownloadActive      = false;
                s_SrvRestartPending      = true;
                s_SrvStagedReleaseIndex  = s_SrvDownloadingIndex;
            } else if (status == UPDATER_DOWNLOAD_FAILED) {
                s_SrvDownloadActive      = false;
                s_SrvDownloadFailed      = true;
                s_SrvDownloadingIndex    = -1;
            }
        }

        switch (status) {
        case UPDATER_IDLE:
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Not checked yet");
            break;
        case UPDATER_CHECKING:
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1), "Checking for updates...");
            break;
        case UPDATER_CHECK_DONE: {
            s32 count = updaterGetReleaseCount();
            if (updaterIsUpdateAvailable()) {
                const updater_release_t *lat = updaterGetLatest();
                char latstr[64];
                versionFormat(&lat->version, latstr, sizeof(latstr));
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1),
                    "Update available: v%s  (%d versions found)", latstr, count);
            } else {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1),
                    "Up to date  (%d versions found)", count);
            }
            break;
        }
        case UPDATER_CHECK_FAILED:
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1),
                "Check failed: %s", updaterGetError());
            break;
        case UPDATER_DOWNLOADING:
            if (s_SrvDownloadActive) {
                updater_progress_t prog = updaterGetProgress();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1),
                    "Downloading... %.0f%%", (double)prog.percent);
            }
            break;
        case UPDATER_DOWNLOAD_DONE:
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1),
                "Download complete -- restart server to apply");
            break;
        case UPDATER_DOWNLOAD_FAILED:
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1),
                "Download failed: %s", updaterGetError());
            break;
        }

        ImGui::SameLine();
        if (status != UPDATER_CHECKING && status != UPDATER_DOWNLOADING) {
            if (ImGui::SmallButton("Check Now##srv")) {
                updaterCheckAsync();
            }
        }
        ImGui::Spacing();

        if (status == UPDATER_CHECK_DONE || status == UPDATER_DOWNLOAD_DONE ||
            status == UPDATER_DOWNLOAD_FAILED || s_SrvDownloadActive) {

            s32 count = updaterGetReleaseCount();
            float tableH = panelH
                - ImGui::GetCursorPosY()
                - ImGui::GetStyle().WindowPadding.y * 2.0f
                - 60.0f;
            if (tableH < 60.0f) tableH = 60.0f;

            if (count > 0 && ImGui::BeginTable("srv_versions", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                ImVec2(0, tableH))) {

                ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Title",   ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Size",    ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableHeadersRow();

                for (s32 i = 0; i < count; i++) {
                    const updater_release_t *rel = updaterGetRelease(i);
                    if (!rel) continue;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    char verstr[64];
                    versionFormat(&rel->version, verstr, sizeof(verstr));

                    bool isCurrent = cur && (versionCompare(&rel->version, cur) == 0);
                    bool isNewer   = cur && (versionCompare(&rel->version, cur) > 0);

                    ImGui::TextUnformatted(verstr);
                    if (isCurrent) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "(cur)");
                    } else if (isNewer) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "*");
                    }

                    ImGui::TableNextColumn();
                    if (rel->isPrerelease) {
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Dev");
                    } else {
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Stable");
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(rel->name[0] ? rel->name : "(no title)");

                    ImGui::TableNextColumn();
                    if (rel->assetSize > 0) {
                        ImGui::Text("%.1f MB", (double)rel->assetSize / (1024.0 * 1024.0));
                    } else {
                        ImGui::TextDisabled("--");
                    }

                    ImGui::TableNextColumn();
                    ImGui::PushID(i);

                    bool isDownloading = (s_SrvDownloadingIndex == i) && s_SrvDownloadActive;
                    bool isStaged      = (s_SrvStagedReleaseIndex == i);
                    bool canDownload   = !isCurrent && !s_SrvDownloadActive && rel->assetUrl[0];

                    if (isCurrent) {
                        /* no action */
                    } else if (isDownloading) {
                        updater_progress_t p = updaterGetProgress();
                        ImGui::Text("%.0f%%", (double)p.percent);
                    } else if (isStaged) {
                        if (ImGui::SmallButton("Switch")) {
                            s_SrvRestartPending = true;
                        }
                    } else if (canDownload) {
                        if (ImGui::SmallButton("Download")) {
                            s_SrvDownloadFailed   = false;
                            s_SrvDownloadingIndex = i;
                            updaterDownloadAsync(rel);
                            s_SrvDownloadActive   = true;
                        }
                    } else if (s_SrvDownloadActive) {
                        ImGui::TextDisabled("...");
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (s_SrvDownloadFailed) {
                const char *errMsg = updaterGetError();
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1),
                    "Download failed: %s",
                    (errMsg && errMsg[0]) ? errMsg : "unknown error");
            }
        }

        if (s_SrvRestartPending) {
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                "Update downloaded. Restart server to apply.");
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.55f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.25f, 1.0f));
            if (ImGui::Button("Restart & Update", ImVec2(160, 0))) {
#ifdef _WIN32
                {
                    STARTUPINFOA si;
                    PROCESS_INFORMATION pi;
                    memset(&si, 0, sizeof(si));
                    memset(&pi, 0, sizeof(pi));
                    si.cb = sizeof(si);
                    char exePath[512];
                    GetModuleFileNameA(NULL, exePath, sizeof(exePath));
                    if (CreateProcessA(exePath, GetCommandLineA(),
                            NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                }
#endif
                SDL_Event quitEvent;
                quitEvent.type = SDL_QUIT;
                SDL_PushEvent(&quitEvent);
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0, 16);
            if (ImGui::Button("Later##srv", ImVec2(80, 0))) {
                s_SrvRestartPending = false;
            }
        }
    }
    ImGui::EndChild();
}

/* ========================================================================
 * Tab: Players — name, ping, state, team, kick/ban
 * ======================================================================== */

static void drawTabPlayers(float panelW, float panelH)
{
    if (!ImGui::BeginChild("##players_outer", ImVec2(panelW, panelH), false))
    {
        ImGui::EndChild();
        return;
    }

    s32 playerCount = lobbyGetPlayerCount();
    if (playerCount == 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("No players connected.");
        ImGui::EndChild();
        return;
    }

    float tableH = panelH - ImGui::GetStyle().WindowPadding.y * 2.0f;
    if (tableH < 40.0f) tableH = 40.0f;

    if (ImGui::BeginTable("##player_table", 6,
        ImGuiTableFlags_Borders     |
        ImGuiTableFlags_RowBg       |
        ImGuiTableFlags_ScrollY     |
        ImGuiTableFlags_SizingFixedFit,
        ImVec2(0, tableH)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Agent Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("State",      ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Ping",       ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Team",       ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Kick",       ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Ban",        ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableHeadersRow();

        for (s32 i = 0; i < playerCount; i++) {
            struct lobbyplayer_view pv;
            memset(&pv, 0, sizeof(pv));
            if (!lobbyGetPlayerInfo(i, &pv)) continue;
            if (!pv.active) continue;

            ImGui::TableNextRow();
            ImGui::PushID(i);

            /* Name */
            ImGui::TableNextColumn();
            if (pv.isLeader) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", pv.name);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Room leader");
            } else {
                ImGui::TextUnformatted(pv.name);
            }

            /* State */
            ImGui::TableNextColumn();
            ImGui::TextColored(clientStateColor(pv.state), "%s", clientStateStr(pv.state));

            /* Ping */
            ImGui::TableNextColumn();
            u32 ping = netGetClientPing((s32)pv.clientId);
            if (ping > 0) {
                ImGui::TextColored(pingColor(ping), "%u ms", ping);
            } else {
                ImGui::TextDisabled("--");
            }

            /* Team */
            ImGui::TableNextColumn();
            if (pv.team > 0) {
                ImGui::TextColored(teamColor(pv.team), "%s", teamName(pv.team));
            } else {
                ImGui::TextDisabled("--");
            }

            /* Kick */
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.1f, 0.1f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.15f, 0.15f, 0.8f));
            if (ImGui::SmallButton("Kick")) {
                sysLogPrintf(LOG_NOTE, "SERVER GUI: kicking player %d (%s)",
                             (s32)pv.clientId, pv.name);
                netServerKickClient((s32)pv.clientId, "Kicked by server operator");
            }
            ImGui::PopStyleColor(2);

            /* Ban */
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.05f, 0.3f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.1f, 0.4f, 0.8f));
            if (ImGui::SmallButton("Ban")) {
                sysLogPrintf(LOG_NOTE, "SERVER GUI: banning player %d (%s)",
                             (s32)pv.clientId, pv.name);
                netServerBanClient((s32)pv.clientId, "Banned by server operator");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Kick + log (IP block not yet implemented)");
            ImGui::PopStyleColor(2);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
}

/* ========================================================================
 * Tab: Rooms — player count, map, mode, state
 * ======================================================================== */

static void drawTabRooms(float panelW, float panelH)
{
    if (!ImGui::BeginChild("##rooms_outer", ImVec2(panelW, panelH), false))
    {
        ImGui::EndChild();
        return;
    }

    /* Hub state summary */
    hub_state_t hubState = hubGetState();
    ImVec4 hubColor = (hubState == HUB_STATE_ACTIVE)
        ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
        : ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Hub:");
    ImGui::SameLine();
    ImGui::TextColored(hubColor, "%s", hubGetStateName(hubState));
    ImGui::SameLine(0, 20);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Slots:");
    ImGui::SameLine();
    ImGui::Text("%d / %d", hubGetUsedSlots(), hubGetMaxSlots());
    ImGui::Separator();
    ImGui::Spacing();

    s32 roomCount = roomGetActiveCount();
    if (roomCount == 0) {
        ImGui::TextDisabled("No active rooms.");
        ImGui::EndChild();
        return;
    }

    float tableH = panelH
        - ImGui::GetCursorPosY()
        - ImGui::GetStyle().WindowPadding.y * 2.0f;
    if (tableH < 40.0f) tableH = 40.0f;

    if (ImGui::BeginTable("##room_table", 6,
        ImGuiTableFlags_Borders     |
        ImGuiTableFlags_RowBg       |
        ImGuiTableFlags_ScrollY     |
        ImGuiTableFlags_SizingFixedFit,
        ImVec2(0, tableH)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID",      ImGuiTableColumnFlags_WidthFixed,   28);
        ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed,   64);
        ImGui::TableSetupColumn("State",   ImGuiTableColumnFlags_WidthFixed,   80);
        ImGui::TableSetupColumn("Stage",   ImGuiTableColumnFlags_WidthFixed,   56);
        ImGui::TableSetupColumn("Mode",    ImGuiTableColumnFlags_WidthFixed,   48);
        ImGui::TableHeadersRow();

        for (s32 i = 0; i < roomCount; i++) {
            hub_room_t *r = roomGetByIndex(i);
            if (!r) continue;

            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            ImGui::Text("%u", (unsigned)r->id);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r->name[0] ? r->name : "(unnamed)");

            ImGui::TableNextColumn();
            ImGui::Text("%u / %u", (unsigned)r->client_count, (unsigned)r->max_players);

            ImGui::TableNextColumn();
            ImVec4 stateCol = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            switch (r->state) {
                case ROOM_STATE_LOBBY:     stateCol = ImVec4(0.5f, 0.8f, 0.5f, 1.0f); break;
                case ROOM_STATE_PREPARING: stateCol = ImVec4(1.0f, 0.9f, 0.3f, 1.0f); break;
                case ROOM_STATE_LOADING:   stateCol = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
                case ROOM_STATE_MATCH:     stateCol = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); break;
                case ROOM_STATE_POSTGAME:  stateCol = ImVec4(0.8f, 0.5f, 0.9f, 1.0f); break;
                default: break;
            }
            ImGui::TextColored(stateCol, "%s", roomStateName(r->state));

            ImGui::TableNextColumn();
            if (r->stagenum > 0) {
                ImGui::Text("0x%02X", (unsigned)r->stagenum);
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            ImGui::Text("%u", (unsigned)r->scenario);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
}

/* ========================================================================
 * Tab: Operator — start/end match, change map/mode, restart
 * ======================================================================== */

static void drawTabOperator(float panelW, float panelH)
{
    if (!ImGui::BeginChild("##operator_outer", ImVec2(panelW, panelH), false))
    {
        ImGui::EndChild();
        return;
    }

    float halfW     = panelW * 0.55f;
    float ctrlW     = panelW - halfW
                      - ImGui::GetStyle().WindowPadding.x * 2.0f
                      - ImGui::GetStyle().ItemSpacing.x;
    float innerH    = panelH - ImGui::GetStyle().WindowPadding.y * 2.0f;

    /* === Match Control (left) === */
    if (ImGui::BeginChild("##op_match", ImVec2(halfW, innerH), true)) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Match Control");
        ImGui::Separator();
        ImGui::Spacing();

        /* Game mode */
        ImGui::Text("Game Mode:");
        const char *modes[] = { "Combat Simulator", "Co-op Campaign", "Counter-Operative" };
        s32 currentMode = (s32)g_NetGameMode;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##gamemode", &currentMode, modes, 3)) {
            g_NetGameMode = (u8)currentMode;
            sysLogPrintf(LOG_NOTE, "SERVER GUI: game mode set to %s",
                         gameModeStr(g_NetGameMode));
        }

        ImGui::Spacing();

        /* Stage ID */
        ImGui::Text("Stage ID:");
        const char *curStageId = serverGetStageId();
        if (!s_StageIdDirty) {
            strncpy(s_StageIdBuf, curStageId, sizeof(s_StageIdBuf) - 1);
            s_StageIdBuf[sizeof(s_StageIdBuf) - 1] = '\0';
        }
        /* Reserve space for the Apply button when editing; fill when idle */
        ImGui::SetNextItemWidth(s_StageIdDirty ? -60.0f : -1.0f);
        if (ImGui::InputText("##stageid", s_StageIdBuf, sizeof(s_StageIdBuf))) {
            s_StageIdDirty = true;
        }
        if (s_StageIdDirty) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Apply##stageid")) {
                serverSetStageId(s_StageIdBuf);
                s_StageIdDirty = false;
                sysLogPrintf(LOG_NOTE, "SERVER GUI: stage ID set to '%s'", s_StageIdBuf);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Catalog stage ID (e.g. map.datadyne-1)");
        }

        /* Scenario */
        ImGui::Spacing();
        ImGui::Text("Scenario:");
        int scenInt = (int)serverGetScenario();
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("##scenario", &scenInt, 1, 1)) {
            if (scenInt < 0) scenInt = 0;
            if (scenInt > 255) scenInt = 255;
            serverSetScenario((u8)scenInt);
            sysLogPrintf(LOG_NOTE, "SERVER GUI: scenario set to %d", scenInt);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* Start / End match */
        bool hasPlayers = (g_NetNumClients > 0) && (roomGetActiveCount() > 0);
        if (!hasPlayers) ImGui::BeginDisabled();
        if (ImGui::Button("Force Start Match", ImVec2(-1, 30))) {
            sysLogPrintf(LOG_NOTE, "SERVER GUI: force starting match");
            netServerStageStart();
        }
        if (!hasPlayers) ImGui::EndDisabled();
        ImGui::Spacing();
        if (ImGui::Button("End Match", ImVec2(-1, 30))) {
            sysLogPrintf(LOG_NOTE, "SERVER GUI: ending match");
            netServerStageEnd();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* === Server Control (right) === */
    if (ImGui::BeginChild("##op_server", ImVec2(ctrlW, innerH), true)) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Server");
        ImGui::Separator();
        ImGui::Spacing();

        /* Restart & Update (only shown when update is staged) */
        if (s_SrvRestartPending) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.55f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.25f, 1.0f));
            if (ImGui::Button("Restart & Update", ImVec2(-1, 30))) {
#ifdef _WIN32
                {
                    STARTUPINFOA si;
                    PROCESS_INFORMATION pi;
                    memset(&si, 0, sizeof(si));
                    memset(&pi, 0, sizeof(pi));
                    si.cb = sizeof(si);
                    char exePath[512];
                    GetModuleFileNameA(NULL, exePath, sizeof(exePath));
                    if (CreateProcessA(exePath, GetCommandLineA(),
                            NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                }
#endif
                SDL_Event quitEvent;
                quitEvent.type = SDL_QUIT;
                SDL_PushEvent(&quitEvent);
            }
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.1f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.15f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.9f, 0.2f,  0.2f, 0.7f));
        if (ImGui::Button("Shutdown Server", ImVec2(-1, 30))) {
            sysLogPrintf(LOG_NOTE, "SERVER GUI: shutdown requested");
            SDL_Event quitEvent;
            quitEvent.type = SDL_QUIT;
            SDL_PushEvent(&quitEvent);
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

/* ========================================================================
 * Status bar (top panel)
 * ======================================================================== */

static void drawStatusBar(float fullW)
{
    Uint32 elapsedMs  = SDL_GetTicks() - s_SrvStartMs;
    u32    uptimeSecs = elapsedMs / 1000;
    u32    hh         = uptimeSecs / 3600;
    u32    mm         = (uptimeSecs % 3600) / 60;
    u32    ss         = uptimeSecs % 60;
    float  tickHz     = (uptimeSecs > 0) ? (float)g_NetTick / (float)uptimeSecs : 0.0f;
    s32    memMB      = serverGetMemoryMB();

    const char *ip = "";
    if (netUpnpIsActive() && netUpnpGetExternalIP()[0]) {
        ip = netUpnpGetExternalIP();
    } else if (stunGetStatus() == STUN_STATUS_SUCCESS) {
        ip = stunGetExternalIP();
    }

    ImGui::Columns(4, nullptr, false);

    /* --- Col 1: Title + connect code --- */
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "PD2 DEDICATED SERVER");
    if (ip && ip[0]) {
        char connectCode[256];
        u32 ipAddr = 0;
        u32 a, b, c, d;
        if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            ipAddr = a | (b << 8) | (c << 16) | (d << 24);
        }
        connectCodeEncode(ipAddr, connectCode, sizeof(connectCode));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", connectCode);
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton("Copy##cc")) {
            SDL_SetClipboardText(connectCode);
        }
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Port %u", g_NetServerPort);
    } else {
        s32 upnpStatus = netUpnpGetStatus();
        s32 stunStatus = stunGetStatus();
        if (upnpStatus == UPNP_STATUS_WORKING || stunStatus == STUN_STATUS_WORKING) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f),
                "Port %u (discovering...)", g_NetServerPort);
        } else {
            ImGui::Text("Port %u (LAN only)", g_NetServerPort);
        }
    }

    /* --- Col 2: Players + Rooms --- */
    ImGui::NextColumn();
    s32 displayClients = g_NetDedicated
        ? g_NetNumClients
        : (g_NetNumClients > 0 ? g_NetNumClients - 1 : 0);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Players");
    ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "%d / %d",
                       displayClients, g_NetMaxClients);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Rooms");
    ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "%d active",
                       roomGetActiveCount());

    /* --- Col 3: Uptime + Tick rate --- */
    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Uptime");
    ImGui::Text("%02u:%02u:%02u", hh, mm, ss);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Tick");
    ImGui::Text("%.1f Hz", tickHz);

    /* --- Col 4: Memory + Status + Update badge --- */
    ImGui::NextColumn();
    if (memMB > 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Memory");
        ImGui::Text("%d MB", memMB);
    }
    if (g_NetMode == NETMODE_SERVER) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "ONLINE");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "OFFLINE");
    }
    if (updaterIsUpdateAvailable()) {
        const updater_release_t *lat = updaterGetLatest();
        if (lat) {
            char latstr[64];
            versionFormat(&lat->version, latstr, sizeof(latstr));
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Update: v%s", latstr);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open the Updates tab to download");
            }
        }
    } else if (s_SrvRestartPending) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Restart to apply");
    }

    ImGui::Columns(1);
}

/* ========================================================================
 * Log panel (bottom strip) — severity coloring + category filter
 * ======================================================================== */

static void drawLogPanel(float panelW, float panelH)
{
    static const char *filterLabels[LOG_FILTER_COUNT] = {
        "All", "NET", "ERROR", "WARN", "CHAT", "HUB"
    };

    /* Filter row */
    for (int i = 0; i < LOG_FILTER_COUNT; i++) {
        if (i > 0) ImGui::SameLine(0, 4);
        bool selected = (s_LogFilter == i);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.45f, 0.7f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.55f, 0.8f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        }
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "%s##lf%d", filterLabels[i], i);
        if (ImGui::SmallButton(btnId)) {
            s_LogFilter = i;
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine(0, 16);
    ImGui::Checkbox("Auto-scroll", &s_LogAutoScroll);

    float scrollH = panelH
        - ImGui::GetCursorPosY()
        - ImGui::GetStyle().WindowPadding.y;
    if (scrollH < 30.0f) scrollH = 30.0f;

    if (ImGui::BeginChild("##log_scroll", ImVec2(0, scrollH), false)) {
        s32 lineCount = sysLogRingGetCount();
        for (s32 i = 0; i < lineCount; i++) {
            const char *line = sysLogRingGetLine(i);
            if (!logLineMatchesFilter(line, s_LogFilter)) continue;
            ImGui::TextColored(logLineColor(line), "%s", line);
        }
        if (s_LogAutoScroll &&
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

/* ========================================================================
 * Frame render
 * ======================================================================== */

extern "C" void serverGuiFrame(SDL_Window *window)
{
    if (!s_Initialized) return;

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);

    glViewport(0, 0, winW, winH);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    updaterTick();

    /* Sync stage ID buffer when not actively editing */
    if (!s_StageIdDirty) {
        const char *sid = serverGetStageId();
        strncpy(s_StageIdBuf, sid, sizeof(s_StageIdBuf) - 1);
        s_StageIdBuf[sizeof(s_StageIdBuf) - 1] = '\0';
    }

    const float margin   = 10.0f;
    const float statusH  = 120.0f;
    const float tabBarH  = 28.0f;
    const float logMinH  = 160.0f;
    const float fullW    = (float)winW - margin * 2.0f;

    float available      = (float)winH - margin * 4.0f - statusH;
    float middleH        = available * 0.58f;
    if (middleH < 120.0f) middleH = 120.0f;
    float logH           = available - middleH;
    if (logH < logMinH) { logH = logMinH; middleH = available - logH; }

    float middleTop      = margin + statusH + margin;
    float logTop         = middleTop + middleH + margin;

    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove
                                | ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_NoCollapse;

    /* === Status Bar === */
    ImGui::SetNextWindowPos(ImVec2(margin, margin));
    ImGui::SetNextWindowSize(ImVec2(fullW, statusH));
    if (ImGui::Begin("Server Status", nullptr, panelFlags)) {
        drawStatusBar(fullW);
    }
    ImGui::End();

    /* === Tabbed middle panel === */
    ImGui::SetNextWindowPos(ImVec2(margin, middleTop));
    ImGui::SetNextWindowSize(ImVec2(fullW, middleH));
    if (ImGui::Begin("##middle_tabs", nullptr,
                     panelFlags | ImGuiWindowFlags_NoTitleBar)) {

        if (ImGui::BeginTabBar("##srv_tabs")) {
            float innerH = middleH - tabBarH
                           - ImGui::GetStyle().WindowPadding.y * 2.0f;
            float innerW = fullW - ImGui::GetStyle().WindowPadding.x * 2.0f;

            /* Players tab */
            if (ImGui::BeginTabItem("Players")) {
                drawTabPlayers(innerW, innerH);
                ImGui::EndTabItem();
            }

            /* Rooms tab */
            if (ImGui::BeginTabItem("Rooms")) {
                drawTabRooms(innerW, innerH);
                ImGui::EndTabItem();
            }

            /* Operator tab */
            if (ImGui::BeginTabItem("Operator")) {
                drawTabOperator(innerW, innerH);
                ImGui::EndTabItem();
            }

            /* Updates tab (badged when update available) */
            {
                const char *updateLabel = updaterIsUpdateAvailable()
                    ? "Updates (*)" : "Updates";
                if (ImGui::BeginTabItem(updateLabel)) {
                    static bool s_TabChecked = false;
                    if (!s_TabChecked) {
                        s_TabChecked = true;
                        updater_status_t st = updaterGetStatus();
                        if (st == UPDATER_IDLE || st == UPDATER_CHECK_FAILED) {
                            updaterCheckAsync();
                        }
                    }
                    drawTabUpdate(innerW, innerH);
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    /* === Log panel === */
    ImGui::SetNextWindowPos(ImVec2(margin, logTop));
    ImGui::SetNextWindowSize(ImVec2(fullW, logH));
    if (ImGui::Begin("Server Log", nullptr, panelFlags)) {
        drawLogPanel(fullW, logH - ImGui::GetStyle().WindowPadding.y * 2.0f
                                 - ImGui::GetFrameHeightWithSpacing());
    }
    ImGui::End();

    /* Finalize */
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
}

extern "C" void serverGuiShutdown(void)
{
    if (!s_Initialized) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    s_Initialized = false;
}
