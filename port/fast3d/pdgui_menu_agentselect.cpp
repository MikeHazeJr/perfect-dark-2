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
#include "pdgui_layout.h"       /* pdguiPopupDarkenBehind (M-4 confirm modal) */
#include "pdgui_nav.h"
#include "pdgui_glyphs.h"
#include "pdgui_charpreview.h"
#include "screenmfst.h"
#include "net/netmanifest.h"
#include "system.h"
#include "inputctx.h"
#include "menupool.h"
#include "menugraph.h"

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

/* S309: per-agent preferences sidecar. Declared here because the C header
 * sits in port/include/ and the C++ ABI guard (#define bool s32) blocks
 * including prefs_agent.h directly from this translation unit. */
extern "C" void prefsAgentLoad(const char *agent_name);
extern "C" void prefsAgentResetVisuals(void);
extern "C" void prefsAgentMigrateLegacySidecar(const char *raw_name, const char *display_name);
extern "C" s32  presenceIsAgentLoaded(void);

static void prefsLoadForFile(struct filelistfile *file)
{
    if (!file) return;
    /* Use gamefileGetOverview to decode the display name exactly as Agent
     * Select shows it -- the raw file->name[] bytes may be encoded as
     * variable-length character codes and produce different text than
     * the overview decoder yields.  Keeping the same decoder here means
     * the sidecar path matches the visible agent name 1:1. */
    char name[32];
    u8 stage = 0, diff = 0;
    u32 ptime = 0;
    memset(name, 0, sizeof(name));
    gamefileGetOverview(file->name, name, &stage, &diff, &ptime);
    /* Defensive: trim trailing spaces + ensure NUL terminator. */
    name[sizeof(name) - 1] = '\0';
    for (s32 i = (s32)strlen(name) - 1; i >= 0 && name[i] == ' '; --i) {
        name[i] = '\0';
    }
    if (!name[0]) {
        snprintf(name, sizeof(name), "id%d", (int)file->fileid);
    }
    /* One-shot migration: agents created before the S313 gamefileGetOverview
     * fix had sidecars named from raw save bytes.  Rename to display-name path
     * if the old file exists and the new one doesn't. */
    prefsAgentMigrateLegacySidecar(file->name, name);
    prefsAgentLoad(name);
}
s32 filemgrSaveOrLoad(struct fileguid *guid, s32 fileop, uintptr_t playernum);

extern struct fileguid g_FilemgrFileToDelete;
void filemgrDeleteCurrentFile(void);

#define FILETYPE_GAME 0
void filemgrPushSelectLocationDialog(s32 arg0, u32 filetype);

const char *langSafe(s32 textid);

struct solostage {
    u32 stagenum;
    u8  unk04;
    u16 name1;
    u16 name2;
    u16 name3;
    const char *catalog_id;
};
extern struct solostage g_SoloStages[];

#define DIFF_PA 3
#define SOLOSTAGEINDEX_SKEDARRUINS 16

extern s32 g_MpPlayerNum;

s32 viGetWidth(void);
s32 viGetHeight(void);

u8  mpPlayerConfigGetHead(s32 playernum);  /* DEPRECATED */
u8  mpPlayerConfigGetBody(s32 playernum);  /* DEPRECATED */
const char *mpPlayerConfigGetHeadId(s32 playernum);
const char *mpPlayerConfigGetBodyId(s32 playernum);

} /* extern "C" */

typedef struct AgentSelectLoadPayload {
    struct filelistfile *file;
    const struct menudialogdef *release_def;
} AgentSelectLoadPayload;

static s32 agentSelectGraphLoad(void *userdata)
{
    AgentSelectLoadPayload *payload = (AgentSelectLoadPayload *)userdata;
    if (!payload || !payload->file) {
        return -1;
    }

    if (payload->release_def) {
        menupoolReleaseDialog(payload->release_def);
    }

    g_GameFileGuid.fileid = payload->file->fileid;
    g_GameFileGuid.deviceserial = payload->file->deviceserial;
    filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0);
    prefsLoadForFile(payload->file);
    return 0;
}

/* ========================================================================
 * State
 * ======================================================================== */

static s32 s_SelectedIdx = 0;
static s32 s_PrevSelectedIdx = -1;
static bool s_Registered = false;
static s32 s_DefaultAgentFileId = -1; /* file ID of the default agent (-1 = none) */
static bool s_DefaultAgentConfigured = false;
static bool s_AutoLoadTriggered = false;

/* Confirmation prompt state — M-4 (2026-04-19): upgraded from inline dimmed
 * overlay to BeginPopupModal. Delete defaults to Cancel focus (destructive),
 * Copy defaults to Confirm focus (non-destructive). 5-frame force-focus +
 * 3-frame input debounce mirrors M-1 pattern in pdgui_menu_warning.cpp so
 * the triggering X/Delete press can't bleed through into Confirm. */
#define CONFIRM_NONE   0
#define CONFIRM_DELETE 1
#define CONFIRM_COPY   2
static s32 s_ConfirmMode = CONFIRM_NONE;
static s32 s_ConfirmIdx = -1;
static s32 s_ConfirmOpenFrame = -1;
#define AGENTSEL_CONFIRM_FRAME_DEBOUNCE     3
#define AGENTSEL_CONFIRM_FORCE_FOCUS_FRAMES 5
#define AGENTSEL_CONFIRM_POPUP_ID "##agent_select_confirm_modal"

/* S300: s_AgentSelectPushedCtx removed — menu pool owns the ctx for
 * MENU_TYPE_AGENT_SELECT via menupoolAcquireDialog / menupoolReleaseDialog. */

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

static void buildAgentSelectFooter(bool existingAgent, char *buf, size_t bufSize)
{
    char accept[24];
    pdguiGlyphGetActionLabel(ACTION_MENU_ACCEPT, accept, (s32)sizeof(accept));

    if (existingAgent) {
        char context[24], remove[24], tertiary[24];
        pdguiGlyphGetActionLabel(ACTION_MENU_SECONDARY, context, (s32)sizeof(context));
        s32 hasDelete =
            pdguiGlyphGetActionLabel(ACTION_MENU_DELETE, remove, (s32)sizeof(remove));
        pdguiGlyphGetActionLabel(ACTION_MENU_TERTIARY, tertiary, (s32)sizeof(tertiary));

        if (hasDelete) {
            snprintf(buf, bufSize,
                     "[%s] Load  [%s]/Right-click Menu  [%s] Delete  [%s] Default",
                     accept, context, remove, tertiary);
        } else {
            snprintf(buf, bufSize,
                     "[%s] Load  [%s]/Right-click Menu (Copy/Delete)  [%s] Default",
                     accept, context, tertiary);
        }
    } else {
        char up[24], down[24];
        pdguiGlyphGetActionLabel(ACTION_MENU_UP, up, (s32)sizeof(up));
        pdguiGlyphGetActionLabel(ACTION_MENU_DOWN, down, (s32)sizeof(down));
        snprintf(buf, bufSize, "[%s] Select   [%s]/[%s] Navigate", accept, up, down);
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
        /* S295 F4: leak guard — S300 pool owns ctx, release pops it. */
        menupoolReleaseDialog(menupoolDialogDef(dialog));
        return 1;
    }

    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_ConfirmMode = CONFIRM_NONE;
        s_ConfirmIdx = -1;

        /* Agent Select is pre-sign-in: reset to base defaults so the
         * screen never shows a previous agent's custom theme/chrome/font.
         * Per-agent visuals are applied below only if auto-load fires, or
         * later when the user explicitly selects an agent. */
        prefsAgentResetVisuals();

        /* Auto-load default agent on first appearance -- only when no
         * agent is loaded yet. If presenceIsAgentLoaded() is true the
         * user reached Agent Select from main_menu via Change Agent
         * (an explicit "I want to pick a different agent" gesture);
         * triggering filemgrSaveOrLoad(FILEOP_LOAD_GAME) here cascades
         * through filemgrHandleSuccess -> func0f0f820c(MENUROOT_MAINMENU)
         * which queues a menuPushRootDialog. menupoolReleaseAll() in
         * that path nukes the agent_select pool slot, slamming the user
         * back to a fresh main_menu after 14 frames -- the "Change
         * Agent flashes open then closes" jank Mike reported. */
        if (!s_AutoLoadTriggered && s_DefaultAgentFileId >= 0 &&
                presenceIsAgentLoaded() == 0) {
            s_AutoLoadTriggered = true;
            for (s32 i = 0; i < fl->numfiles; i++) {
                if (fl->files[i].fileid == s_DefaultAgentFileId) {
                    g_GameFileGuid.fileid = fl->files[i].fileid;
                    g_GameFileGuid.deviceserial = fl->files[i].deviceserial;
                    filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0);
                    prefsLoadForFile(&fl->files[i]);
                    s_SelectedIdx = i;
                    break;
                }
            }
        } else if (!s_AutoLoadTriggered) {
            /* Mark triggered anyway so the gate flips for the rest of the
             * session -- a later sign-out + re-enter Agent Select should
             * present the picker, not auto-load the prior agent. */
            s_AutoLoadTriggered = true;
            /* Preselect the default if any; the user can A-confirm it. */
            if (s_DefaultAgentFileId >= 0) {
                for (s32 i = 0; i < fl->numfiles; i++) {
                    if (fl->files[i].fileid == s_DefaultAgentFileId) {
                        s_SelectedIdx = i;
                        break;
                    }
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
                    pdguiPalImU32(PDPAL_TITLEFG, 255), "Perfect Dark");
    }

    pdguiSetCursorBelowTitle(pdTitleH);
    ImGui::TextDisabled("Choose Your Reality");
    ImGui::Separator();

    /* ================================================================
     * M-4: Confirmation modal — when s_ConfirmMode is active, gate all
     * agent-list key input so the modal is the only input sink. The
     * BeginPopupModal rendering itself happens at the end of the function
     * (after ImGui::End()) so the modal is a viewport-level overlay rather
     * than nested inside the agent-select window.
     * ================================================================ */
    bool confirmActive = (s_ConfirmMode != CONFIRM_NONE &&
                          s_ConfirmIdx >= 0 && s_ConfirmIdx < fl->numfiles);

    /* Menu Accept = load/select — disabled while confirm modal is open so
     * the modal owns input. */
    if (!confirmActive && pdguiMenuAcceptPressed()) {
        if (s_SelectedIdx == fl->numfiles) {
            pdguiPlaySound(PDGUI_SND_SELECT);
            /* B-124 / S300: pop owned ctx before transitioning away */
            menupoolReleaseDialog(menupoolDialogDef(dialog));
            gamefileLoadDefaults(&g_GameFile);
            menuGraphFirePushDialog(MENU_TYPE_AGENT_SELECT, "create",
                &g_FilemgrEnterNameMenuDialog);
        } else if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
            struct filelistfile *file = &fl->files[s_SelectedIdx];
            pdguiPlaySound(PDGUI_SND_SELECT);
            AgentSelectLoadPayload payload = { file, menupoolDialogDef(dialog) };
            menuGraphFireLocalOp(MENU_TYPE_AGENT_SELECT, "load",
                agentSelectGraphLoad, &payload);
        }
    }
    /* X / C / right-click on row = open context menu (Mike directive
     * 2026-05-17 + menu-input-interaction-grammar Rule 5 X-context).
     * Items inside the popup: Load, Copy, Delete, Set Default, Cancel.
     * The C keyboard key + the controller secondary action open the same
     * popup; right-click on a list row also opens it (handled at the
     * row Selectable below). The direct C-fires-Copy and Y-fires-Delete
     * paths are kept as power-user shortcuts on the keyboard. */
    if (!confirmActive && pdguiMenuSecondaryPressed()) {
        if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
            ImGui::OpenPopup("##agent_ctx");
            pdguiPlaySound(PDGUI_SND_TOGGLEOFF);
        }
    }
    /* Menu Delete = delete (with confirmation). Controller users can reach
     * Delete through the Menu Secondary context menu when Delete is unbound. */
    if (!confirmActive && pdguiMenuDeletePressed()) {
        if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
            pdguiPlaySound(PDGUI_SND_ERROR);
            s_ConfirmMode = CONFIRM_DELETE;
            s_ConfirmIdx = s_SelectedIdx;
            s_ConfirmOpenFrame = (s32)ImGui::GetFrameCount();
            ImGui::OpenPopup(AGENTSEL_CONFIRM_POPUP_ID);
        }
    }
    /* Menu Tertiary = set as default agent. */
    if (!confirmActive && pdguiMenuTertiaryPressed()) {
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
    /* L-4: B / Escape = go back to previous menu — only when the confirm
     * modal isn't open. When it is, Escape cancels the modal (handled in
     * the BeginPopupModal block below). */
    if (!confirmActive && pdguiMenuCancelPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
        menuGraphFirePop(MENU_TYPE_AGENT_SELECT, "back");
        ImGui::End();
        return 1;
    }
    /* Arrow key navigation for MKB — frozen while modal is open. */
    if (!confirmActive && pdguiMenuDownRepeat()) {
        s_SelectedIdx++;
        if (s_SelectedIdx >= totalEntries) s_SelectedIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (!confirmActive && pdguiMenuUpRepeat()) {
        s_SelectedIdx--;
        if (s_SelectedIdx < 0) s_SelectedIdx = totalEntries - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }

    /* ================================================================
     * Agent List
     * ================================================================ */
    char footerText[320];
    buildAgentSelectFooter(s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles,
                           footerText, sizeof(footerText));

    /* The cursor is already below the title/header.  Measure from the actual
     * remaining content region instead of subtracting title constants from
     * the full window height (B-999/T-MENUS-002).  The footer measurement uses
     * the active theme font and wrapped dynamic glyph labels. */
    ImVec2 contentAvail = ImGui::GetContentRegionAvail();
    float desiredFooterH = pdguiHintFooterHeight(footerText, contentAvail.x);
    pdgui_hint_footer_layout footerLayout = pdguiResolveHintFooterLayout(
        contentAvail.y, desiredFooterH, ImGui::GetTextLineHeightWithSpacing());
    float listH = footerLayout.body_height;
    float rowH = 64.0f * scale;

    if (ImGui::BeginChild("##agent_list", ImVec2(0, listH), true, 0)) {
        for (s32 i = 0; i < totalEntries; i++) {
            bool isSelected = (i == s_SelectedIdx);

            ImGui::PushID(i);

            if (i == fl->numfiles) {
                if (ImGui::Selectable("  + New Agent...", isSelected,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(0, 40.0f * scale))) {
                    pdguiPlaySound(PDGUI_SND_SELECT);
                    gamefileLoadDefaults(&g_GameFile);
                    menuGraphFirePushDialog(MENU_TYPE_AGENT_SELECT, "create",
                        &g_FilemgrEnterNameMenuDialog);
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
                             langSafe(g_SoloStages[stage - 1].name1),
                             langSafe(g_SoloStages[stage - 1].name2));
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
                    AgentSelectLoadPayload payload = { file, NULL };
                    menuGraphFireLocalOp(MENU_TYPE_AGENT_SELECT, "load",
                        agentSelectGraphLoad, &payload);
                }
                if (ImGui::IsItemHovered()) s_SelectedIdx = i;

                /* Right-click on this row opens the per-row context
                 * menu (parallel path to controller X / keyboard C).
                 * Set the row as selected first so the popup acts on
                 * the right entry. Same popup id (##agent_ctx) backs
                 * both routes per Rule 5 X-context-menu (one builder
                 * for both input devices). */
                if (!confirmActive && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    s_SelectedIdx = i;
                    ImGui::OpenPopup("##agent_ctx");
                    pdguiPlaySound(PDGUI_SND_TOGGLEOFF);
                }

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
                                pdguiImU32TintInfo(200), 3.0f * scale);
                } else {
                    dl->AddRectFilled(ImVec2(thumbX, thumbY),
                                      ImVec2(thumbX + thumbSize, thumbY + thumbSize),
                                      pdguiPalImU32(PDPAL_TITLEBG, 180), 3.0f * scale);
                    dl->AddRect(ImVec2(thumbX, thumbY),
                                ImVec2(thumbX + thumbSize, thumbY + thumbSize),
                                pdguiImU32TintInfo(200), 3.0f * scale);
                    char initials[4] = {0};
                    if (name[0]) { initials[0] = name[0]; if (name[1]) initials[1] = name[1]; }
                    ImVec2 iSz = ImGui::CalcTextSize(initials);
                    dl->AddText(ImVec2(thumbX + (thumbSize - iSz.x) * 0.5f,
                                       thumbY + (thumbSize - iSz.y) * 0.5f),
                                pdguiImU32TitleGlow(255), initials);
                }

                float textX = thumbX + thumbSize + 10.0f * scale;
                float lineY = itemMin.y + 8.0f * scale;

                dl->AddText(ImVec2(textX, lineY), pdguiPalImU32(PDPAL_TITLEFG, 255), name);

                /* Show [DEFAULT] tag if this agent is the default — S311 theme success tint. */
                if (file->fileid == s_DefaultAgentFileId) {
                    ImVec2 nameSize = ImGui::CalcTextSize(name);
                    dl->AddText(ImVec2(textX + nameSize.x + 8.0f * scale, lineY),
                                pdguiImU32TintSuccess(200), "[DEFAULT]");
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

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    /* Per-row context menu (Mike directive 2026-05-17 + Rule 5 X-context).
     * Opened by controller secondary (X), keyboard C, or right-click on
     * a row. The popup body fires the same Load / Copy / Delete /
     * Set-Default actions as the existing keyboard shortcuts, just
     * presented as a discoverable menu. */
    if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles
            && ImGui::BeginPopup("##agent_ctx")) {
        struct filelistfile *cfile = &fl->files[s_SelectedIdx];
        char cname[12] = {0};
        u8 cstage = 0, cdiff = 0;
        u32 ctime = 0;
        gamefileGetOverview(cfile->name, cname, &cstage, &cdiff, &ctime);

        ImGui::TextColored(pdguiVec4TitleGlow(), "Agent: %s", cname[0] ? cname : "(unnamed)");
        ImGui::Separator();

        if (ImGui::MenuItem("Load")) {
            ImGui::CloseCurrentPopup();
            AgentSelectLoadPayload payload = { cfile, NULL };
            menuGraphFireLocalOp(MENU_TYPE_AGENT_SELECT, "load",
                agentSelectGraphLoad, &payload);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        if (ImGui::MenuItem("Copy")) {
            ImGui::CloseCurrentPopup();
            s_ConfirmMode = CONFIRM_COPY;
            s_ConfirmIdx = s_SelectedIdx;
            s_ConfirmOpenFrame = (s32)ImGui::GetFrameCount();
            ImGui::OpenPopup(AGENTSEL_CONFIRM_POPUP_ID);
            pdguiPlaySound(PDGUI_SND_TOGGLEOFF);
        }
        if (ImGui::MenuItem("Delete")) {
            ImGui::CloseCurrentPopup();
            s_ConfirmMode = CONFIRM_DELETE;
            s_ConfirmIdx = s_SelectedIdx;
            s_ConfirmOpenFrame = (s32)ImGui::GetFrameCount();
            ImGui::OpenPopup(AGENTSEL_CONFIRM_POPUP_ID);
            pdguiPlaySound(PDGUI_SND_ERROR);
        }
        const bool isDefault = (cfile->fileid == s_DefaultAgentFileId);
        if (ImGui::MenuItem(isDefault ? "Clear Default Agent" : "Set as Default Agent")) {
            ImGui::CloseCurrentPopup();
            s_DefaultAgentFileId = isDefault ? -1 : cfile->fileid;
            configSave("pd.ini");
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    /* Character preview for selected agent */
    if (s_SelectedIdx >= 0 && s_SelectedIdx < fl->numfiles) {
        s32 pnum = g_MpPlayerNum;
        if (pnum < 0) pnum = 0;
        pdguiCharPreviewRequest(mpPlayerConfigGetHeadId(pnum), mpPlayerConfigGetBodyId(pnum));
    }

    /* Focus sound */
    if (s_PrevSelectedIdx != s_SelectedIdx && s_PrevSelectedIdx >= 0) {
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    s_PrevSelectedIdx = s_SelectedIdx;

    /* Docked, wrapped, font-measured hints remain wholly inside the PD frame.
     * Labels still come from the live action map, so MKB/controller switching
     * changes text without changing or dropping any input behavior. */
    pdguiDrawHintFooter("##agent_select_footer", footerText,
                        footerLayout.footer_height);

    ImGui::End();

    /* ================================================================
     * M-4: Delete / Copy confirmation modal (rendered as viewport-level
     * popup after the agent-select window closes). Mirrors the M-1
     * pattern in pdgui_menu_warning.cpp — 5-frame SetKeyboardFocusHere
     * on Cancel (Delete) / Confirm (Copy), 3-frame input debounce so the
     * X/Delete press that triggered the popup can't bleed through.
     * ================================================================ */
    if (s_ConfirmMode != CONFIRM_NONE &&
            s_ConfirmIdx >= 0 && s_ConfirmIdx < fl->numfiles) {

        pdguiPopupDarkenBehind(0.65f);

        struct filelistfile *cf = &fl->files[s_ConfirmIdx];
        char cfName[12] = {0};
        u8 cs = 0, cd = 0; u32 ct = 0;
        gamefileGetOverview(cf->name, cfName, &cs, &cd, &ct);

        const bool isDelete = (s_ConfirmMode == CONFIRM_DELETE);

        /* Red palette for Delete, blue for Copy. */
        s32 prevPalette = pdguiGetPalette();
        pdguiSetPalette(isDelete ? 2 : 1);

        float modalW = pdguiScale(540.0f);
        float modalH = pdguiScale(260.0f);
        ImVec2 modalPos = pdguiCenterPos(modalW, modalH);

        float pdTitleH = pdguiScale(36.0f);
        if (pdTitleH < 18.0f) pdTitleH = 18.0f;

        s32 curFrame = (s32)ImGui::GetFrameCount();

        ImGui::SetNextWindowPos(modalPos);
        ImGui::SetNextWindowSize(ImVec2(modalW, modalH));

        ImGuiWindowFlags mflags = ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_NoMove
                                | ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_NoSavedSettings
                                | ImGuiWindowFlags_NoTitleBar
                                | ImGuiWindowFlags_NoBackground
                                | ImGuiWindowFlags_NoScrollbar;

        bool open = ImGui::BeginPopupModal(AGENTSEL_CONFIRM_POPUP_ID, nullptr, mflags);
        if (open) {
            float modalX = ImGui::GetWindowPos().x;
            float modalY = ImGui::GetWindowPos().y;

            /* Opaque backdrop behind the PD-authentic frame. */
            {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(ImVec2(modalX, modalY),
                                  ImVec2(modalX + modalW, modalY + modalH),
                                  pdguiPalImU32(PDPAL_BODYBG, 255), 0.0f);
            }

            /* PD-authentic frame + title. */
            const char *title = isDelete ? "Delete Agent?" : "Copy Agent?";
            pdguiDrawPdDialog(modalX, modalY, modalW, modalH, title, 1);
            {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                pdguiDrawTextGlow(modalX + 8.0f, modalY + 2.0f,
                                  modalW - 16.0f, pdTitleH - 4.0f);
                ImVec2 titleSize = ImGui::CalcTextSize(title);
                ImU32 titleCol = isDelete
                    ? IM_COL32(255, 255, 0, 255)
                    : pdguiImU32TitleGlow(255);
                dl->AddText(ImVec2(modalX + (modalW - titleSize.x) * 0.5f,
                                   modalY + (pdTitleH - titleSize.y) * 0.5f),
                            titleCol, title);
            }

            /* Body */
            pdguiSetCursorBelowTitle(pdTitleH);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * scale);

            char bodyMsg[160];
            if (isDelete) {
                snprintf(bodyMsg, sizeof(bodyMsg),
                         "Delete agent \"%s\"?\nThis cannot be undone.", cfName);
            } else {
                snprintf(bodyMsg, sizeof(bodyMsg),
                         "Copy agent \"%s\" to a new profile?", cfName);
            }
            {
                float mAvailW = modalW - ImGui::GetStyle().WindowPadding.x * 2.0f;
                ImVec2 ts = ImGui::CalcTextSize(bodyMsg, nullptr, false, mAvailW);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (mAvailW - ts.x) * 0.5f);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + mAvailW);
                if (isDelete) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.85f, 1.0f));
                    ImGui::TextWrapped("%s", bodyMsg);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextWrapped("%s", bodyMsg);
                }
                ImGui::PopTextWrapPos();
            }

            ImGui::Spacing();
            ImGui::Spacing();

            /* Buttons */
            float btnW = pdguiScale(160.0f);
            float btnH = pdguiScale(32.0f);
            float gap  = pdguiScale(16.0f);
            float mAvailW = modalW - ImGui::GetStyle().WindowPadding.x * 2.0f;
            float totalW = btnW * 2.0f + gap;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (mAvailW - totalW) * 0.5f);

            bool doConfirm = false;
            bool doCancel  = false;

            s32 framesOpen = (s_ConfirmOpenFrame >= 0)
                             ? (curFrame - s_ConfirmOpenFrame)
                             : AGENTSEL_CONFIRM_FORCE_FOCUS_FRAMES + 1;
            bool forceFocus = (framesOpen >= 0 &&
                               framesOpen < AGENTSEL_CONFIRM_FORCE_FOCUS_FRAMES);
            bool inputDebounced = (framesOpen >= 0 &&
                                   framesOpen < AGENTSEL_CONFIRM_FRAME_DEBOUNCE);

            if (isDelete) {
                /* Delete: default focus on Cancel (safer for destructive). */
                if (forceFocus) ImGui::SetKeyboardFocusHere(0);

                if (ImGui::Button("Cancel##agent_delete_cancel", ImVec2(btnW, btnH))) {
                    if (!inputDebounced) doCancel = true;
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine(0.0f, gap);

                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4(1.00f, 0.20f, 0.20f, 1.0f));
                if (ImGui::Button("Delete##agent_delete_confirm",
                                   ImVec2(btnW, btnH))) {
                    if (!inputDebounced) doConfirm = true;
                }
                ImGui::PopStyleColor(3);
            } else {
                /* Copy: non-destructive, default focus on Confirm. */
                if (ImGui::Button("Cancel##agent_copy_cancel", ImVec2(btnW, btnH))) {
                    if (!inputDebounced) doCancel = true;
                }
                ImGui::SameLine(0.0f, gap);

                if (forceFocus) ImGui::SetKeyboardFocusHere(0);
                if (ImGui::Button("Copy##agent_copy_confirm", ImVec2(btnW, btnH))) {
                    if (!inputDebounced) doConfirm = true;
                }
                ImGui::SetItemDefaultFocus();
            }

            /* Keybinding hints */
            {
                char accept[24], cancel[24], hintL[64], hintR[64];
                pdguiGlyphGetActionLabel(ACTION_MENU_ACCEPT, accept, (s32)sizeof(accept));
                pdguiGlyphGetActionLabel(ACTION_CANCEL_USE, cancel, (s32)sizeof(cancel));
                snprintf(hintL, sizeof(hintL), "[%s] Confirm", accept);
                snprintf(hintR, sizeof(hintR), "[%s] Cancel", cancel);

                float hintY = modalH - pdguiScale(22.0f);
                if (hintY < ImGui::GetCursorPosY() + 4.0f * scale) {
                    hintY = ImGui::GetCursorPosY() + 4.0f * scale;
                }
                ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, hintY));
                ImGui::TextDisabled("%s", hintL);

                ImVec2 rSize = ImGui::CalcTextSize(hintR);
                ImGui::SetCursorPos(ImVec2(modalW - ImGui::GetStyle().WindowPadding.x - rSize.x,
                                           hintY));
                ImGui::TextDisabled("%s", hintR);
            }

            /* Keyboard + gamepad shortcuts — debounced for a few frames so
             * the X/Delete press that opened the popup doesn't bleed through. */
            if (!inputDebounced) {
                if (pdguiMenuAcceptPressed()) {
                    doConfirm = true;
                }
                if (pdguiMenuCancelPressed()) {
                    doCancel = true;
                }
            }

            if (doConfirm) {
                if (isDelete) {
                    pdguiPlaySound(PDGUI_SND_SELECT);
                    g_FilemgrFileToDelete.fileid = cf->fileid;
                    g_FilemgrFileToDelete.deviceserial = cf->deviceserial;
                    filemgrDeleteCurrentFile();
                    if (s_SelectedIdx >= fl->numfiles) s_SelectedIdx = fl->numfiles - 1;
                } else {
                    pdguiPlaySound(PDGUI_SND_SELECT);
                    g_GameFileGuid.fileid = cf->fileid;
                    g_GameFileGuid.deviceserial = cf->deviceserial;
                    filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_LOAD_GAME, 0);
                    prefsLoadForFile(cf);
                    filemgrPushSelectLocationDialog(0, FILETYPE_GAME);
                }
                ImGui::CloseCurrentPopup();
                s_ConfirmMode = CONFIRM_NONE;
                s_ConfirmIdx = -1;
                s_ConfirmOpenFrame = -1;
            } else if (doCancel) {
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                ImGui::CloseCurrentPopup();
                s_ConfirmMode = CONFIRM_NONE;
                s_ConfirmIdx = -1;
                s_ConfirmOpenFrame = -1;
            }

            ImGui::EndPopup();
        } else {
            /* Popup was closed externally — clear state so we don't reopen
             * it next frame. */
            s_ConfirmMode = CONFIRM_NONE;
            s_ConfirmIdx = -1;
            s_ConfirmOpenFrame = -1;
        }

        pdguiSetPalette(prevPalette);
    }

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
