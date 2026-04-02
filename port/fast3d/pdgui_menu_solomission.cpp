/**
 * pdgui_menu_solomission.cpp -- ImGui replacements for the Solo Mission flow.
 *
 * Group 1 (11 dialogs):
 *   g_SelectMissionMenuDialog         -- mission list with progressive unlock
 *   g_SoloMissionDifficultyMenuDialog -- Agent / Special Agent / Perfect Agent / PD Mode
 *   g_SoloMissionBriefingMenuDialog   -- scrollable briefing text
 *   g_PreAndPostMissionBriefingMenuDialog -- pre/post briefing (same renderer)
 *   g_AcceptMissionMenuDialog         -- objectives overview + Accept / Decline
 *   g_SoloMissionPauseMenuDialog      -- in-game pause: objectives + Abort
 *   g_SoloMissionOptionsMenuDialog    -- options hub: Audio/Video/Control/Display/Extended
 *   g_MissionAbortMenuDialog          -- danger confirmation: Cancel / Abort
 *
 * Registered with NULL renderFn (keep legacy rendering for model/controller previews):
 *   g_SoloMissionInventoryMenuDialog  -- weapon model preview (legacy 3D)
 *   g_FrWeaponsAvailableMenuDialog    -- training weapons model preview (legacy 3D)
 *   g_SoloMissionControlStyleMenuDialog -- controller button diagram (legacy 3D)
 *
 * Design notes:
 *   - All sizing via pdguiScale() — zero hardcoded pixels.
 *   - Abort dialog uses the Red palette (danger) and restores Blue on exit.
 *   - Legacy dialog handlers (e.g. menudialog00103608, soloMenuDialogPauseStatus)
 *     still fire on OPEN/CLOSE/TICK regardless of which renderer is active, so
 *     g_Briefing is always correctly populated before these renderers run.
 *   - Complex dialogs with 3D previews are registered as NULL (force PD native)
 *     so their weapon-model and controller-diagram UX is fully preserved.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
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

/* =========================================================================
 * Forward declarations — game symbols (extern "C" to avoid types.h)
 * ========================================================================= */

extern "C" {

/* ---- Dialog definitions ---- */
extern struct menudialogdef g_SelectMissionMenuDialog;
extern struct menudialogdef g_SoloMissionDifficultyMenuDialog;
extern struct menudialogdef g_SoloMissionBriefingMenuDialog;
extern struct menudialogdef g_PreAndPostMissionBriefingMenuDialog;
extern struct menudialogdef g_AcceptMissionMenuDialog;
extern struct menudialogdef g_SoloMissionPauseMenuDialog;
extern struct menudialogdef g_SoloMissionOptionsMenuDialog;
extern struct menudialogdef g_MissionAbortMenuDialog;
extern struct menudialogdef g_SoloMissionInventoryMenuDialog;
extern struct menudialogdef g_FrWeaponsAvailableMenuDialog;
extern struct menudialogdef g_SoloMissionControlStyleMenuDialog;
/* Sub-dialogs opened from the Options hub */
extern struct menudialogdef g_AudioOptionsMenuDialog;
extern struct menudialogdef g_VideoOptionsMenuDialog;
extern struct menudialogdef g_MissionControlOptionsMenuDialog;
extern struct menudialogdef g_MissionDisplayOptionsMenuDialog;
extern struct menudialogdef g_ExtendedMenuDialog;
/* PD Mode settings — opened from Difficulty, keep legacy */
extern struct menudialogdef g_PdModeSettingsMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);
void menuStop(void);

/* ---- Language ---- */
char *langGet(s32 textid);

/* ---- Solo stage table (21 entries, indices 0–20) ----
 * Mirrors struct solostage from types.h.  The compiler inserts 1 byte of
 * natural alignment padding between unk04 (u8) and name1 (u16). */
struct sm_solostage {
    u32 stagenum;
    u8  unk04;
    /* 1 byte compiler pad */
    u16 name1;  /* e.g. "dataDyne Central" */
    u16 name2;  /* e.g. " - Defection"     */
    u16 name3;  /* e.g. "dataDyne Defection" (short form) */
};
extern struct sm_solostage g_SoloStages[];

/* ---- Mission configuration ----
 * Layout mirrors struct missionconfig from types.h.
 * Bitfield byte 0: bits 0–6 = difficulty, bit 7 = pdmode.
 * Bitfield byte 3: bit 0 = iscoop, bit 1 = isanti. */
struct sm_missionconfig {
    u8 diff_pdmode;   /* difficulty:7, pdmode:1 */
    u8 stagenum;
    u8 stageindex;
    u8 coop_anti;     /* iscoop:1, isanti:1 */
    /* remaining fields not needed here */
};
extern struct sm_missionconfig g_MissionConfig;

#define SM_DIFFICULTY(mc)   ((mc)->diff_pdmode & 0x7F)
#define SM_PDMODE(mc)       (((mc)->diff_pdmode >> 7) & 0x1)
#define SM_ISCOOP(mc)       ((mc)->coop_anti & 0x01)
#define SM_ISANTI(mc)       (((mc)->coop_anti >> 1) & 0x01)
#define SM_SET_DIFFICULTY(mc, d) \
    ((mc)->diff_pdmode = (u8)(((mc)->diff_pdmode & 0x80) | ((d) & 0x7F)))
#define SM_CLEAR_PDMODE(mc) \
    ((mc)->diff_pdmode &= 0x7F)

/* ---- Game file — only the besttimes slice we need ----
 * struct gamefile layout: name[11] + flags(2) + pad(3) + totaltime(4) +
 *   flags[10](10) + unk1e(2) + besttimes[21][3] at offset 0x20. */
struct sm_gamefile {
    char name[11];                    /* 0x00 */
    u8   thumbnail_autodifficulty;    /* 0x0b: thumbnail:5, autodifficulty:3 */
    u8   autostageindex;              /* 0x0c */
    u8   _pad0d[3];                   /* 0x0d–0x0f: alignment padding */
    u32  totaltime;                   /* 0x10 */
    u8   flags[10];                   /* 0x14 */
    u16  unk1e;                       /* 0x1e */
    u16  besttimes[21][3];            /* 0x20  (NUM_SOLOSTAGES=21, 3 difficulties) */
};
extern struct sm_gamefile g_GameFile;

#define SM_AUTODIFFICULTY(gf) (((gf)->thumbnail_autodifficulty >> 5) & 0x07)
#define SOLOSTAGEINDEX_SKEDARRUINS 16
#define NUM_SOLOSTAGES             21
#define DIFF_A  0
#define DIFF_SA 1
#define DIFF_PA 2

/* ---- Briefing (set by dialog handler before ImGui renderer runs) ---- */
struct sm_briefing {
    u16 briefingtextnum;
    u16 objectivenames[6];
    u16 objectivedifficulties[6];
    u16 langbank;
};
extern struct sm_briefing g_Briefing;

/* ---- Functions ---- */
bool         isStageDifficultyUnlocked(s32 stageindex, s32 difficulty);
s32          getNumUnlockedSpecialStages(void);
s32          func0f104720(s32 slot);   /* special-stage slot → g_SoloStages index */
void         lvSetDifficulty(s32 difficulty);
extern s32   g_MpPlayerNum;

/* Accept / abort mission — only MENUOP_SET branch used; item/data may be NULL */
#define MENUOP_SET 6
uintptr_t menuhandlerAcceptMission(s32 op, void *item, void *data);
uintptr_t menuhandlerAbortMission(s32 op, void *item, void *data);

/* Language text IDs for mission group headings */
#define L_OPTIONS_122  0x007a  /* "Mission Select"     */
#define L_OPTIONS_123  0x007b  /* "Mission 1"          */
#define L_OPTIONS_124  0x007c  /* "Mission 2"          */
#define L_OPTIONS_125  0x007d  /* "Mission 3"          */
#define L_OPTIONS_126  0x007e  /* "Mission 4"          */
#define L_OPTIONS_127  0x007f  /* "Mission 5"          */
#define L_OPTIONS_128  0x0080  /* "Mission 6"          */
#define L_OPTIONS_129  0x0081  /* "Mission 7"          */
#define L_OPTIONS_130  0x0082  /* "Mission 8"          */
#define L_OPTIONS_131  0x0083  /* "Mission 9"          */
#define L_OPTIONS_132  0x0084  /* "Special Assignments"*/
#define L_OPTIONS_172  0x00ac  /* "Status"             */
#define L_OPTIONS_173  0x00ad  /* "Abort!"             */
#define L_OPTIONS_174  0x00ae  /* "Warning"            */
#define L_OPTIONS_175  0x00af  /* "Do you want to abort the mission?" */
#define L_OPTIONS_176  0x00b0  /* "Cancel"             */
#define L_OPTIONS_177  0x00b1  /* "Abort"              */
#define L_OPTIONS_178  0x00b2  /* "Inventory"          */
#define L_OPTIONS_181  0x00b5  /* "Audio"              */
#define L_OPTIONS_182  0x00b6  /* "Video"              */
#define L_OPTIONS_183  0x00b7  /* "Control"            */
#define L_OPTIONS_184  0x00b8  /* "Display"            */
#define L_OPTIONS_247  0x00f7  /* "Briefing"           */
#define L_OPTIONS_248  0x00f8  /* "Select Difficulty"  */
#define L_OPTIONS_249  0x00f9  /* "Difficulty"         */
#define L_OPTIONS_251  0x00fb  /* "Agent"              */
#define L_OPTIONS_252  0x00fc  /* "Special Agent"      */
#define L_OPTIONS_253  0x00fd  /* "Perfect Agent"      */
#define L_OPTIONS_254  0x00fe  /* "Cancel"             */
#define L_OPTIONS_273  0x0111  /* "Overview"           */
#define L_OPTIONS_274  0x0112  /* "Accept"             */
#define L_OPTIONS_275  0x0113  /* "Decline"            */
#define L_MPWEAPONS_221 0x80dd /* "Perfect Dark" (mode)*/

} /* extern "C" */

/* =========================================================================
 * Module state
 * ========================================================================= */

static bool s_Registered = false;

/* Mission Select */
static s32 s_MissionSelectIdx = 0;

/* Difficulty */
static s32 s_DiffSelectIdx = 0;

/* Briefing scroll */
static float s_BriefingScroll = 0.0f;

/* Accept Mission */
static s32 s_AcceptSelectIdx = 0;  /* 0 = Accept, 1 = Decline */

/* Pause */
static s32 s_PauseSelectIdx = 0;   /* 0 = close, 1 = Inventory, 2 = Options, 3 = Abort */

/* Abort confirmation */
static s32 s_AbortSelectIdx = 0;   /* 0 = Cancel, 1 = Abort */

/* Options hub */
static s32 s_OptionsSelectIdx = 0;

/* =========================================================================
 * Helpers
 * ========================================================================= */

/**
 * Format a best-time value (seconds) into "Mm:SSs" or "--:--" if zero.
 * Mirrors soloMenuTextBestTime() from mainmenu.c.
 */
static void formatBestTime(char *buf, size_t bufsz, u16 t)
{
    if (t == 0) {
        snprintf(buf, bufsz, "--:--");
        return;
    }
    if (t >= 0xFFF) {
        snprintf(buf, bufsz, "==:==");
        return;
    }
    s32 h = t / 3600;
    s32 m = (t % 3600) / 60;
    s32 s = t % 60;
    if (h > 0) {
        snprintf(buf, bufsz, "%dh:%02dm:%02ds", h, m, s);
    } else {
        snprintf(buf, bufsz, "%dm:%02ds", m, s);
    }
}


/* =========================================================================
 * langSafe — null-safe langGet wrapper.
 * langGet() returns NULL when the bank is unloaded or the entry is zero.
 * Passing NULL to ImGui::Button / ImDrawList::AddText causes 0xc0000005.
 * Always use langSafe() when the result goes directly to an ImGui call.
 * ========================================================================= */
static const char *langSafe(s32 textid)
{
    const char *s = langGet(textid);
    return s ? s : "";
}


/* Difficulty badge fill colors: Agent=green, SA=blue, PA=gold */
static const ImU32 k_DiffBadgeColor[] = {
    IM_COL32( 60, 200,  80, 255),   /* Agent */
    IM_COL32( 80, 160, 255, 255),   /* Special Agent */
    IM_COL32(220, 190,  50, 255),   /* Perfect Agent */
};
static const char *k_DiffShort[] = { "A", "S", "P" };

/* =========================================================================
 * Mission Select
 * ========================================================================= */

/* Mission group data: {firstStageIndex, langId} — mirrors menuhandlerMissionList */
struct MissionGroup { s32 firstIdx; s32 langId; };
static const MissionGroup k_MissionGroups[] = {
    {  0, L_OPTIONS_123 },
    {  3, L_OPTIONS_124 },
    {  4, L_OPTIONS_125 },
    {  6, L_OPTIONS_126 },
    {  9, L_OPTIONS_127 },
    { 12, L_OPTIONS_128 },
    { 14, L_OPTIONS_129 },
    { 15, L_OPTIONS_130 },
    { 16, L_OPTIONS_131 },
};
static const s32 k_NumRegularGroups = (s32)(sizeof(k_MissionGroups) / sizeof(k_MissionGroups[0]));

/** Return the mission group index (0–8) for a given stage index. */
static s32 stageToGroupIdx(s32 stageIdx)
{
    s32 grp = 0;
    for (s32 g = 1; g < k_NumRegularGroups; g++) {
        if (stageIdx >= k_MissionGroups[g].firstIdx) grp = g;
        else break;
    }
    return grp;
}

/*
 * renderMissionSelect — flat mission list with blip completion indicators.
 *
 * Each row shows three blip dots (Agent / Special Agent / Perfect Agent) and
 * the mission name.  A lit blip = beaten on that difficulty; unlit = not yet.
 *
 * Hover behaviour:
 *   - Hovering directly over a blip shows the difficulty name + best time.
 *   - Hovering elsewhere on the row shows the consolidated unlockable count
 *     (e.g. "0/1").  If a stage has no unlockables the count is omitted.
 *   - Hovering over a chapter heading shows the summed count for the group
 *     (e.g. "0/3" when the group has three stages).
 *
 * Clicking an accessible row sets g_MissionConfig.stagenum/stageindex and
 * pushes g_SoloMissionDifficultyMenuDialog.  That dialog shows record times
 * per difficulty and per-difficulty objective hover tooltips.
 */
static s32 renderMissionSelect(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH)
{
    float mw      = pdguiMenuWidth();
    float mh      = pdguiMenuHeight();
    ImVec2 mpos   = pdguiMenuPos();
    float titleH  = pdguiScale(26.0f);
    float footerH = pdguiScale(22.0f);
    float listH   = mh - titleH - pdguiScale(8.0f) - footerH;
    float rowH    = pdguiScale(28.0f);
    float blipR   = pdguiScale(5.0f);
    float blipGap = pdguiScale(13.0f);
    s32 pendingStage = -1;

    static const char *k_DiffFullNames[] = {
        "Agent", "Special Agent", "Perfect Agent"
    };

    ImGui::SetNextWindowPos(mpos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_mission_select", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    pdguiDrawPdDialog(mpos.x, mpos.y, mw, mh, "Mission Select", 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().ItemSpacing.y);

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    if (ImGui::BeginChild("##mission_list", ImVec2(0, listH), false,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {

        /* ------------------------------------------------------------------ */
        /* Regular missions 0..SOLOSTAGEINDEX_SKEDARRUINS                     */
        /* ------------------------------------------------------------------ */
        s32 prevGroup = -1;
        for (s32 i = 0; i <= SOLOSTAGEINDEX_SKEDARRUINS && pendingStage < 0; i++) {
            s32 grp     = stageToGroupIdx(i);
            s32 chap    = grp + 1;
            s32 chapPos = i - k_MissionGroups[grp].firstIdx + 1;

            /* Chapter heading when the group changes */
            if (grp != prevGroup) {
                if (prevGroup >= 0) ImGui::Spacing();

                /* Group unlockable count (stub: 1 per stage) */
                s32 groupEnd = (grp + 1 < k_NumRegularGroups)
                                 ? k_MissionGroups[grp + 1].firstIdx
                                 : SOLOSTAGEINDEX_SKEDARRUINS + 1;
                int grpEarned = 0;
                int grpTotal  = groupEnd - k_MissionGroups[grp].firstIdx;

                const char *chapLang = langSafe(k_MissionGroups[grp].langId);
                char chapHdr[64];
                if (chapLang[0]) {
                    snprintf(chapHdr, sizeof(chapHdr), "-- %s --", chapLang);
                } else {
                    snprintf(chapHdr, sizeof(chapHdr), "-- Mission %d --", chap);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.0f, 1.0f));
                ImGui::TextUnformatted(chapHdr);
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered() && grpTotal > 0) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%d/%d", grpEarned, grpTotal);
                    ImGui::EndTooltip();
                }

                prevGroup = grp;
            }

            bool       accessible = (bool)isStageDifficultyUnlocked(i, DIFF_A);
            const char *ln1       = langSafe(g_SoloStages[i].name1);
            const char *ln2       = langSafe(g_SoloStages[i].name2);
            char nodeLabel[192];
            snprintf(nodeLabel, sizeof(nodeLabel), "%d.%d  %s%s",
                     chap, chapPos, ln1, ln2);

            ImGui::PushID(i);

            ImVec2 rowPos   = ImGui::GetCursorScreenPos();
            float  contentW = ImGui::GetContentRegionAvail().x;
            bool   doSelect = false;
            bool   rowHover = false;

            if (accessible) {
                doSelect = ImGui::Selectable("##ms_row", false,
                                             ImGuiSelectableFlags_None,
                                             ImVec2(contentW, rowH));
                rowHover = ImGui::IsItemHovered();
            } else {
                ImGui::Dummy(ImVec2(contentW, rowH));
            }

            /* Blip dots + mission name drawn over the selectable area */
            {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                float rcy = rowPos.y + rowH * 0.5f;
                float bx  = rowPos.x + blipR + pdguiScale(4.0f);
                for (s32 d = 0; d < 3; d++) {
                    bool beaten = (g_GameFile.besttimes[i][d] != 0);
                    float bcx   = bx + d * blipGap;
                    ImU32 fill  = beaten ? k_DiffBadgeColor[d]
                                         : IM_COL32(40, 40, 50, 160);
                    ImU32 ring  = beaten ? IM_COL32(200, 200, 200, 100)
                                         : IM_COL32(80, 80, 100, 120);
                    dl->AddCircleFilled(ImVec2(bcx, rcy), blipR, fill);
                    dl->AddCircle(ImVec2(bcx, rcy), blipR, ring);
                }
                float nameX  = bx + 3.0f * blipGap + pdguiScale(4.0f);
                ImU32 nameCol = accessible
                    ? IM_COL32(230, 230, 240, 255)
                    : IM_COL32(100, 100, 115, 150);
                dl->AddText(
                    ImVec2(nameX, rcy - ImGui::GetTextLineHeight() * 0.5f),
                    nameCol, nodeLabel);
            }

            /* Hover tooltips: blip-specific first, then unlockable count */
            if (rowHover) {
                float rcy = rowPos.y + rowH * 0.5f;
                float bx  = rowPos.x + blipR + pdguiScale(4.0f);
                ImVec2 mp = ImGui::GetMousePos();
                bool   shownBlipTip = false;
                float  hitR = blipR + pdguiScale(3.0f);

                for (s32 d = 0; d < 3 && !shownBlipTip; d++) {
                    float bcx = bx + d * blipGap;
                    float dx  = mp.x - bcx;
                    float dy  = mp.y - rcy;
                    if (dx*dx + dy*dy <= hitR*hitR) {
                        bool beaten = (g_GameFile.besttimes[i][d] != 0);
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(k_DiffFullNames[d]);
                        ImGui::Separator();
                        if (beaten) {
                            char timeStr[32];
                            formatBestTime(timeStr, sizeof(timeStr),
                                           g_GameFile.besttimes[i][d]);
                            ImGui::Text("Best: %s", timeStr);
                        } else {
                            ImGui::TextDisabled("Not completed");
                        }
                        ImGui::EndTooltip();
                        shownBlipTip = true;
                    }
                }

                if (!shownBlipTip) {
                    /* Stub: 1 unlockable per stage, 0 earned.
                     * Replace soloGetStageUnlockCount() when system is wired. */
                    int stgTotal = 1, stgEarned = 0;
                    if (stgTotal > 0) {
                        ImGui::BeginTooltip();
                        ImGui::Text("%d/%d", stgEarned, stgTotal);
                        ImGui::EndTooltip();
                    }
                }
            }

            if (doSelect) pendingStage = i;

            ImGui::PopID();
        } /* stage loop */

        /* ------------------------------------------------------------------ */
        /* Special Assignments (stages 17..20)                                */
        /* ------------------------------------------------------------------ */
        if (pendingStage < 0) {
            s32 specialStart = SOLOSTAGEINDEX_SKEDARRUINS + 1;

            ImGui::Spacing();

            /* Group unlockable count (stub) */
            int saGrpTotal  = NUM_SOLOSTAGES - specialStart;
            int saGrpEarned = 0;

            const char *saLang = langSafe(L_OPTIONS_132);
            char saBuf[64];
            if (saLang[0]) {
                snprintf(saBuf, sizeof(saBuf), "-- %s --", saLang);
            } else {
                snprintf(saBuf, sizeof(saBuf), "-- Special Assignments --");
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
            ImGui::TextUnformatted(saBuf);
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered() && saGrpTotal > 0) {
                ImGui::BeginTooltip();
                ImGui::Text("%d/%d", saGrpEarned, saGrpTotal);
                ImGui::EndTooltip();
            }

            for (s32 j = specialStart;
                 j < NUM_SOLOSTAGES && pendingStage < 0; j++) {
                s32 saNum       = j - specialStart + 1;
                bool accessible = (bool)isStageDifficultyUnlocked(j, DIFF_A);
                const char *ln1 = langSafe(g_SoloStages[j].name1);
                char nodeLabel[192];
                snprintf(nodeLabel, sizeof(nodeLabel), "SA-%d  %s", saNum, ln1);

                ImGui::PushID(0x200 + j);

                ImVec2 rowPos   = ImGui::GetCursorScreenPos();
                float  contentW = ImGui::GetContentRegionAvail().x;
                bool   doSelect = false;
                bool   rowHover = false;

                if (accessible) {
                    doSelect = ImGui::Selectable("##ms_sp_row", false,
                                                 ImGuiSelectableFlags_None,
                                                 ImVec2(contentW, rowH));
                    rowHover = ImGui::IsItemHovered();
                } else {
                    ImGui::Dummy(ImVec2(contentW, rowH));
                }

                /* Blip dots + name (gold tint for specials) */
                {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    float rcy = rowPos.y + rowH * 0.5f;
                    float bx  = rowPos.x + blipR + pdguiScale(4.0f);
                    for (s32 d = 0; d < 3; d++) {
                        bool beaten = (g_GameFile.besttimes[j][d] != 0);
                        float bcx   = bx + d * blipGap;
                        ImU32 fill  = beaten ? k_DiffBadgeColor[d]
                                             : IM_COL32(40, 40, 50, 160);
                        ImU32 ring  = beaten ? IM_COL32(200, 200, 200, 100)
                                             : IM_COL32(80, 80, 100, 120);
                        dl->AddCircleFilled(ImVec2(bcx, rcy), blipR, fill);
                        dl->AddCircle(ImVec2(bcx, rcy), blipR, ring);
                    }
                    float nameX  = bx + 3.0f * blipGap + pdguiScale(4.0f);
                    ImU32 nameCol = accessible
                        ? IM_COL32(255, 230, 140, 255)
                        : IM_COL32(110, 100, 80, 150);
                    dl->AddText(
                        ImVec2(nameX, rcy - ImGui::GetTextLineHeight() * 0.5f),
                        nameCol, nodeLabel);
                }

                /* Hover tooltips for special assignment rows */
                if (rowHover) {
                    float rcy = rowPos.y + rowH * 0.5f;
                    float bx  = rowPos.x + blipR + pdguiScale(4.0f);
                    ImVec2 mp = ImGui::GetMousePos();
                    bool   shownBlipTip = false;
                    float  hitR = blipR + pdguiScale(3.0f);

                    for (s32 d = 0; d < 3 && !shownBlipTip; d++) {
                        float bcx = bx + d * blipGap;
                        float dx  = mp.x - bcx;
                        float dy  = mp.y - rcy;
                        if (dx*dx + dy*dy <= hitR*hitR) {
                            bool beaten = (g_GameFile.besttimes[j][d] != 0);
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(k_DiffFullNames[d]);
                            ImGui::Separator();
                            if (beaten) {
                                char timeStr[32];
                                formatBestTime(timeStr, sizeof(timeStr),
                                               g_GameFile.besttimes[j][d]);
                                ImGui::Text("Best: %s", timeStr);
                            } else {
                                ImGui::TextDisabled("Not completed");
                            }
                            ImGui::EndTooltip();
                            shownBlipTip = true;
                        }
                    }

                    if (!shownBlipTip) {
                        int stgTotal = 1, stgEarned = 0;
                        if (stgTotal > 0) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%d/%d", stgEarned, stgTotal);
                            ImGui::EndTooltip();
                        }
                    }
                }

                if (doSelect) pendingStage = j;

                ImGui::PopID();
            } /* special stage loop */
        }

    }
    ImGui::EndChild();

    ImGui::TextDisabled("Click to select difficulty   Esc: Back");
    ImGui::End();

    /* Push difficulty dialog after End() to avoid nesting state changes. */
    if (pendingStage >= 0 && pendingStage < NUM_SOLOSTAGES) {
        g_MissionConfig.stagenum   = (u8)g_SoloStages[pendingStage].stagenum;
        g_MissionConfig.stageindex = (u8)pendingStage;
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        menuPushDialog(&g_SoloMissionDifficultyMenuDialog);
    }

    return 1;
}

/* =========================================================================
 * Difficulty Selection
 * ========================================================================= */

static s32 renderDifficulty(struct menudialog *dialog,
                             struct menu *menu,
                             s32 winW, s32 winH)
{
    float sf  = pdguiScaleFactor();
    float mw  = pdguiMenuWidth() * 0.60f;   /* narrower dialog for difficulty */
    float mh  = pdguiMenuHeight() * 0.55f;
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_difficulty", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        /* Pre-select the last auto-difficulty */
        s32 autoD = SM_AUTODIFFICULTY(&g_GameFile);
        s_DiffSelectIdx = (autoD <= DIFF_PA) ? autoD : 0;
    }

    float titleH = pdguiScale(26.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, langSafe(L_OPTIONS_248), 1);

    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);

    /* Current stage name */
    s32 si = g_MissionConfig.stageindex;
    if (si >= 0 && si < NUM_SOLOSTAGES) {
        char stageName[128];
        snprintf(stageName, sizeof(stageName), "%s%s",
                 langSafe(g_SoloStages[si].name1),
                 langSafe(g_SoloStages[si].name2));
        ImGui::TextDisabled("%s", stageName);
    }
    ImGui::Separator();

    /* Show PD Mode option? Only when Skedar Ruins is beaten on PA */
    bool pdModeVisible = (g_GameFile.besttimes[SOLOSTAGEINDEX_SKEDARRUINS][DIFF_PA] != 0);

    /* Difficulty rows: Agent, Special Agent, Perfect Agent, [PD Mode], Cancel */
    static const s32 k_Diffs[]  = { DIFF_A, DIFF_SA, DIFF_PA };
    static const s32 k_DiffIds[] = { L_OPTIONS_251, L_OPTIONS_252, L_OPTIONS_253 };
    s32 numOptions = 3 + (pdModeVisible ? 1 : 0) + 1; /* +1 for Cancel */

    if (s_DiffSelectIdx < 0)           s_DiffSelectIdx = 0;
    if (s_DiffSelectIdx >= numOptions) s_DiffSelectIdx = numOptions - 1;

    /* Navigation */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) ||
        ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_DiffSelectIdx++;
        if (s_DiffSelectIdx >= numOptions) s_DiffSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true) ||
        ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_DiffSelectIdx--;
        if (s_DiffSelectIdx < 0) s_DiffSelectIdx = numOptions - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    float rowH  = pdguiScale(36.0f);
    float timeW = pdguiScale(80.0f);

    /* ---- Difficulty rows ---- */
    for (s32 i = 0; i < 3; i++) {
        s32 diff = k_Diffs[i];
        bool locked   = !isStageDifficultyUnlocked(si, diff);
        bool isActive = (s_DiffSelectIdx == i);

        ImGui::PushID(i);

        if (isActive) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(16.0f), rowH);
        }

        if (ImGui::Selectable("##diff_row", isActive,
                              ImGuiSelectableFlags_None, ImVec2(0, rowH))) {
            if (!locked) {
                s_DiffSelectIdx = i;
                SM_CLEAR_PDMODE(&g_MissionConfig);
                SM_SET_DIFFICULTY(&g_MissionConfig, diff);
                lvSetDifficulty(diff);
                pdguiPlaySound(PDGUI_SND_SELECT);
                menuPopDialog();
                menuPushDialog(&g_AcceptMissionMenuDialog);
            } else {
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }
        if (ImGui::IsItemHovered()) { s_DiffSelectIdx = i; }

        /* Confirm from keyboard/gamepad */
        if (isActive && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
            if (!locked) {
                SM_CLEAR_PDMODE(&g_MissionConfig);
                SM_SET_DIFFICULTY(&g_MissionConfig, diff);
                lvSetDifficulty(diff);
                pdguiPlaySound(PDGUI_SND_SELECT);
                menuPopDialog();
                menuPushDialog(&g_AcceptMissionMenuDialog);
            } else {
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }

        /* Hover tooltip: objectives for this difficulty from g_Briefing.
         * g_Briefing is populated by the dialog handler before this runs.
         * objectivedifficulties bits: 0=Agent, 1=SA, 2=PA. */
        if (!locked && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", langSafe(k_DiffIds[i]));
            ImGui::Separator();
            bool anyObj = false;
            for (s32 oi = 0; oi < 6; oi++) {
                if (g_Briefing.objectivenames[oi] == 0) continue;
                u16 bits = g_Briefing.objectivedifficulties[oi];
                if (bits & (1u << (unsigned)diff)) {
                    ImGui::TextUnformatted(langSafe(g_Briefing.objectivenames[oi]));
                    anyObj = true;
                }
            }
            if (!anyObj) {
                ImGui::TextDisabled("(No objectives)");
            }
            ImGui::EndTooltip();
        }

        /* Overlay: difficulty name + best time */
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 rmin = ImGui::GetItemRectMin();
            float cy = rmin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            float tx = rmin.x + pdguiScale(10.0f);

            ImU32 nameCol = locked ? IM_COL32(100, 100, 120, 180) : IM_COL32(255, 255, 255, 255);
            dl->AddText(ImVec2(tx, cy), nameCol, langSafe(k_DiffIds[i]));

            if (locked) {
                dl->AddText(ImVec2(tx + pdguiScale(120.0f), cy),
                            IM_COL32(180, 60, 60, 200), "[Locked]");
            } else {
                char timeStr[32];
                formatBestTime(timeStr, sizeof(timeStr),
                               g_GameFile.besttimes[si < NUM_SOLOSTAGES ? si : 0][diff]);
                ImVec2 tSz = ImGui::CalcTextSize(timeStr);
                dl->AddText(ImVec2(rmin.x + mw - tSz.x - pdguiScale(20.0f), cy),
                            IM_COL32(160, 200, 140, 210), timeStr);
            }
        }

        ImGui::PopID();
    }

    /* ---- PD Mode row (optional) ---- */
    if (pdModeVisible) {
        s32 pdIdx    = 3;
        bool isActive = (s_DiffSelectIdx == pdIdx);

        ImGui::PushID(0x10);

        if (isActive) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(16.0f), rowH);
        }

        bool doSelect = ImGui::Selectable("##diff_pd", isActive,
                                          ImGuiSelectableFlags_None, ImVec2(0, rowH));
        if (ImGui::IsItemHovered()) s_DiffSelectIdx = pdIdx;
        if (isActive && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
            doSelect = true;

        if (doSelect) {
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            menuPushDialog(&g_PdModeSettingsMenuDialog);
        }

        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 rmin = ImGui::GetItemRectMin();
            float cy = rmin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            dl->AddText(ImVec2(rmin.x + pdguiScale(10.0f), cy),
                        IM_COL32(200, 180, 255, 255), langSafe(L_MPWEAPONS_221));
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    /* ---- Cancel row ---- */
    {
        s32 cancelIdx = 3 + (pdModeVisible ? 1 : 0);
        bool isActive  = (s_DiffSelectIdx == cancelIdx);

        ImGui::PushID(0x20);

        if (isActive) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(16.0f), rowH * 0.8f);
        }

        bool doCancel = ImGui::Selectable("##diff_cancel", isActive,
                                          ImGuiSelectableFlags_None,
                                          ImVec2(0, rowH * 0.8f));
        if (ImGui::IsItemHovered()) s_DiffSelectIdx = cancelIdx;
        if (isActive && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
            doCancel = true;

        if (doCancel) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }

        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 rmin = ImGui::GetItemRectMin();
            float cy = rmin.y + (rowH * 0.8f - ImGui::GetTextLineHeight()) * 0.5f;
            dl->AddText(ImVec2(rmin.x + pdguiScale(10.0f), cy),
                        IM_COL32(180, 180, 180, 210), langSafe(L_OPTIONS_254));
        }

        ImGui::PopID();
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Briefing (shared by g_SoloMissionBriefingMenuDialog and
 *            g_PreAndPostMissionBriefingMenuDialog)
 * g_Briefing is populated before this renders by the dialog's C handler.
 * ========================================================================= */

static s32 renderBriefingImpl(struct menudialog *dialog,
                               struct menu *menu,
                               s32 winW, s32 winH,
                               const char *windowId)
{
    float mw  = pdguiMenuWidth();
    float mh  = pdguiMenuHeight();
    ImVec2 pos = pdguiMenuPos();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin(windowId, nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_BriefingScroll = 0.0f;
    }

    float titleH = pdguiScale(26.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, langSafe(L_OPTIONS_247), 1);

    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);
    ImGui::Separator();

    /* Close with B / Escape */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
    }

    /* Scrollable briefing text */
    float footerH = pdguiScale(32.0f);
    float bodyH   = mh - titleH - pdguiScale(24.0f) - footerH;

    if (ImGui::BeginChild("##briefing_scroll", ImVec2(0, bodyH), false,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        const char *txt = (g_Briefing.briefingtextnum != 0)
                          ? langSafe(g_Briefing.briefingtextnum)
                          : "(No briefing text available)";

        ImGui::PushTextWrapPos(mw - pdguiScale(40.0f));
        ImGui::TextUnformatted(txt);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextDisabled("D-Pad/Scroll: Read   B/Esc: Close");

    ImGui::End();
    return 1;
}

static s32 renderSoloBriefing(struct menudialog *dialog,
                               struct menu *menu, s32 winW, s32 winH)
{
    return renderBriefingImpl(dialog, menu, winW, winH, "##solo_briefing");
}

static s32 renderPrePostBriefing(struct menudialog *dialog,
                                  struct menu *menu, s32 winW, s32 winH)
{
    return renderBriefingImpl(dialog, menu, winW, winH, "##prepost_briefing");
}

/* =========================================================================
 * Accept Mission (Overview)
 * Shows mission objectives + Accept / Decline.
 * g_Briefing populated by menudialog00103608 before this renders.
 * ========================================================================= */

static s32 renderAcceptMission(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH)
{
    float mw  = pdguiMenuWidth();
    float mh  = pdguiMenuHeight();
    ImVec2 pos = pdguiMenuPos();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##accept_mission", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_AcceptSelectIdx = 0;  /* default to Accept */
    }

    /* Title = "StageName: Overview" */
    char title[128] = "Overview";
    {
        s32 si = g_MissionConfig.stageindex;
        if (si >= 0 && si < NUM_SOLOSTAGES)
            snprintf(title, sizeof(title), "%s: %s",
                     langSafe(g_SoloStages[si].name3),
                     langSafe(L_OPTIONS_273));
    }

    float titleH = pdguiScale(26.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, title, 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);

    /* Difficulty badge */
    {
        s32 d = SM_DIFFICULTY(&g_MissionConfig);
        const char *diffNames[] = { "Agent", "Special Agent", "Perfect Agent" };
        const char *dn = (d <= DIFF_PA) ? diffNames[d] : "Agent";
        ImGui::TextDisabled("Difficulty: %s", dn);
    }
    ImGui::Separator();

    /* Navigation */
    bool doAccept  = false;
    bool doDecline = false;

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) ||
        ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_AcceptSelectIdx = 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true) ||
        ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_AcceptSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (s_AcceptSelectIdx == 0) doAccept  = true;
        else                        doDecline = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        doDecline = true;
    }

    if (doAccept) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        menuhandlerAcceptMission(MENUOP_SET, nullptr, nullptr);
        ImGui::End();
        return 1;
    }
    if (doDecline) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    /* ---- Objectives list ---- */
    float btnH    = pdguiScale(38.0f);
    float bodyH   = mh - titleH - pdguiScale(50.0f) - btnH * 2.0f - pdguiScale(16.0f);

    if (ImGui::BeginChild("##objectives_scroll", ImVec2(0, bodyH), false,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        bool anyObj = false;
        for (s32 i = 0; i < 6; i++) {
            if (g_Briefing.objectivenames[i] == 0) continue;
            anyObj = true;

            const char *objText = langSafe(g_Briefing.objectivenames[i]);

            /* Difficulty indicators */
            u16 bits = g_Briefing.objectivedifficulties[i];
            char diffStr[16] = "";
            if (bits & 1) strcat(diffStr, "A ");
            if (bits & 2) strcat(diffStr, "SA ");
            if (bits & 4) strcat(diffStr, "PA");

            float dotSz = pdguiScale(8.0f);
            ImVec2 cp = ImGui::GetCursorScreenPos();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled(
                ImVec2(cp.x + dotSz * 0.5f + pdguiScale(4.0f),
                       cp.y + ImGui::GetTextLineHeight() * 0.5f + pdguiScale(2.0f)),
                dotSz * 0.5f,
                IM_COL32(80, 160, 255, 220));

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotSz + pdguiScale(10.0f));
            ImGui::PushTextWrapPos(mw - pdguiScale(60.0f));
            ImGui::TextUnformatted(objText);
            ImGui::PopTextWrapPos();

            if (diffStr[0]) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", diffStr);
            }
            ImGui::Spacing();
        }

        if (!anyObj) {
            ImGui::TextDisabled("(No objectives data)");
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    /* ---- Accept / Decline buttons ---- */
    float btnW  = (mw - ImGui::GetStyle().WindowPadding.x * 2.0f - pdguiScale(10.0f)) * 0.5f;

    auto drawBtn = [&](s32 idx, const char *lbl, ImU32 hlCol) {
        bool sel = (s_AcceptSelectIdx == idx);
        ImVec2 cp = ImGui::GetCursorScreenPos();
        if (sel) pdguiDrawItemHighlight(cp.x, cp.y, btnW, btnH);

        bool clicked = ImGui::Button(lbl, ImVec2(btnW, btnH));
        if (ImGui::IsItemHovered()) s_AcceptSelectIdx = idx;
        return clicked;
    };

    if (drawBtn(0, langSafe(L_OPTIONS_274), IM_COL32(80, 160, 80, 255))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        menuhandlerAcceptMission(MENUOP_SET, nullptr, nullptr);
        ImGui::End();
        return 1;
    }
    ImGui::SameLine(0.0f, pdguiScale(10.0f));
    if (drawBtn(1, langSafe(L_OPTIONS_275), IM_COL32(160, 80, 80, 255))) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Pause Menu
 * Shows stage name, objectives (with completion status), and navigation
 * to Inventory / Options, plus the Abort button.
 * g_Briefing populated by soloMenuDialogPauseStatus before this renders.
 * ========================================================================= */

static s32 renderPauseMenu(struct menudialog *dialog,
                            struct menu *menu,
                            s32 winW, s32 winH)
{
    float mw  = pdguiMenuWidth();
    float mh  = pdguiMenuHeight();
    ImVec2 pos = pdguiMenuPos();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_pause", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_PauseSelectIdx = 0;
    }

    /* Title: "StageName: Status" */
    char title[128] = "Status";
    {
        s32 si = g_MissionConfig.stageindex;
        if (si >= 0 && si < NUM_SOLOSTAGES)
            snprintf(title, sizeof(title), "%s: %s",
                     langSafe(g_SoloStages[si].name3),
                     langSafe(L_OPTIONS_172));
    }

    float titleH = pdguiScale(26.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, title, 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);

    /* Difficulty badge */
    {
        s32 d = SM_DIFFICULTY(&g_MissionConfig);
        const char *dnames[] = { "Agent", "Special Agent", "Perfect Agent" };
        ImGui::TextDisabled("Difficulty: %s", (d <= DIFF_PA) ? dnames[d] : "?");
    }
    ImGui::Separator();

    /* Number of nav items: Resume + Inventory + Options + Abort */
    static const s32 k_NumPauseItems = 4;
    if (s_PauseSelectIdx < 0)                s_PauseSelectIdx = 0;
    if (s_PauseSelectIdx >= k_NumPauseItems) s_PauseSelectIdx = k_NumPauseItems - 1;

    /* Navigation */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) ||
        ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_PauseSelectIdx++;
        if (s_PauseSelectIdx >= k_NumPauseItems) s_PauseSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true) ||
        ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_PauseSelectIdx--;
        if (s_PauseSelectIdx < 0) s_PauseSelectIdx = k_NumPauseItems - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    bool doConfirm = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                     ImGui::IsKeyPressed(ImGuiKey_Enter, false);

    /* ---- Objectives (read-only display) ---- */
    float objH  = mh - titleH - pdguiScale(60.0f) - pdguiScale(38.0f * 4.0f);
    if (objH < pdguiScale(60.0f)) objH = pdguiScale(60.0f);

    if (ImGui::BeginChild("##pause_obj", ImVec2(0, objH), false,
                           ImGuiWindowFlags_None)) {
        bool anyObj = false;
        for (s32 i = 0; i < 6; i++) {
            if (g_Briefing.objectivenames[i] == 0) continue;
            anyObj = true;

            float dotSz = pdguiScale(8.0f);
            ImVec2 cp = ImGui::GetCursorScreenPos();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled(
                ImVec2(cp.x + dotSz * 0.5f + pdguiScale(4.0f),
                       cp.y + ImGui::GetTextLineHeight() * 0.5f + pdguiScale(2.0f)),
                dotSz * 0.5f,
                IM_COL32(80, 160, 255, 200));

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotSz + pdguiScale(10.0f));
            ImGui::PushTextWrapPos(mw - pdguiScale(20.0f));
            ImGui::TextUnformatted(langSafe(g_Briefing.objectivenames[i]));
            ImGui::PopTextWrapPos();
        }
        if (!anyObj) {
            ImGui::TextDisabled("(No mission data)");
        }
    }
    ImGui::EndChild();
    ImGui::Separator();

    /* ---- Action buttons ---- */
    float btnH = pdguiScale(36.0f);

    struct PauseBtn { const char *label; s32 idx; };
    const PauseBtn k_Btns[] = {
        { "Resume",                 0 },
        { langSafe(L_OPTIONS_178),  1 },  /* "Inventory" */
        { "Options",                2 },
        { langSafe(L_OPTIONS_173),  3 },  /* "Abort!" */
    };

    for (s32 b = 0; b < 4; b++) {
        bool isSel = (s_PauseSelectIdx == k_Btns[b].idx);
        ImGui::PushID(b);

        ImVec2 cp = ImGui::GetCursorScreenPos();
        if (isSel) pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(16.0f), btnH);

        /* Abort gets a red tint */
        if (b == 3) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));

        bool clicked = ImGui::Button(k_Btns[b].label,
                                     ImVec2(mw - pdguiScale(16.0f), btnH));
        if (b == 3) ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) s_PauseSelectIdx = k_Btns[b].idx;

        bool doThis = clicked || (isSel && doConfirm);
        if (doThis) {
            switch (b) {
            case 0: /* Resume */
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                menuPopDialog();
                break;
            case 1: /* Inventory (uses legacy 3D renderer via NULL registration) */
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                menuPushDialog(&g_SoloMissionInventoryMenuDialog);
                break;
            case 2: /* Options */
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                menuPushDialog(&g_SoloMissionOptionsMenuDialog);
                break;
            case 3: /* Abort! */
                pdguiPlaySound(PDGUI_SND_ERROR);
                menuPushDialog(&g_MissionAbortMenuDialog);
                break;
            }
            ImGui::PopID();
            ImGui::End();
            return 1;
        }

        ImGui::PopID();
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Abort Mission (Danger dialog)
 * ========================================================================= */

static s32 renderAbortMission(struct menudialog *dialog,
                               struct menu *menu,
                               s32 winW, s32 winH)
{
    float mw  = pdguiMenuWidth() * 0.55f;
    float mh  = pdguiMenuHeight() * 0.35f;
    ImVec2 pos = pdguiCenterPos(mw, mh);

    /* Switch to danger (red) palette for this dialog */
    s32 prevPalette = pdguiGetPalette();
    pdguiSetPalette(2);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##abort_mission", nullptr, wf)) {
        pdguiSetPalette(prevPalette);
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_AbortSelectIdx = 0;  /* default to Cancel (safer) */
    }

    float titleH = pdguiScale(26.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, langSafe(L_OPTIONS_174), 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);

    /* Warning text */
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x + pdguiScale(8.0f));
    ImGui::PushTextWrapPos(mw - pdguiScale(16.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.85f, 1.0f), "%s", langSafe(L_OPTIONS_175));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();

    /* Navigation */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true)  ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) ||
        ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)        ||
        ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
        s_AbortSelectIdx = 1 - s_AbortSelectIdx;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    /* B / Escape always cancels — safety default */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        pdguiSetPalette(prevPalette);
        ImGui::End();
        return 1;
    }

    bool doConfirm = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                     ImGui::IsKeyPressed(ImGuiKey_Enter, false);

    /* ---- Cancel / Abort buttons side by side ---- */
    float btnH = pdguiScale(36.0f);
    float btnW = (mw - ImGui::GetStyle().WindowPadding.x * 2.0f - pdguiScale(10.0f)) * 0.5f;

    /* Cancel */
    {
        bool isSel = (s_AbortSelectIdx == 0);
        ImVec2 cp = ImGui::GetCursorScreenPos();
        if (isSel) pdguiDrawItemHighlight(cp.x, cp.y, btnW, btnH);

        bool clicked = ImGui::Button(langSafe(L_OPTIONS_176), ImVec2(btnW, btnH));
        if (ImGui::IsItemHovered()) s_AbortSelectIdx = 0;
        if (clicked || (isSel && doConfirm)) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
            pdguiSetPalette(prevPalette);
            ImGui::End();
            return 1;
        }
    }

    ImGui::SameLine(0.0f, pdguiScale(10.0f));

    /* Abort */
    {
        bool isSel = (s_AbortSelectIdx == 1);
        ImVec2 cp = ImGui::GetCursorScreenPos();
        if (isSel) pdguiDrawItemHighlight(cp.x, cp.y, btnW, btnH);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        bool clicked = ImGui::Button(langSafe(L_OPTIONS_177), ImVec2(btnW, btnH));
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) s_AbortSelectIdx = 1;
        if (clicked || (isSel && doConfirm)) {
            pdguiPlaySound(PDGUI_SND_EXPLOSION);
            menuhandlerAbortMission(MENUOP_SET, nullptr, nullptr);
            pdguiSetPalette(prevPalette);
            ImGui::End();
            return 1;
        }
    }

    pdguiSetPalette(prevPalette);
    ImGui::End();
    return 1;
}

/* =========================================================================
 * Options Hub
 * Audio / Video / Control / Display / Extended — each pushes sub-dialog.
 * ========================================================================= */

static s32 renderOptions(struct menudialog *dialog,
                          struct menu *menu,
                          s32 winW, s32 winH)
{
    float mw  = pdguiMenuWidth() * 0.55f;
    float mh  = pdguiMenuHeight() * 0.55f;
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_options", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_OptionsSelectIdx = 0;
    }

    float titleH = pdguiScale(26.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, "Options", 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);
    ImGui::Separator();

    struct OptionBtn { const char *label; struct menudialogdef *dlg; };
    OptionBtn k_Opts[] = {
        { langSafe(L_OPTIONS_181), &g_AudioOptionsMenuDialog         },
        { langSafe(L_OPTIONS_182), &g_VideoOptionsMenuDialog         },
        { langSafe(L_OPTIONS_183), &g_MissionControlOptionsMenuDialog},
        { langSafe(L_OPTIONS_184), &g_MissionDisplayOptionsMenuDialog},
        { "Extended",              &g_ExtendedMenuDialog             },
    };
    static const s32 k_NumOpts = 5;

    if (s_OptionsSelectIdx < 0)          s_OptionsSelectIdx = 0;
    if (s_OptionsSelectIdx >= k_NumOpts) s_OptionsSelectIdx = k_NumOpts - 1;

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) ||
        ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_OptionsSelectIdx++;
        if (s_OptionsSelectIdx >= k_NumOpts) s_OptionsSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true) ||
        ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_OptionsSelectIdx--;
        if (s_OptionsSelectIdx < 0) s_OptionsSelectIdx = k_NumOpts - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    float btnH = pdguiScale(36.0f);

    for (s32 i = 0; i < k_NumOpts; i++) {
        bool isSel = (s_OptionsSelectIdx == i);
        ImGui::PushID(i);

        ImVec2 cp = ImGui::GetCursorScreenPos();
        if (isSel) pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(16.0f), btnH);

        bool clicked = ImGui::Button(k_Opts[i].label,
                                     ImVec2(mw - pdguiScale(16.0f), btnH));
        if (ImGui::IsItemHovered()) s_OptionsSelectIdx = i;

        bool doThis = clicked ||
                      (isSel && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                                 ImGui::IsKeyPressed(ImGuiKey_Enter, false)));
        if (doThis) {
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            menuPushDialog(k_Opts[i].dlg);
            ImGui::PopID();
            ImGui::End();
            return 1;
        }

        ImGui::PopID();
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" {

void pdguiMenuSoloMissionRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    /* ---- Full ImGui replacements ---- */
    pdguiHotswapRegister(&g_SelectMissionMenuDialog,
                          renderMissionSelect,         "Mission Select");

    pdguiHotswapRegister(&g_SoloMissionDifficultyMenuDialog,
                          renderDifficulty,            "Solo Difficulty");

    pdguiHotswapRegister(&g_SoloMissionBriefingMenuDialog,
                          renderSoloBriefing,          "Solo Briefing");

    pdguiHotswapRegister(&g_PreAndPostMissionBriefingMenuDialog,
                          renderPrePostBriefing,       "Pre/Post Briefing");

    pdguiHotswapRegister(&g_AcceptMissionMenuDialog,
                          renderAcceptMission,         "Accept Mission");

    pdguiHotswapRegister(&g_SoloMissionPauseMenuDialog,
                          renderPauseMenu,             "Solo Pause");

    pdguiHotswapRegister(&g_MissionAbortMenuDialog,
                          renderAbortMission,          "Abort Mission");

    pdguiHotswapRegister(&g_SoloMissionOptionsMenuDialog,
                          renderOptions,               "Solo Options");

    /* ---- Keep legacy rendering — these have 3D model/controller previews ----
     * Registering with NULL renderFn forces PD-native rendering for these
     * dialogs while still blocking type-based fallback renderers. */
    pdguiHotswapRegister(&g_SoloMissionInventoryMenuDialog,
                          nullptr,  "Inventory (legacy)");

    pdguiHotswapRegister(&g_FrWeaponsAvailableMenuDialog,
                          nullptr,  "Fr. Weapons (legacy)");

    pdguiHotswapRegister(&g_SoloMissionControlStyleMenuDialog,
                          nullptr,  "Control Style (legacy)");

    sysLogPrintf(LOG_NOTE,
        "pdgui_menu_solomission: Registered Group 1 — 8 ImGui + 3 legacy");
}

} /* extern "C" */
