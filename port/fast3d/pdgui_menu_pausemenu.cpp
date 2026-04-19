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
#include "pdgui_layout.h" /* M-6: pdguiPopupDarkenBehind */
#include "system.h"
#include "inputctx.h"
#include "actionmap.h"

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
void pdguiEndscreenExitToMainMenu(void);
void pdguiSoloRoomReturn(void);

/* Mouse */
s32 inputMouseIsLocked(void);

/* S297 content-inset API — pull content off the chrome nineslice border. */
void pdguiThemeGetContentInset(float *out_l, float *out_r,
                               float *out_t, float *out_b);

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
    /* PRIMARY: catalog ID strings — must match types.h mpchrconfig layout */
    char head_id[64];
    char body_id[64];
    /*0x0f*/ u8 mpheadnum; /* DEPRECATED */
    /*0x10*/ u8 mpbodynum; /* DEPRECATED */
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
u64 pdguiPauseGetChrSlots(void);
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
/* M-6 (Menu Stack Compliance Tier 1): End Game confirm modal state. Replaces
 * the prior inline "Confirm?"/"Cancel" toggle with a proper BeginPopupModal
 * using the canonical S385 pattern (5-frame SetKeyboardFocusHere(0) on Cancel
 * + 3-frame input debounce). s_EndGameConfirm now means "popup is armed";
 * s_EndGameOpenFrame tracks the ImGui frame at which OpenPopup fired. See
 * context/designs/menu-stack-architecture.md §C4/§C5. */
static bool s_EndGameConfirm = false;
static s32  s_EndGameOpenFrame = -1;
#define ENDGAME_PM_FRAME_DEBOUNCE     3
#define ENDGAME_PM_FORCE_FOCUS_FRAMES 5
static s32 s_GameOverTab = 0;  /* 0=Rankings, 1=Personal */

/* Simple SDL-based cooldown to prevent double-press (replaces menumgr) */
static Uint32 s_PauseCooldownUntil = 0;
#define PAUSE_COOLDOWN_MS 100

static bool s_pauseInCooldown(void) {
    return SDL_GetTicks() < s_PauseCooldownUntil;
}

static void s_pauseSetCooldown(void) {
    s_PauseCooldownUntil = SDL_GetTicks() + PAUSE_COOLDOWN_MS;
}

/* ========================================================================
 * Pause Menu API (C-callable)
 * ======================================================================== */

void pdguiPauseMenuOpen(void)
{
    if (s_pauseInCooldown()) return; /* prevent double-press */

    /* Push pause context — handles mouse release and game pause via on_push.
     * F-1.1: Removed direct SDL_WarpMouseInWindow call that pre-dated
     * inputCtxSyncMouseMode(). The context system's on_push callback for
     * g_CtxPauseMenu now handles mouse mode transition exclusively. */
    inputCtxPush(&g_CtxPauseMenu);

    s_PauseMenuOpen = true;
    s_PauseJustOpened = true;
    s_PauseTab = 0;
    s_EndGameConfirm = false;
    s_EndGameOpenFrame = -1;

    s_pauseSetCooldown();

    /* Pause the game (single-player combat sim only -- network handles differently) */
    if (g_NetMode == NETMODE_NONE) {
        mpSetPaused(MPPAUSEMODE_PAUSED);
    }
}

void pdguiPauseMenuClose(void)
{
    if (s_pauseInCooldown()) return; /* B-124 fix: prevent double-fire close within cooldown */

    s_PauseMenuOpen = false;
    s_EndGameConfirm = false;
    s_EndGameOpenFrame = -1;

    /* Pop pause context — gameplay context's on_push restores mouse capture. */
    if (inputCtxIsActive(&g_CtxPauseMenu)) {
        inputCtxPopDeferred(&g_CtxPauseMenu);
    }

    s_pauseSetCooldown();

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

/* Team color table — shared by rankings tab, overlay, and game-over screen */
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

/* Dim version of team colors for row backgrounds */
static ImVec4 teamRowBg(u8 team, bool isPlayer)
{
    if (team >= 8) team = 7;
    ImVec4 c = s_TeamColors[team];
    float a = isPlayer ? 0.25f : 0.12f;
    return ImVec4(c.x, c.y, c.z, a);
}

static void renderRankingsTab(float contentW)
{
    ScorecardRow rows[MAX_MPCHRS_PM];
    s32 count = buildScorecardData(rows, MAX_MPCHRS_PM);

    u32 options = pdguiPauseGetOptions();
    bool teamsEnabled = (options & MPOPTION_TEAMSENABLED) != 0;

    s32 numCols = teamsEnabled ? 6 : 5;
    ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit
                           | ImGuiTableFlags_NoBordersInBodyUntilResize
                           | ImGuiTableFlags_PadOuterX;

    if (!ImGui::BeginTable("##rankings_tbl", numCols, tflags)) return;

    /* Setup columns */
    ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, pdguiScale(32.0f));
    ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
    if (teamsEnabled)
        ImGui::TableSetupColumn("Team",  ImGuiTableColumnFlags_WidthFixed, pdguiScale(50.0f));
    ImGui::TableSetupColumn("Score",  ImGuiTableColumnFlags_WidthFixed, pdguiScale(60.0f));
    ImGui::TableSetupColumn("Kills",  ImGuiTableColumnFlags_WidthFixed, pdguiScale(55.0f));
    ImGui::TableSetupColumn("Deaths", ImGuiTableColumnFlags_WidthFixed, pdguiScale(60.0f));

    /* Header row */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();

    for (s32 i = 0; i < count; i++) {
        ImGui::TableNextRow();

        /* Team-colored row background (entire row) */
        if (teamsEnabled) {
            ImVec4 bg = teamRowBg(rows[i].team, rows[i].isPlayer);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                   ImGui::GetColorU32(bg));
        } else if (rows[i].isPlayer) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                   IM_COL32(255, 230, 100, 40));
        }

        char buf[32];

        /* # */
        ImGui::TableNextColumn();
        snprintf(buf, sizeof(buf), "%d.", i + 1);
        ImGui::TextUnformatted(buf);

        /* Name */
        ImGui::TableNextColumn();
        if (rows[i].isPlayer) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.7f, 1.0f));
            ImGui::TextUnformatted(rows[i].name);
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(rows[i].name);
        }

        /* Team */
        if (teamsEnabled) {
            ImGui::TableNextColumn();
            u8 team = rows[i].team;
            if (team >= 8) team = 7;
            ImGui::PushStyleColor(ImGuiCol_Text, s_TeamColors[team]);
            snprintf(buf, sizeof(buf), "T%d", team + 1);
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
        }

        /* Score */
        ImGui::TableNextColumn();
        snprintf(buf, sizeof(buf), "%d", rows[i].score);
        ImGui::TextUnformatted(buf);

        /* Kills */
        ImGui::TableNextColumn();
        snprintf(buf, sizeof(buf), "%d", rows[i].kills);
        ImGui::TextUnformatted(buf);

        /* Deaths */
        ImGui::TableNextColumn();
        snprintf(buf, sizeof(buf), "%d", rows[i].deaths);
        ImGui::TextUnformatted(buf);
    }

    ImGui::EndTable();
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
    u64 activeMask = pdguiPauseGetChrSlots();
    s32 numPlayers = 0, numBots = 0;
    for (s32 i = 0; i < MAX_MPCHRS_PM; i++) {
        if (activeMask & (1ull << i)) {
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

        /* Inset content area — S297 chrome-aware padding.  The base values
         * (24px / 60px) assume the procedural dialog + title bar; large
         * user chrome corners could otherwise clip buttons and the
         * Resume button against the bottom border. */
        float insL = 0, insR = 0, insT = 0, insB = 0;
        pdguiThemeGetContentInset(&insL, &insR, &insT, &insB);
        float padX = pdguiScale(24.0f);
        float padY = pdguiScale(60.0f); /* below title */
        if (insL > padX) padX = insL;
        if (insT + pdguiScale(28.0f) > padY) padY = insT + pdguiScale(28.0f);
        float padR = pdguiScale(24.0f);
        if (insR > padR) padR = insR;
        float padB = pdguiScale(14.0f);
        if (insB > padB) padB = insB;

        ImGui::SetCursorPos(ImVec2(padX, padY));

        /* Tab buttons across the top */
        float tabW = (menuW - padX - padR - pdguiScale(12.0f) * 2) / 3.0f;
        ImVec2 tabSize(tabW, pdguiScale(42.0f));

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pdguiScale(12.0f), pdguiScale(12.0f)));

        if (PdPauseButton("Rankings##pm", tabSize)) { s_PauseTab = 0; pdguiPlaySound(PDGUI_SND_FOCUS); }
        ImGui::SameLine();
        if (PdPauseButton("Settings##pm", tabSize)) { s_PauseTab = 1; pdguiPlaySound(PDGUI_SND_FOCUS); }
        ImGui::SameLine();

        /* End Game button — danger styled. M-6: click arms a proper
         * BeginPopupModal confirm (canonical C4/C5 pattern). The modal
         * renders below near the Resume button, after the tab content. */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        if (PdPauseButton("End Game##pm", tabSize)) {
            if (!s_EndGameConfirm) {
                s_EndGameConfirm = true;
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::PopStyleVar(); /* ItemSpacing */

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* Tab content area (scrollable) */
        float contentTop = ImGui::GetCursorPosY();
        float resumeH = pdguiScale(36.0f);
        float resumeSpacing = pdguiScale(14.0f);
        float contentH = menuH - contentTop - resumeH - padB - resumeSpacing;
        float contentW = menuW - padX - padR;

        ImGui::BeginChild("##PauseTabContent", ImVec2(contentW, contentH), false);

        switch (s_PauseTab) {
        case 0: renderRankingsTab(contentW); break;
        case 1: renderSettingsTab(); break;
        }

        ImGui::EndChild();

        /* Resume button at bottom center */
        float resumeW = pdguiScale(180.0f);
        ImGui::SetCursorPos(ImVec2((menuW - resumeW) * 0.5f, menuH - resumeH - padB));
        if (PdPauseButton("Resume##pm", ImVec2(resumeW, resumeH))) {
            pdguiPauseMenuClose();
        }

        /* M-6: End Game confirm modal — canonical S385 pattern. Click the
         * End Game tab button above to arm (sets s_EndGameConfirm); this
         * modal then opens over the pause menu. pdguiPopupDarkenBehind
         * scrims the whole scene, 5-frame SetKeyboardFocusHere(0) locks
         * focus onto Cancel, 3-frame input debounce swallows any Enter/A
         * press that bled through from the armed button. */
        bool endgamePopupWasOpen = s_EndGameConfirm;
        {
            const char *endgamePopupId = "End Match?##pm_endgame_confirm";
            if (s_EndGameConfirm && !ImGui::IsPopupOpen(endgamePopupId)) {
                ImGui::OpenPopup(endgamePopupId);
                s_EndGameOpenFrame = (s32)ImGui::GetFrameCount();
            }

            ImGui::SetNextWindowSize(ImVec2(pdguiScale(460.0f), 0.0f));
            if (ImGui::BeginPopupModal(endgamePopupId, nullptr,
                                        ImGuiWindowFlags_AlwaysAutoResize)) {
                pdguiPopupDarkenBehind(0.65f);

                s32 curFrame   = (s32)ImGui::GetFrameCount();
                s32 framesOpen = (s_EndGameOpenFrame >= 0)
                                 ? (curFrame - s_EndGameOpenFrame)
                                 : ENDGAME_PM_FORCE_FOCUS_FRAMES + 1;
                bool forceFocus     = (framesOpen >= 0 && framesOpen < ENDGAME_PM_FORCE_FOCUS_FRAMES);
                bool inputDebounced = (framesOpen >= 0 && framesOpen < ENDGAME_PM_FRAME_DEBOUNCE);

                ImGui::TextColored(pdguiVec4TitleGlow(), "End Match?");
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextWrapped("End the current match? "
                                   "Final rankings will be shown.");
                ImGui::Spacing();

                float bw = pdguiScale(150.0f);
                bool doConfirm = false;
                bool doCancel  = false;

                /* Cancel first — safer default focus. */
                if (forceFocus) ImGui::SetKeyboardFocusHere(0);
                if (ImGui::Button("Cancel##pmendgame", ImVec2(bw, 0.0f))) {
                    if (!inputDebounced) doCancel = true;
                }
                ImGui::SetItemDefaultFocus();

                ImGui::SameLine();

                /* Red "End Match" confirm. */
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.20f, 0.20f, 1.0f));
                if (ImGui::Button("End Match##pmendgame", ImVec2(bw, 0.0f))) {
                    if (!inputDebounced) doConfirm = true;
                }
                ImGui::PopStyleColor(3);

                if (!inputDebounced) {
                    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                        doConfirm = true;
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                        doCancel = true;
                    }
                }

                if (doConfirm) {
                    /* GAP-3: route BOTH offline and online through mainEndStage
                     * so the player sees final rankings/awards before returning
                     * to the main menu. The MP endscreen's "Disconnect" button
                     * already calls netDisconnect for the teardown once the
                     * player has seen results. */
                    pdguiPlaySound(PDGUI_SND_SELECT);
                    pdguiPauseSetPlayerAborted();
                    mainEndStage();
                    s_EndGameConfirm = false;
                    s_EndGameOpenFrame = -1;
                    ImGui::CloseCurrentPopup();
                    pdguiPauseMenuClose();
                } else if (doCancel) {
                    pdguiPlaySound(PDGUI_SND_KBCANCEL);
                    s_EndGameConfirm = false;
                    s_EndGameOpenFrame = -1;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            } else if (s_EndGameConfirm) {
                /* Popup was closed externally (hotswap / stage transition). */
                s_EndGameConfirm = false;
                s_EndGameOpenFrame = -1;
            }
        }

        /* B-14 fix: On the frame the menu opens, the legacy path (bondmove→
         * mpPushPauseDialog→ingame.c) already opened us. ImGui also sees
         * the same START press via polling. Skip close checks this frame
         * to prevent open+close in one tick.
         *
         * M-6: also skip when the End Game confirm modal was open at frame
         * start — the modal absorbs Escape, so the parent should not
         * double-consume the same press and close the pause menu. */
        if (s_PauseJustOpened) {
            s_PauseJustOpened = false;
        } else if (!endgamePopupWasOpen) {
            /* S311: title X button or Escape closes (X channel avoids
             * the one-frame-swallow class that needed two clicks). */
            if (pdguiConsumeTitleClose() ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                pdguiPauseMenuClose();
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

    /* Action map: Tab / Back → ACTION_SCORECARD (hold-to-show).
     * Replaces parallel SDL_GetKeyboardState + ImGui::IsKeyDown paths. */
    s_ScorecardVisible = actionHeld(0, ACTION_SCORECARD) != 0;
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
    float minW = pdguiScale(540.0f);
    if (boardW < minW) boardW = minW;
    float rowH    = pdguiScale(33.0f);
    float headerH = pdguiScale(45.0f);
    float padding = pdguiScale(12.0f);
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

        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
        ImGui::Text("SCOREBOARD");
        ImGui::SameLine(boardW - pdguiScale(100.0f));
        ImGui::Text("%s", timebuf);
        ImGui::PopStyleColor();

        ImGui::Separator();

        /* Table-based layout for proper column alignment */
        s32 numCols = teamsEnabled ? 6 : 5;
        ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit
                               | ImGuiTableFlags_PadOuterX;

        if (ImGui::BeginTable("##sc_tbl", numCols, tflags)) {
            ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, pdguiScale(28.0f));
            ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
            if (teamsEnabled)
                ImGui::TableSetupColumn("T",  ImGuiTableColumnFlags_WidthFixed, pdguiScale(36.0f));
            ImGui::TableSetupColumn("Score",  ImGuiTableColumnFlags_WidthFixed, pdguiScale(52.0f));
            ImGui::TableSetupColumn("K",      ImGuiTableColumnFlags_WidthFixed, pdguiScale(40.0f));
            ImGui::TableSetupColumn("D",      ImGuiTableColumnFlags_WidthFixed, pdguiScale(40.0f));

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.6f, 0.8f, 0.9f));
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            for (s32 i = 0; i < count; i++) {
                ImGui::TableNextRow();

                /* Team-colored row background */
                if (teamsEnabled) {
                    ImVec4 bg = teamRowBg(rows[i].team, rows[i].isPlayer);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                           ImGui::GetColorU32(bg));
                } else if (rows[i].isPlayer) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                           IM_COL32(255, 230, 100, 35));
                }

                char buf[32];

                ImGui::TableNextColumn();
                snprintf(buf, sizeof(buf), "%d.", i + 1);
                ImGui::TextUnformatted(buf);

                ImGui::TableNextColumn();
                if (rows[i].isPlayer) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.7f, 1.0f));
                    ImGui::TextUnformatted(rows[i].name);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextUnformatted(rows[i].name);
                }

                if (teamsEnabled) {
                    ImGui::TableNextColumn();
                    u8 team = rows[i].team;
                    if (team >= 8) team = 7;
                    ImGui::PushStyleColor(ImGuiCol_Text, s_TeamColors[team]);
                    snprintf(buf, sizeof(buf), "T%d", team + 1);
                    ImGui::TextUnformatted(buf);
                    ImGui::PopStyleColor();
                }

                ImGui::TableNextColumn();
                snprintf(buf, sizeof(buf), "%d", rows[i].score);
                ImGui::TextUnformatted(buf);

                ImGui::TableNextColumn();
                snprintf(buf, sizeof(buf), "%d", rows[i].kills);
                ImGui::TextUnformatted(buf);

                ImGui::TableNextColumn();
                snprintf(buf, sizeof(buf), "%d", rows[i].deaths);
                ImGui::TextUnformatted(buf);
            }

            ImGui::EndTable();
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
    s32 numCols = teamsEnabled ? 7 : 6;
    ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit
                           | ImGuiTableFlags_PadOuterX;

    if (!ImGui::BeginTable("##go_rank_tbl", numCols, tflags)) return;

    ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, pdguiScale(28.0f));
    ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
    if (teamsEnabled)
        ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, pdguiScale(44.0f));
    ImGui::TableSetupColumn("Score",  ImGuiTableColumnFlags_WidthFixed, pdguiScale(56.0f));
    ImGui::TableSetupColumn("K",      ImGuiTableColumnFlags_WidthFixed, pdguiScale(40.0f));
    ImGui::TableSetupColumn("D",      ImGuiTableColumnFlags_WidthFixed, pdguiScale(40.0f));
    ImGui::TableSetupColumn("Acc%",   ImGuiTableColumnFlags_WidthFixed, pdguiScale(52.0f));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.65f, 0.85f, 1.0f));
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();

    s32 prevTeam = -1;
    for (s32 i = 0; i < count; i++) {
        /* Team section header — emitted each time the team number changes */
        if (teamsEnabled && (s32)rows[i].team != prevTeam) {
            prevTeam = (s32)rows[i].team;
            u8 hteam = rows[i].team < 8 ? rows[i].team : 7;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, s_TeamColors[hteam]);
            ImGui::Text("  -- Team %d --", hteam + 1);
            ImGui::PopStyleColor();
        }

        ImGui::TableNextRow();

        /* Team-colored row background */
        if (teamsEnabled) {
            ImVec4 bg = teamRowBg(rows[i].team, rows[i].isPlayer);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                   ImGui::GetColorU32(bg));
        } else if (i == 0) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                   IM_COL32(255, 210, 50, 40));
        } else if (rows[i].isPlayer) {
            /* S311: local-player row tint tracks theme title glow. */
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                   pdguiImU32TitleGlow(35));
        }

        /* Name color: gold for 1st, cyan for local player, white for others */
        ImVec4 nameColor;
        if (i == 0) {
            nameColor = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
        } else if (rows[i].isPlayer) {
            nameColor = pdguiVec4TitleGlow();
        } else {
            nameColor = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        }

        char buf[32];

        ImGui::TableNextColumn();
        snprintf(buf, sizeof(buf), "%d.", i + 1);
        ImGui::TextUnformatted(buf);

        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, nameColor);
        ImGui::TextUnformatted(rows[i].name);
        ImGui::PopStyleColor();

        if (teamsEnabled) {
            ImGui::TableNextColumn();
            u8 team = rows[i].team < 8 ? rows[i].team : 7;
            ImGui::PushStyleColor(ImGuiCol_Text, s_TeamColors[team]);
            snprintf(buf, sizeof(buf), "T%d", team + 1);
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
        }

        ImGui::TableNextColumn();
        snprintf(buf, sizeof(buf), "%d", rows[i].score);
        ImGui::TextUnformatted(buf);

        ImGui::TableNextColumn();
        snprintf(buf, sizeof(buf), "%d", rows[i].kills);
        ImGui::TextUnformatted(buf);

        ImGui::TableNextColumn();
        snprintf(buf, sizeof(buf), "%d", rows[i].deaths);
        ImGui::TextUnformatted(buf);

        ImGui::TableNextColumn();
        if (rows[i].accuracy >= 0.0f) {
            snprintf(buf, sizeof(buf), "%.1f", rows[i].accuracy);
        } else {
            buf[0] = '-'; buf[1] = '-'; buf[2] = '\0';
        }
        ImGui::TextUnformatted(buf);
    }

    ImGui::EndTable();
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
        ImGui::SameLine(pdguiScale(135.0f));
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
        ImGui::SameLine(pdguiScale(135.0f));
        ImGui::Text("%s", weapon);
    }

    /* Awards */
    if (award1 && award1[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.65f, 0.85f, 1.0f));
        ImGui::Text("Award");
        ImGui::PopStyleColor();
        ImGui::SameLine(pdguiScale(135.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::Text("%s", award1);
        ImGui::PopStyleColor();
    }
    if (award2 && award2[0]) {
        ImGui::Text("     ");
        ImGui::SameLine(pdguiScale(135.0f));
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
        ImGui::SameLine(pdguiScale(135.0f));

        float squareSize = pdguiScale(21.0f);
        float spacing    = pdguiScale(9.0f);
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
    /* B-45 legacy: this standalone renderer was the only GAMEOVER screen before
     * pdgui_menu_endscreen.cpp existed.  Now the endscreen hotswap renderers
     * (mpGameOverIndRender / mpGameOverTeamRender) handle everything — they
     * provide rankings, stats, awards, input context push, and action buttons.
     * Running both produces two overlapping ImGui windows fighting for focus,
     * which is the root cause of the post-match "stuck" bug.
     *
     * S295 F2: The previous #if 0 body (~250 lines) was deleted. It contained a
     * stray inputCtxPush(&g_CtxImGuiMenu) that made push/pop audits misleading.
     * See git history (d8c75373 and earlier) if you need to review the old body. */
    (void)winW;
    (void)winH;
}

/* Companion: clear the game-over tab state on stage transition so the
 * next match always opens on Rankings. Called from pdguiMpIngameReset(). */
void pdguiGameOverResetTab(void)
{
    s_GameOverTab = 0;
}
