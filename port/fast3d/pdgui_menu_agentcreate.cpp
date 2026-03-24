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

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_charpreview.h"
#include "system.h"

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
void mpPlayerConfigSetHeadBody(s32 playernum, u8 headnum, u8 bodynum);

extern s32 g_MpPlayerNum;

/* Head/body accessors (defined in mplayer.c) */
s32 mpGetNumHeads2(void);
s32 mpGetNumHeads(void);
s32 mpGetHeadId(u8 headnum);
u32 mpGetNumBodies(void);
s32 mpGetBodyId(u8 bodynum);
char *mpGetBodyName(u8 mpbodynum);
s32 mpGetMpheadnumByMpbodynum(s32 mpbodynum);

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

/* ========================================================================
 * Helpers
 * ======================================================================== */

/**
 * Get a display-friendly name for a head index.
 * PD heads don't have localized names in the base game — they're identified
 * by HEAD_* constant. We show a short description derived from the constant.
 * For now, show "Head XX" with the internal ID. Future: map to descriptive names.
 */
static void getHeadDisplayName(char *buf, size_t bufsize, s32 headIdx)
{
    if (headIdx < 0 || headIdx >= s_NumHeads) {
        snprintf(buf, bufsize, "Head ???");
        return;
    }
    s32 headId = mpGetHeadId((u8)headIdx);
    snprintf(buf, bufsize, "Head %d", headId);
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
        s32 headIdx = mpGetMpheadnumByMpbodynum(s_SelectedBody);
        if (headIdx >= 0 && headIdx < s_NumHeads) {
            s_SelectedHead = headIdx;
        }
    }
}

/* Track previous head/body to detect changes */
static s32 s_PrevPreviewHead = -1;
static s32 s_PrevPreviewBody = -1;

/**
 * Draw the character portrait preview.
 *
 * If the 3D render-to-texture system has produced a valid preview,
 * displays the actual character model render via ImGui::Image.
 * Otherwise, falls back to a placeholder silhouette.
 */
static void drawPortraitPreview(ImDrawList *dl, float x, float y,
                                 float size, float scale)
{
    /* Request a new preview render if head/body changed */
    if (s_SelectedHead != s_PrevPreviewHead ||
        s_SelectedBody != s_PrevPreviewBody) {
        pdguiCharPreviewRequest((u8)s_SelectedHead, (u8)s_SelectedBody);
        s_PrevPreviewHead = s_SelectedHead;
        s_PrevPreviewBody = s_SelectedBody;
    }

    /* Center X — used for both the preview content and the body name label */
    float cx = x + size * 0.5f;

    /* Background frame — always drawn behind the preview */
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size),
                      IM_COL32(10, 15, 30, 240), 4.0f * scale);
    dl->AddRect(ImVec2(x, y), ImVec2(x + size, y + size),
                IM_COL32(60, 100, 180, 200), 4.0f * scale, 0, 2.0f * scale);

    /* Try to display the 3D rendered preview */
    u32 texId = pdguiCharPreviewGetTextureId();
    if (texId != 0 && pdguiCharPreviewIsReady()) {
        /* Display the FBO texture via ImGui::Image.
         * The texture ID is a GL texture handle cast to ImTextureID. */
        ImVec2 uv0(0.0f, 1.0f);  /* Flip Y — FBO textures are upside-down */
        ImVec2 uv1(1.0f, 0.0f);
        dl->AddImage((ImTextureID)(uintptr_t)texId,
                     ImVec2(x + 2.0f, y + 2.0f),
                     ImVec2(x + size - 2.0f, y + size - 2.0f),
                     uv0, uv1);
    } else {
        /* Fallback: placeholder silhouette */
        float cy = y + size * 0.4f;
        float headR = size * 0.15f;

        dl->AddCircleFilled(ImVec2(cx, cy), headR,
                            IM_COL32(50, 80, 140, 200), 24);
        dl->AddRectFilled(
            ImVec2(cx - size * 0.3f, cy + headR * 0.8f),
            ImVec2(cx + size * 0.3f, cy + headR * 0.8f + size * 0.25f),
            IM_COL32(50, 80, 140, 200), headR);

        /* Initials */
        char initials[4] = {0};
        if (s_AgentName[0]) {
            initials[0] = s_AgentName[0];
            for (int i = 1; s_AgentName[i]; i++) {
                if (s_AgentName[i] == ' ' && s_AgentName[i + 1]) {
                    initials[1] = s_AgentName[i + 1];
                    break;
                }
            }
            if (!initials[1] && s_AgentName[1]) {
                initials[1] = s_AgentName[1];
            }
        } else {
            initials[0] = '?';
        }

        ImVec2 textSize = ImGui::CalcTextSize(initials);
        float textY = y + size * 0.72f;
        dl->AddText(ImVec2(cx - textSize.x * 0.5f, textY),
                    IM_COL32(180, 210, 255, 255), initials);
    }

    /* Body name below portrait */
    const char *bodyName = getBodyDisplayName(s_SelectedBody);
    ImVec2 bodyNameSize = ImGui::CalcTextSize(bodyName);
    dl->AddText(ImVec2(cx - bodyNameSize.x * 0.5f, y + size + 4.0f * scale),
                IM_COL32(140, 160, 200, 180), bodyName);
}

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

    /* Clamp selections */
    if (s_SelectedBody >= s_NumBodies) s_SelectedBody = s_NumBodies - 1;
    if (s_SelectedBody < 0) s_SelectedBody = 0;
    if (s_SelectedHead >= s_NumHeads) s_SelectedHead = s_NumHeads - 1;
    if (s_SelectedHead < 0) s_SelectedHead = 0;

    /* ---- Layout ---- */
    float scale = pdguiScaleFactor();
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;

    /* PD-authentic title bar height */
    float pdTitleH = dialogH * 0.07f;
    if (pdTitleH < 20.0f) pdTitleH = 20.0f;
    if (pdTitleH > 30.0f) pdTitleH = 30.0f;

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
        s_PrevPreviewHead = -1;
        s_PrevPreviewBody = -1;
    }

    /* Opaque backdrop — this dialog overlays Agent Select, so the body
     * must not be see-through. Draw a solid dark fill before the PD frame. */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(8, 8, 16, 255), 0.0f);
    }

    /* Draw PD-authentic dialog frame */
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH,
                      "Create Agent", 1);

    /* Draw title text with glow */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);

        ImVec2 titleSize = ImGui::CalcTextSize("Create Agent");
        dl->AddText(ImVec2(dialogX + 10.0f,
                           dialogY + (pdTitleH - titleSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), "Create Agent");
    }

    /* Content starts below PD title bar */
    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    float pad = 12.0f * scale;
    float contentW = dialogW - pad * 2.0f;

    /* ================================================================
     * Two-column layout: Left = form fields, Right = portrait preview
     * ================================================================ */
    float portraitSize = 120.0f * scale;
    float formW = contentW - portraitSize - pad * 2.0f;

    /* Portrait preview (right side) */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float px = dialogX + pad + formW + pad;
        float py = dialogY + pdTitleH + pad * 2.0f;
        drawPortraitPreview(dl, px, py, portraitSize, scale);
    }

    /* ================================================================
     * Name Input
     * ================================================================ */
    ImGui::Text("Agent Name");
    ImGui::PushItemWidth(formW);

    /* Focus the name input on first frame */
    if (s_FirstFrame) {
        ImGui::SetKeyboardFocusHere();
        s_FirstFrame = false;
    }

    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    bool nameEntered = ImGui::InputText("##agent_name", s_AgentName,
                                         sizeof(s_AgentName), inputFlags);
    ImGui::PopItemWidth();

    ImGui::Spacing();

    /* ================================================================
     * Body Selection — carousel with left/right arrows
     * ================================================================ */
    ImGui::Text("Character");
    ImGui::Spacing();

    {
        /* Left arrow */
        if (ImGui::ArrowButton("##body_prev", ImGuiDir_Left) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
            s_SelectedBody--;
            if (s_SelectedBody < 0) s_SelectedBody = s_NumBodies - 1;
            s_HeadOverridden = false;
            autoSelectHead();
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }

        ImGui::SameLine();

        /* Body name display — centered, fixed width */
        {
            const char *bodyName = getBodyDisplayName(s_SelectedBody);
            float nameW = formW - 80.0f * scale;  /* space for arrows */
            float textW = ImGui::CalcTextSize(bodyName).x;
            float padLeft = (nameW - textW) * 0.5f;
            if (padLeft < 0) padLeft = 0;

            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(padLeft, 0));
            ImGui::SameLine(0, 0);
            ImGui::Text("%s", bodyName);
            ImGui::EndGroup();

            /* Make the text region take the fixed width */
            ImGui::SameLine(0, 0);
            float remaining = nameW - (padLeft + textW);
            if (remaining > 0) {
                ImGui::Dummy(ImVec2(remaining, 0));
                ImGui::SameLine(0, 0);
            }
        }

        ImGui::SameLine();

        /* Right arrow */
        if (ImGui::ArrowButton("##body_next", ImGuiDir_Right) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
            s_SelectedBody++;
            if (s_SelectedBody >= s_NumBodies) s_SelectedBody = 0;
            s_HeadOverridden = false;
            autoSelectHead();
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }

        /* Body index display */
        ImGui::SameLine();
        ImGui::TextDisabled("(%d/%d)", s_SelectedBody + 1, s_NumBodies);
    }

    ImGui::Spacing();

    /* ================================================================
     * Head Selection — carousel with left/right arrows
     * ================================================================ */
    ImGui::Text("Head");
    ImGui::Spacing();

    {
        if (ImGui::ArrowButton("##head_prev", ImGuiDir_Left)) {
            s_SelectedHead--;
            if (s_SelectedHead < 0) s_SelectedHead = s_NumHeads - 1;
            s_HeadOverridden = true;
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }

        ImGui::SameLine();

        {
            char headName[64];
            getHeadDisplayName(headName, sizeof(headName), s_SelectedHead);
            float nameW = formW - 80.0f * scale;
            float textW = ImGui::CalcTextSize(headName).x;
            float padLeft = (nameW - textW) * 0.5f;
            if (padLeft < 0) padLeft = 0;

            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(padLeft, 0));
            ImGui::SameLine(0, 0);
            ImGui::Text("%s", headName);
            ImGui::EndGroup();

            ImGui::SameLine(0, 0);
            float remaining = nameW - (padLeft + textW);
            if (remaining > 0) {
                ImGui::Dummy(ImVec2(remaining, 0));
                ImGui::SameLine(0, 0);
            }
        }

        ImGui::SameLine();

        if (ImGui::ArrowButton("##head_next", ImGuiDir_Right)) {
            s_SelectedHead++;
            if (s_SelectedHead >= s_NumHeads) s_SelectedHead = 0;
            s_HeadOverridden = true;
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(%d/%d)", s_SelectedHead + 1, s_NumHeads);

        /* Auto-reset hint */
        if (!s_HeadOverridden) {
            ImGui::TextDisabled("(auto-matched to character)");
        } else {
            if (ImGui::SmallButton("Auto")) {
                s_HeadOverridden = false;
                autoSelectHead();
                pdguiPlaySound(PDGUI_SND_TOGGLEON);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Reset to character default");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* ================================================================
     * Action Buttons
     * ================================================================ */
    {
        float buttonW = 120.0f * scale;
        float buttonH = 32.0f * scale;
        float totalW = buttonW * 2.0f + pad;

        /* Center the buttons */
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (formW - totalW) * 0.5f);

        /* Disable Create if name is empty */
        bool nameValid = (s_AgentName[0] != '\0');
        if (!nameValid) {
            ImGui::BeginDisabled();
        }

        bool doCreate = ImGui::Button("Create", ImVec2(buttonW, buttonH));

        /* Also create on Enter from the name field */
        if (nameEntered && nameValid) {
            doCreate = true;
        }

        /* Also create on gamepad A when focused (ImGui handles this via nav) */

        if (!nameValid) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine(0, pad);

        bool doCancel = ImGui::Button("Cancel", ImVec2(buttonW, buttonH));

        /* B button / Escape = cancel */
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
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

            /* Set head and body on the active player config temporarily
             * so the save includes this data. The agent is NOT loaded
             * for play — the player must still select it from Agent Select. */
            s32 pnum = g_MpPlayerNum;
            if (pnum < 0) pnum = 0;

            mpPlayerConfigSetHeadBody(pnum, (u8)s_SelectedHead, (u8)s_SelectedBody);
            mpPlayerConfigSetName(pnum, s_AgentName);

            /* Pop the Agent Create dialog to return to Agent Select */
            menuPopDialog();

            /* Save the new agent to disk. The PC port auto-saves to
             * SAVEDEVICE_GAMEPAK without a location dialog. The new agent
             * will appear in the file list, but is NOT auto-loaded —
             * the player must explicitly select it to play. */
            filemgrPushSelectLocationDialog(0, FILETYPE_GAME);

            sysLogPrintf(LOG_NOTE, "pdgui_agentcreate: Saved new agent '%s' "
                         "body=%d head=%d (not loaded, requires selection)",
                         s_AgentName, s_SelectedBody, s_SelectedHead);

            /* Reset state for next use */
            s_AgentName[0] = '\0';
            s_SelectedBody = 0;
            s_SelectedHead = 0;
            s_HeadOverridden = false;
        }

        /* ---- Execute Cancel ---- */
        if (doCancel) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();

            /* Reset state */
            s_AgentName[0] = '\0';
            s_SelectedBody = 0;
            s_SelectedHead = 0;
            s_HeadOverridden = false;
        }
    }

    /* ---- Footer ---- */
    ImGui::Spacing();
    ImGui::TextDisabled("Choose a name and character for your agent");

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
