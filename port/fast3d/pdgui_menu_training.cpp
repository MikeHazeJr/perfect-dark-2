/**
 * pdgui_menu_training.cpp -- ImGui replacements for Training Mode dialogs.
 *
 * Group 6 (22 dialogs total):
 *
 * FIRING RANGE (FR) — ImGui:
 *   g_FrDifficultyMenuDialog         -- Bronze / Silver / Gold selector
 *   g_FrTrainingInfoPreGameMenuDialog -- pre-session details + Ok/Cancel
 *   g_FrTrainingInfoInGameMenuDialog  -- in-session details + Resume/Abort
 *   g_FrCompletedMenuDialog           -- session completed stats
 *   g_FrFailedMenuDialog              -- session failed stats
 *
 * BIOGRAPHIES (CI) — ImGui:
 *   g_BioTextMenuDialog               -- miscellaneous bio scrollable text
 *
 * DEVICE TRAINING (DT) — ImGui:
 *   g_DtFailedMenuDialog              -- device training failed + time + tip
 *   g_DtCompletedMenuDialog           -- device training completed + time + tip
 *
 * HOLO TRAINING (HT) — ImGui:
 *   g_HtListMenuDialog                -- holo-training list
 *   g_HtFailedMenuDialog              -- holo training failed + time + tip
 *   g_HtCompletedMenuDialog           -- holo training completed + time + tip
 *
 * MISC:
 *   g_NowSafeMenuDialog               -- "Now safe to turn off" notice
 *
 * NULL (keep legacy — 3-D model renders or opaque struct access):
 *   g_FrWeaponListMenuDialog          -- custom GBI weapon-list render
 *   g_BioListMenuDialog               -- needs opaque struct chrbio/miscbio
 *   g_BioProfileMenuDialog            -- 3-D character model + rotation
 *   g_DtListMenuDialog                -- needs opaque device-name structs
 *   g_DtDetailsMenuDialog             -- 3-D weapon model preview
 *   g_HtDetailsMenuDialog             -- MENUITEMTYPE_MODEL item
 *   g_HangarListMenuDialog            -- needs opaque hangarbio structs
 *   g_HangarVehicleHolographMenuDialog-- 3-D vehicle holograph
 *   g_HangarVehicleDetailsMenuDialog  -- MENUITEMTYPE_MODEL + GBI render
 *   g_HangarLocationDetailsMenuDialog -- location texture via GBI texSelect
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Auto-discovered by GLOB_RECURSE for port/fast3d cpp files in CMakeLists.txt.
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
#include "system.h"

/* =========================================================================
 * Forward declarations — game symbols (extern "C" to avoid types.h)
 * ========================================================================= */

extern "C" {

/* ---- Dialog definitions (all 22 dialogs we register) ---- */
extern struct menudialogdef g_FrDifficultyMenuDialog;
extern struct menudialogdef g_FrWeaponListMenuDialog;
extern struct menudialogdef g_FrTrainingInfoPreGameMenuDialog;
extern struct menudialogdef g_FrTrainingInfoInGameMenuDialog;
extern struct menudialogdef g_FrCompletedMenuDialog;
extern struct menudialogdef g_FrFailedMenuDialog;

extern struct menudialogdef g_BioListMenuDialog;
extern struct menudialogdef g_BioProfileMenuDialog;
extern struct menudialogdef g_BioTextMenuDialog;

extern struct menudialogdef g_DtListMenuDialog;
extern struct menudialogdef g_DtDetailsMenuDialog;
extern struct menudialogdef g_DtFailedMenuDialog;
extern struct menudialogdef g_DtCompletedMenuDialog;

extern struct menudialogdef g_HtListMenuDialog;
extern struct menudialogdef g_HtDetailsMenuDialog;
extern struct menudialogdef g_HtFailedMenuDialog;
extern struct menudialogdef g_HtCompletedMenuDialog;
extern struct menudialogdef g_NowSafeMenuDialog;

extern struct menudialogdef g_HangarListMenuDialog;
extern struct menudialogdef g_HangarVehicleHolographMenuDialog;
extern struct menudialogdef g_HangarVehicleDetailsMenuDialog;
extern struct menudialogdef g_HangarLocationDetailsMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* ---- Firing Range API (training.h / trainingmenus.h) ---- */
/* Difficulty (0=Bronze, 1=Silver, 2=Gold) */
#define FRDIFFICULTY_BRONZE 0
#define FRDIFFICULTY_SILVER 1
#define FRDIFFICULTY_GOLD   2
s32  frGetDifficulty(void);
void frSetDifficulty(s32 difficulty);

/* Weapon slot selection */
s32  frGetSlot(void);
u32  frGetWeaponBySlot(s32 slot);
u32  frGetWeaponIndexByWeapon(u32 weaponnum);
s32  frIsInTraining(void);
u8   ciGetFiringRangeScore(s32 weaponindex);  /* score tier for visibility */

/* Weapon description text */
char *frGetWeaponDescription(void);

/* Text accessors — pass NULL for the menuitem* arg; they don't dereference it */
char *frMenuTextDifficultyName(void *item);
char *frMenuTextFailReason(void *item);
char *frMenuTextScoreValue(void *item);
char *frMenuTextTargetsDestroyedValue(void *item);
char *frMenuTextAccuracyValue(void *item);
char *frMenuTextWeaponName(void *item);
char *frMenuTextTimeTakenValue(void *item);
char *frMenuTextGoalScoreLabel(void *item);
char *frMenuTextGoalScoreValue(void *item);
char *frMenuTextMinAccuracyOrTargetsLabel(void *item);
char *frMenuTextMinAccuracyOrTargetsValue(void *item);
char *frMenuTextTimeLimitLabel(void *item);
char *frMenuTextTimeLimitValue(void *item);
char *frMenuTextAmmoLimitLabel(void *item);
char *frMenuTextAmmoLimitValue(void *item);

/* Legacy handlers invoked for side-effects (MENUOP_SET; item/data not used) */
#define MENUOP_SET 6
uintptr_t frDetailsOkMenuHandler(s32 op, void *item, void *data);
uintptr_t frAbortMenuHandler(s32 op, void *item, void *data);
uintptr_t menuhandlerFrFailedContinue(s32 op, void *item, void *data);

/* ---- Biography (CI) API ---- */
char *ciMenuTextMiscBioName(void *item);       /* misc bio title (uses g_ChrBioSlot) */
char *ciGetMiscBioDescription(void);           /* misc bio body text */

/* ---- Device Training (DT) API ---- */
char *dtMenuTextTimeTakenValue(void *item);    /* time taken string */
char *dtGetTip1(void);                         /* tip text for failed dialog */
char *dtGetTip2(void);                         /* tip text for completed dialog */

/* ---- Holo Training (HT) API ---- */
s32   htGetNumUnlocked(void);
s32   htGetIndexBySlot(s32 slot);
char *htGetName(s32 index);
extern u8 var80088bb4;                         /* current HT selection state */
char *htMenuTextTimeTakenValue(void *item);
char *htGetTip1(void);
char *htGetTip2(void);

} /* extern "C" */

/* =========================================================================
 * Shared helper — PdButton with edge-glow and sound
 * ========================================================================= */

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

/* Shared boilerplate: begin a standard-sized PD-styled dialog window.
 * Returns false if the window is collapsed; caller must End() and return. */
static bool beginTrainingWindow(const char *id, const char *title,
                                s32 winW, s32 winH)
{
    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    float diagW = pdguiMenuWidth();
    float diagH = pdguiMenuHeight();
    ImVec2 pos  = pdguiMenuPos();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(diagW, diagH));

    if (!ImGui::Begin(id, nullptr, wflags)) {
        return false;
    }

    /* Dark backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + diagW, pos.y + diagH),
                          IM_COL32(8, 8, 16, 255));
    }

    pdguiDrawPdDialog(pos.x, pos.y, diagW, diagH, title, 1);

    /* Title row */
    {
        float titleH = pdguiScale(26.0f);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(pos.x + 8.0f, pos.y + 2.0f, diagW - 16.0f, titleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(pos.x + (diagW - ts.x) * 0.5f,
                           pos.y + (titleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), title);
        ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);
    }

    return true;
}

/* Render a two-column label row (label: value).
 * Strips trailing newlines that the legacy text functions sometimes append. */
static void renderLabelRow(const char *label, const char *value)
{
    char lbuf[128];
    char vbuf[128];
    int  i;

    if (!label || !value) return;

    snprintf(lbuf, sizeof(lbuf), "%s", label);
    snprintf(vbuf, sizeof(vbuf), "%s", value);

    for (i = (int)strlen(lbuf) - 1;
         i >= 0 && (lbuf[i] == '\n' || lbuf[i] == '\r'); i--)
        lbuf[i] = '\0';
    for (i = (int)strlen(vbuf) - 1;
         i >= 0 && (vbuf[i] == '\n' || vbuf[i] == '\r'); i--)
        vbuf[i] = '\0';

    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 0.9f), "%s", lbuf);
    ImGui::SameLine(pdguiScale(150.0f));
    ImGui::Text("%s", vbuf);
}

/* =========================================================================
 * FR — Difficulty selector
 * ========================================================================= */

static s32 renderFrDifficulty(struct menudialog *dialog,
                               struct menu *menu,
                               s32 winW, s32 winH)
{
    float diagW    = pdguiMenuWidth();
    float diagH    = pdguiMenuHeight();
    float btnW     = pdguiScale(180.0f);
    float btnH     = pdguiScale(32.0f);
    float footerH  = pdguiScale(50.0f);
    float pad      = pdguiScale(10.0f);
    float contentH = diagH - pdguiScale(26.0f) - footerH
                     - ImGui::GetStyle().WindowPadding.y;
    float startY   = pdguiScale(26.0f) + ImGui::GetStyle().WindowPadding.y
                     + (contentH - 3.0f * (btnH + pad)) * 0.5f;
    float startX   = (diagW - btnW) * 0.5f;
    bool  locked;
    bool  active;

    /* Score tier for the current weapon slot: 0=none, >=1=Bronze done, >=2=Silver done */
    s32 weaponIndex = (s32)frGetWeaponIndexByWeapon(frGetWeaponBySlot(frGetSlot()));
    u8  score       = ciGetFiringRangeScore(weaponIndex);

    if (!beginTrainingWindow("##fr_diff", "Difficulty", winW, winH)) {
        ImGui::End();
        return 1;
    }

    ImGui::SetCursorPos(ImVec2(startX, startY));

    /* Bronze — always available */
    active = (frGetDifficulty() == FRDIFFICULTY_BRONZE);
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.3f, 0.1f, 1.0f));
    if (PdButton("Bronze", ImVec2(btnW, btnH))) {
        frSetDifficulty(FRDIFFICULTY_BRONZE);
        menuPushDialog(&g_FrTrainingInfoPreGameMenuDialog);
    }
    if (active) ImGui::PopStyleColor();

    ImGui::SetCursorPosX(startX);

    /* Silver — requires at least Bronze completed */
    locked = (score < 1);
    if (locked) ImGui::BeginDisabled();
    active = (frGetDifficulty() == FRDIFFICULTY_SILVER);
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.55f, 1.0f));
    if (PdButton("Silver", ImVec2(btnW, btnH))) {
        frSetDifficulty(FRDIFFICULTY_SILVER);
        menuPushDialog(&g_FrTrainingInfoPreGameMenuDialog);
    }
    if (active) ImGui::PopStyleColor();
    if (locked) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.7f), "(complete Bronze first)");
    }

    ImGui::SetCursorPosX(startX);

    /* Gold — requires Silver completed */
    locked = (score < 2);
    if (locked) ImGui::BeginDisabled();
    active = (frGetDifficulty() == FRDIFFICULTY_GOLD);
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.5f, 0.1f, 1.0f));
    if (PdButton("Gold", ImVec2(btnW, btnH))) {
        frSetDifficulty(FRDIFFICULTY_GOLD);
        menuPushDialog(&g_FrTrainingInfoPreGameMenuDialog);
    }
    if (active) ImGui::PopStyleColor();
    if (locked) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.7f), "(complete Silver first)");
    }

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float backW = pdguiScale(140.0f);
        float backH = pdguiScale(28.0f);
        ImGui::SetCursorPosX((diagW - backW) * 0.5f);

        if (PdButton("Cancel", ImVec2(backW, backH))
            || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
            || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * FR — Training Info (shared by Pre-Game and In-Game variants)
 * ========================================================================= */

static s32 renderFrTrainingInfo(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH,
                                bool inGame)
{
    float diagW    = pdguiMenuWidth();
    float diagH    = pdguiMenuHeight();
    float titleH   = pdguiScale(26.0f);
    float footerH  = pdguiScale(58.0f);
    float pad      = ImGui::GetStyle().WindowPadding.x;
    float contentH = diagH - titleH - footerH - ImGui::GetStyle().WindowPadding.y * 2.0f;
    float childW   = diagW - pad * 2.0f;
    float btnW;
    float btnH;

    if (!beginTrainingWindow(inGame ? "##fr_info_ig" : "##fr_info_pg",
                             "Training Info", winW, winH)) {
        ImGui::End();
        return 1;
    }

    /* Stats panel (top 55% of content area) */
    ImGui::BeginChild("##fr_stats", ImVec2(childW, contentH * 0.55f), false);
    renderLabelRow("Difficulty:",   frMenuTextDifficultyName(nullptr));
    renderLabelRow(frMenuTextGoalScoreLabel(nullptr),
                   frMenuTextGoalScoreValue(nullptr));
    renderLabelRow(frMenuTextMinAccuracyOrTargetsLabel(nullptr),
                   frMenuTextMinAccuracyOrTargetsValue(nullptr));
    renderLabelRow(frMenuTextTimeLimitLabel(nullptr),
                   frMenuTextTimeLimitValue(nullptr));
    renderLabelRow(frMenuTextAmmoLimitLabel(nullptr),
                   frMenuTextAmmoLimitValue(nullptr));
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();

    /* Weapon description (scrollable, bottom 45%) */
    ImGui::BeginChild("##fr_desc",
                      ImVec2(childW, contentH * 0.45f - pdguiScale(6.0f)), true);
    {
        char *desc = frGetWeaponDescription();
        if (desc && desc[0]) {
            ImGui::TextWrapped("%s", desc);
        }
    }
    ImGui::EndChild();

    /* Footer: two action buttons */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    btnW = (childW - pdguiScale(12.0f)) * 0.5f;
    btnH = pdguiScale(30.0f);

    if (PdButton(inGame ? "Resume" : "Ok", ImVec2(btnW, btnH))
        || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false))
    {
        frDetailsOkMenuHandler(MENUOP_SET, nullptr, nullptr);
    }

    ImGui::SameLine(0, pdguiScale(12.0f));

    if (PdButton(inGame ? "Abort" : "Cancel", ImVec2(btnW, btnH))
        || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        frAbortMenuHandler(MENUOP_SET, nullptr, nullptr);
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

static s32 renderFrTrainingInfoPreGame(struct menudialog *dialog,
                                       struct menu *menu,
                                       s32 winW, s32 winH)
{
    return renderFrTrainingInfo(dialog, menu, winW, winH, false);
}

static s32 renderFrTrainingInfoInGame(struct menudialog *dialog,
                                      struct menu *menu,
                                      s32 winW, s32 winH)
{
    return renderFrTrainingInfo(dialog, menu, winW, winH, true);
}

/* =========================================================================
 * FR — Completed / Failed stats (shared renderer)
 * ========================================================================= */

static s32 renderFrStats(struct menudialog *dialog,
                          struct menu *menu,
                          s32 winW, s32 winH,
                          bool completed)
{
    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float footerH = pdguiScale(50.0f);
    float startX;
    const char *headline;

    if (!beginTrainingWindow(completed ? "##fr_done" : "##fr_fail",
                             "Training Stats", winW, winH)) {
        ImGui::End();
        return 1;
    }

    /* Status headline */
    headline = completed ? "Completed!" : "Failed!";
    {
        ImVec4 col = completed
            ? ImVec4(0.1f, 1.0f, 0.3f, 1.0f)
            : ImVec4(1.0f, 0.25f, 0.2f, 1.0f);
        startX = (diagW - ImGui::CalcTextSize(headline).x) * 0.5f;
        ImGui::SetCursorPosX(startX > 0 ? startX : 0);
        ImGui::TextColored(col, "%s", headline);
    }

    ImGui::Spacing();

    /* Fail reason (failed only) */
    if (!completed) {
        char *reason = frMenuTextFailReason(nullptr);
        if (reason && reason[0]) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", reason);
            ImGui::Spacing();
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    renderLabelRow("Score:",      frMenuTextScoreValue(nullptr));
    renderLabelRow("Targets:",    frMenuTextTargetsDestroyedValue(nullptr));
    renderLabelRow("Accuracy:",   frMenuTextAccuracyValue(nullptr));
    renderLabelRow("Difficulty:", frMenuTextDifficultyName(nullptr));
    renderLabelRow("Time:",       frMenuTextTimeTakenValue(nullptr));
    renderLabelRow("Weapon:",     frMenuTextWeaponName(nullptr));

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float btnW = pdguiScale(160.0f);
        float btnH = pdguiScale(28.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);

        if (PdButton("Continue", ImVec2(btnW, btnH))
            || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)
            || ImGui::IsKeyPressed(ImGuiKey_Enter, false)
            || ImGui::IsKeyPressed(ImGuiKey_Space, false))
        {
            menuhandlerFrFailedContinue(MENUOP_SET, nullptr, nullptr);
        }
    }

    ImGui::End();
    return 1;
}

static s32 renderFrCompleted(struct menudialog *dialog,
                              struct menu *menu,
                              s32 winW, s32 winH)
{
    return renderFrStats(dialog, menu, winW, winH, true);
}

static s32 renderFrFailed(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
    return renderFrStats(dialog, menu, winW, winH, false);
}

/* =========================================================================
 * Bio — Miscellaneous bio text (scrollable)
 * ========================================================================= */

static s32 renderBioText(struct menudialog *dialog,
                          struct menu *menu,
                          s32 winW, s32 winH)
{
    float diagW    = pdguiMenuWidth();
    float diagH    = pdguiMenuHeight();
    float footerH  = pdguiScale(50.0f);
    float titleH   = pdguiScale(26.0f);
    float contentH = diagH - titleH - footerH
                     - ImGui::GetStyle().WindowPadding.y * 2.0f;
    float childW   = diagW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    char  titleBuf[128];
    char *bioTitle;
    int   i;

    /* Title from the misc bio name (g_ChrBioSlot already set by list handler) */
    bioTitle = ciMenuTextMiscBioName(nullptr);
    snprintf(titleBuf, sizeof(titleBuf), "%s",
             (bioTitle && bioTitle[0]) ? bioTitle : "Information");
    for (i = (int)strlen(titleBuf) - 1;
         i >= 0 && (titleBuf[i] == '\n' || titleBuf[i] == '\r'); i--)
        titleBuf[i] = '\0';

    if (!beginTrainingWindow("##bio_text", titleBuf, winW, winH)) {
        ImGui::End();
        return 1;
    }

    ImGui::BeginChild("##bio_scroll", ImVec2(childW, contentH), true);
    {
        char *desc = ciGetMiscBioDescription();
        if (desc && desc[0]) {
            ImGui::TextWrapped("%s", desc);
        } else {
            ImGui::TextDisabled("(no data)");
        }
    }
    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float backW = pdguiScale(140.0f);
        float backH = pdguiScale(28.0f);
        ImGui::SetCursorPosX((diagW - backW) * 0.5f);

        if (PdButton("Back", ImVec2(backW, backH))
            || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
            || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * DT — Device Training completed / failed (shared renderer)
 * ========================================================================= */

static s32 renderDtResult(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH,
                           bool completed)
{
    float diagW    = pdguiMenuWidth();
    float diagH    = pdguiMenuHeight();
    float footerH  = pdguiScale(50.0f);
    float titleH   = pdguiScale(26.0f);
    float contentH = diagH - titleH - footerH
                     - ImGui::GetStyle().WindowPadding.y * 2.0f;
    float childW   = diagW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    const char *headline;
    float cx;

    if (!beginTrainingWindow(completed ? "##dt_done" : "##dt_fail",
                             "Training Stats", winW, winH)) {
        ImGui::End();
        return 1;
    }

    headline = completed ? "Completed!" : "Failed!";
    cx = (diagW - ImGui::CalcTextSize(headline).x) * 0.5f;
    ImGui::SetCursorPosX(cx > 0 ? cx : 0);
    ImGui::TextColored(completed
        ? ImVec4(0.1f, 1.0f, 0.3f, 1.0f)
        : ImVec4(1.0f, 0.25f, 0.2f, 1.0f),
        "%s", headline);

    ImGui::Separator();
    ImGui::Spacing();
    renderLabelRow("Time Taken:", dtMenuTextTimeTakenValue(nullptr));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("##dt_tip",
                      ImVec2(childW, contentH - pdguiScale(60.0f)), true);
    {
        char *tip = completed ? dtGetTip2() : dtGetTip1();
        if (tip && tip[0]) {
            ImGui::TextWrapped("%s", tip);
        }
    }
    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float btnW = pdguiScale(140.0f);
        float btnH = pdguiScale(28.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);

        if (PdButton("Continue", ImVec2(btnW, btnH))
            || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)
            || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        {
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

static s32 renderDtFailed(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
    return renderDtResult(dialog, menu, winW, winH, false);
}

static s32 renderDtCompleted(struct menudialog *dialog,
                              struct menu *menu,
                              s32 winW, s32 winH)
{
    return renderDtResult(dialog, menu, winW, winH, true);
}

/* =========================================================================
 * HT — Holo-Training list
 * ========================================================================= */

static int  s_HtSelectedSlot = 0;
static bool s_HtNeedsInit    = true;
static bool s_Registered     = false;

static s32 renderHtList(struct menudialog *dialog,
                         struct menu *menu,
                         s32 winW, s32 winH)
{
    float diagW    = pdguiMenuWidth();
    float diagH    = pdguiMenuHeight();
    float footerH  = pdguiScale(50.0f);
    float titleH   = pdguiScale(26.0f);
    float contentH = diagH - titleH - footerH
                     - ImGui::GetStyle().WindowPadding.y * 2.0f;
    float childW   = diagW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    int   numHt    = htGetNumUnlocked();
    int   i;

    if (s_HtNeedsInit) {
        s_HtSelectedSlot = (int)var80088bb4;
        if (s_HtSelectedSlot < 0 || s_HtSelectedSlot >= numHt)
            s_HtSelectedSlot = 0;
        s_HtNeedsInit = false;
    }

    if (!beginTrainingWindow("##ht_list", "Holotraining", winW, winH)) {
        ImGui::End();
        return 1;
    }

    ImGui::BeginChild("##ht_entries", ImVec2(childW, contentH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (numHt == 0) {
        ImGui::TextDisabled("No holo-training programmes unlocked.");
    }

    for (i = 0; i < numHt; i++) {
        bool   isSelected = (i == s_HtSelectedSlot);
        int    index      = htGetIndexBySlot(i);
        char  *name       = htGetName(index);

        ImGui::PushID(i);

        if (ImGui::Selectable(name ? name : "---", isSelected,
                              ImGuiSelectableFlags_None)) {
            s_HtSelectedSlot = i;
            var80088bb4 = (u8)i;
            pdguiPlaySound(PDGUI_SND_SELECT);
            s_HtNeedsInit = true;
            menuPushDialog(&g_HtDetailsMenuDialog);
        }

        if (isSelected && ImGui::IsWindowFocused()) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)
                || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true))
            {
                if (s_HtSelectedSlot < numHt - 1) {
                    s_HtSelectedSlot++;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)
                || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true))
            {
                if (s_HtSelectedSlot > 0) {
                    s_HtSelectedSlot--;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float backW = pdguiScale(140.0f);
        float backH = pdguiScale(28.0f);
        ImGui::SetCursorPosX((diagW - backW) * 0.5f);

        if (PdButton("Back", ImVec2(backW, backH))
            || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
            || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            s_HtNeedsInit = true;
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * HT — Failed / Completed (shared renderer)
 * ========================================================================= */

static s32 renderHtResult(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH,
                           bool completed)
{
    float diagW    = pdguiMenuWidth();
    float diagH    = pdguiMenuHeight();
    float footerH  = pdguiScale(50.0f);
    float titleH   = pdguiScale(26.0f);
    float contentH = diagH - titleH - footerH
                     - ImGui::GetStyle().WindowPadding.y * 2.0f;
    float childW   = diagW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    const char *headline;
    float cx;

    if (!beginTrainingWindow(completed ? "##ht_done" : "##ht_fail",
                             "Training Stats", winW, winH)) {
        ImGui::End();
        return 1;
    }

    headline = completed ? "Completed!" : "Failed!";
    cx = (diagW - ImGui::CalcTextSize(headline).x) * 0.5f;
    ImGui::SetCursorPosX(cx > 0 ? cx : 0);
    ImGui::TextColored(completed
        ? ImVec4(0.1f, 1.0f, 0.3f, 1.0f)
        : ImVec4(1.0f, 0.25f, 0.2f, 1.0f),
        "%s", headline);

    ImGui::Separator();
    ImGui::Spacing();
    renderLabelRow("Time Taken:", htMenuTextTimeTakenValue(nullptr));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("##ht_tip",
                      ImVec2(childW, contentH - pdguiScale(60.0f)), true);
    {
        char *tip = completed ? htGetTip2() : htGetTip1();
        if (tip && tip[0]) {
            ImGui::TextWrapped("%s", tip);
        }
    }
    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float btnW = pdguiScale(140.0f);
        float btnH = pdguiScale(28.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);

        if (PdButton("Continue", ImVec2(btnW, btnH))
            || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)
            || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        {
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

static s32 renderHtFailed(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
    return renderHtResult(dialog, menu, winW, winH, false);
}

static s32 renderHtCompleted(struct menudialog *dialog,
                              struct menu *menu,
                              s32 winW, s32 winH)
{
    return renderHtResult(dialog, menu, winW, winH, true);
}

/* =========================================================================
 * NowSafe — "It is now safe to turn off your computer."
 * ========================================================================= */

static s32 renderNowSafe(struct menudialog *dialog,
                          struct menu *menu,
                          s32 winW, s32 winH)
{
    float diagW  = pdguiMenuWidth();
    float diagH  = pdguiMenuHeight();
    float titleH = pdguiScale(26.0f);
    float midY   = titleH + (diagH - titleH) * 0.35f;
    float cx;
    static const char *msg = "It is now safe to turn off your computer.";

    if (!beginTrainingWindow("##nowsafe", "Cheats", winW, winH)) {
        ImGui::End();
        return 1;
    }

    ImGui::SetCursorPosY(midY);
    cx = (diagW - ImGui::CalcTextSize(msg).x) * 0.5f;
    ImGui::SetCursorPosX(cx > 0 ? cx : 0);
    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", msg);

    ImGui::Spacing();
    ImGui::Spacing();

    {
        float btnW = pdguiScale(140.0f);
        float btnH = pdguiScale(28.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);

        if (PdButton("Cancel", ImVec2(btnW, btnH))
            || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
            || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" {

void pdguiMenuTrainingRegister(void)
{
    if (s_Registered) {
        return;
    }
    s_Registered = true;

    /* ---- Firing Range ---- */
    pdguiHotswapRegister(&g_FrDifficultyMenuDialog,
                         renderFrDifficulty, "FR Difficulty");
    pdguiHotswapRegister(&g_FrTrainingInfoPreGameMenuDialog,
                         renderFrTrainingInfoPreGame, "FR Pre-Game Info");
    pdguiHotswapRegister(&g_FrTrainingInfoInGameMenuDialog,
                         renderFrTrainingInfoInGame, "FR In-Game Info");
    pdguiHotswapRegister(&g_FrCompletedMenuDialog,
                         renderFrCompleted, "FR Completed");
    pdguiHotswapRegister(&g_FrFailedMenuDialog,
                         renderFrFailed, "FR Failed");

    /* FR weapon list: custom GBI rendering — keep legacy */
    pdguiHotswapRegister(&g_FrWeaponListMenuDialog,
                         nullptr, "FR Weapon List (legacy)");

    /* ---- Biographies ---- */
    /* Bio list: needs opaque chrbio/miscbio structs — keep legacy */
    pdguiHotswapRegister(&g_BioListMenuDialog,
                         nullptr, "Bio List (legacy)");
    /* Bio profile: 3-D character model — keep legacy */
    pdguiHotswapRegister(&g_BioProfileMenuDialog,
                         nullptr, "Bio Profile (legacy)");
    pdguiHotswapRegister(&g_BioTextMenuDialog,
                         renderBioText, "Bio Text");

    /* ---- Device Training ---- */
    /* DT list: needs opaque device-name structs — keep legacy */
    pdguiHotswapRegister(&g_DtListMenuDialog,
                         nullptr, "DT List (legacy)");
    /* DT details: 3-D weapon model — keep legacy */
    pdguiHotswapRegister(&g_DtDetailsMenuDialog,
                         nullptr, "DT Details (legacy)");
    pdguiHotswapRegister(&g_DtFailedMenuDialog,
                         renderDtFailed, "DT Failed");
    pdguiHotswapRegister(&g_DtCompletedMenuDialog,
                         renderDtCompleted, "DT Completed");

    /* ---- Holo Training ---- */
    pdguiHotswapRegister(&g_HtListMenuDialog,
                         renderHtList, "HT List");
    /* HT details: MENUITEMTYPE_MODEL — keep legacy */
    pdguiHotswapRegister(&g_HtDetailsMenuDialog,
                         nullptr, "HT Details (legacy)");
    pdguiHotswapRegister(&g_HtFailedMenuDialog,
                         renderHtFailed, "HT Failed");
    pdguiHotswapRegister(&g_HtCompletedMenuDialog,
                         renderHtCompleted, "HT Completed");

    /* Misc */
    pdguiHotswapRegister(&g_NowSafeMenuDialog,
                         renderNowSafe, "Now Safe");

    /* ---- Hangar — all keep legacy (3-D models / GBI texture renders) ---- */
    pdguiHotswapRegister(&g_HangarListMenuDialog,
                         nullptr, "Hangar List (legacy)");
    pdguiHotswapRegister(&g_HangarVehicleHolographMenuDialog,
                         nullptr, "Hangar Holograph (legacy)");
    pdguiHotswapRegister(&g_HangarVehicleDetailsMenuDialog,
                         nullptr, "Hangar Vehicle Details (legacy)");
    pdguiHotswapRegister(&g_HangarLocationDetailsMenuDialog,
                         nullptr, "Hangar Location Details (legacy)");

    sysLogPrintf(LOG_NOTE, "pdgui_menu_training: registered (22 dialogs)");
}

} /* extern "C" */
