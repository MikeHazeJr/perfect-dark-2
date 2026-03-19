/**
 * pdgui_menubuilder.cpp -- ImGui component library & menu builder registry.
 *
 * This file provides:
 *   1. PD-authentic UI components (PdDialog, PdMenuItem, PdSlider, etc.)
 *      that render using the active palette from pdgui_style.cpp.
 *
 *   2. A registry of per-menu "builder" functions.  The storyboard calls
 *      pdguiMenuBuilderRender(catalogIndex) and if a builder is registered
 *      for that index, it draws the NEW ImGui version of that menu.
 *
 *   3. Mock data for populating menu previews with realistic-looking content.
 *
 * IMPORTANT: C++ file -- must NOT include types.h (bool→s32 conflict).
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 * Part of Phase D4 -- see context/menu-storyboard.md (ADR-001).
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include "pdgui_menubuilder.h"
#include "pdgui_style.h"
#include "system.h"

/* ========================================================================
 * Mock Data
 * ======================================================================== */

struct MockPlayer {
    char name[32];
    s32  kills;
    s32  deaths;
    s32  accuracy;
};

struct MockWeapon {
    char name[32];
    s32  ammo;
};

#define OBJSTATE_COMPLETE   1
#define OBJSTATE_FAILED     2
#define OBJSTATE_INCOMPLETE 3

struct MockObjective {
    char text[64];
    s32  state;  /* OBJSTATE_* */
};

struct StoryboardMockData {
    MockPlayer   players[8];
    s32          numPlayers;

    MockWeapon   weapons[8];
    s32          numWeapons;

    MockObjective objectives[6];
    s32           numObjectives;

    char stageTitle[64];
    char scenarioName[64];
    char challengeName[64];
};

static StoryboardMockData g_Mock;

extern "C" {

void pdguiMockDataInit(void)
{
    memset(&g_Mock, 0, sizeof(g_Mock));

    /* Players */
    g_Mock.numPlayers = 4;
    snprintf(g_Mock.players[0].name, 32, "DarkStar");
    g_Mock.players[0].kills = 47; g_Mock.players[0].deaths = 12; g_Mock.players[0].accuracy = 68;
    snprintf(g_Mock.players[1].name, 32, "Jo Dark");
    g_Mock.players[1].kills = 38; g_Mock.players[1].deaths = 18; g_Mock.players[1].accuracy = 55;
    snprintf(g_Mock.players[2].name, 32, "SpySim#2");
    g_Mock.players[2].kills = 22; g_Mock.players[2].deaths = 31; g_Mock.players[2].accuracy = 42;
    snprintf(g_Mock.players[3].name, 32, "TrentEaston");
    g_Mock.players[3].kills = 15; g_Mock.players[3].deaths = 29; g_Mock.players[3].accuracy = 39;

    /* Weapons */
    g_Mock.numWeapons = 5;
    snprintf(g_Mock.weapons[0].name, 32, "Falcon 2");        g_Mock.weapons[0].ammo = 80;
    snprintf(g_Mock.weapons[1].name, 32, "CMP150");           g_Mock.weapons[1].ammo = 200;
    snprintf(g_Mock.weapons[2].name, 32, "Dragon");            g_Mock.weapons[2].ammo = 150;
    snprintf(g_Mock.weapons[3].name, 32, "Laptop Gun");        g_Mock.weapons[3].ammo = 100;
    snprintf(g_Mock.weapons[4].name, 32, "Slayer");            g_Mock.weapons[4].ammo = 20;

    /* Objectives */
    g_Mock.numObjectives = 5;
    snprintf(g_Mock.objectives[0].text, 64, "Gain entrance to the building");
    g_Mock.objectives[0].state = OBJSTATE_COMPLETE;
    snprintf(g_Mock.objectives[1].text, 64, "Destroy the dataDyne server");
    g_Mock.objectives[1].state = OBJSTATE_COMPLETE;
    snprintf(g_Mock.objectives[2].text, 64, "Obtain the key code necklace");
    g_Mock.objectives[2].state = OBJSTATE_COMPLETE;
    snprintf(g_Mock.objectives[3].text, 64, "Escape the building");
    g_Mock.objectives[3].state = OBJSTATE_FAILED;
    snprintf(g_Mock.objectives[4].text, 64, "Collect the evidence");
    g_Mock.objectives[4].state = OBJSTATE_INCOMPLETE;

    /* Strings */
    snprintf(g_Mock.stageTitle, 64, "dataDyne Central - Defection");
    snprintf(g_Mock.scenarioName, 64, "Combat Simulator");
    snprintf(g_Mock.challengeName, 64, "Challenge 1 - The Skedar");

    sysLogPrintf(LOG_NOTE, "menubuilder: Mock data initialized");
}

/* ========================================================================
 * PD-Authentic UI Components
 *
 * These draw using ImGui's draw list API, styled by the active palette.
 * They are building blocks for the per-menu builder functions.
 * ======================================================================== */

/* Colors pulled from the current palette via pdguiGetPalette().
 * For now, we re-derive them here using the same PdColor conversion.
 * In a future refactor, pdgui_style could export the computed ImU32 values. */

/* PD 0xRRGGBBAA -> ImU32 (ABGR) -- duplicated from pdgui_style.cpp for now */
static inline ImU32 PdCol(unsigned int rgba)
{
    unsigned char r = (rgba >> 24) & 0xFF;
    unsigned char g = (rgba >> 16) & 0xFF;
    unsigned char b = (rgba >>  8) & 0xFF;
    unsigned char a = (rgba >>  0) & 0xFF;
    return IM_COL32(r, g, b, a);
}

/* ---- PdDialog: renders a complete dialog frame ---- */

static void PdDialogBegin(ImDrawList *dl, float x, float y, float w, float h,
                            const char *title, ImU32 borderCol, ImU32 titleBgCol,
                            ImU32 titleFgCol, ImU32 bodyBgCol, float titleH)
{
    ImVec2 p1(x, y);
    ImVec2 p2(x + w, y + h);
    ImVec2 titleP2(x + w, y + titleH);

    /* Body fill */
    dl->AddRectFilled(p1, p2, bodyBgCol);

    /* Title bar gradient (simplified -- two-tone) */
    dl->AddRectFilledMultiColor(
        p1, titleP2,
        titleBgCol, titleBgCol,
        (titleBgCol & 0x00FFFFFF) | 0x40000000,
        (titleBgCol & 0x00FFFFFF) | 0x40000000);

    /* Title text */
    dl->AddText(ImVec2(x + 8.0f, y + (titleH - ImGui::GetTextLineHeight()) * 0.5f),
                titleFgCol, title);

    /* Borders */
    dl->AddRect(p1, p2, borderCol, 0.0f, 0, 1.5f);
    dl->AddLine(ImVec2(x, y + titleH), ImVec2(x + w, y + titleH), borderCol, 1.0f);
}

/* ---- PdMenuItem: selectable row with label + optional value ---- */

static void PdMenuItemRow(ImDrawList *dl, float x, float y, float w, float h,
                           const char *label, const char *value,
                           bool focused, bool disabled,
                           ImU32 normalCol, ImU32 focusCol, ImU32 focusBgCol,
                           ImU32 disabledCol)
{
    if (focused) {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), focusBgCol);
    }

    ImU32 textCol = disabled ? disabledCol : (focused ? focusCol : normalCol);
    float textY = y + (h - ImGui::GetTextLineHeight()) * 0.5f;

    dl->AddText(ImVec2(x + 8.0f, textY), textCol, label);

    if (value && value[0]) {
        ImVec2 valSize = ImGui::CalcTextSize(value);
        dl->AddText(ImVec2(x + w - valSize.x - 8.0f, textY), textCol, value);
    }
}

/* ---- PdSeparator: 1px line ---- */

static void PdSeparatorLine(ImDrawList *dl, float x, float y, float w, ImU32 col)
{
    dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), col, 1.0f);
}

/* ---- PdRankingRow: "1st  PlayerName  47" ---- */

static void PdRankingRow(ImDrawList *dl, float x, float y, float w, float h,
                          s32 rank, const char *name, s32 score,
                          bool isLocal, ImU32 normalCol, ImU32 highlightCol)
{
    static const char *rankLabels[] = { "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th" };
    const char *rankStr = (rank >= 0 && rank < 8) ? rankLabels[rank] : "?";

    ImU32 col = isLocal ? highlightCol : normalCol;
    float textY = y + (h - ImGui::GetTextLineHeight()) * 0.5f;

    dl->AddText(ImVec2(x + 8.0f, textY), col, rankStr);
    dl->AddText(ImVec2(x + 50.0f, textY), col, name);

    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "%d", score);
    ImVec2 scoreSize = ImGui::CalcTextSize(scoreStr);
    dl->AddText(ImVec2(x + w - scoreSize.x - 8.0f, textY), col, scoreStr);
}

/* ---- PdObjectiveRow: "✓ Objective text" / "✗ Objective text" ---- */

static void PdObjectiveRow(ImDrawList *dl, float x, float y, float w, float h,
                            const char *text, s32 state)
{
    float textY = y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    ImU32 col;
    const char *icon;

    switch (state) {
        case OBJSTATE_COMPLETE:
            col = IM_COL32(60, 220, 60, 255);
            icon = "OK";
            break;
        case OBJSTATE_FAILED:
            col = IM_COL32(220, 60, 60, 255);
            icon = "X ";
            break;
        default:
            col = IM_COL32(140, 140, 140, 255);
            icon = "? ";
            break;
    }

    dl->AddText(ImVec2(x + 8.0f, textY), col, icon);
    dl->AddText(ImVec2(x + 30.0f, textY), col, text);
}

/* ========================================================================
 * Per-Menu Builder Functions
 *
 * Each builder draws the NEW ImGui version of a specific menu dialog.
 * They receive the bounding box and draw into it using the components above.
 *
 * Builders are registered by catalog index in the s_Builders[] table below.
 * Unimplemented menus return 0 (the storyboard shows "not yet built").
 * ======================================================================== */

/* Type for builder functions */
typedef s32 (*MenuBuilderFn)(float x, float y, float w, float h);

/* --- Builder: Game Over (Individual) --- catalog index 0 */
static s32 buildGameOverIndividual(float x, float y, float w, float h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();

    float scale = h / 240.0f;
    float titleH = 22.0f * scale;
    float rowH = 18.0f * scale;

    /* Dialog colors -- DEFAULT type (blue) */
    ImU32 borderCol  = IM_COL32(0, 96, 191, 127);
    ImU32 titleBgCol = IM_COL32(0, 0, 80, 127);
    ImU32 titleFgCol = IM_COL32(255, 255, 255, 255);
    ImU32 bodyBgCol  = IM_COL32(0, 0, 47, 159);
    ImU32 normalCol  = IM_COL32(0, 255, 255, 255);
    ImU32 focusCol   = IM_COL32(255, 255, 255, 255);
    ImU32 focusBgCol = IM_COL32(0, 0, 68, 255);

    /* Center the dialog */
    float dlgW = 280.0f * scale;
    float dlgH = (titleH + rowH * (g_Mock.numPlayers + 3));
    float dlgX = x + (w - dlgW) * 0.5f;
    float dlgY = y + (h - dlgH) * 0.35f;

    PdDialogBegin(dl, dlgX, dlgY, dlgW, dlgH,
                   "Game Over", borderCol, titleBgCol, titleFgCol, bodyBgCol, titleH);

    /* Rankings */
    float curY = dlgY + titleH + 4.0f * scale;

    for (s32 i = 0; i < g_Mock.numPlayers; i++) {
        PdRankingRow(dl, dlgX, curY, dlgW, rowH,
                      i, g_Mock.players[i].name, g_Mock.players[i].kills,
                      (i == 0), normalCol, focusCol);
        curY += rowH;
    }

    /* Separator */
    curY += 4.0f * scale;
    PdSeparatorLine(dl, dlgX + 4.0f, curY, dlgW - 8.0f, borderCol);
    curY += 6.0f * scale;

    /* Action items */
    PdMenuItemRow(dl, dlgX, curY, dlgW, rowH,
                   "Exit to Menu", nullptr, true, false,
                   normalCol, focusCol, focusBgCol, normalCol);
    curY += rowH;

    PdMenuItemRow(dl, dlgX, curY, dlgW, rowH,
                   "Save and Exit", nullptr, false, false,
                   normalCol, focusCol, focusBgCol, normalCol);

    return 1;
}

/* --- Builder: Player Ranking --- catalog index 2 */
static s32 buildPlayerRanking(float x, float y, float w, float h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();

    float scale = h / 240.0f;
    float titleH = 22.0f * scale;
    float rowH = 18.0f * scale;

    ImU32 borderCol  = IM_COL32(0, 96, 191, 127);
    ImU32 titleBgCol = IM_COL32(0, 0, 80, 127);
    ImU32 titleFgCol = IM_COL32(255, 255, 255, 255);
    ImU32 bodyBgCol  = IM_COL32(0, 0, 47, 159);
    ImU32 normalCol  = IM_COL32(0, 255, 255, 255);
    ImU32 focusCol   = IM_COL32(255, 255, 255, 255);

    float dlgW = 300.0f * scale;
    float dlgH = titleH + rowH * (g_Mock.numPlayers + 2);
    float dlgX = x + (w - dlgW) * 0.5f;
    float dlgY = y + (h - dlgH) * 0.35f;

    PdDialogBegin(dl, dlgX, dlgY, dlgW, dlgH,
                   "Player Ranking", borderCol, titleBgCol, titleFgCol, bodyBgCol, titleH);

    /* Column headers */
    float curY = dlgY + titleH + 2.0f * scale;
    dl->AddText(ImVec2(dlgX + 8.0f, curY), IM_COL32(128, 128, 128, 200), "Rank");
    dl->AddText(ImVec2(dlgX + 50.0f, curY), IM_COL32(128, 128, 128, 200), "Player");

    char hdrKills[] = "Kills";
    ImVec2 hdrSize = ImGui::CalcTextSize(hdrKills);
    dl->AddText(ImVec2(dlgX + dlgW - hdrSize.x - 8.0f, curY),
                IM_COL32(128, 128, 128, 200), hdrKills);
    curY += rowH;

    PdSeparatorLine(dl, dlgX + 4.0f, curY, dlgW - 8.0f, borderCol);
    curY += 2.0f * scale;

    for (s32 i = 0; i < g_Mock.numPlayers; i++) {
        PdRankingRow(dl, dlgX, curY, dlgW, rowH,
                      i, g_Mock.players[i].name, g_Mock.players[i].kills,
                      (i == 0), normalCol, focusCol);
        curY += rowH;
    }

    return 1;
}

/* --- Builder: Objectives (Completed) --- catalog index 29 */
static s32 buildObjectivesCompleted(float x, float y, float w, float h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();

    float scale = h / 240.0f;
    float titleH = 22.0f * scale;
    float rowH = 18.0f * scale;

    /* SUCCESS type (green) */
    ImU32 borderCol  = IM_COL32(0, 191, 0, 127);
    ImU32 titleBgCol = IM_COL32(0, 80, 0, 127);
    ImU32 titleFgCol = IM_COL32(255, 255, 0, 255);
    ImU32 bodyBgCol  = IM_COL32(0, 47, 0, 159);

    float dlgW = 320.0f * scale;
    float dlgH = titleH + rowH * (g_Mock.numObjectives + 1);
    float dlgX = x + (w - dlgW) * 0.5f;
    float dlgY = y + (h - dlgH) * 0.35f;

    PdDialogBegin(dl, dlgX, dlgY, dlgW, dlgH,
                   "Objectives", borderCol, titleBgCol, titleFgCol, bodyBgCol, titleH);

    float curY = dlgY + titleH + 4.0f * scale;

    for (s32 i = 0; i < g_Mock.numObjectives; i++) {
        PdObjectiveRow(dl, dlgX, curY, dlgW, rowH,
                        g_Mock.objectives[i].text, g_Mock.objectives[i].state);
        curY += rowH;
    }

    return 1;
}

/* ========================================================================
 * Builder Registry
 *
 * Maps flat catalog index → builder function.
 * Entries are sparse (most are NULL = not yet implemented).
 * ======================================================================== */

/* Maximum catalog size we support */
#define MAX_CATALOG 256

static MenuBuilderFn s_Builders[MAX_CATALOG] = { nullptr };

static void registerBuilders(void)
{
    /* Phase D4d priority menus */
    s_Builders[0]  = buildGameOverIndividual;        /* Game Over (Individual) */
    s_Builders[2]  = buildPlayerRanking;             /* Player Ranking */
    s_Builders[29] = buildObjectivesCompleted;       /* Objectives (Completed) */

    /* More builders will be added as menus are implemented.
     * The storyboard rating system helps prioritize which to build next. */
}

static bool s_BuildersRegistered = false;

/* ========================================================================
 * Public API
 * ======================================================================== */

s32 pdguiMenuBuilderCount(void)
{
    s32 count = 0;
    for (s32 i = 0; i < MAX_CATALOG; i++) {
        if (s_Builders[i]) count++;
    }
    return count;
}

s32 pdguiMenuBuilderRender(s32 catalogIndex, float previewX, float previewY,
                            float previewW, float previewH)
{
    if (!s_BuildersRegistered) {
        registerBuilders();
        s_BuildersRegistered = true;
    }

    if (catalogIndex < 0 || catalogIndex >= MAX_CATALOG) return 0;
    if (!s_Builders[catalogIndex]) return 0;

    return s_Builders[catalogIndex](previewX, previewY, previewW, previewH);
}

/* --- Dialog tint (placeholder — will integrate with palette system) --- */

void pdguiApplyDialogTint(s32 dialogType, float tintStrength)
{
    /* Phase D4c: This will blend g_MenuColours[dialogType] with the active
     * custom theme palette at the given strength and re-apply ImGui colors.
     * For now it's a no-op — the storyboard works without tinting. */
    (void)dialogType;
    (void)tintStrength;
}

void pdguiClearDialogTint(void)
{
    /* Phase D4c: Revert to pure theme palette.  No-op for now. */
    pdguiApplyPdStyle();
}

} /* extern "C" */
