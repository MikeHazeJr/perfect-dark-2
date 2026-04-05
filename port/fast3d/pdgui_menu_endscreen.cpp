/**
 * pdgui_menu_endscreen.cpp -- ImGui end screen for solo missions and MP match results.
 *
 * Group 2 of the menu migration (13+ dialogs):
 *   Solo: Completed, Failed, Objectives, Retry, Next Mission, 2P variants
 *   MP:   Game Over (ind/team), Challenge screens, Rankings, Stats, Save Player
 *
 * Design: polished visual hierarchy matching PD aesthetic.
 *   - Solo: title banner, key stats (time/difficulty/kills/accuracy with fill bar),
 *     objectives with color-coded status icons, cheat-unlock announcement in gold.
 *   - MP: player placement headline, full rankings table, awards row.
 *   All sizing exclusively through pdguiScale().
 *
 * Sibling dialogs (Objectives pages, Ranking sub-pages, etc.) are registered as
 * no-ops so the legacy system is suppressed while the root dialog shows everything.
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
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_hotswap.h"
#include "system.h"

/* ========================================================================
 * Forward declarations (C boundary — cannot include types.h)
 * ======================================================================== */

extern "C" {

/* Opaque forward declarations */
struct menuitem;
struct menudialogdef;
struct menudialog;
struct menu;

/* Shot region constants (from constants.h, repeated here to avoid types.h) */
#define ES_SHOT_TOTAL  0
#define ES_SHOT_HEAD   1
#define ES_SHOT_BODY   2
#define ES_SHOT_LIMB   3
#define ES_SHOT_GUN    4
#define ES_SHOT_HAT    5
#define ES_SHOT_OBJECT 6

/* formatTime precision: show up to seconds */
#define ES_TIME_SECS   3

/* Objective status constants */
#define ES_OBJ_INCOMPLETE 0
#define ES_OBJ_COMPLETE   1
#define ES_OBJ_FAILED     2

/* Difficulty bit constants */
#define ES_DIFFBIT_A  0x01u
#define ES_DIFFBIT_SA 0x02u
#define ES_DIFFBIT_PA 0x04u
#define ES_DIFFBIT_PD 0x08u

/* MP layout constants (must match MAX_PLAYERS/MAX_BOTS in constants.h) */
#define ES_MAX_PLAYERS     8
#define ES_MAX_BOTS       32
#define ES_MAX_MPCHRS     (ES_MAX_PLAYERS + ES_MAX_BOTS)

/* MP option flags */
#define ES_MPOPTION_TEAMSENABLED 0x00000002u

/* Mission stats */
s32 mpstatsGetPlayerKillCount(void);
s32 mpstatsGetPlayerShotCountByRegion(u32 type);
u32 playerGetMissionTime(void);
void formatTime(char *dst, s32 time60, s32 precision);

/* Objectives (item param is unused by these functions — safe to pass NULL) */
s32 objectiveGetCount(void);
char *objectiveGetText(s32 index);
s32 objectiveCheck(s32 index);
u32 objectiveGetDifficultyBits(s32 index);

/* Endscreen text helpers (all item params unused — safe to pass NULL) */
char *endscreenMenuTextMissionStatus(struct menuitem *item);
char *endscreenMenuTextAgentStatus(struct menuitem *item);
char *endscreenMenuTitleStageCompleted(struct menuitem *item);
char *endscreenMenuTitleStageFailed(struct menuitem *item);

/* Bridge functions (pdgui_bridge.c) */
s32 pdguiEndscreenGetDifficulty(void);
const char *pdguiEndscreenGetCheatTimedName(void);
const char *pdguiEndscreenGetCheatComplName(void);
void pdguiEndscreenStartMission(void);
void pdguiEndscreenNextMission(void);
void pdguiEndscreenExitToMainMenu(void);
s32 pdguiEndscreenHasNextMission(void);
s32 pdguiEndscreenGetPlacementIndex(void);
const char *pdguiEndscreenGetTitle(void);
s32 pdguiEndscreenTitleChanged(void);
const char *pdguiEndscreenGetWeaponOfChoiceName(void);
const char *pdguiEndscreenGetAward1(void);
const char *pdguiEndscreenGetAward2(void);
u32 pdguiEndscreenGetMedals(void);
s32 pdguiEndscreenGetChallengeStatus(void);

/* MP rankings — layout-compatible with pause menu */
struct mpchrconfig_es {
    char name[15];
    u8 mpheadnum;
    u8 mpbodynum;
    u8 team;
    u8 _pad0[2];
    u32 displayoptions;
    u16 unk18;
    u16 unk1a;
    u16 unk1c;
    s8 placement;
    u8 _pad1;
    s32 rankablescore;
    s16 killcounts[ES_MAX_MPCHRS];
    s16 numdeaths;
    s16 numpoints;
    s16 unk40;
};

struct ranking_es {
    struct mpchrconfig_es *mpchr;
    union { u32 teamnum; u32 chrnum; };
    u32 positionindex;
    u8 unk0c;
    s32 score;
};

s32 mpGetPlayerRankings(struct ranking_es *rankings);
s32 mpGetTeamRankings(struct ranking_es *rankings);
u32 pdguiPauseGetOptions(void);

/* Game state */
extern s32 g_MpPlayerNum;
extern s32 g_NetMode;
#define ES_NETMODE_NONE   0
#define ES_NETMODE_SERVER 1
#define ES_NETMODE_CLIENT 2

/* Navigation */
void netDisconnect(void);
void pdguiSetInRoom(s32 inRoom);
void pdguiSoloRoomOpen(void);
void pdguiSoloRoomReturn(void); /* U-12: return to room preserving config */

/* Dialog definitions for registration */
extern struct menudialogdef g_SoloMissionEndscreenCompletedMenuDialog;
extern struct menudialogdef g_SoloMissionEndscreenFailedMenuDialog;
extern struct menudialogdef g_SoloEndscreenObjectivesCompletedMenuDialog;
extern struct menudialogdef g_SoloEndscreenObjectivesFailedMenuDialog;
extern struct menudialogdef g_RetryMissionMenuDialog;
extern struct menudialogdef g_NextMissionMenuDialog;
extern struct menudialogdef g_MissionContinueOrReplyMenuDialog;
extern struct menudialogdef g_2PMissionEndscreenCompletedHMenuDialog;
extern struct menudialogdef g_2PMissionEndscreenFailedHMenuDialog;
extern struct menudialogdef g_2PMissionEndscreenCompletedVMenuDialog;
extern struct menudialogdef g_2PMissionEndscreenFailedVMenuDialog;
extern struct menudialogdef g_2PMissionEndscreenObjectivesCompletedVMenuDialog;
extern struct menudialogdef g_2PMissionEndscreenObjectivesFailedVMenuDialog;
extern struct menudialogdef g_MpEndscreenIndGameOverMenuDialog;
extern struct menudialogdef g_MpEndscreenTeamGameOverMenuDialog;
extern struct menudialogdef g_MpEndscreenChallengeCompletedMenuDialog;
extern struct menudialogdef g_MpEndscreenChallengeCheatedMenuDialog;
extern struct menudialogdef g_MpEndscreenChallengeFailedMenuDialog;
extern struct menudialogdef g_MpEndscreenPlayerRankingMenuDialog;
extern struct menudialogdef g_MpEndscreenTeamRankingMenuDialog;
extern struct menudialogdef g_MpEndscreenPlayerStatsMenuDialog;

} /* extern "C" */

/* ========================================================================
 * Local constants
 * ======================================================================== */

static const char *s_DiffNames[] = {
    "Agent", "Special Agent", "Perfect Agent", "Perfect Dark"
};

/* Team color table — matches pause menu */
static const ImVec4 s_TeamColors[] = {
    ImVec4(1.0f, 0.3f, 0.3f, 1.0f),   /* Red */
    ImVec4(0.3f, 0.5f, 1.0f, 1.0f),   /* Blue */
    ImVec4(0.3f, 1.0f, 0.3f, 1.0f),   /* Green */
    ImVec4(1.0f, 1.0f, 0.3f, 1.0f),   /* Yellow */
    ImVec4(1.0f, 0.5f, 0.0f, 1.0f),   /* Orange */
    ImVec4(0.8f, 0.3f, 1.0f, 1.0f),   /* Purple */
    ImVec4(0.5f, 0.5f, 0.5f, 1.0f),   /* Grey */
    ImVec4(1.0f, 1.0f, 1.0f, 1.0f),   /* White */
};

/* Medal names in order of bit position */
static const char *s_MedalNames[] = {
    "Kill Master", "Headshot Expert", "Most Accurate", "Last Man Standing"
};

/* Team display names indexed by team number */
static const char *s_TeamNames[] = {
    "Red", "Blue", "Green", "Yellow", "Orange", "Purple", "Grey", "White"
};

/* ========================================================================
 * Helpers
 * ======================================================================== */

/* Strip trailing whitespace and newlines from a string in-place. */
static void stripTrailing(char *s)
{
    s32 len = (s32)strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' '))
        s[--len] = '\0';
}

/* Safe copy from possibly-NULL source; result always NUL-terminated. */
static void safeCopy(char *dst, const char *src, s32 maxLen)
{
    if (!src) src = "";
    strncpy(dst, src, (size_t)(maxLen - 1));
    dst[maxLen - 1] = '\0';
}

/* PD-styled button with audio + edge glow. */
static bool PdEndButton(const char *label, const ImVec2 &size = ImVec2(0, 0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);

    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

/* Horizontal rule with a label. */
static void SectionHeader(const char *label)
{
    float padX = pdguiScale(4.0f);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

/* Two-column stat row: label on left, value right-aligned. */
static void StatRow(const char *label, const char *value, const ImVec4 *valueColor = nullptr)
{
    float scale   = pdguiScaleFactor();
    float colW    = pdguiScale(130.0f);   /* label column width */

    ImGui::Text("%s", label);
    ImGui::SameLine(colW);
    if (valueColor) ImGui::PushStyleColor(ImGuiCol_Text, *valueColor);
    ImGui::Text("%s", value);
    if (valueColor) ImGui::PopStyleColor();
    (void)scale;
}

/* ========================================================================
 * Rankings data
 * ======================================================================== */

struct ESRankRow {
    char  name[32];
    s32   score;
    s32   kills;
    s32   deaths;
    u8    team;
    bool  isLocalPlayer;
};

static s32 buildRankings(ESRankRow *rows, s32 maxRows, bool teams)
{
    struct ranking_es raw[ES_MAX_MPCHRS];
    s32 count = teams ? mpGetTeamRankings(raw) : mpGetPlayerRankings(raw);
    if (count > maxRows) count = maxRows;

    for (s32 i = 0; i < count; i++) {
        struct mpchrconfig_es *cfg = raw[i].mpchr;
        if (!cfg) {
            rows[i].name[0] = '?'; rows[i].name[1] = '\0';
            rows[i].score = rows[i].kills = rows[i].deaths = 0;
            rows[i].team = 0;
            rows[i].isLocalPlayer = false;
            continue;
        }

        /* Copy name (PD uses '\n' as terminator, not '\0') */
        s32 j;
        for (j = 0; j < 30 && cfg->name[j] != '\0' && cfg->name[j] != '\n'; j++)
            rows[i].name[j] = cfg->name[j];
        rows[i].name[j] = '\0';

        rows[i].score         = raw[i].score;
        rows[i].deaths        = (s32)cfg->numdeaths;
        rows[i].team          = cfg->team;
        rows[i].isLocalPlayer = (raw[i].chrnum < (u32)ES_MAX_PLAYERS);

        s32 kills = 0;
        for (s32 k = 0; k < ES_MAX_MPCHRS; k++) {
            if ((u32)k != raw[i].chrnum)
                kills += (s32)cfg->killcounts[k];
        }
        rows[i].kills = kills;
    }
    return count;
}

/* Sort rankings by team (ascending) then score (descending within team) */
static void sortRankingsByTeam(ESRankRow *rows, s32 count)
{
    for (s32 i = 1; i < count; i++) {
        ESRankRow tmp = rows[i];
        s32 j = i - 1;
        while (j >= 0 && (rows[j].team > tmp.team ||
               (rows[j].team == tmp.team && rows[j].score < tmp.score))) {
            rows[j + 1] = rows[j];
            j--;
        }
        rows[j + 1] = tmp;
    }
}

/* ========================================================================
 * Solo End Screen
 * ======================================================================== */

static void renderSoloEndscreen(bool completed)
{
    /* ----- Palette ---------------------------------------------------- */
    /* E.3: Save and restore so this screen's palette doesn't bleed into
     * subsequent menus (main menu, etc.) rendered after us. */
    s32 prevPalette = pdguiGetPalette();
    pdguiSetPalette(completed ? 3 : 2);

    ImVec2 disp = ImGui::GetIO().DisplaySize;
    float  sf   = pdguiScaleFactor();

    /* Panel dimensions — slightly wider than pause menu for stat/obj columns */
    float menuW = disp.x * 0.62f;
    float menuH = disp.y * 0.82f;
    if (menuW > pdguiScale(900.0f)) menuW = pdguiScale(900.0f);
    float menuX = (disp.x - menuW) * 0.5f;
    float menuY = (disp.y - menuH) * 0.5f;

    float padX = pdguiScale(18.0f);
    float padY = pdguiScale(36.0f);  /* below title bar */

    /* ----- Dim the background ----------------------------------------- */
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), disp, IM_COL32(0, 0, 0, 160));

    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##SoloEndscreen", NULL, flags)) {
        ImGui::End();
        pdguiSetPalette(prevPalette);
        return;
    }

    /* E.2: Release mouse grab on first appear so endscreen buttons are clickable.
     * The game may still have SDL in relative mode from active gameplay. */
    if (ImGui::IsWindowAppearing()) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        SDL_Window *win = SDL_GetMouseFocus();
        if (win) {
            int w, h;
            SDL_GetWindowSize(win, &w, &h);
            SDL_WarpMouseInWindow(win, w / 2, h / 2);
        }
    }

    /* ----- PD dialog frame -------------------------------------------- */
    pdguiDrawPdDialog(menuX, menuY, menuW, menuH,
                      completed ? "Mission Complete" : "Mission Failed", 1);

    ImGui::SetCursorPos(ImVec2(padX, padY));

    /* ----- Big title -------------------------------------------------- */
    {
        char titleBuf[128];
        const char *raw = completed ? endscreenMenuTitleStageCompleted(NULL)
                                    : endscreenMenuTitleStageFailed(NULL);
        safeCopy(titleBuf, raw, (s32)sizeof(titleBuf));
        stripTrailing(titleBuf);

        ImGui::SetWindowFontScale(sf * 1.35f);
        if (completed) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.35f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        }
        ImGui::TextUnformatted(titleBuf);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);
    }

    ImGui::Spacing();

    /* ----- Gather stats ----------------------------------------------- */
    char missionTimeBuf[32];
    formatTime(missionTimeBuf, (s32)playerGetMissionTime(), ES_TIME_SECS);

    char missionStatusBuf[64];
    safeCopy(missionStatusBuf, endscreenMenuTextMissionStatus(NULL), 64);

    char agentStatusBuf[64];
    safeCopy(agentStatusBuf, endscreenMenuTextAgentStatus(NULL), 64);

    s32 diff = pdguiEndscreenGetDifficulty();
    const char *diffName = (diff >= 0 && diff <= 3) ? s_DiffNames[diff] : "Unknown";

    s32 kills      = mpstatsGetPlayerKillCount();
    s32 totalShots = mpstatsGetPlayerShotCountByRegion(ES_SHOT_TOTAL);
    s32 headShots  = mpstatsGetPlayerShotCountByRegion(ES_SHOT_HEAD);
    s32 bodyShots  = mpstatsGetPlayerShotCountByRegion(ES_SHOT_BODY);
    s32 limbShots  = mpstatsGetPlayerShotCountByRegion(ES_SHOT_LIMB);
    s32 gunShots   = mpstatsGetPlayerShotCountByRegion(ES_SHOT_GUN);
    s32 hatShots   = mpstatsGetPlayerShotCountByRegion(ES_SHOT_HAT);
    s32 objShots   = mpstatsGetPlayerShotCountByRegion(ES_SHOT_OBJECT);

    float accuracy = 0.0f;
    char  accuracyBuf[32] = "0.0%";
    if (totalShots > 0) {
        s32 hits = headShots + bodyShots + limbShots + gunShots + hatShots + objShots;
        accuracy = (float)hits / (float)totalShots;
        if (accuracy > 1.0f) accuracy = 1.0f;
        snprintf(accuracyBuf, sizeof(accuracyBuf), "%.1f%%", accuracy * 100.0f);
    }

    /* ----- Left column: stats  |  Right column: objectives ------------ */
    float colSplit = menuW * 0.47f;
    float contentW = menuW - padX * 2.0f;
    float contentH = menuH - padY - pdguiScale(52.0f); /* leave room for buttons */

    ImGui::BeginChild("##EsContent", ImVec2(contentW, contentH), false);

    /* Two-column layout via table */
    if (ImGui::BeginTable("##EsCols", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Stats",      ImGuiTableColumnFlags_WidthFixed, colSplit - padX);
        ImGui::TableSetupColumn("Objectives", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();

        /* === LEFT: Stats === */
        ImGui::TableSetColumnIndex(0);

        SectionHeader("DEBRIEF");

        {
            ImVec4 statusColor = completed
                ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f)
                : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            StatRow("Status:", missionStatusBuf, &statusColor);
        }
        StatRow("Agent:",     agentStatusBuf);
        StatRow("Time:",      missionTimeBuf);
        StatRow("Difficulty:", diffName);

        ImGui::Spacing();
        SectionHeader("COMBAT");

        {
            char killBuf[16];
            snprintf(killBuf, sizeof(killBuf), "%d", kills);
            StatRow("Kills:", killBuf);
        }

        /* Accuracy with fill bar */
        StatRow("Accuracy:", accuracyBuf);
        {
            float barW = colSplit - padX - pdguiScale(8.0f);
            float barH = pdguiScale(8.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pdguiScale(4.0f));

            /* Color: green for good accuracy, yellow for ok, red for poor */
            ImVec4 barColor;
            if (accuracy >= 0.60f) {
                barColor = ImVec4(0.2f, 0.85f, 0.35f, 0.9f);
            } else if (accuracy >= 0.30f) {
                barColor = ImVec4(0.95f, 0.75f, 0.1f, 0.9f);
            } else {
                barColor = ImVec4(0.85f, 0.25f, 0.25f, 0.9f);
            }

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.2f, 0.8f));
            ImGui::ProgressBar(accuracy, ImVec2(barW, barH), "");
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();

        /* Shot breakdown (compact) */
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.75f, 1.0f));
        ImGui::Text("Head %d  Body %d  Limb %d",
                    headShots, bodyShots, limbShots);
        ImGui::Text("Other %d  Total %d",
                    gunShots + hatShots + objShots, totalShots);
        ImGui::PopStyleColor();

        /* === RIGHT: Objectives === */
        ImGui::TableSetColumnIndex(1);

        SectionHeader("OBJECTIVES");

        {
            s32 objCount = objectiveGetCount();
            u32 diffbit  = ES_DIFFBIT_A << (u32)diff;   /* bit for current difficulty */
            bool anyShown = false;

            for (s32 i = 0; i < objCount; i++) {
                u32 bits = objectiveGetDifficultyBits(i);
                if (!(bits & diffbit)) continue;   /* not applicable at this difficulty */

                char *text  = objectiveGetText(i);
                s32   status = objectiveCheck(i);
                anyShown = true;

                ImVec4 iconColor;
                const char *icon;
                if (status == ES_OBJ_COMPLETE) {
                    iconColor = ImVec4(0.3f, 1.0f, 0.45f, 1.0f);
                    icon = "[+]";
                } else if (status == ES_OBJ_FAILED) {
                    iconColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                    icon = "[X]";
                } else {
                    iconColor = ImVec4(1.0f, 0.55f, 0.2f, 1.0f);
                    icon = "[ ]";
                }

                ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
                ImGui::TextUnformatted(icon);
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, pdguiScale(6.0f));
                ImGui::TextWrapped("%s", text ? text : "");
                ImGui::Spacing();
            }

            if (!anyShown) {
                ImGui::TextDisabled("No objectives.");
            }
        }

        ImGui::EndTable();
    }

    /* ----- Cheat unlock (gold announcement) --------------------------- */
    const char *timedCheat = pdguiEndscreenGetCheatTimedName();
    const char *complCheat = pdguiEndscreenGetCheatComplName();

    if (timedCheat || complCheat) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
        ImGui::TextUnformatted("  * NEW CHEAT UNLOCKED *");
        if (timedCheat) ImGui::Text("    %s", timedCheat);
        if (complCheat) ImGui::Text("    %s", complCheat);
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();

    /* ----- Action buttons at bottom ----------------------------------- */
    float btnH   = pdguiScale(32.0f);
    float btnGap = pdguiScale(12.0f);
    float btnY   = menuH - btnH - pdguiScale(12.0f);

    if (completed) {
        /* RETRY  |  NEXT MISSION */
        float totalBtnW = menuW * 0.7f;
        float halfW     = (totalBtnW - btnGap) * 0.5f;
        float startX    = (menuW - totalBtnW) * 0.5f;

        ImGui::SetCursorPos(ImVec2(startX, btnY));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(btnGap, pdguiScale(4.0f)));

        if (PdEndButton("Retry Mission", ImVec2(halfW, btnH))) {
            pdguiEndscreenStartMission();
        }
        ImGui::SameLine();

        if (pdguiEndscreenHasNextMission()) {
            if (PdEndButton("Next Mission", ImVec2(halfW, btnH))) {
                pdguiEndscreenNextMission();
            }
        } else {
            if (PdEndButton("Main Menu", ImVec2(halfW, btnH))) {
                pdguiEndscreenExitToMainMenu();
            }
        }

        ImGui::PopStyleVar();
    } else {
        /* RETRY  |  MAIN MENU */
        float totalBtnW = menuW * 0.7f;
        float halfW     = (totalBtnW - btnGap) * 0.5f;
        float startX    = (menuW - totalBtnW) * 0.5f;

        ImGui::SetCursorPos(ImVec2(startX, btnY));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(btnGap, pdguiScale(4.0f)));

        if (PdEndButton("Retry Mission", ImVec2(halfW, btnH))) {
            pdguiEndscreenStartMission();
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(0.3f, 0.1f, 0.1f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
        if (PdEndButton("Main Menu", ImVec2(halfW, btnH))) {
            pdguiEndscreenExitToMainMenu();
        }
        ImGui::PopStyleColor(3);

        ImGui::PopStyleVar();
    }

    /* Keyboard navigation: Enter/Start or Escape/Back */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        pdguiEndscreenExitToMainMenu();
    }

    ImGui::End();

    /* E.3: Restore palette so the next renderer (main menu, etc.) is clean. */
    pdguiSetPalette(prevPalette);
}

/* ========================================================================
 * MP End Screen
 * ======================================================================== */

/* challengeResult: 0=normal, 1=completed, 2=failed, 3=cheated */
static void renderMpEndscreen(const char *titleOverride, s32 challengeResult)
{
    /* ----- Palette ---------------------------------------------------- */
    /* E.3: Save and restore so this screen's palette doesn't bleed out. */
    s32 prevPalette = pdguiGetPalette();
    if (challengeResult == 1) {
        pdguiSetPalette(3);  /* green for challenge complete */
    } else if (challengeResult >= 2) {
        pdguiSetPalette(2);  /* red for challenge failed/cheated */
    } else {
        pdguiSetPalette(6);  /* black/gold for regular game over */
    }

    ImVec2 disp  = ImGui::GetIO().DisplaySize;
    float  sf    = pdguiScaleFactor();
    float  menuW = disp.x * 0.68f;
    float  menuH = disp.y * 0.80f;
    if (menuW > pdguiScale(960.0f)) menuW = pdguiScale(960.0f);
    float menuX = (disp.x - menuW) * 0.5f;
    float menuY = (disp.y - menuH) * 0.5f;

    float padX = pdguiScale(18.0f);
    float padY = pdguiScale(36.0f);

    /* ----- Dim background --------------------------------------------- */
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), disp, IM_COL32(0, 0, 0, 160));

    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##MpEndscreen", NULL, flags)) {
        ImGui::End();
        pdguiSetPalette(prevPalette);
        return;
    }

    /* E.2: Release mouse grab on first appear so buttons are clickable. */
    if (ImGui::IsWindowAppearing()) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        SDL_Window *win = SDL_GetMouseFocus();
        if (win) {
            int w, h;
            SDL_GetWindowSize(win, &w, &h);
            SDL_WarpMouseInWindow(win, w / 2, h / 2);
        }
    }

    pdguiDrawPdDialog(menuX, menuY, menuW, menuH, "Game Over", 1);

    ImGui::SetCursorPos(ImVec2(padX, padY));

    /* ----- Big title -------------------------------------------------- */
    {
        const char *title = titleOverride ? titleOverride : "GAME OVER";
        ImVec4 titleColor;
        if (challengeResult == 1)      titleColor = ImVec4(0.95f, 0.85f, 0.35f, 1.0f);
        else if (challengeResult >= 2) titleColor = ImVec4(1.0f,  0.35f, 0.35f, 1.0f);
        else                           titleColor = ImVec4(0.90f, 0.80f, 0.50f, 1.0f);

        ImGui::SetWindowFontScale(sf * 1.35f);
        ImGui::PushStyleColor(ImGuiCol_Text, titleColor);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);
    }

    /* ----- Placement headline ----------------------------------------- */
    {
        static const char *s_PlaceSuffixes[] = {
            "1st Place!", "2nd Place", "3rd Place", "4th Place",
            "5th Place",  "6th Place", "7th Place", "8th Place",
            "9th Place",  "10th Place","11th Place","12th Place",
        };
        s32 placement = pdguiEndscreenGetPlacementIndex();
        if (placement >= 0 && placement < 12) {
            bool winner = (placement == 0);
            ImVec4 placeColor = winner
                ? ImVec4(1.0f, 0.85f, 0.0f, 1.0f)   /* gold for 1st */
                : ImVec4(0.75f, 0.75f, 0.85f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, placeColor);
            ImGui::SetWindowFontScale(sf * 1.15f);
            ImGui::Text("You placed %s", s_PlaceSuffixes[placement]);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        }

        /* Title change flash */
        if (pdguiEndscreenTitleChanged()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
            ImGui::Text("  Title: %s", pdguiEndscreenGetTitle());
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();

    /* ----- Content area ----------------------------------------------- */
    float contentH = menuH - padY - pdguiScale(100.0f);  /* room for awards + button */
    float contentW = menuW - padX * 2.0f;

    ImGui::BeginChild("##MpContent", ImVec2(contentW, contentH), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    /* Rankings table */
    u32 options     = pdguiPauseGetOptions();
    bool teamsMode  = (options & ES_MPOPTION_TEAMSENABLED) != 0;

    SectionHeader("FINAL RANKINGS");

    ESRankRow rows[ES_MAX_MPCHRS];
    s32 count = buildRankings(rows, ES_MAX_MPCHRS, teamsMode);

    if (teamsMode) {
        sortRankingsByTeam(rows, count);
    }

    /* Header row */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.75f, 1.0f, 1.0f));
    if (teamsMode) {
        ImGui::Text("%-4s %-14s %5s %6s %6s %7s",
                    "#", "Name", "Team", "Score", "Kills", "Deaths");
    } else {
        ImGui::Text("%-4s %-14s %7s %7s %7s",
                    "#", "Name", "Score", "Kills", "Deaths");
    }
    ImGui::PopStyleColor();
    ImGui::Separator();

    u8 currentTeam = 0xFF;
    s32 teamRank = 0;

    for (s32 i = 0; i < count; i++) {
        /* Team group header */
        if (teamsMode && rows[i].team != currentTeam) {
            currentTeam = rows[i].team;
            teamRank = 0;
            u8 tidx = currentTeam < 8 ? currentTeam : 7u;
            if (i > 0) ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, s_TeamColors[tidx]);
            ImGui::Text("--- Team %s ---", s_TeamNames[tidx]);
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        bool localHighlight = rows[i].isLocalPlayer;
        if (localHighlight)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));

        teamRank++;
        char rankStr[8];
        snprintf(rankStr, sizeof(rankStr), "%d.", teamsMode ? teamRank : (i + 1));

        if (teamsMode) {
            ImGui::Text("  %-4s %-14s %7d %7d %7d",
                        rankStr, rows[i].name,
                        rows[i].score, rows[i].kills, rows[i].deaths);
        } else {
            ImGui::Text("%-4s %-14s %7d %7d %7d",
                        rankStr, rows[i].name,
                        rows[i].score, rows[i].kills, rows[i].deaths);
        }

        if (localHighlight)
            ImGui::PopStyleColor();
    }

    /* Awards + medals */
    const char *award1 = pdguiEndscreenGetAward1();
    const char *award2 = pdguiEndscreenGetAward2();
    u32 medals         = pdguiEndscreenGetMedals();

    if ((award1 && award1[0]) || (award2 && award2[0]) || medals) {
        ImGui::Spacing();
        SectionHeader("AWARDS");

        if (award1 && award1[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
            ImGui::Text("  %s", award1);
            ImGui::PopStyleColor();
        }
        if (award2 && award2[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
            ImGui::Text("  %s", award2);
            ImGui::PopStyleColor();
        }
        for (s32 m = 0; m < 4; m++) {
            if (medals & (1u << m)) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.3f, 1.0f));
                ImGui::Text("  [Medal] %s", s_MedalNames[m]);
                ImGui::PopStyleColor();
            }
        }
    }

    /* Weapon of choice */
    const char *woc = pdguiEndscreenGetWeaponOfChoiceName();
    if (woc && woc[0]) {
        ImGui::Spacing();
        ImGui::TextDisabled("  Weapon of Choice: %s", woc);
    }

    ImGui::EndChild();

    /* ----- Action buttons ---------------------------------------------- */
    float btnH   = pdguiScale(32.0f);
    float btnGap = pdguiScale(12.0f);
    float btnY   = menuH - btnH - pdguiScale(12.0f);
    bool networked = (g_NetMode != ES_NETMODE_NONE);

    if (networked) {
        /* Two buttons: Return to Room (blue) | Disconnect (red) */
        float halfW = (menuW - padX * 2 - btnGap) * 0.5f;

        ImGui::SetCursorPos(ImVec2(padX, btnY));
        if (PdEndButton("Return to Room", ImVec2(halfW, btnH))) {
            pdguiEndscreenExitToMainMenu();
            pdguiSetInRoom(1);
        }

        ImGui::SetCursorPos(ImVec2(padX + halfW + btnGap, btnY));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.15f, 0.15f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (PdEndButton("Disconnect", ImVec2(halfW, btnH))) {
            netDisconnect();
            pdguiEndscreenExitToMainMenu();
        }
        ImGui::PopStyleColor(3);
    } else {
        /* Solo: Play Again (blue) | Quit (red) */
        float halfW = (menuW - padX * 2 - btnGap) * 0.5f;

        ImGui::SetCursorPos(ImVec2(padX, btnY));
        if (PdEndButton("Play Again", ImVec2(halfW, btnH))) {
            pdguiEndscreenExitToMainMenu();
            pdguiSoloRoomReturn(); /* U-12: preserve config for rematch */
        }

        ImGui::SetCursorPos(ImVec2(padX + halfW + btnGap, btnY));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.15f, 0.15f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (PdEndButton("Quit", ImVec2(halfW, btnH))) {
            pdguiEndscreenExitToMainMenu();
        }
        ImGui::PopStyleColor(3);
    }

    /* Keyboard shortcuts */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        if (networked) {
            netDisconnect();
        }
        pdguiEndscreenExitToMainMenu();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
        pdguiEndscreenExitToMainMenu();
        if (networked) {
            pdguiSetInRoom(1);
        } else {
            pdguiSoloRoomReturn(); /* U-12: preserve config for rematch */
        }
    }

    ImGui::End();

    /* E.3: Restore palette so the next renderer (main menu, etc.) is clean. */
    pdguiSetPalette(prevPalette);
}

/* ========================================================================
 * Hotswap render callbacks
 * ======================================================================== */

/* Solo — completed screen */
static s32 soloCompletedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderSoloEndscreen(true);
    return 1;
}

/* Solo — failed screen */
static s32 soloFailedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderSoloEndscreen(false);
    return 1;
}

/* Suppress legacy — draw nothing, just consume the render slot.
 * Used for sibling dialogs whose content is folded into the root screen. */
static s32 noopRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    return 1;
}

/* MP — individual game over */
static s32 mpGameOverIndRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen(NULL, 0);
    return 1;
}

/* MP — team game over */
static s32 mpGameOverTeamRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen(NULL, 0);
    return 1;
}

/* MP — challenge completed */
static s32 mpChallengeCompletedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen("CHALLENGE COMPLETED!", 1);
    return 1;
}

/* MP — challenge cheated */
static s32 mpChallengeCheatedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen("CHALLENGE CHEATED!", 3);
    return 1;
}

/* MP — challenge failed */
static s32 mpChallengeFailedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen("CHALLENGE FAILED!", 2);
    return 1;
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" void pdguiMenuEndscreenRegister(void)
{
    /* Solo root dialogs — full ImGui screens */
    pdguiHotswapRegister(&g_SoloMissionEndscreenCompletedMenuDialog,
                         soloCompletedRender, "Solo Complete");
    pdguiHotswapRegister(&g_SoloMissionEndscreenFailedMenuDialog,
                         soloFailedRender,    "Solo Failed");

    /* Solo sibling/sub dialogs — suppressed (content shown in root screen) */
    pdguiHotswapRegister(&g_SoloEndscreenObjectivesCompletedMenuDialog,
                         noopRender, "Solo Objectives OK");
    pdguiHotswapRegister(&g_SoloEndscreenObjectivesFailedMenuDialog,
                         noopRender, "Solo Objectives Fail");
    pdguiHotswapRegister(&g_RetryMissionMenuDialog,
                         noopRender, "Retry Mission");
    pdguiHotswapRegister(&g_NextMissionMenuDialog,
                         noopRender, "Next Mission");
    pdguiHotswapRegister(&g_MissionContinueOrReplyMenuDialog,
                         noopRender, "Continue or Replay");

    /* 2P variants — same full screens (splitscreen handled transparently) */
    pdguiHotswapRegister(&g_2PMissionEndscreenCompletedHMenuDialog,
                         soloCompletedRender, "2P Complete H");
    pdguiHotswapRegister(&g_2PMissionEndscreenFailedHMenuDialog,
                         soloFailedRender,    "2P Failed H");
    pdguiHotswapRegister(&g_2PMissionEndscreenCompletedVMenuDialog,
                         soloCompletedRender, "2P Complete V");
    pdguiHotswapRegister(&g_2PMissionEndscreenFailedVMenuDialog,
                         soloFailedRender,    "2P Failed V");
    pdguiHotswapRegister(&g_2PMissionEndscreenObjectivesCompletedVMenuDialog,
                         noopRender, "2P Objectives OK");
    pdguiHotswapRegister(&g_2PMissionEndscreenObjectivesFailedVMenuDialog,
                         noopRender, "2P Objectives Fail");

    /* MP root dialogs — full ImGui screens */
    pdguiHotswapRegister(&g_MpEndscreenIndGameOverMenuDialog,
                         mpGameOverIndRender,       "MP Game Over");
    pdguiHotswapRegister(&g_MpEndscreenTeamGameOverMenuDialog,
                         mpGameOverTeamRender,      "MP Team Over");
    pdguiHotswapRegister(&g_MpEndscreenChallengeCompletedMenuDialog,
                         mpChallengeCompletedRender, "Challenge Done");
    pdguiHotswapRegister(&g_MpEndscreenChallengeCheatedMenuDialog,
                         mpChallengeCheatedRender,   "Challenge Cheated");
    pdguiHotswapRegister(&g_MpEndscreenChallengeFailedMenuDialog,
                         mpChallengeFailedRender,    "Challenge Failed");

    /* MP sub dialogs — suppressed (content shown in root screen) */
    pdguiHotswapRegister(&g_MpEndscreenPlayerRankingMenuDialog,
                         noopRender, "MP Player Ranking");
    pdguiHotswapRegister(&g_MpEndscreenTeamRankingMenuDialog,
                         noopRender, "MP Team Ranking");
    pdguiHotswapRegister(&g_MpEndscreenPlayerStatsMenuDialog,
                         noopRender, "MP Player Stats");

    /* NOTE: g_MpEndscreenSavePlayerMenuDialog and g_MpEndscreenConfirmNameMenuDialog
     * are intentionally NOT registered here — they use legacy keyboard input and
     * are left as PD native rendering. */
}
