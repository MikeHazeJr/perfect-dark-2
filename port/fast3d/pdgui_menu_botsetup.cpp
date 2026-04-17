/**
 * pdgui_menu_botsetup.cpp -- ImGui replacement for Bot/Simulant setup dialogs.
 *
 * D5 Phase 3 Batch 6.
 *
 * Covers 5 legacy menudialogdefs in the Combat Simulator bot-setup path:
 *
 *   g_MpSimulantsMenuDialog          -> renderMpSimulants
 *   g_MpAddSimulantMenuDialog        -> renderMpAddSimulant      (ADD variant)
 *   g_MpChangeSimulantMenuDialog     -> renderMpChangeSimulant   (CHANGE variant)
 *   g_MpEditSimulantMenuDialog       -> renderMpEditSimulant
 *   g_MpSimulantCharacterMenuDialog  -> renderMpSimulantCharacter
 *
 * Design:
 *   - Reuses primitives from pdgui_layout / pdgui_style / pdgui_audio (scrim,
 *     PD title frame, docked action bar, SFX).  No new helpers invented.
 *   - Delegates ALL state mutation to legacy C handlers in setup.c via the
 *     s204 shadow-struct call-through pattern (cloned from s203 in
 *     pdgui_menu_mpsetup.cpp / s202 in pdgui_menu_cheats.cpp / s194 in
 *     pdgui_menu_solomission.cpp).  s204 adds a `carousel` handlerdata
 *     variant (new for this batch) so we can invoke the two
 *     MENUITEMTYPE_CAROUSEL handlers `menuhandlerMpSimulantHead` and
 *     `menuhandlerMpSimulantBody`.
 *   - The legacy handlers own:
 *         g_BotConfigsArray[].base.{name,mpheadnum,mpbodynum}
 *         g_BotConfigsArray[].{type,difficulty}
 *         g_Menus[0].mpsetup.{slotindex,slotcount,unke24}
 *         mpCopySimulant / mpRemoveSimulant / mpCreateBotFromProfile
 *         mpSetBotDifficulty / mpchrSetHeadByIndex / mpchrSetBodyByIndex
 *         mpGenerateBotNames
 *     We never touch any of those globals directly from C++.
 *   - Dialog registration via pdguiHotswapRegister, called from
 *     pdguiMenusRegisterAll() through pdguiMenuBotSetupRegister()
 *     (declared in pdgui_menus.h).
 *
 * What is NOT in this file (per Mike's standing order):
 *   - No edits to pdgui_menu_solomission.cpp (critical collision rule)
 *   - No edits to pdgui_menu_mpsetup.cpp    (Batch 5 file, separate batch)
 *   - No edits to pdgui_menu_room.cpp       (modern lobby, unrelated flow)
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32 breaks
 * C++).  Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 *
 * See context/scratch/D5-P3-batch6-2026-04-11.md for the dialog -> handler
 * -> state-write map and the zero-function-loss audit.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <float.h>   /* FLT_MIN for ImGui::SetNextItemWidth(-FLT_MIN) */

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui_model_preview.h" /* reusable 3D model preview widget (FBO+ImGui) */
#include "pdgui.h"        /* langSafe */
#include "system.h"
#include "inputctx.h"
#include "menupool.h"

extern "C" {
#include "pdgui_menus.h"  /* for pdguiMenuBotSetupRegister declaration */
}

/* =========================================================================
 * Forward declarations -- game symbols (extern "C", no types.h)
 * ========================================================================= */

extern "C" {

/* ---- Dialog definitions ---- */
extern struct menudialogdef g_MpSimulantsMenuDialog;
extern struct menudialogdef g_MpAddSimulantMenuDialog;
extern struct menudialogdef g_MpChangeSimulantMenuDialog;
extern struct menudialogdef g_MpEditSimulantMenuDialog;
extern struct menudialogdef g_MpSimulantCharacterMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* ---- Language ---- */
char *langGet(s32 textid);
/* langSafe comes from pdgui.h */

/* ---- Bot/participant public API (read-only here; mutation via handlers) ---- */
bool mpIsParticipantActive(s32 index);
char *mpGetBodyName(u8 mpbodynum);
const char *catalogMpHeadId(s32 mpheadnum);
const char *catalogMpBodyId(s32 mpbodynum);

/* ---- MENUOP_* opcodes (declared locally per Batch 4 gotcha; values must
 * match src/include/constants.h exactly) ---- */
#define MENUOP_GETOPTIONCOUNT      1
#define MENUOP_GETOPTGROUPCOUNT    2
#define MENUOP_GETOPTIONTEXT       3
#define MENUOP_GETOPTGROUPTEXT     4
#define MENUOP_GETGROUPSTARTINDEX  5
#define MENUOP_SET                 6
#define MENUOP_GETSELECTEDINDEX    7
#define MENUOP_GET                 8
#define MENUOP_11                  11 /* "refresh character model" -- fires via runtime, not from C++ */
#define MENUOP_CHECKDISABLED       12
#define MENUOP_FOCUS               13
#define MENUOP_LISTITEMFOCUS       16
#define MENUOP_CHECKHIDDEN         24
#define MENUOP_OPEN                100
#define MENUOP_CLOSE               101
#define MENUOP_TICK                102

/* ---- Bot slots live at MAX_PLAYERS..MAX_MPCHRS-1 in the participant pool
 * (B-12 Phase 3 — BOT_SLOT_OFFSET constant was retired). Local alias kept
 * ABI-stable for the row-label predicates below; all state mutation still
 * flows through the server. ---- */
#define BOT_SLOT_OFFSET MAX_PLAYERS

/* ---- s204 shadow menuitem / handlerdata
 * ABI-compatible with the real types in src/include/types.h:3337..3417.
 * Cloned from the s203 pattern in pdgui_menu_mpsetup.cpp, extended with a
 * `carousel` variant so we can call the two MENUITEMTYPE_CAROUSEL handlers.
 * C handlers can be invoked through function pointers from C++ without
 * including types.h (#define bool s32 breaks C++ compilation). */
struct s204_handlerdata_carousel { s32 value; u32 unk04; };
struct s204_handlerdata_checkbox { u32 value; };
struct s204_handlerdata_dropdown { uintptr_t value; uintptr_t unk04; };
struct s204_handlerdata_list_t {
    uintptr_t value;          /* union { uintptr_t value; intptr_t values32; } */
    s32       unk04;          /* union { s32 unk04; u32 unk04u32; } */
    s32       groupstartindex;
    s32       unk0c;
};
struct s204_handlerdata_slider { u32 value; char *label; };

union s204_handlerdata {
    struct s204_handlerdata_carousel carousel;
    struct s204_handlerdata_checkbox checkbox;
    struct s204_handlerdata_dropdown dropdown;
    struct s204_handlerdata_list_t   list;
    struct s204_handlerdata_slider   slider;
    u8 _pad[256];
};

struct s204_menuitem {
    u8        type;
    u8        param;
    u32       flags;
    intptr_t  param2;
    intptr_t  param3;
    uintptr_t (*handler)(s32 op, struct s204_menuitem *, union s204_handlerdata *);
};

/* ---- Legacy item handlers we delegate to (setup.c) ---- */

/* Bot profile list (Add/Change Simulant) */
uintptr_t mpAddChangeSimulantMenuHandler (s32, struct s204_menuitem *, union s204_handlerdata *);

/* Character carousels (Simulant Character) */
uintptr_t menuhandlerMpSimulantHead      (s32, struct s204_menuitem *, union s204_handlerdata *);
uintptr_t menuhandlerMpSimulantBody      (s32, struct s204_menuitem *, union s204_handlerdata *);

/* Edit Simulant item handlers */
uintptr_t mpBotDifficultyMenuHandler     (s32, struct s204_menuitem *, union s204_handlerdata *);
uintptr_t menuhandlerMpChangeSimulantType(s32, struct s204_menuitem *, union s204_handlerdata *);
uintptr_t menuhandlerMpCopySimulant      (s32, struct s204_menuitem *, union s204_handlerdata *);
uintptr_t menuhandlerMpDeleteSimulant    (s32, struct s204_menuitem *, union s204_handlerdata *);

/* Simulants roster item handlers */
uintptr_t menuhandlerMpAddSimulant       (s32, struct s204_menuitem *, union s204_handlerdata *);
uintptr_t menuhandlerMpSimulantSlot      (s32, struct s204_menuitem *, union s204_handlerdata *);
uintptr_t menuhandlerMpClearAllSimulants (s32, struct s204_menuitem *, union s204_handlerdata *);

/* ---- Legacy dynamic-text function pointers (shadow-cast) ----
 * These take `struct menuitem *` / `struct menudialogdef *` but we pass
 * s204_menuitem* / nullptr because their bodies do not dereference the
 * passed pointer (except for mpMenuTextSimulantName which reads item->param
 * at offset 1, matching our shadow layout). */
char *mpMenuTextSimulantName        (struct s204_menuitem *item);
char *mpMenuTextSimulantDescription (struct s204_menuitem *item);
char *mpMenuTitleEditSimulant       (void *dialogdef_nullable);

} /* extern "C" */

/* =========================================================================
 * Module state
 * ========================================================================= */

static bool s_Registered = false;

/* =========================================================================
 * s204 call-through helpers
 *
 * Each helper builds a local shadow menuitem, invokes the legacy handler with
 * the requested MENUOP_*, and returns the result (or writes back through the
 * handlerdata union).  No game globals touched directly.
 * ========================================================================= */

/* ---- List (bot profile list -- grouped) ---- */

typedef uintptr_t (*ListFn)(s32, s204_menuitem *, s204_handlerdata *);

static s32 list_GetOptionCount(ListFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.list.value;
}

static const char *list_GetOptionText(ListFn fn, u8 param, s32 idx)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 list_GetSelectedIndex(ListFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return (s32)h.list.value;
}

static void list_Set(ListFn fn, u8 param, s32 idx)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    fn(MENUOP_SET, &it, &h);
}

static s32 list_GetOptGroupCount(ListFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    fn(MENUOP_GETOPTGROUPCOUNT, &it, &h);
    return (s32)h.list.value;
}

static const char *list_GetOptGroupText(ListFn fn, u8 param, s32 groupIdx)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    h.list.value = (uintptr_t)groupIdx;
    uintptr_t r = fn(MENUOP_GETOPTGROUPTEXT, &it, &h);
    return (const char *)r;
}

static s32 list_GetGroupStartIndex(ListFn fn, u8 param, s32 groupIdx)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    h.list.value = (uintptr_t)groupIdx;
    fn(MENUOP_GETGROUPSTARTINDEX, &it, &h);
    return (s32)h.list.groupstartindex;
}

static void list_ListItemFocus(ListFn fn, u8 param, s32 idx)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    fn(MENUOP_LISTITEMFOCUS, &it, &h);
}

/* ---- Dropdown (Difficulty) ---- */

typedef uintptr_t (*DropFn)(s32, s204_menuitem *, s204_handlerdata *);

static s32 dd_GetOptionCount(DropFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.dropdown.value;
}

static const char *dd_GetOptionText(DropFn fn, u8 param, s32 idx)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    h.dropdown.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 dd_GetSelectedIndex(DropFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return (s32)h.dropdown.value;
}

static void dd_Set(DropFn fn, u8 param, s32 idx)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    h.dropdown.value = (uintptr_t)idx;
    fn(MENUOP_SET, &it, &h);
}

/* ---- Carousel (Head / Body) ---- */

typedef uintptr_t (*CarFn)(s32, s204_menuitem *, s204_handlerdata *);

static s32 car_GetCount(CarFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return h.carousel.value;
}

static s32 car_GetSelectedIndex(CarFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return h.carousel.value;
}

static void car_Set(CarFn fn, u8 param, s32 idx)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    h.carousel.value = idx;
    fn(MENUOP_SET, &it, &h);
}

/* ---- Plain action button (handlers that only care about MENUOP_SET) ---- */

typedef uintptr_t (*PlainFn)(s32, s204_menuitem *, s204_handlerdata *);

static void plain_Set(PlainFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    fn(MENUOP_SET, &it, &h);
}

static bool plain_IsDisabled(PlainFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    uintptr_t r = fn(MENUOP_CHECKDISABLED, &it, &h);
    return r != 0;
}

static bool plain_IsHidden(PlainFn fn, u8 param)
{
    s204_menuitem it{};
    it.param = param;
    s204_handlerdata h{};
    uintptr_t r = fn(MENUOP_CHECKHIDDEN, &it, &h);
    return r != 0;
}

/* =========================================================================
 * Dynamic-text helpers (call legacy function pointers via shadow cast)
 * ========================================================================= */

static const char *bot_GetSlotName(s32 slotIndex)
{
    s204_menuitem it{};
    it.param = (u8)slotIndex;
    char *n = mpMenuTextSimulantName(&it);
    return n ? n : "";
}

static const char *bot_GetProfileDescription(void)
{
    s204_menuitem it{};
    char *d = mpMenuTextSimulantDescription(&it);
    return d ? d : "";
}

static const char *bot_GetEditTitle(void)
{
    /* mpMenuTitleEditSimulant does NOT dereference its argument; it reads
     * g_Menus[0].mpsetup.slotindex internally.  Safe to pass nullptr. */
    char *t = mpMenuTitleEditSimulant(nullptr);
    return t ? t : "Edit Simulant";
}

/* =========================================================================
 * Head display name builder (cloned from pdgui_menu_agentcreate.cpp:138)
 *
 * There is no mpGetHeadName() API, only catalogMpHeadId() which returns the
 * raw catalog ID string ("head_dark_combat").  We format it the same way
 * agentcreate.cpp does: strip prefix, underscores -> spaces, title-case.
 * ========================================================================= */

static void bot_FormatHeadName(s32 mpheadnum, char *out, size_t outsz)
{
    if (outsz == 0) return;
    out[0] = '\0';

    const char *raw = catalogMpHeadId(mpheadnum);
    if (!raw) {
        snprintf(out, outsz, "Head %d", (int)mpheadnum);
        return;
    }

    static const char k_prefix[] = "head_";
    const size_t prefixLen = sizeof(k_prefix) - 1;
    const char *src = raw;
    if (strncmp(src, k_prefix, prefixLen) == 0) {
        src += prefixLen;
    }

    bool capitalize = true;
    size_t i = 0;
    while (*src && i + 1 < outsz) {
        unsigned char c = (unsigned char)*src++;
        if (c == '_') {
            out[i++] = ' ';
            capitalize = true;
        } else if (capitalize) {
            out[i++] = (char)toupper(c);
            capitalize = false;
        } else {
            out[i++] = (char)tolower(c);
        }
    }
    out[i] = '\0';
}

/* =========================================================================
 * Window-frame helpers (match the shape used by mpsetup.cpp / cheats.cpp)
 * ========================================================================= */

struct WindowFrame {
    float mw;
    float mh;
    ImVec2 pos;
};

/* S300: s_BotSetupPushedCtx removed — menu pool owns the ctx for
 * MENU_TYPE_MP_BOT_SETUP via menupoolAcquireDialog / menupoolReleaseDialog. */

static WindowFrame bs_BeginStandardWindow(const char *imguiId, const char *title,
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

static void bs_CloseCurrentDialog(void)
{
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
    /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
    menuPopDialog();
}

/* True if this frame saw Escape or gamepad-B (the universal back button). */
static bool bs_BackPressed(void)
{
    return !ImGui::IsWindowAppearing() &&
           ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

/* =========================================================================
 * Inline content: Simulants roster body (public -- used by modal wrapper
 * below and by pdgui_menu_room.cpp's inline integration)
 * =========================================================================
 *
 * 8 slot rows + Add Simulant + Clear All.  Each slot row shows either
 * "i+1: <botname>" (when occupied) or "i+1: (empty)" (when free).
 * Clicking a slot runs menuhandlerMpSimulantSlot::SET which sets
 * slotindex and pushes the Add or Edit dialog depending on participant
 * state (these drill-downs are naturally modal single-task screens).
 *
 * Does NOT draw a window frame or action bar -- callers own their
 * surrounding chrome.  Passing bodyHeight=0 uses the remaining content
 * region height inside the current window.
 */

extern "C" void pdguiBotSetupDrawSimulantsBody(float bodyHeight)
{
    if (bodyHeight <= 0.0f) {
        bodyHeight = ImGui::GetContentRegionAvail().y;
    }

    if (ImGui::BeginChild("##bs_sim_body", ImVec2(0, bodyHeight), false,
                          ImGuiWindowFlags_NoBackground)) {

        /* Add Simulant... */
        bool addDisabled = plain_IsDisabled(menuhandlerMpAddSimulant, 0);
        if (addDisabled) ImGui::BeginDisabled();
        if (ImGui::Selectable("Add Simulant...", false)) {
            plain_Set(menuhandlerMpAddSimulant, 0);
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            /* Handler already pushed g_MpAddSimulantMenuDialog. */
        }
        if (addDisabled) ImGui::EndDisabled();

        ImGui::Separator();

        /* 8 slot rows */
        for (s32 i = 0; i < 8; i++) {
            if (plain_IsHidden(menuhandlerMpSimulantSlot, (u8)i)) {
                continue;
            }
            bool rowDisabled = plain_IsDisabled(menuhandlerMpSimulantSlot, (u8)i);

            const char *name = bot_GetSlotName(i);
            bool occupied = (name && name[0] != '\0') &&
                            mpIsParticipantActive(i + BOT_SLOT_OFFSET);

            char label[96];
            if (occupied) {
                snprintf(label, sizeof(label), "%d: %s", i + 1, name);
            } else {
                snprintf(label, sizeof(label), "%d: (empty)", i + 1);
            }

            ImGui::PushID(i);
            if (rowDisabled) ImGui::BeginDisabled();
            if (ImGui::Selectable(label, false)) {
                plain_Set(menuhandlerMpSimulantSlot, (u8)i);
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                /* Handler pushes Add or Edit dialog depending on slot state. */
            }
            if (rowDisabled) ImGui::EndDisabled();
            ImGui::PopID();
        }

        ImGui::Separator();

        /* Clear All */
        if (ImGui::Selectable("Clear All", false)) {
            plain_Set(menuhandlerMpClearAllSimulants, 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
    }
    ImGui::EndChild();
}

/* =========================================================================
 * Renderer: Simulants roster modal wrapper (g_MpSimulantsMenuDialog)
 * =========================================================================
 *
 * Thin wrapper that hosts pdguiBotSetupDrawSimulantsBody inside the
 * standard PD-styled modal frame.  Preserved so the legacy Combat
 * Simulator menu path (which push-dialogs g_MpSimulantsMenuDialog)
 * continues to render correctly -- satisfies "don't break linking" and
 * "zero function loss" for the legacy push path even though the
 * primary room.cpp entry point is the inline panel.
 */

static s32 renderMpSimulants(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = bs_BeginStandardWindow("##bs_simulants", "Simulants", 0.50f, 0.72f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (bs_BackPressed()) {
        bs_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    pdguiBotSetupDrawSimulantsBody(bodyH);

    if (pdguiBeginActionBar("##bs_sim_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            bs_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: Add / Change Simulant (shared items, different title)
 * =========================================================================
 *
 * Same g_MpAddChangeSimulantMenuItems for both dialogs.  Legacy dialog def
 * has MENUDIALOGFLAG_CLOSEONSELECT -- picking a profile closes the dialog.
 * The SET handler's behaviour depends on slotindex: -1 means "create"
 * (set by menuhandlerMpAddSimulant), non-negative means "change profile".
 *
 * We dispatch on a variant enum purely for window title + imgui id.
 */

enum SimDialogVariant {
    SIM_VAR_ADD,
    SIM_VAR_CHANGE,
};

static s32 renderMpAddChangeSimulantImpl(SimDialogVariant variant,
                                          const struct menudialogdef *def)
{
    const char *title   = (variant == SIM_VAR_ADD) ? "Add Simulant"  : "Change Simulant";
    const char *imguiId = (variant == SIM_VAR_ADD) ? "##bs_addsim"   : "##bs_chgsim";

    WindowFrame wf = bs_BeginStandardWindow(imguiId, title, 0.60f, 0.78f, def);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (bs_BackPressed()) {
        bs_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    /* Reserve the bottom ~28% of the body for the description block. */
    float descH = bodyH * 0.28f;
    float listH = bodyH - descH - ImGui::GetStyle().ItemSpacing.y * 2.0f;
    if (listH < pdguiScale(60.0f)) {
        listH = pdguiScale(60.0f);
    }

    s32 selected = list_GetSelectedIndex(mpAddChangeSimulantMenuHandler, 0);
    s32 count    = list_GetOptionCount(mpAddChangeSimulantMenuHandler, 0);
    s32 numGroups = list_GetOptGroupCount(mpAddChangeSimulantMenuHandler, 0);

    /* Build a list of group boundaries so we can insert header rows. */
    s32 groupStart[8] = {0};
    if (numGroups > 8) numGroups = 8;
    for (s32 g = 0; g < numGroups; g++) {
        groupStart[g] = list_GetGroupStartIndex(mpAddChangeSimulantMenuHandler, 0, g);
    }

    if (ImGui::BeginChild("##bs_addsim_body", ImVec2(0, listH), false,
                          ImGuiWindowFlags_NoBackground)) {
        s32 curGroup = 0;
        for (s32 i = 0; i < count; i++) {
            /* Emit group header(s) before this index if we have reached
             * (or passed) the next group's start.  Using a while-loop
             * covers the case where a group has zero rows (skip). */
            while (curGroup < numGroups &&
                   i == groupStart[curGroup]) {
                const char *gName = list_GetOptGroupText(mpAddChangeSimulantMenuHandler, 0, curGroup);
                if (!gName) gName = "";
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.85f, 1.0f, 1.0f));
                ImGui::Selectable(gName, false, ImGuiSelectableFlags_Disabled);
                ImGui::PopStyleColor();
                curGroup++;
            }

            const char *text = list_GetOptionText(mpAddChangeSimulantMenuHandler, 0, i);
            if (!text) text = "";

            ImGui::PushID(i);
            bool isSelected = (i == selected);
            if (ImGui::Selectable(text, isSelected, ImGuiSelectableFlags_None)) {
                list_Set(mpAddChangeSimulantMenuHandler, 0, i);
                pdguiPlaySound(PDGUI_SND_SELECT);
                ImGui::PopID();
                /* Legacy MENUDIALOGFLAG_CLOSEONSELECT -- close explicitly to
                 * avoid relying on runtime-side auto-close in hot-swap path. */
                bs_CloseCurrentDialog();
                break;
            }
            if (ImGui::IsItemHovered()) {
                /* Drive description update via LISTITEMFOCUS -- legacy
                 * writes g_Menus[0].mpsetup.unke24 from this opcode. */
                list_ListItemFocus(mpAddChangeSimulantMenuHandler, 0, i);
            }
            ImGui::PopID();
        }

        /* Any trailing empty groups (defensive). */
        while (curGroup < numGroups) {
            const char *gName = list_GetOptGroupText(mpAddChangeSimulantMenuHandler, 0, curGroup);
            if (gName && gName[0]) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.85f, 1.0f, 1.0f));
                ImGui::Selectable(gName, false, ImGuiSelectableFlags_Disabled);
                ImGui::PopStyleColor();
            }
            curGroup++;
        }
    }
    ImGui::EndChild();

    /* Description block (static wrapped text -- replaces legacy marquee). */
    if (ImGui::BeginChild("##bs_addsim_desc", ImVec2(0, descH), false,
                          ImGuiWindowFlags_NoBackground)) {
        ImGui::Separator();
        const char *desc = bot_GetProfileDescription();
        if (desc && desc[0]) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##bs_addsim_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            bs_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

static s32 renderMpAddSimulant(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpAddChangeSimulantImpl(SIM_VAR_ADD,
                                          menupoolDialogDef(dialog));
}

static s32 renderMpChangeSimulant(struct menudialog *dialog, struct menu *, s32, s32)
{
    return renderMpAddChangeSimulantImpl(SIM_VAR_CHANGE,
                                          menupoolDialogDef(dialog));
}

/* =========================================================================
 * Renderer: Edit Simulant (g_MpEditSimulantMenuDialog)
 * =========================================================================
 *
 * Per-bot detail screen.  Dynamic window title (bot name) via
 * mpMenuTitleEditSimulant.  Items: Difficulty dropdown, Change Type,
 * Character, separator, Copy, Delete, Back.
 *
 * Copy and Delete handlers pop the Edit dialog automatically.  Change Type
 * handler pushes g_MpChangeSimulantMenuDialog.  Character is wired up to
 * menuPushDialog(&g_MpSimulantCharacterMenuDialog) directly (legacy used
 * SELECTABLE_OPENSDIALOG with dialogdef in param3; we do the push in C++).
 */

static s32 renderMpEditSimulant(struct menudialog *dialog, struct menu *, s32, s32)
{
    const char *title = bot_GetEditTitle();
    WindowFrame wf = bs_BeginStandardWindow("##bs_editsim", title, 0.52f, 0.62f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (bs_BackPressed()) {
        bs_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##bs_editsim_body", ImVec2(0, bodyH), false,
                          ImGuiWindowFlags_NoBackground)) {

        /* Difficulty dropdown */
        {
            s32 cur  = dd_GetSelectedIndex(mpBotDifficultyMenuHandler, 0);
            s32 nOpt = dd_GetOptionCount   (mpBotDifficultyMenuHandler, 0);

            const char *curLabel = dd_GetOptionText(mpBotDifficultyMenuHandler, 0, cur);
            if (!curLabel) curLabel = "";

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Difficulty:");
            ImGui::SameLine();
            ImGui::PushID("##bs_diff");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##bs_diff_cb", curLabel)) {
                for (s32 i = 0; i < nOpt; i++) {
                    const char *t = dd_GetOptionText(mpBotDifficultyMenuHandler, 0, i);
                    if (!t) t = "";
                    bool sel = (i == cur);
                    if (ImGui::Selectable(t, sel)) {
                        dd_Set(mpBotDifficultyMenuHandler, 0, i);
                        pdguiPlaySound(PDGUI_SND_SELECT);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }

        ImGui::Spacing();

        /* Change Type... */
        if (ImGui::Selectable("Change Type...", false)) {
            plain_Set(menuhandlerMpChangeSimulantType, 0);
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            /* Handler pushes g_MpChangeSimulantMenuDialog internally. */
        }

        /* Character... */
        if (ImGui::Selectable("Character...", false)) {
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            menuPushDialog(&g_MpSimulantCharacterMenuDialog);
        }

        ImGui::Separator();

        /* Copy Simulant -- legacy item[4] is literal text "Copy Simulant\n" */
        {
            bool copyDisabled = plain_IsDisabled(menuhandlerMpCopySimulant, 0);
            if (copyDisabled) ImGui::BeginDisabled();
            if (ImGui::Selectable("Copy Simulant", false)) {
                plain_Set(menuhandlerMpCopySimulant, 0);
                pdguiPlaySound(PDGUI_SND_SELECT);
                /* Handler pops the Edit dialog internally -- do not pop here. */
            }
            if (copyDisabled) ImGui::EndDisabled();
        }

        /* Delete Simulant */
        if (ImGui::Selectable("Delete Simulant", false)) {
            plain_Set(menuhandlerMpDeleteSimulant, 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
            /* Handler pops the Edit dialog internally. */
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##bs_editsim_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            bs_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: Simulant Character (g_MpSimulantCharacterMenuDialog)
 * =========================================================================
 *
 * Two carousels: Head + Body.  mpCharacterHeadMenuHandler reports count via
 * mpGetNumHeads2() through carousel.value; mpCharacterBodyMenuHandler reports
 * mpGetNumBodies().  We render both as dropdowns.
 *
 * Head display names are formatted from catalogMpHeadId (no mpGetHeadName
 * symbol exists -- matches pdgui_menu_agentcreate.cpp formatting).
 * Body display names come from mpGetBodyName (localized).
 *
 * Live 3D preview (B6 polish, 2026-04-11):
 *   - Restored via pdguiModelPreviewDraw (reusable Batch-0 widget that wraps
 *     pdgui_charpreview's FBO render + idle rotation + placeholder fallback).
 *   - Left column: 3D preview panel showing the currently-selected head+body.
 *     Runtime mp_idx from the carousel handlers is resolved to a catalog ID
 *     string via catalogMpHeadId/catalogMpBodyId, which is what the widget
 *     expects (same pattern as pdgui_menu_agentcreate.cpp:432-441).
 *   - Right column: Head + Body dropdowns (unchanged semantics; same legacy
 *     handler calls).  Vertically centered against the preview.
 *   - Legacy MENUOP_11 tick from menudialog0017ccfc still fires via runtime
 *     and writes g_Menus[0].menumodel.newparams; the widget's per-frame
 *     pdguiCharPreviewRequest overwrites that write with the same value, so
 *     no behavioral conflict.
 */

static s32 renderMpSimulantCharacter(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = bs_BeginStandardWindow("##bs_simchar", "Simulant Character", 0.62f, 0.62f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (bs_BackPressed()) {
        bs_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##bs_simchar_body", ImVec2(0, bodyH), false,
                          ImGuiWindowFlags_NoBackground)) {

        /* Pull the current carousel selection once -- both columns use the
         * same values so we keep them consistent across the frame. */
        s32 curHead = car_GetSelectedIndex(menuhandlerMpSimulantHead, 0);
        s32 curBody = car_GetSelectedIndex(menuhandlerMpSimulantBody, 0);
        s32 nHead   = car_GetCount        (menuhandlerMpSimulantHead, 0);
        s32 nBody   = car_GetCount        (menuhandlerMpSimulantBody, 0);

        /* Layout sizes (1080p baseline -- pdguiScale rescales at other DPIs). */
        float previewW = pdguiScale(300.0f);
        float previewH = pdguiScale(340.0f);
        float colGap   = pdguiScale(20.0f);

        /* ------------------------------------------------------------
         * Left column: live 3D preview of currently-selected head+body.
         *
         * pdguiModelPreviewDraw draws via ImDrawList into the current
         * window (not the cursor), so we capture the cursor screen pos,
         * hand those absolute coords to the widget, then reserve ImGui
         * layout space with Dummy so SameLine can place the dropdowns
         * to the right.  Same pattern as pdgui_menu_agentcreate.cpp.
         * ------------------------------------------------------------ */
        {
            const char *headId = catalogMpHeadId(curHead);
            const char *bodyId = catalogMpBodyId(curBody);

            ImVec2 pos = ImGui::GetCursorScreenPos();

            ModelPreviewOpts opts = pdguiModelPreviewDefaultOpts();
            opts.showBodyName = 0;  /* dropdowns already label the selection */
            opts.showHeadName = 0;
            opts.idleRotation = 1;
            opts.idleRotSpeed = 0.4f;
            opts.cornerRadius = pdguiScale(4.0f);

            pdguiModelPreviewDraw(headId, bodyId,
                                   pos.x, pos.y,
                                   previewW, previewH,
                                   &opts);

            ImGui::Dummy(ImVec2(previewW, previewH));
        }

        ImGui::SameLine(0.0f, colGap);

        /* ------------------------------------------------------------
         * Right column: Head + Body dropdowns (vertically centered
         * against the preview for visual balance).
         * ------------------------------------------------------------ */
        ImGui::BeginGroup();
        {
            /* Rough "picker height" estimate for vertical centering:
             * 2 combos + 1 Spacing row.  Using frame height * 2 + padding
             * is close enough -- exact pixel-perfect centering is not
             * worth a second-pass measure for a modal dialog. */
            float approxPickerH = ImGui::GetFrameHeightWithSpacing() * 2.5f;
            float yOffset = (previewH - approxPickerH) * 0.5f;
            if (yOffset < 0.0f) yOffset = 0.0f;
            ImGui::Dummy(ImVec2(0.0f, yOffset));

            /* Head dropdown */
            {
                char curLabel[64];
                bot_FormatHeadName(curHead, curLabel, sizeof(curLabel));

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Head:");
                ImGui::SameLine();
                ImGui::PushID("##bs_head");
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##bs_head_cb", curLabel)) {
                    for (s32 i = 0; i < nHead; i++) {
                        char t[64];
                        bot_FormatHeadName(i, t, sizeof(t));
                        bool sel = (i == curHead);
                        ImGui::PushID(i);
                        if (ImGui::Selectable(t, sel)) {
                            car_Set(menuhandlerMpSimulantHead, 0, i);
                            pdguiPlaySound(PDGUI_SND_SELECT);
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }

            ImGui::Spacing();

            /* Body dropdown */
            {
                char *curLabelRaw = mpGetBodyName((u8)curBody);
                const char *curLabel = curLabelRaw ? curLabelRaw : "???";

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Body:");
                ImGui::SameLine();
                ImGui::PushID("##bs_body");
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##bs_body_cb", curLabel)) {
                    for (s32 i = 0; i < nBody; i++) {
                        char *tRaw = mpGetBodyName((u8)i);
                        const char *t = tRaw ? tRaw : "???";
                        bool sel = (i == curBody);
                        ImGui::PushID(i);
                        if (ImGui::Selectable(t, sel)) {
                            car_Set(menuhandlerMpSimulantBody, 0, i);
                            pdguiPlaySound(PDGUI_SND_SELECT);
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##bs_simchar_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            bs_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" void pdguiMenuBotSetupRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    pdguiHotswapRegister(&g_MpSimulantsMenuDialog,         renderMpSimulants,         "MP Simulants");
    pdguiHotswapRegister(&g_MpAddSimulantMenuDialog,       renderMpAddSimulant,       "MP Add Simulant");
    pdguiHotswapRegister(&g_MpChangeSimulantMenuDialog,    renderMpChangeSimulant,    "MP Change Simulant");
    pdguiHotswapRegister(&g_MpEditSimulantMenuDialog,      renderMpEditSimulant,      "MP Edit Simulant");
    pdguiHotswapRegister(&g_MpSimulantCharacterMenuDialog, renderMpSimulantCharacter, "MP Simulant Character");

    sysLogPrintf(LOG_NOTE, "MENU_IMGUI: Batch 6 Bot Setup registered (5 dialogs)");
}
