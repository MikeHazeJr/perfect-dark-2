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

/* End game */
void mainEndStage(void);
void netDisconnect(void);

/* Game state */
struct vars_opaque;
extern s32 g_MainIsEndscreen;

/* Player index for stat lookups */
extern s32 g_MpPlayerNum;

/* Ranking system */
#define MAX_PLAYERS_PM     8
#define MAX_BOTS_PM       24
#define MAX_MPCHRS_PM     (MAX_PLAYERS_PM + MAX_BOTS_PM)

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
    /*0x24*/ s16 killcounts[MAX_MPCHRS_PM];
    /*0x64*/ s16 numdeaths;       /* offset depends on MAX_MPCHRS value */
    /*0x66*/ s16 numpoints;
    /*0x68*/ s16 unk40;
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

/* ========================================================================
 * Pause Menu API (C-callable)
 * ======================================================================== */

void pdguiPauseMenuOpen(void)
{
    s_PauseMenuOpen = true;
    s_PauseJustOpened = true; /* B-14: skip close checks this frame */
    s_PauseTab = 0;
    s_EndGameConfirm = false;

    /* Pause the game (single-player combat sim only — network handles differently) */
    if (g_NetMode == NETMODE_NONE) {
        mpSetPaused(MPPAUSEMODE_PAUSED);
    }
}

void pdguiPauseMenuClose(void)
{
    s_PauseMenuOpen = false;
    s_EndGameConfirm = false;

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
    bool isPlayer; /* true if slot < MAX_PLAYERS */
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
    }

    return count;
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

        if (PdPauseButton("Rankings", tabSize)) { s_PauseTab = 0; pdguiPlaySound(PDGUI_SND_FOCUS); }
        ImGui::SameLine();
        if (PdPauseButton("Settings", tabSize)) { s_PauseTab = 1; pdguiPlaySound(PDGUI_SND_FOCUS); }
        ImGui::SameLine();

        /* End Game button — danger styled */
        if (!s_EndGameConfirm) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            if (PdPauseButton("End Game", tabSize)) {
                s_EndGameConfirm = true;
            }
            ImGui::PopStyleColor(3);
        } else {
            /* Confirmation state: show "Confirm?" / "Cancel" */
            float halfTab = (tabW - 4.0f) * 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.05f, 0.05f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (PdPauseButton("Confirm?", ImVec2(halfTab, 28.0f))) {
                /* Actually end the game */
                pdguiPauseSetPlayerAborted();
                if (g_NetMode == NETMODE_CLIENT) {
                    netDisconnect();
                } else {
                    mainEndStage();
                }
                s_PauseMenuOpen = false;
                s_EndGameConfirm = false;
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (PdPauseButton("Cancel", ImVec2(halfTab, 28.0f))) {
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
        if (PdPauseButton("Resume", ImVec2(resumeW, resumeH))) {
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
