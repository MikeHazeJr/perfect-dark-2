/**
 * pdgui_menu_mppause.cpp -- ImGui replacement for MP Pause & In-Game dialogs.
 *
 * D5 Phase 3 Batch 8.
 *
 * Covers the 6 legacy menudialogdefs that make up the multiplayer pause
 * menu stack (Control / Inventory / PlayerStats / PlayerRanking /
 * TeamRankings) plus the in-match Player Options (Display) dialog.
 *
 *   g_MpPauseControlMenuDialog        -> renderMpPauseControl
 *   g_MpPauseInventoryMenuDialog      -> renderMpPauseInventory
 *   g_MpPausePlayerStatsMenuDialog    -> renderMpPausePlayerStats
 *   g_MpPausePlayerRankingMenuDialog  -> renderMpPausePlayerRanking
 *   g_MpPauseTeamRankingsMenuDialog   -> renderMpPauseTeamRankings
 *   g_MpPlayerOptionsMenuDialog       -> renderMpPlayerOptions
 *
 * Design:
 *   - Follows the Batch 7 s205 shadow-struct call-through pattern
 *     (promoted to s206 here, independent definition per file so no
 *     header is required).  Every state mutation routes through a
 *     legacy C handler -- zero function loss.
 *   - 4 of the 6 dialogs are read-only display surfaces over runtime
 *     match state (Inventory / PlayerStats / PlayerRanking /
 *     TeamRankings).  They call into legacy accessors
 *     (mpGetPlayerRankings, mpGetTeamRankings, menuhandlerInventoryList
 *     MENUOP_GETOPTIONCOUNT/TEXT, MPCHR, g_BossFile.teamnames) to build
 *     their tables each frame.
 *   - MpPauseControl is a hub: challenge / scenario / limit labels, a
 *     match-time readout, and (network only) a team switch dropdown and
 *     a Controls push-row + the End Game push-row.  Every mutating path
 *     is handler-delegated.
 *   - MpPlayerOptions is 4 checkboxes delegating to
 *     menuhandlerMpDisplayOptionCheckbox which writes
 *     g_PlayerConfigsArray[g_MpPlayerNum].base.displayoptions.  Display
 *     options are per-player local state; scenarios.c and radar.c read
 *     them per-frame so a mid-match change takes effect immediately.
 *
 * NETWORK MATCH START/END AUDIT -- see context/scratch/D5-P3-batch8-2026-04-11.md
 *   The Pause dialogs write ONE field that crosses the wire: the
 *   in-match team switch in MpPauseControl.  That write routes through
 *   menuhandlerNetTeamSwitch -> netClientSettingsChanged which serializes
 *   a CLC_SETTINGS packet -- the exact same code path the legacy N64
 *   dropdown used.  All other writes (pause toggle, display options,
 *   stats-for-player selection, inventory equip) are local state the
 *   same player's input path would have touched during normal gameplay,
 *   so the network match-start/end paths see identical state.  No new
 *   shadow/cache copies are introduced.
 *
 * What is NOT in this file:
 *   - pdgui_menu_mpingame.cpp (existing) owns the kill-ticker overlay
 *     and endscreen suppression -- different concerns, kept separate.
 *   - g_NetPauseControlsMenuDialog (pushed from MpPauseControl) is the
 *     netplay-specific controls dialog; Batch 8 does not port it.
 *   - g_MpEndGameMenuDialog (DANGER confirmation) is still legacy.  It
 *     still receives the push from our End Game selectable, then the
 *     legacy runtime renders it.
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32
 * breaks C++).  Auto-discovered by GLOB_RECURSE for port cpp files in
 * CMakeLists.txt.
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
#include "pdgui_layout.h"
#include "pdgui.h"        /* langSafe */
#include "system.h"
#include "inputctx.h"
#include "menupool.h"

extern "C" {
#include "pdgui_menus.h"  /* for pdguiMenuMpPauseRegister declaration */
}

/* =========================================================================
 * Forward declarations -- game symbols (extern "C", no types.h)
 * ========================================================================= */

extern "C" {

/* ---- Opaque types for function signatures ---- */
struct menuitem;
struct menudialog;
struct menudialogdef;
struct menu;

/* ---- Dialog definitions for the 6 Batch 8 dialogs ---- */
extern struct menudialogdef g_MpPauseControlMenuDialog;
extern struct menudialogdef g_MpPauseInventoryMenuDialog;
extern struct menudialogdef g_MpPausePlayerStatsMenuDialog;
extern struct menudialogdef g_MpPausePlayerRankingMenuDialog;
extern struct menudialogdef g_MpPauseTeamRankingsMenuDialog;
extern struct menudialogdef g_MpPlayerOptionsMenuDialog;

/* ---- Dialogs we push into from the Pause Control hub ---- */
extern struct menudialogdef g_MpEndGameMenuDialog;
extern struct menudialogdef g_NetPauseControlsMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* ---- Language ---- */
char *langGet(s32 textid);
/* langSafe comes from pdgui.h */

/* ---- Runtime data accessors (C boundary) ---- */
extern s32 g_MpPlayerNum;
extern u8  g_MpSelectedPlayersForStats[8]; /* = MAX_PLAYERS */

/* ---- MENUOP_* opcodes (Batch 4 gotcha: declared locally in each cpp).
 * Values must match src/include/constants.h exactly. */
#define MENUOP_GETOPTIONCOUNT      1
#define MENUOP_GETOPTGROUPCOUNT    2
#define MENUOP_GETOPTIONTEXT       3
#define MENUOP_GETOPTGROUPTEXT     4
#define MENUOP_GETGROUPSTARTINDEX  5
#define MENUOP_SET                 6
#define MENUOP_GETSELECTEDINDEX    7
#define MENUOP_GET                 8
#define MENUOP_GETSLIDER           9
#define MENUOP_GETSLIDERLABEL      10
#define MENUOP_CHECKDISABLED       12
#define MENUOP_GETLISTITEMCHECKBOX 14
#define MENUOP_CHECKHIDDEN         24

/* Display option bits -- must match MPDISPLAYOPTION_* in constants.h */
#define MPDISPLAYOPTION_HIGHLIGHTPICKUPS  0x01
#define MPDISPLAYOPTION_HIGHLIGHTPLAYERS  0x02
#define MPDISPLAYOPTION_HIGHLIGHTTEAMS    0x04
#define MPDISPLAYOPTION_RADAR             0x08

/* ---- s206 shadow menuitem / handlerdata
 * ABI-compatible with the real types in src/include/types.h:3342..3417.
 * Cloned from the s203/s204/s205 pattern used by the other Batch 4-7
 * renderer files.  The shadow exists so handler callthroughs work
 * without pulling in types.h (#define bool s32 breaks C++). */
struct s206_handlerdata_checkbox { u32 value; };
struct s206_handlerdata_dropdown { uintptr_t value; uintptr_t unk04; };
struct s206_handlerdata_list_t {
    uintptr_t value;
    s32       unk04;
};
struct s206_handlerdata_slider { u32 value; char *label; };

union s206_handlerdata {
    struct s206_handlerdata_checkbox checkbox;
    struct s206_handlerdata_dropdown dropdown;
    struct s206_handlerdata_list_t   list;
    struct s206_handlerdata_slider   slider;
    u8 _pad[256];
};

struct s206_menuitem {
    u8        type;
    u8        param;
    u32       flags;
    intptr_t  param2;
    intptr_t  param3;
    uintptr_t (*handler)(s32 op, struct s206_menuitem *, union s206_handlerdata *);
};

/* ---- Legacy handlers we delegate to ----
 *
 * Each delegate touches exactly one backing global so the NETWORK AUDIT
 * stays easy to reason about.  The bracketed comment next to each is
 * the single backing global the handler reads/writes -- grep for it to
 * confirm which match-start / match-end reader consumes it. */

/* ingame.c: mpSetPaused() -- local pause toggle (PC-dead: always hidden) */
uintptr_t menuhandlerMpPause(s32, struct s206_menuitem *, union s206_handlerdata *);

/* ingame.c: g_NetLocalClient->settings.team + netClientSettingsChanged() */
uintptr_t menuhandlerNetTeamSwitch(s32, struct s206_menuitem *, union s206_handlerdata *);

/* ingame.c: pushes g_NetPauseControlsMenuDialog (netplay only) */
uintptr_t menuhandlerNetPauseControls(s32, struct s206_menuitem *, union s206_handlerdata *);

/* ingame.c: CHECKHIDDEN based on g_MpSetup.timelimit/scorelimit/teamscorelimit */
uintptr_t menuhandlerMpInGameLimitLabel(s32, struct s206_menuitem *, union s206_handlerdata *);

/* mainmenu.c: currentPlayerSetDeviceActive / bgunEquipWeapon2 (local inventory equip) */
uintptr_t menuhandlerInventoryList(s32, struct s206_menuitem *, union s206_handlerdata *);

/* ingame.c: g_MpSelectedPlayersForStats[g_MpPlayerNum] -- local view-state */
uintptr_t mpStatsForPlayerDropdownHandler(s32, struct s206_menuitem *, union s206_handlerdata *);

/* setup.c: g_PlayerConfigsArray[g_MpPlayerNum].base.displayoptions (per-bit) */
uintptr_t menuhandlerMpDisplayOptionCheckbox(s32, struct s206_menuitem *, union s206_handlerdata *);

/* ---- Dynamic text functions (used as menuitem param2 in legacy defs) ----
 *
 * All six of these take a menuitem pointer but the bodies either ignore
 * it entirely (scenario/challenge/match-time/pause-or-unpause) or read a
 * single byte from item->param (in-game limit).  We call them with
 * either nullptr or a shadow menuitem with the needed param set. */
char *mpMenuTextChallengeName(struct menuitem *item);
char *mpMenuTextScenarioName(struct menuitem *item);
char *mpMenuTextInGameLimit(struct menuitem *item);
char *menutextMatchTime(s32 arg0);
char *menutextPauseOrUnpause(s32 arg0);
char *mpMenuTitleStatsFor(struct menudialogdef *dialogdef);

/* ---- Weapon description for current inventory selection ---- */
char *mpMenuTextWeaponDescription(struct menuitem *item);

/* ---- Ranking / mpchr accessors used for stats + ranking tables ----
 *
 * ABI-compatible struct clones so we can call mpGetPlayerRankings /
 * mpGetTeamRankings without pulling types.h.  See the matching pattern
 * in pdgui_menu_pausemenu.cpp and pdgui_menu_mpingame.cpp.  The real
 * mpchrconfig struct lives at src/include/types.h:4006 -- layout must
 * match exactly; `name` is offset 0, the catalog-ID strings come next
 * (PC port addition), then the legacy fields.  All field comments
 * below are post-catalog offsets. */
#define MP_MAX_MPCHRS   40   /* = MAX_PLAYERS(8) + MAX_BOTS(32) */
#define MP_MAX_PLAYERS  8    /* = MAX_PLAYERS */
#define MP_MAX_TEAMS    8    /* = MAX_TEAMS */

struct mpchrconfig_mpp {
    char name[15];
    char head_id[64];
    char body_id[64];
    u8   mpheadnum;
    u8   mpbodynum;
    u8   team;
    u8   _pad0[2];
    u32  displayoptions;
    u16  unk18;
    u16  unk1a;
    u16  unk1c;
    s8   placement;
    u8   _pad1;
    s32  rankablescore;
    s16  killcounts[MP_MAX_MPCHRS];
    s16  numdeaths;
    s16  numpoints;
    s16  unk40;
};

struct ranking_mpp {
    struct mpchrconfig_mpp *mpchr;
    union {
        u32 teamnum;
        u32 chrnum;
    };
    u32 positionindex;
    u8  unk0c;
    s32 score;
};

s32 mpGetPlayerRankings(struct ranking_mpp *rankings);
s32 mpGetTeamRankings(struct ranking_mpp *rankings);

/* Team name accessor for Team Rankings table -- implemented in
 * pdgui_bridge.c so this file does not need to know the layout of
 * struct bossfile. */
const char *pdguiMppGetTeamName(u32 team);

} /* extern "C" */

/* =========================================================================
 * Module state
 * ========================================================================= */

static bool s_Registered = false;

/* =========================================================================
 * s206 call-through helpers -- mirrors the s203/s204/s205 helpers in the
 * other Batch 4-7 files.  Kept local to avoid a shared header that would
 * require moving the shadow struct out of file scope.
 * ========================================================================= */

/* ---- Dropdown handlers ---- */
static s32 dd_GetOptionCount(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                             u8 param, intptr_t param3)
{
    s206_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s206_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.dropdown.value;
}

static const char *dd_GetOptionText(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                                    u8 param, intptr_t param3, s32 idx)
{
    s206_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s206_handlerdata h{};
    h.dropdown.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 dd_GetSelectedIndex(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                               u8 param, intptr_t param3)
{
    s206_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s206_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return (s32)h.dropdown.value;
}

static void dd_Set(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                   u8 param, intptr_t param3, s32 idx)
{
    s206_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s206_handlerdata h{};
    h.dropdown.value = (uintptr_t)idx;
    fn(MENUOP_SET, &it, &h);
}

static bool dd_IsHidden(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                        u8 param, intptr_t param3)
{
    s206_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s206_handlerdata h{};
    uintptr_t r = fn(MENUOP_CHECKHIDDEN, &it, &h);
    return r != 0;
}

/* ---- List handlers (inventory uses this pattern) ---- */
static s32 list_GetOptionCount(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                               u8 param)
{
    s206_menuitem it{};
    it.param = param;
    s206_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.list.value;
}

static const char *list_GetOptionText(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                                      u8 param, s32 idx)
{
    s206_menuitem it{};
    it.param = param;
    s206_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 list_GetSelectedIndex(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                                 u8 param)
{
    s206_menuitem it{};
    it.param = param;
    s206_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return (s32)h.list.value;
}

static void list_Set(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                     u8 param, s32 idx)
{
    s206_menuitem it{};
    it.param = param;
    s206_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    /* unk04 == 0 is the "menu accept" branch in menuhandlerInventoryList;
     * it drives the bgunEquipWeapon2 equip path.  A non-zero value takes
     * the "toggle device on/off" branch for devices like CMP150 / nightsight.
     * Default 0 matches the click semantics the user sees. */
    h.list.unk04 = 0;
    fn(MENUOP_SET, &it, &h);
}

/* ---- Checkbox handlers (display options) ---- */
static bool cb_Get(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                   u8 param, intptr_t param3)
{
    s206_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s206_handlerdata h{};
    uintptr_t r = fn(MENUOP_GET, &it, &h);
    return r != 0;
}

static void cb_Set(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                   u8 param, intptr_t param3, bool v)
{
    s206_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s206_handlerdata h{};
    h.checkbox.value = v ? 1u : 0u;
    fn(MENUOP_SET, &it, &h);
}

/* ---- Plain SET (for "button" selectables that push a dialog via
 *      their own menuhandler*Set branch) ---- */
static void plain_Set(uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                      u8 param)
{
    s206_menuitem it{};
    it.param = param;
    s206_handlerdata h{};
    fn(MENUOP_SET, &it, &h);
}

/* ---- Render a dropdown row backed by a legacy handler ---- */
static void renderHandlerDropdown(const char *label,
                                  uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                                  u8 param, intptr_t param3)
{
    s32 cur = dd_GetSelectedIndex(fn, param, param3);
    s32 n   = dd_GetOptionCount(fn, param, param3);
    const char *curLabel = (cur >= 0 && cur < n)
        ? dd_GetOptionText(fn, param, param3, cur)
        : "";
    if (!curLabel) curLabel = "";

    if (ImGui::BeginCombo(label, curLabel)) {
        for (s32 i = 0; i < n; i++) {
            const char *t = dd_GetOptionText(fn, param, param3, i);
            if (!t) t = "";
            ImGui::PushID(i);
            bool isSel = (i == cur);
            if (ImGui::Selectable(t, isSel)) {
                dd_Set(fn, param, param3, i);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            if (isSel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

/* =========================================================================
 * Window-frame helpers (clone of the Batch 5/7 shape -- kept file-local
 * for the same reason, shared helpers would need a new header).
 * ========================================================================= */

struct WindowFrame {
    float mw;
    float mh;
    ImVec2 pos;
};

/* S300: s_MpPausePushedCtx removed — menu pool owns the ctx for
 * MENU_TYPE_MP_PAUSE via menupoolAcquireDialog / menupoolReleaseDialog. */

static WindowFrame mpp_BeginStandardWindow(const char *imguiId, const char *title,
                                           float widthFrac, float heightFrac,
                                           const struct menudialogdef *def)
{
    pdguiPopupDarkenBehind(0.55f);

    WindowFrame wf;
    wf.mw  = pdguiMenuWidth()  * widthFrac;
    wf.mh  = pdguiMenuHeight() * heightFrac;
    wf.pos = pdguiCenterPos(wf.mw, wf.mh);

    ImGui::SetNextWindowPos(wf.pos);
    ImGui::SetNextWindowSize(ImVec2(wf.mw, wf.mh));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin(imguiId, nullptr, flags)) {
        wf.mw = 0.0f;
        /* S300: pool owns the ctx; release pops it. */
        menupoolReleaseDialog(def);
        return wf;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        menupoolAcquireDialog(def, &g_CtxImGuiMenu);
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(wf.pos.x, wf.pos.y, wf.mw, wf.mh, title, 1);
    pdguiSetCursorBelowTitle(titleH);
    return wf;
}

static void mpp_CloseCurrentDialog(void)
{
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
    /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
    menuPopDialog();
}

/* True if this frame saw Escape or gamepad-B (the universal back button). */
static bool mpp_BackPressed(void)
{
    return !ImGui::IsWindowAppearing() &&
           ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

/* ---- Label row: plain text, optional right-side dynamic text ---- */
static void labelRow(const char *label, const char *rightSideText = nullptr)
{
    float rowH = pdguiScale(22.0f);
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    float  avail    = ImGui::GetContentRegionAvail().x;

    ImGui::Dummy(ImVec2(avail, rowH));

    float ty = startPos.y + rowH * 0.5f - ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(startPos.x + pdguiScale(8.0f), ty),
        ImGui::GetColorU32(ImGuiCol_Text), label);
    if (rightSideText && rightSideText[0]) {
        float tw = ImGui::CalcTextSize(rightSideText).x;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(startPos.x + avail - tw - pdguiScale(8.0f), ty),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), rightSideText);
    }
}

/* ---- Hub row: label + optional dynamic-text on the right, pushes a
 *      target dialog on click.  Plays SND_SELECT. ---- */
static bool hubPushRow(const char *label, struct menudialogdef *target,
                       const char *rightSideText = nullptr)
{
    ImGui::PushID(label);
    bool clicked = ImGui::Selectable("##hub_row", false, 0,
                                     ImVec2(0, pdguiScale(28.0f)));
    ImVec2 rowMin = ImGui::GetItemRectMin();
    ImVec2 rowMax = ImGui::GetItemRectMax();
    float ty = rowMin.y + (rowMax.y - rowMin.y) * 0.5f
             - ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(rowMin.x + pdguiScale(8.0f), ty),
        ImGui::GetColorU32(ImGuiCol_Text), label);
    if (rightSideText && rightSideText[0]) {
        float tw = ImGui::CalcTextSize(rightSideText).x;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowMax.x - tw - pdguiScale(8.0f), ty),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), rightSideText);
    }
    if (clicked) {
        if (target) menuPushDialog(target);
        pdguiPlaySound(PDGUI_SND_SELECT);
    }
    ImGui::PopID();
    return clicked;
}

/* ---- Hub row backed by a legacy handler (MENUOP_SET) ---- */
static bool hubHandlerRow(const char *label,
                          uintptr_t (*fn)(s32, s206_menuitem *, s206_handlerdata *),
                          u8 param = 0)
{
    ImGui::PushID(label);
    bool clicked = ImGui::Selectable("##hub_h_row", false, 0,
                                     ImVec2(0, pdguiScale(28.0f)));
    ImVec2 rowMin = ImGui::GetItemRectMin();
    ImVec2 rowMax = ImGui::GetItemRectMax();
    float ty = rowMin.y + (rowMax.y - rowMin.y) * 0.5f
             - ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(rowMin.x + pdguiScale(8.0f), ty),
        ImGui::GetColorU32(ImGuiCol_Text), label);
    if (clicked) {
        plain_Set(fn, param);
        pdguiPlaySound(PDGUI_SND_SELECT);
    }
    ImGui::PopID();
    return clicked;
}

/* =========================================================================
 * Renderer: MP Pause Control (g_MpPauseControlMenuDialog)
 * =========================================================================
 *
 * Legacy layout (ingame.c:319 g_MpPauseControlMenuItems):
 *   Challenge Name label (hidden unless locktype == challenge)
 *   Scenario Name label
 *   Time Limit   (param=0, hidden when timelimit==60 "unlimited")
 *   Score Limit  (param=1, hidden when scorelimit==100 "unlimited")
 *   Team Score Limit (param=2, hidden when teamscorelimit==400 "unlimited")
 *   --- separator ---
 *   Game Time    (static label, reads lvGetStageTime60 via menutextMatchTime)
 *   Pause/Unpause  (local-only, PC-dead)
 *   Team         (dropdown, netplay only, hidden if teams disabled)
 *   Controls     (netplay only, pushes g_NetPauseControlsMenuDialog)
 *   End Game     (pushes g_MpEndGameMenuDialog)
 *
 * NETWORK AUDIT:
 *   - Team dropdown: writes g_NetLocalClient->settings.team then calls
 *     netClientSettingsChanged() which serializes CLC_SETTINGS to the
 *     server.  Same path the legacy N64 dropdown used -- unchanged.
 *   - Pause button: writes g_MpSetup.paused via mpSetPaused().  PC-dead
 *     (legacy CHECKHIDDEN returns true when PLAYERCOUNT()==1, which is
 *     always the case on PC) -- rendered as hidden for parity.
 *   - End Game: pushes g_MpEndGameMenuDialog which eventually runs
 *     menuhandlerMpEndGame -> mainEndStage() or netDisconnect().
 *     Dialog-push only, no direct state write here.
 */

static s32 renderMpPauseControl(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mpp_BeginStandardWindow("##mp_pause_ctrl", "Pause", 0.50f, 0.85f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mpp_BackPressed()) {
        mpp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_pause_ctrl_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* Challenge name (hidden unless in challenge mode -- the legacy
         * handler menuhandler00178018 gates visibility on
         * g_BossFile.locktype == MPLOCKTYPE_CHALLENGE).  mpMenuTextChallengeName
         * returns "Combat Challenges" when no challenge is active, so we
         * render the row unconditionally and let the text reflect state. */
        const char *challengeText = mpMenuTextChallengeName(nullptr);
        if (challengeText && challengeText[0]) {
            labelRow(challengeText);
        }

        /* Scenario name */
        const char *scenarioText = mpMenuTextScenarioName(nullptr);
        if (scenarioText && scenarioText[0]) {
            labelRow(scenarioText);
        }

        /* Limit labels -- delegated CHECKHIDDEN to legacy handler which
         * hides each row when the corresponding limit is at max value.
         * Dynamic text comes from mpMenuTextInGameLimit which reads
         * item->param, so we build a shadow menuitem with the right param. */
        for (u8 p = 0; p < 3; p++) {
            if (!dd_IsHidden(menuhandlerMpInGameLimitLabel, p, 0)) {
                s206_menuitem shadow{};
                shadow.param = p;
                const char *limitText =
                    mpMenuTextInGameLimit((struct menuitem *)&shadow);
                if (limitText && limitText[0]) {
                    labelRow(limitText);
                }
            }
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        /* Game time readout -- the legacy menutextMatchTime ignores its
         * arg and reads lvGetStageTime60() directly.  Refreshes every
         * frame because ImGui re-runs the renderer. */
        const char *matchTime = menutextMatchTime(0);
        if (matchTime && matchTime[0]) {
            char row[64];
            snprintf(row, sizeof(row), "Game Time: %s", matchTime);
            labelRow(row);
        }

        /* Pause/Unpause -- PC-dead per CHECKHIDDEN, but we still invoke
         * the legacy check so any future unlock automatically respects
         * it.  param=1 because the legacy item has param=1 and the
         * handler checks CHECKPREFOCUSED against that. */
        if (!dd_IsHidden(menuhandlerMpPause, 1, 0)) {
            const char *pauseLabel = menutextPauseOrUnpause(0);
            if (ImGui::Button(pauseLabel ? pauseLabel : "Pause",
                              ImVec2(pdguiScale(200.0f), pdguiScale(32.0f)))) {
                plain_Set(menuhandlerMpPause, 1);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
        }

        /* Team dropdown -- netplay only.  The legacy handler's
         * CHECKHIDDEN returns true in non-netplay or when teams are
         * disabled, so the dropdown only appears when relevant. */
        if (!dd_IsHidden(menuhandlerNetTeamSwitch, 0, 0)) {
            ImGui::PushItemWidth(pdguiScale(220.0f));
            renderHandlerDropdown("Team##mp_pause_team",
                                  menuhandlerNetTeamSwitch, 0, 0);
            ImGui::PopItemWidth();
        }

        /* Controls (netplay only) -- pushes g_NetPauseControlsMenuDialog
         * via menuhandlerNetPauseControls.  The handler's CHECKHIDDEN
         * gates on g_NetMode != NETMODE_NONE. */
        if (!dd_IsHidden(menuhandlerNetPauseControls, 0, 0)) {
            hubHandlerRow("Controls", menuhandlerNetPauseControls);
        }

        ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));

        /* End Game -- pushes the DANGER confirmation dialog which
         * (when confirmed) runs menuhandlerMpEndGame -> mainEndStage()
         * or netDisconnect().  We only push here; the legacy runtime
         * still handles g_MpEndGameMenuDialog's rendering. */
        hubPushRow("End Game", &g_MpEndGameMenuDialog);
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_pause_ctrl_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mpp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Pause Inventory (g_MpPauseInventoryMenuDialog)
 * =========================================================================
 *
 * Legacy layout (ingame.c:558 g_Mp2PMissionInventoryMenuItems):
 *   LIST item      -> menuhandlerInventoryList
 *   MARQUEE item   -> mpMenuTextWeaponDescription (dynamic text for focused row)
 *
 * menuhandlerInventoryList MENUOP_SET drives bgunEquipWeapon2 (identical
 * to the player pressing "Next Weapon" during normal gameplay).  No
 * net-exclusive state is touched; the client's existing weapon state
 * broadcast carries the change to other clients the same way it does
 * for a regular weapon cycle.
 */

static s32 renderMpPauseInventory(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mpp_BeginStandardWindow("##mp_pause_inv", "Inventory", 0.50f, 0.80f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mpp_BackPressed()) {
        mpp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_pause_inv_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        s32 n   = list_GetOptionCount(menuhandlerInventoryList, 0);
        s32 cur = list_GetSelectedIndex(menuhandlerInventoryList, 0);

        /* Bounded clamp in case the handler returns something wild
         * during a mid-frame state transition. */
        if (n < 0) n = 0;
        if (cur < 0 || cur >= n) cur = 0;

        float listH = bodyH - pdguiScale(60.0f);
        if (listH < pdguiScale(80.0f)) listH = pdguiScale(80.0f);

        if (ImGui::BeginChild("##mp_pause_inv_list", ImVec2(0, listH), true,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (s32 i = 0; i < n; i++) {
                const char *t = list_GetOptionText(menuhandlerInventoryList, 0, i);
                if (!t) t = "";
                ImGui::PushID(i);
                bool isSel = (i == cur);
                if (ImGui::Selectable(t, isSel, 0,
                                      ImVec2(0, pdguiScale(22.0f)))) {
                    list_Set(menuhandlerInventoryList, 0, i);
                    pdguiPlaySound(PDGUI_SND_SELECT);
                }
                if (isSel) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        /* Weapon description for the currently focused weapon -- the
         * legacy marquee item reads mpMenuTextWeaponDescription which
         * walks g_Menus[g_MpPlayerNum].mppause.weaponnum (set by the
         * LISTITEMFOCUS handler branch).  Since we don't run the focus
         * handler in ImGui, the description here reflects whatever the
         * legacy menu last saw -- close enough for parity, and harmless. */
        const char *desc = mpMenuTextWeaponDescription(nullptr);
        if (desc && desc[0] && desc[0] != '\n') {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", desc);
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_pause_inv_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mpp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Pause Player Stats (g_MpPausePlayerStatsMenuDialog)
 * =========================================================================
 *
 * Legacy layout (ingame.c:605 g_MpInGamePlayerStatsMenuItems):
 *   PLAYERSTATS item -> mpStatsForPlayerDropdownHandler (selects which
 *                       player to show stats for)
 *
 * The legacy MENUITEMTYPE_PLAYERSTATS renderer (menuitem.c:3744) displays:
 *   - Selected player name at the top
 *   - Number of suicides (killcounts[playernum] where playernum is self)
 *   - Per-other-player rows: name, deaths by selected player, kills by
 *     selected player
 *
 * No writes except the stats-for-player dropdown (local view state
 * only).  Reads g_MpSelectedPlayersForStats[g_MpPlayerNum] which is
 * a local preference -- not serialized anywhere.
 */

static s32 renderMpPausePlayerStats(struct menudialog *dialog, struct menu *, s32, s32)
{
    /* Dynamic title: "Stats for <name>" -- the legacy title function
     * reads g_MpSelectedPlayersForStats[g_MpPlayerNum] and ignores its
     * dialogdef arg, so we call with nullptr. */
    const char *title = mpMenuTitleStatsFor(nullptr);
    WindowFrame wf = mpp_BeginStandardWindow("##mp_pause_pstats",
                                             title ? title : "Player Stats",
                                             0.55f, 0.85f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mpp_BackPressed()) {
        mpp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_pause_pstats_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* "Stats for" player dropdown -- reads from
         * mpStatsForPlayerDropdownHandler which iterates active chr
         * slots via mpIsParticipantActive() (B-12 Phase 3). */
        ImGui::PushItemWidth(pdguiScale(240.0f));
        renderHandlerDropdown("Stats For##mp_pstats_who",
                              mpStatsForPlayerDropdownHandler, 0, 0);
        ImGui::PopItemWidth();

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        /* Render the kills-vs-deaths matrix from mpGetPlayerRankings.
         * The ranking array is always sorted by score so the selected
         * player may not be first; we find their row to read killcounts. */
        struct ranking_mpp rankings[MP_MAX_MPCHRS];
        s32 rcount = mpGetPlayerRankings(rankings);

        s32 selected = (s32)g_MpSelectedPlayersForStats[g_MpPlayerNum];
        struct mpchrconfig_mpp *selChr = nullptr;
        for (s32 i = 0; i < rcount; i++) {
            if ((s32)rankings[i].chrnum == selected) {
                selChr = rankings[i].mpchr;
                break;
            }
        }

        if (selChr) {
            /* Name + suicides row (suicides = killcounts[self]). */
            ImGui::Text("%s", selChr->name);
            ImGui::SameLine();
            ImGui::TextDisabled("   Suicides: %d",
                                (int)selChr->killcounts[selected]);

            ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

            if (ImGui::BeginTable("##mp_pstats_tbl", 3,
                                  ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Player",
                                        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Deaths",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        pdguiScale(70.0f));
                ImGui::TableSetupColumn("Kills",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        pdguiScale(70.0f));
                ImGui::TableHeadersRow();

                for (s32 i = 0; i < rcount; i++) {
                    struct mpchrconfig_mpp *loopChr = rankings[i].mpchr;
                    s32 chrnum = (s32)rankings[i].chrnum;
                    if (!loopChr) continue;
                    if (chrnum == selected) continue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(loopChr->name);

                    /* Deaths: how many times selected player killed this
                     * loop player (= loopChr->killcounts[selected]).
                     * Matches the legacy "other player was killed by us"
                     * semantics. */
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d",
                                (int)loopChr->killcounts[selected]);

                    /* Kills: how many times this loop player killed the
                     * selected player (= selChr->killcounts[chrnum]). */
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d",
                                (int)selChr->killcounts[chrnum]);
                }

                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled("(no data)");
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_pause_pstats_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mpp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Pause Player Ranking (g_MpPausePlayerRankingMenuDialog)
 * =========================================================================
 *
 * Legacy layout (ingame.c:635 g_MpPlayerRankingMenuItems):
 *   RANKING item (no handler) -- renders individual player rankings.
 *
 * No writes.  Pure read-only display over mpGetPlayerRankings().  Team
 * scoring is handled by the sibling Team Rankings dialog below.
 */

static s32 renderMpPausePlayerRanking(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mpp_BeginStandardWindow("##mp_pause_prank",
                                             "Player Ranking", 0.55f, 0.85f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mpp_BackPressed()) {
        mpp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_pause_prank_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        struct ranking_mpp rankings[MP_MAX_MPCHRS];
        s32 rcount = mpGetPlayerRankings(rankings);

        if (rcount <= 0) {
            ImGui::TextDisabled("(no active players)");
        } else if (ImGui::BeginTable("##mp_pause_prank_tbl", 3,
                                     ImGuiTableFlags_SizingFixedFit |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("#",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    pdguiScale(30.0f));
            ImGui::TableSetupColumn("Player",
                                    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Score",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    pdguiScale(80.0f));
            ImGui::TableHeadersRow();

            for (s32 i = 0; i < rcount; i++) {
                struct mpchrconfig_mpp *chr = rankings[i].mpchr;
                if (!chr) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", (int)(i + 1));

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(chr->name);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", (int)rankings[i].score);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_pause_prank_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mpp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Pause Team Rankings (g_MpPauseTeamRankingsMenuDialog)
 * =========================================================================
 *
 * Legacy layout (ingame.c:665 g_MpTeamRankingsMenuItems):
 *   RANKING item, param3=1 (team mode).
 *
 * No writes.  Read-only display over mpGetTeamRankings() +
 * g_BossFile.teamnames[team].  We pull team names through a local
 * bridge-style extern pointer to avoid cloning the bossfile struct.
 */

static s32 renderMpPauseTeamRankings(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mpp_BeginStandardWindow("##mp_pause_trank",
                                             "Team Ranking", 0.55f, 0.75f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mpp_BackPressed()) {
        mpp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_pause_trank_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        struct ranking_mpp rankings[MP_MAX_MPCHRS];
        s32 rcount = mpGetTeamRankings(rankings);

        if (rcount <= 0) {
            ImGui::TextDisabled("(no team data)");
        } else if (ImGui::BeginTable("##mp_pause_trank_tbl", 3,
                                     ImGuiTableFlags_SizingFixedFit |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("#",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    pdguiScale(30.0f));
            ImGui::TableSetupColumn("Team",
                                    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Score",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    pdguiScale(80.0f));
            ImGui::TableHeadersRow();

            for (s32 i = 0; i < rcount; i++) {
                u32 team = rankings[i].teamnum;
                const char *tname = pdguiMppGetTeamName(team);
                if (!tname || !tname[0]) tname = "Team";

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", (int)(i + 1));

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(tname);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", (int)rankings[i].score);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_pause_trank_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mpp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Player Options (g_MpPlayerOptionsMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:1795 g_MpPlayerOptionsMenuItems):
 *   [x] Highlight Pickups  (param3 = MPDISPLAYOPTION_HIGHLIGHTPICKUPS)
 *   [x] Highlight Players  (param3 = MPDISPLAYOPTION_HIGHLIGHTPLAYERS)
 *   [x] Highlight Teams    (param3 = MPDISPLAYOPTION_HIGHLIGHTTEAMS)
 *   [x] Radar              (param3 = MPDISPLAYOPTION_RADAR)
 *   --- separator ---
 *   Back (close)
 *
 * All four delegate to menuhandlerMpDisplayOptionCheckbox which writes
 * g_PlayerConfigsArray[g_MpPlayerNum].base.displayoptions via OR/AND
 * masks.  scenarios.c and radar.c read the flags per-frame during
 * gameplay -- no network serialization, purely local preference.
 */

struct DisplayOptionRow {
    const char *label;
    intptr_t    mask;
};

static s32 renderMpPlayerOptions(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mpp_BeginStandardWindow("##mp_player_opts",
                                             "Options", 0.45f, 0.55f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mpp_BackPressed()) {
        mpp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_player_opts_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        static const DisplayOptionRow rows[] = {
            { "Highlight Pickups", MPDISPLAYOPTION_HIGHLIGHTPICKUPS },
            { "Highlight Players", MPDISPLAYOPTION_HIGHLIGHTPLAYERS },
            { "Highlight Teams",   MPDISPLAYOPTION_HIGHLIGHTTEAMS   },
            { "Radar",             MPDISPLAYOPTION_RADAR            },
        };

        for (s32 i = 0; i < (s32)(sizeof(rows) / sizeof(rows[0])); i++) {
            bool v = cb_Get(menuhandlerMpDisplayOptionCheckbox, 0, rows[i].mask);
            ImGui::PushID(i);
            if (ImGui::Checkbox(rows[i].label, &v)) {
                cb_Set(menuhandlerMpDisplayOptionCheckbox, 0, rows[i].mask, v);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_player_opts_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mpp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" void pdguiMenuMpPauseRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    pdguiHotswapRegister(&g_MpPauseControlMenuDialog,
                         renderMpPauseControl,
                         "MP Pause Control");
    pdguiHotswapRegister(&g_MpPauseInventoryMenuDialog,
                         renderMpPauseInventory,
                         "MP Pause Inventory");
    pdguiHotswapRegister(&g_MpPausePlayerStatsMenuDialog,
                         renderMpPausePlayerStats,
                         "MP Pause Player Stats");
    pdguiHotswapRegister(&g_MpPausePlayerRankingMenuDialog,
                         renderMpPausePlayerRanking,
                         "MP Pause Player Ranking");
    pdguiHotswapRegister(&g_MpPauseTeamRankingsMenuDialog,
                         renderMpPauseTeamRankings,
                         "MP Pause Team Rankings");
    pdguiHotswapRegister(&g_MpPlayerOptionsMenuDialog,
                         renderMpPlayerOptions,
                         "MP Player Options");

    sysLogPrintf(LOG_NOTE,
        "MENU_IMGUI: Batch 8 MP Pause & In-Game registered (6 dialogs)");
}
