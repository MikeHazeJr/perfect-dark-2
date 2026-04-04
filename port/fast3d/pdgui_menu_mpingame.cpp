/**
 * pdgui_menu_mpingame.cpp -- MP In-Game overlays: kill ticker + endscreen suppression.
 *
 * Responsibilities:
 *   1. Kill / score ticker overlay: polls mpGetPlayerRankings each frame, detects
 *      score deltas, and renders animated slide-in notifications (top-right corner,
 *      below the HUD scorebox).  Active only during normmplayerisrunning and only
 *      when MPPAUSEMODE_GAMEOVER is NOT set.
 *
 *   2. Endscreen dialog suppression: registers no-op hotswap renders for the five
 *      DEFAULT-type endscreen dialogs so the legacy N64 GBI rendering is silenced.
 *      pdguiGameOverRender() (in pdgui_menu_pausemenu.cpp) owns the full tabbed
 *      end-screen and fires independently of hotswap state.
 *      g_MpEndscreenSavePlayerMenuDialog is intentionally kept NATIVE — it contains
 *      a keyboard text-input field that we do not yet replace.
 *
 * IMPORTANT: C++ translation unit — must NOT include types.h (#define bool s32 breaks
 * C++ bool).  All game data access goes through pdgui_bridge.c functions.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 *
 * Part of Legacy Dialog Migration — Group 5: MP In-Game.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "imgui/imgui.h"
#include "pdgui_scaling.h"
#include "pdgui_style.h"
#include "pdgui_hotswap.h"

/* ============================================================================
 * C boundary declarations
 * ========================================================================== */

extern "C" {

/* Game-running guard */
s32 pdguiPauseGetNormMplayerIsRunning(void);

/* Pause mode — same values as MPPAUSEMODE_* in types.h */
s32 pdguiPauseGetPaused(void);
#define MPPAUSEMODE_GAMEOVER_TICKER  2  /* matches MPPAUSEMODE_GAMEOVER in types.h */

/* Score / ranking access — mirrors the minimal struct from pdgui_hud.cpp.
 * MAX_MPCHRS = MAX_PLAYERS + MAX_BOTS = 8 + 32 = 40 */
#define MAX_MPCHRS_TICKER  40

struct mpchrconfig_ticker {
    char name[15];   /* offset 0x00 — always first field, layout-safe */
};

struct ranking_ticker {
    struct mpchrconfig_ticker *mpchr;
    union {
        u32 teamnum;
        u32 chrnum;
    };
    u32 positionindex;
    u8  unk0c;
    s32 score;
};

s32 mpGetPlayerRankings(struct ranking_ticker *rankings);

/* Endscreen dialog symbols not declared in data.h */
extern struct menudialogdef g_MpEndscreenIndGameOverMenuDialog;
extern struct menudialogdef g_MpEndscreenTeamGameOverMenuDialog;
extern struct menudialogdef g_MpEndscreenPlayerRankingMenuDialog;
extern struct menudialogdef g_MpEndscreenTeamRankingMenuDialog;
extern struct menudialogdef g_MpEndscreenPlayerStatsMenuDialog;
extern struct menudialogdef g_MpEndscreenSavePlayerMenuDialog;

} /* extern "C" */

/* ============================================================================
 * Kill / score ticker
 * ========================================================================== */

static const int   TICKER_MAX       = 5;       /* max simultaneous notifications */
static const float TICKER_LIFE_S    = 3.5f;    /* total lifetime in seconds */
static const float TICKER_FADEIN_S  = 0.15f;   /* slide-in duration */
static const float TICKER_FADEOUT_S = 0.45f;   /* fade-out duration */
static const float TICKER_SLIDE_PX  = 80.0f;   /* right-to-left slide-in distance (at 720p) */
static const float TICKER_POLL_S    = 0.08f;   /* score poll interval (~12 Hz is plenty) */

struct TickerNote {
    char   playerName[16];
    s32    delta;
    float  birthTime;   /* SDL_GetTicks64() in seconds */
    bool   active;
};

static TickerNote  s_Notes[TICKER_MAX];
static s32         s_LastScores[MAX_MPCHRS_TICKER];
static bool        s_ScoresInited = false;
static float       s_NextPollTime = 0.0f;

static float tickerNow()
{
    return (float)(SDL_GetTicks64()) / 1000.0f;
}

static void tickerPush(const char *name, s32 delta)
{
    /* Evict oldest if full */
    float now = tickerNow();
    int   slot = -1;
    float oldest = 1e30f;
    for (int i = 0; i < TICKER_MAX; i++) {
        if (!s_Notes[i].active) { slot = i; break; }
        if (s_Notes[i].birthTime < oldest) { oldest = s_Notes[i].birthTime; slot = i; }
    }
    if (slot < 0) slot = 0;

    TickerNote &n = s_Notes[slot];
    strncpy(n.playerName, name, sizeof(n.playerName) - 1);
    n.playerName[sizeof(n.playerName) - 1] = '\0';
    n.delta     = delta;
    n.birthTime = now;
    n.active    = true;
}

static void tickerPoll()
{
    float now = tickerNow();
    if (now < s_NextPollTime) return;
    s_NextPollTime = now + TICKER_POLL_S;

    struct ranking_ticker rankings[MAX_MPCHRS_TICKER];
    s32 count = mpGetPlayerRankings(rankings);
    if (count <= 0) { s_ScoresInited = false; return; }

    if (!s_ScoresInited) {
        /* Baseline snapshot — don't fire notifications on first seen */
        for (int i = 0; i < count && i < MAX_MPCHRS_TICKER; i++) {
            s_LastScores[i] = rankings[i].score;
        }
        s_ScoresInited = true;
        return;
    }

    for (int i = 0; i < count && i < MAX_MPCHRS_TICKER; i++) {
        s32 newScore = rankings[i].score;
        s32 oldScore = s_LastScores[i];
        if (newScore > oldScore && rankings[i].mpchr) {
            tickerPush(rankings[i].mpchr->name, newScore - oldScore);
        }
        s_LastScores[i] = newScore;
    }
}

static void tickerReset()
{
    for (int i = 0; i < TICKER_MAX; i++) {
        s_Notes[i].active = false;
    }
    s_ScoresInited = false;
    s_NextPollTime = 0.0f;
}

/* ============================================================================
 * Public render entry point
 * ========================================================================== */

extern "C" void pdguiMpIngameRender(s32 winW, s32 winH)
{
    /* Only active during a live match, and suppress during game over */
    if (!pdguiPauseGetNormMplayerIsRunning()) {
        tickerReset();
        return;
    }
    if (pdguiPauseGetPaused() >= MPPAUSEMODE_GAMEOVER_TICKER) {
        tickerReset();
        return;
    }

    tickerPoll();

    /* --- Render active notifications --- */
    float now = tickerNow();
    float sf  = pdguiScaleFactor();

    /* Notification pill dimensions */
    float pillH  = pdguiScale(22.0f);
    float pillW  = pdguiScale(180.0f);
    float padX   = pdguiScale(8.0f);
    float padY   = pdguiScale(4.0f);
    float gapY   = pdguiScale(4.0f);

    /* Position: top-right, offset below HUD box (~76px of HUD at 720p) */
    float baseX = (float)winW - pillW - pdguiScale(12.0f);
    float baseY = pdguiScale(84.0f);  /* below top-right HUD scorebox */

    int activeCount = 0;
    for (int i = 0; i < TICKER_MAX; i++) {
        TickerNote &n = s_Notes[i];
        if (!n.active) continue;

        float age = now - n.birthTime;
        if (age >= TICKER_LIFE_S) { n.active = false; continue; }

        /* Alpha: fade in, hold, fade out */
        float alpha = 1.0f;
        if (age < TICKER_FADEIN_S) {
            alpha = age / TICKER_FADEIN_S;
        } else if (age > TICKER_LIFE_S - TICKER_FADEOUT_S) {
            alpha = (TICKER_LIFE_S - age) / TICKER_FADEOUT_S;
        }
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;

        /* Slide: enter from the right */
        float slideProgress = (age < TICKER_FADEIN_S)
            ? (age / TICKER_FADEIN_S)
            : 1.0f;
        float slideOffset = pdguiScale(TICKER_SLIDE_PX) * (1.0f - slideProgress);

        float x = baseX + slideOffset;
        float y = baseY + activeCount * (pillH + gapY);

        /* Build window name per-slot to allow multiple unique windows */
        char wname[32];
        snprintf(wname, sizeof(wname), "##ticker%d", i);

        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(pillW, pillH + padY * 2.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f * alpha);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration    |
            ImGuiWindowFlags_NoInputs        |
            ImGuiWindowFlags_NoNav           |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing;

        ImGui::PushStyleColor(ImGuiCol_WindowBg,   ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border,     ImVec4(0.40f, 0.40f, 0.50f, alpha * 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

        if (ImGui::Begin(wname, nullptr, flags)) {
            /* Delta label: "+1", "+2", etc. — gold colored */
            char deltaStr[12];
            snprintf(deltaStr, sizeof(deltaStr), "+%d", n.delta);

            /* Arrow glyph */
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.75f, 0.20f, alpha));
            ImGui::TextUnformatted("\xe2\x96\xba");  /* ► U+25BA */
            ImGui::PopStyleColor();

            ImGui::SameLine(0.0f, pdguiScale(4.0f));

            /* Player name — white */
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, alpha));
            ImGui::TextUnformatted(n.playerName);
            ImGui::PopStyleColor();

            /* Score delta — right-aligned gold */
            float deltaW = ImGui::CalcTextSize(deltaStr).x;
            float available = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(available - deltaW);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, alpha));
            ImGui::TextUnformatted(deltaStr);
            ImGui::PopStyleColor();
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);

        activeCount++;
    }
}

/* ============================================================================
 * No-op hotswap render — silences a legacy dialog without showing anything.
 * The game-over screen is handled by pdguiGameOverRender in pausemenu.cpp.
 *
 * Signature must match PdguiMenuRenderFn:
 *   s32 (*)(struct menudialog*, struct menu*, s32 winW, s32 winH)
 * ========================================================================== */

/* Forward-declare the opaque game types so we can match the function signature
 * without pulling in types.h (which #define bool s32 and breaks C++). */
struct menudialog;
struct menu;

static s32 renderNoop(struct menudialog * /*dialog*/, struct menu * /*root*/,
                      s32 /*winW*/, s32 /*winH*/) { return 1; }

/* ============================================================================
 * Registration
 * ========================================================================== */

extern "C" void pdguiMpIngameRegister(void)
{
    /* Suppress the five DEFAULT-type endscreen dialogs.
     * pdguiGameOverRender owns the actual end-screen UI. */
    pdguiHotswapRegister(&g_MpEndscreenIndGameOverMenuDialog,
        renderNoop, "MP Endscreen Ind (suppressed)");
    pdguiHotswapRegister(&g_MpEndscreenTeamGameOverMenuDialog,
        renderNoop, "MP Endscreen Team (suppressed)");
    pdguiHotswapRegister(&g_MpEndscreenPlayerRankingMenuDialog,
        renderNoop, "MP Endscreen Player Ranking (suppressed)");
    pdguiHotswapRegister(&g_MpEndscreenTeamRankingMenuDialog,
        renderNoop, "MP Endscreen Team Ranking (suppressed)");
    pdguiHotswapRegister(&g_MpEndscreenPlayerStatsMenuDialog,
        renderNoop, "MP Endscreen Player Stats (suppressed)");

    /* Keep Save Player native — keyboard text input, do not replace yet */
    pdguiHotswapRegister(&g_MpEndscreenSavePlayerMenuDialog,
        NULL, "MP Endscreen Save Player (native)");
}
