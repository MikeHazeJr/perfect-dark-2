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
 *   - Cancel button → pops back to Agent Select
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
#include "system.h"
#include "assetcatalog.h"

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

extern s32 g_MpPlayerNum;

/* Head/body accessors (defined in mplayer.c) */
s32 mpGetNumHeads2(void);
s32 mpGetNumHeads(void);
s32 mpGetHeadId(u8 headnum);
u32 mpGetNumBodies(void);
s32 mpGetBodyId(u8 bodynum);
char *mpGetBodyName(u8 mpbodynum);
s32 catalogGetBodyDefaultMpHeadIdx(s32 mpbodynum);
/* Body/head data accessed via catalog accessors (catalogMpBodyId, catalogMpHeadId) */

/* Feature checking — unlock system */
s32 mpGetHeadRequiredFeature(u8 headnum);
s32 mpGetBodyRequiredFeature(u8 bodynum);

/* File operations.
 * In PD, "New Agent" creates a campaign GAME save (FILETYPE_GAME).
 * The original flow: enter name → filemgrPushSelectLocationDialog(0, FILETYPE_GAME).
 * On the PC port with a unified save system, we can create the save directly. */
#define FILEOP_SAVE_GAME_000 101
#define FILETYPE_GAME 0
void filemgrPushSelectLocationDialog(s32 arg0, u32 filetype);
s32 filemgrSaveOrLoad(struct fileguid *guid, s32 fileop, uintptr_t playernum);

/* Menu stack */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

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

/* Cached counts (refreshed each frame) */
static s32 s_NumHeads = 0;
static s32 s_NumBodies = 0;

/* Frame-scoped flag: set inside the LEFT-pane name input render, read in the
 * docked action bar later in the same frame so Enter from the name field
 * commits Create.  Reset to false at the top of each render pass. */
static bool s_NameEnterPressedThisFrame = false;

/* Head sort map — alphabetically sorted by display name */
#define MAX_HEAD_COUNT 128
static s32  s_SortedHeadIndices[MAX_HEAD_COUNT];
static char s_HeadDisplayNames[MAX_HEAD_COUNT][64];
static s32  s_SortedHeadCount    = 0;
static s32  s_SortedHeadNumHeads = 0; /* triggers rebuild when != s_NumHeads */

/* ========================================================================
 * Helpers
 * ======================================================================== */

/**
 * Format a catalog entry ID to a human-readable display name.
 * Strips prefix, replaces underscores with spaces, title-cases each word.
 * e.g. "head_dark_combat" with prefix "head_" -> "Dark Combat"
 */
static void formatCatalogId(const char *raw_id, const char *prefix,
                             char *out, size_t outsz)
{
    const char *src = raw_id ? raw_id : "";
    size_t prefixLen = strlen(prefix);
    if (strncmp(src, prefix, prefixLen) == 0)
        src += prefixLen;
    bool capitalizeNext = true;
    size_t i = 0;
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
    return strcmp(s_HeadDisplayNames[*(const s32 *)a],
                  s_HeadDisplayNames[*(const s32 *)b]);
}

/**
 * Rebuild the sorted head index map.
 * s_SortedHeadIndices[i] is the mpheadnum at sorted position i.
 * s_HeadDisplayNames[mpheadnum] is the formatted display name.
 */
static void rebuildHeadSortMap(void)
{
    s32 n = s_NumHeads;
    if (n > MAX_HEAD_COUNT) n = MAX_HEAD_COUNT;
    s_SortedHeadCount = n;
    for (s32 i = 0; i < n; i++) {
        s_SortedHeadIndices[i] = i;
        const char *catId = catalogMpHeadId(i);
        if (catId)
            formatCatalogId(catId, "head_",
                            s_HeadDisplayNames[i], sizeof(s_HeadDisplayNames[i]));
        else
            snprintf(s_HeadDisplayNames[i], sizeof(s_HeadDisplayNames[i]),
                     "Head %d", i);
    }
    qsort(s_SortedHeadIndices, n, sizeof(s_SortedHeadIndices[0]),
          s_compareHeadByName);
    s_SortedHeadNumHeads = s_NumHeads;
}

/**
 * Find the sorted-list position for a given mpheadnum.
 * Returns 0 if not found.
 */
static s32 findSortedPosForMpHeadnum(s32 mpheadnum)
{
    for (s32 i = 0; i < s_SortedHeadCount; i++) {
        if (s_SortedHeadIndices[i] == mpheadnum) return i;
    }
    return 0;
}

/**
 * Get the display name for the head at sorted position sortedPos.
 * Returns a pointer into s_HeadDisplayNames (stable until rebuildHeadSortMap).
 */
static const char *getHeadDisplayName(s32 sortedPos)
{
    if (sortedPos < 0 || sortedPos >= s_SortedHeadCount)
        return "Head ???";
    return s_HeadDisplayNames[s_SortedHeadIndices[sortedPos]];
}

/**
 * Get body display name via the game's localization system.
 */
static const char *getBodyDisplayName(s32 bodyIdx)
{
    if (bodyIdx < 0 || bodyIdx >= s_NumBodies) {
        return "???";
    }
    char *name = mpGetBodyName((u8)bodyIdx);
    return name ? name : "???";
}

/**
 * Auto-select a head that matches the current body.
 * Uses the same logic as the original game.
 */
static void autoSelectHead(void)
{
    if (!s_HeadOverridden && s_SelectedBody >= 0 && s_SelectedBody < s_NumBodies) {
        s32 mpheadnum = catalogGetBodyDefaultMpHeadIdx(s_SelectedBody);
        if (mpheadnum >= 0 && mpheadnum < s_NumHeads)
            s_SelectedHead = findSortedPosForMpHeadnum(mpheadnum);
    }
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
    /* Refresh counts each frame in case mods change them */
    s_NumHeads = mpGetNumHeads2();
    s_NumBodies = (s32)mpGetNumBodies();

    /* Rebuild head sort map if head count changed */
    if (s_SortedHeadNumHeads != s_NumHeads) {
        rebuildHeadSortMap();
        pdguiModelPreviewInvalidate();
    }

    /* Clamp selections */
    if (s_SelectedBody >= s_NumBodies) s_SelectedBody = s_NumBodies - 1;
    if (s_SelectedBody < 0) s_SelectedBody = 0;
    if (s_SelectedHead >= s_SortedHeadCount) s_SelectedHead = s_SortedHeadCount - 1;
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

        float contentW = ImGui::GetContentRegionAvail().x;
        float colGap   = pdguiScale(20.0f);
        float leftW    = (contentW - colGap) * 0.5f;
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
                if (s_SelectedBody < 0) s_SelectedBody = s_NumBodies - 1;
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
                if (s_SelectedBody >= s_NumBodies) s_SelectedBody = 0;
                s_HeadOverridden = false;
                autoSelectHead();
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%d/%d)", s_SelectedBody + 1, s_NumBodies);

            ImGui::Dummy(ImVec2(0, pdguiScale(8.0f)));

            /* ----- Head carousel ----- */
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Head");
            ImGui::Spacing();

            if (ImGui::ArrowButton("##head_prev", ImGuiDir_Left)) {
                s_SelectedHead--;
                if (s_SelectedHead < 0) s_SelectedHead = s_SortedHeadCount - 1;
                s_HeadOverridden = true;
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
            ImGui::SameLine();

            {
                const char *headName = getHeadDisplayName(s_SelectedHead);
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
                if (s_SelectedHead >= s_SortedHeadCount) s_SelectedHead = 0;
                s_HeadOverridden = true;
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%d/%d)", s_SelectedHead + 1, s_SortedHeadCount);

            /* Auto-match indicator / Reset button */
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
            float paneW = rightW;
            float paneH = bodyH - pdguiScale(8.0f);
            /* Keep the render box square; cap at the smaller dimension. */
            float side = (paneW < paneH) ? paneW : paneH;
            float px = colsOrigin.x + leftW + colGap + (paneW - side) * 0.5f;
            float py = colsOrigin.y + (paneH - side) * 0.5f;

            const char *hid = catalogMpHeadId(s_SortedHeadIndices[s_SelectedHead]);
            const char *bid = catalogMpBodyId(s_SelectedBody);

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

            pdguiModelPreviewDraw(hid, bid, px, py, side, side, &opts);

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
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
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
            const char *hid = catalogMpHeadId(s_SortedHeadIndices[s_SelectedHead]);
            const char *bid = catalogMpBodyId(s_SelectedBody);
            mpPlayerConfigSetHeadBody(pnum, hid ? hid : "", bid ? bid : "");
        }
        mpPlayerConfigSetName(pnum, s_AgentName);

        /* Pop the Agent Create dialog to return to Agent Select */
        menuPopDialog();

        filemgrPushSelectLocationDialog(0, FILETYPE_GAME);

        sysLogPrintf(LOG_NOTE, "pdgui_agentcreate: Saved new agent '%s' "
                     "body=%d head=%d (not loaded, requires selection)",
                     s_AgentName, s_SelectedBody,
                     s_SortedHeadIndices[s_SelectedHead]);

        s_AgentName[0] = '\0';
        s_SelectedBody = 0;
        s_SelectedHead = 0;
        s_HeadOverridden = false;
    }

    /* ---- Execute Cancel ---- */
    if (doCancel) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();

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
