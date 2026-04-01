/**
 * pdgui_menu_mpsettings.cpp -- MP per-player handicap screen.
 *
 * Replaces g_MpHandicapsMenuDialog with an ImGui screen that shows
 * per-player handicap sliders.
 *
 * Handicap is stored as a u8 in g_PlayerConfigsArray[n].handicap.
 * 0x80 = 100% (no modifier). mpHandicapToDamageScale(h) returns
 * the multiplier (e.g., 0x40 → 0.5× = 50% damage received).
 *
 * matchGetPlayerHandicap / matchSetPlayerHandicap / matchResetHandicaps
 * are wrappers in matchsetup.c that avoid exposing types.h here.
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

/* Dialog we replace */
extern struct menudialogdef g_MpHandicapsMenuDialog;

/* Menu stack */
void menuPopDialog(void);

/* Handicap wrappers (matchsetup.c — avoid types.h in C++) */
u8   matchGetPlayerHandicap(s32 playernum);
void matchSetPlayerHandicap(s32 playernum, u8 val);
void matchResetHandicaps(void);

/* Damage scale display (mplayer.c) */
f32  mpHandicapToDamageScale(u8 value);

/* Player names (for labels) */
const char *mpPlayerConfigGetName(s32 playernum);

/* Match config read (how many human players are active) */
#define MAX_PLAYER_NAME 32
#define MATCH_MAX_SLOTS 32
#define SLOT_EMPTY  0
#define SLOT_PLAYER 1
#define SLOT_BOT    2

struct matchslot {
    u8 type;
    u8 team;
    u8 headnum;
    u8 bodynum;
    u8 botType;
    u8 botDifficulty;
    char name[MAX_PLAYER_NAME];
};

struct matchconfig {
    struct matchslot slots[MATCH_MAX_SLOTS];
    u8 scenario;
    u8 stagenum;
    u8 timelimit;
    u8 scorelimit;
    u16 teamscorelimit;
    u32 options;
    u8 weapons[6];
    s8 weaponSetIndex;
    u8 numSlots;
    u8 spawnWeaponNum;
};

extern struct matchconfig g_MatchConfig;

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
 * Helpers
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
 * Main render function
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
    float pdTitleH = pdguiScale(26.0f);

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

    /* Backdrop */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + diagW, pos.y + diagH),
                          IM_COL32(8, 8, 16, 255));
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
                    IM_COL32(255, 255, 255, 255), title);
    }

    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    float footerH  = pdguiScale(50.0f);
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

            /* Read current handicap */
            u8 h = matchGetPlayerHandicap(playerSlot);
            int hInt = (int)h;

            /* Slider: 0–255. Display as damage percent. */
            float pct = mpHandicapToDamageScale(h) * 100.0f;
            char fmtBuf[32];
            snprintf(fmtBuf, sizeof(fmtBuf), "%.0f%%", pct);

            ImGui::SetNextItemWidth(sliderW);
            if (ImGui::SliderInt("##h", &hInt, 0, 255, fmtBuf)) {
                matchSetPlayerHandicap(playerSlot, (u8)hInt);
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 0.8f),
                               "%.0f%% dmg received", pct);

            ImGui::Spacing();
            ImGui::PopID();
            playerSlot++;
        }
    }

    ImGui::EndChild();

    /* ---- Footer ---- */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(8.0f));
    ImGui::Separator();
    ImGui::Spacing();

    float btnW = pdguiScale(130.0f);
    float btnH = pdguiScale(28.0f);
    float totalW = btnW * 2.0f + pdguiScale(8.0f);
    ImGui::SetCursorPosX((diagW - totalW) * 0.5f);

    if (PdButton("Restore Defaults", ImVec2(btnW, btnH))) {
        matchResetHandicaps();
        pdguiPlaySound(PDGUI_SND_SELECT);
    }

    ImGui::SameLine(0, pdguiScale(8.0f));

    if (PdButton("Done", ImVec2(btnW, btnH))
        || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
    }

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
        pdguiHotswapRegister(&g_MpHandicapsMenuDialog, renderHandicap, "Player Handicaps");
        s_Registered = true;
    }
    sysLogPrintf(LOG_NOTE, "pdgui_menu_mpsettings: registered");
}

} /* extern "C" */
