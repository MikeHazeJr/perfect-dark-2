/**
 * pdgui_menu_challenges.cpp -- Combat challenge browser and launcher.
 *
 * Replaces g_MpChallengeListOrDetailsMenuDialog and
 * g_MpCompletedChallengesMenuDialog with an ImGui two-panel screen:
 *
 *   Left:  scrollable challenge list — name + 4-player completion dots
 *   Right: selected challenge description + "Accept Challenge" button
 *
 * Accepting calls matchStartFromChallenge(slot) which wraps
 * challengeSetCurrentBySlot() + mpStartMatch() without clobbering the
 * challenge's g_MpSetup config through the normal g_MatchConfig path.
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
#include "system.h"

/* ========================================================================
 * Forward declarations (C boundary)
 * ======================================================================== */

extern "C" {

/* Dialogs we replace */
extern struct menudialogdef g_MpChallengeListOrDetailsMenuDialog;
extern struct menudialogdef g_MpCompletedChallengesMenuDialog;

/* Menu stack */
void menuPopDialog(void);

/* Challenge API (challenge.c) */
s32   challengeGetNumAvailable(void);
char *challengeGetNameBySlot(s32 slot);
char *challengeGetCurrentDescription(void);
s32   challengeGetAutoFocusedIndex(s32 mpchrnum);
s32   challengeGetCurrent(void);

/* Completion check: has playernum completed slot with numplayers players? */
/* Note: challengeIsCompletedByChrWithNumPlayersBySlot is the by-slot variant */
/* Returns bool (s32) */
s32   challengeIsCompletedByChrWithNumPlayersBySlot(s32 mpchrnum, s32 slot, s32 numplayers);

/* Challenge start (matchsetup.c) */
s32   matchStartFromChallenge(s32 slot);

/* Player num for completion checks (g_MpPlayerNum is legacy global) */
extern s32 g_MpPlayerNum;

} /* extern "C" */

/* ========================================================================
 * Button wrapper
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
 * Per-session state
 * ======================================================================== */

static int  s_SelectedSlot = 0;     /* currently highlighted challenge slot */
static bool s_NeedsInit    = true;  /* re-focus auto-selection on open */
static bool s_Registered   = false;

/* ========================================================================
 * Main render function
 * ======================================================================== */

static s32 renderChallenges(struct menudialog *dialog,
                             struct menu *menu,
                             s32 winW, s32 winH)
{
    int numChallenges = challengeGetNumAvailable();

    /* Auto-select first challenge on open */
    if (s_NeedsInit) {
        s_SelectedSlot = challengeGetAutoFocusedIndex(g_MpPlayerNum);
        if (s_SelectedSlot < 0 || s_SelectedSlot >= numChallenges) {
            s_SelectedSlot = 0;
        }
        s_NeedsInit = false;
    }

    float scale    = pdguiScaleFactor();
    float diagW    = pdguiMenuWidth();
    float diagH    = pdguiMenuHeight();
    ImVec2 pos     = pdguiMenuPos();
    float pdTitleH = pdguiScale(26.0f);

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(diagW, diagH));

    if (!ImGui::Begin("##challenges", nullptr, wflags)) {
        ImGui::End();
        return 1;
    }

    /* Backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + diagW, pos.y + diagH),
                          IM_COL32(8, 8, 16, 255));
    }

    pdguiDrawPdDialog(pos.x, pos.y, diagW, diagH, "Combat Challenges", 1);

    /* Title */
    {
        const char *title = "Combat Challenges";
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(pos.x + 8.0f, pos.y + 2.0f,
                          diagW - 16.0f, pdTitleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(pos.x + (diagW - ts.x) * 0.5f,
                           pos.y + (pdTitleH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), title);
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    float footerH  = pdguiScale(50.0f);
    float contentH = diagH - pdTitleH - footerH - ImGui::GetStyle().WindowPadding.y;
    float pad      = pdguiScale(8.0f);
    float leftW    = diagW * 0.45f;
    float rightW   = diagW - leftW - pad * 3.0f;

    /* ---- LEFT: Challenge list ---- */
    ImGui::BeginGroup();
    ImGui::BeginChild("##chal_list", ImVec2(leftW, contentH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Challenges");
    ImGui::Separator();
    ImGui::Spacing();

    /* Completion column header */
    ImGui::TextDisabled("  Name                        1P 2P 3P 4P");
    ImGui::Separator();

    if (numChallenges == 0) {
        ImGui::TextDisabled("No challenges available.");
    }

    for (int i = 0; i < numChallenges; i++) {
        ImGui::PushID(i);

        bool isSelected = (i == s_SelectedSlot);
        char *cname = challengeGetNameBySlot(i);

        /* Row label */
        char rowLabel[80];
        snprintf(rowLabel, sizeof(rowLabel), "%-28s##chal%d",
                 cname ? cname : "---", i);

        if (ImGui::Selectable(rowLabel, isSelected,
                              ImGuiSelectableFlags_None,
                              ImVec2(leftW * 0.62f, 0))) {
            s_SelectedSlot = i;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }

        /* Completion dots (1–4 players) inline on same row */
        ImGui::SameLine();
        for (int np = 1; np <= 4; np++) {
            bool done = challengeIsCompletedByChrWithNumPlayersBySlot(
                g_MpPlayerNum, i, np) != 0;
            ImVec4 dotCol = done
                ? ImVec4(0.1f, 0.9f, 0.1f, 1.0f)   /* green = completed */
                : ImVec4(0.2f, 0.2f, 0.25f, 1.0f);  /* dark = not done */
            ImGui::TextColored(dotCol, "\xe2\x97\x8f");
            if (np < 4) ImGui::SameLine(0, pdguiScale(2.0f));
        }

        /* Keyboard / gamepad navigation: up/down arrows */
        if (isSelected && ImGui::IsWindowFocused()) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)
                || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true))
            {
                if (s_SelectedSlot < numChallenges - 1) {
                    s_SelectedSlot++;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)
                || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true))
            {
                if (s_SelectedSlot > 0) {
                    s_SelectedSlot--;
                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                }
            }
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::SameLine(0, pad);

    /* ---- RIGHT: Description + Accept ---- */
    ImGui::BeginGroup();
    ImGui::BeginChild("##chal_detail", ImVec2(rightW, contentH), true);

    /* Challenge name header */
    if (numChallenges > 0 && s_SelectedSlot < numChallenges) {
        char *selName = challengeGetNameBySlot(s_SelectedSlot);
        if (selName && selName[0]) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", selName);
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    /* Completion summary for selected challenge */
    if (numChallenges > 0 && s_SelectedSlot < numChallenges) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 0.85f), "Completion:");
        for (int np = 1; np <= 4; np++) {
            bool done = challengeIsCompletedByChrWithNumPlayersBySlot(
                g_MpPlayerNum, s_SelectedSlot, np) != 0;
            ImVec4 dotCol = done
                ? ImVec4(0.1f, 0.9f, 0.1f, 1.0f)
                : ImVec4(0.35f, 0.35f, 0.4f, 0.9f);
            ImGui::SameLine();
            char numLabel[8];
            snprintf(numLabel, sizeof(numLabel), "%dP", np);
            ImGui::TextColored(dotCol, "%s", numLabel);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Description — note: only available after a challenge has been loaded
     * (challengeGetCurrentDescription returns "" before challengeSetCurrentBySlot).
     * We show a placeholder and the real description after accept is pressed. */
    ImGui::TextWrapped("Select a challenge and press Accept to view the configuration "
                       "and start the match.\n\nChallenge settings (stage, weapons, "
                       "simulants) are pre-configured. Winning unlocks rewards.");

    ImGui::Spacing();
    ImGui::Spacing();

    /* ---- Accept button ---- */
    float acceptW = rightW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    float acceptH = pdguiScale(36.0f);

    bool canAccept = (numChallenges > 0 && s_SelectedSlot >= 0
                      && s_SelectedSlot < numChallenges);
    if (!canAccept) ImGui::BeginDisabled();

    if (PdButton("Accept Challenge", ImVec2(acceptW, acceptH))
        || (canAccept && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)))
    {
        sysLogPrintf(LOG_NOTE, "CHALLENGES: accepting slot %d", s_SelectedSlot);
        s_NeedsInit = true;  /* reset for next time */
        matchStartFromChallenge(s_SelectedSlot);
    }

    if (!canAccept) ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::EndGroup();

    /* ---- Footer ---- */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    float backW = pdguiScale(140.0f);
    float backH = pdguiScale(28.0f);
    ImGui::SetCursorPosX((diagW - backW) * 0.5f);

    if (PdButton("Back", ImVec2(backW, backH))
        || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        s_NeedsInit = true;
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

/* Completed-challenges view: same layout, all rows highlighted for browsing */
static s32 renderCompletedChallenges(struct menudialog *dialog,
                                      struct menu *menu,
                                      s32 winW, s32 winH)
{
    return renderChallenges(dialog, menu, winW, winH);
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMenuChallengesRegister(void)
{
    if (!s_Registered) {
        pdguiHotswapRegister(&g_MpChallengeListOrDetailsMenuDialog,
                             renderChallenges, "Combat Challenges");
        pdguiHotswapRegister(&g_MpCompletedChallengesMenuDialog,
                             renderCompletedChallenges, "Completed Challenges");
        s_Registered = true;
    }
    sysLogPrintf(LOG_NOTE, "pdgui_menu_challenges: registered");
}

} /* extern "C" */
