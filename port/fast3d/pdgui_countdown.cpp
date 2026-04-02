/**
 * pdgui_countdown.cpp -- Pre-match countdown popup overlay.
 *
 * Renders a full-screen centered popup when the server fires the
 * MANIFEST_PHASE_LOADING countdown (3 → 2 → 1 → GO).
 *
 * Features:
 *   - Large PD-styled number, counts 3 → 2 → 1 → GO
 *   - Audio cue on each number change (tick for 3/2/1, success chime for GO)
 *   - ESC or gamepad B-button cancels the countdown for all players
 *   - "[Name] cancelled the match start" banner shown on cancellation
 *   - Banner auto-clears after CANCEL_DISPLAY_TICKS frames (~3s at 60fps)
 *
 * IMPORTANT: C++ translation unit — must NOT include types.h (#define bool s32
 * breaks C++).  All game state access goes through pdgui_bridge.c functions.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_scaling.h"
#include "pdgui_style.h"
#include "pdgui_audio.h"
#include "system.h"

/* ============================================================================
 * C boundary declarations
 * ========================================================================== */

extern "C" {

/* Countdown state accessors (pdgui_bridge.c) */
s32         pdguiCountdownIsActive(void);
s32         pdguiCountdownGetSecs(void);
s32         pdguiCancelledIsActive(void);
const char *pdguiCancelledGetName(void);
void        pdguiCancelledClear(void);

/* Cancel request sender (pdgui_bridge.c) */
s32 netLobbyRequestCancel(void);

} /* extern "C" */

/* ============================================================================
 * Internal state
 * ========================================================================== */

/* Tracks countdown_secs from the previous frame to detect when to fire audio. */
static s32 s_PrevCountdownSecs = -1;

/* How long to show the cancel banner (frames at 60fps ≈ 3 seconds). */
#define CANCEL_DISPLAY_TICKS 180

static s32 s_CancelDisplayTimer = 0;
static char s_CancelMessage[96];  /* "[Name] cancelled the match start" */

/* ============================================================================
 * pdguiCountdownRender
 * ========================================================================== */

extern "C" void pdguiCountdownRender(s32 winW, s32 winH)
{
    s32 currentSecs;
    s32 showCountdown;
    s32 showCancel;

    /* ---- Tick the cancel banner timer ---- */
    if (s_CancelDisplayTimer > 0) {
        s_CancelDisplayTimer--;
        if (s_CancelDisplayTimer == 0) {
            pdguiCancelledClear();
            s_CancelMessage[0] = '\0';
        }
    }

    /* ---- Arm cancel banner when a new SVC_MATCH_CANCELLED arrives ---- */
    if (pdguiCancelledIsActive() && s_CancelDisplayTimer == 0) {
        const char *who = pdguiCancelledGetName();
        snprintf(s_CancelMessage, sizeof(s_CancelMessage),
                 "%s cancelled the match start", who && who[0] ? who : "Someone");
        s_CancelDisplayTimer = CANCEL_DISPLAY_TICKS;
    }

    showCountdown = pdguiCountdownIsActive();
    showCancel    = (s_CancelDisplayTimer > 0);

    if (!showCountdown && !showCancel) {
        s_PrevCountdownSecs = -1;
        return;
    }

    /* ---- Detect countdown value change → fire audio + log ---- */
    if (showCountdown) {
        currentSecs = pdguiCountdownGetSecs();
        if (currentSecs != s_PrevCountdownSecs) {
            /* First activation: s_PrevCountdownSecs was -1 */
            if (s_PrevCountdownSecs == -1) {
                sysLogPrintf(LOG_NOTE, "MENU_STACK: countdown START secs=%d", currentSecs);
            }
            s_PrevCountdownSecs = currentSecs;
            if (currentSecs > 0) {
                sysLogPrintf(LOG_NOTE, "MENU_STACK: countdown TICK secs=%d", currentSecs);
                /* Tick / beep for 3, 2, 1 */
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            } else {
                sysLogPrintf(LOG_NOTE, "MENU_STACK: countdown GO");
                /* Distinct chime for GO */
                pdguiPlaySound(PDGUI_SND_SUCCESS);
            }
        }
    }

    /* ---- Check for cancel input (ESC or gamepad B) ---- */
    if (showCountdown) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
            sysLogPrintf(LOG_NOTE, "MENU_STACK: countdown CANCEL by local player (ESC/B)");
            netLobbyRequestCancel();
        }
    }

    /* ========== Render the countdown popup ========== */

    float scale = pdguiScaleFactor();
    float cx    = (float)winW * 0.5f;
    float cy    = (float)winH * 0.5f;

    if (showCountdown) {
        currentSecs = pdguiCountdownGetSecs();

        /* --- Semi-transparent full-screen dim --- */
        ImDrawList *bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(ImVec2(0, 0), ImVec2((float)winW, (float)winH),
                          IM_COL32(0, 0, 0, 160));

        /* --- Popup box --- */
        float boxW = pdguiScale(280.0f);
        float boxH = pdguiScale(200.0f);
        float boxX = cx - boxW * 0.5f;
        float boxY = cy - boxH * 0.5f;

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar    |
            ImGuiWindowFlags_NoResize      |
            ImGuiWindowFlags_NoMove        |
            ImGuiWindowFlags_NoScrollbar   |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav         |
            ImGuiWindowFlags_NoDecoration  |
            ImGuiWindowFlags_NoBackground;

        ImGui::SetNextWindowPos(ImVec2(boxX, boxY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(boxW, boxH), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        if (ImGui::Begin("##countdown_popup", nullptr, flags)) {
            ImDrawList *dl = ImGui::GetWindowDrawList();

            /* Box background */
            dl->AddRectFilled(ImVec2(boxX, boxY),
                              ImVec2(boxX + boxW, boxY + boxH),
                              IM_COL32(8, 8, 20, 235),
                              pdguiScale(6.0f));

            /* PD-style accent border */
            dl->AddRect(ImVec2(boxX, boxY),
                        ImVec2(boxX + boxW, boxY + boxH),
                        IM_COL32(80, 160, 255, 200),
                        pdguiScale(6.0f), 0,
                        pdguiScale(2.0f));

            /* Header label */
            {
                float headerFontSize = pdguiScale(13.0f);
                ImGui::SetCursorPos(ImVec2(0, pdguiScale(14.0f)));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.0f, 0.9f));
                float tw = ImGui::CalcTextSize("MATCH STARTING").x *
                           (headerFontSize / ImGui::GetFontSize());
                ImGui::SetCursorPosX((boxW - tw) * 0.5f);
                ImGui::TextUnformatted("MATCH STARTING");
                ImGui::PopStyleColor();
            }

            /* Big countdown number or GO */
            {
                char numBuf[8];
                ImVec4 numColor;

                if (currentSecs > 0) {
                    snprintf(numBuf, sizeof(numBuf), "%d", currentSecs);
                    /* Color shifts: 3=yellow, 2=orange, 1=red */
                    if (currentSecs == 3) {
                        numColor = ImVec4(1.0f, 0.95f, 0.3f, 1.0f);
                    } else if (currentSecs == 2) {
                        numColor = ImVec4(1.0f, 0.65f, 0.15f, 1.0f);
                    } else {
                        numColor = ImVec4(1.0f, 0.25f, 0.15f, 1.0f);
                    }
                } else {
                    snprintf(numBuf, sizeof(numBuf), "GO!");
                    numColor = ImVec4(0.3f, 1.0f, 0.45f, 1.0f);
                }

                /* Scale the big number via font scale push */
                float bigScale = 4.5f * scale;
                ImGui::SetWindowFontScale(bigScale);
                float numW = ImGui::CalcTextSize(numBuf).x;
                float numH = ImGui::CalcTextSize(numBuf).y;
                ImGui::SetWindowFontScale(1.0f);

                float numX = (boxW - numW) * 0.5f;
                float numY = (boxH - numH) * 0.5f + pdguiScale(6.0f);

                dl->AddText(nullptr, ImGui::GetFontSize() * bigScale,
                            ImVec2(boxX + numX, boxY + numY),
                            ImGui::ColorConvertFloat4ToU32(numColor),
                            numBuf);
            }

            /* Footer hint */
            {
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.55f, 0.8f));
                const char *hint = "Press ESC / B to cancel";
                float hw = ImGui::CalcTextSize(hint).x;
                ImGui::SetCursorPos(ImVec2((boxW - hw) * 0.5f,
                                           boxH - pdguiScale(26.0f)));
                ImGui::TextUnformatted(hint);
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    /* ========== Render the cancel banner ========== */

    if (showCancel && s_CancelMessage[0]) {
        float alpha = 1.0f;
        /* Fade out in the last 60 frames */
        if (s_CancelDisplayTimer < 60) {
            alpha = (float)s_CancelDisplayTimer / 60.0f;
        }

        float bannerH = pdguiScale(40.0f);
        float bannerW = pdguiScale(420.0f);
        float bannerX = cx - bannerW * 0.5f;
        float bannerY = (float)winH * 0.72f;

        ImGuiWindowFlags bflags =
            ImGuiWindowFlags_NoTitleBar    |
            ImGuiWindowFlags_NoResize      |
            ImGuiWindowFlags_NoMove        |
            ImGuiWindowFlags_NoScrollbar   |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav         |
            ImGuiWindowFlags_NoDecoration  |
            ImGuiWindowFlags_NoBackground  |
            ImGuiWindowFlags_NoInputs;

        ImGui::SetNextWindowPos(ImVec2(bannerX, bannerY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(bannerW, bannerH), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        if (ImGui::Begin("##cancel_banner", nullptr, bflags)) {
            ImDrawList *dl = ImGui::GetWindowDrawList();

            /* Banner background */
            dl->AddRectFilled(
                ImVec2(bannerX, bannerY),
                ImVec2(bannerX + bannerW, bannerY + bannerH),
                IM_COL32(25, 8, 8, (int)(200 * alpha)),
                pdguiScale(4.0f));
            dl->AddRect(
                ImVec2(bannerX, bannerY),
                ImVec2(bannerX + bannerW, bannerY + bannerH),
                IM_COL32(200, 60, 60, (int)(180 * alpha)),
                pdguiScale(4.0f), 0,
                pdguiScale(1.5f));

            /* Message text */
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(1.0f, 0.55f, 0.45f, alpha));
            float tw = ImGui::CalcTextSize(s_CancelMessage).x;
            ImGui::SetCursorPos(ImVec2((bannerW - tw) * 0.5f,
                                       (bannerH - ImGui::GetFontSize()) * 0.5f));
            ImGui::TextUnformatted(s_CancelMessage);
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
}
