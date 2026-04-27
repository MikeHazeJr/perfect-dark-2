/**
 * pdgui_menu_mpsetup.cpp -- ImGui replacement for MP Setup Core dialogs.
 *
 * D5 Phase 3 Batch 5.
 *
 * Covers 14 legacy menudialogdefs in the Combat Simulator setup path:
 *
 *   g_MpArenaMenuDialog               -> renderMpArena
 *   g_MpScenarioMenuDialog            -> renderMpScenario         (param 0)
 *   g_MpQuickTeamScenarioMenuDialog   -> renderMpScenario         (param 1)
 *   g_MpWeaponsMenuDialog             -> renderMpWeapons          (full)
 *   g_MpSelectRandomWeaponsMenuDialog -> renderMpSelectRandomWeapons
 *   g_MpQuickTeamWeaponsMenuDialog    -> renderMpQuickTeamWeapons
 *   g_MpLimitsMenuDialog              -> renderMpLimits
 *   g_MpCombatOptionsMenuDialog       -> renderMpScenarioOptions  (COMBAT)
 *   g_CtcOptionsMenuDialog            -> renderMpScenarioOptions  (CTC)
 *   g_HtmOptionsMenuDialog            -> renderMpScenarioOptions  (HTM)
 *   g_HtbOptionsMenuDialog            -> renderMpScenarioOptions  (HTB)
 *   g_KohOptionsMenuDialog            -> renderMpScenarioOptions  (KOH)
 *   g_PacOptionsMenuDialog            -> renderMpScenarioOptions  (PAC)
 *   g_ExtGameOptionsMenuDialog        -> renderMpExtGameOptions
 *
 * Design:
 *   - Reuses primitives from pdgui_layout / pdgui_style (scrim, PD title
 *     frame, docked action bar).  No new helpers invented.
 *   - Delegates ALL state mutation to legacy C handlers (setup.c / scenarios.c
 *     / scenarios/*.inc) through the s203 shadow-struct call-through pattern
 *     (cloned from s194 in solomission.cpp:345 and s202 in cheats.cpp).
 *   - The legacy handlers own `g_MpSetup.*`, `g_MpWeaponSetRandomFilters[]`,
 *     `g_Vars.mphilltime`, weapon slot maps, slider ranges, feature-gating,
 *     mutual-exclusion, and `scenarioInit()` side-effects.  We never touch
 *     those globals directly.
 *   - Dialog registration via pdguiHotswapRegister, called from
 *     pdguiMenusRegisterAll by way of pdguiMenuMpSetupRegister()
 *     (declared in pdgui_menus.h).
 *
 * What is NOT in this file (per Mike's standing order):
 *   - No edits to pdgui_menu_solomission.cpp (critical collision rule)
 *   - No edits to pdgui_menu_room.cpp (modern room flow is unrelated; the
 *     menu-replacement plan left absorption as optional, contingent on
 *     retiring g_CombatSimulatorMenuDialog which is out of scope)
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32 breaks
 * C++).  Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui_widgets.h"      /* Priority L: shared label-left widget helpers */
#include "pdgui.h"        /* langSafe */
#include "system.h"
#include "inputctx.h"
#include "menupool.h"
#include "assetcatalog.h" /* maps/arenas catalog migration: ASSET_ARENA iteration */

extern "C" {
#include "pdgui_menus.h"  /* for pdguiMenuMpSetupRegister declaration */
}

/* =========================================================================
 * Forward declarations -- game symbols (extern "C", no types.h)
 * ========================================================================= */

extern "C" {

/* ---- Dialog definitions ---- */
extern struct menudialogdef g_MpArenaMenuDialog;
extern struct menudialogdef g_MpScenarioMenuDialog;
extern struct menudialogdef g_MpQuickTeamScenarioMenuDialog;
extern struct menudialogdef g_MpWeaponsMenuDialog;
extern struct menudialogdef g_MpSelectRandomWeaponsMenuDialog;
extern struct menudialogdef g_MpQuickTeamWeaponsMenuDialog;
extern struct menudialogdef g_MpLimitsMenuDialog;
extern struct menudialogdef g_MpCombatOptionsMenuDialog;
extern struct menudialogdef g_CtcOptionsMenuDialog;
extern struct menudialogdef g_HtmOptionsMenuDialog;
extern struct menudialogdef g_HtbOptionsMenuDialog;
extern struct menudialogdef g_KohOptionsMenuDialog;
extern struct menudialogdef g_PacOptionsMenuDialog;
extern struct menudialogdef g_ExtGameOptionsMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* ---- Language ---- */
char *langGet(s32 textid);
/* langSafe comes from pdgui.h */

/* ---- MP setup bridge (defined in pdgui_bridge.c) ---- */
void        pdguiMpSetupSetArena(u8 stagenum, const char *stage_id);
const char *pdguiMpSetupGetStageId(void);
u8          pdguiPauseGetStagenum(void);

/* ---- Unlock filter ---- */
s32 challengeIsFeatureUnlocked(u32 feature);

/* ---- MENUOP_* opcodes (Batch 4 gotcha: declared locally in each cpp)
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

/* ---- s203 shadow menuitem / handlerdata
 * ABI-compatible with the real types in src/include/types.h:3342..3417.
 * Cloned from the s194/s202 pattern in solomission.cpp / cheats.cpp so
 * C handlers can be invoked through function pointers from C++ without
 * including types.h (#define bool s32 breaks C++). */
struct s203_handlerdata_checkbox { u32 value; };
struct s203_handlerdata_dropdown { uintptr_t value; uintptr_t unk04; };
struct s203_handlerdata_list_t {
    uintptr_t value;   /* union { uintptr_t value; intptr_t values32; } */
    s32       unk04;   /* union { s32 unk04; u32 unk04u32; } */
};
struct s203_handlerdata_slider { u32 value; char *label; };

union s203_handlerdata {
    struct s203_handlerdata_checkbox checkbox;
    struct s203_handlerdata_dropdown dropdown;
    struct s203_handlerdata_list_t   list;
    struct s203_handlerdata_slider   slider;
    u8 _pad[256];
};

struct s203_menuitem {
    u8        type;
    u8        param;
    u32       flags;
    intptr_t  param2;
    intptr_t  param3;
    uintptr_t (*handler)(s32 op, struct s203_menuitem *, union s203_handlerdata *);
};

/* ---- Legacy handlers we delegate to ---- */

/* Arena (setup.c:412) */
uintptr_t mpArenaMenuHandler(s32, struct s203_menuitem *, union s203_handlerdata *);

/* Scenario picker (scenarios.c:309) */
uintptr_t scenarioScenarioMenuHandler(s32, struct s203_menuitem *, union s203_handlerdata *);

/* Weapons (setup.c) */
uintptr_t menuhandlerMpWeaponSetDropdown (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpWeaponSlot        (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpSelectRandomWeapons(s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpAutoRandomWeapon  (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t mpSelectRandomWeaponListHandler(s32, struct s203_menuitem *, union s203_handlerdata *);

/* Limits (setup.c:2681..) */
uintptr_t menuhandlerMpTimeLimitSlider      (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpScoreLimitSlider     (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpTeamScoreLimitSlider (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpRestoreScoreDefaults (s32, struct s203_menuitem *, union s203_handlerdata *);

/* Shared scenario-option handlers (scenarios.c + setup.c) */
uintptr_t menuhandlerMpCheckboxOption (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpDisplayTeam    (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpOneHitKills    (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpSlowMotion     (s32, struct s203_menuitem *, union s203_handlerdata *);
uintptr_t menuhandlerMpHillTime       (s32, struct s203_menuitem *, union s203_handlerdata *);

} /* extern "C" */

/* ---- Arena name override (defined in pdgui_menu_room.cpp; C++ linkage). */
const char *arenaGetName(u16 textId);

/* =========================================================================
 * Module state
 * ========================================================================= */

static bool s_Registered = false;

/* =========================================================================
 * s203 call-through helpers
 *
 * Each helper builds a local shadow menuitem, invokes the legacy handler with
 * the requested MENUOP_*, and returns the result (or writes back through the
 * handlerdata union).  No game globals touched directly.
 * ========================================================================= */

/* ---- List handlers (arena / scenario / select-random-weapons) ---- */

static s32 list_GetOptionCount(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *), u8 param)
{
    s203_menuitem it{};
    it.param = param;
    s203_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.list.value;
}

static const char *list_GetOptionText(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                                      u8 param, s32 idx)
{
    s203_menuitem it{};
    it.param = param;
    s203_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 list_GetSelectedIndex(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *), u8 param)
{
    s203_menuitem it{};
    it.param = param;
    s203_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return (s32)h.list.value;
}

static void list_Set(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                     u8 param, s32 idx)
{
    s203_menuitem it{};
    it.param = param;
    s203_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    fn(MENUOP_SET, &it, &h);
}

/* ---- Dropdown handlers (weapon set / weapon slot / auto-random) ---- */

static s32 dd_GetOptionCount(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                              u8 param, intptr_t param3)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.dropdown.value;
}

static const char *dd_GetOptionText(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                                    u8 param, intptr_t param3, s32 idx)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    h.dropdown.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 dd_GetSelectedIndex(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                                u8 param, intptr_t param3)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return (s32)h.dropdown.value;
}

static void dd_Set(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                   u8 param, intptr_t param3, s32 idx)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    h.dropdown.value = (uintptr_t)idx;
    fn(MENUOP_SET, &it, &h);
}

static bool dd_IsHidden(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                        u8 param, intptr_t param3)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    uintptr_t r = fn(MENUOP_CHECKHIDDEN, &it, &h);
    return r != 0;
}

/* ---- Checkbox handlers (generic option toggle) ---- */

static bool cb_Get(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                   u8 param, intptr_t param3)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    uintptr_t r = fn(MENUOP_GET, &it, &h);
    return r != 0;
}

static void cb_Set(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                   u8 param, intptr_t param3, bool value)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    h.checkbox.value = value ? 1 : 0;
    fn(MENUOP_SET, &it, &h);
}

static bool cb_IsDisabled(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                          u8 param, intptr_t param3)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    uintptr_t r = fn(MENUOP_CHECKDISABLED, &it, &h);
    return r != 0;
}

static bool cb_IsHidden(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                        u8 param, intptr_t param3)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    uintptr_t r = fn(MENUOP_CHECKHIDDEN, &it, &h);
    return r != 0;
}

/* ---- Slider handlers (time/score/team-score/hill-time/handicap) ---- */

static u32 sl_Get(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                  u8 param, intptr_t param3)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    fn(MENUOP_GETSLIDER, &it, &h);
    return h.slider.value;
}

static void sl_Set(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                   u8 param, intptr_t param3, u32 value)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    h.slider.value = value;
    fn(MENUOP_SET, &it, &h);
}

static void sl_GetLabel(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                        u8 param, intptr_t param3, u32 value,
                        char *buf, size_t bufn)
{
    s203_menuitem it{};
    it.param  = param;
    it.param3 = param3;
    s203_handlerdata h{};
    h.slider.value = value;
    h.slider.label = buf;
    if (bufn > 0) buf[0] = '\0';
    fn(MENUOP_GETSLIDERLABEL, &it, &h);
    if (bufn > 0) buf[bufn - 1] = '\0';
}

/* ---- Generic SET for buttons (Restore Defaults, Select All/None, etc.) ---- */

static void plain_Set(uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *), u8 param)
{
    s203_menuitem it{};
    it.param = param;
    s203_handlerdata h{};
    fn(MENUOP_SET, &it, &h);
}

/* =========================================================================
 * Window-frame helpers (match the shape used by cheats.cpp / solomission.cpp)
 * ========================================================================= */

struct WindowFrame {
    float mw;
    float mh;
    ImVec2 pos;
};

/* S300: s_MpSetupPushedCtx removed — menu pool owns the ctx for
 * MENU_TYPE_MP_SETUP via menupoolAcquireDialog / menupoolReleaseDialog.
 * Each renderer passes its live dialogdef so the pool lookup finds the
 * right slot. */

static WindowFrame mp_BeginStandardWindow(const char *imguiId, const char *title,
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
        /* Defer End() to caller via a return-value convention:
         * wf.mw==0 signals "Begin returned false". */
        wf.mw = 0.0f;
        /* S300: pool owns the ctx; release pops it. */
        menupoolReleaseDialog(def);
        return wf;
    }

    menupoolAcquireDialog(def, &g_CtxImGuiMenu);

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(wf.pos.x, wf.pos.y, wf.mw, wf.mh, title, 1);
    pdguiSetCursorBelowTitle(titleH);
    return wf;
}

static void mp_CloseCurrentDialog(void)
{
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
    /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
    menuPopDialog();
}

/* M-19 (menu-stack §6 progressive focus helper):
 *
 * Each picker dialog in this file (Arena, Scenario, Weapons, Limits,
 * Options, …) is a single-list selector pushed from the Match Setup hub
 * with MENUDIALOGFLAG_CLOSEONSELECT — so picking a row auto-pops back to
 * the hub, and the match-setup "A narrows / B backs up" progressive flow
 * is realised by the push/pop of these pickers rather than by tier
 * changes within one menu (like pdgui_menu_solomission.cpp).
 *
 * For controller UX parity with the solomission progressive-focus flow,
 * each picker should:
 *   1. Auto-focus the currently-selected row on open (so D-pad works
 *      immediately without a preparatory press).
 *   2. On a row selection, fire CLOSEONSELECT so the hub regains input
 *      authority. Focus returns to the invoker row on the hub naturally
 *      via ImGui's popup focus stack.
 *
 * Step (2) is already handled by the legacy dialog-def flag. Step (1) is
 * what this helper provides: a per-picker "focus pending" flag driven by
 * IsWindowAppearing and consumed on the row whose index matches the
 * current selection. Place the `mp_ConsumePendingFocus` call immediately
 * before the Selectable for that row. */
static void mp_ArmFocusOnOpen(bool *pendingFlag)
{
    if (ImGui::IsWindowAppearing()) *pendingFlag = true;
}

static void mp_ConsumePendingFocus(bool *pendingFlag, bool isTargetRow)
{
    if (*pendingFlag && isTargetRow) {
        ImGui::SetKeyboardFocusHere(0);
        *pendingFlag = false;
    }
}

/* True if this frame saw Escape or gamepad-B (the universal back button). */
static bool mp_BackPressed(void)
{
    return !ImGui::IsWindowAppearing() &&
           ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

/* =========================================================================
 * Renderer: Arena picker (g_MpArenaMenuDialog)
 * =========================================================================
 *
 * Maps / arenas catalog migration (2026-04-26).  Reads the asset catalog
 * directly via assetCatalogIterateUnlockedByType(ASSET_ARENA, ...) so the
 * list reflects "catalog INTERSECT unlock-state" -- Mike's standing
 * directive.  The legacy mpArenaMenuHandler is no longer consulted here;
 * the picker now mirrors the canonical pdgui_menu_room.cpp shape:
 * collector callback -> sort by section + alphabetical -> per-row
 * Selectable + commit via pdguiMpSetupSetArena().
 */

/* Mirror of pdgui_menu_room.cpp arena_entry layout (kept module-local so
 * the two pickers stay independent and can diverge in future polish). */
#define MPSETUP_ARENA_SEC_MP_BASE  0
#define MPSETUP_ARENA_SEC_CAMPAIGN 1
#define MPSETUP_ARENA_SEC_MOD      2
#define MPSETUP_ARENA_SEC_COUNT    3

struct mpsetup_arena_entry {
    char name[64];
    char id[64];
    s32  stagenum;
    s32  section;       /* MPSETUP_ARENA_SEC_* */
    char category[32];
};

static mpsetup_arena_entry *s_MpArenaList = nullptr;
static s32                  s_MpArenaCount = 0;
static s32                  s_MpArenaCapacity = 0;
static s32                  s_MpArenaUnlockedCountKnown = -1; /* invalidate trigger */
static s32                  s_MpArenaSectionStart[MPSETUP_ARENA_SEC_COUNT] = { 0 };
static s32                  s_MpArenaSectionCount[MPSETUP_ARENA_SEC_COUNT] = { 0 };

static void mpsetupArenaCollect(const asset_entry_t *e, void *userdata)
{
    (void)userdata;
    if (!e || e->type != ASSET_ARENA) return;

    /* Capacity grow.  Mirror pdgui_menu_room.cpp::catalogArenaCollect: start at 32, double. */
    if (s_MpArenaCount >= s_MpArenaCapacity) {
        s32 newCap = (s_MpArenaCapacity == 0) ? 32 : s_MpArenaCapacity * 2;
        mpsetup_arena_entry *newBuf = (mpsetup_arena_entry *)realloc(
                s_MpArenaList, (size_t)newCap * sizeof(mpsetup_arena_entry));
        if (!newBuf) {
            sysLogPrintf(LOG_WARNING,
                "MPSETUP.ARENA: list realloc failed at %d entries",
                s_MpArenaCount);
            return;
        }
        s_MpArenaList = newBuf;
        s_MpArenaCapacity = newCap;
    }

    /* Display name: route through arenaGetName (override table + langbank
     * fallback) so the AllInOne-era langid override map applies here too.
     * A registered-but-nameless arena is a catalog data bug; skip it. */
    const char *name = arenaGetName((u16)e->ext.arena.name_langid);
    if (!name || !name[0] || strcmp(name, "???") == 0) {
        sysLogPrintf(LOG_WARNING,
            "MPSETUP.ARENA: id=\"%s\" stagenum=0x%02x langid=0x%04x has no name, skipping",
            e->id, e->ext.arena.stagenum, e->ext.arena.name_langid);
        return;
    }

    mpsetup_arena_entry *a = &s_MpArenaList[s_MpArenaCount];
    strncpy(a->name, name, sizeof(a->name) - 1);
    a->name[sizeof(a->name) - 1] = '\0';
    strncpy(a->id, e->id, sizeof(a->id) - 1);
    a->id[sizeof(a->id) - 1] = '\0';
    a->stagenum = (s32)e->ext.arena.stagenum;
    strncpy(a->category, e->category, sizeof(a->category) - 1);
    a->category[sizeof(a->category) - 1] = '\0';

    /* Section split mirrors pdgui_menu_room.cpp: mod entries first by
     * !bundled, then Solo Missions = CAMPAIGN, everything else = MP_BASE. */
    if (!e->bundled) {
        a->section = MPSETUP_ARENA_SEC_MOD;
    } else if (strcmp(a->category, "Solo Missions") == 0) {
        a->section = MPSETUP_ARENA_SEC_CAMPAIGN;
    } else {
        a->section = MPSETUP_ARENA_SEC_MP_BASE;
    }

    s_MpArenaCount++;
}

static int mpsetupArenaCompare(const void *a, const void *b)
{
    const mpsetup_arena_entry *ea = (const mpsetup_arena_entry *)a;
    const mpsetup_arena_entry *eb = (const mpsetup_arena_entry *)b;
    if (ea->section != eb->section) return ea->section - eb->section;
    return strcasecmp(ea->name, eb->name);
}

static void mpsetupArenaListBuild(void)
{
    s_MpArenaCount = 0;
    /* assetCatalogIterateUnlockedByType already applies the
     * challengeIsFeatureUnlocked filter; the collector does not need to
     * re-check.  This is the SHAPE the audit prescribes: selector pool =
     * catalog INTERSECT unlock-state, single source of truth. */
    assetCatalogIterateUnlockedByType(ASSET_ARENA, mpsetupArenaCollect, nullptr);

    if (s_MpArenaCount > 1) {
        qsort(s_MpArenaList, (size_t)s_MpArenaCount,
              sizeof(mpsetup_arena_entry), mpsetupArenaCompare);
    }

    /* Section boundary scan -- mirrors pdgui_menu_room.cpp. */
    for (s32 s = 0; s < MPSETUP_ARENA_SEC_COUNT; s++) {
        s_MpArenaSectionStart[s] = 0;
        s_MpArenaSectionCount[s] = 0;
    }
    for (s32 i = 0; i < s_MpArenaCount; i++) {
        s32 sec = s_MpArenaList[i].section;
        if (sec >= 0 && sec < MPSETUP_ARENA_SEC_COUNT) {
            if (s_MpArenaSectionCount[sec] == 0) s_MpArenaSectionStart[sec] = i;
            s_MpArenaSectionCount[sec]++;
        }
    }

    s_MpArenaUnlockedCountKnown = assetCatalogGetUnlockedCountByType(ASSET_ARENA);
    sysLogPrintf(LOG_NOTE,
        "MPSETUP.ARENA: list built: %d arenas (MP=%d, Campaign=%d, Mod=%d)",
        s_MpArenaCount,
        s_MpArenaSectionCount[MPSETUP_ARENA_SEC_MP_BASE],
        s_MpArenaSectionCount[MPSETUP_ARENA_SEC_CAMPAIGN],
        s_MpArenaSectionCount[MPSETUP_ARENA_SEC_MOD]);
}

/* Rebuild trigger: count delta on appearing or whenever the catalog has
 * gained/lost unlocked arenas since the last build (e.g. challenge flag
 * change, mod scan added a map). */
static void mpsetupArenaListEnsure(bool windowAppearing)
{
    s32 currentUnlocked = assetCatalogGetUnlockedCountByType(ASSET_ARENA);
    if (windowAppearing
            || s_MpArenaUnlockedCountKnown < 0
            || currentUnlocked != s_MpArenaUnlockedCountKnown) {
        mpsetupArenaListBuild();
    }
}

static const char *mpsetupArenaSectionLabel(s32 section)
{
    switch (section) {
    case MPSETUP_ARENA_SEC_MP_BASE:  return "Combat Simulator";
    case MPSETUP_ARENA_SEC_CAMPAIGN: return "Solo Missions";
    case MPSETUP_ARENA_SEC_MOD:      return "Mods";
    default:                         return "Other";
    }
}

static s32 renderMpArena(struct menudialog *dialog, struct menu *, s32, s32)
{
    static bool s_ArenaFocusPending = false;   /* M-19 focus-on-open */

    WindowFrame wf = mp_BeginStandardWindow("##mp_arena", "Arena", 0.70f, 0.80f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mp_BackPressed()) {
        mp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    mp_ArmFocusOnOpen(&s_ArenaFocusPending);

    /* Catalog-driven list rebuild on appearing or unlock-count delta. */
    mpsetupArenaListEnsure(ImGui::IsWindowAppearing());

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    /* Resolve the currently selected stagenum to a list index for the
     * focus latch.  Falls back to 0 when no selection matches (fresh
     * build / mod-removed arena). */
    const u8 currentStagenum = pdguiPauseGetStagenum();
    s32 selectedIdx = -1;
    for (s32 i = 0; i < s_MpArenaCount; i++) {
        if ((u8)s_MpArenaList[i].stagenum == currentStagenum) {
            selectedIdx = i;
            break;
        }
    }

    bool selectionMade = false;

    if (ImGui::BeginChild("##mp_arena_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {
        s32 lastSection = -1;
        for (s32 i = 0; i < s_MpArenaCount; i++) {
            const mpsetup_arena_entry *a = &s_MpArenaList[i];

            if (a->section != lastSection) {
                if (lastSection != -1) ImGui::Spacing();
                ImGui::TextDisabled("%s", mpsetupArenaSectionLabel(a->section));
                ImGui::Separator();
                lastSection = a->section;
            }

            ImGui::PushID(i);
            bool isSelected = (i == selectedIdx);
            mp_ConsumePendingFocus(&s_ArenaFocusPending, isSelected);
            if (ImGui::Selectable(a->name, isSelected, ImGuiSelectableFlags_None)) {
                pdguiMpSetupSetArena((u8)a->stagenum, a->id);
                pdguiPlaySound(PDGUI_SND_SELECT);
                /* Legacy dialog def has MENUDIALOGFLAG_CLOSEONSELECT.  We
                 * replicate that close ourselves after the body block to
                 * keep ImGui's Begin/End nesting balanced. */
                selectionMade = true;
            }
            ImGui::PopID();
        }

        if (s_MpArenaCount == 0) {
            ImGui::TextDisabled("No arenas available.");
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_arena_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();

    if (selectionMade) {
        mp_CloseCurrentDialog();
    }
    return 1;
}

/* =========================================================================
 * Renderer: Scenario picker (g_MpScenarioMenuDialog + QuickTeam variant)
 * =========================================================================
 *
 * Uses scenarioScenarioMenuHandler with item.param = {0 normal, 1 quickteam}.
 * Legacy def has CLOSEONSELECT -- picking a scenario auto-pops.
 */

static s32 renderMpScenarioImpl(u8 param, const char *imguiId, const char *title,
                                 const struct menudialogdef *def)
{
    /* M-19: scenario + quickteam-scenario share one impl but track focus
     * state independently via the param-indexed static array. */
    static bool s_ScenFocusPending[2] = { false, false };

    WindowFrame wf = mp_BeginStandardWindow(imguiId, title, 0.60f, 0.75f, def);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mp_BackPressed()) {
        mp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    mp_ArmFocusOnOpen(&s_ScenFocusPending[param & 1]);

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    s32 selected = list_GetSelectedIndex(scenarioScenarioMenuHandler, param);

    if (ImGui::BeginChild("##mp_scen_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {
        s32 count = list_GetOptionCount(scenarioScenarioMenuHandler, param);
        for (s32 i = 0; i < count; i++) {
            const char *text = list_GetOptionText(scenarioScenarioMenuHandler, param, i);
            if (!text) text = "";

            ImGui::PushID(i);
            bool isSelected = (i == selected);
            mp_ConsumePendingFocus(&s_ScenFocusPending[param & 1], isSelected);
            if (ImGui::Selectable(text, isSelected)) {
                list_Set(scenarioScenarioMenuHandler, param, i);
                pdguiPlaySound(PDGUI_SND_SELECT);
                /* scenarioInit() fires inside the legacy SET path. */
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_scen_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

static s32 renderMpScenario(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpScenarioImpl(0, "##mp_scen", "Scenario",
                                 menupoolDialogDef(dialog));
}

static s32 renderMpQuickTeamScenario(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpScenarioImpl(1, "##mp_scen_qt", "Scenario",
                                 menupoolDialogDef(dialog));
}

/* =========================================================================
 * Renderer: Weapons (g_MpWeaponsMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:1593):
 *   - "Set:" dropdown  (menuhandlerMpWeaponSetDropdown, param=1)
 *   - "Select Weapons" selectable (menuhandlerMpSelectRandomWeapons, hidden unless random set)
 *   - "Auto Random" dropdown (menuhandlerMpAutoRandomWeapon, hidden unless random set)
 *   - 6x slot dropdowns (menuhandlerMpWeaponSlot, param3 = slot index 0..5)
 *   - Back
 *
 * Shows "Select Weapons" button (opens sub-dialog) only when the current set
 * is a random variant -- we ask the handler via CHECKHIDDEN to keep the gate
 * in one place.  Same for Auto Random.
 */

/* Render a dropdown row backed by a legacy handler.  Mirrors ImGui Combo. */
static void renderHandlerDropdown(const char *label,
                                  uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
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

static s32 renderMpWeapons(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mp_BeginStandardWindow("##mp_weapons", "Weapons", 0.60f, 0.80f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mp_BackPressed()) {
        mp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_weapons_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        ImGui::PushItemWidth(pdguiScale(260.0f));

        /* M-19 focus-on-open: the first dropdown ("Set") is the entry
         * point for a controller user — land focus on it so D-pad / left-
         * stick moves into the combo immediately. */
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(0);
        }

        renderHandlerDropdown("Set##mp_ws", menuhandlerMpWeaponSetDropdown, 1, 0);

        ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));

        /* "Select Weapons" -- only visible when current set is random.  The
         * legacy handler uses CHECKHIDDEN: returns false (visible) when the
         * current set is WEAPONSET_RANDOM or WEAPONSET_RANDOMFIVE. */
        bool randomHidden = cb_IsHidden(menuhandlerMpSelectRandomWeapons, 0, 0);
        if (!randomHidden) {
            if (ImGui::Button("Select Weapons...##mp_srw")) {
                /* Delegate push to the legacy handler so the same menuPushDialog
                 * path runs (some versions register as a selectable-opens-dialog
                 * with NULL handler; the explicit handler approach here uses
                 * menuhandlerMpSelectRandomWeapons SET which calls menuPushDialog
                 * internally). */
                plain_Set(menuhandlerMpSelectRandomWeapons, 0);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }

            ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
            renderHandlerDropdown("Auto Random##mp_ar", menuhandlerMpAutoRandomWeapon, 0, 0);
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        const char *slotLabels[6] = {
            "Slot 1##mp_ws0",
            "Slot 2##mp_ws1",
            "Slot 3##mp_ws2",
            "Slot 4##mp_ws3",
            "Slot 5##mp_ws4",
            "Slot 6##mp_ws5",
        };
        for (s32 i = 0; i < 6; i++) {
            /* Legacy uses param3 as the slot index for menuhandlerMpWeaponSlot. */
            renderHandlerDropdown(slotLabels[i], menuhandlerMpWeaponSlot, 0, (intptr_t)i);
        }

        ImGui::PopItemWidth();
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_weapons_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: Select Random Weapons (g_MpSelectRandomWeaponsMenuDialog)
 * =========================================================================
 *
 * Legacy list handler exposes (count + 4) rows: first <count> are per-weapon
 * checkboxes, last 4 are "Select Dark", "Select Classic", "Select All",
 * "Select None".  We render the same thing with real ImGui widgets.
 *
 * For the per-weapon rows, MENUOP_GETLISTITEMCHECKBOX writes the current
 * checkbox state into data->list.unk04.  Clicking a row calls SET with
 * data->list.unk04 == 0 (toggle semantics match the legacy "if unk04==0"
 * branch).
 */

static bool srw_GetRowChecked(s32 idx)
{
    s203_menuitem it{};
    s203_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    h.list.unk04 = 0;
    mpSelectRandomWeaponListHandler(MENUOP_GETLISTITEMCHECKBOX, &it, &h);
    return h.list.unk04 != 0;
}

static void srw_ToggleRow(s32 idx)
{
    s203_menuitem it{};
    s203_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    h.list.unk04 = 0;  /* must be 0 to hit the toggle branch in the handler */
    mpSelectRandomWeaponListHandler(MENUOP_SET, &it, &h);
}

static s32 renderMpSelectRandomWeapons(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mp_BeginStandardWindow("##mp_srw", "Select Weapons", 0.55f, 0.85f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mp_BackPressed()) {
        mp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    s32 total = list_GetOptionCount(mpSelectRandomWeaponListHandler, 0);
    s32 numWeapons = total - 4;
    if (numWeapons < 0) numWeapons = 0;

    if (ImGui::BeginChild("##mp_srw_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* Per-weapon checkbox rows */
        for (s32 i = 0; i < numWeapons; i++) {
            const char *text = list_GetOptionText(mpSelectRandomWeaponListHandler, 0, i);
            if (!text) text = "";

            bool checked = srw_GetRowChecked(i);
            ImGui::PushID(i);
            bool before = checked;
            /* Priority L (2026-04-25): label LEFT via pdguiCheckbox.
             * pdguiCheckbox plays its own toggle sound, so we drop the
             * legacy explicit pdguiPlaySound. */
            if (pdguiCheckbox(text, &checked)) {
                if (before != checked) {
                    srw_ToggleRow(i);
                }
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        /* 4 action rows (Select Dark/Classic/All/None) */
        for (s32 i = 0; i < 4; i++) {
            const char *text = list_GetOptionText(mpSelectRandomWeaponListHandler, 0, numWeapons + i);
            if (!text) text = "";
            ImGui::PushID(numWeapons + i);
            if (ImGui::Button(text)) {
                list_Set(mpSelectRandomWeaponListHandler, 0, numWeapons + i);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            ImGui::PopID();
            if (i < 3) ImGui::SameLine();
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_srw_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: QuickTeam Weapons (g_MpQuickTeamWeaponsMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:1710):
 *   - "Set:" dropdown (menuhandlerMpWeaponSetDropdown, param=0)
 *   - 5x read-only label rows showing each weapon slot's current weapon
 *
 * We piggyback on menuhandlerMpWeaponSlot GETSELECTEDINDEX+GETOPTIONTEXT to
 * read the name of whatever is currently set into each slot, then render
 * as text.  No backing-store writes from this screen except via the dropdown.
 */

static const char *qtw_SlotName(s32 slot)
{
    s32 cur = dd_GetSelectedIndex(menuhandlerMpWeaponSlot, 0, (intptr_t)slot);
    const char *t = dd_GetOptionText(menuhandlerMpWeaponSlot, 0, (intptr_t)slot, cur);
    return t ? t : "";
}

static s32 renderMpQuickTeamWeapons(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mp_BeginStandardWindow("##mp_qtw", "Weapons", 0.55f, 0.75f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mp_BackPressed()) {
        mp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_qtw_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        ImGui::PushItemWidth(pdguiScale(260.0f));
        renderHandlerDropdown("Set##mp_qtws", menuhandlerMpWeaponSetDropdown, 0, 0);
        ImGui::PopItemWidth();

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        ImGui::TextDisabled("Current Weapon Setup:");
        for (s32 i = 0; i < 5; i++) {
            ImGui::Text("  %d:  %s", i + 1, qtw_SlotName(i));
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_qtw_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: Limits (g_MpLimitsMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:3099):
 *   - Time slider  (handler=menuhandlerMpTimeLimitSlider,  max=0x3c=60)
 *   - Score slider (handler=menuhandlerMpScoreLimitSlider, max=0x64=100)
 *   - Team Score   (handler=menuhandlerMpTeamScoreLimitSlider, max=0x190=400)
 *   - Restore Defaults (handler=menuhandlerMpRestoreScoreDefaults)
 *   - Back
 *
 * Slider labels come from MENUOP_GETSLIDERLABEL so "No Limit" / "%d Min" /
 * "%d" localized text stays in the legacy handler.
 */

static bool renderSliderRow(const char *label,
                            uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                            u8 param, intptr_t param3, s32 maxVal)
{
    u32 cur = sl_Get(fn, param, param3);

    char labelBuf[64];
    sl_GetLabel(fn, param, param3, cur, labelBuf, sizeof(labelBuf));

    s32 v = (s32)cur;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", labelBuf[0] ? labelBuf : "");
    bool changed = ImGui::SliderInt("##slider", &v, 0, maxVal, "");
    if (changed) {
        if (v < 0) v = 0;
        if (v > maxVal) v = maxVal;
        sl_Set(fn, param, param3, (u32)v);
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    ImGui::PopID();
    return changed;
}

static s32 renderMpLimits(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mp_BeginStandardWindow("##mp_limits", "Limits", 0.55f, 0.65f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mp_BackPressed()) {
        mp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_limits_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        ImGui::PushItemWidth(pdguiScale(260.0f));

        /* M-19 focus-on-open: Time slider is the first interactive widget
         * in the Limits flow — controller lands here on open so the user
         * can adjust without a preparatory D-pad press. */
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(0);
        }

        renderSliderRow("Time",       menuhandlerMpTimeLimitSlider,      0, 0x3c, 60);
        ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));

        renderSliderRow("Score",      menuhandlerMpScoreLimitSlider,     0, 0x64, 100);
        ImGui::Dummy(ImVec2(0, pdguiScale(6.0f)));

        renderSliderRow("Team Score", menuhandlerMpTeamScoreLimitSlider, 0, 0x190, 400);
        ImGui::Dummy(ImVec2(0, pdguiScale(8.0f)));

        ImGui::PopItemWidth();

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        if (ImGui::Button("Restore Defaults##mp_rd")) {
            plain_Set(menuhandlerMpRestoreScoreDefaults, 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_limits_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: Scenario Options (6 dialogs share one implementation)
 * =========================================================================
 *
 * Legacy layouts (combat.inc / capturethecase.inc / hackthatmac.inc /
 * holdthebriefcase.inc / kingofthehill.inc / popacap.inc) all follow the
 * same shape:
 *   1. One-Hit Kills checkbox   (menuhandlerMpOneHitKills)
 *   2. Slow Motion dropdown     (menuhandlerMpSlowMotion)
 *   3. Fast Movement checkbox   (menuhandlerMpCheckboxOption + FASTMOVEMENT)
 *   4. Display Team checkbox    (menuhandlerMpDisplayTeam + DISPLAYTEAM)
 *   5. No Radar checkbox        (menuhandlerMpCheckboxOption + NORADAR)
 *   6. No Auto-Aim checkbox     (menuhandlerMpCheckboxOption + NOAUTOAIM)
 *   7. (combat only) No Player Highlight / No Pickup Highlight
 *   7. (others)     Kills Score                                (OPTIONS_493)
 *   + per-scenario extras (CTC_SHOWONRADAR, KOH_HILLONRADAR + MOBILEHILL +
 *     Hill Time slider, HTM_HIGHLIGHTTERMINAL + SHOWONRADAR, HTB_HIGHLIGHT +
 *     SHOWONRADAR, PAC_HIGHLIGHTTARGET + SHOWONRADAR)
 *
 * The "Combat Options" variant lacks "Kills Score" but has two extra rows
 * (NOPLAYERHIGHLIGHT, NOPICKUPHIGHLIGHT) after a separator.  All 6 variants
 * share the first 6 rows.
 *
 * Also: all 6 dialogs set nextsibling = &g_ExtGameOptionsMenuDialog in the
 * legacy def, which the native renderer interprets as a tab strip.  Our
 * replacement renders the sibling as an "Extended Options..." action-bar
 * button that opens g_ExtGameOptionsMenuDialog -- same content, clearer UX.
 */

#define MPOPTION_ONEHITKILLS            0x00000001u
#define MPOPTION_NORADAR                0x00000004u
#define MPOPTION_NOAUTOAIM              0x00000008u
#define MPOPTION_NOPLAYERHIGHLIGHT      0x00000010u
#define MPOPTION_NOPICKUPHIGHLIGHT      0x00000020u
#define MPOPTION_FASTMOVEMENT           0x00000100u
#define MPOPTION_DISPLAYTEAM            0x00000200u
#define MPOPTION_KILLSSCORE             0x00000400u
#define MPOPTION_HTB_HIGHLIGHTBRIEFCASE 0x00000800u
#define MPOPTION_HTB_SHOWONRADAR        0x00001000u
#define MPOPTION_CTC_SHOWONRADAR        0x00002000u
#define MPOPTION_KOH_HILLONRADAR        0x00004000u
#define MPOPTION_KOH_MOBILEHILL         0x00008000u
#define MPOPTION_HTM_HIGHLIGHTTERMINAL  0x00020000u
#define MPOPTION_HTM_SHOWONRADAR        0x00040000u
#define MPOPTION_PAC_HIGHLIGHTTARGET    0x00080000u
#define MPOPTION_PAC_SHOWONRADAR        0x00100000u
#define MPOPTION_SPAWNWITHWEAPON        0x00200000u
#define MPOPTION_NODRUGBLUR             0x00400000u
#define MPOPTION_FRIENDLYFIRE           0x02000000u
#define MPOPTION_NOPLAYERONRADAR        0x04000000u
#define MPOPTION_NODOORS                0x08000000u

enum ScenarioOptionVariant {
    SC_OPT_COMBAT = 0,
    SC_OPT_CTC,
    SC_OPT_HTM,
    SC_OPT_HTB,
    SC_OPT_KOH,
    SC_OPT_PAC,
    SC_OPT_COUNT
};

/* Render a generic option checkbox row that delegates to a legacy handler.
 * bitFlag = MPOPTION_* used as item->param3 in the legacy item table. */
static void renderOptionCheckboxRow(const char *label,
                                    uintptr_t (*fn)(s32, s203_menuitem *, s203_handlerdata *),
                                    u32 bitFlag)
{
    bool disabled = cb_IsDisabled(fn, 0, (intptr_t)bitFlag);
    bool hidden   = cb_IsHidden  (fn, 0, (intptr_t)bitFlag);
    if (hidden) return;

    bool v = cb_Get(fn, 0, (intptr_t)bitFlag);

    ImGui::PushID(label);
    if (disabled) ImGui::BeginDisabled();
    bool before = v;
    /* Priority L (2026-04-25): label LEFT via pdguiCheckbox. */
    if (pdguiCheckbox(label, &v)) {
        if (before != v) {
            cb_Set(fn, 0, (intptr_t)bitFlag, v);
        }
    }
    if (disabled) ImGui::EndDisabled();
    ImGui::PopID();
}

static void renderSharedScenarioTop(void)
{
    /* One-Hit Kills + feature gating */
    renderOptionCheckboxRow("One-Hit Kills",  menuhandlerMpOneHitKills,   MPOPTION_ONEHITKILLS);

    /* Slow Motion dropdown */
    {
        ImGui::PushID("slowmo");
        bool disabled = cb_IsDisabled(menuhandlerMpSlowMotion, 0, 0);
        bool hidden   = cb_IsHidden  (menuhandlerMpSlowMotion, 0, 0);
        if (!hidden) {
            if (disabled) ImGui::BeginDisabled();
            ImGui::PushItemWidth(pdguiScale(180.0f));
            renderHandlerDropdown("Slow Motion", menuhandlerMpSlowMotion, 0, 0);
            ImGui::PopItemWidth();
            if (disabled) ImGui::EndDisabled();
        }
        ImGui::PopID();
    }

    /* Generic options */
    renderOptionCheckboxRow("Fast Movement",  menuhandlerMpCheckboxOption, MPOPTION_FASTMOVEMENT);
    renderOptionCheckboxRow("Display Team",   menuhandlerMpDisplayTeam,    MPOPTION_DISPLAYTEAM);
    renderOptionCheckboxRow("No Radar",       menuhandlerMpCheckboxOption, MPOPTION_NORADAR);
    renderOptionCheckboxRow("No Auto-Aim",    menuhandlerMpCheckboxOption, MPOPTION_NOAUTOAIM);
}

static void renderScenarioOptionsBody(ScenarioOptionVariant variant)
{
    renderSharedScenarioTop();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

    switch (variant) {
    case SC_OPT_COMBAT:
        renderOptionCheckboxRow("No Player Highlight", menuhandlerMpCheckboxOption, MPOPTION_NOPLAYERHIGHLIGHT);
        renderOptionCheckboxRow("No Pickup Highlight", menuhandlerMpCheckboxOption, MPOPTION_NOPICKUPHIGHLIGHT);
        break;

    case SC_OPT_CTC:
        renderOptionCheckboxRow("Kills Score",   menuhandlerMpCheckboxOption, MPOPTION_KILLSSCORE);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        renderOptionCheckboxRow("Show on Radar", menuhandlerMpCheckboxOption, MPOPTION_CTC_SHOWONRADAR);
        break;

    case SC_OPT_HTM:
        renderOptionCheckboxRow("Kills Score",        menuhandlerMpCheckboxOption, MPOPTION_KILLSSCORE);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        renderOptionCheckboxRow("Highlight Terminal", menuhandlerMpCheckboxOption, MPOPTION_HTM_HIGHLIGHTTERMINAL);
        renderOptionCheckboxRow("Show on Radar",      menuhandlerMpCheckboxOption, MPOPTION_HTM_SHOWONRADAR);
        break;

    case SC_OPT_HTB:
        renderOptionCheckboxRow("Kills Score",         menuhandlerMpCheckboxOption, MPOPTION_KILLSSCORE);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        renderOptionCheckboxRow("Highlight Briefcase", menuhandlerMpCheckboxOption, MPOPTION_HTB_HIGHLIGHTBRIEFCASE);
        renderOptionCheckboxRow("Show on Radar",       menuhandlerMpCheckboxOption, MPOPTION_HTB_SHOWONRADAR);
        break;

    case SC_OPT_KOH:
        renderOptionCheckboxRow("Kills Score",   menuhandlerMpCheckboxOption, MPOPTION_KILLSSCORE);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        renderOptionCheckboxRow("Hill on Radar", menuhandlerMpCheckboxOption, MPOPTION_KOH_HILLONRADAR);
        renderOptionCheckboxRow("Mobile Hill",   menuhandlerMpCheckboxOption, MPOPTION_KOH_MOBILEHILL);
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        ImGui::PushItemWidth(pdguiScale(260.0f));
        renderSliderRow("Hill Time", menuhandlerMpHillTime, 0, 0x6e, 110);
        ImGui::PopItemWidth();
        break;

    case SC_OPT_PAC:
        renderOptionCheckboxRow("Kills Score",     menuhandlerMpCheckboxOption, MPOPTION_KILLSSCORE);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        renderOptionCheckboxRow("Highlight Target", menuhandlerMpCheckboxOption, MPOPTION_PAC_HIGHLIGHTTARGET);
        renderOptionCheckboxRow("Show on Radar",    menuhandlerMpCheckboxOption, MPOPTION_PAC_SHOWONRADAR);
        break;

    default:
        break;
    }
}

struct ScenarioOptionsDesc {
    const char *imguiId;
    const char *title;
    ScenarioOptionVariant variant;
};

static const ScenarioOptionsDesc k_ScenarioOptions[SC_OPT_COUNT] = {
    { "##mp_opt_combat", "Combat Options",    SC_OPT_COMBAT },
    { "##mp_opt_ctc",    "Capture Options",   SC_OPT_CTC },
    { "##mp_opt_htm",    "Hacker Options",    SC_OPT_HTM },
    { "##mp_opt_htb",    "Briefcase Options", SC_OPT_HTB },
    { "##mp_opt_koh",    "Hill Options",      SC_OPT_KOH },
    { "##mp_opt_pac",    "Pop a Cap Options", SC_OPT_PAC },
};

static s32 renderMpScenarioOptionsImpl(ScenarioOptionVariant variant,
                                        const struct menudialogdef *def)
{
    const ScenarioOptionsDesc &d = k_ScenarioOptions[variant];

    WindowFrame wf = mp_BeginStandardWindow(d.imguiId, d.title, 0.60f, 0.80f, def);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mp_BackPressed()) {
        mp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    bool wantExtOpts = false;

    if (ImGui::BeginChild("##mp_opt_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {
        renderScenarioOptionsBody(variant);
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_opt_ab")) {
        float barW = ImGui::GetContentRegionAvail().x;
        float btnW = barW * 0.5f;
        if (pdguiActionBarButton("More Options...", 0, btnW)) {
            wantExtOpts = true;
        }
        ImGui::SameLine();
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();

    if (wantExtOpts) {
        menuPushDialog(&g_ExtGameOptionsMenuDialog);
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }
    return 1;
}

static s32 renderMpCombatOptions  (struct menudialog *dialog, struct menu *, s32, s32)
{ return renderMpScenarioOptionsImpl(SC_OPT_COMBAT,
                                      menupoolDialogDef(dialog)); }
static s32 renderMpCtcOptions     (struct menudialog *dialog, struct menu *, s32, s32)
{ return renderMpScenarioOptionsImpl(SC_OPT_CTC,
                                      menupoolDialogDef(dialog)); }
static s32 renderMpHtmOptions     (struct menudialog *dialog, struct menu *, s32, s32)
{ return renderMpScenarioOptionsImpl(SC_OPT_HTM,
                                      menupoolDialogDef(dialog)); }
static s32 renderMpHtbOptions     (struct menudialog *dialog, struct menu *, s32, s32)
{ return renderMpScenarioOptionsImpl(SC_OPT_HTB,
                                      menupoolDialogDef(dialog)); }
static s32 renderMpKohOptions     (struct menudialog *dialog, struct menu *, s32, s32)
{ return renderMpScenarioOptionsImpl(SC_OPT_KOH,
                                      menupoolDialogDef(dialog)); }
static s32 renderMpPacOptions     (struct menudialog *dialog, struct menu *, s32, s32)
{ return renderMpScenarioOptionsImpl(SC_OPT_PAC,
                                      menupoolDialogDef(dialog)); }

/* =========================================================================
 * Renderer: Extended Game Options (g_ExtGameOptionsMenuDialog)
 * =========================================================================
 *
 * Legacy (setup.c:6486): 5 global match checkboxes.  Uses
 * menuhandlerMpDisplayTeam for Friendly Fire (CHECKDISABLED gates on teams).
 */

static s32 renderMpExtGameOptions(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = mp_BeginStandardWindow("##mp_extopt", "More Options", 0.55f, 0.65f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (mp_BackPressed()) {
        mp_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##mp_extopt_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {
        renderOptionCheckboxRow("Start Armed",       menuhandlerMpCheckboxOption, MPOPTION_SPAWNWITHWEAPON);
        renderOptionCheckboxRow("No Drug Blur",      menuhandlerMpCheckboxOption, MPOPTION_NODRUGBLUR);
        renderOptionCheckboxRow("Friendly Fire",     menuhandlerMpDisplayTeam,    MPOPTION_FRIENDLYFIRE);
        renderOptionCheckboxRow("No Player on Radar",menuhandlerMpCheckboxOption, MPOPTION_NOPLAYERONRADAR);
        renderOptionCheckboxRow("No Doors",          menuhandlerMpCheckboxOption, MPOPTION_NODOORS);
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##mp_extopt_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            mp_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" void pdguiMenuMpSetupRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    pdguiHotswapRegister(&g_MpArenaMenuDialog,              renderMpArena,              "MP Arena");
    pdguiHotswapRegister(&g_MpScenarioMenuDialog,           renderMpScenario,           "MP Scenario");
    pdguiHotswapRegister(&g_MpQuickTeamScenarioMenuDialog,  renderMpQuickTeamScenario,  "MP QT Scenario");
    pdguiHotswapRegister(&g_MpWeaponsMenuDialog,            renderMpWeapons,            "MP Weapons");
    pdguiHotswapRegister(&g_MpSelectRandomWeaponsMenuDialog,renderMpSelectRandomWeapons,"MP Select Random Weapons");
    pdguiHotswapRegister(&g_MpQuickTeamWeaponsMenuDialog,   renderMpQuickTeamWeapons,   "MP QT Weapons");
    pdguiHotswapRegister(&g_MpLimitsMenuDialog,             renderMpLimits,             "MP Limits");

    pdguiHotswapRegister(&g_MpCombatOptionsMenuDialog,      renderMpCombatOptions,      "MP Combat Options");
    pdguiHotswapRegister(&g_CtcOptionsMenuDialog,           renderMpCtcOptions,         "MP CTC Options");
    pdguiHotswapRegister(&g_HtmOptionsMenuDialog,           renderMpHtmOptions,         "MP HTM Options");
    pdguiHotswapRegister(&g_HtbOptionsMenuDialog,           renderMpHtbOptions,         "MP HTB Options");
    pdguiHotswapRegister(&g_KohOptionsMenuDialog,           renderMpKohOptions,         "MP KOH Options");
    pdguiHotswapRegister(&g_PacOptionsMenuDialog,           renderMpPacOptions,         "MP PAC Options");
    pdguiHotswapRegister(&g_ExtGameOptionsMenuDialog,       renderMpExtGameOptions,     "MP Ext Game Options");

    sysLogPrintf(LOG_NOTE, "MENU_IMGUI: Batch 5 MP Setup Core registered (14 dialogs)");
}
