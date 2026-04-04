/**
 * pdgui_menu_pausemenu.cpp -- ImGui combat simulator pause menu + scorecard overlay.
 *
 * Replaces the legacy g_MpPauseControlMenuDialog stack for combat simulator.
 * Also provides a hold-to-show scorecard overlay during gameplay (Tab/B button).
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
#include "pdgui_pausemenu.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "system.h"
#include "menumgr.h"
#include "pdmain.h"

/* ========================================================================
 * Forward declarations (C boundary)
 * ======================================================================== */

extern "C" {

/* Window dimensions */
s32 viGetWidth(void);
s32 viGetHeight(void);

/* Pause state management */
void mpSetPaused(u8 mode);
s32 mpIsPaused(void);

#define MPPAUSEMODE_UNPAUSED  0
#define MPPAUSEMODE_PAUSED    1
#define MPPAUSEMODE_GAMEOVER  2

/* Match setup — read-only for display */
struct mpsetup_opaque; /* can't include types.h */
extern u32 g_MpSetupChrslots;  /* We'll access via bridge functions instead */

/* Net mode */
extern s32 g_NetMode;
#define NETMODE_NONE   0
#define NETMODE_SERVER 1
#define NETMODE_CLIENT 2

/* End game / stage transition */
void mainEndStage(void);
void mainChangeToStage(s32 stagenum);
void netDisconnect(void);

/* Mouse */
s32 inputMouseIsLocked(void);

/* Game state */
struct vars_opaque;
extern s32 g_MainIsEndscreen;

/* Player index for stat lookups */
extern s32 g_MpPlayerNum;

/* Endscreen personal summary bridge (pdgui_bridge.c) */
s32         pdguiEndscreenGetPlacementIndex(void);
const char *pdguiEndscreenGetTitle(void);
s32         pdguiEndscreenTitleChanged(void);
const char *pdguiEndscreenGetWeaponOfChoiceName(void);
const char *pdguiEndscreenGetAward1(void);
const char *pdguiEndscreenGetAward2(void);
u32         pdguiEndscreenGetMedals(void);
s32         pdguiEndscreenGetChallengeStatus(void);  /* 0=none 1=complete 2=failed 3=cheated */

/* Ranking system — must match MAX_PLAYERS, MAX_BOTS, MAX_MPCHRS in src/include/constants.h.
 * Cannot include constants.h here (types.h bool conflict with C++). */
#define MAX_PLAYERS_PM     8   /* = MAX_PLAYERS */
#define MAX_BOTS_PM       32   /* = MAX_BOTS = PARTICIPANT_DEFAULT_CAPACITY (raised from 24 in S45) */
#define MAX_MPCHRS_PM     (MAX_PLAYERS_PM + MAX_BOTS_PM)  /* = 40 = MAX_MPCHRS */

/* We need to call mpGetPlayerRankings, but it uses struct ranking
 * which we can't include from types.h. Define a compatible layout. */
/* Must match struct mpchrconfig in types.h exactly (including alignment) */
struct mpchrconfig_pm {
    /*0x00*/ char name[15];
    /*0x0f*/ u8 mpheadnum;
    /*0x10*/ u8 mpbodynum;
    /*0x11*/ u8 team;
    /*0x12*/ u8 _pad0[2];         /* alignment padding to 0x14 */
    /*0x14*/ u32 displayoptions;
    /*0x18*/ u16 unk18;
    /*0x1a*/ u16 unk1a;
    /*0x1c*/ u16 unk1c;
    /*0x1e*/ s8 placement;
    /*0x1f*/ u8 _pad1;            /* alignment padding to 0x20 */
    /*0x20*/ s32 rankablescore;
    /*0x24*/ s16 killcounts[MAX_MPCHRS_PM];  /* 0x24 + MAX_MPCHRS*2 bytes */
    /*0x74*/ s16 numdeaths;       /* 0x24 + 40*2 = 0x74 with MAX_MPCHRS=40 */
    /*0x76*/ s16 numpoints;
    /*0x78*/ s16 unk40;
};

struct ranking_pm {
    struct mpchrconfig_pm *mpchr;
    union {
        u32 teamnum;
        u32 chrnum;
    };
    u32 positionindex;
    u8 unk0c;
    s32 score;
};

s32 mpGetPlayerRankings(struct ranking_pm *rankings);
s32 mpGetTeamRankings(struct ranking_pm *rankings);

/* Shot region constants — for local player accuracy in the post-match scoreboard */
#define PM_SHOT_TOTAL  0
#define PM_SHOT_HEAD   1
#define PM_SHOT_BODY   2
#define PM_SHOT_LIMB   3
#define PM_SHOT_GUN    4
#define PM_SHOT_HAT    5
#define PM_SHOT_OBJECT 6
s32 mpstatsGetPlayerShotCountByRegion(u32 type);

/* Room screen navigation (pdgui_lobby.cpp) — show room interior after match ends */
void pdguiSetInRoom(s32 inRoom);

/* Match setup access — bridge functions we declare in the bridge section below.
 * These avoid needing to include types.h for g_MpSetup. */
u32 pdguiPauseGetChrSlots(void);
u32 pdguiPauseGetOptions(void);
u8 pdguiPauseGetScenario(void);
u8 pdguiPauseGetStagenum(void);
u8 pdguiPauseGetTimelimit(void);
u8 pdguiPauseGetScorelimit(void);
u8 pdguiPauseGetPaused(void);
s32 pdguiPauseGetNormMplayerIsRunning(void);

/* Scenario names (langGet approach — but we'll use our own table for now) */
char *langGet(s32 textid);

/* Match time */
u32 lvGetStageTime60(void);
void formatTime(char *dst, s32 time60, s32 precision);
#define TIMEPRECISION_SECONDS 2

/* MP option flags */
#define MPOPTION_TEAMSENABLED       0x00000002
#define MPOPTION_ONEHITKILLS        0x00000001
#define MPOPTION_SLOWMOTION_ON      0x00000040
#define MPOPTION_FASTMOVEMENT       0x00000100

/* Player abort flag — set via bridge */
void pdguiPauseSetPlayerAborted(void);

/* Arena/stage name lookup */
const char *pdguiPauseGetStageName(u8 stagenum);

/* Menu stack — to close legacy dialogs if any are open */
void menuCloseAllDialogs(void);

/* Config save */
s32 configSave(const char *fname);

/* Right stick Y invert — from port/include/input.h */
s32 inputControllerGetInvertRStickY(s32 cidx);
void inputControllerSetInvertRStickY(s32 cidx, s32 invert);

} /* extern "C" */

/* ========================================================================
 * Scenario name table (local, since we can't use langGet easily from C++)
 * ======================================================================== */

static const char *s_ScenarioNames[] = {
    "Combat",
    "Hold the Briefcase",
    "Hacker Central",
    "Pop a Cap",
    "King of the Hill",
    "Capture the Case",
};
static const s32 s_NumScenarios = sizeof(s_ScenarioNames) / sizeof(s_ScenarioNames[0]);

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_PauseMenuOpen = false;
static bool s_PauseJustOpened = false; /* B-14 fix: prevents same-frame open+close */
static bool s_ScorecardVisible = false;
static s32 s_PauseTab = 0;  /* 0=Rankings, 1=Stats, 2=Settings */
static bool s_EndGameConfirm = false;
static s32 s_GameOverTab = 0;  /* 0=Rankings, 1=Personal */

/* ========================================================================
 * Pause Menu API (C-callable)
 * ======================================================================== */

void pdguiPauseMenuOpen(void)
{
    if (menuIsInCooldown()) return; /* prevent double-press */

    /* Release mouse grab so the cursor is visible and clickable in the menu.
     * Must happen before s_PauseMenuOpen = true (before pdguiIsActive blocks SDL). */
    pdmainSetInputMode(INPUTMODE_MENU);
    {
        SDL_Window *win = SDL_GetMouseFocus();
        if (win) {
            int w, h;
            SDL_GetWindowSize(win, &w, &h);
            SDL_WarpMouseInWindow(win, w / 2, h / 2);
        }
    }

    s_PauseMenuOpen = true;
    s_PauseJustOpened = true;
    s_PauseTab = 0;
    s_EndGameConfirm = false;

    menuPush(MENU_PAUSE); /* register with menu manager for cooldown */

    /* Pause the game (single-player combat sim only -- network handles differently) */
    if (g_NetMode == NETMODE_NONE) {
        mpSetPaused(MPPAUSEMODE_PAUSED);
    }
}

void pdguiPauseMenuClose(void)
{
    s_PauseMenuOpen = false;
    s_EndGameConfirm = false;

    /* Restore mouse state to what the game expects. */
    pdmainSetInputMode(INPUTMODE_GAMEPLAY);

    menuPop(); /* deregister from menu manager */

    /* Unpause */
    if (g_NetMode == NETMODE_NONE) {
        mpSetPaused(MPPAUSEMODE_UNPAUSED);
    }
}

s32 pdguiIsPauseMenuOpen(void)
{
    return s_PauseMenuOpen ? 1 : 0;
}

/* ========================================================================
 * Scorecard Overlay API
 * ======================================================================== */

void pdguiScorecardSetVisible(s32 visible)
{
    s_ScorecardVisible = (visible != 0);
}

s32 pdguiIsScorecardVisible(void)
{
    return s_ScorecardVisible ? 1 : 0;
}

/* ========================================================================
 * Helper: Build sorted ranking data
 * ======================================================================== */

struct ScorecardRow {
    char name[16];
    s32 score;
    s32 kills;
    s32 deaths;
    u8 team;
    bool isPlayer;  /* true if slot < MAX_PLAYERS */
    float accuracy; /* percentage 0–100, or -1.0f = N/A (only valid for local player) */
};

static s32 buildScorecardData(ScorecardRow *rows, s32 maxRows)
{
    struct ranking_pm rankings[MAX_MPCHRS_PM];
    s32 count = mpGetPlayerRankings(rankings);

    if (count > maxRows) count = maxRows;

    for (s32 i = 0; i < count; i++) {
        struct mpchrconfig_pm *mpchr = rankings[i].mpchr;

        if (!mpchr) {
            rows[i].name[0] = '?'; rows[i].name[1] = '\0';
            rows[i].score = 0;
            rows[i].kills = 0;
            rows[i].deaths = 0;
            rows[i].team = 0;
            rows[i].isPlayer = false;
            continue;
        }

        /* Copy name (null-terminate — original uses newline terminator) */
        s32 j;
        for (j = 0; j < 14 && mpchr->name[j] != '\0' && mpchr->name[j] != '\n'; j++) {
            rows[i].name[j] = mpchr->name[j];
        }
        rows[i].name[j] = '\0';

        rows[i].score = rankings[i].score;
        rows[i].deaths = mpchr->numdeaths;
        rows[i].team = mpchr->team;
        rows[i].isPlayer = (rankings[i].chrnum < (u32)MAX_PLAYERS_PM);

        /* Calculate kills: sum of killcounts[] excluding self (suicides) */
        s32 kills = 0;
        for (s32 k = 0; k < MAX_MPCHRS_PM; k++) {
            if ((u32)k != rankings[i].chrnum) {
                kills += mpchr->killcounts[k];
            }
        }
        rows[i].kills = kills;

        /* Accuracy — available only for the local player (chrnum == g_MpPlayerNum) */
        rows[i].accuracy = -1.0f;
        if ((s32)rankings[i].chrnum == g_MpPlayerNum) {
            s32 totalShots = mpstatsGetPlayerShotCountByRegion(PM_SHOT_TOTAL);
            if (totalShots > 0) {
                s32 hits = mpstatsGetPlayerShotCountByRegion(PM_SHOT_HEAD)
                         + mpstatsGetPlayerShotCountByRegion(PM_SHOT_BODY)
                         + mpstatsGetPlayerShotCountByRegion(PM_SHOT_LIMB)
                         + mpstatsGetPlayerShotCountByRegion(PM_SHOT_GUN)
                         + mpstatsGetPlayerShotCountByRegion(PM_SHOT_HAT)
                         + mpstatsGetPlayerShotCountByRegion(PM_SHOT_OBJECT);
                float acc = (float)hits / (float)totalShots;
                if (acc > 1.0f) acc = 1.0f;
                rows[i].accuracy = acc * 100.0f;
            } else {
                rows[i].accuracy = 0.0f;
            }
        }
    }

    return count;
}

/* ========================================================================
 * Team sort — stable insertion sort by team number, preserving score order
 * within each team.  Max 40 entries; negligible overhead.
 * ======================================================================== */

static void sortRowsByTeam(ScorecardRow *rows, s32 count)
{
    for (s32 i = 1; i < count; i++) {
        ScorecardRow tmp = rows[i];
        s32 j = i - 1;
        while (j >= 0 && rows[j].team > tmp.team) {
            rows[j + 1] = rows[j];
            j--;
        }
        rows[j + 1] = tmp;
    }
}

/* ========================================================================
 * PD-styled button helper (matches pdgui_menu_mainmenu.cpp pattern)
 * ======================================================================== */

static bool PdPauseButton(const char *label, const ImVec2 &size = ImVec2(0,0))
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

/* ========================================================================
 * Pause Menu — Tab: Rankings
 * ======================================================================== */

static void renderRankingsTab(float contentW)
{
    ScorecardRow rows[MAX_MPCHRS_PM];
    s32 count = buildScorecardData(rows, MAX_MPCHRS_PM);

    u32 options = pdguiPauseGetOptions();
    bool teamsEnabled = (options & MPOPTION_TEAMSENABLED) != 0;

    /* Column headers */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
    if (teamsEnabled) {
        ImGui::Text("%-4s %-14s %5s %5s %5s %5s", "#", "Name", "Team", "Score", "Kills", "Deaths");
    } else {
        ImGui::Text("%-4s %-14s %7s %7s %7s", "#", "Name", "Score", "Kills", "Deaths");
    }
    ImGui::PopStyleColor();

    ImGui::Separator();

    /* Team color table */
    static const ImVec4 s_TeamColors[] = {
        ImVec4(1.0f, 0.3f, 0.3f, 1.0f),  /* Red */
        ImVec4(0.3f, 0.5f, 1.0f, 1.0f),  /* Blue */
        ImVec4(0.3f, 1.0f, 0.3f, 1.0f),  /* Green */
        ImVec4(1.0f, 1.0f, 0.3f, 1.0f),  /* Yellow */
        ImVec4(1.0f, 0.5f, 0.0f, 1.0f),  /* Orange */
        ImVec4(0.8f, 0.3f, 1.0f, 1.0f),  /* Purple */
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f),  /* Grey */
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f),  /* White */
    };

    for (s32 i = 0; i < count; i++) {
        /* Highlight the local player */
        if (rows[i].isPlayer) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
        }

        char rankStr[8];
        snprintf(rankStr, sizeof(rankStr), "%d.", i + 1);

        if (teamsEnabled) {
            u8 team = rows[i].team;
            if (team >= 8) team = 7;
            ImVec4 tc = s_TeamColors[team];

            ImGui::Text("%-4s %-14s", rankStr, rows[i].name);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, tc);
            ImGui::Text("  T%d  ", team + 1);
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Text("%5d %5d %5d", rows[i].score, rows[i].kills, rows[i].deaths);
        } else {
            ImGui::Text("%-4s %-14s %7d %7d %7d",
                         rankStr, rows[i].name,
                         rows[i].score, rows[i].kills, rows[i].deaths);
        }

        if (rows[i].isPlayer) {
            ImGui::PopStyleColor();
        }
    }
}

/* ========================================================================
 * Pause Menu — Tab: Match Settings (read-only)
 * ======================================================================== */

static void renderSettingsTab(void)
{
    u8 scenario = pdguiPauseGetScenario();
    u32 options = pdguiPauseGetOptions();

    const char *scenarioName = (scenario < s_NumScenarios) ? s_ScenarioNames[scenario] : "Unknown";
    const char *stageName = pdguiPauseGetStageName(pdguiPauseGetStagenum());

    ImGui::Text("Scenario:    %s", scenarioName);
    ImGui::Text("Arena:       %s", stageName ? stageName : "Unknown");

    u8 timelimit = pdguiPauseGetTimelimit();
    u8 scorelimit = pdguiPauseGetScorelimit();

    if (timelimit > 0) {
        ImGui::Text("Time Limit:  %d min", timelimit);
    } else {
        ImGui::Text("Time Limit:  None");
    }

    if (scorelimit > 0) {
        ImGui::Text("Score Limit: %d", scorelimit);
    } else {
        ImGui::Text("Score Limit: None");
    }

    /* Match time */
    char timebuf[32];
    formatTime(timebuf, lvGetStageTime60(), TIMEPRECISION_SECONDS);
    ImGui::Text("Match Time:  %s", timebuf);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Active options */
    ImGui::Text("Options:");
    if (options & MPOPTION_ONEHITKILLS)   ImGui::BulletText("One Hit Kills");
    if (options & MPOPTION_TEAMSENABLED)  ImGui::BulletText("Teams Enabled");
    if (options & MPOPTION_SLOWMOTION_ON) ImGui::BulletText("Slow Motion");
    if (options & MPOPTION_FASTMOVEMENT)  ImGui::BulletText("Fast Movement");

    /* Count players and bots */
    u32 chrslots = pdguiPauseGetChrSlots();
    s32 numPlayers = 0, numBots = 0;
    for (s32 i = 0; i < MAX_MPCHRS_PM; i++) {
        if (chrslots & (1u << i)) {
            if (i < MAX_PLAYERS_PM) numPlayers++;
            else numBots++;
        }
    }
    ImGui::Text("Players: %d   Bots: %d", numPlayers, numBots);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Controls */
    ImGui::TextDisabled("Controls");

    bool invertRStick = inputControllerGetInvertRStickY(0) != 0;
    if (ImGui::Checkbox("Invert Y-Axis", &invertRStick)) {
        inputControllerSetInvertRStickY(0, invertRStick ? 1 : 0);
        configSave("pd.ini");
    }
}

/* ========================================================================
 * Pause Menu — Main Render
 * ======================================================================== */

void pdguiPauseMenuRender(s32 winW, s32 winH)
{
    if (!s_PauseMenuOpen) return;

    /* Red palette for combat simulator */
    pdguiSetPalette(2);

    /* Center the pause menu — 50% width, 60% height (viewport-relative) */
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    float menuW = disp.x * 0.50f;
    float menuH = disp.y * 0.60f;
    float menuX = (disp.x - menuW) * 0.5f;
    float menuY = (disp.y - menuH) * 0.5f;
    float scale = pdguiScaleFactor();

    /* Dim the background */
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), disp, IM_COL32(0, 0, 0, 140));

    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##PdPauseMenu", NULL, flags)) {
        /* PD-authentic dialog frame */
        pdguiDrawPdDialog(menuX, menuY, menuW, menuH, "PAUSED", 1);

        /* Inset content area */
        float padX = pdguiScale(16.0f);
        float padY = pdguiScale(40.0f); /* below title */

        ImGui::SetCursorPos(ImVec2(padX, padY));

        /* Tab buttons across the top */
        float tabW = (menuW - padX * 2 - pdguiScale(8.0f) * 2) / 3.0f;
        ImVec2 tabSize(tabW, pdguiScale(28.0f));

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pdguiScale(8.0f), pdguiScale(8.0f)));

        if (PdPauseButton("Rankings##pm", tabSize)) { s_PauseTab = 0; pdguiPlaySound(PDGUI_SND_FOCUS); }
        ImGui::SameLine();
        if (PdPauseButton("Settings##pm", tabSize)) { s_PauseTab = 1; pdguiPlaySound(PDGUI_SND_FOCUS); }
        ImGui::SameLine();

        /* End Game button — danger styled */
        if (!s_EndGameConfirm) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            if (PdPauseButton("End Game##pm", tabSize)) {
                s_EndGameConfirm = true;
            }
            ImGui::PopStyleColor(3);
        } else {
            /* Confirmation state: show "Confirm?" / "Cancel" */
            float halfTab = (tabW - 4.0f) * 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.05f, 0.05f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (PdPauseButton("Confirm?##pm", ImVec2(halfTab, 28.0f))) {
                /* Actually end the game */
                pdguiPauseSetPlayerAborted();
                if (g_NetMode == NETMODE_CLIENT) {
                    netDisconnect();
                } else {
                    mainEndStage();
                }
                pdguiPauseMenuClose();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (PdPauseButton("Cancel##pm", ImVec2(halfTab, 28.0f))) {
                s_EndGameConfirm = false;
            }
        }

        ImGui::PopStyleVar(); /* ItemSpacing */

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* Tab content area (scrollable) */
        float contentTop = ImGui::GetCursorPosY();
        float contentH = menuH - contentTop - 50.0f; /* leave room for Resume button */

        ImGui::BeginChild("##PauseTabContent", ImVec2(menuW - padX * 2, contentH), false);

        switch (s_PauseTab) {
        case 0: renderRankingsTab(menuW - padX * 2); break;
        case 1: renderSettingsTab(); break;
        }

        ImGui::EndChild();

        /* Resume button at bottom center */
        float resumeW = 180.0f;
        float resumeH = 36.0f;
        ImGui::SetCursorPos(ImVec2((menuW - resumeW) * 0.5f, menuH - resumeH - 10.0f));
        if (PdPauseButton("Resume##pm", ImVec2(resumeW, resumeH))) {
            pdguiPauseMenuClose();
        }

        /* B-14 fix: On the frame the menu opens, the legacy path (bondmove→
         * mpPushPauseDialog→ingame.c) already opened us. ImGui also sees
         * the same START press via polling. Skip close checks this frame
         * to prevent open+close in one tick. */
        if (s_PauseJustOpened) {
            s_PauseJustOpened = false;
        } else {
            /* Close on START, Escape, or B button (when not in confirm dialog) */
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadStart)) {
                pdguiPauseMenuClose();
            }

            /* B-16 fix: B button (GamepadFaceRight) navigates back.
             * If in End Game confirm → cancel back to normal pause.
             * Otherwise → close the pause menu (resume game). */
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
                if (s_EndGameConfirm) {
                    s_EndGameConfirm = false;
                } else {
                    pdguiPauseMenuClose();
                }
            }
        }
    }
    ImGui::End();

    /* Restore default palette */
    pdguiSetPalette(1);
}

/* ========================================================================
 * Scorecard Overlay — Tick (button hold detection via SDL)
 *
 * Called each frame from the render path. Reads SDL keyboard and
 * controller state to determine if the scorecard button is held.
 * Tab (keyboard) or Back/Select (controller) toggles visibility.
 * ======================================================================== */

static void scorecardTickButtonState(void)
{
    /* Only show during combat sim gameplay */
    if (!pdguiPauseGetNormMplayerIsRunning()) {
        s_ScorecardVisible = false;
        return;
    }

    /* Don't show during endscreen or game-over */
    if (pdguiPauseGetPaused() == MPPAUSEMODE_GAMEOVER) {
        s_ScorecardVisible = false;
        return;
    }

    /* Check SDL keyboard state directly — works even without an ImGui frame.
     * Tab key is the PC-standard "show scoreboard" binding. */
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    bool tabHeld = (keys != NULL) && (keys[SDL_SCANCODE_TAB] != 0);

    /* Check controller Back/Select via ImGui (if frame is active) */
    bool backHeld = false;
    if (ImGui::GetCurrentContext()) {
        backHeld = ImGui::IsKeyDown(ImGuiKey_GamepadBack);
    }

    s_ScorecardVisible = tabHeld || backHeld;
}

/* ========================================================================
 * Scorecard Overlay — Render (hold-to-show during gameplay)
 * ======================================================================== */

void pdguiScorecardRender(s32 winW, s32 winH)
{
    /* Poll button state each frame */
    scorecardTickButtonState();

    if (!s_ScorecardVisible) return;
    if (s_PauseMenuOpen) return; /* Don't show over pause menu */

    ScorecardRow rows[MAX_MPCHRS_PM];
    s32 count = buildScorecardData(rows, MAX_MPCHRS_PM);
    if (count <= 0) return;

    u32 options = pdguiPauseGetOptions();
    bool teamsEnabled = (options & MPOPTION_TEAMSENABLED) != 0;

    /* Size: centered, ~40% width, auto-height based on row count */
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    float boardW = disp.x * 0.40f;
    float minW = pdguiScale(360.0f);
    if (boardW < minW) boardW = minW;
    float rowH    = pdguiScale(22.0f);
    float headerH = pdguiScale(30.0f);
    float padding = pdguiScale(8.0f);
    float boardH = headerH + (count * rowH) + padding * 2;
    float boardX = (disp.x - boardW) * 0.5f;
    float boardY = disp.y * 0.08f; /* near top of screen */

    ImGui::SetNextWindowPos(ImVec2(boardX, boardY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(boardW, boardH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoInputs
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;

    /* Semi-transparent background */
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.05f, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.4f, 0.8f, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

    if (ImGui::Begin("##PdScorecard", NULL, flags)) {
        /* Match time in header */
        char timebuf[32];
        formatTime(timebuf, lvGetStageTime60(), TIMEPRECISION_SECONDS);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 1.0f, 1.0f));
        ImGui::Text("SCOREBOARD");
        ImGui::SameLine(boardW - 100.0f);
        ImGui::Text("%s", timebuf);
        ImGui::PopStyleColor();

        ImGui::Separator();

        /* Column header */
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.6f, 0.8f, 0.9f));
        if (teamsEnabled) {
            ImGui::Text("%-3s %-12s %4s %5s %5s %6s", "#", "Name", "T", "Score", "K", "D");
        } else {
            ImGui::Text("%-3s %-14s %7s %5s %5s", "#", "Name", "Score", "K", "D");
        }
        ImGui::PopStyleColor();

        /* Team colors (same as pause menu) */
        static const ImVec4 s_TeamColors[] = {
            ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
            ImVec4(0.3f, 0.5f, 1.0f, 1.0f),
            ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
            ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
            ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
            ImVec4(0.8f, 0.3f, 1.0f, 1.0f),
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
        };

        /* Rows */
        for (s32 i = 0; i < count; i++) {
            /* Highlight local player with gold text */
            if (rows[i].isPlayer) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
            }

            char rankStr[8];
            snprintf(rankStr, sizeof(rankStr), "%d.", i + 1);

            if (teamsEnabled) {
                u8 team = rows[i].team;
                if (team >= 8) team = 7;

                ImGui::Text("%-3s %-12s", rankStr, rows[i].name);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, s_TeamColors[team]);
                ImGui::Text(" T%d ", team + 1);
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::Text(" %5d %5d %5d", rows[i].score, rows[i].kills, rows[i].deaths);
            } else {
                ImGui::Text("%-3s %-14s %7d %5d %5d",
                             rankStr, rows[i].name,
                             rows[i].score, rows[i].kills, rows[i].deaths);
            }

            if (rows[i].isPlayer) {
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

/* ========================================================================
 * Match Over (GAMEOVER) Overlay
 *
 * Shown when mpEndMatch() sets MPPAUSEMODE_GAMEOVER. Tabbed display:
 *   Rankings — full sorted leaderboard (all players/bots)
 *   Personal — local player's placement, title, weapon, awards, medals
 *
 * Challenge outcomes shown as a banner above the tabs when applicable.
 * Without this call the game freezes on match end — the N64 menu system drove
 * the post-match transition, but the PC port ImGui doesn't observe prevmenuroot.
 * ======================================================================== */

/* Ordinal suffix table for placement display */
static const char *s_PlacementLabels[] = {
    "1st", "2nd", "3rd", "4th", "5th", "6th",
    "7th", "8th", "9th", "10th", "11th", "12th"
};
static const s32 s_NumPlacements = (s32)(sizeof(s_PlacementLabels) / sizeof(s_PlacementLabels[0]));

/* Medal definitions: bit index, color, label */
struct MedalDef { s32 bit; ImVec4 color; const char *label; };
static const MedalDef s_MedalDefs[] = {
    { 0, ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "Killmaster"   },
    { 1, ImVec4(1.0f, 0.85f, 0.1f,  1.0f), "Headshot"     },
    { 2, ImVec4(0.2f, 0.9f,  0.2f,  1.0f), "Accuracy"     },
    { 3, ImVec4(0.2f, 0.7f,  1.0f,  1.0f), "Survivor"     },
};

/* Render the Rankings tab content into the current child region.
 * When teamsEnabled, rows must be pre-sorted by team (done by caller). */
static void renderGameOverRankings(float contentW, s32 count,
                                   const ScorecardRow *rows, bool teamsEnabled)
{
    /* Team color table */
    static const ImVec4 s_TeamCols[] = {
        ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ImVec4(0.3f, 0.5f, 1.0f, 1.0f),
        ImVec4(0.3f, 1.0f, 0.3f, 1.0f), ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
        ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ImVec4(0.8f, 0.3f, 1.0f, 1.0f),
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
    };

    /* Column header */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.65f, 0.85f, 1.0f));
    ImGui::Text("%-3s %-14s %6s %5s %5s %6s", "#", "Name", "Score", "K", "D", "Acc%");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    s32 prevTeam = -1;
    for (s32 i = 0; i < count; i++) {
        /* Team section header — emitted each time the team number changes */
        if (teamsEnabled && (s32)rows[i].team != prevTeam) {
            prevTeam = (s32)rows[i].team;
            u8 hteam = rows[i].team < 8 ? rows[i].team : 7;
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, s_TeamCols[hteam]);
            ImGui::Text("  -- Team %d --", hteam + 1);
            ImGui::PopStyleColor();
        }

        /* Row color: gold for overall 1st, team-color for team members,
         * cyan for local human player, dim for bots */
        ImVec4 rowColor;
        if (i == 0) {
            rowColor = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);   /* gold: overall 1st */
        } else if (rows[i].isPlayer) {
            rowColor = ImVec4(0.5f, 0.9f, 1.0f, 1.0f);    /* cyan: local human */
        } else if (teamsEnabled) {
            u8 tc = rows[i].team < 8 ? rows[i].team : 7;
            ImVec4 tc4 = s_TeamCols[tc];
            rowColor = ImVec4(tc4.x * 0.85f, tc4.y * 0.85f, tc4.z * 0.85f, 1.0f);
        } else {
            rowColor = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);  /* dim: bot */
        }

        char rankStr[8];
        snprintf(rankStr, sizeof(rankStr), "%d.", i + 1);

        char accBuf[10];
        if (rows[i].accuracy >= 0.0f) {
            snprintf(accBuf, sizeof(accBuf), "%.1f", rows[i].accuracy);
        } else {
            accBuf[0] = '-'; accBuf[1] = '-'; accBuf[2] = '\0';
        }

        ImGui::PushStyleColor(ImGuiCol_Text, rowColor);
        ImGui::Text("%-3s %-14s %6d %5d %5d %6s",
                    rankStr, rows[i].name,
                    rows[i].score, rows[i].kills, rows[i].deaths, accBuf);
        ImGui::PopStyleColor();
    }
    (void)contentW;
}

/* Render the Personal tab content into the current child region */
static void renderGameOverPersonal(float contentW)
{
    s32 placement = pdguiEndscreenGetPlacementIndex();
    const char *placementStr = (placement >= 0 && placement < s_NumPlacements)
                               ? s_PlacementLabels[placement] : "?";
    const char *title   = pdguiEndscreenGetTitle();
    const char *weapon  = pdguiEndscreenGetWeaponOfChoiceName();
    const char *award1  = pdguiEndscreenGetAward1();
    const char *award2  = pdguiEndscreenGetAward2();
    u32 medals          = pdguiEndscreenGetMedals();
    bool titleChanged   = pdguiEndscreenTitleChanged() != 0;

    float scale = pdguiScaleFactor();

    /* Placement — gold for 1st, silver for 2nd, bronze for 3rd */
    ImVec4 placeColor;
    if      (placement == 0) placeColor = ImVec4(1.0f, 0.85f, 0.15f, 1.0f); /* gold   */
    else if (placement == 1) placeColor = ImVec4(0.8f, 0.8f,  0.85f, 1.0f); /* silver */
    else if (placement == 2) placeColor = ImVec4(0.8f, 0.5f,  0.2f,  1.0f); /* bronze */
    else                     placeColor = ImVec4(0.7f, 0.7f,  0.7f,  1.0f);

    ImGui::Spacing();

    /* Big placement label */
    ImGui::SetCursorPosX((contentW - ImGui::CalcTextSize(placementStr).x * 2.5f) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, placeColor);
    float origScale = ImGui::GetIO().FontGlobalScale;
    ImGui::GetIO().FontGlobalScale = origScale * 2.5f * scale;
    ImGui::Text("%s", placementStr);
    ImGui::GetIO().FontGlobalScale = origScale;
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Title */
    if (title && title[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.65f, 0.85f, 1.0f));
        ImGui::Text("Title");
        ImGui::PopStyleColor();
        ImGui::SameLine(pdguiScale(90.0f));
        if (titleChanged) {
            /* Animate: oscillate between two gold tones to draw attention */
            float t = (float)(SDL_GetTicks() % 800) / 800.0f;
            float pulse = (t < 0.5f) ? t * 2.0f : (1.0f - t) * 2.0f;
            ImVec4 titleColor = ImVec4(1.0f, 0.7f + pulse * 0.3f, pulse * 0.5f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, titleColor);
            ImGui::Text("%s  [NEW!]", title);
            ImGui::PopStyleColor();
        } else {
            ImGui::Text("%s", title);
        }
    }

    /* Weapon of Choice */
    if (weapon && weapon[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.65f, 0.85f, 1.0f));
        ImGui::Text("Weapon");
        ImGui::PopStyleColor();
        ImGui::SameLine(pdguiScale(90.0f));
        ImGui::Text("%s", weapon);
    }

    /* Awards */
    if (award1 && award1[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.65f, 0.85f, 1.0f));
        ImGui::Text("Award");
        ImGui::PopStyleColor();
        ImGui::SameLine(pdguiScale(90.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::Text("%s", award1);
        ImGui::PopStyleColor();
    }
    if (award2 && award2[0]) {
        ImGui::Text("     ");
        ImGui::SameLine(pdguiScale(90.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::Text("%s", award2);
        ImGui::PopStyleColor();
    }

    /* Medals */
    bool hasMedal = false;
    for (s32 m = 0; m < 4; m++) {
        if (medals & (1u << m)) { hasMedal = true; break; }
    }
    if (hasMedal) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.65f, 0.85f, 1.0f));
        ImGui::Text("Medals");
        ImGui::PopStyleColor();
        ImGui::SameLine(pdguiScale(90.0f));

        float squareSize = pdguiScale(14.0f);
        float spacing    = pdguiScale(6.0f);
        ImDrawList *draw = ImGui::GetWindowDrawList();

        for (s32 m = 0; m < 4; m++) {
            if (!(medals & (1u << m))) continue;
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec4 c = s_MedalDefs[m].color;
            ImU32 col = IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),220);
            draw->AddRectFilled(cursor,
                ImVec2(cursor.x + squareSize, cursor.y + squareSize), col, 2.0f);
            draw->AddRect(cursor,
                ImVec2(cursor.x + squareSize, cursor.y + squareSize),
                IM_COL32(255,255,255,80), 2.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + squareSize + spacing);
            ImGui::SameLine(0, 0);

            /* Tooltip on hover */
            ImGui::InvisibleButton(s_MedalDefs[m].label,
                ImVec2(squareSize, squareSize));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", s_MedalDefs[m].label);
            }
            ImGui::SameLine(0, spacing);
        }
        ImGui::NewLine();
    }
}

void pdguiGameOverRender(s32 winW, s32 winH)
{
    if (pdguiPauseGetPaused() != MPPAUSEMODE_GAMEOVER) return;
    if (!pdguiPauseGetNormMplayerIsRunning()) return;

    /* Reset tab selection each time game over activates so it always starts
     * on Rankings (the immediately useful view). Only reset when transitioning
     * into GAMEOVER, not on every frame. */
    static u8 s_prevWasGameOver = 0;
    if (!s_prevWasGameOver) {
        s_GameOverTab = 0;
        s_prevWasGameOver = 1;
    }
    /* When no longer in game over (e.g. after returning to lobby), clear flag */
    /* Note: this function exits early above when not in GAMEOVER, so the flag
     * will stay set for the duration of the GAMEOVER state and clear on next
     * non-GAMEOVER frame via the early return. */

    pdguiSetPalette(2);

    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), disp, IM_COL32(0, 0, 0, 168));

    ScorecardRow rows[MAX_MPCHRS_PM];
    s32 count = buildScorecardData(rows, MAX_MPCHRS_PM);
    u32 options     = pdguiPauseGetOptions();
    bool teamsEnabled = (options & MPOPTION_TEAMSENABLED) != 0;
    s32 challengeStatus = pdguiEndscreenGetChallengeStatus();

    /* Sort by team when teams are enabled so section headers are contiguous */
    if (teamsEnabled && count > 1) {
        sortRowsByTeam(rows, count);
    }

    float scale     = pdguiScaleFactor();
    float padX      = pdguiScale(16.0f);
    float titleH    = pdguiScale(42.0f);
    float challengeH = (challengeStatus > 0) ? pdguiScale(30.0f) : 0.0f;
    float tabBarH   = pdguiScale(30.0f);
    float tabGapH   = pdguiScale(6.0f);
    float rowH      = pdguiScale(21.0f);
    float btnH      = pdguiScale(36.0f);
    float bottomPad = pdguiScale(14.0f);

    /* Content area height: large enough for max(rankings, personal).
     * When teams are enabled, add extra vertical space for team section headers. */
    s32 numTeamHeaders = 0;
    if (teamsEnabled && count > 0) {
        s32 prevT = -1;
        for (s32 ri = 0; ri < count; ri++) {
            if ((s32)rows[ri].team != prevT) { prevT = (s32)rows[ri].team; numTeamHeaders++; }
        }
    }
    float rankingsH  = pdguiScale(22.0f) + (count > 0 ? count * rowH : rowH)
                     + numTeamHeaders * rowH + pdguiScale(8.0f);
    float personalH  = pdguiScale(160.0f); /* enough for all personal fields */
    float contentH   = (rankingsH > personalH) ? rankingsH : personalH;

    float menuH = titleH + challengeH + tabBarH + tabGapH + contentH + btnH + bottomPad;
    float menuW = disp.x * 0.52f;
    float minW  = pdguiScale(440.0f);
    float maxW  = pdguiScale(700.0f);
    if (menuW < minW) menuW = minW;
    if (menuW > maxW) menuW = maxW;
    float menuX = (disp.x - menuW) * 0.5f;
    float menuY = (disp.y - menuH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoBackground
                           | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, pdguiScale(8.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(pdguiScale(6.0f), pdguiScale(4.0f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,     ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (ImGui::Begin("##PdGameOver", NULL, flags)) {
        /* Release mouse grab on first appear so buttons are clickable.
         * The game holds SDL in relative mode during active gameplay. */
        if (ImGui::IsWindowAppearing()) {
            pdmainSetInputMode(INPUTMODE_MENU);
        }

        pdguiDrawPdDialog(menuX, menuY, menuW, menuH, "MATCH OVER", 1);

        /* Challenge outcome banner */
        if (challengeStatus > 0) {
            ImGui::SetCursorPos(ImVec2(padX, titleH));
            float bannerW = menuW - padX * 2;

            ImVec4 bannerBg, bannerText;
            const char *bannerLabel = "Challenge";
            switch (challengeStatus) {
            case 1:
                bannerBg   = ImVec4(0.05f, 0.35f, 0.05f, 0.85f);
                bannerText = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
                bannerLabel = "Challenge Completed!";
                break;
            case 3:
                bannerBg   = ImVec4(0.35f, 0.15f, 0.0f, 0.85f);
                bannerText = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
                bannerLabel = "Challenge Cheated!";
                break;
            default: /* 2 = failed */
                bannerBg   = ImVec4(0.35f, 0.04f, 0.04f, 0.85f);
                bannerText = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
                bannerLabel = "Challenge Failed";
                break;
            }

            ImVec2 bmin = ImGui::GetCursorScreenPos();
            ImVec2 bmax = ImVec2(bmin.x + bannerW, bmin.y + challengeH);
            ImGui::GetWindowDrawList()->AddRectFilled(bmin, bmax,
                IM_COL32((int)(bannerBg.x*255),(int)(bannerBg.y*255),
                          (int)(bannerBg.z*255),(int)(bannerBg.w*255)), 3.0f);
            ImGui::GetWindowDrawList()->AddRect(bmin, bmax,
                IM_COL32((int)(bannerText.x*255),(int)(bannerText.y*255),
                          (int)(bannerText.z*255),120), 3.0f);

            float textW = ImGui::CalcTextSize(bannerLabel).x;
            ImGui::SetCursorPosX((menuW - textW) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, bannerText);
            ImGui::Text("%s", bannerLabel);
            ImGui::PopStyleColor();
        }

        /* Tab bar */
        float tabY = titleH + challengeH + tabGapH;
        ImGui::SetCursorPos(ImVec2(padX, tabY));

        float tabW = (menuW - padX * 2 - pdguiScale(6.0f)) * 0.5f;
        ImVec2 tabSz(tabW, tabBarH);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pdguiScale(6.0f), 0.0f));

        /* Rankings tab */
        if (s_GameOverTab == 0) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.25f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.25f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.25f, 0.5f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.1f, 0.1f, 0.18f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.2f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.2f, 0.3f, 0.55f, 1.0f));
        }
        if (ImGui::Button("Rankings##go", tabSz)) {
            s_GameOverTab = 0;
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        /* Personal tab */
        if (s_GameOverTab == 1) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.25f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.25f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.25f, 0.5f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.1f, 0.1f, 0.18f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.2f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.2f, 0.3f, 0.55f, 1.0f));
        }
        if (ImGui::Button("Personal##go", tabSz)) {
            s_GameOverTab = 1;
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }
        ImGui::PopStyleColor(3);

        ImGui::PopStyleVar(); /* ItemSpacing */

        /* Tab content */
        float contentTop = tabY + tabBarH + pdguiScale(6.0f);
        ImGui::SetCursorPos(ImVec2(padX, contentTop));

        float childW = menuW - padX * 2;
        ImGui::BeginChild("##GameOverContent", ImVec2(childW, contentH), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        switch (s_GameOverTab) {
        case 0: renderGameOverRankings(childW, count, rows, teamsEnabled); break;
        case 1: renderGameOverPersonal(childW); break;
        }
        ImGui::EndChild();

        /* Action buttons — pinned at bottom, two side-by-side */
        float btnY = menuH - btnH - bottomPad;
        ImGui::SetCursorPos(ImVec2(padX, btnY));
        ImGui::Separator();
        ImGui::Spacing();

        float contentW2 = menuW - padX * 2.0f;
        float twoGap    = pdguiScale(8.0f);
        float twoW      = (contentW2 - twoGap) * 0.5f;
        float twoH      = pdguiScale(28.0f);

        ImGui::SetCursorPosX(padX);

        /* --- Return to Lobby: end match, stay connected, show room screen --- */
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.08f, 0.25f, 0.55f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.40f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.25f, 0.55f, 1.00f, 1.0f));
        if (ImGui::Button("Return to Lobby##go", ImVec2(twoW, twoH))) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            s_prevWasGameOver = 0;
            mpSetPaused(MPPAUSEMODE_UNPAUSED);
            pdmainSetInputMode(INPUTMODE_MENU);
            mainChangeToStage(0x26); /* STAGE_CITRAINING — lobby hub */
            if (g_NetMode != NETMODE_NONE) {
                pdguiSetInRoom(1);   /* remain in room, show room screen */
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, twoGap);

        /* --- Quit to Menu: disconnect (online) or go to title (offline) --- */
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.08f, 0.08f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Quit to Menu##go", ImVec2(twoW, twoH))) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            s_prevWasGameOver = 0;
            mpSetPaused(MPPAUSEMODE_UNPAUSED);
            pdmainSetInputMode(INPUTMODE_MENU);
            if (g_NetMode == NETMODE_CLIENT) {
                netDisconnect(); /* handles mainEndStage + mainChangeToStage(CITRAINING) */
            } else {
                mainChangeToStage(0x5a); /* STAGE_TITLE — offline or server: main menu */
            }
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    /* Clear reset flag when leaving GAMEOVER state */
    /* (already handled: early return at top clears s_prevWasGameOver on next entry) */
}

/* Companion: clear the game-over tab state on stage transition so the
 * next match always opens on Rankings. Called from pdguiMpIngameReset(). */
void pdguiGameOverResetTab(void)
{
    s_GameOverTab = 0;
}
