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
 *      g_MpEndscreenSavePlayerMenuDialog suppressed (B-115 fix) — auto-save via
 *      configSave("pd.ini") handles PC saving; legacy dialog stole input.
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

/* Endscreen dialog symbols — only SavePlayer is still registered here.
 * Root + sub dialogs are handled by pdgui_menu_endscreen.cpp. */
extern struct menudialogdef g_MpEndscreenSavePlayerMenuDialog;

/* Team colors — shared with pdgui_menu_pausemenu.cpp pattern */
u32 pdguiPauseGetOptions(void);
#define MPOPTION_TEAMSENABLED_KF  0x00000002

} /* extern "C" */

/* ============================================================================
 * Killfeed — event-driven ring buffer
 *
 * Receives kill events via pdguiKillfeedPush() (called from mpstats.c).
 * Shows "attacker killed victim" with team-colored names.
 * ========================================================================== */

static const int   KILLFEED_MAX       = 12;     /* ring buffer capacity */
static const float KILLFEED_LIFE_S    = 5.0f;   /* total display lifetime */
static const float KILLFEED_FADEIN_S  = 0.12f;  /* slide-in duration */
static const float KILLFEED_FADEOUT_S = 0.6f;   /* fade-out duration */
static const float KILLFEED_SLIDE_PX  = 60.0f;  /* right-to-left slide distance (720p) */

struct KillfeedEntry {
    char   attackerName[16];
    char   victimName[16];
    u8     attackerTeam;
    u8     victimTeam;
    u8     isSuicide;
    float  birthTime;
    bool   active;
};

static KillfeedEntry s_Killfeed[KILLFEED_MAX];
static int           s_KillfeedHead = 0;  /* next write slot (ring) */

/* Team color palette (same as scorecard) */
static const ImVec4 s_KfTeamColors[] = {
    ImVec4(1.0f, 0.35f, 0.35f, 1.0f),  /* Red */
    ImVec4(0.35f, 0.55f, 1.0f, 1.0f),  /* Blue */
    ImVec4(0.35f, 1.0f, 0.35f, 1.0f),  /* Green */
    ImVec4(1.0f, 1.0f, 0.35f, 1.0f),   /* Yellow */
    ImVec4(1.0f, 0.55f, 0.05f, 1.0f),  /* Orange */
    ImVec4(0.85f, 0.35f, 1.0f, 1.0f),  /* Purple */
    ImVec4(0.6f, 0.6f, 0.6f, 1.0f),    /* Grey */
    ImVec4(1.0f, 1.0f, 1.0f, 1.0f),    /* White */
};

static float kfNow()
{
    return (float)(SDL_GetTicks64()) / 1000.0f;
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

static void killfeedReset()
{
    for (int i = 0; i < KILLFEED_MAX; i++) {
        s_Killfeed[i].active = false;
    }
    s_KillfeedHead = 0;
}

/* C-callable: push a kill event into the ring buffer */
extern "C" void pdguiKillfeedPush(const char *attackerName, u8 attackerTeam,
                                   const char *victimName, u8 victimTeam,
                                   s32 isSuicide)
{
    KillfeedEntry &e = s_Killfeed[s_KillfeedHead];
    copyName(e.attackerName, sizeof(e.attackerName), attackerName);
    copyName(e.victimName,   sizeof(e.victimName),   victimName);
    e.attackerTeam = attackerTeam;
    e.victimTeam   = victimTeam;
    e.isSuicide    = (u8)isSuicide;
    e.birthTime    = kfNow();
    e.active       = true;

    s_KillfeedHead = (s_KillfeedHead + 1) % KILLFEED_MAX;
}

/* ============================================================================
 * Public render entry point
 * ========================================================================== */

extern "C" void pdguiMpIngameRender(s32 winW, s32 winH)
{
    /* Only active during a live match, and suppress during game over */
    if (!pdguiPauseGetNormMplayerIsRunning()) {
        killfeedReset();
        return;
    }
    if (pdguiPauseGetPaused() >= MPPAUSEMODE_GAMEOVER_TICKER) {
        return; /* Keep entries but stop rendering — they'll expire naturally */
    }

    bool teamsEnabled = (pdguiPauseGetOptions() & MPOPTION_TEAMSENABLED_KF) != 0;
    float now = kfNow();

    /* Layout — smaller pills for a compact killfeed */
    float fontSize = pdguiScale(13.0f); /* smaller than default HUD text */
    float pillH  = pdguiScale(22.0f);
    float pillW  = pdguiScale(320.0f);
    float padX   = pdguiScale(8.0f);
    float padY   = pdguiScale(3.0f);
    float gapY   = pdguiScale(2.0f);

    /* Position: top-right, below the HUD scorebox */
    float baseX = (float)winW - pillW - pdguiScale(14.0f);
    float baseY = pdguiScale(110.0f);

    /* Collect and sort active entries by birth time (newest first at top) */
    struct SortEntry { int idx; float birth; };
    SortEntry sorted[KILLFEED_MAX];
    int activeCount = 0;

    for (int i = 0; i < KILLFEED_MAX; i++) {
        KillfeedEntry &e = s_Killfeed[i];
        if (!e.active) continue;
        float age = now - e.birthTime;
        if (age >= KILLFEED_LIFE_S) { e.active = false; continue; }
        sorted[activeCount].idx   = i;
        sorted[activeCount].birth = e.birthTime;
        activeCount++;
    }

    /* Sort newest-first */
    for (int i = 1; i < activeCount; i++) {
        SortEntry tmp = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j].birth < tmp.birth) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = tmp;
    }

    /* Apply a small global font scale for the killfeed */
    ImGui::PushFont(nullptr); /* use default font */

    for (int si = 0; si < activeCount; si++) {
        KillfeedEntry &e = s_Killfeed[sorted[si].idx];
        float age = now - e.birthTime;

        /* Alpha: fade in, hold, fade out */
        float alpha = 1.0f;
        if (age < KILLFEED_FADEIN_S) {
            alpha = age / KILLFEED_FADEIN_S;
        } else if (age > KILLFEED_LIFE_S - KILLFEED_FADEOUT_S) {
            alpha = (KILLFEED_LIFE_S - age) / KILLFEED_FADEOUT_S;
        }
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;

        /* Slide: enter from the right */
        float slideProgress = (age < KILLFEED_FADEIN_S)
            ? (age / KILLFEED_FADEIN_S) : 1.0f;
        float slideOffset = pdguiScale(KILLFEED_SLIDE_PX) * (1.0f - slideProgress);

        float x = baseX + slideOffset;
        float y = baseY + si * (pillH + gapY);

        char wname[32];
        snprintf(wname, sizeof(wname), "##kf%d", sorted[si].idx);

        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(pillW, pillH + padY * 2.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.65f * alpha);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration    |
            ImGuiWindowFlags_NoInputs        |
            ImGuiWindowFlags_NoNav           |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.30f, 0.30f, 0.40f, alpha * 0.4f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(padX, padY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.5f);

        float fontScale = fontSize / ImGui::GetFontSize();
        ImGui::SetWindowFontScale(fontScale);

        if (ImGui::Begin(wname, nullptr, flags)) {
            ImGui::SetWindowFontScale(fontScale);

            if (e.isSuicide) {
                /* Suicide: "Player suicided" */
                u8 vt = e.victimTeam < 8 ? e.victimTeam : 7;
                ImVec4 vc = teamsEnabled ? s_KfTeamColors[vt]
                                         : ImVec4(0.9f, 0.9f, 0.9f, alpha);
                vc.w = alpha;

                ImGui::PushStyleColor(ImGuiCol_Text, vc);
                ImGui::TextUnformatted(e.victimName);
                ImGui::PopStyleColor();

                ImGui::SameLine(0.0f, pdguiScale(4.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, alpha));
                ImGui::TextUnformatted("suicided");
                ImGui::PopStyleColor();
            } else {
                /* Kill: "Attacker killed Victim" */
                u8 at = e.attackerTeam < 8 ? e.attackerTeam : 7;
                u8 vt = e.victimTeam < 8   ? e.victimTeam   : 7;
                ImVec4 ac = teamsEnabled ? s_KfTeamColors[at]
                                         : ImVec4(1.0f, 1.0f, 1.0f, alpha);
                ImVec4 vc = teamsEnabled ? s_KfTeamColors[vt]
                                         : ImVec4(0.7f, 0.7f, 0.7f, alpha);
                ac.w = alpha;
                vc.w = alpha;

                ImGui::PushStyleColor(ImGuiCol_Text, ac);
                ImGui::TextUnformatted(e.attackerName);
                ImGui::PopStyleColor();

                ImGui::SameLine(0.0f, pdguiScale(4.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, alpha));
                ImGui::TextUnformatted("killed");
                ImGui::PopStyleColor();

                ImGui::SameLine(0.0f, pdguiScale(4.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, vc);
                ImGui::TextUnformatted(e.victimName);
                ImGui::PopStyleColor();
            }

            ImGui::SetWindowFontScale(1.0f);
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    ImGui::PopFont();
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
    /* Endscreen ROOT dialogs (IndGameOver, TeamGameOver) are registered by
     * pdguiMenuEndscreenRegister() with full ImGui renders (rankings, stats,
     * action buttons, input context push).  Do NOT re-register them here
     * as noops — that was the pre-endscreen.cpp approach and caused a
     * dual-render conflict when both paths existed.
     *
     * Endscreen SUB dialogs (PlayerRanking, TeamRanking, PlayerStats) are
     * also registered by endscreen.cpp as noops — content folded into root.
     *
     * B-115 fix: suppress Save Player — auto-save via configSave("pd.ini")
     * in pdguiEndscreenExitToMainMenu() handles PC saving. Legacy N64 dialog
     * was for Controller Pak writes; keeping it native stole input from ImGui.
     * This dialog is NOT registered by endscreen.cpp, so we keep it here. */
    pdguiHotswapRegister(&g_MpEndscreenSavePlayerMenuDialog,
        renderNoop, "MP Endscreen Save Player (suppressed)");
}
