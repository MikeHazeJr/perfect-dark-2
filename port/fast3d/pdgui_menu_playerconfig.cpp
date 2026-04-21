/**
 * pdgui_menu_playerconfig.cpp -- ImGui replacement for Player Config & Stats dialogs.
 *
 * D5 Phase 3 Batch 11.
 *
 * Covers 5 legacy menudialogdefs under Player Setup → (Character / Stats /
 * Load Player) and Game Setup → (Load Settings / Load Preset):
 *
 *   g_MpCharacterMenuDialog     -> renderMpCharacter      (live 3D preview + picker)
 *   g_MpPlayerStatsMenuDialog   -> renderMpPlayerStats    (lifetime stats table)
 *   g_MpLoadSettingsMenuDialog  -> renderMpLoadSettings   (saved setups + presets)
 *   g_MpLoadPresetMenuDialog    -> renderMpLoadPreset     (presets only via QuickGo)
 *   g_MpLoadPlayerMenuDialog    -> renderMpLoadPlayer     (saved player file list)
 *
 * Design:
 *   - Follows the s207 shadow-struct call-through pattern (cloned from
 *     s206 in pdgui_menu_mppause.cpp).  Every state mutation routes
 *     through a legacy C handler -- zero function loss.
 *   - Character picker reuses `pdguiModelPreviewDraw` (Batch 0 widget)
 *     for the live 3D preview -- same pattern as renderMpSimulantCharacter
 *     in pdgui_menu_botsetup.cpp after the S207 head-preview polish.
 *     Selection resolved to catalog ID strings via catalogMpBodyId /
 *     catalogMpHeadId, exactly how pdgui_menu_agentcreate.cpp does it.
 *   - Stats dialog is purely read-only: 16 label rows driven by legacy
 *     mpMenuText* dynamic-text functions, 4 medal rows drawn with an
 *     ImGui-native colored circle + count text (legacy mpMedalMenuHandler
 *     MENUOP_RENDER is a GBI path we cannot reuse from ImGui), plus
 *     the legacy USERNAME/PASSWORD Easter egg tail gated on
 *     menuhandlerMpUsernamePassword MENUOP_CHECKHIDDEN.
 *   - Load Settings / Load Preset both show preset+custom groups by
 *     forcing g_Menus[g_MpPlayerNum].mpsetup.showpresets = 1 at open.
 *     Legacy N64 toggled the preset group for screen real-estate;
 *     on PC with ImGui scrolling we always show both.  The separate
 *     Load Preset dialog still exists because its item has param=1
 *     which routes MENUOP_SET through the QuickGo push branch.
 *   - Load Player LIST groups by device (legacy grouping preserved).
 *
 * NETWORK MATCH START/END AUDIT
 *   See context/scratch/D5-P3-batch11-2026-04-11.md for the full audit.
 *   Character selection writes g_PlayerConfigsArray[0].base.{body_id,
 *   head_id, mpbodynum, mpheadnum}.  On a listen server / host, the next
 *   netServerStageStart (net.c:658) / netServerCoopStageStart (net.c:772)
 *   call picks up the fresh base.body_id/head_id via
 *   netClientReadConfig(g_NetLocalClient, 0) before broadcasting
 *   SVC_STAGE_START -- fully wired.  On a remote client, we call
 *   netClientSettingsChanged() on dialog close so the server learns the
 *   fresh character via CLC_SETTINGS before it reaches the match-start
 *   broadcast path.  Same enhancement applied to the Load Player path
 *   which replaces g_PlayerConfigsArray[0] wholesale.
 *
 * What is NOT in this file:
 *   - g_MpPlayerNameMenuDialog (text input, TYPE-FB)
 *   - g_MpSaveSetupNameMenuDialog / g_MpSaveSetupExistsMenuDialog /
 *     g_MpSavePlayerMenuDialog (confirmations, TYPE-FB)
 *   - g_MpControlMenuDialog (Batch 10, 3D-preview NULL-FN)
 *   - g_MpPlayerOptionsMenuDialog (Batch 8, pdgui_menu_mppause.cpp)
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32
 * breaks C++).  Auto-discovered by GLOB_RECURSE for port/fast3d/*.cpp
 * in CMakeLists.txt.  All game-state access that requires types.h
 * knowledge goes through pdgui_bridge.c accessors.
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
#include "pdgui_model_preview.h"  /* Batch 0 reusable model preview widget */
#include "pdgui.h"                /* langSafe */
#include "system.h"
#include "inputctx.h"
#include "menupool.h"

extern "C" {
#include "pdgui_menus.h"  /* for pdguiMenuPlayerConfigRegister declaration */
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

/* ---- Dialog definitions for the 5 Batch 11 dialogs ---- */
extern struct menudialogdef g_MpCharacterMenuDialog;
extern struct menudialogdef g_MpPlayerStatsMenuDialog;
extern struct menudialogdef g_MpLoadSettingsMenuDialog;
extern struct menudialogdef g_MpLoadPresetMenuDialog;
extern struct menudialogdef g_MpLoadPlayerMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* ---- Language ---- */
char *langGet(s32 textid);
/* langSafe comes from pdgui.h */

/* ---- Runtime data accessors (C boundary) ---- */
extern s32 g_MpPlayerNum;

/* ---- Net ---- */
extern s32 g_NetMode;
void netClientSettingsChanged(void);

/* ---- Asset catalog (declared in assetcatalog.h; simple externs so we
 *      don't pull heavy headers that would drag types.h via transitive
 *      includes on some compiler configs). ---- */
const char *catalogMpBodyId(s32 mpbodynum);
const char *catalogMpHeadId(s32 mpheadnum);
char       *mpGetBodyName(u8 mpbodynum);

/* ---- Batch 11 bridge accessors (pdgui_bridge.c) ---- */
void pdguiPcMpSetShowPresets(s32 on);
s32  pdguiPcMpGetShowPresets(void);
s32  pdguiPcPlayerConfigGetTitle(void);
s32  pdguiPcPlayerConfigGetMedalCount(s32 which);

/* ---- Title text (stats dialog) ---- */
char *mpMenuTitleStatsForPlayerName(struct menudialogdef *dialogdef);

/* ---- Stats dynamic text functions (legacy, defined in setup.c) ----
 *
 * All take `struct menuitem *` but their bodies do not dereference the
 * pointer -- they read g_PlayerConfigsArray[g_MpPlayerNum] directly.
 * Safe to call with nullptr.  The exception is
 * mpMenuTextUsernamePassword which reads item->param (0 = username,
 * 1 = password), so we build a shadow menuitem with param set. */
char *mpMenuTextKills           (struct menuitem *item);
char *mpMenuTextDeaths          (struct menuitem *item);
char *mpMenuTextAccuracy        (struct menuitem *item);
char *mpMenuTextHeadShots       (struct menuitem *item);
char *mpMenuTextAmmoUsed        (struct menuitem *item);
char *mpMenuTextDamageDealt     (struct menuitem *item);
char *mpMenuTextPainReceived    (struct menuitem *item);
char *mpMenuTextGamesPlayed     (struct menuitem *item);
char *mpMenuTextGamesWon        (struct menuitem *item);
char *mpMenuTextGamesLost       (struct menuitem *item);
char *mpMenuTextTime            (struct menuitem *item);
char *mpMenuTextDistance        (struct menuitem *item);
char *mpMenuTextMedalAccuracy   (struct menuitem *item);
char *mpMenuTextMedalHeadShot   (struct menuitem *item);
char *mpMenuTextMedalKillMaster (struct menuitem *item);
char *mpMenuTextMedalSurvivor   (struct menuitem *item);
char *mpMenuTextUsernamePassword(struct menuitem *item);

/* mpMenuTextPlayerTitle has a DIFFERENT signature -- takes s32 (ignored)
 * and reads g_PlayerConfigsArray[g_MpPlayerNum].title directly.  See
 * ingame.c:807. */
char *mpMenuTextPlayerTitle(s32 arg0);

/* ---- Load Settings marquee text (setup.c:2570) ---- */
char *mpMenuTextMpconfigMarquee(struct menuitem *item);

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
#define MENUOP_CHECKDISABLED       12
#define MENUOP_LISTITEMFOCUS       16
#define MENUOP_CHECKHIDDEN         24

/* Legacy Easter-egg threshold -- Perfect Dark title unlocks USERNAME/PASSWORD
 * rows at the bottom of the stats dialog.  Source of truth:
 * src/include/constants.h MPPLAYERTITLE_PERFECT.  Mirrored here so we don't
 * need a bridge call for a single constant. */
#define PCP_TITLE_PERFECT 5

/* ---- s207 shadow menuitem / handlerdata ----
 * ABI-compatible with the real types in src/include/types.h:3337..3417.
 * Cloned from the s206 pattern in pdgui_menu_mppause.cpp, with the
 * carousel variant from s204 (botsetup) so we can invoke the
 * MENUITEMTYPE_CAROUSEL handler menuhandlerMpCharacterHead. */
struct s207_handlerdata_carousel { s32 value; u32 unk04; };
struct s207_handlerdata_checkbox { u32 value; };
struct s207_handlerdata_dropdown { uintptr_t value; uintptr_t unk04; };
struct s207_handlerdata_list_t {
    uintptr_t value;           /* union { uintptr_t value; intptr_t values32; } */
    s32       unk04;
    s32       groupstartindex;
    s32       unk0c;
};
struct s207_handlerdata_slider { u32 value; char *label; };

union s207_handlerdata {
    struct s207_handlerdata_carousel carousel;
    struct s207_handlerdata_checkbox checkbox;
    struct s207_handlerdata_dropdown dropdown;
    struct s207_handlerdata_list_t   list;
    struct s207_handlerdata_slider   slider;
    u8 _pad[256];
};

struct s207_menuitem {
    u8        type;
    u8        param;
    u32       flags;
    intptr_t  param2;
    intptr_t  param3;
    uintptr_t (*handler)(s32 op, struct s207_menuitem *, union s207_handlerdata *);
};

/* ---- Legacy item handlers we delegate to (setup.c) ---- */
uintptr_t mpCharacterBodyListHandler   (s32, struct s207_menuitem *, union s207_handlerdata *);
uintptr_t menuhandlerMpCharacterHead   (s32, struct s207_menuitem *, union s207_handlerdata *);
uintptr_t mpLoadSettingsMenuHandler    (s32, struct s207_menuitem *, union s207_handlerdata *);
uintptr_t mpLoadPlayerMenuHandler      (s32, struct s207_menuitem *, union s207_handlerdata *);
uintptr_t menuhandlerMpUsernamePassword(s32, struct s207_menuitem *, union s207_handlerdata *);

} /* extern "C" */

/* =========================================================================
 * Module state
 * ========================================================================= */

static bool s_Registered = false;

/* =========================================================================
 * s207 call-through helpers
 *
 * Each helper builds a local shadow menuitem, invokes the legacy handler
 * with the requested MENUOP_*, and returns the result (or writes back
 * through the handlerdata union).  Identical pattern to s206 in
 * pdgui_menu_mppause.cpp.
 * ========================================================================= */

/* ---- List (body list, saved-setup list, load-player list) ---- */

typedef uintptr_t (*ListFn)(s32, s207_menuitem *, s207_handlerdata *);

static s32 list_GetOptionCount(ListFn fn, u8 param)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.list.value;
}

static const char *list_GetOptionText(ListFn fn, u8 param, s32 idx)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 list_GetSelectedIndex(ListFn fn, u8 param)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    /* Legacy sentinel values: g_PlayerConfigsArray[...].base.mpbodynum for
     * the body list, 0xfffff for Load Settings / Load Player.  Return raw
     * value; the caller clamps for ImGui-safe indexing. */
    return (s32)h.list.value;
}

static void list_Set(ListFn fn, u8 param, s32 idx)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    fn(MENUOP_SET, &it, &h);
}

static void list_Focus(ListFn fn, u8 param, s32 idx)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    fn(MENUOP_LISTITEMFOCUS, &it, &h);
}

static s32 list_GetOptGroupCount(ListFn fn, u8 param)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    fn(MENUOP_GETOPTGROUPCOUNT, &it, &h);
    return (s32)h.list.value;
}

static const char *list_GetOptGroupText(ListFn fn, u8 param, s32 groupIdx)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    h.list.value = (uintptr_t)groupIdx;
    uintptr_t r = fn(MENUOP_GETOPTGROUPTEXT, &it, &h);
    return (const char *)r;
}

static s32 list_GetGroupStartIndex(ListFn fn, u8 param, s32 groupIdx)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    h.list.value = (uintptr_t)groupIdx;
    fn(MENUOP_GETGROUPSTARTINDEX, &it, &h);
    return (s32)h.list.groupstartindex;
}

/* ---- Carousel (character head) ---- */

typedef uintptr_t (*CarFn)(s32, s207_menuitem *, s207_handlerdata *);

static s32 car_GetCount(CarFn fn, u8 param)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return h.carousel.value;
}

static s32 car_GetSelectedIndex(CarFn fn, u8 param)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return h.carousel.value;
}

static void car_Set(CarFn fn, u8 param, s32 idx)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    h.carousel.value = idx;
    fn(MENUOP_SET, &it, &h);
}

/* ---- Plain CHECKHIDDEN (username/password Easter egg gate) ---- */

typedef uintptr_t (*PlainFn)(s32, s207_menuitem *, s207_handlerdata *);

static bool plain_IsHiddenWithParam(PlainFn fn, u8 param)
{
    s207_menuitem it{};
    it.param = param;
    s207_handlerdata h{};
    uintptr_t r = fn(MENUOP_CHECKHIDDEN, &it, &h);
    return r != 0;
}

/* Invoke a dynamic-text helper that takes `struct menuitem *` with a
 * specific param value.  Used for mpMenuTextUsernamePassword. */
static const char *pc_GetDynTextWithParam(char *(*fn)(struct menuitem *), u8 param)
{
    s207_menuitem shadow{};
    shadow.param = param;
    char *r = fn((struct menuitem *)&shadow);
    return r ? r : "";
}

/* =========================================================================
 * Window-frame helpers (clone of the Batch 8 mppause shape -- kept file-
 * local for the same reason, shared helpers would need a new header).
 * ========================================================================= */

struct WindowFrame {
    float mw;
    float mh;
    ImVec2 pos;
};

/* S300: s_PlayerConfigPushedCtx removed — menu pool owns the ctx for
 * MENU_TYPE_MP_PLAYER_CONFIG via menupoolAcquireDialog / menupoolReleaseDialog. */

static WindowFrame pc_BeginStandardWindow(const char *imguiId, const char *title,
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

static void pc_CloseCurrentDialog(void)
{
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
    /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
    menuPopDialog();
}

static bool pc_BackPressed(void)
{
    return !ImGui::IsWindowAppearing() &&
           ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

/* Notify the networking layer that the local player's settings have
 * changed.  Called after any writer path that mutates
 * g_PlayerConfigsArray[0].base (body_id, head_id, whole struct reload).
 *
 * On a listen server / host this is redundant -- netServerStageStart
 * re-reads g_PlayerConfigsArray[0] via its own netClientReadConfig
 * invocation before broadcasting SVC_STAGE_START (net.c:658).  On a
 * remote client it is REQUIRED: without the notify, the client's
 * settings.body_id / .head_id remain stale until the next
 * cl->settings-touching event, meaning the server's authoritative
 * view of the client's character is out of date.  The modern
 * netmenu.c Character dropdown calls this for the same reason
 * (netmenu.c:179 / :403). */
static void pc_NetNotifyLocalPlayerChanged(void)
{
    if (g_NetMode != 0) {
        netClientSettingsChanged();
    }
}

/* =========================================================================
 * Renderer: MP Character select (g_MpCharacterMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:2919 g_MpCharacterMenuItems):
 *   MENUITEMTYPE_LIST (body)    -> mpCharacterBodyListHandler
 *   MENUITEMTYPE_CAROUSEL (head) -> menuhandlerMpCharacterHead
 *
 * ImGui layout:
 *   Left:  300x340 live 3D preview (pdguiModelPreviewDraw, idle rotation)
 *   Right: Body scrollable list (GETOPTIONCOUNT / GETOPTIONTEXT) + Head
 *          carousel (forward/back buttons reading car_*). Head carousel
 *          auto-locks for bodies with an integrated head (the legacy
 *          handler's CHECKHIDDEN/count-pinning is preserved via the
 *          shadow-struct call).
 *
 * Commit semantics:
 *   - Scroll hover (Selectable focus) -> LISTITEMFOCUS (updates legacy
 *     preview state, still fires idly via the menu runtime TICK path).
 *   - Click / A -> SET (commits via mpchrSetBodyByIndex / mpchrSetHeadByIndex).
 *
 * Network wiring:
 *   pc_NetNotifyLocalPlayerChanged() fires on dialog close so CLC_SETTINGS
 *   propagates the fresh character to the server.  Same enhancement that
 *   netmenu.c:179 / :403 apply to the modern netmenu character dropdowns.
 */

static s32 renderMpCharacter(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = pc_BeginStandardWindow("##pc_char", "Character", 0.68f, 0.70f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (pc_BackPressed()) {
        pc_NetNotifyLocalPlayerChanged();
        pc_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pc_char_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* Current committed selection (read via legacy handlers so we match
         * the preview state the legacy menu runtime sees for its own
         * MENUOP_11 preview-tick side effects). */
        s32 numBodies = list_GetOptionCount(mpCharacterBodyListHandler, 0);
        s32 curBody   = list_GetSelectedIndex(mpCharacterBodyListHandler, 0);
        if (curBody < 0 || curBody >= numBodies) curBody = 0;

        s32 numHeads  = car_GetCount(menuhandlerMpCharacterHead, 0);
        s32 curHead   = car_GetSelectedIndex(menuhandlerMpCharacterHead, 0);
        if (curHead < 0) curHead = 0;
        if (numHeads <= 0) numHeads = 1;
        if (curHead >= numHeads) curHead = numHeads - 1;

        float previewW = pdguiScale(300.0f);
        float previewH = pdguiScale(340.0f);
        float colGap   = pdguiScale(20.0f);

        /* ------------------------------------------------------------
         * Left column: live 3D preview.
         * ------------------------------------------------------------ */
        {
            const char *headId = catalogMpHeadId(curHead);
            const char *bodyId = catalogMpBodyId(curBody);

            ImVec2 pos = ImGui::GetCursorScreenPos();

            ModelPreviewOpts opts = pdguiModelPreviewDefaultOpts();
            opts.showBodyName = 0;
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
         * Right column: scrollable body list + head carousel.
         * ------------------------------------------------------------ */
        ImGui::BeginGroup();
        {
            float rightW = ImGui::GetContentRegionAvail().x;

            /* Body list ----------------------------------------------- */
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Body");
            ImGui::Spacing();

            float listH = previewH - pdguiScale(120.0f);
            if (listH < pdguiScale(120.0f)) listH = pdguiScale(120.0f);

            if (ImGui::BeginChild("##pc_char_body_list",
                                  ImVec2(rightW, listH), true,
                                  ImGuiWindowFlags_NoBackground)) {
                for (s32 i = 0; i < numBodies; i++) {
                    const char *t = list_GetOptionText(mpCharacterBodyListHandler, 0, i);
                    if (!t) t = "???";
                    ImGui::PushID(i);
                    bool isSel = (i == curBody);
                    if (ImGui::Selectable(t, isSel, 0,
                                          ImVec2(0, pdguiScale(22.0f)))) {
                        /* Click -> legacy commit (writes
                         * g_PlayerConfigsArray[g_MpPlayerNum].base.body_id
                         * + mpbodynum + auto-picks default head). */
                        list_Set(mpCharacterBodyListHandler, 0, i);
                        pdguiPlaySound(PDGUI_SND_SELECT);
                    } else if (ImGui::IsItemHovered()) {
                        /* Hover -> legacy focus preview (writes
                         * s_PreviewBodyNum / s_PreviewHeadNum via the
                         * LISTITEMFOCUS branch).  Preview widget itself
                         * reads the CURRENT committed selection, so the
                         * focus path is only kept here to preserve the
                         * legacy s_PreviewBodyNum tracker behavior for
                         * any code that still inspects it. */
                        list_Focus(mpCharacterBodyListHandler, 0, i);
                    }
                    if (isSel) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            ImGui::Spacing();

            /* Head carousel ------------------------------------------- */
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Head");
            ImGui::SameLine();

            /* The head carousel MENUOP_SET accepts any valid index.  For
             * bodies with an integrated head the legacy handler clamps
             * the count to 1 and ignores SET, which is a no-op here too. */
            bool canCycle = (numHeads > 1);

            ImGui::BeginDisabled(!canCycle);
            if (ImGui::ArrowButton("##pc_char_head_prev", ImGuiDir_Left)) {
                s32 next = (curHead - 1 + numHeads) % numHeads;
                car_Set(menuhandlerMpCharacterHead, 0, next);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            ImGui::SameLine();

            /* Show a numeric label + catalog ID hint -- heads have no
             * localized display name (the N64 UI used a 3D preview and
             * carousel arrows only).  The catalog ID is the most
             * meaningful thing we can surface in text. */
            char headLbl[96];
            const char *curHeadId = catalogMpHeadId(curHead);
            if (curHeadId && curHeadId[0]) {
                snprintf(headLbl, sizeof(headLbl), "%d / %d  (%s)",
                         (int)(curHead + 1), (int)numHeads, curHeadId);
            } else {
                snprintf(headLbl, sizeof(headLbl), "%d / %d",
                         (int)(curHead + 1), (int)numHeads);
            }
            ImGui::TextUnformatted(headLbl);

            ImGui::SameLine();
            if (ImGui::ArrowButton("##pc_char_head_next", ImGuiDir_Right)) {
                s32 next = (curHead + 1) % numHeads;
                car_Set(menuhandlerMpCharacterHead, 0, next);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            ImGui::EndDisabled();
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##pc_char_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            pc_NetNotifyLocalPlayerChanged();
            pc_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Player Stats (g_MpPlayerStatsMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:2065 g_MpPlayerStatsMenuItems):
 *   13 LABEL rows with dynamic text (mpMenuText*) for core stats
 *   + 4 MEDAL rows (LIST_CUSTOMRENDER rendering colored icons via
 *     mpMedalMenuHandler -- GBI path we cannot reuse; we draw our own
 *     colored circle + count in ImGui instead)
 *   + "Your Title" row (mpMenuTextPlayerTitle -- different signature)
 *   + USERNAME/PASSWORD Easter egg rows (gated by
 *     menuhandlerMpUsernamePassword CHECKHIDDEN on Perfect title)
 *
 * No writes.  Pure read-only display over g_PlayerConfigsArray[g_MpPlayerNum]
 * lifetime stats.  Title: "Stats for <name>" from mpMenuTitleStatsForPlayerName.
 */

struct StatRow {
    const char *label;
    char *(*textFn)(struct menuitem *);
};

struct MedalRow {
    const char *label;
    s32         index;    /* bridge index: 0=KM, 1=HS, 2=Acc, 3=Surv */
    u32         colorRGB; /* icon color (matches legacy mpMedalMenuHandler) */
};

static void pc_DrawStatRow(const char *label, const char *value)
{
    /* Label on left, value right-aligned.  A plain two-column line,
     * rendered without ImGui tables so the stats dialog maintains a
     * "menu"-style rhythm rather than a spreadsheet look. */
    float rowH = pdguiScale(22.0f);
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    float  avail    = ImGui::GetContentRegionAvail().x;

    ImGui::Dummy(ImVec2(avail, rowH));

    float ty = startPos.y + rowH * 0.5f - ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(startPos.x + pdguiScale(8.0f), ty),
        ImGui::GetColorU32(ImGuiCol_Text), label);

    if (value && value[0]) {
        float tw = ImGui::CalcTextSize(value).x;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(startPos.x + avail - tw - pdguiScale(8.0f), ty),
            ImGui::GetColorU32(ImGuiCol_Text), value);
    }
}

static void pc_DrawMedalRow(const char *label, u32 colorRGB, s32 count)
{
    float rowH = pdguiScale(24.0f);
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    float  avail    = ImGui::GetContentRegionAvail().x;

    ImGui::Dummy(ImVec2(avail, rowH));

    float ty = startPos.y + rowH * 0.5f - ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(startPos.x + pdguiScale(8.0f), ty),
        ImGui::GetColorU32(ImGuiCol_Text), label);

    /* Draw a colored medal circle right of label text.  ImU32 is ABGR
     * at 0xAABBGGRR byte order; the colorRGB param is RGB with A=0xFF
     * assumed.  IM_COL32 handles the repack. */
    u8 r = (u8)((colorRGB >> 16) & 0xff);
    u8 g = (u8)((colorRGB >>  8) & 0xff);
    u8 b = (u8)(colorRGB         & 0xff);

    float cx = startPos.x + avail - pdguiScale(52.0f);
    float cy = startPos.y + rowH * 0.5f;
    float rad = pdguiScale(8.0f);
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(cx, cy), rad, IM_COL32(r, g, b, 0xff), 20);
    ImGui::GetWindowDrawList()->AddCircle(
        ImVec2(cx, cy), rad, IM_COL32(255, 255, 255, 160), 20, 1.5f);

    /* Count text right-aligned */
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)count);
    float tw = ImGui::CalcTextSize(buf).x;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(startPos.x + avail - tw - pdguiScale(8.0f), ty),
        ImGui::GetColorU32(ImGuiCol_Text), buf);
}

/* Strip trailing '\n' from legacy dynamic-text buffers that stat helpers
 * sometimes append so the ImGui rows don't break onto a second line. */
static const char *pc_Strip(const char *s, char *buf, size_t bufsz)
{
    if (!s) return "";
    size_t len = strnlen(s, bufsz - 1);
    if (len == 0) return "";
    if (len >= bufsz) len = bufsz - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return buf;
}

static s32 renderMpPlayerStats(struct menudialog *dialog, struct menu *, s32, s32)
{
    const char *title = mpMenuTitleStatsForPlayerName(nullptr);
    char titleBuf[128];
    const char *titleSafe = pc_Strip(title, titleBuf, sizeof(titleBuf));
    if (!titleSafe[0]) titleSafe = "Statistics";

    WindowFrame wf = pc_BeginStandardWindow("##pc_stats", titleSafe, 0.55f, 0.80f,
                                            menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (pc_BackPressed()) {
        pc_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pc_stats_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        static const StatRow combatRows[] = {
            { "Kills",       mpMenuTextKills       },
            { "Deaths",      mpMenuTextDeaths      },
            { "Accuracy",    mpMenuTextAccuracy    },
            { "Head Shots",  mpMenuTextHeadShots   },
        };
        static const StatRow resourceRows[] = {
            { "Ammo Used",    mpMenuTextAmmoUsed    },
            { "Damage Dealt", mpMenuTextDamageDealt },
            { "Pain Received",mpMenuTextPainReceived},
        };
        static const StatRow careerRows[] = {
            { "Games Played", mpMenuTextGamesPlayed },
            { "Games Won",    mpMenuTextGamesWon    },
            { "Games Lost",   mpMenuTextGamesLost   },
            { "Time",         mpMenuTextTime        },
            { "Distance",     mpMenuTextDistance    },
        };

        /* --- Combat Section --- */
        for (s32 i = 0; i < (s32)(sizeof(combatRows) / sizeof(combatRows[0])); i++) {
            char buf[128];
            const char *v = pc_Strip(combatRows[i].textFn(nullptr), buf, sizeof(buf));
            pc_DrawStatRow(combatRows[i].label, v);
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        /* --- Resource Section --- */
        for (s32 i = 0; i < (s32)(sizeof(resourceRows) / sizeof(resourceRows[0])); i++) {
            char buf[128];
            const char *v = pc_Strip(resourceRows[i].textFn(nullptr), buf, sizeof(buf));
            pc_DrawStatRow(resourceRows[i].label, v);
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        /* --- Career Section --- */
        for (s32 i = 0; i < (s32)(sizeof(careerRows) / sizeof(careerRows[0])); i++) {
            char buf[128];
            const char *v = pc_Strip(careerRows[i].textFn(nullptr), buf, sizeof(buf));
            pc_DrawStatRow(careerRows[i].label, v);
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        /* --- Medals Section ---
         * Legacy colors (mpMedalMenuHandler):
         *   KillMaster 0xff7f7f -> red
         *   Head Shot  0xbfbf00 -> yellow
         *   Accuracy   0x00ff00 -> green
         *   Survivor   0x00bfbf -> cyan */
        ImGui::TextDisabled("Medals Won");
        ImGui::Dummy(ImVec2(0, pdguiScale(2.0f)));

        static const MedalRow medals[] = {
            { "Accuracy",  2, 0x00ff00 }, /* green  */
            { "Head Shot", 1, 0xbfbf00 }, /* yellow */
            { "KillMaster",0, 0xff7f7f }, /* red    */
            { "Survivor",  3, 0x00bfbf }, /* cyan   */
        };
        for (s32 i = 0; i < (s32)(sizeof(medals) / sizeof(medals[0])); i++) {
            s32 count = pdguiPcPlayerConfigGetMedalCount(medals[i].index);
            pc_DrawMedalRow(medals[i].label, medals[i].colorRGB, count);
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));

        /* --- Title --- */
        {
            char buf[128];
            const char *v = pc_Strip(mpMenuTextPlayerTitle(0), buf, sizeof(buf));
            pc_DrawStatRow("Your Title", v);
        }

        /* --- USERNAME/PASSWORD Easter egg rows ---
         * Legacy gates these rows on menuhandlerMpUsernamePassword
         * CHECKHIDDEN which returns true unless the player has earned
         * the MPPLAYERTITLE_PERFECT title.  We delegate the gate so
         * any future tweak to the unlock condition continues to work. */
        bool hideUsername = plain_IsHiddenWithParam(menuhandlerMpUsernamePassword, 0);
        if (!hideUsername) {
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
            ImGui::TextDisabled("Unlock Code");
            ImGui::Dummy(ImVec2(0, pdguiScale(2.0f)));

            const char *user = pc_GetDynTextWithParam(mpMenuTextUsernamePassword, 0);
            const char *pass = pc_GetDynTextWithParam(mpMenuTextUsernamePassword, 1);

            char userBuf[128], passBuf[128];
            pc_DrawStatRow("Username", pc_Strip(user, userBuf, sizeof(userBuf)));
            pc_DrawStatRow("Password", pc_Strip(pass, passBuf, sizeof(passBuf)));
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##pc_stats_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            pc_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Shared LIST-with-groups renderer (Load Settings / Load Preset / Load Player)
 * =========================================================================
 *
 * Load Settings / Load Preset / Load Player all share the same shape: a
 * scrollable list grouped by headers (presets / custom / device), plus
 * an optional bottom marquee (mpMenuTextMpconfigMarquee for the load
 * settings variants).  We factor the group-render out into a helper
 * that takes the legacy handler function pointer and a `param` byte.
 * Returns true if the user clicked a row (in which case the caller
 * should optionally close the dialog or do follow-up actions).
 */

struct GroupedListResult {
    bool  clicked;
    s32   clickedGlobalIdx;
};

static GroupedListResult pc_RenderGroupedList(const char *childId,
                                               ListFn fn, u8 param,
                                               float listH,
                                               s32 *focusedIdxIO)
{
    GroupedListResult res = { false, -1 };

    s32 totalCount = list_GetOptionCount(fn, param);
    s32 groupCount = list_GetOptGroupCount(fn, param);
    if (groupCount < 1) groupCount = 1;

    s32 focused = focusedIdxIO ? *focusedIdxIO : -1;

    if (ImGui::BeginChild(childId, ImVec2(0, listH), true,
                          ImGuiWindowFlags_NoBackground)) {
        for (s32 g = 0; g < groupCount; g++) {
            const char *groupName = list_GetOptGroupText(fn, param, g);
            if (groupName && groupName[0]) {
                ImGui::TextDisabled("%s", groupName);
                ImGui::Dummy(ImVec2(0, pdguiScale(2.0f)));
            }

            /* Group end = next group's start, or total for last group. */
            s32 groupStart = list_GetGroupStartIndex(fn, param, g);
            s32 groupEnd   = (g + 1 < groupCount)
                ? list_GetGroupStartIndex(fn, param, g + 1)
                : totalCount;
            if (groupStart < 0) groupStart = 0;
            if (groupEnd > totalCount) groupEnd = totalCount;

            for (s32 i = groupStart; i < groupEnd; i++) {
                const char *t = list_GetOptionText(fn, param, i);
                if (!t) t = "???";
                ImGui::PushID(i);
                bool isSel = (i == focused);
                if (ImGui::Selectable(t, isSel, 0,
                                      ImVec2(0, pdguiScale(22.0f)))) {
                    res.clicked = true;
                    res.clickedGlobalIdx = i;
                }
                if (ImGui::IsItemHovered()) {
                    if (focused != i) {
                        focused = i;
                        /* Tell the legacy handler which row has focus
                         * so its backing state (e.g., mpsetup.slotindex
                         * for the marquee overview) matches. */
                        list_Focus(fn, param, i);
                    }
                }
                if (isSel) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        }

        if (totalCount <= 0) {
            ImGui::TextDisabled("(no entries)");
        }
    }
    ImGui::EndChild();

    if (focusedIdxIO) *focusedIdxIO = focused;
    return res;
}

/* =========================================================================
 * Renderer: MP Load Settings (g_MpLoadSettingsMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:2975 g_MpLoadSettingsMenuItems):
 *   LIST (param=0) -> mpLoadSettingsMenuHandler (grouped: presets+custom)
 *   MARQUEE       -> mpMenuTextMpconfigMarquee (overview of focused row)
 *   SEPARATOR
 *   LABEL         -> "Menu Alt: Toggle Presets\n" (N64 toggle hint, obsolete)
 *
 * Dialog handler: mpLoadSettingsDialogHandler toggles
 *   g_Menus[g_MpPlayerNum].mpsetup.showpresets on Menu-Alt TICK.  The TICK
 *   still fires via the menu runtime because hot-swap only intercepts
 *   RENDER, so legacy toggle behavior is preserved for anything that
 *   still reads showpresets.  We force it to 1 at first frame so both
 *   groups always appear in the ImGui list (screen-real-estate is no
 *   longer a concern on PC).
 *
 * Network: mpsetupLoadSetup / mp0f18dec4 write g_MpSetup which flows
 * through the existing room/lobby broadcast pipeline -- no batch-specific
 * net surface.  See scratch doc row 6 for full audit.
 */

static s32 s_LoadSettingsFocus = -1;

static s32 renderMpLoadSettings(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = pc_BeginStandardWindow("##pc_load_settings",
                                             "Load Game Settings", 0.58f, 0.72f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ImGui::IsWindowAppearing()) {
        /* Always show presets + custom on PC -- no toggle UX needed.
         * Legacy N64 toggled showpresets for screen-real-estate; PC has
         * scroll so both groups fit comfortably. */
        pdguiPcMpSetShowPresets(1);
        s_LoadSettingsFocus = -1;
    }

    if (pc_BackPressed()) {
        pc_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pc_load_settings_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        float listH = bodyH - pdguiScale(80.0f);
        if (listH < pdguiScale(120.0f)) listH = pdguiScale(120.0f);

        GroupedListResult r = pc_RenderGroupedList("##pc_load_settings_list",
                                                    mpLoadSettingsMenuHandler,
                                                    /*param=*/0,
                                                    listH,
                                                    &s_LoadSettingsFocus);

        if (r.clicked && r.clickedGlobalIdx >= 0) {
            /* Legacy SET runs mpCloseDialogsForNewSetup() +
             * mpsetupLoadSetup() (or mp0f18dec4 for a preset).  Param=0
             * does NOT push QuickGo -- that's what the separate Load
             * Preset dialog (param=1) is for. */
            list_Set(mpLoadSettingsMenuHandler, 0, r.clickedGlobalIdx);
            pdguiPlaySound(PDGUI_SND_SELECT);
            /* The legacy MENUDIALOGFLAG_CLOSEONSELECT on the dialog def
             * means the menu runtime will close the dialog when SET
             * fires -- we mirror that by closing ourselves so ImGui
             * state releases cleanly. */
            pc_CloseCurrentDialog();
            ImGui::EndChild();
            ImGui::End();
            return 1;
        }

        /* Marquee row: overview of currently focused setup.  The legacy
         * body reads g_Menus[g_MpPlayerNum].mpsetup.slotindex which the
         * LISTITEMFOCUS branch maintains; we just forward the same call
         * each hover and the marquee text reflects it. */
        const char *marquee = mpMenuTextMpconfigMarquee(nullptr);
        if (marquee && marquee[0]) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", marquee);
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##pc_load_settings_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            pc_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Load Preset (g_MpLoadPresetMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:3020 g_MpLoadPresetMenuItems):
 *   LIST (param=1) -> mpLoadSettingsMenuHandler
 *   MARQUEE        -> mpMenuTextMpconfigMarquee
 *
 * Identical LIST handler with param=1.  The SET branch in
 * mpLoadSettingsMenuHandler checks item->param == 1 and pushes QuickGo
 * after the load (the whole point of the separate dialog: Quick Go flow
 * loads a preset and immediately transitions to the MpQuickGo screen).
 *
 * ImGui shape: same grouped list as Load Settings but with param=1 so
 * SET routes through the QuickGo branch.  We force showpresets=1 so
 * unlocked preset slots appear regardless of legacy toggle state.
 */

static s32 s_LoadPresetFocus = -1;

static s32 renderMpLoadPreset(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = pc_BeginStandardWindow("##pc_load_preset",
                                             "Load Game Settings", 0.58f, 0.72f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ImGui::IsWindowAppearing()) {
        pdguiPcMpSetShowPresets(1);
        s_LoadPresetFocus = -1;
    }

    if (pc_BackPressed()) {
        pc_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pc_load_preset_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        float listH = bodyH - pdguiScale(80.0f);
        if (listH < pdguiScale(120.0f)) listH = pdguiScale(120.0f);

        GroupedListResult r = pc_RenderGroupedList("##pc_load_preset_list",
                                                    mpLoadSettingsMenuHandler,
                                                    /*param=*/1,
                                                    listH,
                                                    &s_LoadPresetFocus);

        if (r.clicked && r.clickedGlobalIdx >= 0) {
            list_Set(mpLoadSettingsMenuHandler, 1, r.clickedGlobalIdx);
            pdguiPlaySound(PDGUI_SND_SELECT);
            /* Legacy dialog has no CLOSEONSELECT flag, but the SET branch
             * for param=1 calls func0f0f820c(&g_MpQuickGoMenuDialog,...)
             * which replaces the whole menu root.  We pop our local
             * input context to match. */
            pc_CloseCurrentDialog();
            ImGui::EndChild();
            ImGui::End();
            return 1;
        }

        const char *marquee = mpMenuTextMpconfigMarquee(nullptr);
        if (marquee && marquee[0]) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", marquee);
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##pc_load_preset_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            pc_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Load Player (g_MpLoadPlayerMenuDialog)
 * =========================================================================
 *
 * Legacy layout (setup.c:3049 g_MpLoadPlayerMenuItems):
 *   LIST  -> mpLoadPlayerMenuHandler (grouped by device from g_FileLists[0])
 *   LABEL -> "B Button to cancel"   (hint, replaced by action-bar Back)
 *
 * SET branch:
 *   1. Find the file's (fileid, deviceserial) pair.
 *   2. If another active player slot already has it, push the
 *      "already loaded" error dialog.
 *   3. Otherwise: menuPopDialog + filemgrSaveOrLoad(FILEOP_LOAD_MPPLAYER,
 *      g_MpPlayerNum) -- this replaces g_PlayerConfigsArray[g_MpPlayerNum]
 *      with the on-disk player file contents.
 *
 * Network wiring: same as g_MpCharacterMenuDialog -- the legacy path
 * does NOT call netClientSettingsChanged(), so we do it ourselves on
 * close.  Without this, a remote client that loads a player file
 * mid-lobby would have a stale body_id/head_id on the server side
 * until the next settings-touching action.
 */

static s32 s_LoadPlayerFocus = -1;

static s32 renderMpLoadPlayer(struct menudialog *dialog, struct menu *, s32, s32)
{
    WindowFrame wf = pc_BeginStandardWindow("##pc_load_player",
                                             "Load Player", 0.56f, 0.72f,
                                             menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ImGui::IsWindowAppearing()) {
        s_LoadPlayerFocus = -1;
    }

    if (pc_BackPressed()) {
        pc_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pc_load_player_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        float listH = bodyH - pdguiScale(40.0f);
        if (listH < pdguiScale(120.0f)) listH = pdguiScale(120.0f);

        GroupedListResult r = pc_RenderGroupedList("##pc_load_player_list",
                                                    mpLoadPlayerMenuHandler,
                                                    /*param=*/0,
                                                    listH,
                                                    &s_LoadPlayerFocus);

        if (r.clicked && r.clickedGlobalIdx >= 0) {
            /* Legacy SET branches:
             *   - Already-loaded error: pushes file-error dialog.
             *   - Successful load: menuPopDialog() + filemgrSaveOrLoad
             *     (which replaces g_PlayerConfigsArray[0] wholesale).
             * Either way we don't need to do anything else -- the
             * legacy handler drives the dialog transitions.  BUT we
             * do want to propagate the character/name changes to
             * netplay if a load actually succeeded.
             *
             * We can't distinguish error vs success from the SET
             * return value, so we notify unconditionally.
             * netClientSettingsChanged() is idempotent and cheap. */
            list_Set(mpLoadPlayerMenuHandler, 0, r.clickedGlobalIdx);
            pdguiPlaySound(PDGUI_SND_SELECT);
            pc_NetNotifyLocalPlayerChanged();
            /* Legacy menuPopDialog inside the handler already triggered
             * our hotswap renderer's close -- but since ImGui state
             * resets on next Begin(), calling pc_CloseCurrentDialog
             * again would double-pop.  The safe thing is to simply
             * return and let the menu runtime drop us. */
            ImGui::EndChild();
            ImGui::End();
            return 1;
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##pc_load_player_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            pc_CloseCurrentDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" void pdguiMenuPlayerConfigRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    pdguiHotswapRegister(&g_MpCharacterMenuDialog,
                         renderMpCharacter,
                         "MP Character");
    pdguiHotswapRegister(&g_MpPlayerStatsMenuDialog,
                         renderMpPlayerStats,
                         "MP Player Stats");
    pdguiHotswapRegister(&g_MpLoadSettingsMenuDialog,
                         renderMpLoadSettings,
                         "MP Load Settings");
    pdguiHotswapRegister(&g_MpLoadPresetMenuDialog,
                         renderMpLoadPreset,
                         "MP Load Preset");
    pdguiHotswapRegister(&g_MpLoadPlayerMenuDialog,
                         renderMpLoadPlayer,
                         "MP Load Player");

    sysLogPrintf(LOG_NOTE,
        "MENU_IMGUI: Batch 11 Player Config & Stats registered (5 dialogs)");
}
