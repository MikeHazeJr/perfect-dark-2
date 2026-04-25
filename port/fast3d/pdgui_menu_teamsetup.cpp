/**
 * pdgui_menu_teamsetup.cpp -- Team assignment and auto-team presets screen.
 *
 * Replaces g_MpTeamsMenuDialog and g_MpAutoTeamMenuDialog with an ImGui
 * implementation that allows per-slot team assignment and one-click auto-team
 * presets (Two Teams, Three Teams, Four Teams, Humans vs. Sims, Human-Sim Pairs).
 *
 * Team assignments are stored in g_MatchConfig.slots[i].team (0-7).
 * MPOPTION_TEAMSENABLED in g_MatchConfig.options controls whether teams are active.
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
#include "pdgui_widgets.h"      /* Priority L: shared label-left widget helpers */
#include "system.h"
#include "inputctx.h"
#include "menupool.h"

/* ========================================================================
 * Forward declarations (C boundary)
 * ======================================================================== */

extern "C" {

/* Dialogs we replace */
extern struct menudialogdef g_MpTeamsMenuDialog;
extern struct menudialogdef g_MpAutoTeamMenuDialog;

/* Menu stack */
void menuPopDialog(void);

/* Match config types, struct definitions, and g_MatchConfig */
#include "net/matchsetup.h"
#define MPOPTION_TEAMSENABLED 0x00000002

} /* extern "C" */

/* ========================================================================
 * Team colors — MUST match src/game/radar.c::g_TeamColours and every other
 * team palette in port/fast3d/ (Room, Pause, Endscreen, Killfeed, HUD).
 * Reordered 2026-04-19 for B-186 so Two Teams / Humans vs Sims reads
 * Red (0) vs Blue (1) consistently in menu and engine.
 * ======================================================================== */

static const ImVec4 s_TeamColors[] = {
    ImVec4(1.0f, 0.3f, 0.3f, 1.0f),  /* 0: Red    */
    ImVec4(0.3f, 0.5f, 1.0f, 1.0f),  /* 1: Blue   */
    ImVec4(0.3f, 1.0f, 0.3f, 1.0f),  /* 2: Green  */
    ImVec4(1.0f, 1.0f, 0.3f, 1.0f),  /* 3: Yellow */
    ImVec4(1.0f, 0.5f, 0.0f, 1.0f),  /* 4: Orange */
    ImVec4(0.8f, 0.3f, 1.0f, 1.0f),  /* 5: Purple */
    ImVec4(0.6f, 0.6f, 0.6f, 1.0f),  /* 6: Grey   */
    ImVec4(1.0f, 1.0f, 1.0f, 1.0f),  /* 7: White  */
};

static const char *s_TeamNames[] = {
    "Red", "Blue", "Green", "Yellow", "Orange", "Purple", "Grey", "White"
};

/* ========================================================================
 * Button wrapper with audio + edge glow
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
 * Auto-team preset implementations
 * ======================================================================== */

static void applyTwoTeams(void)
{
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        g_MatchConfig.slots[i].team = (u8)(i % 2);
    }
    pdguiPlaySound(PDGUI_SND_SELECT);
}

static void applyThreeTeams(void)
{
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        g_MatchConfig.slots[i].team = (u8)(i % 3);
    }
    pdguiPlaySound(PDGUI_SND_SELECT);
}

static void applyFourTeams(void)
{
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        g_MatchConfig.slots[i].team = (u8)(i % 4);
    }
    pdguiPlaySound(PDGUI_SND_SELECT);
}

/* All human players = team 0 (Red), all bots = team 1 (Blue) */
static void applyHumansVsSims(void)
{
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        g_MatchConfig.slots[i].team =
            (g_MatchConfig.slots[i].type == SLOT_PLAYER) ? 0 : 1;
    }
    pdguiPlaySound(PDGUI_SND_SELECT);
}

/* Pair each human with the next bot on the same team, cycling team index */
static void applyHumanSimPairs(void)
{
    int playerCount = 0;
    int botCount = 0;

    /* Count players and bots first to size teams correctly */
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type == SLOT_PLAYER) playerCount++;
        else if (g_MatchConfig.slots[i].type == SLOT_BOT)  botCount++;
    }

    int teamIdx = 0;
    int humanPaired[MATCH_MAX_SLOTS] = {0};

    /* Assign each player a team, then pair the nearest bot to the same team */
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type != SLOT_PLAYER) continue;
        g_MatchConfig.slots[i].team = (u8)teamIdx;
        humanPaired[i] = teamIdx;

        /* Find the first unpaired bot and assign to same team */
        for (int j = i + 1; j < (int)g_MatchConfig.numSlots; j++) {
            if (g_MatchConfig.slots[j].type == SLOT_BOT
                && g_MatchConfig.slots[j].team == 255)  /* 255 = unassigned marker */
            {
                g_MatchConfig.slots[j].team = (u8)teamIdx;
                break;
            }
        }
        teamIdx = (teamIdx + 1) % 8;
    }

    /* Any remaining bots without a partner join team 0 */
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        if (g_MatchConfig.slots[i].type == SLOT_BOT
            && g_MatchConfig.slots[i].team == 255)
        {
            g_MatchConfig.slots[i].team = 0;
        }
    }
    pdguiPlaySound(PDGUI_SND_SELECT);
}

/* Zero out team marker used by applyHumanSimPairs before call */
static void resetTeamMarkers(void)
{
    for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
        g_MatchConfig.slots[i].team = 255;  /* unassigned */
    }
}

/* ========================================================================
 * Main render function
 * ======================================================================== */

static bool s_Registered = false;

static s32 renderTeamSetup(struct menudialog *dialog,
                            struct menu *menu,
                            s32 winW, s32 winH)
{
    float scale  = pdguiScaleFactor();
    float diagW  = pdguiMenuWidth();
    float diagH  = pdguiMenuHeight();
    ImVec2 pos   = pdguiMenuPos();
    float pdTitleH = pdguiScale(39.0f);

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(diagW, diagH));

    if (!ImGui::Begin("##team_setup", nullptr, wflags)) {
        /* S300: pool owns ctx; release when ImGui culls the window. */
        menupoolReleaseDialog(menupoolDialogDef(dialog));
        ImGui::End();
        return 1;
    }

    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    /* C-3: focus on appear so controller can nav team slots. */
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    /* Backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + diagW, pos.y + diagH),
                          pdguiPalImU32(PDPAL_BODYBG, 255));
    }

    pdguiDrawPdDialog(pos.x, pos.y, diagW, diagH, "Team Control", 1);

    /* Title */
    {
        const char *title = "Team Control";
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
    float contentH = diagH - pdTitleH - footerH
                     - ImGui::GetStyle().WindowPadding.y * 2.0f;

    /* ---- Two-column layout: slots on left, presets on right ---- */
    float leftW  = diagW * 0.60f - pdguiScale(6.0f);
    float rightW = diagW * 0.40f - pdguiScale(6.0f);
    float pad     = pdguiScale(12.0f);

    /* ---- LEFT: Teams Enabled + per-slot assignment ---- */
    ImGui::BeginGroup();
    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##team_slots", ImVec2(leftW, contentH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);

    /* Teams enabled toggle */
    {
        bool teamsOn = (g_MatchConfig.options & MPOPTION_TEAMSENABLED) != 0;
        /* Priority L (2026-04-25): label LEFT via pdguiCheckbox. */
        if (pdguiCheckbox("Teams Enabled", &teamsOn)) {
            if (teamsOn) g_MatchConfig.options |= MPOPTION_TEAMSENABLED;
            else         g_MatchConfig.options &= ~MPOPTION_TEAMSENABLED;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool teamsActive = (g_MatchConfig.options & MPOPTION_TEAMSENABLED) != 0;

    if (!teamsActive) {
        ImGui::TextDisabled("Enable teams above to assign players to teams.");
    } else {
        ImGui::TextColored(pdguiVec4TitleGlow(), "Slot Assignments");
        ImGui::Spacing();

        float comboW = leftW * 0.45f;

        for (int i = 0; i < (int)g_MatchConfig.numSlots; i++) {
            struct matchslot *slot = &g_MatchConfig.slots[i];
            ImGui::PushID(i);

            /* Type tag */
            const char *typeTag = (slot->type == SLOT_PLAYER) ? "[P]" : "[B]";
            ImVec4 typeCol = (slot->type == SLOT_PLAYER)
                ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                : ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
            ImGui::TextColored(typeCol, "%s", typeTag);
            ImGui::SameLine();

            /* Player/bot name */
            ImGui::Text("%-14s", slot->name[0] ? slot->name : "---");
            ImGui::SameLine();

            /* Team color dot */
            int team = (slot->team < 8) ? slot->team : 0;
            ImGui::TextColored(s_TeamColors[team], "\xe2\x97\x8f");
            ImGui::SameLine();

            /* Team combo */
            char comboLabel[16];
            snprintf(comboLabel, sizeof(comboLabel), "##team%d", i);
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo(comboLabel, s_TeamNames[team])) {
                for (int t = 0; t < 8; t++) {
                    bool isSel = (t == team);
                    ImGui::TextColored(s_TeamColors[t], "\xe2\x97\x8f");
                    ImGui::SameLine();
                    char teamLabel[32];
                    snprintf(teamLabel, sizeof(teamLabel), "%s##t%d_%d",
                             s_TeamNames[t], i, t);
                    if (ImGui::Selectable(teamLabel, isSel)) {
                        slot->team = (u8)t;
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::PopID();
        }

        if (g_MatchConfig.numSlots == 0) {
            ImGui::TextDisabled("No slots configured.");
        }
    }

    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::SameLine(0, pad);

    /* ---- RIGHT: Auto-team presets ---- */
    ImGui::BeginGroup();
    /* Priority L (2026-04-25): NavFlattened so D-pad traverses across columns. */
    ImGui::BeginChild("##team_presets", ImVec2(rightW, contentH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);

    ImGui::TextColored(pdguiVec4TitleGlow(), "Auto Team Presets");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("One click assigns all slots.");
    ImGui::Spacing();

    float btnW = rightW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    float btnH = pdguiScale(45.0f);

    bool canAuto = teamsActive;
    if (!canAuto) ImGui::BeginDisabled();

    if (PdButton("Two Teams", ImVec2(btnW, btnH))) {
        applyTwoTeams();
    }
    ImGui::Spacing();

    if (PdButton("Three Teams", ImVec2(btnW, btnH))) {
        applyThreeTeams();
    }
    ImGui::Spacing();

    if (PdButton("Four Teams", ImVec2(btnW, btnH))) {
        applyFourTeams();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (PdButton("Humans vs. Simulants", ImVec2(btnW, btnH))) {
        applyHumansVsSims();
    }
    ImGui::Spacing();

    if (PdButton("Human-Simulant Pairs", ImVec2(btnW, btnH))) {
        resetTeamMarkers();
        applyHumanSimPairs();
    }

    if (!canAuto) ImGui::EndDisabled();
    if (!canAuto) {
        ImGui::Spacing();
        ImGui::TextDisabled("Enable teams to use presets.");
    }

    ImGui::EndChild();
    ImGui::EndGroup();

    /* ---- Footer ---- */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();

    float doneW = pdguiScale(210.0f);
    float doneH = pdguiScale(42.0f);
    ImGui::SetCursorPosX((diagW - doneW) * 0.5f);

    if (PdButton("Done", ImVec2(doneW, doneH))
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

/* g_MpAutoTeamMenuDialog: inline version — shows same screen, auto team section
 * expanded to full width for the narrow 4MB path that pushed this directly. */
static s32 renderAutoTeam(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
    /* Reuse the full team screen — auto-team presets are in the right panel */
    return renderTeamSetup(dialog, menu, winW, winH);
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMenuTeamSetupRegister(void)
{
    if (!s_Registered) {
        pdguiHotswapRegister(&g_MpTeamsMenuDialog,   renderTeamSetup, "Team Control");
        pdguiHotswapRegister(&g_MpAutoTeamMenuDialog, renderAutoTeam,  "Auto Team");
        s_Registered = true;
    }
    sysLogPrintf(LOG_NOTE, "pdgui_menu_teamsetup: registered");
}

} /* extern "C" */
