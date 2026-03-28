/**
 * pdgui_hud.cpp -- In-match HUD overlay for Combat Sim.
 *
 * Renders during normmplayerisrunning (CLSTATE_GAME equivalent):
 *   1. Top 2 scorers by game-calculated score (name + score)
 *   2. Match time remaining in MM:SS (only when a time limit is set)
 *
 * Position: top-right corner, semi-transparent background.
 * Timer color: white > 60s, yellow <= 60s, red <= 15s.
 * Timer freezes naturally because g_StageTimeElapsed60 stops incrementing
 * when the game is paused (lvupdate60 = 0 during pause).
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32 breaks C++).
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_hud.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"

/* ========================================================================
 * C boundary declarations
 * ======================================================================== */

extern "C" {

/* Game-running guard */
s32 pdguiPauseGetNormMplayerIsRunning(void);

/* Score / ranking access.
 * MAX_MPCHRS = MAX_PLAYERS + MAX_BOTS = 8 + 32 = 40 (constants.h) */
#define MAX_MPCHRS_HUD    40

/* Minimal mirror of struct mpchrconfig in types.h.
 * We only read name[] (offset 0x00, always first field, layout-safe).
 * The game-computed score comes from ranking_hud.score, not killcounts[],
 * so we never access the struct beyond offset 0x00. */
struct mpchrconfig_hud {
    char name[15]; /* offset 0x00 */
};

struct ranking_hud {
    struct mpchrconfig_hud *mpchr;
    union {
        u32 teamnum;
        u32 chrnum;
    };
    u32 positionindex;
    u8  unk0c;
    s32 score;
};

s32 mpGetPlayerRankings(struct ranking_hud *rankings);

/* Time: elapsed ticks at 60Hz; limit in ticks (0 = unlimited) */
u32 lvGetStageTime60(void);
s32 pdguiHudGetTimeLimitTicks(void);

} /* extern "C" */

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

struct HudScoreRow {
    char name[16];
    s32  score;
};

static s32 buildTopScorers(HudScoreRow *out, s32 maxOut)
{
    struct ranking_hud rankings[MAX_MPCHRS_HUD];
    s32 count = mpGetPlayerRankings(rankings);
    if (count > maxOut) count = maxOut;

    for (s32 i = 0; i < count; i++) {
        struct mpchrconfig_hud *mpchr = rankings[i].mpchr;
        if (!mpchr) {
            out[i].name[0] = '?';
            out[i].name[1] = '\0';
            out[i].score = 0;
            continue;
        }

        /* Copy name -- original uses newline terminator, not null */
        s32 j;
        for (j = 0; j < 14 && mpchr->name[j] != '\0' && mpchr->name[j] != '\n'; j++) {
            out[i].name[j] = mpchr->name[j];
        }
        out[i].name[j] = '\0';

        /* Use game-computed score (scenarioCalculatePlayerScore result).
         * For Combat/deathmatch this equals kill count. Using this avoids
         * accessing killcounts[] and the associated struct-layout concerns. */
        out[i].score = rankings[i].score;
    }

    return count;
}

/* ========================================================================
 * HUD render
 * ======================================================================== */

void pdguiHudRender(s32 winW, s32 winH)
{
    /* Only show during an active match */
    if (!pdguiPauseGetNormMplayerIsRunning()) {
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Build score data                                                     */
    /* ------------------------------------------------------------------ */

    HudScoreRow topScorers[2];
    s32 scorerCount = buildTopScorers(topScorers, 2);

    /* ------------------------------------------------------------------ */
    /* Build time data                                                      */
    /* ------------------------------------------------------------------ */

    s32 limitTicks = pdguiHudGetTimeLimitTicks();
    bool hasTimer  = (limitTicks > 0);
    s32 remainSecs = 0;

    if (hasTimer) {
        s32 elapsed = (s32)lvGetStageTime60();
        s32 remaining = limitTicks - elapsed;
        if (remaining < 0) remaining = 0;
        /* SECSTOTIME60(1) = 60 (constants.h), so ticks / 60 = seconds */
        remainSecs = remaining / 60;
    }

    /* ------------------------------------------------------------------ */
    /* Window layout                                                        */
    /* ------------------------------------------------------------------ */

    float scale  = winW / 640.0f;
    float padX   = 10.0f * scale;
    float padY   = 10.0f * scale;
    float lineH  = 14.0f * scale;
    float panelW = 160.0f * scale;

    s32 rowCount = scorerCount + (hasTimer ? 1 : 0);
    if (rowCount == 0) return;

    float panelH = padY * 2.0f + rowCount * lineH
                 + (rowCount > 1 ? (rowCount - 1) * 2.0f * scale : 0.0f);

    float posX = winW - panelW - padX;
    float posY = padY;

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar   |
        ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoScrollbar  |
        ImGuiWindowFlags_NoInputs     |
        ImGuiWindowFlags_NoNav        |
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4.0f * scale, 2.0f * scale));

    if (ImGui::Begin("##hud_overlay", nullptr, flags)) {

        static const ImVec4 kGold = ImVec4(1.0f, 0.85f, 0.2f,  1.0f);
        static const ImVec4 kSilv = ImVec4(0.8f, 0.8f,  0.8f,  1.0f);
        static const ImVec4 kCyan = ImVec4(0.35f, 0.85f, 1.0f, 1.0f);

        /* ---- Top scorers ---- */
        for (s32 i = 0; i < scorerCount; i++) {
            const ImVec4 &nameColor = (i == 0) ? kGold : kSilv;
            char nameBuf[32];
            char scoreBuf[8];

            snprintf(nameBuf,  sizeof(nameBuf),  "%d. %s", i + 1, topScorers[i].name);
            snprintf(scoreBuf, sizeof(scoreBuf),  " %d", topScorers[i].score);

            ImGui::PushStyleColor(ImGuiCol_Text, nameColor);
            ImGui::TextUnformatted(nameBuf);
            ImGui::PopStyleColor();

            ImGui::SameLine(0, 2.0f * scale);

            ImGui::PushStyleColor(ImGuiCol_Text, kCyan);
            ImGui::TextUnformatted(scoreBuf);
            ImGui::PopStyleColor();
        }

        /* ---- Timer ---- */
        if (hasTimer) {
            s32 mins = remainSecs / 60;
            s32 secs = remainSecs % 60;

            ImVec4 timerColor;
            if (remainSecs > 60) {
                timerColor = ImVec4(1.0f, 1.0f,  1.0f,  0.9f);  /* white */
            } else if (remainSecs > 15) {
                timerColor = ImVec4(1.0f, 0.85f, 0.0f,  1.0f);  /* yellow */
            } else {
                timerColor = ImVec4(1.0f, 0.25f, 0.15f, 1.0f);  /* red */
            }

            char timeBuf[16];
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", mins, secs);

            /* Center the timer line horizontally */
            float tw     = ImGui::CalcTextSize(timeBuf).x;
            float availW = panelW - padX * 2.0f;
            float offset = (availW - tw) * 0.5f;
            if (offset < 0.0f) offset = 0.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

            ImGui::PushStyleColor(ImGuiCol_Text, timerColor);
            ImGui::TextUnformatted(timeBuf);
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
}
