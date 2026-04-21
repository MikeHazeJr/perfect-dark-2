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
#include "menupool.h"

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

/* ---- B-140 Issue B: network sync for mod playlist (room leader -> room members) ---- */
extern s32 g_NetMode;
extern s32 g_NetDedicated;
#define MPSETTINGS_NETMODE_SERVER 1
#define MPSETTINGS_NETMODE_CLIENT 2
s32  lobbyIsLocalLeader(void);
void netSendRoomPlaylistUpdate(void);
void musicRestoreInterval(void);  /* restore background music after preview */

/* ---- Preview playback (Issue D: click-to-play) ----
 * menuChooseMusic is declared in src/include/game/menu.h, but we can't include
 * that header from C++ (pulls types.h). Declare locally at the C boundary. */
u32 menuChooseMusic(void);

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

/* S-3 / S300: Per-dialog ownership is now authoritatively tracked by the
 * menu pool. Each of the 4 sub-dialogs (Handicap, Soundtrack, SelectTunes,
 * TeamNames) maps to its own MENU_TYPE_* slot so stacking (e.g., Soundtrack
 * → SelectTunes) remains safe: only the first opener's pool slot actually
 * pushes g_CtxImGuiMenu; subsequent openers enter shared mode and the pop
 * stays with the outer slot. Release on cull or close is idempotent. */
static PdmsWindowFrame pdms_BeginStandardWindow(const char *imguiId, const char *title,
                                                 float widthFrac, float heightFrac,
                                                 const struct menudialogdef *def)
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
        /* S295 F4 leak guard — S300: pool owns the ctx; release pops it if
         * and only if this slot owned the push. */
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

static void pdms_CloseCurrentDialog(void)
{
    pdguiPlaySound(PDGUI_SND_KBCANCEL);
    /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
    menuPopDialog();
}

static bool pdms_BackPressed(void)
{
    return !ImGui::IsWindowAppearing() &&
           ImGui::IsKeyPressed(ImGuiKey_Escape, false);
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

    pdguiSetCursorBelowTitle(pdTitleH);

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
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        /* Handicap uses its own Begin path and never pushes g_CtxImGuiMenu,
         * so no pool-owned ctx involved — plain menuPopDialog. */
        pdms_CloseCurrentDialog();
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

/* Issue D: click-to-play preview state. s_TunesPreviewIdx is a unified row
 * index into the virtual [base tracks | mod tracks] list (base rows take the
 * 0..numTracks-1 range; mod rows occupy numTracks..numTracks+mc.count-1).
 * -1 means nothing is currently being previewed. */
static s32 s_TunesPreviewIdx = -1;

/* Stop the preview (if any) and fall back to normal background music as if
 * the BG had been playing the whole time. Safe to call when no preview is
 * active (acts as a no-op). */
static void pdms_EndTunesPreview(void)
{
    if (s_TunesPreviewIdx < 0) {
        return;
    }

    /* Stop any mod PCM stream and clear the mod track id so the server path
     * doesn't pick it up on the next match. */
    modMusicStop();
    audioSetModTrackId("");

    /* Snap back to the normal menu background music. musicStartTrackAsMenu
     * is idempotent when the target tracknum equals g_MenuTrack. */
    musicStartTrackAsMenu((s32)menuChooseMusic());

    s_TunesPreviewIdx = -1;
    musicRestoreInterval();
}

/* Start previewing a base-game track (slot index in the mp track table). */
static void pdms_PreviewBaseTrack(s32 slotIdx)
{
    /* Kill any mod stream that was previewing so base music isn't muted. */
    modMusicStop();
    audioSetModTrackId("");

    s32 mnum = mpGetTrackMusicNum(slotIdx);
    if (mnum >= 0) {
        musicStartTrackAsMenu(mnum);
    }
    s_TunesPreviewIdx = slotIdx;
}

/* Start previewing a mod track (catalog id + file path). */
static void pdms_PreviewModTrack(s32 rowIdx, const char *cid, const char *file_path)
{
    if (!file_path || !file_path[0]) {
        return;
    }
    modMusicPlay(file_path);
    if (cid) {
        audioSetModTrackId(cid);
    }
    s_TunesPreviewIdx = rowIdx;
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
    /* S309: log rejection cause so bug reports like "my songs aren't
     * showing up" surface the filter that dropped them. Fires at most
     * a handful of times per render — cheap. */
    if (entry->ext.audio.category != 1 /* AUDIO_CAT_MUSIC */) {
        sysLogPrintf(LOG_NOTE, "SELECTTUNES: skip '%s' (category=%d, want 1 MUSIC)",
                     entry->id ? entry->id : "?",
                     (int)entry->ext.audio.category);
        return;
    }
    if (entry->bundled) return;  /* skip base game tracks */

    ModTrackInfo *t = &col->tracks[col->count];
    t->catalog_id = entry->id;
    t->display_name = entry->ext.audio.name;
    t->file_path = entry->ext.audio.file_path;
    col->count++;
}

/* B-188: Base-game music catalog lookup — mirrors ModTrackCollector but
 * holds bundled entries. We need the catalog_id of each base track so a
 * click can call audioAddModPlaylistEntry() exactly like mod tracks. The
 * musicnum field bridges the UI's mp-slot index space
 * (mpGetTrackMusicNum) to the catalog sound_id. */
#define MAX_BASE_TRACKS 128

struct BaseTrackInfo {
    s32         musicnum;       /* catalog ext.audio.sound_id (MUSIC_* enum) */
    const char *catalog_id;
    const char *display_name;
};

struct BaseTrackCollector {
    BaseTrackInfo tracks[MAX_BASE_TRACKS];
    int           count;
};

static void collectBaseMusicTrack(const asset_entry_t *entry, void *userdata)
{
    BaseTrackCollector *col = (BaseTrackCollector *)userdata;
    if (col->count >= MAX_BASE_TRACKS) return;
    if (entry->ext.audio.category != 1 /* AUDIO_CAT_MUSIC */) return;
    if (!entry->bundled) return;

    BaseTrackInfo *t = &col->tracks[col->count];
    t->musicnum     = entry->ext.audio.sound_id;
    t->catalog_id   = entry->id;
    t->display_name = entry->ext.audio.name;
    col->count++;
}

static const char *baseCatalogIdByMusicnum(const BaseTrackCollector *bc, s32 musicnum)
{
    for (int i = 0; i < bc->count; i++) {
        if (bc->tracks[i].musicnum == musicnum) return bc->tracks[i].catalog_id;
    }
    return NULL;
}

static const char *baseDisplayNameByCatalogId(const BaseTrackCollector *bc, const char *cid)
{
    if (!cid) return NULL;
    for (int i = 0; i < bc->count; i++) {
        if (bc->tracks[i].catalog_id && strcmp(bc->tracks[i].catalog_id, cid) == 0) {
            return bc->tracks[i].display_name;
        }
    }
    return NULL;
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

static s32 renderSelectTunes(struct menudialog *dialog, struct menu *, s32, s32)
{
    /* S300: s_TunesOwnsCtx removed — pool owns MENU_TYPE_MP_TUNES ctx. */
    PdmsWindowFrame wf = pdms_BeginStandardWindow("##pdms_tunes",
                                                    "Select Tunes",
                                                    0.70f, 0.82f,
                                                    menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (ImGui::IsWindowAppearing()) {
        /* Entering the dialog fresh: no preview is playing yet. */
        s_TunesPreviewIdx = -1;
    }

    if (pdms_BackPressed()) {
        pdms_EndTunesPreview();
        pdms_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    const int numTracks = mpGetNumUnlockedTracks();

    /* B-208: cache catalog scans — full audio iteration per frame was hot. */
    static ModTrackCollector s_TunesMc;
    static BaseTrackCollector s_TunesBc;
    static u32 s_TunesCatGen = 0xffffffffu;
    u32 catgen = assetCatalogGetGeneration();
    if (ImGui::IsWindowAppearing() || catgen != s_TunesCatGen) {
        s_TunesCatGen = catgen;
        memset(&s_TunesMc, 0, sizeof(s_TunesMc));
        memset(&s_TunesBc, 0, sizeof(s_TunesBc));
        assetCatalogIterateByType(ASSET_AUDIO, collectModMusicTrack, &s_TunesMc);
        if (s_TunesMc.count > 1) {
            qsort(s_TunesMc.tracks, s_TunesMc.count, sizeof(ModTrackInfo), modTrackCompare);
        }
        assetCatalogIterateByType(ASSET_AUDIO, collectBaseMusicTrack, &s_TunesBc);
    }
    ModTrackCollector &mc = s_TunesMc;
    BaseTrackCollector &bc = s_TunesBc;

    /* S309: one-shot diagnostic when the screen opens so the log names
     * the mod-track count. Pairs with the per-entry 'skip' lines emitted
     * by collectModMusicTrack — helps triage "where did my songs go?". */
    if (ImGui::IsWindowAppearing()) {
        sysLogPrintf(LOG_NOTE,
            "SELECTTUNES: open — base_tracks=%d mod_tracks=%d base_catalog=%d",
            (int)numTracks, (int)mc.count, (int)bc.count);
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
    /* Issue D: width reserved for the per-row play/stop button. Laid out
     * inline at the start of each row with SameLine so the name selectable
     * still fills the remaining width. */
    float playBtnW = pdguiScale(40.0f);

    /* ---- LEFT: Library ---- */
    ImGui::BeginGroup();
    /* S311: "Library" section header follows theme title glow. */
    ImGui::TextColored(pdguiVec4TitleGlow(), "Library");
    ImGui::Separator();
    if (ImGui::BeginChild("##tunes_lib", ImVec2(colW, bodyH), false,
                          ImGuiWindowFlags_NoBackground)) {

        /* Base Game tracks -- play button previews; name selectable toggles
         * the shared mod playlist (B-188). Mod and base tracks coexist in
         * audioModPlaylist; catalog_id is the key.
         *
         * Issue D: preview is click-to-play (mouse play button or gamepad X
         * on the focused row). Hover no longer triggers preview. */
        if (numTracks > 0) {
            char baseHdr[48];
            snprintf(baseHdr, sizeof(baseHdr), "Base Game (%d)", numTracks);
            if (ImGui::TreeNodeEx(baseHdr, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int i = 0; i < numTracks; i++) {
                    const char *name = mpGetTrackName(i);
                    if (!name || !name[0]) name = "???";
                    const s32   musicnum = mpGetTrackMusicNum(i);
                    const char *cid      = baseCatalogIdByMusicnum(&bc, musicnum);
                    bool        inPl     = (cid != NULL) && audioIsInModPlaylist(cid) != 0;
                    bool        isPrev   = (s_TunesPreviewIdx == i);

                    ImGui::PushID(i);

                    /* Play/stop button (mouse preview trigger) */
                    const char *playLabel = isPrev ? "[]" : ">";
                    if (ImGui::Button(playLabel,
                                      ImVec2(playBtnW, pdguiScale(22.0f)))) {
                        if (isPrev) {
                            pdms_EndTunesPreview();
                            pdguiPlaySound(PDGUI_SND_KBCANCEL);
                        } else {
                            pdms_PreviewBaseTrack(i);
                            pdguiPlaySound(PDGUI_SND_SELECT);
                        }
                    }
                    ImGui::SameLine();

                    if (ImGui::Selectable(name, inPl, 0,
                                          ImVec2(0, pdguiScale(22.0f)))) {
                        if (cid) {
                            if (inPl) {
                                audioRemoveModPlaylistEntry(cid);
                                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                            } else {
                                audioAddModPlaylistEntry(cid);
                                pdguiPlaySound(PDGUI_SND_SELECT);
                            }
                            audioResetPlaylistIndex();
                            if (lobbyIsLocalLeader()
                                && (g_NetMode == MPSETTINGS_NETMODE_CLIENT
                                    || (g_NetMode == MPSETTINGS_NETMODE_SERVER && !g_NetDedicated))) {
                                netSendRoomPlaylistUpdate();
                            }
                        } else {
                            /* Catalog miss (shouldn't happen -- base music
                             * registers at boot). Fall back to legacy
                             * single-tune select so the click isn't a
                             * no-op on a broken build. */
                            list_SetClick(mpSelectTuneListHandler, 0, i);
                            pdguiPlaySound(PDGUI_SND_SELECT);
                            sysLogPrintf(LOG_WARNING,
                                "SELECTTUNES: no catalog entry for base track "
                                "slot=%d musicnum=%d -- using legacy single-select",
                                i, (int)musicnum);
                        }
                    }
                    /* Gamepad X (ImGui::GamepadFaceLeft = Xbox X / PS Square)
                     * while this row is focused -> toggle preview. */
                    if (ImGui::IsItemFocused()
                        && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)) {
                        if (isPrev) {
                            pdms_EndTunesPreview();
                            pdguiPlaySound(PDGUI_SND_KBCANCEL);
                        } else {
                            pdms_PreviewBaseTrack(i);
                            pdguiPlaySound(PDGUI_SND_SELECT);
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }

        /* Mod Tracks -- play button previews; name selectable adds/removes
         * from Selected Tracks. Issue D: no hover-preview. */
        if (mc.count > 0) {
            ImGui::Dummy(ImVec2(0, pdguiScale(4.0f)));
            char modHdr[48];
            snprintf(modHdr, sizeof(modHdr), "Mod Tracks (%d)", mc.count);
            if (ImGui::TreeNodeEx(modHdr, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int m = 0; m < mc.count; m++) {
                    const ModTrackInfo *t = &mc.tracks[m];
                    const s32 rowIdx = numTracks + m;
                    const bool isPrev = (s_TunesPreviewIdx == rowIdx);
                    ImGui::PushID(rowIdx);
                    bool inPl = audioIsInModPlaylist(t->catalog_id) != 0;
                    const char *disp = t->display_name ? t->display_name : t->catalog_id;

                    /* Play/stop button */
                    const char *playLabel = isPrev ? "[]" : ">";
                    if (ImGui::Button(playLabel,
                                      ImVec2(playBtnW, pdguiScale(22.0f)))) {
                        if (isPrev) {
                            pdms_EndTunesPreview();
                            pdguiPlaySound(PDGUI_SND_KBCANCEL);
                        } else {
                            pdms_PreviewModTrack(rowIdx, t->catalog_id,
                                                 t->file_path);
                            pdguiPlaySound(PDGUI_SND_SELECT);
                        }
                    }
                    ImGui::SameLine();

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
                        if (lobbyIsLocalLeader()
                            && (g_NetMode == MPSETTINGS_NETMODE_CLIENT
                                || (g_NetMode == MPSETTINGS_NETMODE_SERVER && !g_NetDedicated))) {
                            netSendRoomPlaylistUpdate();
                        }
                    }
                    /* Gamepad X preview toggle while row is focused */
                    if (ImGui::IsItemFocused()
                        && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)) {
                        if (isPrev) {
                            pdms_EndTunesPreview();
                            pdguiPlaySound(PDGUI_SND_KBCANCEL);
                        } else {
                            pdms_PreviewModTrack(rowIdx, t->catalog_id,
                                                 t->file_path);
                            pdguiPlaySound(PDGUI_SND_SELECT);
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
            ImGui::TextDisabled("Click a track on");
            ImGui::TextDisabled("the left to add.");
        } else {
            bool removed = false;
            for (s32 p = 0; p < plCount && !removed; p++) {
                const char *cid = audioGetModPlaylistEntry(p);
                if (!cid) continue;
                /* Resolve display name from collected tracks — mod first,
                 * then base (B-188: base-game tracks may appear here too). */
                const char *disp = cid;
                for (int m = 0; m < mc.count; m++) {
                    if (mc.tracks[m].catalog_id
                        && strcmp(mc.tracks[m].catalog_id, cid) == 0) {
                        if (mc.tracks[m].display_name) disp = mc.tracks[m].display_name;
                        break;
                    }
                }
                if (disp == cid) {
                    const char *baseDisp = baseDisplayNameByCatalogId(&bc, cid);
                    if (baseDisp && baseDisp[0]) disp = baseDisp;
                }
                char label[128];
                snprintf(label, sizeof(label), "%s##sel%d", disp, p);
                ImGui::PushID(2000 + p);
                if (ImGui::Selectable(label, false, 0,
                                      ImVec2(0, pdguiScale(22.0f)))) {
                    audioRemoveModPlaylistEntry(cid);
                    audioResetPlaylistIndex();
                    if (lobbyIsLocalLeader()
                        && (g_NetMode == MPSETTINGS_NETMODE_CLIENT
                            || (g_NetMode == MPSETTINGS_NETMODE_SERVER && !g_NetDedicated))) {
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
                    if (lobbyIsLocalLeader()
                        && (g_NetMode == MPSETTINGS_NETMODE_CLIENT
                            || (g_NetMode == MPSETTINGS_NETMODE_SERVER && !g_NetDedicated))) {
                        netSendRoomPlaylistUpdate();
                    }
                    pdguiPlaySound(PDGUI_SND_KBCANCEL);
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    /* Issue D: preview persists until the user explicitly stops it or closes
     * the dialog -- no hover-off resume. pdms_EndTunesPreview fires on
     * Back/Escape (pdms_BackPressed branch above) and on the action-bar
     * Back button below. */

    if (pdguiBeginActionBar("##pdms_tunes_ab")) {
        if (pdguiActionBarButton("Back", 1, ImGui::GetContentRegionAvail().x)) {
            pdms_EndTunesPreview();
            pdms_CloseCurrentDialog();
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

static s32 renderSoundtrack(struct menudialog *dialog, struct menu *, s32, s32)
{
    /* S300: s_SoundtrackOwnsCtx removed — pool owns MENU_TYPE_MP_SOUNDTRACK. */
    PdmsWindowFrame wf = pdms_BeginStandardWindow("##pdms_soundtrack",
                                                    "Soundtrack",
                                                    0.50f, 0.58f,
                                                    menupoolDialogDef(dialog));
    if (wf.mw == 0.0f) { ImGui::End(); return 1; }

    if (pdms_BackPressed()) {
        pdms_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pdms_soundtrack_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* "Current:" row — show playlist info or single track name */
        ImGui::TextColored(pdguiVec4TitleGlow(), "Current Track:");
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
            pdms_CloseCurrentDialog();
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

static s32 renderTeamNames(struct menudialog *dialog, struct menu *, s32, s32)
{
    /* S300: s_TeamNamesOwnsCtx removed — pool owns MENU_TYPE_MP_TEAMNAMES. */
    PdmsWindowFrame wf = pdms_BeginStandardWindow("##pdms_teamnames",
                                                    "Team Names",
                                                    0.56f, 0.72f,
                                                    menupoolDialogDef(dialog));
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
        pdms_CloseCurrentDialog();
        ImGui::End();
        return 1;
    }

    ImGui::TextDisabled("Edit team labels shown on the scoreboard. 11 chars max.");
    ImGui::Spacing();

    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##pdms_teamnames_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
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
            pdms_CloseCurrentDialog();
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
