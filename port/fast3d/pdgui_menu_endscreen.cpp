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
#include "pdgui_layout.h"
#include "system.h"
#include "inputctx.h"
#include "menupool.h"
#include "achievements.h"
#include "pdgui_achievement_toast.h"

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

/* MP layout constants — pulled from the C++-safe mirror header.
 * See pdgui_constants.h for why direct constants.h inclusion doesn't work
 * from C++ and how drift is caught at build time. */
#include "pdgui_constants.h"

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

/* S297 content-inset API: pull content in from the dialog edge so it never
 * overlaps the chrome nineslice border / title bar artwork. */
void pdguiThemeGetContentInset(float *out_l, float *out_r,
                               float *out_t, float *out_b);

/* MP rankings — layout-compatible with pause menu */
struct mpchrconfig_es {
    char name[15];
    /* PRIMARY: catalog ID strings — must match types.h mpchrconfig layout */
    char head_id[64];
    char body_id[64];
    u8 mpheadnum; /* DEPRECATED */
    u8 mpbodynum; /* DEPRECATED */
    u8 team;
    u8 _pad0[2];
    u32 displayoptions;
    u16 unk18;
    u16 unk1a;
    u16 unk1c;
    s8 placement;
    u8 _pad1;
    s32 rankablescore;
    s16 killcounts[MAX_MPCHRS];
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

/* Player management */
void setCurrentPlayerNum(s32 playernum);

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

/* Config persistence */
s32 configSave(const char *fname);

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

/* S297: Resolve effective inner padding for a PD dialog so content clears the
 * active chrome nineslice border.  The defaults (27px / 54px) are designed for
 * the procedural dialog + title bar; a user chrome style may have larger
 * border corners, which without this adjustment would clip rankings/stats
 * into the frame artwork.  We take the max so built-in padding is preserved. */
static void resolveEndscreenPadding(float basePadX, float basePadY,
                                    float *outPadX, float *outPadY,
                                    float *outPadR, float *outPadB)
{
    float l = 0.0f, r = 0.0f, t = 0.0f, b = 0.0f;
    pdguiThemeGetContentInset(&l, &r, &t, &b);

    float px = basePadX;
    if (l > px) px = l;

    /* padY already budgets the title bar (~32px). The top inset only covers
     * the frame corner, so keep basePadY as the floor. */
    float py = basePadY;
    if (t + pdguiScale(22.0f) > py) py = t + pdguiScale(22.0f);

    float pr = basePadX;
    if (r > pr) pr = r;

    float pb = pdguiScale(18.0f);
    if (b > pb) pb = b;

    if (outPadX) *outPadX = px;
    if (outPadY) *outPadY = py;
    if (outPadR) *outPadR = pr;
    if (outPadB) *outPadB = pb;
}

/* Horizontal rule with a label. */
static void SectionHeader(const char *label)
{
    float padX = pdguiScale(6.0f);
    ImGui::Spacing();
    /* S311: section header follows theme title glow so the endscreen
     * reads the same palette as every other menu. */
    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

/* Two-column stat row: label on left, value right-aligned. */
static void StatRow(const char *label, const char *value, const ImVec4 *valueColor = nullptr)
{
    float scale   = pdguiScaleFactor();
    float colW    = pdguiScale(195.0f);   /* label column width */

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
    struct ranking_es raw[MAX_MPCHRS];
    (void)teams;
    /* Always build from per-player rankings; team mode is represented by
     * ordering/grouping, not by mpGetTeamRankings() aggregate rows. */
    s32 count = mpGetPlayerRankings(raw);
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
        rows[i].isLocalPlayer = (raw[i].chrnum < (u32)MAX_PLAYERS);

        s32 kills = 0;
        for (s32 k = 0; k < MAX_MPCHRS; k++) {
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

/* Input debounce: when the endscreen first appears, suppress A/confirm for
 * a few frames so the button press that skipped the cutscene doesn't
 * immediately activate a menu button. */
static s32 s_SoloEndscreenDebounce = 0;

/* M-6-B (2026-04-19): S385 destructive-action confirm modal tracker for
 * the solo failed-mission "Main Menu" button.  pdguiRenderConfirmModal()
 * reads/clears this; set to GetFrameCount() at the site that opens the
 * popup.  The MP trackers live further down near the MP statics. */
static s32 s_SoloFailedMainMenuOpenFrame = -1;

static void renderSoloEndscreen(struct menudialog *dialog, bool completed)
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
    if (menuW > pdguiScale(1350.0f)) menuW = pdguiScale(1350.0f);
    float menuX = (disp.x - menuW) * 0.5f;
    float menuY = (disp.y - menuH) * 0.5f;

    /* S297: honour the active chrome style's content inset so the debrief
     * columns clear any nineslice border corners.  `padX` is the left edge,
     * `padR` the right, `padY` the top (inclusive of title bar), `padB` the
     * bottom; the bottom is used to park the action buttons. */
    float padX, padY, padR, padB;
    resolveEndscreenPadding(pdguiScale(27.0f), pdguiScale(54.0f),
                            &padX, &padY, &padR, &padB);

    /* ----- Dim the background (P9: palette-derived) --------------------- */
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), disp, pdguiPalImU32(PDPAL_BODYBG, 160));

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

    /* B-RMB-Endscreen fix: Acquire the ImGuiMenu context unconditionally each
     * frame. The previous IsWindowAppearing gating had the same failure mode as
     * B-171 (solo pause): if the first-frame push raced with menupoolReleaseAll's
     * deferred pop, the ctx never attached and the endscreen was stuck in
     * gameplay input mode (RMB-only navigation, invisible cursor). Unconditional
     * re-acquire is idempotent -- menupoolAcquire's S300 branch returns early if
     * the slot is already active with the ctx attached. */
    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    /* Defensive force-push: if the top context is still gameplay when the
     * endscreen is rendering, the acquire path failed to attach (stale
     * owned_ctx, marked_for_removal race, etc.). Push directly so the
     * endscreen always has input authority for keyboard + gamepad + mouse. */
    if (!inputCtxIsActive(&g_CtxImGuiMenu)) {
        inputCtxPush(&g_CtxImGuiMenu);
    }

    if (ImGui::IsWindowAppearing()) {
        /* M2.3: Refresh achievements so newly unlocked ones show.
         * D6 P3: poll for newly-unlocked IDs and push toast notifications. */
        achievementsRefresh();
        pdguiAchievementToastPollUnlocks();
        /* Debounce: the A press that skipped the end-of-mission cutscene
         * must not pass through to the endscreen buttons. Suppress confirm
         * input for 3 frames so the player has to press A again. */
        s_SoloEndscreenDebounce = 3;
    }

    /* Tick debounce counter */
    if (s_SoloEndscreenDebounce > 0) {
        s_SoloEndscreenDebounce--;
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
    float contentW = menuW - padX - padR;
    /* Reserve space for the docked action bar (C1) below; cursor is already
     * past the big title + spacing, so measure what's left and subtract the
     * bar height + a small gap + the chrome inset. */
    float availY   = ImGui::GetContentRegionAvail().y;
    float contentH = availY - pdguiActionBarHeight() - pdguiScale(12.0f) - padB;
    if (contentH < pdguiScale(100.0f)) contentH = pdguiScale(100.0f);

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
            float barW = colSplit - padX - pdguiScale(12.0f);
            float barH = pdguiScale(12.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pdguiScale(6.0f));

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
                ImGui::SameLine(0.0f, pdguiScale(9.0f));
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

    /* ----- Action buttons (docked action bar, C1) ---------------------- */
    /* During input debounce, suppress button activations so the A press
     * that skipped the cutscene doesn't immediately trigger a button. */
    bool inputSuppressed = (s_SoloEndscreenDebounce > 0);

    if (pdguiBeginActionBar("##es_solo_ab")) {
        float availW = ImGui::GetContentRegionAvail().x;
        float halfW  = availW * 0.5f;

        if (completed) {
            /* NEXT MISSION (default)  |  RETRY MISSION / MAIN MENU */
            if (pdguiEndscreenHasNextMission()) {
                if (pdguiActionBarButton("Next Mission", 1, halfW) && !inputSuppressed) {
                    pdguiEndscreenNextMission();
                }
                ImGui::SameLine();
                if (pdguiActionBarButton("Retry Mission", 0,
                                          ImGui::GetContentRegionAvail().x)
                        && !inputSuppressed) {
                    pdguiEndscreenStartMission();
                }
            } else {
                if (pdguiActionBarButton("Retry Mission", 1, halfW) && !inputSuppressed) {
                    pdguiEndscreenStartMission();
                }
                ImGui::SameLine();
                if (pdguiActionBarButton("Main Menu", 0,
                                          ImGui::GetContentRegionAvail().x)
                        && !inputSuppressed) {
                    pdguiEndscreenExitToMainMenu();
                }
            }
        } else {
            /* RETRY MISSION (default)  |  MAIN MENU (danger)
             * M-6-B: Main Menu on a failed mission is destructive (discards
             * any unsaved progress), so route through the canonical S385
             * confirm modal. */
            if (pdguiActionBarButton("Retry Mission", 1, halfW) && !inputSuppressed) {
                pdguiEndscreenStartMission();
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(0.3f, 0.1f, 0.1f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
            const char *sfmmPopupId = "Return to Main Menu?##es_solo_mm";
            if (pdguiActionBarButton("Main Menu", 0,
                                      ImGui::GetContentRegionAvail().x)
                    && !inputSuppressed
                    && !ImGui::IsPopupOpen(sfmmPopupId)) {
                ImGui::OpenPopup(sfmmPopupId);
                s_SoloFailedMainMenuOpenFrame = (s32)ImGui::GetFrameCount();
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            }
            ImGui::PopStyleColor(3);
        }
    }
    pdguiEndActionBar();

    /* Keyboard navigation: Enter/Start or Escape/Back — also debounced.
     * S311: title X button also exits (first-click reliability).
     * M-6-B: when the failed-mission main-menu confirm is open, suppress
     * Esc here so it routes to the modal only; completed runs still have
     * Esc as the quick exit (non-destructive in that branch). */
    if (!inputSuppressed) {
        const char *sfmmPopupId = "Return to Main Menu?##es_solo_mm";
        bool confirmOpen = ImGui::IsPopupOpen(sfmmPopupId);
        if (!confirmOpen &&
            (pdguiConsumeTitleClose() ||
             ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            if (!completed) {
                ImGui::OpenPopup(sfmmPopupId);
                s_SoloFailedMainMenuOpenFrame = (s32)ImGui::GetFrameCount();
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            } else {
                pdguiEndscreenExitToMainMenu();
            }
        }
    }

    /* M-6-B: Solo failed-mission Main Menu confirm (destructive = discards
     * unsaved progress).  Rendered before End() so the popup parents to
     * this window. */
    s32 sfmmRes = pdguiRenderConfirmModal(
        "Return to Main Menu?##es_solo_mm",
        "Return to Main Menu?",
        "Return to the main menu? Any unsaved progress will be lost.",
        "Main Menu",
        &s_SoloFailedMainMenuOpenFrame);

    ImGui::End();

    if (sfmmRes == PDGUI_CONFIRM_OK) {
        pdguiEndscreenExitToMainMenu();
    }

    /* E.3: Restore palette so the next renderer (main menu, etc.) is clean. */
    pdguiSetPalette(prevPalette);
}

/* ========================================================================
 * MP End Screen
 * ======================================================================== */

/* Suppress A/Enter for a few frames after the endscreen appears so the
 * pause→End Game confirm / Alt combo cannot instantly dismiss or rematch. */
static s32 s_MpEndscreenDebounce = 0;

/* M-6-B (2026-04-19): S385 destructive-action confirm modal trackers for
 * the MP endscreen.  pdguiRenderConfirmModal() uses these to drive the
 * 5-frame force-focus and 3-frame input debounce, and clears them to -1
 * on dismiss.  Solo-failed tracker (s_SoloFailedMainMenuOpenFrame) is
 * declared above the solo renderer. */
static s32 s_MpDisconnectOpenFrame = -1;  /* MP networked: Disconnect */
static s32 s_MpQuitOpenFrame       = -1;  /* solo-MP: Quit */

/* S301 Bug C diagnostic state. These counters survive a single match's
 * endscreen lifetime and reset when a fresh entry is detected. The goal
 * is to capture every signal needed to distinguish the six hypotheses
 * in context/scratch/archive/2026-04-13/session-state-endgame-crash.md
 * §4c without re-reading the file. */
static s32 s_MpEndscreenDiagFramesRendered = 0;  /* count of Begin()=true frames */
static s32 s_MpEndscreenDiagLastLoggedFrame = -1; /* rate-limit section logs */
static s32 s_MpEndscreenDiagRankingsCount = 0;
static s32 s_MpEndscreenDiagAwardsShown = 0;
static u32 s_MpEndscreenDiagPrintCount = 0;       /* cap total DIAG output */

#define ENDSCREEN_DIAG_MAX_PRINTS 80

/* Gated log helper: per-match limited, avoids flooding pd.log when the
 * endscreen oscillates. Messages tagged "ENDSCREEN.DIAG:" so existing
 * LOG_CH_MENU filter + grep tooling picks them up. */
#define ENDSCREEN_DIAG_LOG(...) do {                                \
	if (s_MpEndscreenDiagPrintCount < ENDSCREEN_DIAG_MAX_PRINTS) {  \
		sysLogPrintf(LOG_NOTE, __VA_ARGS__);                        \
		s_MpEndscreenDiagPrintCount++;                              \
	}                                                               \
} while (0)

/* S295 F5 → B-End-Game-Input (2026-04-19): the fresh-entry-only ctx push
 * has a race with menuPushRootDialog's menupoolReleaseAll: that call
 * SCHEDULES a deferred pop of any previously-owned ctx, which fires at
 * end-of-frame. On the endscreen renderer's first frame, the ctx appears
 * active (deferred pop hasn't fired yet) so the push is skipped; on
 * frame 2, freshEntry is false (s_MpEndscreenLastFrame was just set), so
 * no re-push — and the endscreen runs with no owner for g_ImcMenu, which
 * deactivates Enter / A / Escape / B at the action-map level. Mike's
 * "CS end-of-match opens a menu where I have no input" symptom.
 *
 * The replacement: push g_CtxImGuiMenu whenever it isn't active while
 * this renderer runs. This is safe against the historical "force-close
 * site resurrects menu" concern from S295 F5 because every force-close
 * site also clears the endscreen dialog from the legacy menu stack
 * (pdguiEndscreenExitToMainMenu → func0f0f8120, menupoolReleaseAll from
 * menuPushRootDialog, stage transitions). Once the dialog is gone the
 * hotswap dispatcher stops calling this renderer, so the re-push loop
 * doesn't fire.
 *
 * s_MpEndscreenLastFrame is retained for fresh-entry DIAG logging and
 * debounce-frame reset — it no longer gates the ctx push. */
static s32 s_MpEndscreenLastFrame = -1;

/* challengeResult: 0=normal, 1=completed, 2=failed, 3=cheated */
static void renderMpEndscreen(struct menudialog *dialog, const char *titleOverride, s32 challengeResult)
{
    /* S301 Bug C diag: log every entry so we can see the oscillation
     * pattern (how many frames this renderer is called, and in what
     * order relative to force-close sites). The first log line per
     * match captures everything we need to reconstruct state. */
    {
        s32 curFrameForDiag = (s32)ImGui::GetFrameCount();
        bool diagFresh = (s_MpEndscreenLastFrame < 0) ||
                         ((curFrameForDiag - s_MpEndscreenLastFrame) > 1);
        if (diagFresh) {
            /* Reset per-match counters on fresh entry */
            s_MpEndscreenDiagFramesRendered = 0;
            s_MpEndscreenDiagLastLoggedFrame = -1;
            s_MpEndscreenDiagRankingsCount = 0;
            s_MpEndscreenDiagAwardsShown = 0;
            s_MpEndscreenDiagPrintCount = 0;

            ENDSCREEN_DIAG_LOG(
                "ENDSCREEN.DIAG: ENTRY (fresh) frame=%d g_MpPlayerNum=%d g_NetMode=%d "
                "challengeResult=%d title='%s'",
                curFrameForDiag, g_MpPlayerNum, g_NetMode,
                challengeResult, titleOverride ? titleOverride : "(null)");
        }
    }

    /* M-E1: ensure stats functions read the correct player in splitscreen */
    setCurrentPlayerNum(g_MpPlayerNum);

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
    if (sf < 0.05f) {
        sf = 1.0f; /* degenerate DPI / theme edge case — keep text visible */
    }
    float  menuW = disp.x * 0.68f;
    float  menuH = disp.y * 0.80f;
    if (menuW > pdguiScale(1440.0f)) menuW = pdguiScale(1440.0f);
    float menuX = (disp.x - menuW) * 0.5f;
    float menuY = (disp.y - menuH) * 0.5f;

    /* S297: resolve the effective inner padding for the active chrome style
     * so rankings / awards / buttons all clear the nineslice border. */
    float padX, padY, padR, padB;
    resolveEndscreenPadding(pdguiScale(27.0f), pdguiScale(54.0f),
                            &padX, &padY, &padR, &padB);

    /* ----- Dim background (P9: palette-derived) ------------------------- */
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), disp, pdguiPalImU32(PDPAL_BODYBG, 160));

    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##MpEndscreen", NULL, flags)) {
        /* Bug C instrumentation: this early-return is one of the six hypotheses
         * for the invisible body. Log the geometry on the frame it fires. */
        sysLogPrintf(LOG_WARNING,
            "ENDSCREEN: Begin=false sf=%.3f w=%.1f h=%.1f disp=%.1fx%.1f",
            sf, menuW, menuH, disp.x, disp.y);
        ImGui::End();
        pdguiSetPalette(prevPalette);
        return;
    }

    /* B-End-Game-Input (2026-04-19): unconditional ctx push while the
     * endscreen is rendering. Fresh-entry gating has a first-frame race
     * with menupoolReleaseAll's deferred pop; see s_MpEndscreenLastFrame
     * comment for the full trace. The endscreen dialog is always force-
     * closed before another menu can take over, so this cannot trap the
     * player. */
    s32 curFrame = (s32)ImGui::GetFrameCount();
    bool freshEntry = (s_MpEndscreenLastFrame < 0) ||
                      ((curFrame - s_MpEndscreenLastFrame) > 1);
    s_MpEndscreenLastFrame = curFrame;

    /* M-22: migrated from direct inputCtxPush(&g_CtxImGuiMenu) to the pool.
     * menupoolAcquireDialog honours an already-active slot by attaching the
     * ctx via menupoolAcquire's already-active branch (S300 change), so this
     * is idempotent across frames and matches the B-End-Game-Input semantics
     * of pushing unconditionally whenever the ctx isn't live. */
    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    /* B-RMB-Endscreen fix: defensive force-push. If gameplay is still on top
     * when we reach this point, the acquire path failed to attach the ctx
     * (stale owned_ctx, marked_for_removal resurrect race, etc.). Push the
     * menu ctx directly so the endscreen always has input authority for
     * keyboard + gamepad + mouse without requiring the player to hold RMB. */
    if (!inputCtxIsActive(&g_CtxImGuiMenu)) {
        inputCtxPush(&g_CtxImGuiMenu);
    }

    if (ImGui::IsWindowAppearing() || freshEntry) {
        ImGui::SetWindowFocus();
        s_MpEndscreenDebounce = 5;
        /* D6 P3: refresh + emit achievement toasts on fresh MP entry. */
        achievementsRefresh();
        pdguiAchievementToastPollUnlocks();
    }
    if (s_MpEndscreenDebounce > 0) {
        s_MpEndscreenDebounce--;
    }

    /* S301 Bug C: log geometry + ImGui state on fresh entry and every
     * ~2 seconds thereafter (120 frames). Captures the "body invisible"
     * symptom by dumping content region size, window pos, scroll, etc. */
    s_MpEndscreenDiagFramesRendered++;
    if (freshEntry || (curFrame - s_MpEndscreenDiagLastLoggedFrame) >= 120) {
        s_MpEndscreenDiagLastLoggedFrame = curFrame;
        ImVec2 winPos  = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        ImVec2 cra     = ImGui::GetContentRegionAvail();
        float  scrollY = ImGui::GetScrollY();
        float  scrollMax = ImGui::GetScrollMaxY();
        ENDSCREEN_DIAG_LOG(
            "ENDSCREEN.DIAG: frame=%d renderedFrames=%d freshEntry=%d "
            "sf=%.3f menu=(x=%.1f y=%.1f w=%.1f h=%.1f) disp=(%.1fx%.1f) "
            "pad=(x=%.1f y=%.1f r=%.1f b=%.1f) win=(pos=%.1f,%.1f size=%.1f,%.1f) "
            "contentAvail=%.1fx%.1f scroll=%.1f/%.1f",
            curFrame, s_MpEndscreenDiagFramesRendered, (s32)freshEntry,
            sf, menuX, menuY, menuW, menuH, disp.x, disp.y,
            padX, padY, padR, padB,
            winPos.x, winPos.y, winSize.x, winSize.y,
            cra.x, cra.y, scrollY, scrollMax);
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
    /* Reserve space for the docked action bar (C1) below; cursor is past
     * the title + placement headline, so measure what's left. */
    float availY = ImGui::GetContentRegionAvail().y;
    float contentH = availY - pdguiActionBarHeight() - pdguiScale(12.0f) - padB;
    {
        const float minH = pdguiScale(100.0f);
        if (contentH < minH) {
            /* Bug C instrumentation: clamped path. If this logs often, the body
             * child is being starved of height -- investigate menuH / padY /
             * pdguiScale() factors. */
            sysLogPrintf(LOG_WARNING,
                "ENDSCREEN: contentH clamped sf=%.3f menuH=%.1f padY=%.1f padB=%.1f raw=%.1f min=%.1f",
                sf, menuH, padY, padB, contentH, minH);
            contentH = minH; /* avoid zero/negative ImGui child height (invisible body) */
        }
    }
    float contentW = menuW - padX - padR;

    ImGui::BeginChild("##MpContent", ImVec2(contentW, contentH), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    /* Explicit body text color — style Text can match body BG on some palette/theme combos. */
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.84f, 0.78f, 1.0f));

    /* S301 Bug C: we reached the body child. If the body is invisible but
     * we log this, the BeginChild call succeeded — the problem is the
     * content itself (empty rankings? zero text?) not the child being
     * culled. Log content child geometry. */
    {
        ImVec2 bodyCRA = ImGui::GetContentRegionAvail();
        if (s_MpEndscreenDiagFramesRendered == 1 ||
            s_MpEndscreenDiagFramesRendered == 5 ||
            s_MpEndscreenDiagFramesRendered == 30) {
            ENDSCREEN_DIAG_LOG(
                "ENDSCREEN.DIAG: body child opened contentW=%.1f contentH=%.1f "
                "innerCRA=%.1fx%.1f",
                contentW, contentH, bodyCRA.x, bodyCRA.y);
        }
    }

    /* Rankings table */
    u32 options     = pdguiPauseGetOptions();
    bool teamsMode  = (options & ES_MPOPTION_TEAMSENABLED) != 0;

    SectionHeader("FINAL RANKINGS");

    ESRankRow rows[MAX_MPCHRS];
    s32 count = buildRankings(rows, MAX_MPCHRS, teamsMode);
    s_MpEndscreenDiagRankingsCount = count;

    /* S301 Bug C: log how many rankings rows buildRankings returned.
     * Empty rankings = empty body = looks invisible. */
    if (s_MpEndscreenDiagFramesRendered == 1) {
        ENDSCREEN_DIAG_LOG(
            "ENDSCREEN.DIAG: rankings built count=%d teamsMode=%d options=0x%08x",
            count, (s32)teamsMode, (unsigned)options);
        if (count == 0) {
            ENDSCREEN_DIAG_LOG(
                "ENDSCREEN.DIAG: WARNING rankings count is 0 -- "
                "body will appear empty (Bug C hypothesis 'empty table')");
        }
    }

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

    /* S301 Bug C: log what awards path we took. */
    if (s_MpEndscreenDiagFramesRendered == 1) {
        bool hasAwards = (award1 && award1[0]) || (award2 && award2[0]) || medals;
        s_MpEndscreenDiagAwardsShown = hasAwards ? 1 : 0;
        ENDSCREEN_DIAG_LOG(
            "ENDSCREEN.DIAG: awards path=%s award1='%s' award2='%s' medals=0x%x",
            hasAwards ? "SHOW" : "SKIP",
            award1 ? award1 : "(null)",
            award2 ? award2 : "(null)",
            (unsigned)medals);
    }

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

    /* ----- Your Stats (local player combat breakdown) ----------------- */
    {
        ImGui::Spacing();
        SectionHeader("YOUR STATS");

        s32 kills      = mpstatsGetPlayerKillCount();
        s32 totalShots = mpstatsGetPlayerShotCountByRegion(ES_SHOT_TOTAL);
        s32 headShots  = mpstatsGetPlayerShotCountByRegion(ES_SHOT_HEAD);
        s32 bodyShots  = mpstatsGetPlayerShotCountByRegion(ES_SHOT_BODY);
        s32 limbShots  = mpstatsGetPlayerShotCountByRegion(ES_SHOT_LIMB);
        s32 gunShots   = mpstatsGetPlayerShotCountByRegion(ES_SHOT_GUN);
        s32 hatShots   = mpstatsGetPlayerShotCountByRegion(ES_SHOT_HAT);
        s32 objShots   = mpstatsGetPlayerShotCountByRegion(ES_SHOT_OBJECT);

        char killBuf[16];
        snprintf(killBuf, sizeof(killBuf), "%d", kills);
        StatRow("  Kills:", killBuf);

        float accuracy = 0.0f;
        char accuracyBuf[32] = "0.0%";
        if (totalShots > 0) {
            s32 hits = headShots + bodyShots + limbShots + gunShots + hatShots + objShots;
            accuracy = (float)hits / (float)totalShots;
            if (accuracy > 1.0f) accuracy = 1.0f;
            snprintf(accuracyBuf, sizeof(accuracyBuf), "%.1f%%", accuracy * 100.0f);
        }
        StatRow("  Accuracy:", accuracyBuf);

        /* Accuracy bar */
        {
            float barW = contentW * 0.4f;
            float barH = pdguiScale(12.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pdguiScale(18.0f));

            ImVec4 barColor;
            if (accuracy >= 0.60f)      barColor = ImVec4(0.2f, 0.85f, 0.35f, 0.9f);
            else if (accuracy >= 0.30f) barColor = ImVec4(0.95f, 0.75f, 0.1f, 0.9f);
            else                        barColor = ImVec4(0.85f, 0.25f, 0.25f, 0.9f);

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.2f, 0.8f));
            ImGui::ProgressBar(accuracy, ImVec2(barW, barH), "");
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.75f, 1.0f));
        ImGui::Text("  Head %d  Body %d  Limb %d",
                    headShots, bodyShots, limbShots);
        ImGui::Text("  Other %d  Total %d",
                    gunShots + hatShots + objShots, totalShots);
        ImGui::PopStyleColor();
    }

    ImGui::PopStyleColor(); /* ImGuiCol_Text for ##MpContent body */
    ImGui::EndChild();

    /* ----- Action buttons (docked action bar, C1) --------------------- */
    bool networked = (g_NetMode != ES_NETMODE_NONE);
    const bool inputSuppressed = (s_MpEndscreenDebounce > 0);

    /* S301 Bug C: confirm we reached the action-button section. If the
     * body was invisible but this logs, the entire content child rendered
     * and it's a CSS-style issue (text color = bg color, etc.). */
    if (s_MpEndscreenDiagFramesRendered == 1 ||
        s_MpEndscreenDiagFramesRendered == 30) {
        ENDSCREEN_DIAG_LOG(
            "ENDSCREEN.DIAG: actions section networked=%d barH=%.1f "
            "debounce=%d rankings=%d awards=%d",
            (s32)networked, pdguiActionBarHeight(), s_MpEndscreenDebounce,
            s_MpEndscreenDiagRankingsCount, s_MpEndscreenDiagAwardsShown);
    }

    const char *mpDisconnectPopupId = "Disconnect?##es_mp_disc";
    const char *mpQuitPopupId       = "Quit Game?##es_mp_quit";

    if (pdguiBeginActionBar("##es_mp_ab")) {
        float availW = ImGui::GetContentRegionAvail().x;
        float halfW  = availW * 0.5f;

        if (networked) {
            if (pdguiActionBarButton("Return to Room", 1, halfW)
                    && !inputSuppressed) {
                pdguiEndscreenExitToMainMenu();
                pdguiSetInRoom(1);
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.15f, 0.15f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
            /* M-6-B: route Disconnect through canonical S385 confirm. */
            if (pdguiActionBarButton("Disconnect", 0,
                                      ImGui::GetContentRegionAvail().x)
                    && !inputSuppressed
                    && !ImGui::IsPopupOpen(mpDisconnectPopupId)) {
                ImGui::OpenPopup(mpDisconnectPopupId);
                s_MpDisconnectOpenFrame = (s32)ImGui::GetFrameCount();
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            }
            ImGui::PopStyleColor(3);
        } else {
            if (pdguiActionBarButton("Play Again", 1, halfW)
                    && !inputSuppressed) {
                pdguiEndscreenExitToMainMenu();
                pdguiSoloRoomReturn(); /* U-12: preserve config for rematch */
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.15f, 0.15f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
            /* M-6-B: route Quit through canonical S385 confirm. */
            if (pdguiActionBarButton("Quit", 0,
                                      ImGui::GetContentRegionAvail().x)
                    && !inputSuppressed
                    && !ImGui::IsPopupOpen(mpQuitPopupId)) {
                ImGui::OpenPopup(mpQuitPopupId);
                s_MpQuitOpenFrame = (s32)ImGui::GetFrameCount();
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            }
            ImGui::PopStyleColor(3);
        }
    }
    pdguiEndActionBar();

    /* Escape / title X close: still need explicit handling because the
     * action bar's focused-button Enter path covers the primary action
     * but not the cancel path.  S311: title X button mirrors Escape exit.
     * M-6-B: route Esc/title-X through the destructive-action confirm
     * (networked = Disconnect, solo-MP = Quit). */
    if (!inputSuppressed) {
        bool anyConfirmOpen = ImGui::IsPopupOpen(mpDisconnectPopupId)
                           || ImGui::IsPopupOpen(mpQuitPopupId);
        if (!anyConfirmOpen &&
            (pdguiConsumeTitleClose() ||
             ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            if (networked) {
                ImGui::OpenPopup(mpDisconnectPopupId);
                s_MpDisconnectOpenFrame = (s32)ImGui::GetFrameCount();
            } else {
                ImGui::OpenPopup(mpQuitPopupId);
                s_MpQuitOpenFrame = (s32)ImGui::GetFrameCount();
            }
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        }
    }

    /* M-6-B: render both confirm modals — only one can be open at a time. */
    s32 discRes = pdguiRenderConfirmModal(
        mpDisconnectPopupId,
        "Disconnect?",
        "Disconnect from the server and return to the main menu?",
        "Disconnect",
        &s_MpDisconnectOpenFrame);

    s32 quitRes = pdguiRenderConfirmModal(
        mpQuitPopupId,
        "Quit Game?",
        "Quit and return to the main menu? The current match will end.",
        "Quit",
        &s_MpQuitOpenFrame);

    ImGui::End();

    if (discRes == PDGUI_CONFIRM_OK) {
        netDisconnect();
        pdguiEndscreenExitToMainMenu();
    } else if (quitRes == PDGUI_CONFIRM_OK) {
        pdguiEndscreenExitToMainMenu();
    }

    /* E.3: Restore palette so the next renderer (main menu, etc.) is clean. */
    pdguiSetPalette(prevPalette);
}

/* ========================================================================
 * Hotswap render callbacks
 * ======================================================================== */

/* Solo — completed screen */
static s32 soloCompletedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderSoloEndscreen(dialog, true);
    return 1;
}

/* Solo — failed screen */
static s32 soloFailedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderSoloEndscreen(dialog, false);
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
    renderMpEndscreen(dialog, NULL, 0);
    return 1;
}

/* MP — team game over */
static s32 mpGameOverTeamRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen(dialog, NULL, 0);
    return 1;
}

/* MP — challenge completed */
static s32 mpChallengeCompletedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen(dialog, "CHALLENGE COMPLETED!", 1);
    return 1;
}

/* MP — challenge cheated */
static s32 mpChallengeCheatedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen(dialog, "CHALLENGE CHEATED!", 3);
    return 1;
}

/* MP — challenge failed */
static s32 mpChallengeFailedRender(struct menudialog *dialog, struct menu *menu, s32 winW, s32 winH)
{
    renderMpEndscreen(dialog, "CHALLENGE FAILED!", 2);
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

    /* g_MpEndscreenSavePlayerMenuDialog suppressed in pdgui_menu_mpingame.cpp (B-115).
     * g_MpEndscreenConfirmNameMenuDialog suppressed in pdgui_menu_warning.cpp (B-115).
     * Both redundant on PC — auto-save via configSave("pd.ini") handles saving. */
}
