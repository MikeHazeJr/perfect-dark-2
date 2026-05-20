/**
 * pdgui_menu_agentcreate.cpp -- ImGui replacement for the Agent Create screen.
 *
 * Replaces g_FilemgrEnterNameMenuDialog ("Enter Agent Name") which is pushed
 * by the Agent Select screen when "New Agent..." is chosen.
 *
 * Features:
 *   - Name text input (15 chars max, matching PD's char name[15])
 *   - Body selection carousel with localized names
 *   - Head selection carousel (auto-set from body, user can override)
 *   - Portrait preview placeholder (colored silhouette with initials)
 *   - Create button → saves new agent via filemgrSaveOrLoad
 *   - Cancel button -> returns to Agent Select
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Use extern "C" forward declarations for all game symbols.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_charpreview.h"
#include "pdgui_model_preview.h"
#include "pdgui_layout.h"  /* B-253: docked action bar primitives */
#include "pdgui_nav.h"
#include "system.h"
#include "assetcatalog.h"
#include "menupool.h"
#include "menugraph.h"

/* ========================================================================
 * Forward declarations for game symbols
 * ======================================================================== */

extern "C" {

/* The dialog we're replacing */
extern struct menudialogdef g_FilemgrEnterNameMenuDialog;

/* File list system */
struct filelist;
struct fileguid {
    s32 fileid;
    u16 deviceserial;
};

extern struct filelist *g_FileLists[4];
extern struct fileguid g_GameFileGuid;

/* Game file — agent save data structure */
struct gamefile;
extern struct gamefile g_GameFile;
void gamefileLoadDefaults(struct gamefile *file);

/* g_GameFile.name is at offset 0x00 in the gamefile struct, 11 bytes.
 * The campaign save name, NOT the MP player name (which is 15 bytes in mpchrconfig). */

/* Player config access — via bridge functions in pdgui_bridge.c.
 * We can't include types.h (bool conflict), so pdgui_bridge.c provides
 * safe accessor functions that handle struct layout correctly. */
void mpPlayerConfigSetName(s32 playernum, const char *name);
void mpPlayerConfigSetHeadBody(s32 playernum, const char *head_id, const char *body_id);
const char *mpPlayerConfigGetBodyId(s32 playernum);
const char *mpPlayerConfigGetHeadId(s32 playernum);

extern s32 g_MpPlayerNum;

/* Head/body accessors (defined in mplayer.c) */
char *mpGetBodyName(u8 mpbodynum);
/* Body/head data accessed via catalog accessors (catalogMpBodyId,
 * catalogGetBodyDefaultHead, assetCatalogIterateUnlockedByType, etc.). Both
 * pools read through the catalog with the unlock filter applied -- per
 * Mike's directive "selector pool = catalog INTERSECT unlock-state". */

/* File operations.
 * In PD, "New Agent" creates a campaign GAME save (FILETYPE_GAME).
 * The original flow: enter name → filemgrPushSelectLocationDialog(0, FILETYPE_GAME).
 * On the PC port with a unified save system, we can create the save directly. */
#define FILEOP_SAVE_GAME_000 101
#define FILETYPE_GAME 0
void filemgrPushSelectLocationDialog(s32 arg0, u32 filetype);
s32 filemgrSaveOrLoad(struct fileguid *guid, s32 fileop, uintptr_t playernum);

/* Language strings */
char *langGet(s32 textid);

} /* extern "C" */

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_Registered = false;

/* Agent creation form state */
static char s_AgentName[16] = {0};  /* 15 chars + null */
static s32  s_SelectedBody = 0;
static s32  s_SelectedHead = 0;
static bool s_HeadOverridden = false; /* Has user manually picked a head? */
static bool s_FirstFrame = true;      /* Focus name input on first frame */

/* Frame-scoped flag: set inside the LEFT-pane name input render, read in the
 * docked action bar later in the same frame so Enter from the name field
 * commits Create.  Reset to false at the top of each render pass. */
static bool s_NameEnterPressedThisFrame = false;

/* Head + body pools -- catalog entries filtered by the local player's unlock
 * state, sorted by display name.  Built via assetCatalogIterateUnlockedByType
 * so the carousels only ever show entries the player has actually unlocked.
 * Mod heads / bodies enter the pool automatically (their requirefeature is 0
 * by convention).  Entries with mp_index == -1 (mod / SP fallback) are
 * included -- the catalog ID is the canonical identity, not mp_index.
 *
 * Cache invalidation: rebuilt when the unlocked count differs from the
 * tracked count or when the dialog appears.  Both signals catch unlock
 * state changes mid-session (Unlock All cheat, save load) without paying
 * for a full rebuild every frame.
 */
#define MAX_HEAD_COUNT 128
#define MAX_BODY_COUNT 128
struct HeadEntry {
    char id[CATALOG_ID_LEN];   /* "base:head_carrington", "modid:head_x" */
    char display[64];           /* formatted from id */
};
struct BodyEntry {
    char id[CATALOG_ID_LEN];   /* "base:dark_combat", "modid:body_x" */
    char display[64];           /* mpGetBodyName for mp_index >= 0, else formatCatalogId */
    s32  mp_index;              /* g_MpBodies[] position; -1 for SP / mod bodies */
};
static HeadEntry s_HeadList[MAX_HEAD_COUNT];
static s32       s_HeadListCount       = 0;
static s32       s_HeadListCountKnown  = -1; /* triggers rebuild when stale */
static BodyEntry s_BodyList[MAX_BODY_COUNT];
static s32       s_BodyListCount       = 0;
static s32       s_BodyListCountKnown  = -1; /* triggers rebuild when stale */

/* ========================================================================
 * Helpers
 * ======================================================================== */

/**
 * Format a catalog entry ID to a human-readable display name.
 * Mike directive 2026-05-17: catalog IDs are "namespace:type_name" form
 * (e.g. "base:head_carrington", "base:sp_head_67", "base:body_dark_combat").
 * Pre-fix: this function only stripped the type prefix ("head_" / "body_")
 * but NOT the namespace, so the display read "Base:head Carrington" instead
 * of "Carrington". Order of stripping now:
 *   1. namespace: prefix    -> drops "base:" / "modid:"
 *   2. "sp_head_" / "sp_body_" -> drops the SP marker AND the type prefix in
 *                              one step ("base:sp_head_67" -> "67")
 *   3. type prefix          -> drops "head_" / "body_" for normal entries
 * Then replaces underscores with spaces and title-cases.
 * Pure-numeric remainders (sp_head_67 -> "67") get a readable prefix so
 * the UI doesn't show bare numbers; "head_" prefix becomes "Head 67",
 * "body_" -> "Body 67".
 */
static void formatCatalogId(const char *raw_id, const char *prefix,
                             char *out, size_t outsz)
{
    const char *src = raw_id ? raw_id : "";

    /* Step 1: strip namespace: prefix (everything up to and including the
     * first colon). assetcatalog ID format guarantees at most one colon. */
    const char *colon = strchr(src, ':');
    if (colon) src = colon + 1;

    /* Step 2: handle sp_<type>_ markers as a single unit. The SP single-
     * player entries have ids like "sp_head_67" -- the prefix matches our
     * "head_" naively but we want to drop "sp_head_" entirely so the
     * remainder is just "67" not "67" prefixed with "Sp ". */
    const size_t typePrefixLen = strlen(prefix); /* "head_" or "body_" */
    bool spStripped = false;
    if (strncmp(src, "sp_", 3) == 0 &&
            strncmp(src + 3, prefix, typePrefixLen) == 0) {
        src += 3 + typePrefixLen;
        spStripped = true;
    } else if (strncmp(src, prefix, typePrefixLen) == 0) {
        src += typePrefixLen;
    }

    /* If the remainder is pure-numeric (SP entries are commonly just digits),
     * prefix with the readable type label so the user sees "Head 67" not
     * just "67". */
    bool isNumeric = (*src != '\0');
    for (const char *p = src; *p; p++) {
        if (*p < '0' || *p > '9') { isNumeric = false; break; }
    }
    size_t i = 0;
    if (isNumeric && spStripped) {
        const char *label =
            (strncmp(prefix, "head", 4) == 0) ? "Head " :
            (strncmp(prefix, "body", 4) == 0) ? "Body " : "";
        while (*label && i + 1 < outsz) out[i++] = *label++;
    }

    bool capitalizeNext = true;
    while (*src && i + 1 < outsz) {
        unsigned char c = (unsigned char)*src++;
        if (c == '_') {
            out[i++] = ' ';
            capitalizeNext = true;
        } else if (capitalizeNext) {
            out[i++] = (char)toupper(c);
            capitalizeNext = false;
        } else {
            out[i++] = (char)tolower(c);
        }
    }
    out[i] = '\0';
}

static int s_compareHeadByName(const void *a, const void *b)
{
    return strcmp(((const HeadEntry *)a)->display,
                  ((const HeadEntry *)b)->display);
}

/* Iteration callback for assetCatalogIterateUnlockedByType.  Appends to
 * s_HeadList[] while count < MAX_HEAD_COUNT. */
static void s_collectUnlockedHead(const asset_entry_t *e, void *userdata)
{
    (void)userdata;
    if (s_HeadListCount >= MAX_HEAD_COUNT) return;
    HeadEntry *h = &s_HeadList[s_HeadListCount++];
    strncpy(h->id, e->id, sizeof(h->id) - 1);
    h->id[sizeof(h->id) - 1] = '\0';
    /* formatCatalogId strips the legacy "head_" prefix only.  Catalog IDs
     * have a "namespace:" prefix ("base:head_carrington",
     * "base:sp_head_67") which is preserved in the display today.  Display
     * polish (strip namespace + handle sp_head_) is a follow-up. */
    formatCatalogId(e->id, "head_", h->display, sizeof(h->display));
}

/* Rebuild s_HeadList from the catalog, filtered by unlock-state.  O(N) over
 * the full catalog pool.  Called when the dialog appears or when the
 * unlocked count changes (cheap detection signal for catalog mutations). */
static void rebuildHeadSortMap(void)
{
    s_HeadListCount = 0;
    assetCatalogIterateUnlockedByType(ASSET_HEAD, s_collectUnlockedHead, NULL);
    if (s_HeadListCount > 1) {
        qsort(s_HeadList, (size_t)s_HeadListCount, sizeof(s_HeadList[0]),
              s_compareHeadByName);
    }
    s_HeadListCountKnown = s_HeadListCount;
}

/* Find list position for a catalog head ID.  Returns 0 if not found. */
static s32 findHeadIndexById(const char *head_id)
{
    if (!head_id || !head_id[0]) return 0;
    for (s32 i = 0; i < s_HeadListCount; i++) {
        if (strcmp(s_HeadList[i].id, head_id) == 0) return i;
    }
    return 0;
}

/* Get the display name for the head at list position pos. */
static const char *getHeadDisplayName(s32 pos)
{
    if (pos < 0 || pos >= s_HeadListCount) return "Head ???";
    return s_HeadList[pos].display;
}

/* ----- Body pool ----- */

static int s_compareBodyByName(const void *a, const void *b)
{
    return strcmp(((const BodyEntry *)a)->display,
                  ((const BodyEntry *)b)->display);
}

/* Iteration callback for assetCatalogIterateUnlockedByType. Appends to
 * s_BodyList[] while count < MAX_BODY_COUNT.  Display name preference:
 *   - mp_index >= 0: mpGetBodyName preserves langbank + B-226 catalog
 *     overrides (Bond actors, Skedar, Dr. Caroll).
 *   - mp_index <  0: formatCatalogId on the entry id ("base:sp_body_67"
 *     becomes "Base:sp Body 67"; cosmetic but legible).  Display polish
 *     for SP body human names is a follow-up. */
static void s_collectUnlockedBody(const asset_entry_t *e, void *userdata)
{
    (void)userdata;
    if (s_BodyListCount >= MAX_BODY_COUNT) return;
    BodyEntry *b = &s_BodyList[s_BodyListCount++];
    strncpy(b->id, e->id, sizeof(b->id) - 1);
    b->id[sizeof(b->id) - 1] = '\0';
    b->mp_index = (s32)e->mp_index;
    b->display[0] = '\0';
    if (b->mp_index >= 0) {
        char *raw = mpGetBodyName((u8)b->mp_index);
        if (raw && raw[0]) {
            strncpy(b->display, raw, sizeof(b->display) - 1);
            b->display[sizeof(b->display) - 1] = '\0';
        }
    }
    if (!b->display[0]) {
        formatCatalogId(e->id, "body_", b->display, sizeof(b->display));
    }
}

/* Rebuild s_BodyList from the catalog, filtered by unlock-state.  O(N) over
 * the full catalog pool.  Called when the dialog appears or when the
 * unlocked count changes (cheap detection signal for catalog mutations). */
static void rebuildBodySortMap(void)
{
    s_BodyListCount = 0;
    assetCatalogIterateUnlockedByType(ASSET_BODY, s_collectUnlockedBody, NULL);
    if (s_BodyListCount > 1) {
        qsort(s_BodyList, (size_t)s_BodyListCount, sizeof(s_BodyList[0]),
              s_compareBodyByName);
    }
    s_BodyListCountKnown = s_BodyListCount;
}

/* Find list position for a catalog body ID. Returns 0 if not found. */
static s32 findBodyIndexById(const char *body_id)
{
    if (!body_id || !body_id[0]) return 0;
    for (s32 i = 0; i < s_BodyListCount; i++) {
        if (strcmp(s_BodyList[i].id, body_id) == 0) return i;
    }
    return 0;
}

/* True when the body at list position pos has an integrated head model
 * (unk00_01 == 1) -- e.g. Dr. Carroll, Skedar, Eye Spy.  These bodies
 * cannot accept a separate head; the head carousel is disabled. */
static bool s_bodyHasIntegratedHead(s32 bodyListPos)
{
    if (bodyListPos < 0 || bodyListPos >= s_BodyListCount) return false;
    const asset_entry_t *be = assetCatalogResolve(s_BodyList[bodyListPos].id);
    if (!be || be->type != ASSET_BODY) return false;
    /* runtime_index for body entries is the g_HeadsAndBodies[] index. */
    return catalogGetBodyIsComplete(be->runtime_index) ? true : false;
}

/* Get the display name for the body at list position pos. */
static const char *getBodyDisplayName(s32 bodyIdx)
{
    if (bodyIdx < 0 || bodyIdx >= s_BodyListCount) return "???";
    return s_BodyList[bodyIdx].display;
}

/* Auto-select the head paired with the current body.
 *
 * Uses catalogGetBodyDefaultHead to get the body's declared default head as
 * a catalog ID string, then finds its position in the unlocked list.  If
 * the default head is locked it won't be in the list -- findHeadIndexById
 * returns 0, the carousel falls to the first available unlocked head.
 * This is acceptable because the carousel only EVER contains unlocked
 * entries, so any selection is guaranteed loadable. */
static void autoSelectHead(void)
{
    if (s_HeadOverridden) return;
    if (s_SelectedBody < 0 || s_SelectedBody >= s_BodyListCount) return;
    const char *bid = s_BodyList[s_SelectedBody].id;
    if (!bid || !bid[0]) return;
    const char *defaultHeadId = catalogGetBodyDefaultHead(bid);
    if (!defaultHeadId) return;
    s_SelectedHead = findHeadIndexById(defaultHeadId);
}

/**
 * 3/4-turn body angle in radians.  Shows mostly the front with a hint of
 * profile so the head + shoulder silhouette read clearly.  Negative offset
 * twists the model so the right shoulder leads (matching N64 PD's
 * character-select pose convention).
 */
#define AGENTCREATE_PREVIEW_ROTY  (-0.45f)

/* ========================================================================
 * ImGui Render Callback
 * ======================================================================== */

static s32 renderAgentCreate(struct menudialog *dialog,
                              struct menu *menu,
                              s32 winW, s32 winH)
{
    /* Refresh counts each frame in case mods or unlock-state change them */
    s32 unlockedHeadCount = assetCatalogGetUnlockedCountByType(ASSET_HEAD);
    s32 unlockedBodyCount = assetCatalogGetUnlockedCountByType(ASSET_BODY);

    /* Rebuild head pool when the unlocked count changed.  Initial state
     * (s_HeadListCountKnown == -1) always triggers the first-time build.
     * Subsequent unlock-state changes (Unlock All cheat toggle, save load)
     * change the count and trigger rebuild.  A catalog mutation that
     * preserves count but swaps IDs is not detected -- acceptable for a
     * settings dialog; matches the arena builder's one-shot pattern. */
    if (s_HeadListCountKnown != unlockedHeadCount) {
        rebuildHeadSortMap();
        pdguiModelPreviewInvalidate();
    }
    /* Same delta-detection for the body pool. */
    if (s_BodyListCountKnown != unlockedBodyCount) {
        rebuildBodySortMap();
        pdguiModelPreviewInvalidate();
    }

    /* Clamp selections */
    if (s_SelectedBody >= s_BodyListCount) s_SelectedBody = s_BodyListCount - 1;
    if (s_SelectedBody < 0) s_SelectedBody = 0;
    if (s_SelectedHead >= s_HeadListCount) s_SelectedHead = s_HeadListCount - 1;
    if (s_SelectedHead < 0) s_SelectedHead = 0;

    /* ---- Layout ---- */
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;

    /* PD-authentic title bar height. 39px @ 1080p baseline matches the rest
     * of the Phase-3 menus (cf. pdgui_menu_playerconfig.cpp). */
    float pdTitleH = pdguiScale(39.0f);

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##agent_create", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    /* Auto-possess on first appearance */
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_FirstFrame = true;
        /* Force preview re-render on screen open */
        pdguiModelPreviewInvalidate();

        /* B-297 (S593): seed body/head from the player's currently-saved
         * config rather than the alphabetically-first unlocked entry.
         *
         * Why this matters: the previous default (s_SelectedBody=0,
         * s_SelectedHead=0) picked alphabetically-first body and head
         * from the unlocked pool.  Those two are picked INDEPENDENTLY,
         * so on certain mod / unlock combinations the pair could be
         * rig-incompatible (e.g. integrated-head body + non-integrated
         * head, or maian body + human head).  When that happened the
         * preview's request seam (`pdguiCharPreviewRequestEx`) would
         * apply rig fallback or integrated-head clearing -- valid for
         * the renderer, but the body itself could still hit a load
         * problem and `body0f02ce8c` would log a WARNING and skip the
         * model.  Result: FBO cleared to black, `s_PreviewReady` still
         * goes to 1, ImGui drew the black texture and Mike saw "just
         * black, nothing visible for preview".
         *
         * The Player Config menu (`pdgui_menu_playerconfig.cpp`) starts
         * from `mpPlayerConfigGetBodyId/HeadId` -- a known-valid pair
         * (it's what the player is currently using in MP) -- and its
         * preview always renders correctly.  Mirror that pattern here
         * so Agent Create starts from the same known-good baseline.
         * The user can still cycle to any unlocked body/head; this
         * just changes the OPENING selection. */
        s32 pnum = g_MpPlayerNum;
        if (pnum < 0) pnum = 0;
        const char *playerBodyId = mpPlayerConfigGetBodyId(pnum);
        const char *playerHeadId = mpPlayerConfigGetHeadId(pnum);
        if (playerBodyId && playerBodyId[0]) {
            s_SelectedBody = findBodyIndexById(playerBodyId);
        }
        if (playerHeadId && playerHeadId[0]) {
            s_SelectedHead = findHeadIndexById(playerHeadId);
        }
        /* Treat the seed as user-implicit so autoSelectHead does NOT
         * override head choice on the next body cycle.  Once the user
         * cycles bodies the head will auto-pair from the new body's
         * default head per the existing `s_HeadOverridden = false`
         * branch in the body carousel. */
        s_HeadOverridden = (playerHeadId && playerHeadId[0]) ? true : false;
    }

    /* Opaque backdrop — this dialog overlays Agent Select, so the body
     * must not be see-through. Draw a solid dark fill before the PD frame. */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          pdguiPalImU32(PDPAL_BODYBG, 255), 0.0f);
    }

    /* Draw PD-authentic dialog frame */
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH,
                      "Create Agent", 1);

    /* Content starts below PD title bar */
    pdguiSetCursorBelowTitle(pdTitleH);

    float pad = pdguiScale(16.0f);

    /* Reserve room for the docked action bar at the bottom. */
    float availY = ImGui::GetContentRegionAvail().y;
    float bodyH  = pdguiBodyHeightForActionBar(availY);

    /* ================================================================
     * Two-column body: LEFT = controls (1/2 width), RIGHT = 3D pane
     * (1/2 width).  The 3D pane is square, vertically centered.
     * ================================================================ */
    if (ImGui::BeginChild("##ac_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {

        /* B-253 follow-up: layout is content-sized on the left, fills-rest
         * on the right.  Mike's brief: "left panel sized to its content
         * (~280-320px), right panel fills the rest of the window".  The
         * left holds 3 control rows (Name input + Body carousel + Head
         * carousel + auto-match line) + ~16px row gaps.  At pdguiScale
         * 1.0 (1080p baseline) ~320px is comfortable; we scale so the
         * value tracks viewport size. */
        float contentW = ImGui::GetContentRegionAvail().x;
        float colGap   = pdguiScale(20.0f);
        float leftW    = pdguiScale(320.0f);
        if (leftW > contentW * 0.5f) leftW = contentW * 0.5f;  /* sanity cap */
        float rightW   = contentW - colGap - leftW;

        ImVec2 colsOrigin = ImGui::GetCursorScreenPos();

        /* ============================================================
         * LEFT: Control rows
         * ============================================================ */
        ImGui::BeginGroup();
        {
            ImGui::PushItemWidth(leftW);

            /* ----- Agent Name ----- */
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Agent Name");
            ImGui::Spacing();

            if (s_FirstFrame) {
                ImGui::SetKeyboardFocusHere();
                s_FirstFrame = false;
            }

            ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
            bool nameEntered = ImGui::InputText("##agent_name", s_AgentName,
                                                 sizeof(s_AgentName), inputFlags);
            ImGui::Dummy(ImVec2(0, pdguiScale(8.0f)));

            /* ----- Character (Body) carousel ----- */
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Character");
            ImGui::Spacing();

            float arrowW = pdguiScale(28.0f);
            float bodyNameW = leftW - arrowW * 2.0f - pdguiScale(80.0f);
            if (bodyNameW < pdguiScale(80.0f)) bodyNameW = pdguiScale(80.0f);

            if (ImGui::ArrowButton("##body_prev", ImGuiDir_Left)) {
                s_SelectedBody--;
                if (s_SelectedBody < 0) s_SelectedBody = s_BodyListCount - 1;
                s_HeadOverridden = false;
                autoSelectHead();
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
            ImGui::SameLine();

            {
                const char *bodyName = getBodyDisplayName(s_SelectedBody);
                ImVec2 cur = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(bodyNameW, ImGui::GetFrameHeight()));
                ImVec2 sz = ImGui::CalcTextSize(bodyName);
                float ty = cur.y + (ImGui::GetFrameHeight() - sz.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(cur.x + (bodyNameW - sz.x) * 0.5f, ty),
                    pdguiPalImU32(PDPAL_TITLEFG, 255), bodyName);
            }
            ImGui::SameLine();

            if (ImGui::ArrowButton("##body_next", ImGuiDir_Right)) {
                s_SelectedBody++;
                if (s_SelectedBody >= s_BodyListCount) s_SelectedBody = 0;
                s_HeadOverridden = false;
                autoSelectHead();
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%d/%d)", s_SelectedBody + 1, s_BodyListCount);

            ImGui::Dummy(ImVec2(0, pdguiScale(8.0f)));

            /* ----- Head carousel ----- */
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Head");
            ImGui::Spacing();

            /* H.5 (audit): bodies with an integrated head model (Dr. Carroll,
             * Skedar, Eye Spy) cannot accept a separate head.  Disable the
             * carousel; the body's own head will render. */
            bool integratedHead = s_bodyHasIntegratedHead(s_SelectedBody);
            ImGui::BeginDisabled(integratedHead || s_HeadListCount <= 1);

            if (ImGui::ArrowButton("##head_prev", ImGuiDir_Left)) {
                s_SelectedHead--;
                if (s_SelectedHead < 0) s_SelectedHead = s_HeadListCount - 1;
                s_HeadOverridden = true;
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
            ImGui::SameLine();

            {
                const char *headName = integratedHead
                    ? "(integrated)"
                    : getHeadDisplayName(s_SelectedHead);
                ImVec2 cur = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(bodyNameW, ImGui::GetFrameHeight()));
                ImVec2 sz = ImGui::CalcTextSize(headName);
                float ty = cur.y + (ImGui::GetFrameHeight() - sz.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(cur.x + (bodyNameW - sz.x) * 0.5f, ty),
                    pdguiPalImU32(PDPAL_TITLEFG, 255), headName);
            }
            ImGui::SameLine();

            if (ImGui::ArrowButton("##head_next", ImGuiDir_Right)) {
                s_SelectedHead++;
                if (s_SelectedHead >= s_HeadListCount) s_SelectedHead = 0;
                s_HeadOverridden = true;
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
            ImGui::SameLine();
            if (integratedHead) {
                ImGui::TextDisabled("(N/A)");
            } else {
                ImGui::TextDisabled("(%d/%d)", s_SelectedHead + 1, s_HeadListCount);
            }

            ImGui::EndDisabled();

            /* Auto-match indicator / Reset button (skipped for integrated bodies). */
            if (!integratedHead) {
                if (!s_HeadOverridden) {
                    ImGui::TextDisabled("(auto-matched to character)");
                } else {
                    if (ImGui::SmallButton("Auto-match")) {
                        s_HeadOverridden = false;
                        autoSelectHead();
                        pdguiPlaySound(PDGUI_SND_TOGGLEON);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("Reset head to character default");
                }
            }

            /* Capture Enter-press for the action bar later this frame. */
            s_NameEnterPressedThisFrame = nameEntered;

            ImGui::PopItemWidth();
        }
        ImGui::EndGroup();

        /* ============================================================
         * RIGHT: 3D render pane (1/2 width, square, vertically centered)
         * ============================================================ */
        ImGui::SameLine(0.0f, colGap);

        {
            /* B-253 follow-up: pane fills the full remaining rectangle.
             * pdgui_model_preview computes the aspect ratio from these
             * dimensions and passes it to the FBO renderer, so the model
             * appears with correct proportions even though the FBO itself
             * is square (square FBO + matching projection aspect cancel
             * out into the displayed rect cleanly). */
            float paneW = rightW;
            float paneH = bodyH - pdguiScale(8.0f);
            float px = colsOrigin.x + leftW + colGap;
            float py = colsOrigin.y;

            /* Live preview reads from the unlocked-and-filtered s_HeadList /
             * s_BodyList, not from raw mp_idx -- the catalog ID is the
             * canonical identity for the renderer.  For integrated-head
             * bodies the head_id is empty; pdguiModelPreviewDraw falls back
             * to the body's own integrated head. */
            const char *hid = (s_HeadListCount > 0)
                ? s_HeadList[s_SelectedHead].id
                : NULL;
            const char *bid = (s_BodyListCount > 0)
                ? s_BodyList[s_SelectedBody].id
                : NULL;

            /* Fix the body at a 3/4-turn pose.  We disable idle rotation in
             * pdgui_model_preview's options so the model is static; pose
             * orientation is set explicitly via pdguiCharPreviewSetRotY()
             * BEFORE the request fires. */
            pdguiCharPreviewSetRotY(AGENTCREATE_PREVIEW_ROTY);

            ModelPreviewOpts opts = pdguiModelPreviewDefaultOpts();
            opts.showBodyName = 1;
            opts.showHeadName = 1;
            opts.idleRotation = 0;
            opts.cornerRadius = pdguiScale(6.0f);

            pdguiModelPreviewDraw(hid, bid, px, py, paneW, paneH, &opts);

            /* Reserve cursor space so the layout child reports a sane size. */
            ImGui::Dummy(ImVec2(rightW, paneH));
        }
    }
    ImGui::EndChild();

    /* ================================================================
     * Action bar: Create + Cancel
     * ================================================================ */
    bool doCreate = false;
    bool doCancel = false;

    bool nameValid = (s_AgentName[0] != '\0');

    if (pdguiBeginActionBar("##ac_actionbar")) {
        float avail = ImGui::GetContentRegionAvail().x;
        float btnW  = (avail - pad) * 0.5f;

        /* Create — disabled when name is empty */
        if (!nameValid) ImGui::BeginDisabled();
        if (pdguiActionBarButton("Create", 1, btnW)) {
            doCreate = true;
        }
        if (!nameValid) ImGui::EndDisabled();

        ImGui::SameLine(0.0f, pad);

        if (pdguiActionBarButton("Cancel", 0, ImGui::GetContentRegionAvail().x)) {
            doCancel = true;
        }
    }
    pdguiEndActionBar();

    /* Allow Enter from the name field to create. */
    if (s_NameEnterPressedThisFrame && nameValid) {
        doCreate = true;
    }
    s_NameEnterPressedThisFrame = false;  /* reset for next frame */

    /* B / Escape cancels at the top level. */
    if (!ImGui::IsWindowAppearing() &&
        pdguiMenuCancelPressed()) {
        doCancel = true;
    }

    /* ---- Execute Create ---- */
    if (doCreate && nameValid) {
        pdguiPlaySound(PDGUI_SND_SUCCESS);

        /* Write name into g_GameFile.name (first 11 bytes of the struct).
         * This is the campaign save file name — what appears in the
         * Agent Select list. Truncate to 10 chars + null. */
        char *gfName = (char *)&g_GameFile;
        strncpy(gfName, s_AgentName, 10);
        gfName[10] = '\0';

        /* Set head and body on the active player config so the save
         * includes this data.  The agent is NOT loaded for play — the
         * player must still select it from Agent Select. */
        s32 pnum = g_MpPlayerNum;
        if (pnum < 0) pnum = 0;
        {
            const char *hid = (s_HeadListCount > 0)
                ? s_HeadList[s_SelectedHead].id
                : "";
            const char *bid = (s_BodyListCount > 0)
                ? s_BodyList[s_SelectedBody].id
                : "";
            mpPlayerConfigSetHeadBody(pnum, hid, bid);
        }
        mpPlayerConfigSetName(pnum, s_AgentName);

        /* Pop the Agent Create dialog to return to Agent Select */
        menuGraphFirePop(MENU_TYPE_AGENT_CREATE, "save");

        filemgrPushSelectLocationDialog(0, FILETYPE_GAME);

        sysLogPrintf(LOG_NOTE, "pdgui_agentcreate: Saved new agent '%s' "
                     "body_id=\"%s\" head_id=\"%s\" (not loaded, requires selection)",
                     s_AgentName,
                     (s_BodyListCount > 0) ? s_BodyList[s_SelectedBody].id : "",
                     (s_HeadListCount > 0) ? s_HeadList[s_SelectedHead].id : "");

        s_AgentName[0] = '\0';
        s_SelectedBody = 0;
        s_SelectedHead = 0;
        s_HeadOverridden = false;
    }

    /* ---- Execute Cancel ---- */
    if (doCancel) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuGraphFirePop(MENU_TYPE_AGENT_CREATE, "cancel");

        s_AgentName[0] = '\0';
        s_SelectedBody = 0;
        s_SelectedHead = 0;
        s_HeadOverridden = false;
    }

    ImGui::End();
    return 1;  /* Handled */
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMenuAgentCreateRegister(void)
{
    if (s_Registered) return;

    pdguiHotswapRegister(
        &g_FilemgrEnterNameMenuDialog,
        renderAgentCreate,
        "Agent Create"
    );

    s_Registered = true;
    sysLogPrintf(LOG_NOTE, "pdgui_menu_agentcreate: Registered");
}

} /* extern "C" */
