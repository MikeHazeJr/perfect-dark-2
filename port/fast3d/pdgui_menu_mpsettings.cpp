/**
 * pdgui_menu_mpsettings.cpp -- MP handicap + Batch 12 Music & Misc screens.
 *
 * D5 Phase 3 Batch 12.
 *
 * This file hosts four distinct renderers:
 *
 *   g_MpHandicapsMenuDialog   -> renderHandicap       (pre-existing — Group 4 player handicaps)
 *   g_MpSelectTunesMenuDialog -> renderSelectTunes    (Batch 12 — music track picker)
 *   g_MpSoundtrackMenuDialog  -> renderSoundtrack     (Batch 12 — soundtrack config hub)
 *   g_MpTeamNamesMenuDialog   -> renderTeamNames      (Batch 12 — inline team-name editor)
 *
 * Design (Batch 12 additions):
 *   - The three new dialogs use the s208 shadow-struct call-through pattern
 *     (cloned from s207 in pdgui_menu_playerconfig.cpp) so every state
 *     mutation routes through a legacy C handler -- zero function loss.
 *     Tunes/Soundtrack delegate to `mpSelectTuneListHandler` and
 *     `menuhandlerMpMultipleTunes`, preserving every MENUOP_* branch.
 *   - Team Names replaces the legacy KEYBOARD drill-down
 *     (g_MpChangeTeamNameMenuDialog) with an inline ImGui::InputText per
 *     row.  The legacy `mpTeamNameMenuHandler::MENUOP_SETTEXT` write
 *     semantics are mirrored in pdgui_bridge.c's `pdguiMpsTeamNameSet`
 *     accessor (11-char cap, '\n' terminator, MODFILE_MPSETUP dirty flag)
 *     so g_BossFile.teamnames is written identically to the legacy path.
 *   - `menudialogMpSelectTune` MENUOP_OPEN/CLOSE still fire via the legacy
 *     menu runtime (hotswap only intercepts RENDER) so the g_MusicInterval240
 *     preview-pacing tuning continues to work.
 *   - `mpMenuTextSelectTuneOrTunes` / `mpMenuTextCurrentTrack` dynamic-text
 *     helpers are invoked directly with nullptr (their bodies ignore the
 *     item argument — they read globals).
 *
 * NETWORK MATCH START/END AUDIT
 *   See context/scratch/D5-P3-batch12-2026-04-11.md for the full 9-row audit.
 *   Summary: 8 of 9 fields are LOCAL-ONLY -- tunes, soundtrack flag, and team
 *   names all live in `g_BossFile` and never cross the wire (grep across
 *   port/src/net/ for 'teamnames\|tracknum' returns nothing).  Each client
 *   plays its own music and displays its own team labels from its own boss
 *   file.  The one WIRED field is the challenge match-start flow, which
 *   propagates via the established `g_MpSetup` -> SVC_STAGE_START pipeline
 *   and is exercised by `matchStartFromChallenge` in pdgui_menu_challenges.cpp.
 *   Batch 12 introduces zero shadow/cached copies.
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32 breaks
 * C++).  Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 * All game-state access that requires types.h knowledge goes through
 * pdgui_bridge.c accessors.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui.h"          /* langSafe */
#include "system.h"
#include "inputctx.h"

/* ========================================================================
 * Forward declarations (C boundary)
 * ======================================================================== */

extern "C" {

/* ---- Opaque types for function signatures ---- */
struct menuitem;
struct menudialog;
struct menudialogdef;
struct menu;

/* ---- Dialog definitions ---- */
extern struct menudialogdef g_MpHandicapsMenuDialog;
extern struct menudialogdef g_MpSelectTunesMenuDialog;
extern struct menudialogdef g_MpSoundtrackMenuDialog;
extern struct menudialogdef g_MpTeamNamesMenuDialog;

/* ---- Menu stack ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* ---- Handicap wrappers (matchsetup.c -- avoid types.h in C++) ---- */
u8   matchGetPlayerHandicap(s32 playernum);
void matchSetPlayerHandicap(s32 playernum, u8 val);
void matchResetHandicaps(void);

/* ---- Player names (for labels) ---- */
const char *mpPlayerConfigGetName(s32 playernum);

/* ---- Match config types, struct definitions, and g_MatchConfig ---- */
#include "net/matchsetup.h"

/* ---- Batch A-4: catalog + audio headers for mod track section ---- */
#include "assetcatalog.h"
#include "audio.h"
#include "modmusic.h"

/* ---- Batch 12 legacy handlers (mpSelectTunes / multi-tunes checkbox)
 * Declared with the shadow types so the C++ function-pointer conversion
 * in list_GetOptionCount / checkbox_Get / etc. is exact.  At link time
 * these resolve to the same symbols as the legacy handlerdata-taking
 * declarations in setup.c because `extern "C"` mangling ignores
 * parameter types. ---- */
struct s208_menuitem;
union  s208_handlerdata;
uintptr_t mpSelectTuneListHandler  (s32 op, struct s208_menuitem *, union s208_handlerdata *);
uintptr_t menuhandlerMpMultipleTunes(s32 op, struct s208_menuitem *, union s208_handlerdata *);

/* ---- Batch 12 dynamic-text helpers (setup.c) ----
 * Both ignore `item` and read globals directly; safe to call with nullptr. */
char *mpMenuTextSelectTuneOrTunes(struct menuitem *item);
char *mpMenuTextCurrentTrack     (struct menuitem *item);

/* ---- Batch 12 plain helpers (mplayer.c / music.c) ---- */
s32   mpGetNumUnlockedTracks(void);
char *mpGetTrackName(s32 slotindex);
s32   mpGetTrackMusicNum(s32 slotindex);
s32   mpGetUsingMultipleTunes(void);
s32   mpIsMultiTrackSlotEnabled(s32 slot);
s32   mpGetCurrentTrackSlotNum(void);
void  mpEnableAllMultiTracks(void);
void  mpDisableAllMultiTracks(void);
void  mpRandomiseMultiTracks(void);
void  mpSetTrackToRandom(void);
void  musicStartTrackAsMenu(s32 tracknum);

/* ---- Batch 12 team name bridge accessors (pdgui_bridge.c) ---- */
void pdguiMpsTeamNameGet(u32 team, char *out, u32 outlen);
void pdguiMpsTeamNameSet(u32 team, const char *text);

/* ---- Language helpers ---- */
char *langGet(s32 textid);

/* ---- B-140 Issue B: network sync for mod playlist (room leader → room members) ---- */
extern s32 g_NetMode;
#define MPSETTINGS_NETMODE_CLIENT 2
s32  lobbyIsLocalLeader(void);
void netSendRoomPlaylistUpdate(void);
void musicRestoreInterval(void);  /* restore background music after hover-preview */

/* ---- MENUOP_* opcodes (declared locally per the Batch 4 gotcha; values
 * must match src/include/constants.h exactly) ---- */
#define MENUOP_GET                 8
#define MENUOP_SET                 6
#define MENUOP_GETOPTIONCOUNT      1
#define MENUOP_GETOPTIONTEXT       3
#define MENUOP_GETSELECTEDINDEX    7
#define MENUOP_LISTITEMFOCUS       16
#define MENUOP_GETLISTITEMCHECKBOX 11

/* ---- Team count constant (constants.h MAX_TEAMS) ---- */
#define PDMS_MAX_TEAMS 8

/* ---- s208 shadow menuitem / handlerdata ----
 * ABI-compatible with the real types in src/include/types.h:3337..3417.
 * Cloned from the s207 pattern in pdgui_menu_playerconfig.cpp.  We only
 * need the list and checkbox variants for Batch 12.
 */
struct s208_handlerdata_list_t {
    uintptr_t value;           /* slot idx, count, char* return */
    s32       unk04;           /* checkbox state on GETLISTITEMCHECKBOX */
    s32       groupstartindex;
    s32       unk0c;
};
struct s208_handlerdata_checkbox { u32 value; };

union s208_handlerdata {
    struct s208_handlerdata_list_t   list;
    struct s208_handlerdata_checkbox checkbox;
    u8 _pad[256];
};

struct s208_menuitem {
    u8        type;
    u8        param;
    u32       flags;
    intptr_t  param2;
    intptr_t  param3;
    uintptr_t (*handler)(s32 op, struct s208_menuitem *, union s208_handlerdata *);
};

} /* extern "C" */

/* ========================================================================
 * Button wrapper (shared with renderHandicap)
 * ======================================================================== */

extern "C" void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

static bool PdButton(const char *label, const ImVec2 &size = ImVec2(0, 0))
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
 * s208 call-through helpers (Batch 12 new)
 * ======================================================================== */

typedef uintptr_t (*ListFn)(s32, s208_menuitem *, s208_handlerdata *);
typedef uintptr_t (*CheckboxFn)(s32, s208_menuitem *, s208_handlerdata *);

static s32 list_GetOptionCount(ListFn fn, u8 param)
{
    s208_menuitem it{};
    it.param = param;
    s208_handlerdata h{};
    fn(MENUOP_GETOPTIONCOUNT, &it, &h);
    return (s32)h.list.value;
}

static const char *list_GetOptionText(ListFn fn, u8 param, s32 idx)
{
    s208_menuitem it{};
    it.param = param;
    s208_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    uintptr_t r = fn(MENUOP_GETOPTIONTEXT, &it, &h);
    return (const char *)r;
}

static s32 list_GetSelectedIndex(ListFn fn, u8 param)
{
    s208_menuitem it{};
    it.param = param;
    s208_handlerdata h{};
    fn(MENUOP_GETSELECTEDINDEX, &it, &h);
    return (s32)h.list.value;
}

static void list_SetClick(ListFn fn, u8 param, s32 idx)
{
    s208_menuitem it{};
    it.param = param;
    s208_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    h.list.unk04 = 0;  /* legacy "click" semantics — see mpSelectTuneListHandler:4487 */
    fn(MENUOP_SET, &it, &h);
}

static void list_Focus(ListFn fn, u8 param, s32 idx)
{
    s208_menuitem it{};
    it.param = param;
    s208_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    fn(MENUOP_LISTITEMFOCUS, &it, &h);
}

static s32 list_GetListItemCheckbox(ListFn fn, u8 param, s32 idx)
{
    s208_menuitem it{};
    it.param = param;
    s208_handlerdata h{};
    h.list.value = (uintptr_t)idx;
    fn(MENUOP_GETLISTITEMCHECKBOX, &it, &h);
    return h.list.unk04;
}

static s32 checkbox_Get(CheckboxFn fn, u8 param)
{
    s208_menuitem it{};
    it.param = param;
    s208_handlerdata h{};
    /* The legacy menuhandlerMpMultipleTunes MENUOP_GET branch returns the
     * value directly via the function return, not through handlerdata. */
    return (s32)fn(MENUOP_GET, &it, &h);
}

static void checkbox_Set(CheckboxFn fn, u8 param, s32 on)
{
    s208_menuitem it{};
    it.param = param;
    s208_handlerdata h{};
    h.checkbox.value = on ? 1 : 0;
    fn(MENUOP_SET, &it, &h);
}

/* ========================================================================
 * Window-frame helpers (Batch 12 new -- clone of pc_BeginStandardWindow in
 * pdgui_menu_playerconfig.cpp).  Kept file-local because a shared helper
 * would require a new header.
 * ======================================================================== */

struct PdmsWindowFrame {
    float mw;
    float mh;
    ImVec2 pos;
};

/* S-3: Per-dialog ownership tracking. A single shared flag corrupted input-
 * context ownership when dialogs stacked (e.g., Soundtrack -> SelectTunes):
 * the nested dialog's IsWindowAppearing saw the ctx already active and
 * cleared the outer dialog's ownership bit. Each caller now owns its own
 * bool; pass NULL if the caller never pushes the menu context itself. */
static PdmsWindowFrame pdms_BeginStandardWindow(const char *imguiId, const char *title,
                                                 float widthFrac, float heightFrac,
                                                 bool *ownsCtx)
{
    pdguiPopupDarkenBehind(0.55f);

    PdmsWindowFrame wf;
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
        return wf;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        if (!inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPush(&g_CtxImGuiMenu);
            if (ownsCtx) *ownsCtx = true;
        } else if (ownsCtx) {
            *ownsCtx = false;
        }
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(wf.pos.x, wf.pos.y, wf.mw, wf.mh, title, 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);
    return wf;
}

static void pdms_CloseCurrentDialog(bool *ownsCtx)
{
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
    if (ownsCtx && *ownsCtx && inputCtxIsActive(&g_CtxImGuiMenu)) {
        inputCtxPopDeferred(&g_CtxImGuiMenu);
        *ownsCtx = false;
    }
    menuPopDialog();
}

static bool pdms_BackPressed(void)
{
    return !ImGui::IsWindowAppearing() &&
           (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false));
}

/* ========================================================================
 * Helpers (handicap renderer — pre-existing)
 * ======================================================================== */

/* Count human-player slots in g_MatchConfig */
static int countHumanSlots(void)
{
    int n = 0;
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type == SLOT_PLAYER) n++;
    }
    return n;
}

/* ========================================================================
 * Main render function — Handicaps (pre-existing, unchanged)
 * ======================================================================== */

static bool s_Registered = false;

static s32 renderHandicap(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
    float scale  = pdguiScaleFactor();
    float diagW  = pdguiMenuWidth() * 0.65f;   /* narrower: handicap is a small screen */
    float diagH  = pdguiMenuHeight() * 0.70f;
    ImVec2 pos   = pdguiCenterPos(diagW, diagH);
    float pdTitleH = pdguiScale(39.0f);

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(diagW, diagH));

    if (!ImGui::Begin("##handicap", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    /* C-6: grab focus on appear so controller nav reaches the handicap sliders. */
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    /* Backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + diagW, pos.y + diagH),
                          pdguiPalImU32(PDPAL_BODYBG, 255));
    }

    pdguiDrawPdDialog(pos.x, pos.y, diagW, diagH, "Player Handicaps", 1);

    /* Title */
    {
        const char *title = "Player Handicaps";
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(pos.x + 8.0f, pos.y + 2.0f,
                          diagW - 16.0f, pdTitleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(pos.x + (diagW - ts.x) * 0.5f,
                           pos.y + (pdTitleH - ts.y) * 0.5f),
                    pdguiPalImU32(PDPAL_TITLEFG, 255), title);
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    float footerH  = pdguiScale(75.0f);
    float contentH = diagH - pdTitleH - footerH;
    float sliderW  = diagW * 0.55f;

    ImGui::BeginChild("##handicap_content", ImVec2(0, contentH), false);

    ImGui::TextDisabled("Adjust per-player damage received. 100%% = default.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    int humanCount = countHumanSlots();

    if (humanCount == 0) {
        ImGui::TextDisabled("No human players in this match setup.");
    } else {
        int playerSlot = 0;
        for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
            if (g_MatchConfig.slots[i].type != SLOT_PLAYER) continue;

            ImGui::PushID(playerSlot);

            /* Label: player name */
            const char *pname = mpPlayerConfigGetName(playerSlot);
            if (!pname || !pname[0]) pname = g_MatchConfig.slots[i].name;
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "P%d: %s",
                               playerSlot + 1,
                               pname && pname[0] ? pname : "Player");

            /* Read current handicap — internal 0-255, where 0x80 (128) = 100%.
             * Display as linear percentage: (h * 100) / 128.
             * mpHandicapToDamageScale() is unusable here — it returns 1.0f
             * unconditionally when g_NetMode != 0, making every value show 100%. */
            u8 h = matchGetPlayerHandicap(playerSlot);
            int pct = ((int)h * 100) / 128;

            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderInt("##h", &pct, 0, 200, "%d%%")) {
                u8 raw = (u8)(((int)pct * 128) / 100);
                if (pct > 0 && raw == 0) raw = 1; /* avoid 0% meaning "nearly dead" */
                matchSetPlayerHandicap(playerSlot, raw);
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 0.8f),
                               "%d%% dmg received", pct);

            ImGui::Spacing();
            ImGui::PopID();
            playerSlot++;
        }
    }

    ImGui::EndChild();

    /* ---- Footer ---- */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    float btnW = pdguiScale(195.0f);
    float btnH = pdguiScale(42.0f);
    float totalW = btnW * 2.0f + pdguiScale(12.0f);
    ImGui::SetCursorPosX((diagW - totalW) * 0.5f);

    if (PdButton("Restore Defaults", ImVec2(btnW, btnH))) {
        matchResetHandicaps();
        pdguiPlaySound(PDGUI_SND_SELECT);
    }

    ImGui::SameLine(0, pdguiScale(12.0f));

    if (PdButton("Done", ImVec2(btnW, btnH))
        || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        /* Handicap uses its own Begin path and never pushes g_CtxImGuiMenu,
         * so no ownership flag needed. */
        pdms_CloseCurrentDialog(nullptr);
    }

    ImGui::End();
    return 1;
}

/* ========================================================================
 * Renderer: Select Tunes (g_MpSelectTunesMenuDialog) -- Batch 12
 * ======================================================================== *
 *
 * Legacy layout (setup.c:4702 g_MpSelectTunesMenuItems):
 *   LIST -> mpSelectTuneListHandler
 *
 * List shape (driven by `mpGetNumUnlockedTracks` + `mpGetUsingMultipleTunes`):
 *   Single-tune mode: [track0, track1, ..., trackN-1, "Random"]
 *     Radio behavior: pick any slot to set `g_BossFile.tracknum`; pick the
 *     "Random" sentinel (index == numTracks) to set `g_BossFile.tracknum = -1`.
 *   Multi-tune mode: [track0, track1, ..., trackN-1, "Select All",
 *                     "Select None", "Randomize"]
 *     Checkbox behavior: each track row toggles its bit in
 *     `g_BossFile.multipletracknums[]` via `mpSetTrackSlotEnabled`.
 *     Bulk rows call `mpEnableAllMultiTracks`/`Disable`/`Randomise`.
 *
 * Hover preview: per-frame LISTITEMFOCUS writes `g_CurrentTrack` (legacy)
 * and calls `musicStartTrackAsMenu(mpGetTrackMusicNum(slot))` to start the
 * track so the player hears what they're selecting.  We keep the same
 * behavior by calling `list_Focus` on hover transition.
 *
 * menudialogMpSelectTune MENUOP_OPEN/CLOSE still fire via the legacy menu
 * runtime (hotswap only intercepts RENDER), so `g_MusicInterval240` gets
 * tuned to 80 on open and restored to 15 on close automatically.
 *
 * Network: LOCAL-ONLY.  `g_BossFile.tracknum` / `multipletracknums[]` are
 * per-client boss file state; no wire field.  Each client plays its own
 * music on match start.  See scratch doc rows 1-4.
 */

static s32 s_TunesHoverIdx  = -1;  /* -1 = nothing hovered */

static void pdms_EndTunesPreview(void)
{
    if (s_TunesHoverIdx >= 0) {
        s_TunesHoverIdx = -1;
        musicRestoreInterval();
    }
}

/* ---- Batch A-4: Mod music tracks from catalog ---- */

#define MAX_MOD_TRACKS 64

struct ModTrackInfo {
    const char *catalog_id;
    const char *display_name;
    const char *file_path;
};

struct ModTrackCollector {
    ModTrackInfo tracks[MAX_MOD_TRACKS];
    int count;
};

static void collectModMusicTrack(const asset_entry_t *entry, void *userdata)
{
    ModTrackCollector *col = (ModTrackCollector *)userdata;
    if (col->count >= MAX_MOD_TRACKS) return;
    if (entry->ext.audio.category != 1 /* AUDIO_CAT_MUSIC */) return;
    if (entry->bundled) return;  /* skip base game tracks */

    ModTrackInfo *t = &col->tracks[col->count];
    t->catalog_id = entry->id;
    t->display_name = entry->ext.audio.name;
    t->file_path = entry->ext.audio.file_path;
    col->count++;
}

/* F-2.1-songs: alpha comparator for mod track sort */
static int modTrackCompare(const void *a, const void *b)
{
    const ModTrackInfo *ta = (const ModTrackInfo *)a;
    const ModTrackInfo *tb = (const ModTrackInfo *)b;
    const char *na = ta->display_name ? ta->display_name : ta->catalog_id;
    const char *nb = tb->display_name ? tb->display_name : tb->catalog_id;
    return strcasecmp(na, nb);
}

static s32 renderSelectTunes(struct menudialog *, struct menu *, s32, s32)
{
    static bool s_TunesOwnsCtx = false;
    PdmsWindowFrame wf = pdms_BeginStandardWindow("##pdms_tunes",
                                                    "Select Tunes",
                                                    0.70f, 0.82f,
                                                    &s_TunesOwnsCtx);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ImGui::IsWindowAppearing()) {
        s_TunesHoverIdx = -1;
    }

    if (pdms_BackPressed()) {
        pdms_EndTunesPreview();
        pdms_CloseCurrentDialog(&s_TunesOwnsCtx);
        ImGui::End();
        return 1;
    }

    const int numTracks = mpGetNumUnlockedTracks();

    /* Collect mod tracks, alpha-sort (F-2.1-songs) */
    ModTrackCollector mc{};
    assetCatalogIterateByType(ASSET_AUDIO, collectModMusicTrack, &mc);
    if (mc.count > 1) {
        qsort(mc.tracks, mc.count, sizeof(ModTrackInfo), modTrackCompare);
    }

    /* Shuffle toggle */
    {
        bool shuffle = audioGetModShuffle() != 0;
        if (ImGui::Checkbox("Shuffle", &shuffle)) {
            audioSetModShuffle(shuffle ? 1 : 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  %s",
            shuffle ? "Random order each round" : "Sequential (jukebox mode)");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* B-140 Issue B: Two-panel layout -- Library (left) + Selected Tracks (right).
     * Click left = add to playlist; click right = remove from playlist.
     * Hover left = preview track; hover off = restore background music.
     * Selected Tracks = audioModPlaylist = network-synced when leader in room. */
    float avail    = ImGui::GetContentRegionAvail().y;
    float bodyH    = pdguiBodyHeightForActionBar(avail);
    float gap      = pdguiScale(10.0f);
    float colW     = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    bool  anyHover = false;

    /* ---- LEFT: Library ---- */
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Library");
    ImGui::Separator();
    if (ImGui::BeginChild("##tunes_lib", ImVec2(colW, bodyH), false,
                          ImGuiWindowFlags_NoBackground)) {

        /* Base Game tracks -- hover-preview only; click = legacy single-tune select */
        if (numTracks > 0) {
            char baseHdr[48];
            snprintf(baseHdr, sizeof(baseHdr), "Base Game (%d)", numTracks);
            if (ImGui::TreeNodeEx(baseHdr, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int i = 0; i < numTracks; i++) {
                    const char *name = mpGetTrackName(i);
                    if (!name || !name[0]) name = "???";
                    ImGui::PushID(i);
                    if (ImGui::Selectable(name, false, 0,
                                          ImVec2(0, pdguiScale(22.0f)))) {
                        list_SetClick(mpSelectTuneListHandler, 0, i);
                        pdguiPlaySound(PDGUI_SND_SELECT);
                    }
                    if (ImGui::IsItemHovered()) {
                        anyHover = true;
                        if (s_TunesHoverIdx != i) {
                            s_TunesHoverIdx = i;
                            list_Focus(mpSelectTuneListHandler, 0, i);
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }

        /* Mod Tracks -- click = add to Selected Tracks; hover = preview */
        if (mc.count > 0) {
            ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
            char modHdr[48];
            snprintf(modHdr, sizeof(modHdr), "Mod Tracks (%d)", mc.count);
            if (ImGui::TreeNodeEx(modHdr, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int m = 0; m < mc.count; m++) {
                    const ModTrackInfo *t = &mc.tracks[m];
                    ImGui::PushID(numTracks + m);
                    bool inPl = audioIsInModPlaylist(t->catalog_id) != 0;
                    const char *disp = t->display_name ? t->display_name : t->catalog_id;
                    /* Highlight in library if already in playlist */
                    if (ImGui::Selectable(disp, inPl, 0,
                                          ImVec2(0, pdguiScale(22.0f)))) {
                        if (inPl) {
                            audioRemoveModPlaylistEntry(t->catalog_id);
                            pdguiPlaySound(PDGUI_SND_KBCANCEL);
                        } else {
                            audioAddModPlaylistEntry(t->catalog_id);
                            pdguiPlaySound(PDGUI_SND_SELECT);
                        }
                        audioResetPlaylistIndex();
                        if (g_NetMode == MPSETTINGS_NETMODE_CLIENT
                            && lobbyIsLocalLeader()) {
                            netSendRoomPlaylistUpdate();
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        anyHover = true;
                        if (s_TunesHoverIdx != numTracks + m) {
                            s_TunesHoverIdx = numTracks + m;
                            audioSetModTrackId(t->catalog_id);
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }

        if (numTracks <= 0 && mc.count <= 0) {
            ImGui::TextDisabled("(No tunes available)");
        }
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);

    /* ---- RIGHT: Selected Tracks ---- */
    ImGui::BeginGroup();
    s32 plCount = audioGetModPlaylistCount();
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f),
                       "Selected (%d)", plCount);
    ImGui::Separator();
    if (ImGui::BeginChild("##tunes_sel", ImVec2(colW, bodyH), false,
                          ImGuiWindowFlags_NoBackground)) {
        if (plCount == 0) {
            ImGui::TextDisabled("(empty)");
            ImGui::Spacing();
            ImGui::TextDisabled("Click a Mod Track");
            ImGui::TextDisabled("on the left to add.");
        } else {
            bool removed = false;
            for (s32 p = 0; p < plCount && !removed; p++) {
                const char *cid = audioGetModPlaylistEntry(p);
                if (!cid) continue;
                /* Resolve display name from collected tracks */
                const char *disp = cid;
                for (int m = 0; m < mc.count; m++) {
                    if (mc.tracks[m].catalog_id
                        && strcmp(mc.tracks[m].catalog_id, cid) == 0) {
                        if (mc.tracks[m].display_name) disp = mc.tracks[m].display_name;
                        break;
                    }
                }
                char label[128];
                snprintf(label, sizeof(label), "%s##sel%d", disp, p);
                ImGui::PushID(2000 + p);
                if (ImGui::Selectable(label, false, 0,
                                      ImVec2(0, pdguiScale(22.0f)))) {
                    audioRemoveModPlaylistEntry(cid);
                    audioResetPlaylistIndex();
                    if (g_NetMode == MPSETTINGS_NETMODE_CLIENT
                        && lobbyIsLocalLeader()) {
                        netSendRoomPlaylistUpdate();
                    }
                    pdguiPlaySound(PDGUI_SND_KBCANCEL);
                    removed = true;
                }
                ImGui::PopID();
            }
            if (!removed && plCount > 1) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Selectable("Clear All##tunes_clr", false, 0,
                                      ImVec2(0, pdguiScale(22.0f)))) {
                    audioClearModPlaylist();
                    if (g_NetMode == MPSETTINGS_NETMODE_CLIENT
                        && lobbyIsLocalLeader()) {
                        netSendRoomPlaylistUpdate();
                    }
                    pdguiPlaySound(PDGUI_SND_KBCANCEL);
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    /* Hover-off resume: when nothing hovered this frame, restore background music */
    if (!anyHover && s_TunesHoverIdx >= 0) {
        pdms_EndTunesPreview();
    }

    if (pdguiBeginActionBar("##pdms_tunes_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            pdms_EndTunesPreview();
            pdms_CloseCurrentDialog(&s_TunesOwnsCtx);
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* ========================================================================
 * Renderer: Soundtrack (g_MpSoundtrackMenuDialog) -- Batch 12
 * ======================================================================== *
 *
 * Legacy layout (setup.c:4723 g_MpSoundtrackMenuItems):
 *   LABEL            "Current:"
 *   LABEL            mpMenuTextCurrentTrack (dynamic: track name or "Multiple Tunes" or "Random")
 *   SEPARATOR
 *   SELECTABLE       mpMenuTextSelectTuneOrTunes -> push g_MpSelectTunesMenuDialog
 *   CHECKBOX         "Multiple Tunes" -> menuhandlerMpMultipleTunes
 *   SEPARATOR
 *   SELECTABLE       "Back" (CLOSESDIALOG)
 *
 * ImGui shape: same four controls stacked in a compact modal, plus a docked
 * Back action bar.
 *
 * Network: LOCAL-ONLY.  Writes to `g_BossFile` via `mpSetUsingMultipleTunes`.
 * See scratch doc row 5.
 */

static s32 renderSoundtrack(struct menudialog *, struct menu *, s32, s32)
{
    static bool s_SoundtrackOwnsCtx = false;
    PdmsWindowFrame wf = pdms_BeginStandardWindow("##pdms_soundtrack",
                                                    "Soundtrack",
                                                    0.50f, 0.58f,
                                                    &s_SoundtrackOwnsCtx);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (pdms_BackPressed()) {
        pdms_CloseCurrentDialog(&s_SoundtrackOwnsCtx);
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pdms_soundtrack_body", ImVec2(0, bodyH), false,
                          ImGuiWindowFlags_NoBackground)) {

        /* "Current:" row — show playlist info or single track name */
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Current Track:");
        ImGui::SameLine();
        {
            s32 plCount = audioGetModPlaylistCount();
            const char *cur = NULL;
            if (plCount > 1) {
                /* Playlist mode — show count and mode */
                static char plBuf[64];
                snprintf(plBuf, sizeof(plBuf), "%d mod tracks (%s)",
                         plCount, audioGetModShuffle() ? "shuffle" : "sequential");
                cur = plBuf;
            } else if (plCount == 1) {
                /* Single mod track in playlist */
                const char *modId = audioGetModPlaylistEntry(0);
                if (modId && modId[0]) {
                    const asset_entry_t *ae = assetCatalogResolve(modId);
                    if (ae && ae->type == ASSET_AUDIO) {
                        cur = ae->ext.audio.name;
                    }
                }
            }
            if (!cur || !cur[0]) {
                cur = mpMenuTextCurrentTrack(nullptr);
            }
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s",
                               cur && cur[0] ? cur : "---");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* "Select Tune(s)" push-selectable.  The dynamic label swaps between
         * "Select Tune" (multi-mode) and "Select Tunes" (single-mode). */
        const char *pickLabel = mpMenuTextSelectTuneOrTunes(nullptr);
        if (!pickLabel || !pickLabel[0]) pickLabel = "Select Tune";

        float selH = pdguiScale(36.0f);
        if (ImGui::Selectable(pickLabel, false, 0, ImVec2(0, selH))) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            menuPushDialog(&g_MpSelectTunesMenuDialog);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* "Multiple Tunes" checkbox routed through the legacy handler. */
        bool multi = checkbox_Get(menuhandlerMpMultipleTunes, 0) != 0;
        bool newMulti = multi;
        if (ImGui::Checkbox("Multiple Tunes", &newMulti)) {
            checkbox_Set(menuhandlerMpMultipleTunes, 0, newMulti ? 1 : 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  Shuffle a set of tracks between matches");
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##pdms_soundtrack_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            pdms_CloseCurrentDialog(&s_SoundtrackOwnsCtx);
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* ========================================================================
 * Renderer: Team Names (g_MpTeamNamesMenuDialog) -- Batch 12
 * ======================================================================== *
 *
 * Legacy layout (setup.c:4813 g_MpTeamNamesMenuItems):
 *   SELECTABLE x8  -> menuhandlerMpTeamNameSlot(team=0..7) pushes g_MpChangeTeamNameMenuDialog
 *   SEPARATOR
 *   SELECTABLE      "Back" (CLOSESDIALOG)
 *
 * The legacy flow pushes a KEYBOARD drill-down for each team name.  PC
 * replaces that with an inline ImGui::InputText per row — faster, no
 * modal-stack thrash.  Writes go through `pdguiMpsTeamNameSet` which
 * mirrors the legacy `mpTeamNameMenuHandler::MENUOP_SETTEXT` byte layout
 * (11-char cap, '\n' terminator, MODFILE_MPSETUP dirty flag).
 *
 * The 8 team colours are fixed (R/Y/B/M/C/O/P/Brown) — legacy uses
 * L_OPTIONS_008..L_OPTIONS_015; we hardcode English fallbacks for
 * consistency with other Batch-N ImGui renderers.
 *
 * Network: LOCAL-ONLY.  `g_BossFile.teamnames[MAX_TEAMS][12]` is per-client
 * display state.  Each client sees team labels from its OWN boss file in
 * the scoreboard and pause rankings.  See scratch doc row 6.
 */

struct TeamSlotInfo {
    const char *colourName;
    ImVec4      swatch;
};

static const TeamSlotInfo s_TeamSlots[PDMS_MAX_TEAMS] = {
    { "Red",     ImVec4(0.90f, 0.18f, 0.18f, 1.0f) },
    { "Yellow",  ImVec4(0.95f, 0.88f, 0.20f, 1.0f) },
    { "Blue",    ImVec4(0.22f, 0.42f, 0.95f, 1.0f) },
    { "Magenta", ImVec4(0.92f, 0.28f, 0.85f, 1.0f) },
    { "Cyan",    ImVec4(0.20f, 0.88f, 0.92f, 1.0f) },
    { "Orange",  ImVec4(0.98f, 0.60f, 0.12f, 1.0f) },
    { "Pink",    ImVec4(0.98f, 0.62f, 0.72f, 1.0f) },
    { "Brown",   ImVec4(0.55f, 0.38f, 0.22f, 1.0f) },
};

/* Per-session edit buffers.  ImGui::InputText owns the buffer during the
 * frame; we seed it from the bridge getter on window-appearing, then push
 * changes back to g_BossFile on commit (Enter, lose focus, or row click). */
static char s_TeamNameBuf[PDMS_MAX_TEAMS][16];
static bool s_TeamNameBufSeeded = false;

static void tn_SeedBuffers(void)
{
    for (u32 t = 0; t < PDMS_MAX_TEAMS; t++) {
        pdguiMpsTeamNameGet(t, s_TeamNameBuf[t], sizeof(s_TeamNameBuf[t]));
    }
    s_TeamNameBufSeeded = true;
}

static void tn_CommitBuffer(u32 team)
{
    if (team >= PDMS_MAX_TEAMS) return;
    pdguiMpsTeamNameSet(team, s_TeamNameBuf[team]);
}

static s32 renderTeamNames(struct menudialog *, struct menu *, s32, s32)
{
    static bool s_TeamNamesOwnsCtx = false;
    PdmsWindowFrame wf = pdms_BeginStandardWindow("##pdms_teamnames",
                                                    "Team Names",
                                                    0.56f, 0.72f,
                                                    &s_TeamNamesOwnsCtx);
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ImGui::IsWindowAppearing()) {
        tn_SeedBuffers();
    }

    if (pdms_BackPressed()) {
        /* Commit any buffered edits before closing so a half-typed name
         * is preserved when the user escapes out. */
        for (u32 t = 0; t < PDMS_MAX_TEAMS; t++) {
            tn_CommitBuffer(t);
        }
        pdms_CloseCurrentDialog(&s_TeamNamesOwnsCtx);
        ImGui::End();
        return 1;
    }

    ImGui::TextDisabled("Edit team labels shown on the scoreboard. 11 chars max.");
    ImGui::Spacing();

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pdms_teamnames_body", ImVec2(0, bodyH), false,
                          ImGuiWindowFlags_NoBackground)) {

        float swatchW = pdguiScale(18.0f);
        float swatchH = pdguiScale(18.0f);
        float labelW  = pdguiScale(92.0f);
        float inputW  = pdguiScale(220.0f);

        for (u32 t = 0; t < PDMS_MAX_TEAMS; t++) {
            ImGui::PushID((int)t);

            /* Colour swatch */
            ImGui::ColorButton("##swatch",
                               s_TeamSlots[t].swatch,
                               ImGuiColorEditFlags_NoTooltip |
                               ImGuiColorEditFlags_NoDragDrop |
                               ImGuiColorEditFlags_NoAlpha,
                               ImVec2(swatchW, swatchH));
            ImGui::SameLine();

            /* Colour label (fixed) */
            ImGui::SetNextItemWidth(labelW);
            ImGui::TextColored(s_TeamSlots[t].swatch, "%s", s_TeamSlots[t].colourName);
            ImGui::SameLine(labelW + swatchW + pdguiScale(24.0f));

            /* Editable team name */
            ImGui::SetNextItemWidth(inputW);
            ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
            if (ImGui::InputText("##name", s_TeamNameBuf[t],
                                 sizeof(s_TeamNameBuf[t]), flags)) {
                /* Enter pressed — commit immediately. */
                tn_CommitBuffer(t);
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            /* Commit on lose-focus too (click-away, tab-out, etc.).
             * ImGui reports IsItemDeactivatedAfterEdit for any edit
             * completed by a focus transition. */
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                tn_CommitBuffer(t);
            }

            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
        }

        if (!s_TeamNameBufSeeded) {
            /* Defensive — should always be seeded by IsWindowAppearing. */
            tn_SeedBuffers();
        }
    }
    ImGui::EndChild();

    if (pdguiBeginActionBar("##pdms_teamnames_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            for (u32 t = 0; t < PDMS_MAX_TEAMS; t++) {
                tn_CommitBuffer(t);
            }
            pdms_CloseCurrentDialog(&s_TeamNamesOwnsCtx);
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMenuMpSettingsRegister(void)
{
    if (!s_Registered) {
        pdguiHotswapRegister(&g_MpHandicapsMenuDialog, renderHandicap,
                             "Player Handicaps");
        pdguiHotswapRegister(&g_MpSelectTunesMenuDialog, renderSelectTunes,
                             "MP Select Tunes");
        pdguiHotswapRegister(&g_MpSoundtrackMenuDialog, renderSoundtrack,
                             "MP Soundtrack");
        pdguiHotswapRegister(&g_MpTeamNamesMenuDialog, renderTeamNames,
                             "MP Team Names");
        s_Registered = true;
    }
    sysLogPrintf(LOG_NOTE,
        "pdgui_menu_mpsettings: registered (handicaps + Batch 12 Music & Team Names)");
}

} /* extern "C" */
