/**
 * pdgui_hud.cpp -- In-match HUD overlay for Combat Sim.
 *
 * Renders during normmplayerisrunning (CLSTATE_GAME equivalent):
 *   1. Score panel docked below the GBI radar/minimap
 *      - Team mode: top 2 teams with team-colored progress bars
 *      - FFA mode: top 2 players with progress bars
 *   2. Match time remaining in MM:SS (only when a time limit is set)
 *
 * Position: docked directly below the radar's bottom edge. When radar
 * is hidden, sits at top-right where the radar would be.
 * No background fill. Text + progress bars at 60% opacity.
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
#define MAX_TEAMS_HUD     8

/* Minimal ABI-compatible struct mirrors -- only access name[15] at offset 0. */
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
s32 mpGetTeamRankings(struct ranking_hud *rankings);

/* Time: elapsed ticks at 60Hz; limit in ticks (0 = unlimited) */
u32 lvGetStageTime60(void);
s32 pdguiHudGetTimeLimitTicks(void);

/* Bridge functions (pdgui_bridge.c) */
s32 pdguiHudIsRadarVisible(void);
s32 pdguiHudGetRadarRect(float *outX, float *outY, float *outW, float *outH);
s32 pdguiHudGetScoreLimit(void);
s32 pdguiHudGetTeamScoreLimit(void);
s32 pdguiHudIsTeamsEnabled(void);
u32 pdguiHudGetTeamColor(s32 teamIndex);
const char *pdguiHudGetTeamName(s32 teamIndex);

} /* extern "C" */

/* ========================================================================
 * Score row data
 * ======================================================================== */

struct HudScoreRow {
    char  name[16];
    s32   score;
    u32   color;    /* RGBA for progress bar */
    s32   teamIdx;  /* -1 for FFA */
};

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

static ImVec4 rgba32ToImVec4(u32 rgba)
{
    float r = ((rgba >> 24) & 0xFF) / 255.0f;
    float g = ((rgba >> 16) & 0xFF) / 255.0f;
    float b = ((rgba >>  8) & 0xFF) / 255.0f;
    float a = ((rgba >>  0) & 0xFF) / 255.0f;
    return ImVec4(r, g, b, a);
}

static ImU32 rgba32ToImU32(u32 rgba, float alphaMul)
{
    u32 r = (rgba >> 24) & 0xFF;
    u32 g = (rgba >> 16) & 0xFF;
    u32 b = (rgba >>  8) & 0xFF;
    float a = ((rgba >> 0) & 0xFF) / 255.0f * alphaMul;
    return IM_COL32(r, g, b, (int)(a * 255.0f));
}

static void copyName(char *dst, s32 dstLen, const char *src)
{
    if (!src) { dst[0] = '?'; dst[1] = '\0'; return; }
    s32 j;
    for (j = 0; j < dstLen - 1 && src[j] != '\0' && src[j] != '\n'; j++) {
        dst[j] = src[j];
    }
    dst[j] = '\0';
}

static s32 buildScoreRows(HudScoreRow *out, s32 maxOut, s32 *outScoreLimit)
{
    s32 isTeams = pdguiHudIsTeamsEnabled();

    if (isTeams) {
        /* Team mode: top 2 teams sorted by score */
        struct ranking_hud rankings[MAX_TEAMS_HUD];
        s32 count = mpGetTeamRankings(rankings);
        if (count > maxOut) count = maxOut;

        for (s32 i = 0; i < count; i++) {
            s32 team = (s32)rankings[i].teamnum;
            const char *tname = pdguiHudGetTeamName(team);
            copyName(out[i].name, sizeof(out[i].name), tname);
            out[i].score   = rankings[i].score;
            out[i].color   = pdguiHudGetTeamColor(team);
            out[i].teamIdx = team;
        }

        s32 limit = pdguiHudGetTeamScoreLimit();
        *outScoreLimit = (limit > 0) ? limit : 0;
        return count;
    } else {
        /* FFA: top 2 players sorted by score */
        struct ranking_hud rankings[MAX_MPCHRS_HUD];
        s32 count = mpGetPlayerRankings(rankings);
        if (count > maxOut) count = maxOut;

        for (s32 i = 0; i < count; i++) {
            struct mpchrconfig_hud *mpchr = rankings[i].mpchr;
            copyName(out[i].name, sizeof(out[i].name),
                     mpchr ? mpchr->name : "???");
            out[i].score   = rankings[i].score;
            out[i].color   = (i == 0) ? 0xFFD833FFu : 0xCCCCCCFFu;  /* gold / silver */
            out[i].teamIdx = -1;
        }

        s32 limit = pdguiHudGetScoreLimit();
        /* scorelimit is 0-based internally; >=99 means unlimited */
        *outScoreLimit = (limit < 99) ? (limit + 1) : 0;
        return count;
    }
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

    HudScoreRow rows[2];
    s32 scoreLimit = 0;
    s32 rowCount = buildScoreRows(rows, 2, &scoreLimit);

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
        remainSecs = remaining / 60;
    }

    if (rowCount == 0 && !hasTimer) return;

    /* ------------------------------------------------------------------ */
    /* Position: dock below radar or top-right fallback                      */
    /* ------------------------------------------------------------------ */

    const float ALPHA = 0.60f;
    float scale = (float)winW / 640.0f;

    float panelW = pdguiScale(150.0f);
    float lineH  = pdguiScale(14.0f);
    float barH   = pdguiScale(3.0f);
    float rowGap = pdguiScale(6.0f);
    float padX   = pdguiScale(6.0f);

    /* Calculate panel height */
    float panelH = 0.0f;
    for (s32 i = 0; i < rowCount; i++) {
        panelH += lineH + barH + (i < rowCount - 1 ? rowGap : 0.0f);
    }
    if (hasTimer) {
        panelH += (rowCount > 0 ? rowGap : 0.0f) + lineH;
    }

    float posX, posY;

    float radarNX, radarNY, radarNW, radarNH;
    if (pdguiHudGetRadarRect(&radarNX, &radarNY, &radarNW, &radarNH)) {
        /* Dock below radar -- convert normalized coords to window pixels */
        float radarRight  = (radarNX + radarNW) * (float)winW;
        float radarBottom = (radarNY + radarNH) * (float)winH;
        float radarCenterX = (radarNX + radarNW * 0.5f) * (float)winW;

        posX = radarCenterX - panelW * 0.5f;
        posY = radarBottom + pdguiScale(4.0f);
    } else {
        /* Radar hidden -- position at top-right where radar would be */
        posX = (float)winW - panelW - pdguiScale(14.0f);
        posY = pdguiScale(10.0f);
    }

    /* Clamp to screen bounds */
    if (posX + panelW > (float)winW - 4.0f) posX = (float)winW - panelW - 4.0f;
    if (posX < 4.0f) posX = 4.0f;

    /* ------------------------------------------------------------------ */
    /* Render: no-background ImGui window                                   */
    /* ------------------------------------------------------------------ */

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH + pdguiScale(4.0f)), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoInputs          |
        ImGuiWindowFlags_NoNav             |
        ImGuiWindowFlags_NoDecoration      |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("##hud_score_panel", nullptr, flags)) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float curY = posY;

        for (s32 i = 0; i < rowCount; i++) {
            HudScoreRow &row = rows[i];

            /* Score text: "Name  123" */
            char scoreBuf[8];
            snprintf(scoreBuf, sizeof(scoreBuf), "%d", row.score);

            ImVec4 nameCol = rgba32ToImVec4(row.color);
            nameCol.w = ALPHA;

            ImVec4 scoreCol(1.0f, 1.0f, 1.0f, ALPHA);

            /* Name (left-aligned) */
            dl->AddText(ImVec2(posX + padX, curY), ImGui::ColorConvertFloat4ToU32(nameCol), row.name);

            /* Score (right-aligned) */
            ImVec2 scoreSize = ImGui::CalcTextSize(scoreBuf);
            dl->AddText(ImVec2(posX + panelW - padX - scoreSize.x, curY),
                        ImGui::ColorConvertFloat4ToU32(scoreCol), scoreBuf);

            curY += lineH;

            /* Progress bar */
            float barLeft  = posX + padX;
            float barRight = posX + panelW - padX;
            float barWidth = barRight - barLeft;

            float ratio = 0.0f;
            if (scoreLimit > 0 && row.score > 0) {
                ratio = (float)row.score / (float)scoreLimit;
                if (ratio > 1.0f) ratio = 1.0f;
            } else if (scoreLimit == 0 && rowCount > 0) {
                /* No limit: show relative to max score in top row */
                s32 maxScore = rows[0].score;
                if (maxScore > 0) {
                    ratio = (float)row.score / (float)maxScore;
                }
            }

            /* Bar background (dim) */
            dl->AddRectFilled(
                ImVec2(barLeft, curY),
                ImVec2(barRight, curY + barH),
                IM_COL32(40, 40, 40, (int)(ALPHA * 120.0f)));

            /* Bar fill */
            if (ratio > 0.0f) {
                dl->AddRectFilled(
                    ImVec2(barLeft, curY),
                    ImVec2(barLeft + barWidth * ratio, curY + barH),
                    rgba32ToImU32(row.color, ALPHA));
            }

            curY += barH;
            if (i < rowCount - 1) curY += rowGap;
        }

        /* ---- Timer ---- */
        if (hasTimer) {
            if (rowCount > 0) curY += rowGap;

            s32 mins = remainSecs / 60;
            s32 secs = remainSecs % 60;

            ImVec4 timerColor;
            if (remainSecs > 60) {
                timerColor = ImVec4(1.0f, 1.0f,  1.0f,  ALPHA);  /* white */
            } else if (remainSecs > 15) {
                timerColor = ImVec4(1.0f, 0.85f, 0.0f,  ALPHA);  /* yellow */
            } else {
                timerColor = ImVec4(1.0f, 0.25f, 0.15f, ALPHA);  /* red */
            }

            char timeBuf[16];
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", mins, secs);

            /* Center the timer below the score rows */
            ImVec2 tw = ImGui::CalcTextSize(timeBuf);
            float timerX = posX + (panelW - tw.x) * 0.5f;
            dl->AddText(ImVec2(timerX, curY),
                        ImGui::ColorConvertFloat4ToU32(timerColor), timeBuf);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
}
