/**
 * pdgui_menu_agentselect.cpp -- ImGui replacement for the Agent Select screen.
 *
 * Replaces g_FilemgrFileSelectMenuDialog ("Perfect Dark" / "Choose Your Reality").
 *
 * Features:
 *   - Agent list with name, stage, difficulty, play time
 *   - Character preview thumbnail
 *   - Contextual actions: A=Load, X=Copy, Y=Delete
 *   - Delete/clone confirmation via inline input prompts (not nested windows)
 *   - Full controller/gamepad navigation
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
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
#include "screenmfst.h"
#include "net/netmanifest.h"
#include "system.h"

extern "C" {

extern struct menudialogdef g_FilemgrFileSelectMenuDialog;
extern struct menudialogdef g_FilemgrEnterNameMenuDialog;

/* Config system — for storing default agent */
s32 configSave(const char *fname);
void configRegisterInt(const char *key, s32 *var, s32 min, s32 max);

struct filelistfile {
    s32 fileid;
    u16 deviceserial;
    char name[16];
};

struct fileguid {
    s32 fileid;
    u16 deviceserial;
};

struct filelist {
    struct filelistfile files[30];
    s16 numfiles;
    s8 spacesfree[5];
    struct fileguid deviceguids[5];
    s8 devicestartindexes[5];
    s8 unk305[5];
    u8 numdevices;
    u8 filetype;
    u8 timeuntilupdate;
    u8 unk30d;
    u8 updatedthisframe;
};

extern struct filelist *g_FileLists[4];
extern struct fileguid g_GameFileGuid;

void gamefileGetOverview(char *arg0, char *name, u8 *stage,
                         u8 *difficulty, u32 *time);

struct gamefile;
extern struct gamefile g_GameFile;
void gamefileLoadDefaults(struct gamefile *file);

#define FILEOP_LOAD_GAME  100
s32 filemgrSaveOrLoad(struct fileguid *guid, s32 fileop, uintptr_t playernum);

extern struct fileguid g_FilemgrFileToDelete;
void filemgrDeleteCurrentFile(void);

#define FILETYPE_GAME 0
void filemgrPushSelectLocationDialog(s32 arg0, u32 filetype);

void menuPushDialog(struct menudialogdef *dialogdef);

char *langGet(s32 textid);

struct solostage {
    u32 stagenum;
    u8  unk04;
    u16 name1;
    u16 name2;
    u16 name3;
};
extern struct solostage g_SoloStages[];

#define DIFF_PA 3
#define SOLOSTAGEINDEX_SKEDARRUINS 16

extern s32 g_MpPlayerNum;

s32 viGetWidth(void);
s32 viGetHeight(void);

u8  mpPlayerConfigGetHead(s32 playernum);
u8  mpPlayerConfigGetBody(s32 playernum);

} /* extern "C" */

/* ========================================================================
 * State
 * ======================================================================== */

static s32 s_SelectedIdx = 0;
static s32 s_PrevSelectedIdx = -1;
static bool s_Registered = false;
static s32 s_DefaultAgentFileId = -1; /* file ID of the default agent (-1 = none) */
static bool s_DefaultAgentConfigured = false;
static bool s_AutoLoadTriggered = false;

/* Confirmation prompt state — rendered inline, no nested windows */
#define CONFIRM_NONE   0
#define CONFIRM_DELETE 1
#define CONFIRM_COPY   2
static s32 s_ConfirmMode = CONFIRM_NONE;
static s32 s_ConfirmIdx = -1;

/* ========================================================================
 * Helpers
 * ======================================================================== */

static void formatPlayTime(char *buf, size_t bufsize, u32 totalSeconds)
{
    u32 days = totalSeconds / 86400;
    u32 hours = (totalSeconds % 86400) / 3600;
    u32 mins = (totalSeconds % 3600) / 60;
    u32 secs = totalSeconds % 60;

    if (days > 0) {
        snprintf(buf, bufsize, "%dd %02d:%02d:%02d", days, hours, mins, secs);
    } else if (hours > 0) {
        snprintf(buf, bufsize, "%d:%02d:%02d", hours, mins, secs);
    } else {
        snprintf(buf, bufsize, "%d:%02d", mins, secs);
    }
}

/* ========================================================================
 * ImGui Render Callback
 * ======================================================================== */

static s32 renderAgentSelect(struct menudialog *dialog,
                              struct menu *menu,
                              s32 winW, s32 winH)
{
    if (!g_FileLists[0]) {
        return 0;
    }

    struct filelist *fl = g_FileLists[0];
    s32 totalEntries = fl->numfiles + 1;

    if (s_SelectedIdx >= totalEntries) s_SelectedIdx = totalEntries - 1;
    if (s_SelectedIdx < 0) s_SelectedIdx = 0;

    float scale = pdguiScaleFactor();
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;

    float pdTitleH = dialogH * 0.06f;
    if (pdTitleH < 20.0f) pdTitleH = 20.0f;
    if (pdTitleH > 28.0f) pdTitleH = 28.0f;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##agent_select", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_ConfirmMode = CONFIRM_NONE;
        s_ConfirmIdx = -1;

        /* Auto-load default agent on first appearance */
        if (!s_AutoLoadTriggered && s_DefaultAgentFileId >= 0) {
            s_AutoLoadTriggered = true;
            for (s32 i = 0; i < fl->numfiles; i++) {
                if (fl->files[i].fileid == s_DefaultAgentFileId) {
                    g_GameFileGuid.fileid = fl->files[i].fileid;
                    g_GameFileGuid.deviceserial = fl->files[i].deviceserial;
                    filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0);
                    s_SelectedIdx = i;
                    break;
                }
            }
        }
    }

    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, "Perfect Dark", 1);

    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 titleSize = ImGui::CalcTextSize("Perfect Dark");
        dl->AddText(ImVec2(dialogX + 10.0f,
                           dialogY + (pdTitleH - titleSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), "Perfect Dark");
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);
    ImGui::TextDisabled("Choose Your Reality");
    ImGui::Separator();

    /* ================================================================
     * Confirmation prompt — rendered INLINE (no nested ImGui::Begin).
     * Draws via the draw list over the dialog body, reads input directly.
     * ================================================================ */
    if (s_ConfirmMode != CONFIRM_NONE && s_ConfirmIdx >= 0 && s_ConfirmIdx < fl->numfiles) {
        struct filelistfile *cf = &fl->files[s_ConfirmIdx];
        char cfName[12] = {0};
        u8 cs = 0, cd = 0; u32 ct = 0;
        gamefileGetOverview(cf->name, cfName, &cs, &cd, &ct);

        const char *actionWord = (s_ConfirmMode == CONFIRM_DELETE) ? "Delete" : "Copy";
        ImU32 promptColor = (s_ConfirmMode == CONFIRM_DELETE)
            ? IM_COL32(255, 100, 100, 255)
            : IM_COL32(100, 200, 255, 255);

        /* Draw a dimmed overlay behind the prompt */
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY + pdTitleH),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          IM_COL32(0, 0, 0, 180));

        /* Prompt text — centered in the dialog */
        char promptLine1[128];
        snprintf(promptLine1, sizeof(promptLine1), "%s agent \"%s\"?", actionWord, cfName);
        const char *promptLine2 = "A/Enter to confirm, B/Esc to cancel";

        ImVec2 sz1 = ImGui::CalcTextSize(promptLine1);
        ImVec2 sz2 = ImGui::CalcTextSize(promptLine2);
        float cx = dialogX + dialogW * 0.5f;
        float cy = dialogY + dialogH * 0.45f;

        dl->AddText(ImVec2(cx - sz1.x * 0.5f, cy - 12.0f * scale), promptColor, promptLine1);
        dl->AddText(ImVec2(cx - sz2.x * 0.5f, cy + 12.0f * scale),
                    IM_COL32(200, 200, 200, 220), promptLine2);

        /* Handle input — A = confirm, B/Escape = cancel */
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            if (s_ConfirmMode == CONFIRM_DELETE) {
                pdguiPlaySound(PDGUI_SND_SELECT);
                g_FilemgrFileToDelete.fileid = cf->fileid;
                g_FilemgrFileToDelete.deviceserial = cf->deviceserial;
                filemgrDeleteCurrentFile();
                if (s_SelectedIdx >= fl->numfiles) s_SelectedIdx = fl->numfiles - 1;
            } else if (s_ConfirmMode == CONFIRM_COPY) {
                pdguiPlaySound(PDGUI_SND_SELECT);
                g_GameFileGuid.fileid = cf->fileid;
                g_GameFileGuid.deviceserial = cf->deviceserial;
                filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0);
                filemgrPushSelectLocationDialog(0, FILETYPE_GAME);
            }
            s_ConfirmMode = CONFIRM_NONE;
            s_ConfirmIdx = -1;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            s_ConfirmMode = CONFIRM_NONE;
            s_ConfirmIdx = -1;
        }

        /* Skip list rendering and input while prompt is active */
        ImGui::End();
        return 1;
    }

    /* ================================================================
     * Gamepad navigation (only when no confirmation prompt)
     * ================================================================ */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true)) {
        s_SelectedIdx++;
        if (s_SelectedIdx >= totalEntries) s_SelectedIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true)) {
        s_SelectedIdx--;
        if (s_SelectedIdx < 0) s_SelectedIdx = totalEntries - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    /* A / Enter = load/select */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (s_SelectedIdx == fl->numfiles) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            gamefileLoadDefaults(&g_GameFile);
            menuPushDialog(&g_FilemgrEnterNameMenuDialog);
        } else if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
            struct filelistfile *file = &fl->files[s_SelectedIdx];
            pdguiPlaySound(PDGUI_SND_SELECT);
            g_GameFileGuid.fileid = file->fileid;
            g_GameFileGuid.deviceserial = file->deviceserial;
            filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0);
        }
    }
    /* X / C = copy (with confirmation) */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) ||
        ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
            pdguiPlaySound(PDGUI_SND_TOGGLEOFF);
            s_ConfirmMode = CONFIRM_COPY;
            s_ConfirmIdx = s_SelectedIdx;
        }
    }
    /* Y / Delete = delete (with confirmation) */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
            pdguiPlaySound(PDGUI_SND_ERROR);
            s_ConfirmMode = CONFIRM_DELETE;
            s_ConfirmIdx = s_SelectedIdx;
        }
    }
    /* D / RB = set as default agent */
    if (ImGui::IsKeyPressed(ImGuiKey_D, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
        if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
            struct filelistfile *file = &fl->files[s_SelectedIdx];
            if (s_DefaultAgentFileId == file->fileid) {
                /* Toggle off default */
                s_DefaultAgentFileId = -1;
            } else {
                s_DefaultAgentFileId = file->fileid;
            }
            configSave("pd.ini");
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
    }
    /* Arrow key navigation for MKB */
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_SelectedIdx++;
        if (s_SelectedIdx >= totalEntries) s_SelectedIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_SelectedIdx--;
        if (s_SelectedIdx < 0) s_SelectedIdx = totalEntries - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }

    /* ================================================================
     * Agent List
     * ================================================================ */
    float footerH = 44.0f * scale;
    float listH = dialogH - pdTitleH - 36.0f * scale - footerH;
    float rowH = 64.0f * scale;

    if (ImGui::BeginChild("##agent_list", ImVec2(0, listH), true,
                           ImGuiWindowFlags_NoNav)) {
        for (s32 i = 0; i < totalEntries; i++) {
            bool isSelected = (i == s_SelectedIdx);

            ImGui::PushID(i);

            if (i == fl->numfiles) {
                if (ImGui::Selectable("  + New Agent...", isSelected,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(0, 40.0f * scale))) {
                    pdguiPlaySound(PDGUI_SND_SELECT);
                    gamefileLoadDefaults(&g_GameFile);
                    menuPushDialog(&g_FilemgrEnterNameMenuDialog);
                }
                if (ImGui::IsItemHovered()) s_SelectedIdx = i;
            } else {
                struct filelistfile *file = &fl->files[i];
                char name[12] = {0};
                u8 stage = 0, difficulty = 0;
                u32 time = 0;
                gamefileGetOverview(file->name, name, &stage, &difficulty, &time);

                if (stage > SOLOSTAGEINDEX_SKEDARRUINS + 1) stage = SOLOSTAGEINDEX_SKEDARRUINS + 1;
                if (difficulty > DIFF_PA) difficulty = DIFF_PA;

                char stageName[128] = "New Recruit";
                if (stage > 0) {
                    snprintf(stageName, sizeof(stageName), "%s %s",
                             langGet(g_SoloStages[stage - 1].name1),
                             langGet(g_SoloStages[stage - 1].name2));
                }

                char timeStr[64] = "";
                formatPlayTime(timeStr, sizeof(timeStr), time);

                const char *diffNames[] = {"Agent", "Special Agent",
                                           "Perfect Agent", "Perfect Agent"};
                const char *diffName = (difficulty <= DIFF_PA) ? diffNames[difficulty] : "Agent";

                if (ImGui::Selectable("##agent_entry", isSelected,
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0, rowH))) {
                    pdguiPlaySound(PDGUI_SND_SELECT);
                    g_GameFileGuid.fileid = file->fileid;
                    g_GameFileGuid.deviceserial = file->deviceserial;
                    filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0);
                }
                if (ImGui::IsItemHovered()) s_SelectedIdx = i;

                /* Draw overlay content */
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImDrawList *dl = ImGui::GetWindowDrawList();

                float thumbSize = 48.0f * scale;
                float thumbX = itemMin.x + 8.0f * scale;
                float thumbY = itemMin.y + (rowH - thumbSize) * 0.5f;

                u32 texId = (isSelected) ? pdguiCharPreviewGetTextureId() : 0;
                if (texId != 0 && pdguiCharPreviewIsReady() && isSelected) {
                    dl->AddRectFilled(ImVec2(thumbX, thumbY),
                                      ImVec2(thumbX + thumbSize, thumbY + thumbSize),
                                      IM_COL32(10, 15, 30, 240), 3.0f * scale);
                    dl->AddImage((ImTextureID)(uintptr_t)texId,
                                 ImVec2(thumbX + 1, thumbY + 1),
                                 ImVec2(thumbX + thumbSize - 1, thumbY + thumbSize - 1),
                                 ImVec2(0, 1), ImVec2(1, 0));
                    dl->AddRect(ImVec2(thumbX, thumbY),
                                ImVec2(thumbX + thumbSize, thumbY + thumbSize),
                                IM_COL32(80, 120, 200, 200), 3.0f * scale);
                } else {
                    dl->AddRectFilled(ImVec2(thumbX, thumbY),
                                      ImVec2(thumbX + thumbSize, thumbY + thumbSize),
                                      IM_COL32(40, 60, 100, 180), 3.0f * scale);
                    dl->AddRect(ImVec2(thumbX, thumbY),
                                ImVec2(thumbX + thumbSize, thumbY + thumbSize),
                                IM_COL32(80, 120, 200, 200), 3.0f * scale);
                    char initials[4] = {0};
                    if (name[0]) { initials[0] = name[0]; if (name[1]) initials[1] = name[1]; }
                    ImVec2 iSz = ImGui::CalcTextSize(initials);
                    dl->AddText(ImVec2(thumbX + (thumbSize - iSz.x) * 0.5f,
                                       thumbY + (thumbSize - iSz.y) * 0.5f),
                                IM_COL32(200, 220, 255, 255), initials);
                }

                float textX = thumbX + thumbSize + 10.0f * scale;
                float lineY = itemMin.y + 8.0f * scale;

                dl->AddText(ImVec2(textX, lineY), IM_COL32(255, 255, 255, 255), name);

                /* Show [DEFAULT] tag if this agent is the default */
                if (file->fileid == s_DefaultAgentFileId) {
                    ImVec2 nameSize = ImGui::CalcTextSize(name);
                    dl->AddText(ImVec2(textX + nameSize.x + 8.0f * scale, lineY),
                                IM_COL32(100, 255, 180, 200), "[DEFAULT]");
                }

                lineY += 18.0f * scale;
                char infoLine[256];
                snprintf(infoLine, sizeof(infoLine), "%s  |  %s", stageName, diffName);
                dl->AddText(ImVec2(textX, lineY), IM_COL32(180, 180, 200, 200), infoLine);

                lineY += 14.0f * scale;
                char timeLine[128];
                snprintf(timeLine, sizeof(timeLine), "Time: %s", timeStr);
                dl->AddText(ImVec2(textX, lineY), IM_COL32(140, 140, 160, 180), timeLine);
            }

            if (isSelected && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) ||
                               ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true))) {
                ImGui::SetScrollHereY();
            }

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    /* Character preview for selected agent */
    if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
        s32 pnum = g_MpPlayerNum;
        if (pnum < 0) pnum = 0;
        pdguiCharPreviewRequest(mpPlayerConfigGetHead(pnum), mpPlayerConfigGetBody(pnum));
    }

    /* Focus sound */
    if (s_PrevSelectedIdx != s_SelectedIdx && s_PrevSelectedIdx >= 0) {
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    s_PrevSelectedIdx = s_SelectedIdx;

    /* ================================================================
     * Footer — input hints
     * ================================================================ */
    ImGui::Separator();
    if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
        ImGui::TextDisabled("A/Enter: Load  X/C: Copy  Y/Del: Delete  D/RB: Default");
    } else {
        ImGui::TextDisabled("A/Enter: Select   D-Pad/Arrows: Navigate");
    }

    ImGui::End();
    return 1;
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMenuAgentSelectInitConfig(void)
{
    if (!s_DefaultAgentConfigured) {
        configRegisterInt("Agent.DefaultFileId", &s_DefaultAgentFileId, -1, 9999);
        s_DefaultAgentConfigured = true;
    }
}

void pdguiMenuAgentSelectRegister(void)
{
    pdguiMenuAgentSelectInitConfig();
    if (s_Registered) return;

    pdguiHotswapRegister(
        &g_FilemgrFileSelectMenuDialog,
        renderAgentSelect,
        "Agent Select"
    );

    s_Registered = true;

    /* Phase 6: Screen mini-manifest.
     * Agent Select shows a character preview — declare the Joanna body/head
     * and misc UI language bank.  Bundled base-game assets are no-op retains;
     * mod overrides of these assets (non-bundled) go through the full
     * ref-counted load/unload lifecycle. */
    {
        static const char *ids[] = {
            "base:dark_combat",       /* Joanna body (default preview) */
            "base:head_dark_combat",  /* Joanna head (default preview) */
            "base:lang_misc",         /* General UI strings */
        };
        static const u8 types[] = {
            MANIFEST_TYPE_BODY,
            MANIFEST_TYPE_HEAD,
            MANIFEST_TYPE_LANG,
        };
        screenManifestRegister(
            (void*)&g_FilemgrFileSelectMenuDialog,
            ids, types, 3);
    }

    sysLogPrintf(LOG_NOTE, "pdgui_menu_agentselect: Registered");
}

} /* extern "C" */
