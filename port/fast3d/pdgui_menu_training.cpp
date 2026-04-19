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
#include "pdgui_model_preview.h"
#include "pdgui_charpreview.h"
#include "system.h"
#include "inputctx.h"
#include "menupool.h"

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

/* FR session setup — must be called before entering a challenge */
void frLoadData(void);

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

/* Batch 10 legacy SET callbacks (training.h / menu.h) -- invoked directly
 * from the ImGui button handlers on DT/HT Details.  They have no
 * parameters and don't touch the menuitem, so we call them as plain
 * functions bypassing the handlerdata shadow-struct. */
void dtBegin(void);
void dtEnd(void);
void htBegin(void);
void htEnd(void);
void func0f0f8120(void);

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

/* ========================================================================
 * Batch 10 bridge accessors (pdgui_bridge.c) -- all of these are small
 * wrappers that sit over the legacy trainingmenus.c providers so the C++
 * renderer can read them without pulling in types.h (bool redefinition).
 * ======================================================================== */

/* Firing Range */
s32         pdguiTrFrNumWeaponsAvailable(void);
u32         pdguiTrFrWeaponBySlot(s32 slot);
const char *pdguiTrFrWeaponName(u32 weaponnum);
s32         pdguiTrFrWeaponScoreTier(u32 weaponnum);
u32         pdguiTrFrWeaponFilenum(u32 weaponnum);
s32         pdguiTrFrGetSlot(void);
void        pdguiTrFrSetSlot(s32 slot);
const char *pdguiTrFrWeaponDescription(void);
s32         pdguiTrFrIsInTraining(void);

/* Bios (characters + misc) */
s32         pdguiTrBioNumChr(void);
s32         pdguiTrBioNumMisc(void);
const char *pdguiTrBioChrName(s32 slot);
const char *pdguiTrBioMiscName(s32 slot);
s32         pdguiTrBioGetSlot(void);
void        pdguiTrBioSetSlot(s32 slot);
const char *pdguiTrBioChrAge(void);
const char *pdguiTrBioChrRace(void);
const char *pdguiTrBioChrDescription(void);
const char *pdguiTrBioMiscDescription(void);
void        pdguiTrBioGetCurrentChrCatalogIds(const char **head_id_out,
                                              const char **body_id_out);

/* Device Training */
s32         pdguiTrDtNumAvailable(void);
u32         pdguiTrDtWeaponBySlot(s32 slot);
const char *pdguiTrDtDeviceName(s32 slot);
s32         pdguiTrDtGetSlot(void);
void        pdguiTrDtSetSlot(s32 slot);
const char *pdguiTrDtCurrentDescription(void);
u32         pdguiTrDtCurrentWeaponFilenum(void);
s32         pdguiTrDtIsInTraining(void);

/* Holo Training */
s32         pdguiTrHtGetSlot(void);
void        pdguiTrHtSetSlot(s32 slot);
const char *pdguiTrHtCurrentDescription(void);
s32         pdguiTrHtIsInTraining(void);
u32         pdguiTrHtCurrentWeaponFilenum(void);

/* Hangar */
s32         pdguiTrHangarNumTotal(void);
s32         pdguiTrHangarNumLocations(void);
s32         pdguiTrHangarGetSlot(void);
void        pdguiTrHangarSetSlot(s32 slot);
s32         pdguiTrHangarSlotIsLocation(s32 slot);
const char *pdguiTrHangarSlotName(s32 slot);
const char *pdguiTrHangarCurrentFullName(void);
const char *pdguiTrHangarCurrentSubheading(void);
const char *pdguiTrHangarCurrentDescription(void);
u32         pdguiTrHangarCurrentVehicleFilenum(void);

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
    /* M-14 (C2 preview-dock invariant): Bio Profile, Training Details
     * (DT/HT weapon preview), and Hangar Holograph all render their 3D
     * previews via pdguiModelPreviewDraw/drawFilenumPreview at absolute
     * screen coords computed from the window origin. If this outer window
     * ever gained a scrollbar, those absolute-coord previews would not
     * scroll with the content beneath them — visually broken. Force the
     * outer frame to be strictly non-scrolling so the preview overlays are
     * always pinned to the correct pixel position. */
    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoScrollbar
                            | ImGuiWindowFlags_NoScrollWithMouse;

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
                          pdguiPalImU32(PDPAL_BODYBG, 255));
    }

    pdguiDrawPdDialog(pos.x, pos.y, diagW, diagH, title, 1);

    /* Title row */
    {
        float titleH = pdguiScale(39.0f);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(pos.x + 8.0f, pos.y + 2.0f, diagW - 16.0f, titleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(pos.x + (diagW - ts.x) * 0.5f,
                           pos.y + (titleH - ts.y) * 0.5f),
                    pdguiPalImU32(PDPAL_TITLEFG, 255), title);
        pdguiSetCursorBelowTitle(titleH);
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
    ImGui::SameLine(pdguiScale(225.0f));
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
    float btnW     = pdguiScale(270.0f);
    float btnH     = pdguiScale(48.0f);
    float footerH  = pdguiScale(75.0f);
    float pad      = pdguiScale(15.0f);
    float contentH = diagH - pdguiScale(39.0f) - footerH
                     - ImGui::GetStyle().WindowPadding.y;
    float startY   = pdguiScale(39.0f) + ImGui::GetStyle().WindowPadding.y
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

    /* M-21 progressive focus: landing on Bronze (always available) gives
     * controller users a valid D-pad target on the first frame. The
     * subsequent PdButton calls — Silver / Gold — pick up focus via
     * normal nav once focus is anchored somewhere in this window. */
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere(0);
    }

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
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float backW = pdguiScale(210.0f);
        float backH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - backW) * 0.5f);

        if (PdButton("Cancel", ImVec2(backW, backH))
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
    float titleH   = pdguiScale(39.0f);
    float footerH  = pdguiScale(87.0f);
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
                      ImVec2(childW, contentH * 0.45f - pdguiScale(9.0f)), true);
    {
        char *desc = frGetWeaponDescription();
        if (desc && desc[0]) {
            ImGui::TextWrapped("%s", desc);
        }
    }
    ImGui::EndChild();

    /* Footer: two action buttons */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    btnW = (childW - pdguiScale(18.0f)) * 0.5f;
    btnH = pdguiScale(45.0f);

    if (PdButton(inGame ? "Resume" : "Ok", ImVec2(btnW, btnH)))
    {
        frDetailsOkMenuHandler(MENUOP_SET, nullptr, nullptr);
    }

    ImGui::SameLine(0, pdguiScale(18.0f));

    if (PdButton(inGame ? "Abort" : "Cancel", ImVec2(btnW, btnH))
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
    float footerH = pdguiScale(75.0f);
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
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float btnW = pdguiScale(240.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);

        if (PdButton("Continue", ImVec2(btnW, btnH))
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
    float footerH  = pdguiScale(75.0f);
    float titleH   = pdguiScale(39.0f);
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
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float backW = pdguiScale(210.0f);
        float backH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - backW) * 0.5f);

        if (PdButton("Back", ImVec2(backW, backH))
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
    float footerH  = pdguiScale(75.0f);
    float titleH   = pdguiScale(39.0f);
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
                      ImVec2(childW, contentH - pdguiScale(90.0f)), true);
    {
        char *tip = completed ? dtGetTip2() : dtGetTip1();
        if (tip && tip[0]) {
            ImGui::TextWrapped("%s", tip);
        }
    }
    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);

        if (PdButton("Continue", ImVec2(btnW, btnH))
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
    float footerH  = pdguiScale(75.0f);
    float titleH   = pdguiScale(39.0f);
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

    /* M-21 progressive focus: on first frame, land focus on the selected
     * row so D-pad/Enter advances into Details without a preparatory
     * button press. */
    bool htFocusOnAppear = ImGui::IsWindowAppearing();

    for (i = 0; i < numHt; i++) {
        bool   isSelected = (i == s_HtSelectedSlot);
        int    index      = htGetIndexBySlot(i);
        char  *name       = htGetName(index);

        ImGui::PushID(i);

        if (htFocusOnAppear && isSelected) {
            ImGui::SetKeyboardFocusHere(0);
        }
        if (ImGui::Selectable(name ? name : "---", isSelected,
                              ImGuiSelectableFlags_None)) {
            s_HtSelectedSlot = i;
            var80088bb4 = (u8)i;
            pdguiPlaySound(PDGUI_SND_SELECT);
            s_HtNeedsInit = true;
            menuPushDialog(&g_HtDetailsMenuDialog);
        }

        if (isSelected && ImGui::IsWindowFocused()) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
            {
                if (s_HtSelectedSlot < numHt - 1) {
                    s_HtSelectedSlot++;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
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
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float backW = pdguiScale(210.0f);
        float backH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - backW) * 0.5f);

        if (PdButton("Back", ImVec2(backW, backH))
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
    float footerH  = pdguiScale(75.0f);
    float titleH   = pdguiScale(39.0f);
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
                      ImVec2(childW, contentH - pdguiScale(90.0f)), true);
    {
        char *tip = completed ? htGetTip2() : htGetTip1();
        if (tip && tip[0]) {
            ImGui::TextWrapped("%s", tip);
        }
    }
    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);

        if (PdButton("Continue", ImVec2(btnW, btnH))
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
    float titleH = pdguiScale(39.0f);
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
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);

        if (PdButton("Cancel", ImVec2(btnW, btnH))
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
 * Batch 10: NULL-FN replacements (Training / Hangar — 10 dialogs)
 *
 * The 10 dialogs below were previously registered with nullptr (forcing
 * legacy fallback) because they depended on either: (a) the generalized
 * model preview pipeline (Batch 0), (b) access to opaque C structs like
 * chrbio/miscbio/hangarbio, or (c) GBI-only custom render logic (FR
 * weapon-list star rendering).  Batch 10 implements real renderers:
 *
 *   - (a) is solved by the Batch-0 pdguiModelPreviewDraw* API + the
 *     pdguiCharPreview FBO path (CHARACTER/WEAPON/VEHICLE kinds wired).
 *   - (b) is solved by Batch 10 bridge accessors in pdgui_bridge.c that
 *     expose chrbio/miscbio/hangarbio fields + slot state without
 *     including types.h in the C++ renderer.
 *   - (c) is replaced by native ImGui visuals (label + score-tier dots).
 *
 * Zero function loss: legacy dialog handlers / data providers are
 * UNTOUCHED; every state mutation routes through
 * pdguiTr*SetSlot / legacy descriptions / catalog accessors so the
 * legacy storage layout (g_ChrBioSlot, g_DtSlot, etc.) is the single
 * source of truth.  Legacy dialog-handler OPEN/TICK/CLOSE paths still
 * fire via the menu runtime (hot-swap only intercepts RENDER), so all
 * side effects like frInitAmmo / func0f1a1ac0 / func0f1a2198 / etc.
 * continue to execute exactly as on the legacy path.
 *
 * Network wiring: this batch is almost entirely local single-player
 * training/bio/hangar content.  See context/scratch/D5-P3-batch10-2026-04-11.md
 * for the full audit.
 * ========================================================================= */

/* -----------------------------------------------------------------------
 * Small helper: draw a self-contained 3D model preview panel keyed by a
 * file number instead of a catalog ID.  Used by DT Details, HT Details
 * and Hangar Holograph where the source data stores raw file indices, not
 * catalog ID strings.
 *
 * Internally: set rotation, request the FBO render via
 * pdguiCharPreviewRequestFilenum, draw the resulting texture or a
 * placeholder silhouette.  Mirrors the visual style of
 * pdguiModelPreviewDraw so the two feel identical on screen.
 * --------------------------------------------------------------------- */

static f32 s_Batch10IdleAngle = 0.0f;

static void drawFilenumPreview(s32 kind, u32 filenum, const char *label,
                                f32 x, f32 y, f32 w, f32 h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 bgCol     = pdguiPalImU32(PDPAL_BODYBG,  240);
    ImU32 borderCol = pdguiPalImU32(PDPAL_BORDER1, 200);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bgCol, 4.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), borderCol, 4.0f, 0, 2.0f);

    /* Idle rotation — bumped per frame so the model slowly turns. */
    f32 dt = ImGui::GetIO().DeltaTime;
    s_Batch10IdleAngle += 0.3f * dt;
    const f32 twoPi = 6.2831853f;
    if (s_Batch10IdleAngle > twoPi) s_Batch10IdleAngle -= twoPi;
    pdguiCharPreviewSetRotY(s_Batch10IdleAngle);

    if (filenum != 0) {
        pdguiCharPreviewRequestFilenum((PdguiPreviewType)kind, filenum);
    }

    float pad = 2.0f;
    float cx = x + pad;
    float cy = y + pad;
    float cw = w - pad * 2.0f;
    float labelH = label && label[0] ? 16.0f : 0.0f;
    float ch = h - pad * 2.0f - labelH;

    u32 texId = pdguiCharPreviewGetTextureId();
    if (texId != 0 && pdguiCharPreviewIsReady() && filenum != 0) {
        /* FBO textures are vertically flipped: UV0 = (0,1), UV1 = (1,0). */
        dl->AddImage((ImTextureID)(uintptr_t)texId,
                     ImVec2(cx, cy), ImVec2(cx + cw, cy + ch),
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        /* Placeholder: neutral box silhouette. */
        float pcx = cx + cw * 0.5f;
        float pcy = cy + ch * 0.4f;
        ImU32 silCol = pdguiPalImU32(PDPAL_BORDER1, 160);
        dl->AddRect(ImVec2(pcx - cw * 0.25f, pcy - ch * 0.15f),
                    ImVec2(pcx + cw * 0.25f, pcy + ch * 0.15f),
                    silCol, 1.5f, 0, 2.0f);

        const char *msg = filenum != 0 ? "Loading..." : "No model";
        ImVec2 msgSz = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(pcx - msgSz.x * 0.5f, cy + ch * 0.75f),
                    pdguiPalImU32(PDPAL_ITEM_DISABLED, 180), msg);
    }

    if (label && label[0]) {
        /* Strip trailing newline from legacy weapon names. */
        char tmp[96];
        snprintf(tmp, sizeof(tmp), "%s", label);
        for (int i = (int)strlen(tmp) - 1;
             i >= 0 && (tmp[i] == '\n' || tmp[i] == '\r'); i--)
            tmp[i] = '\0';

        ImVec2 sz = ImGui::CalcTextSize(tmp);
        dl->AddText(ImVec2(x + (w - sz.x) * 0.5f, y + h - labelH),
                    pdguiPalImU32(PDPAL_ITEM_UNFOCUSED, 220), tmp);
    }
}

/* Small helper: strip trailing newlines returned by legacy text fns. */
static void stripNewline(char *buf)
{
    if (!buf) return;
    for (int i = (int)strlen(buf) - 1;
         i >= 0 && (buf[i] == '\n' || buf[i] == '\r'); i--)
        buf[i] = '\0';
}

/* Shared list navigation: handle D-pad + arrow key cursor movement. */
static bool listHandleKeyboardNav(s32 *cursor, s32 count)
{
    bool moved = false;
    if (count <= 0) return false;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        if (*cursor < count - 1) {
            (*cursor)++;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            moved = true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        if (*cursor > 0) {
            (*cursor)--;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            moved = true;
        }
    }
    return moved;
}

/* Shared "Back pressed" check for any Batch-10 dialog. */
static bool backPressed(void)
{
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

/* =========================================================================
 * FR Weapon List (g_FrWeaponListMenuDialog)
 *
 * Legacy: custom GBI render — weapon name + 3 colored star icons showing
 * the player's score tier (bronze/silver/gold).  ImGui version renders a
 * list of weapons with a native star row after each label.
 * ========================================================================= */

static s32 s_FrWeaponCursor = -1;
/* S300: s_FrWeaponPushedCtx removed — menu pool owns the ctx for
 * MENU_TYPE_TRAINING via menupoolAcquireDialog / menupoolReleaseDialog. */

static void frDrawScoreStars(float x, float y, s32 tier)
{
    /* tier: 0 = none, 1 = bronze (1 star), 2 = silver (2 stars), 3 = gold (3 stars) */
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float r = 5.0f;
    const float sp = 14.0f;
    const ImU32 inactive = IM_COL32(60, 60, 60, 180);
    const ImU32 bronze   = IM_COL32(180, 108, 44,  255);
    const ImU32 silver   = IM_COL32(200, 200, 210, 255);
    const ImU32 gold     = IM_COL32(220, 180, 60,  255);

    for (int i = 0; i < 3; i++) {
        ImU32 c = inactive;
        if (tier > i) {
            switch (i) {
            case 0: c = bronze; break;
            case 1: c = silver; break;
            case 2: c = gold;   break;
            }
        }
        dl->AddCircleFilled(ImVec2(x + i * sp + r, y + r), r, c, 12);
        dl->AddCircle      (ImVec2(x + i * sp + r, y + r), r,
                             IM_COL32(255, 255, 255, 90), 12, 1.0f);
    }
}

static s32 renderFrWeaponList(struct menudialog *dialog,
                               struct menu *menu,
                               s32 winW, s32 winH)
{
    if (!beginTrainingWindow("##fr_weapon_list", "Weapon", winW, winH)) {
        ImGui::End();
        /* S295 F4 leak guard — S300: pool owns ctx, release pops it. */
        menupoolReleaseDialog(menupoolDialogDef(dialog));
        return 1;
    }

    /* Push ImGui menu input context via the pool so gamepad d-pad and A/B
     * route to ImGui instead of gameplay.  Required because this menu opens
     * from CI gameplay (walking around Carrington Institute), not from
     * another ImGui menu.  S300: pool attaches the ctx to MENU_TYPE_TRAINING. */
    if (ImGui::IsWindowAppearing()) {
        menupoolAcquireDialog(menupoolDialogDef(dialog),
                              &g_CtxImGuiMenu);
        ImGui::SetWindowFocus();
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float childH  = diagH - titleH - footerH
                    - ImGui::GetStyle().WindowPadding.y * 2.0f;

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    s32 count = pdguiTrFrNumWeaponsAvailable();
    if (count < 0) count = 0;

    if (s_FrWeaponCursor < 0 || s_FrWeaponCursor >= count) {
        s_FrWeaponCursor = pdguiTrFrGetSlot();
        if (s_FrWeaponCursor < 0 || s_FrWeaponCursor >= count) {
            s_FrWeaponCursor = 0;
        }
    }

    listHandleKeyboardNav(&s_FrWeaponCursor, count);

    /* A button / Enter confirms the currently highlighted weapon.
     * Must match legacy frWeaponListMenuHandler MENUOP_SET behavior:
     * frLoadData() + frSetSlot() + frSetDifficulty() before pushing
     * the sub-dialog.  Without frLoadData(), the pre-game info dialog
     * reads uninitialized g_FrData → ACCESS_VIOLATION. */
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (s_FrWeaponCursor >= 0 && s_FrWeaponCursor < count) {
            frLoadData();
            pdguiTrFrSetSlot(s_FrWeaponCursor);
            u32 weaponnum = pdguiTrFrWeaponBySlot(s_FrWeaponCursor);
            s32 tier = pdguiTrFrWeaponScoreTier(weaponnum);
            if (tier > 0) {
                frSetDifficulty(tier);
                menuPushDialog(&g_FrDifficultyMenuDialog);
            } else {
                frSetDifficulty(FRDIFFICULTY_BRONZE);
                menuPushDialog(&g_FrTrainingInfoPreGameMenuDialog);
            }
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
    }

    ImGui::BeginChild("##fr_wl_body",
                      ImVec2(diagW - ImGui::GetStyle().WindowPadding.x * 2.0f,
                             childH), false);
    /* M-21 progressive focus: on first frame focus lands on the currently
     * selected weapon row so A immediately advances into difficulty/pre-
     * game without the user having to D-pad to re-anchor. */
    bool frWlFocusOnAppear = ImGui::IsWindowAppearing();
    for (s32 i = 0; i < count; i++) {
        u32 weaponnum = pdguiTrFrWeaponBySlot(i);
        const char *raw = pdguiTrFrWeaponName(weaponnum);
        char nameBuf[96];
        snprintf(nameBuf, sizeof(nameBuf), "%s", raw ? raw : "???");
        stripNewline(nameBuf);

        ImGui::PushID(i);
        bool sel = (i == s_FrWeaponCursor);
        const float rowH = pdguiScale(24.0f);
        if (frWlFocusOnAppear && sel) {
            ImGui::SetKeyboardFocusHere(0);
        }
        if (ImGui::Selectable("##fr_wl_row", sel, 0, ImVec2(0, rowH))) {
            s_FrWeaponCursor = i;
            /* Match legacy frWeaponListMenuHandler MENUOP_SET behavior:
             * frLoadData loads the FR challenge config for the weapon. */
            frLoadData();
            pdguiTrFrSetSlot(i);
            s32 tier = pdguiTrFrWeaponScoreTier(weaponnum);
            if (tier > 0) {
                frSetDifficulty(tier);
                menuPushDialog(&g_FrDifficultyMenuDialog);
            } else {
                frSetDifficulty(FRDIFFICULTY_BRONZE);
                menuPushDialog(&g_FrTrainingInfoPreGameMenuDialog);
            }
            pdguiPlaySound(PDGUI_SND_SELECT);
        }

        if (ImGui::IsItemHovered()) {
            s_FrWeaponCursor = i;
        }

        /* Overlay: label on the left, star tier on the right. */
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        ImU32 labelCol = sel
            ? pdguiPalImU32(PDPAL_ITEM_FOCUSED,   255)
            : pdguiPalImU32(PDPAL_ITEM_UNFOCUSED, 220);
        dl->AddText(ImVec2(rmin.x + pdguiScale(10.0f),
                           rmin.y + (rmax.y - rmin.y - 14.0f) * 0.5f),
                    labelCol, nameBuf);

        s32 tier = pdguiTrFrWeaponScoreTier(weaponnum);
        frDrawScoreStars(rmax.x - pdguiScale(60.0f),
                          rmin.y + (rmax.y - rmin.y - 10.0f) * 0.5f,
                          tier);

        ImGui::PopID();
    }
    ImGui::EndChild();

    /* Footer: Back */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
        if (PdButton("Back", ImVec2(btnW, btnH))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            /* S300: menuCloseDialog releases pool slot + pops owned ctx. */
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Bio List (g_BioListMenuDialog) -- grouped list of Character Profiles and
 * Other Information.  Legacy handler: ciOfficeInformationMenuHandler.
 * ========================================================================= */

static s32 s_BioCursor = -1;

static s32 renderBioList(struct menudialog *dialog,
                          struct menu *menu,
                          s32 winW, s32 winH)
{
    if (!beginTrainingWindow("##bio_list", "Information", winW, winH)) {
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float childH  = diagH - titleH - footerH
                    - ImGui::GetStyle().WindowPadding.y * 2.0f;

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    s32 numChr  = pdguiTrBioNumChr();
    s32 numMisc = pdguiTrBioNumMisc();
    s32 total   = numChr + numMisc;
    if (total <= 0) {
        ImGui::TextDisabled("No bios unlocked yet.");
        ImGui::End();
        return 1;
    }

    if (s_BioCursor < 0 || s_BioCursor >= total) {
        s_BioCursor = pdguiTrBioGetSlot();
        if (s_BioCursor < 0 || s_BioCursor >= total) s_BioCursor = 0;
    }
    listHandleKeyboardNav(&s_BioCursor, total);

    ImGui::BeginChild("##bio_body",
                      ImVec2(diagW - ImGui::GetStyle().WindowPadding.x * 2.0f,
                             childH), false);

    if (numChr > 0) {
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
                           "Character Profiles");
        ImGui::Separator();
        for (s32 i = 0; i < numChr; i++) {
            const char *name = pdguiTrBioChrName(i);
            char buf[96];
            snprintf(buf, sizeof(buf), "%s", name ? name : "???");
            stripNewline(buf);
            ImGui::PushID(i);
            bool sel = (i == s_BioCursor);
            if (ImGui::Selectable(buf, sel, 0,
                                  ImVec2(0, pdguiScale(22.0f)))) {
                s_BioCursor = i;
                pdguiTrBioSetSlot(i);
                menuPushDialog(&g_BioProfileMenuDialog);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            if (ImGui::IsItemHovered()) s_BioCursor = i;
            ImGui::PopID();
        }
        ImGui::Spacing();
    }

    if (numMisc > 0) {
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
                           "Other Information");
        ImGui::Separator();
        for (s32 i = 0; i < numMisc; i++) {
            const char *name = pdguiTrBioMiscName(i);
            char buf[96];
            snprintf(buf, sizeof(buf), "%s", name ? name : "???");
            stripNewline(buf);
            ImGui::PushID(numChr + i);
            s32 slot = numChr + i;
            bool sel = (slot == s_BioCursor);
            if (ImGui::Selectable(buf, sel, 0,
                                  ImVec2(0, pdguiScale(22.0f)))) {
                s_BioCursor = slot;
                pdguiTrBioSetSlot(slot);
                menuPushDialog(&g_BioTextMenuDialog);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            if (ImGui::IsItemHovered()) s_BioCursor = slot;
            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
        if (PdButton("Back", ImVec2(btnW, btnH))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Bio Profile (g_BioProfileMenuDialog) -- 3D character preview + labels
 * ========================================================================= */

static s32 renderBioProfile(struct menudialog *dialog,
                             struct menu *menu,
                             s32 winW, s32 winH)
{
    if (!beginTrainingWindow("##bio_profile", "Character Profile", winW, winH)) {
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float padY    = ImGui::GetStyle().WindowPadding.y;

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    /* Two-column layout: 3D preview on the left, labels/description on right. */
    ImVec2 winMin = ImGui::GetWindowPos();
    float previewW = pdguiScale(300.0f);
    float previewH = pdguiScale(340.0f);
    float previewX = winMin.x + pdguiScale(28.0f);
    float previewY = winMin.y + titleH + padY;

    const char *head_id = nullptr;
    const char *body_id = nullptr;
    pdguiTrBioGetCurrentChrCatalogIds(&head_id, &body_id);

    ModelPreviewOpts opts = pdguiModelPreviewDefaultOpts();
    opts.showBodyName = 0;
    opts.showHeadName = 0;
    pdguiModelPreviewDraw(head_id, body_id, previewX, previewY, previewW, previewH, &opts);

    /* Right column: name / age / race / description */
    float rightX = previewX - winMin.x + previewW + pdguiScale(20.0f);
    float rightW = diagW - rightX - pdguiScale(28.0f);
    if (rightW < pdguiScale(200.0f)) rightW = pdguiScale(200.0f);

    ImGui::SetCursorPos(ImVec2(rightX, titleH + padY));
    ImGui::BeginChild("##bp_right",
                      ImVec2(rightW, diagH - titleH - footerH - padY * 2),
                      false);
    {
        const char *name = pdguiTrBioChrName(pdguiTrBioGetSlot());
        char nameBuf[96];
        snprintf(nameBuf, sizeof(nameBuf), "%s", name ? name : "???");
        stripNewline(nameBuf);
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 1.0f, 1.0f), "%s", nameBuf);
        ImGui::Separator();
        ImGui::Spacing();

        renderLabelRow("Age:",  pdguiTrBioChrAge());
        renderLabelRow("Race:", pdguiTrBioChrRace());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const char *desc = pdguiTrBioChrDescription();
        if (desc && desc[0]) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
        if (PdButton("Back", ImVec2(btnW, btnH))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * DT List (g_DtListMenuDialog) -- list of unlocked device training items
 * ========================================================================= */

static s32 s_DtCursor = -1;

static s32 renderDtList(struct menudialog *dialog,
                         struct menu *menu,
                         s32 winW, s32 winH)
{
    if (!beginTrainingWindow("##dt_list", "Device List", winW, winH)) {
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float childH  = diagH - titleH - footerH
                    - ImGui::GetStyle().WindowPadding.y * 2.0f;

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    s32 count = pdguiTrDtNumAvailable();
    if (count < 0) count = 0;

    if (s_DtCursor < 0 || s_DtCursor >= count) {
        s_DtCursor = pdguiTrDtGetSlot();
        if (s_DtCursor < 0 || s_DtCursor >= count) s_DtCursor = 0;
    }
    listHandleKeyboardNav(&s_DtCursor, count);

    ImGui::BeginChild("##dt_body",
                      ImVec2(diagW - ImGui::GetStyle().WindowPadding.x * 2.0f,
                             childH), false);
    /* M-21 progressive focus: land focus on the currently-selected device
     * on first frame so D-pad / Enter immediately advances to details. */
    bool dtFocusOnAppear = ImGui::IsWindowAppearing();
    for (s32 i = 0; i < count; i++) {
        const char *name = pdguiTrDtDeviceName(i);
        char buf[96];
        snprintf(buf, sizeof(buf), "%s", name ? name : "???");
        stripNewline(buf);
        ImGui::PushID(i);
        bool sel = (i == s_DtCursor);
        if (dtFocusOnAppear && sel) {
            ImGui::SetKeyboardFocusHere(0);
        }
        if (ImGui::Selectable(buf, sel, 0, ImVec2(0, pdguiScale(22.0f)))) {
            s_DtCursor = i;
            pdguiTrDtSetSlot(i);
            menuPushDialog(&g_DtDetailsMenuDialog);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        if (ImGui::IsItemHovered()) s_DtCursor = i;
        ImGui::PopID();
    }
    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
        if (PdButton("Back", ImVec2(btnW, btnH))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Shared 3D-preview-and-text renderer used by DT Details and HT Details.
 * Legacy path: MENUITEMTYPE_SCROLLABLE (description) + MENUITEMTYPE_MODEL
 * (weapon display) + OK/Cancel selectables.  ImGui version: side-by-side
 * panels + docked action bar.
 *
 * `begin`/`end` handlers are the legacy SET callbacks for the Ok/Cancel
 * rows.  We store them as callbacks passed in by each dialog's renderer.
 * ========================================================================= */

typedef void (*TrainingVoidFn)(void);

static s32 renderTrainingDetailsImpl(const char *imguiId,
                                      const char *title,
                                      const char *deviceName,
                                      const char *description,
                                      u32 weaponFilenum,
                                      s32 isInTraining,
                                      TrainingVoidFn onBegin,
                                      TrainingVoidFn onAbort,
                                      s32 winW, s32 winH)
{
    if (!beginTrainingWindow(imguiId, title, winW, winH)) {
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float padY    = ImGui::GetStyle().WindowPadding.y;

    if (backPressed()) {
        if (onAbort) onAbort();
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    /* Two-column: left = description (wide), right = 3D weapon preview. */
    ImVec2 winMin   = ImGui::GetWindowPos();
    float previewW  = pdguiScale(220.0f);
    float previewH  = pdguiScale(220.0f);
    float previewX  = winMin.x + diagW - previewW - pdguiScale(28.0f);
    float previewY  = winMin.y + titleH + padY + pdguiScale(12.0f);

    char nameBuf[96] = "";
    if (deviceName && deviceName[0]) {
        snprintf(nameBuf, sizeof(nameBuf), "%s", deviceName);
        stripNewline(nameBuf);
    }

    drawFilenumPreview(PDGUI_PREVIEW_WEAPON, weaponFilenum,
                        nameBuf, previewX, previewY, previewW, previewH);

    /* Left column: description */
    float leftW = previewX - winMin.x - pdguiScale(20.0f);
    if (leftW < pdguiScale(220.0f)) leftW = pdguiScale(220.0f);

    ImGui::SetCursorPos(ImVec2(pdguiScale(28.0f), titleH + padY));
    ImGui::BeginChild("##tr_det_left",
                      ImVec2(leftW,
                             diagH - titleH - footerH - padY * 2),
                      false);
    if (description && description[0]) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    /* Footer: Ok/Resume + Cancel/Abort */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(195.0f);
        float btnH = pdguiScale(42.0f);
        float totalW = btnW * 2.0f + pdguiScale(12.0f);
        ImGui::SetCursorPosX((diagW - totalW) * 0.5f);

        const char *okLabel     = isInTraining ? "Resume" : "Ok";
        const char *cancelLabel = isInTraining ? "Abort"  : "Cancel";

        /* M-21 progressive focus (menu-stack §6.4): this Details dialog is
         * the leaf of the "challenge -> details -> start" flow for FR/DT/
         * HT. When it appears, controller/keyboard focus lands on the
         * Ok/Resume button so the next A press launches training — the
         * user already chose the item on the prior screen, so the details
         * dialog exists to confirm, not re-browse. */
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(0);
        }

        if (PdButton(okLabel, ImVec2(btnW, btnH))) {
            if (onBegin) onBegin();
            pdguiPlaySound(PDGUI_SND_SELECT);
            menuPopDialog();
        }
        ImGui::SameLine(0, pdguiScale(12.0f));
        if (PdButton(cancelLabel, ImVec2(btnW, btnH))) {
            if (onAbort) onAbort();
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* -----------------------------------------------------------------------
 * DT Details (g_DtDetailsMenuDialog)
 * Legacy SET callbacks: `dtBegin` (from menuhandlerDtOkOrResume) and
 * `dtEnd` (from menuhandler001a6514).  Preserved via onBegin/onAbort.
 * --------------------------------------------------------------------- */

static void dt_Begin_cb(void)
{
    dtBegin();
    func0f0f8120();
}

static void dt_End_cb(void)
{
    dtEnd();
}

static s32 renderDtDetails(struct menudialog *dialog,
                            struct menu *menu,
                            s32 winW, s32 winH)
{
    const char *name = pdguiTrDtDeviceName(pdguiTrDtGetSlot());
    const char *desc = pdguiTrDtCurrentDescription();
    u32 filenum      = pdguiTrDtCurrentWeaponFilenum();
    s32 training     = pdguiTrDtIsInTraining();

    return renderTrainingDetailsImpl("##dt_details", "Device Training",
                                      name, desc, filenum, training,
                                      dt_Begin_cb, dt_End_cb, winW, winH);
}

/* -----------------------------------------------------------------------
 * HT Details (g_HtDetailsMenuDialog)
 * Legacy SET callbacks: `htBegin` (from menuhandler001a6a34) and
 * `htEnd` (from menuhandler001a6a70).
 * --------------------------------------------------------------------- */

static void ht_Begin_cb(void)
{
    htBegin();
    func0f0f8120();
}

static void ht_End_cb(void)
{
    htEnd();
}

static s32 renderHtDetails(struct menudialog *dialog,
                            struct menu *menu,
                            s32 winW, s32 winH)
{
    const char *name = htGetName(htGetIndexBySlot(pdguiTrHtGetSlot()));
    const char *desc = pdguiTrHtCurrentDescription();
    u32 filenum      = pdguiTrHtCurrentWeaponFilenum();
    s32 training     = pdguiTrHtIsInTraining();

    return renderTrainingDetailsImpl("##ht_details", "Holotraining",
                                      name, desc, filenum, training,
                                      ht_Begin_cb, ht_End_cb, winW, winH);
}

/* =========================================================================
 * Hangar List (g_HangarListMenuDialog) -- grouped list of Locations and
 * Vehicles.  Legacy handler: ciHangarInformationMenuHandler.
 * ========================================================================= */

static s32 s_HangarCursor = -1;

static s32 renderHangarList(struct menudialog *dialog,
                             struct menu *menu,
                             s32 winW, s32 winH)
{
    if (!beginTrainingWindow("##hgr_list", "Hangar Information", winW, winH)) {
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float childH  = diagH - titleH - footerH
                    - ImGui::GetStyle().WindowPadding.y * 2.0f;

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    s32 total = pdguiTrHangarNumTotal();
    s32 numLoc = pdguiTrHangarNumLocations();
    if (total <= 0) {
        ImGui::TextDisabled("No hangar entries unlocked yet.");
        ImGui::End();
        return 1;
    }

    if (s_HangarCursor < 0 || s_HangarCursor >= total) {
        s_HangarCursor = pdguiTrHangarGetSlot();
        if (s_HangarCursor < 0 || s_HangarCursor >= total) s_HangarCursor = 0;
    }
    listHandleKeyboardNav(&s_HangarCursor, total);

    ImGui::BeginChild("##hgr_body",
                      ImVec2(diagW - ImGui::GetStyle().WindowPadding.x * 2.0f,
                             childH), false);

    if (numLoc > 0) {
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Locations");
        ImGui::Separator();
        for (s32 i = 0; i < numLoc; i++) {
            const char *name = pdguiTrHangarSlotName(i);
            char buf[96];
            snprintf(buf, sizeof(buf), "%s", name ? name : "???");
            /* Names are pipe-separated "Name|Subheading\n".  Truncate at '|'
             * for the list view so we only show the top title. */
            for (char *p = buf; *p; p++) if (*p == '|') { *p = '\0'; break; }
            stripNewline(buf);
            ImGui::PushID(i);
            bool sel = (i == s_HangarCursor);
            if (ImGui::Selectable(buf, sel, 0,
                                  ImVec2(0, pdguiScale(22.0f)))) {
                s_HangarCursor = i;
                pdguiTrHangarSetSlot(i);
                menuPushDialog(&g_HangarLocationDetailsMenuDialog);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            if (ImGui::IsItemHovered()) s_HangarCursor = i;
            ImGui::PopID();
        }
        ImGui::Spacing();
    }

    if (total - numLoc > 0) {
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Vehicles");
        ImGui::Separator();
        for (s32 i = numLoc; i < total; i++) {
            const char *name = pdguiTrHangarSlotName(i);
            char buf[96];
            snprintf(buf, sizeof(buf), "%s", name ? name : "???");
            for (char *p = buf; *p; p++) if (*p == '|') { *p = '\0'; break; }
            stripNewline(buf);
            ImGui::PushID(i);
            bool sel = (i == s_HangarCursor);
            if (ImGui::Selectable(buf, sel, 0,
                                  ImVec2(0, pdguiScale(22.0f)))) {
                s_HangarCursor = i;
                pdguiTrHangarSetSlot(i);
                menuPushDialog(&g_HangarVehicleDetailsMenuDialog);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            if (ImGui::IsItemHovered()) s_HangarCursor = i;
            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
        if (PdButton("Back", ImVec2(btnW, btnH))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Shared Hangar details renderer (location and vehicle).  Shows the full
 * name, subheading, and scrollable description, with a "Holograph" push
 * action for vehicles (not locations).
 * ========================================================================= */

static s32 renderHangarDetailsImpl(const char *imguiId, bool isVehicle,
                                    s32 winW, s32 winH)
{
    const char *fullname = pdguiTrHangarCurrentFullName();
    char titleBuf[96];
    snprintf(titleBuf, sizeof(titleBuf), "%s", fullname ? fullname : "Hangar");
    for (char *p = titleBuf; *p; p++) if (*p == '|') { *p = '\0'; break; }
    stripNewline(titleBuf);

    if (!beginTrainingWindow(imguiId, titleBuf, winW, winH)) {
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    /* Subheading row (extracted from the name string: "Name|Subheading\n"). */
    const char *sub = pdguiTrHangarCurrentSubheading();
    if (sub && sub[0]) {
        char subBuf[128];
        snprintf(subBuf, sizeof(subBuf), "%s", sub);
        stripNewline(subBuf);
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 0.9f), "%s", subBuf);
        ImGui::Spacing();
    }

    /* Description body */
    const char *desc = pdguiTrHangarCurrentDescription();
    float childH = diagH - titleH - footerH
                   - ImGui::GetStyle().WindowPadding.y * 2.0f
                   - pdguiScale(30.0f);

    ImGui::BeginChild("##hgr_det_body",
                      ImVec2(diagW - ImGui::GetStyle().WindowPadding.x * 2.0f,
                             childH), true);
    if (desc && desc[0]) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    /* Footer: Holograph (vehicles only) + Back */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);

        if (isVehicle) {
            float totalW = btnW * 2.0f + pdguiScale(12.0f);
            ImGui::SetCursorPosX((diagW - totalW) * 0.5f);
            if (PdButton("Holograph", ImVec2(btnW, btnH))) {
                menuPushDialog(&g_HangarVehicleHolographMenuDialog);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            ImGui::SameLine(0, pdguiScale(12.0f));
            if (PdButton("Back", ImVec2(btnW, btnH))) {
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                menuPopDialog();
            }
        } else {
            ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
            if (PdButton("Back", ImVec2(btnW, btnH))) {
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                menuPopDialog();
            }
        }
    }

    ImGui::End();
    return 1;
}

static s32 renderHangarVehicleDetails(struct menudialog *dialog,
                                        struct menu *menu,
                                        s32 winW, s32 winH)
{
    return renderHangarDetailsImpl("##hgr_veh_det", true, winW, winH);
}

static s32 renderHangarLocationDetails(struct menudialog *dialog,
                                         struct menu *menu,
                                         s32 winW, s32 winH)
{
    return renderHangarDetailsImpl("##hgr_loc_det", false, winW, winH);
}

/* =========================================================================
 * Hangar Vehicle Holograph (g_HangarVehicleHolographMenuDialog) -- 3D
 * vehicle hologram.  Full-size preview in the centre of the dialog.
 * ========================================================================= */

static s32 renderHangarVehicleHolograph(struct menudialog *dialog,
                                          struct menu *menu,
                                          s32 winW, s32 winH)
{
    const char *fullname = pdguiTrHangarCurrentFullName();
    char titleBuf[96] = "Holograph";
    if (fullname && fullname[0]) {
        snprintf(titleBuf, sizeof(titleBuf), "%s", fullname);
        for (char *p = titleBuf; *p; p++) if (*p == '|') { *p = '\0'; break; }
        stripNewline(titleBuf);
    }

    if (!beginTrainingWindow("##hgr_holo", titleBuf, winW, winH)) {
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float padY    = ImGui::GetStyle().WindowPadding.y;

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    ImVec2 winMin = ImGui::GetWindowPos();
    float previewW = pdguiScale(520.0f);
    float previewH = pdguiScale(340.0f);
    if (previewW > diagW - pdguiScale(40.0f)) {
        previewW = diagW - pdguiScale(40.0f);
    }
    float previewX = winMin.x + (diagW - previewW) * 0.5f;
    float previewY = winMin.y + titleH + padY + pdguiScale(20.0f);

    u32 filenum = pdguiTrHangarCurrentVehicleFilenum();
    drawFilenumPreview(PDGUI_PREVIEW_VEHICLE, filenum,
                        nullptr, previewX, previewY, previewW, previewH);

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
        if (PdButton("Back", ImVec2(btnW, btnH))) {
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

    /* FR Weapon List (Batch 10 — ImGui weapon list with score tier stars) */
    pdguiHotswapRegister(&g_FrWeaponListMenuDialog,
                         renderFrWeaponList, "FR Weapon List");

    /* ---- Biographies ---- */
    /* Bio List (Batch 10 — grouped ImGui list: chrbio + miscbio via bridge) */
    pdguiHotswapRegister(&g_BioListMenuDialog,
                         renderBioList, "Bio List");
    /* Bio Profile (Batch 10 — 3D char preview + name/age/race/description) */
    pdguiHotswapRegister(&g_BioProfileMenuDialog,
                         renderBioProfile, "Bio Profile");
    pdguiHotswapRegister(&g_BioTextMenuDialog,
                         renderBioText, "Bio Text");

    /* ---- Device Training ---- */
    /* DT List (Batch 10 — ImGui device list) */
    pdguiHotswapRegister(&g_DtListMenuDialog,
                         renderDtList, "DT List");
    /* DT Details (Batch 10 — 3D weapon preview via pdguiModelPreview WEAPON) */
    pdguiHotswapRegister(&g_DtDetailsMenuDialog,
                         renderDtDetails, "DT Details");
    pdguiHotswapRegister(&g_DtFailedMenuDialog,
                         renderDtFailed, "DT Failed");
    pdguiHotswapRegister(&g_DtCompletedMenuDialog,
                         renderDtCompleted, "DT Completed");

    /* ---- Holo Training ---- */
    pdguiHotswapRegister(&g_HtListMenuDialog,
                         renderHtList, "HT List");
    /* HT Details (Batch 10 — 3D weapon preview via pdguiModelPreview WEAPON) */
    pdguiHotswapRegister(&g_HtDetailsMenuDialog,
                         renderHtDetails, "HT Details");
    pdguiHotswapRegister(&g_HtFailedMenuDialog,
                         renderHtFailed, "HT Failed");
    pdguiHotswapRegister(&g_HtCompletedMenuDialog,
                         renderHtCompleted, "HT Completed");

    /* Misc */
    pdguiHotswapRegister(&g_NowSafeMenuDialog,
                         renderNowSafe, "Now Safe");

    /* ---- Hangar (Batch 10) ---- */
    pdguiHotswapRegister(&g_HangarListMenuDialog,
                         renderHangarList, "Hangar List");
    pdguiHotswapRegister(&g_HangarVehicleHolographMenuDialog,
                         renderHangarVehicleHolograph, "Hangar Holograph");
    pdguiHotswapRegister(&g_HangarVehicleDetailsMenuDialog,
                         renderHangarVehicleDetails, "Hangar Vehicle Details");
    pdguiHotswapRegister(&g_HangarLocationDetailsMenuDialog,
                         renderHangarLocationDetails, "Hangar Location Details");

    sysLogPrintf(LOG_NOTE, "pdgui_menu_training: registered (22 dialogs, Batch 10 complete)");
}

} /* extern "C" */
