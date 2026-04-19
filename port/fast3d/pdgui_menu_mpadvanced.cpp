/**
 * pdgui_menu_mpadvanced.cpp -- ImGui replacement for MP Advanced / Quick paths.
 *
 * D5 Phase 3 Batch 7.
 *
 * Covers 11 legacy menudialogdefs that act as navigation hubs in the
 * Combat Simulator setup path.  All of them delegate real state work
 * to handlers that Batches 5 and 6 already wire through ImGui, so the
 * renderers in this file are mostly "list of selectables" hubs:
 *
 *   g_MpAdvancedSetupMenuDialog                  -> renderMpAdvancedSetup (normal)
 *   g_MpAdvancedSetupViaAdvChallengeMenuDialog   -> renderMpAdvancedSetup (challenge)
 *   g_MpQuickGoMenuDialog                        -> renderMpQuickGo
 *   g_MpQuickTeamGameSetupMenuDialog             -> renderMpQuickTeamGameSetup
 *   g_MpQuickTeamMenuDialog                      -> renderMpQuickTeam
 *   g_MpStuffMenuDialog                          -> renderMpStuff (normal)
 *   g_MpStuffViaAdvChallengeMenuDialog           -> renderMpStuff (challenge)
 *   g_MpPlayerSetupViaAdvMenuDialog              -> renderMpPlayerSetupHub (normal)
 *   g_MpPlayerSetupViaAdvChallengeMenuDialog     -> renderMpPlayerSetupHub (challenge)
 *   g_MpPlayerSetupViaQuickGoMenuDialog          -> renderMpPlayerSetupHub (quickgo)
 *
 * Design:
 *   - Reuses primitives from pdgui_layout / pdgui_style (scrim, PD title
 *     frame, docked action bar).  No new helpers invented.
 *   - Delegates ALL state mutation to legacy C handlers (setup.c / mainmenu.c
 *     / scenarios.c) through the s205 shadow-struct call-through pattern
 *     (same ABI as the s203/s204 shadows in mpsetup.cpp / botsetup.cpp).
 *   - Selectables that open sub-dialogs call menuPushDialog(&target) directly
 *     -- that's what the legacy menu runtime does when an item has the
 *     MENUITEMFLAG_SELECTABLE_OPENSDIALOG flag.  Zero-function-loss because
 *     every target is either already ImGui-rendered (Batches 5-6) or still
 *     legacy-rendered.
 *   - The dialog-OPEN side effects of `menudialogMpGameSetup`
 *     (sets g_Vars.mpsetupmenu / g_Vars.usingadvsetup) and
 *     `menudialogMpQuickGo` (sets g_Vars.mpsetupmenu) still fire through the
 *     legacy menu runtime because the hot-swap system only hooks the RENDER
 *     phase.  Nothing additional needed here.
 *
 * NETWORK MATCH START/END AUDIT — see context/scratch/D5-P3-batch7-2026-04-11.md
 *   Every field written by a Batch 7 handler lands in the same backing global
 *   that the legacy `mpStartMatch` / `mpConfigureQuickTeamPlayers` /
 *   `mpConfigureQuickTeamSimulants` path already reads.  No shadow/cached copy
 *   is introduced by this file.  The modern room lobby (room.cpp) uses a
 *   separate g_MatchConfig write path that `matchStart()` copies into
 *   g_MpSetup before calling mpStartMatch -- the two paths never interleave
 *   because room.cpp does not push any of the Batch 7 dialogs.
 *
 * What is NOT in this file:
 *   - No edits to pdgui_menu_solomission.cpp (standing rule)
 *   - No edits to pdgui_menu_room.cpp (room lobby is a distinct modern path
 *     that already covers the same user goals; Batch 7 wrappers exist only
 *     so the legacy Combat Simulator push path -- still reachable from
 *     challenge-mode, fmb, menutick, and misc legacy entry points -- stays
 *     zero-function-loss)
 *   - No struct changes to g_MatchConfig: ALL quick-team / advanced-setup
 *     fields already exist on g_Vars / g_MpSetup / g_PlayerConfigsArray
 *     and matchStart() on the room path intentionally does NOT copy them
 *     because room.cpp uses its own slot table for teams.
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32 breaks
 * C++).  Auto-discovered by GLOB_RECURSE for port cpp files in CMakeLists.txt.
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
#include "pdgui_menus.h"  /* for pdguiMenuMpAdvancedRegister declaration */
}

/* =========================================================================
 * Forward declarations -- game symbols (extern "C", no types.h)
 * ========================================================================= */

extern "C" {

/* ---- Dialog definitions for the 11 Batch 7 dialogs ---- */
extern struct menudialogdef g_MpAdvancedSetupMenuDialog;
extern struct menudialogdef g_MpAdvancedSetupViaAdvChallengeMenuDialog;
extern struct menudialogdef g_MpQuickGoMenuDialog;
extern struct menudialogdef g_MpQuickTeamGameSetupMenuDialog;
extern struct menudialogdef g_MpQuickTeamMenuDialog;
extern struct menudialogdef g_MpStuffMenuDialog;
extern struct menudialogdef g_MpStuffViaAdvChallengeMenuDialog;
extern struct menudialogdef g_MpPlayerSetupViaAdvMenuDialog;
extern struct menudialogdef g_MpPlayerSetupViaAdvChallengeMenuDialog;
extern struct menudialogdef g_MpPlayerSetupViaQuickGoMenuDialog;

/* ---- Dialogs we push into from Batch 7 hubs (still linked, may be
 *      ImGui-rendered from Batches 5-6 or legacy) ---- */
extern struct menudialogdef g_MpScenarioMenuDialog;
extern struct menudialogdef g_MpQuickTeamScenarioMenuDialog;
extern struct menudialogdef g_MpArenaMenuDialog;
extern struct menudialogdef g_MpWeaponsMenuDialog;
extern struct menudialogdef g_MpQuickTeamWeaponsMenuDialog;
extern struct menudialogdef g_MpLimitsMenuDialog;
extern struct menudialogdef g_MpHandicapsMenuDialog;
extern struct menudialogdef g_MpSimulantsMenuDialog;
extern struct menudialogdef g_MpTeamsMenuDialog;
extern struct menudialogdef g_ManageSettingsDialog;
extern struct menudialogdef g_MpLoadSettingsMenuDialog;
extern struct menudialogdef g_MpReadyMenuDialog;
extern struct menudialogdef g_MpLoadPlayerMenuDialog;
extern struct menudialogdef g_MpDropOutMenuDialog;
extern struct menudialogdef g_MpPlayerNameMenuDialog;
extern struct menudialogdef g_MpCharacterMenuDialog;
extern struct menudialogdef g_MpControlMenuDialog;
extern struct menudialogdef g_MpPlayerOptionsMenuDialog;
extern struct menudialogdef g_MpPlayerStatsMenuDialog;
extern struct menudialogdef g_MpSoundtrackMenuDialog;
extern struct menudialogdef g_MpTeamNamesMenuDialog;
extern struct menudialogdef g_MpAbortMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* ---- Language ---- */
char *langGet(s32 textid);
/* langSafe comes from pdgui.h */

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

/* ---- s205 shadow menuitem / handlerdata
 * ABI-compatible with the real types in src/include/types.h:3342..3417.
 * Cloned from the s203/s204 pattern in mpsetup.cpp / botsetup.cpp so C
 * handlers can be invoked through function pointers from C++ without
 * including types.h (#define bool s32 breaks C++). */
struct s205_handlerdata_checkbox { u32 value; };
struct s205_handlerdata_dropdown { uintptr_t value; uintptr_t unk04; };
struct s205_handlerdata_list_t {
    uintptr_t value;
    s32       unk04;
};
struct s205_handlerdata_slider { u32 value; char *label; };

union s205_handlerdata {
    struct s205_handlerdata_checkbox checkbox;
    struct s205_handlerdata_dropdown dropdown;
    struct s205_handlerdata_list_t   list;
    struct s205_handlerdata_slider   slider;
    u8 _pad[256];
};

struct s205_menuitem {
    u8        type;
    u8        param;
    u32       flags;
    intptr_t  param2;
    intptr_t  param3;
    uintptr_t (*handler)(s32 op, struct s205_menuitem *, union s205_handlerdata *);
};

/* ---- Legacy handlers we delegate to ----
 *
 * Each delegate touches exactly one backing global so the NETWORK AUDIT
 * stays easy to reason about.  The bracketed comment next to each is
 * the single backing global the handler reads/writes -- grep for it to
 * confirm which match-start reader consumes it. */

/* setup.c: g_MpSetup.options | g_Vars.mpquickteam | g_PlayerConfigsArray[i].base.team */
uintptr_t menuhandlerMpQuickTeamOption(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: g_Vars.mpplayerteams[] */
uintptr_t menuhandlerPlayerTeam(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: g_Vars.mpquickteamnumsims */
uintptr_t menuhandlerMpNumberOfSimulants(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: g_Vars.unk0004a0 (simulants per team) */
uintptr_t menuhandlerMpSimulantsPerTeam(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: g_Vars.mpsimdifficulty */
uintptr_t mpQuickTeamSimulantDifficultyHandler(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: (dialog pushes — does not write match state) */
uintptr_t menuhandlerMpFinishedSetup(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: (CHECKHIDDEN only — visible when mpquickteam != PLAYERSONLY) */
uintptr_t menuhandlerQuickTeamSeparator(s32, struct s205_menuitem *, union s205_handlerdata *);

/* scenarios.c: pushes scenario-specific options dialog (reads g_MpSetup.scenario) */
uintptr_t menuhandlerMpOpenOptions(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: g_BossFile lock state via mpSetLock */
uintptr_t menuhandlerMpLock(s32, struct s205_menuitem *, union s205_handlerdata *);

/* mainmenu.c: optionsSetScreenSplit (PC dead — PLAYERCOUNT()==1) */
uintptr_t menuhandlerScreenSplit(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: pushes save dialog (reads fileguid) */
uintptr_t menuhandlerMpSavePlayer(s32, struct s205_menuitem *, union s205_handlerdata *);

/* setup.c: pushes save dialog (reads g_MpCurrentSetup) */
uintptr_t menuhandlerMpSaveSettings(s32, struct s205_menuitem *, union s205_handlerdata *);

/* ---- Dynamic text functions (used as menuitem param2 in legacy defs) ---- */
char *mpGetCurrentPlayerName(struct menuitem *item);
char *mpMenuTextSavePlayerOrCopy(struct menuitem *item);
char *mpMenuTextArenaName(struct menuitem *item);
char *mpMenuTextScenarioShortName(struct menuitem *item);
char *mpMenuTextWeaponSetName(struct menuitem *item);

} /* extern "C" */

/* =========================================================================
 * Module state
 * ========================================================================= */

static bool s_Registered = false;

/* =========================================================================
 * s205 call-through helpers — same shape as the s203/s204 helpers in
 * mpsetup.cpp / botsetup.cpp, kept local to avoid creating a shared
 * header that would pull the shadow types out of file scope.
 * ========================================================================= */

/* ---- Dropdown handlers ---- */
static s32 dd_GetOptionCount(uintptr_t (*fn)(s32, s205_menuitem *, s205_handlerdata *),
                             u8 param, intptr_t param3)
{
    s205_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s205_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.dropdown.value;
}

static const char *dd_GetOptionText(uintptr_t (*fn)(s32, s205_menuitem *, s205_handlerdata *),
                                    u8 param, intptr_t param3, s32 idx)
{
    s205_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s205_handlerdata h{};
    h.dropdown.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 dd_GetSelectedIndex(uintptr_t (*fn)(s32, s205_menuitem *, s205_handlerdata *),
                               u8 param, intptr_t param3)
{
    s205_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s205_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return (s32)h.dropdown.value;
}

static void dd_Set(uintptr_t (*fn)(s32, s205_menuitem *, s205_handlerdata *),
                   u8 param, intptr_t param3, s32 idx)
{
    s205_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s205_handlerdata h{};
    h.dropdown.value = (uintptr_t)idx;
    fn(MENUOP_SET, &it, &h);
}

static bool dd_IsHidden(uintptr_t (*fn)(s32, s205_menuitem *, s205_handlerdata *),
                        u8 param, intptr_t param3)
{
    s205_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s205_handlerdata h{};
    uintptr_t r = fn(MENUOP_CHECKHIDDEN, &it, &h);
    return r != 0;
}

/* ---- Plain SET (for "button" selectables that push a dialog
 *      via their own menuhandler*Set branch) ---- */
static void plain_Set(uintptr_t (*fn)(s32, s205_menuitem *, s205_handlerdata *),
                      u8 param)
{
    s205_menuitem it{};
    it.param = param;
    s205_handlerdata h{};
    fn(MENUOP_SET, &it, &h);
}

/* ---- Render a dropdown row backed by a legacy handler.
 *      Mirrors ImGui Combo; plays SND_SELECT on change. ---- */
static void renderHandlerDropdown(const char *label,
                                  uintptr_t (*fn)(s32, s205_menuitem *, s205_handlerdata *),
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
 * Window-frame helpers (clone of the s203 shape in mpsetup.cpp — kept
 * file-local for the same reason; shared helpers would need a new header).
 * ========================================================================= */

struct WindowFrame {
    float mw;
    float mh;
    ImVec2 pos;
};

/* S300: s_MpAdvancedPushedCtx removed — menu pool owns the ctx for
 * MENU_TYPE_MP_ADVANCED via menupoolAcquireDialog / menupoolReleaseDialog. */

static WindowFrame ma_BeginStandardWindow(const char *imguiId, const char *title,
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

static void ma_CloseCurrentDialog(void)
{
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
    /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
    menuPopDialog();
}

/* True if this frame saw Escape or gamepad-B (the universal back button). */
static bool ma_BackPressed(void)
{
    return !ImGui::IsWindowAppearing() &&
           ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

/* ---- Hub row: label + optional dynamic-text on the right, pushes a
 *      target dialog on click.  Plays SND_SELECT. ---- */
static bool hubPushRow(const char *label, struct menudialogdef *target,
                       const char *rightSideText = nullptr)
{
    ImGui::PushID(label);
    bool clicked = ImGui::Selectable("##hub_row", false, 0,
                                     ImVec2(0, pdguiScale(28.0f)));
    /* Draw the label overlaid on the selectable (the selectable has an
     * empty label so we control column layout manually). */
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

/* ---- Hub row backed by a legacy handler (MENUOP_SET) instead of a
 *      menuPushDialog target.  Some legacy items run a handler on click
 *      that opens a dialog or triggers a side effect (Save Settings,
 *      Save Player, Open Scenario Options, Advanced Setup). ---- */
static bool hubHandlerRow(const char *label,
                          uintptr_t (*fn)(s32, s205_menuitem *, s205_handlerdata *),
                          u8 param = 0,
                          const char *rightSideText = nullptr)
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
    if (rightSideText && rightSideText[0]) {
        float tw = ImGui::CalcTextSize(rightSideText).x;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowMax.x - tw - pdguiScale(8.0f), ty),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), rightSideText);
    }
    if (clicked) {
        plain_Set(fn, param);
        pdguiPlaySound(PDGUI_SND_SELECT);
    }
    ImGui::PopID();
    return clicked;
}

/* =========================================================================
 * Renderer: Advanced Setup hub (g_MpAdvancedSetupMenuDialog + challenge variant)
 * =========================================================================
 *
 * Legacy layout (setup.c:6043 g_MpAdvancedSetupMenuItems):
 *   Scenario            -> push g_MpScenarioMenuDialog (Batch 5)
 *   Options             -> menuhandlerMpOpenOptions (pushes scenario-options dialog)
 *   Arena               -> push g_MpArenaMenuDialog (Batch 5)
 *   Weapons             -> push g_MpWeaponsMenuDialog (Batch 5)
 *   Limits              -> push g_MpLimitsMenuDialog (Batch 5)
 *   Player Handicaps    -> push g_MpHandicapsMenuDialog (Group 4)
 *   Simulants           -> push g_MpSimulantsMenuDialog (Batch 6)
 *   Teams               -> push g_MpTeamsMenuDialog (pre-D5)
 *   --- separator ---
 *   Manage Settings     -> push g_ManageSettingsDialog
 *   Load Settings       -> push g_MpLoadSettingsMenuDialog (Batch 11)
 *   Save Settings       -> menuhandlerMpSaveSettings
 *
 * No direct match-state writes here — every row either pushes a
 * sub-dialog (handled elsewhere) or runs a legacy handler that itself
 * pushes a dialog.  The two variants are identical item-wise and only
 * differ in the legacy def's nextsibling tab target, which is irrelevant
 * in ImGui.
 */

static s32 renderMpAdvancedSetupImpl(u8 variant, const struct menudialogdef *def)
{
    const char *imguiId = (variant == 0) ? "##mp_adv" : "##mp_adv_c";
    WindowFrame wf = ma_BeginStandardWindow(imguiId, "Game Setup", 0.55f, 0.85f, def);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ma_BackPressed()) {
        ma_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_adv_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* Dynamic right-side text for Scenario / Arena rows: the legacy
         * defs hand mpMenuTextScenarioShortName / mpMenuTextArenaName as
         * the item param2.  We invoke the functions directly (char*(*)(
         * struct menuitem*)) -- passing NULL is safe because the bodies
         * ignore `item` and read g_MpSetup globals directly. */
        const char *scenText = mpMenuTextScenarioShortName(nullptr);
        const char *arenaText = mpMenuTextArenaName(nullptr);

        hubPushRow("Scenario",           &g_MpScenarioMenuDialog, scenText);
        hubHandlerRow("Options",         menuhandlerMpOpenOptions);
        hubPushRow("Arena",              &g_MpArenaMenuDialog, arenaText);
        hubPushRow("Weapons",            &g_MpWeaponsMenuDialog);
        hubPushRow("Limits",             &g_MpLimitsMenuDialog);
        hubPushRow("Player Handicaps",   &g_MpHandicapsMenuDialog);
        hubPushRow("Simulants",          &g_MpSimulantsMenuDialog);
        hubPushRow("Teams",              &g_MpTeamsMenuDialog);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        hubPushRow("Manage Settings",    &g_ManageSettingsDialog);
        hubPushRow("Load Settings",      &g_MpLoadSettingsMenuDialog);
        hubHandlerRow("Save Settings",   menuhandlerMpSaveSettings);
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_adv_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            ma_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

static s32 renderMpAdvancedSetup(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpAdvancedSetupImpl(0, menupoolDialogDef(dialog));
}

static s32 renderMpAdvancedSetupViaChallenge(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpAdvancedSetupImpl(1, menupoolDialogDef(dialog));
}

/* =========================================================================
 * Renderer: Quick Go (g_MpQuickGoMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:6161 g_MpQuickGoMenuItems):
 *   Start Game     -> push g_MpReadyMenuDialog
 *   Load Player    -> push g_MpLoadPlayerMenuDialog
 *   Player Settings-> push g_MpPlayerSetupViaQuickGoMenuDialog
 *   Drop Out       -> push g_MpDropOutMenuDialog
 *
 * No state writes.  The menudialogMpQuickGo OPEN callback (sets
 * g_Vars.mpsetupmenu = MPSETUPMENU_QUICKGO) still fires via the legacy
 * runtime because hot-swap only hooks RENDER.
 */

static s32 renderMpQuickGo(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = ma_BeginStandardWindow("##mp_qg", "Quick Go", 0.45f, 0.55f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ma_BackPressed()) {
        ma_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_qg_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {
        hubPushRow("Start Game",       &g_MpReadyMenuDialog);
        hubPushRow("Load Player",      &g_MpLoadPlayerMenuDialog);
        hubPushRow("Player Settings",  &g_MpPlayerSetupViaQuickGoMenuDialog);
        hubPushRow("Drop Out",         &g_MpDropOutMenuDialog);
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_qg_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            ma_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: Quick Team root (g_MpQuickTeamMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:6357 g_MpQuickTeamMenuItems):
 *   Players Only          (param=0)
 *   Players and Simulants (param=1)
 *   --- separator ---
 *   Player Teams          (param=2)
 *   Players vs. Simulants (param=3)
 *   Player-Simulant Teams (param=4)
 *
 * All five delegate to menuhandlerMpQuickTeamOption, which writes
 * g_Vars.mpquickteam = param, rewrites g_MpSetup.scenario if the chosen
 * mode is incompatible with the current scenario, and then pushes
 * g_MpQuickTeamGameSetupMenuDialog.
 *
 * g_Vars.mpquickteam is read by mpConfigureQuickTeamSimulants inside
 * mpStartMatch (mplayer.c:258) so the match start path sees the choice.
 * g_Vars.mpplayerteams[] (written later in QuickTeamGameSetup) is read
 * by mpConfigureQuickTeamPlayers.
 */

static void qtRootBigButton(const char *label, u8 param)
{
    s205_menuitem it{};
    it.param = param;

    ImGui::PushID((int)param);
    ImVec2 sz(pdguiScale(340.0f), pdguiScale(44.0f));
    if (ImGui::Button(label, sz)) {
        /* menuhandlerMpQuickTeamOption uses item->param for the choice,
         * so we set it on the shadow and invoke MENUOP_SET. */
        s205_handlerdata h{};
        menuhandlerMpQuickTeamOption(MENUOP_SET, &it, &h);
        pdguiPlaySound(PDGUI_SND_SELECT);
        /* The handler itself calls menuPushDialog(&g_MpQuickTeamGameSetupMenuDialog)
         * so we do not need to pop/push ourselves. */
    }
    ImGui::PopID();
}

static s32 renderMpQuickTeam(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = ma_BeginStandardWindow("##mp_qt_root", "Quick Team", 0.50f, 0.65f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ma_BackPressed()) {
        ma_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_qt_root_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* MPQUICKTEAM_PLAYERSONLY == 0, PLAYERSANDSIMS == 1,
         * PLAYERSTEAMS == 2, PLAYERSVSSIMS == 3, PLAYERSIMTEAMS == 4 */
        qtRootBigButton("Players Only",           0);
        qtRootBigButton("Players and Simulants",  1);

        ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));

        qtRootBigButton("Player Teams",           2);
        qtRootBigButton("Players vs. Simulants",  3);
        qtRootBigButton("Player-Simulant Teams",  4);
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_qt_root_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            ma_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: Quick Team Game Setup (g_MpQuickTeamGameSetupMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:6208 g_MpQuickTeamGameSetupMenuItems):
 *   Scenario      -> push g_MpQuickTeamScenarioMenuDialog  (Batch 5)
 *   Options       -> menuhandlerMpOpenOptions
 *   Arena         -> push g_MpArenaMenuDialog              (Batch 5)
 *   Weapons       -> push g_MpQuickTeamWeaponsMenuDialog   (Batch 5)
 *   Limits        -> push g_MpLimitsMenuDialog             (Batch 5)
 *   --- separator (hidden if PLAYERSONLY) ---
 *   Player 1 Team (dropdown, hidden unless PLAYERSTEAMS)
 *   Player 2 Team ...
 *   Player 3 Team ...
 *   Player 4 Team ...
 *   Number Of Simulants (dropdown, hidden unless PLAYERSANDSIMS or PLAYERSVSSIMS)
 *   Simulants Per Team  (dropdown, hidden unless PLAYERSIMTEAMS)
 *   Simulant Difficulty (dropdown, hidden if PLAYERSONLY or PLAYERSTEAMS)
 *   --- separator ---
 *   Finished Setup
 *   --- separator ---
 *   Save Settings
 *
 * All dropdowns delegate to legacy handlers that write g_Vars.mpplayerteams[i]
 * / g_Vars.mpquickteamnumsims / g_Vars.unk0004a0 / g_Vars.mpsimdifficulty
 * respectively -- the same globals mpConfigureQuickTeamPlayers /
 * mpConfigureQuickTeamSimulants read during mpStartMatch.
 */

static s32 renderMpQuickTeamGameSetup(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = ma_BeginStandardWindow("##mp_qtgs", "Game Setup", 0.60f, 0.90f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ma_BackPressed()) {
        ma_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_qtgs_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        const char *scenText  = mpMenuTextScenarioShortName(nullptr);
        const char *arenaText = mpMenuTextArenaName(nullptr);
        const char *wsetText  = mpMenuTextWeaponSetName(nullptr);

        hubPushRow("Scenario",      &g_MpQuickTeamScenarioMenuDialog, scenText);
        hubHandlerRow("Options",    menuhandlerMpOpenOptions);
        hubPushRow("Arena",         &g_MpArenaMenuDialog, arenaText);
        hubPushRow("Weapons",       &g_MpQuickTeamWeaponsMenuDialog, wsetText);
        hubPushRow("Limits",        &g_MpLimitsMenuDialog);

        /* The separator visibility for the quickteam items: the legacy
         * separator handler hides itself when mpquickteam == PLAYERSONLY,
         * so we skip the separator in that mode too. */
        bool sepHidden = dd_IsHidden(menuhandlerQuickTeamSeparator, 0, 0);
        if (!sepHidden) {
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        }

        ImGui::PushItemWidth(pdguiScale(220.0f));

        /* Player Team dropdowns — hidden unless PLAYERSTEAMS.  item->param
         * carries the player index so we cannot reuse dd_IsHidden across
         * all four; call per-param. */
        for (s32 p = 0; p < 4; p++) {
            if (!dd_IsHidden(menuhandlerPlayerTeam, (u8)p, 0)) {
                char label[32];
                snprintf(label, sizeof(label), "Player %d Team##mp_qt_pt%d", p + 1, p);
                renderHandlerDropdown(label, menuhandlerPlayerTeam, (u8)p, 0);
            }
        }

        if (!dd_IsHidden(menuhandlerMpNumberOfSimulants, 0, 0)) {
            renderHandlerDropdown("Number Of Simulants##mp_qt_nsim",
                                  menuhandlerMpNumberOfSimulants, 0, 0);
        }
        if (!dd_IsHidden(menuhandlerMpSimulantsPerTeam, 0, 0)) {
            renderHandlerDropdown("Simulants Per Team##mp_qt_spt",
                                  menuhandlerMpSimulantsPerTeam, 0, 0);
        }
        if (!dd_IsHidden(mpQuickTeamSimulantDifficultyHandler, 0, 0)) {
            renderHandlerDropdown("Simulant Difficulty##mp_qt_sdiff",
                                  mpQuickTeamSimulantDifficultyHandler, 0, 0);
        }

        ImGui::PopItemWidth();

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));

        /* Finished Setup: calls func0f17f428 → mpConfigureQuickTeamPlayers
         * (commits g_Vars.mpplayerteams[] → g_PlayerConfigsArray[i].base.team)
         * then pushes g_MpQuickGoMenuDialog.  The legacy handler also sets
         * g_MpSetup.options TEAMSENABLED bit based on the chosen mode. */
        if (ImGui::Button("Finished Setup##mp_qt_fin",
                          ImVec2(pdguiScale(220.0f), pdguiScale(36.0f)))) {
            plain_Set(menuhandlerMpFinishedSetup, 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }

        ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        if (ImGui::Button("Save Settings##mp_qt_save",
                          ImVec2(pdguiScale(220.0f), pdguiScale(32.0f)))) {
            plain_Set(menuhandlerMpSaveSettings, 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_qtgs_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            ma_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: Stuff hub (g_MpStuffMenuDialog + challenge variant)
 * =========================================================================
 *
 * Legacy layout (setup.c:5817 g_MpStuffMenuItems):
 *   Soundtrack  -> push g_MpSoundtrackMenuDialog      (Batch 12)
 *   Team Names  -> push g_MpTeamNamesMenuDialog       (Batch 12)
 *   Lock        -> dropdown (menuhandlerMpLock)       (g_BossFile.lock*)
 *   --- separator ---
 *   Split       -> dropdown (menuhandlerScreenSplit)  (PC dead — still shown)
 *   --- separator ---
 *   Start Game  -> push g_MpReadyMenuDialog
 *   Drop Out    -> push g_MpDropOutMenuDialog
 *   Abort Game  -> push g_MpAbortMenuDialog
 *
 * The Lock dropdown writes to legacy lock state via mpSetLock.  No match
 * config writes.  The challenge variant is structurally identical; it
 * only differs in the legacy def's nextsibling target.
 */

static s32 renderMpStuffImpl(u8 variant, const struct menudialogdef *def)
{
    const char *imguiId = (variant == 0) ? "##mp_stuff" : "##mp_stuff_c";
    WindowFrame wf = ma_BeginStandardWindow(imguiId, "Stuff", 0.50f, 0.80f, def);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ma_BackPressed()) {
        ma_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_stuff_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        hubPushRow("Soundtrack",  &g_MpSoundtrackMenuDialog);
        hubPushRow("Team Names",  &g_MpTeamNamesMenuDialog);

        ImGui::PushItemWidth(pdguiScale(220.0f));
        renderHandlerDropdown("Lock##mp_stuff_lock", menuhandlerMpLock, 0, 0);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        /* Screen Split dropdown is PC-dead (PLAYERCOUNT() == 1 always) but
         * harmless to display — the legacy handler just writes to
         * optionsSetScreenSplit and the effect is only visible when
         * PLAYERCOUNT() > 1, which cannot happen in the PC port.  We
         * keep it for zero-function-loss parity with the legacy dialog. */
        renderHandlerDropdown("Split##mp_stuff_split", menuhandlerScreenSplit, 0, 0);
        ImGui::PopItemWidth();

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        hubPushRow("Start Game",  &g_MpReadyMenuDialog);
        hubPushRow("Drop Out",    &g_MpDropOutMenuDialog);
        hubPushRow("Abort Game",  &g_MpAbortMenuDialog);
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_stuff_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            ma_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

static s32 renderMpStuff(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpStuffImpl(0, menupoolDialogDef(dialog));
}

static s32 renderMpStuffViaChallenge(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpStuffImpl(1, menupoolDialogDef(dialog));
}

/* =========================================================================
 * Renderer: Player Setup hub (shared across 3 Via* variants)
 * =========================================================================
 *
 * Legacy layout (setup.c:5911 g_MpPlayerSetup234MenuItems):
 *   Name            -> push g_MpPlayerNameMenuDialog    (dynamic text: mpGetCurrentPlayerName)
 *   Character       -> push g_MpCharacterMenuDialog
 *   Control         -> push g_MpControlMenuDialog
 *   Player Options  -> push g_MpPlayerOptionsMenuDialog
 *   Statistics      -> push g_MpPlayerStatsMenuDialog
 *   --- separator ---
 *   Load Player     -> push g_MpLoadPlayerMenuDialog
 *   Save Player     -> menuhandlerMpSavePlayer (dynamic text: mpMenuTextSavePlayerOrCopy)
 *
 * All three Via* variants share g_MpPlayerSetup234MenuItems — they only
 * differ in the legacy def's nextsibling tab target (Stuff normal / Stuff
 * challenge / NULL).  In ImGui we render them all with the same impl and
 * a variant label tag so the window id stays unique per dialog.
 */

enum PlayerSetupHubVariant {
    PSH_VIA_ADV = 0,
    PSH_VIA_ADV_CHAL = 1,
    PSH_VIA_QUICKGO = 2,
};

static s32 renderMpPlayerSetupHubImpl(u8 variant, const struct menudialogdef *def)
{
    const char *imguiId =
        (variant == PSH_VIA_ADV)      ? "##mp_psh_a"  :
        (variant == PSH_VIA_ADV_CHAL) ? "##mp_psh_ac" :
                                         "##mp_psh_qg";
    WindowFrame wf = ma_BeginStandardWindow(imguiId, "Player Setup", 0.50f, 0.75f, def);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ma_BackPressed()) {
        ma_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_psh_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* Dynamic text: the legacy defs pass these function pointers as
         * item->param2; calling with nullptr is safe because the bodies
         * ignore `item` and read g_PlayerConfigsArray[g_MpPlayerNum].base.name
         * / fileguid.fileid globals directly. */
        const char *nameText = mpGetCurrentPlayerName(nullptr);
        const char *saveText = mpMenuTextSavePlayerOrCopy(nullptr);

        hubPushRow("Name",           &g_MpPlayerNameMenuDialog, nameText);
        hubPushRow("Character",      &g_MpCharacterMenuDialog);
        hubPushRow("Control",        &g_MpControlMenuDialog);
        hubPushRow("Player Options", &g_MpPlayerOptionsMenuDialog);
        hubPushRow("Statistics",     &g_MpPlayerStatsMenuDialog);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        hubPushRow("Load Player",    &g_MpLoadPlayerMenuDialog);

        /* Save Player: the legacy handler inspects fileguid.fileid and
         * either pushes the save dialog or the select-location filemgr.
         * The dynamic text above already reflects "Save Player" vs
         * "Save Copy of Player".  Run via MENUOP_SET delegation. */
        if (ImGui::Selectable(saveText ? saveText : "Save Player",
                              false, 0, ImVec2(0, pdguiScale(28.0f)))) {
            plain_Set(menuhandlerMpSavePlayer, 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_psh_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            ma_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

static s32 renderMpPlayerSetupViaAdv(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpPlayerSetupHubImpl(PSH_VIA_ADV,
                                       menupoolDialogDef(dialog));
}

static s32 renderMpPlayerSetupViaAdvChallenge(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpPlayerSetupHubImpl(PSH_VIA_ADV_CHAL,
                                       menupoolDialogDef(dialog));
}

static s32 renderMpPlayerSetupViaQuickGo(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpPlayerSetupHubImpl(PSH_VIA_QUICKGO,
                                       menupoolDialogDef(dialog));
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" void pdguiMenuMpAdvancedRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    pdguiHotswapRegister(&g_MpAdvancedSetupMenuDialog,
                         renderMpAdvancedSetup,
                         "MP Advanced Setup");
    pdguiHotswapRegister(&g_MpAdvancedSetupViaAdvChallengeMenuDialog,
                         renderMpAdvancedSetupViaChallenge,
                         "MP Advanced Setup (Challenge)");

    pdguiHotswapRegister(&g_MpQuickGoMenuDialog,
                         renderMpQuickGo,
                         "MP Quick Go");

    pdguiHotswapRegister(&g_MpQuickTeamMenuDialog,
                         renderMpQuickTeam,
                         "MP Quick Team");
    pdguiHotswapRegister(&g_MpQuickTeamGameSetupMenuDialog,
                         renderMpQuickTeamGameSetup,
                         "MP Quick Team Game Setup");

    pdguiHotswapRegister(&g_MpStuffMenuDialog,
                         renderMpStuff,
                         "MP Stuff");
    pdguiHotswapRegister(&g_MpStuffViaAdvChallengeMenuDialog,
                         renderMpStuffViaChallenge,
                         "MP Stuff (Challenge)");

    pdguiHotswapRegister(&g_MpPlayerSetupViaAdvMenuDialog,
                         renderMpPlayerSetupViaAdv,
                         "MP Player Setup (Adv)");
    pdguiHotswapRegister(&g_MpPlayerSetupViaAdvChallengeMenuDialog,
                         renderMpPlayerSetupViaAdvChallenge,
                         "MP Player Setup (Adv/Challenge)");
    pdguiHotswapRegister(&g_MpPlayerSetupViaQuickGoMenuDialog,
                         renderMpPlayerSetupViaQuickGo,
                         "MP Player Setup (Quick Go)");

    sysLogPrintf(LOG_NOTE, "MENU_IMGUI: Batch 7 MP Advanced/Quick paths registered (11 dialogs)");
}
