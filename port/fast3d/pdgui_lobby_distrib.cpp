/**
 * pdgui_lobby_distrib.cpp -- D3R-9: Download progress overlay + kill feed.
 *
 * Renders two overlays on top of the existing lobby/game UI:
 *
 * 1. DOWNLOAD PROGRESS (client in CLSTATE_LOBBY, distrib state != IDLE):
 *    - Banner across the bottom of the lobby screen.
 *    - Phase 1 (DIFFING): "Comparing mod catalog..." spinner.
 *    - Phase 2 (RECEIVING): Component name, byte progress bar, component counter.
 *    - First-connect prompt (before CLC_CATALOG_DIFF sent): shows missing count
 *      with [Download Permanently] / [This Session Only] / [Skip] buttons.
 *    - Calls netDistribClientSetTemporary() before the diff is sent if the player
 *      has chosen Session Only; skip sends an empty diff.
 *
 * 2. KILL FEED (client in CLSTATE_GAME, active entries present):
 *    - Semi-transparent panel in the top-right corner.
 *    - Shows last N kills (attacker → victim via weapon, with flag icons).
 *    - Entries scroll in and fade after KILLFEED_DISPLAY_SECS.
 *
 * Called from pdguiDistribOverlayRender() and pdguiKillFeedRender()
 * which are invoked from pdguiLobbyRender() in pdgui_lobby.cpp.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "imgui/imgui.h"
#include "pdgui_style.h"

/* ========================================================================
 * C declarations (game symbols, no types.h)
 * ======================================================================== */

extern "C" {

/* Distribution client state */
#define DISTRIB_CSTATE_IDLE      0
#define DISTRIB_CSTATE_DIFFING   1
#define DISTRIB_CSTATE_RECEIVING 2
#define DISTRIB_CSTATE_DONE      3
#define DISTRIB_CSTATE_ERROR     4

/* Kill feed flags */
#define KILLFEED_FLAG_HEADSHOT   (1 << 0)
#define KILLFEED_FLAG_EXPLOSION  (1 << 1)
#define KILLFEED_FLAG_PROXY_MINE (1 << 2)
#define KILLFEED_FLAG_MULTI_KILL (1 << 3)

#define KILLFEED_MAX_ENTRIES 16
#define KILLFEED_NAME_LEN    32
#define KILLFEED_WEAPON_LEN  48

typedef struct {
    s32 state;
    s32 missing_count;
    s32 received_count;
    char current_id[64];
    u32 current_bytes_received;
    u32 current_bytes_total;
    s32 temporary;
    u32 session_bytes_total;
} distrib_client_status_t;

typedef struct {
    char attacker[KILLFEED_NAME_LEN];
    char victim[KILLFEED_NAME_LEN];
    char weapon[KILLFEED_WEAPON_LEN];
    u8   flags;
    u32  timestamp;
    s32  active;
} killfeed_entry_t;

void netDistribClientGetStatus(distrib_client_status_t *out);
s32  netDistribClientGetKillFeed(killfeed_entry_t *out, s32 maxout);
void netDistribClientSetTemporary(s32 temporary);

} /* extern "C" */

/* ========================================================================
 * Kill feed
 * ======================================================================== */

#define KILLFEED_DISPLAY_SECS 6.0f
#define KILLFEED_FADE_SECS    1.0f
#define KILLFEED_SHOW_MAX     5   /* how many entries to show at once */

extern "C" void pdguiKillFeedRender(s32 winW, s32 winH)
{
    killfeed_entry_t entries[KILLFEED_MAX_ENTRIES];
    s32 count = netDistribClientGetKillFeed(entries, KILLFEED_MAX_ENTRIES);
    if (count <= 0) {
        return;
    }

    float scale = (float)winH / 720.0f;
    if (scale < 0.5f) scale = 0.5f;

    float fontSize  = floorf(14.0f * scale);
    float lineH     = fontSize + floorf(4.0f * scale);
    float padX      = floorf(8.0f  * scale);
    float padY      = floorf(6.0f  * scale);
    float panelW    = floorf(280.0f * scale);

    s32 show = count < KILLFEED_SHOW_MAX ? count : KILLFEED_SHOW_MAX;
    float panelH = padY * 2.0f + lineH * (float)show;

    float posX = (float)winW - panelW - floorf(8.0f * scale);
    float posY = floorf(8.0f * scale);

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##killfeed", nullptr, flags)) {
        ImGui::SetWindowFontScale(fontSize / ImGui::GetFontSize());

        for (s32 i = 0; i < show; ++i) {
            const killfeed_entry_t &e = entries[i];

            /* Build display string */
            char line[128];
            const char *sep = (e.flags & KILLFEED_FLAG_HEADSHOT) ? " [HS] " : " > ";
            snprintf(line, sizeof(line), "%s%s%s (%s)",
                     e.attacker, sep, e.victim, e.weapon);

            /* Colour: headshot = yellow, explosion = orange, else white */
            ImVec4 col(1.0f, 1.0f, 1.0f, 1.0f);
            if (e.flags & KILLFEED_FLAG_HEADSHOT) {
                col = ImVec4(1.0f, 0.95f, 0.3f, 1.0f);
            } else if (e.flags & KILLFEED_FLAG_EXPLOSION) {
                col = ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
            }

            ImGui::TextColored(col, "%s", line);
        }

        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
}

/* ========================================================================
 * Download overlay
 * ======================================================================== */

/* Prompt state: shown once when we first discover missing components. */
static bool  s_PromptShown  = false;   /* have we shown the initial prompt? */
static bool  s_PromptDone   = false;   /* has the user made a choice? */
static s32   s_PromptChoice = 0;       /* 0=permanent, 1=session, 2=skip */

static void resetPromptState(void)
{
    s_PromptShown  = false;
    s_PromptDone   = false;
    s_PromptChoice = 0;
}

/* Format bytes as human-readable (KB/MB) */
static const char *fmtBytes(u32 bytes, char *buf, s32 bufsz)
{
    if (bytes >= 1024u * 1024u) {
        snprintf(buf, (size_t)bufsz, "%.1f MB", (float)bytes / (1024.0f * 1024.0f));
    } else if (bytes >= 1024u) {
        snprintf(buf, (size_t)bufsz, "%.1f KB", (float)bytes / 1024.0f);
    } else {
        snprintf(buf, (size_t)bufsz, "%u B", bytes);
    }
    return buf;
}

extern "C" void pdguiDistribOverlayRender(s32 winW, s32 winH)
{
    distrib_client_status_t st;
    netDistribClientGetStatus(&st);

    /* Reset prompt state when download goes idle again */
    if (st.state == DISTRIB_CSTATE_IDLE || st.state == DISTRIB_CSTATE_DONE) {
        resetPromptState();
        return;
    }

    if (st.state == DISTRIB_CSTATE_ERROR) {
        /* Show brief error notice in the bottom bar */
        float scale = (float)winH / 720.0f;
        if (scale < 0.5f) scale = 0.5f;
        float barH = floorf(36.0f * scale);
        ImGui::SetNextWindowPos(ImVec2(0.0f, (float)winH - barH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)winW, barH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##distrib_err", nullptr, flags)) {
            float fs = floorf(13.0f * scale);
            ImGui::SetWindowFontScale(fs / ImGui::GetFontSize());
            ImGui::SetCursorPosY((barH - fs) * 0.5f - 2.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "  Mod download failed. You may be missing required components.");
            ImGui::SetWindowFontScale(1.0f);
        }
        ImGui::End();
        return;
    }

    float scale = (float)winH / 720.0f;
    if (scale < 0.5f) scale = 0.5f;

    /* -------------------------------------------------------------------
     * Phase 1: DIFFING — show initial prompt if components are missing
     * ------------------------------------------------------------------- */
    if (st.state == DISTRIB_CSTATE_DIFFING) {
        if (st.missing_count > 0 && !s_PromptShown) {
            /* Show modal-ish prompt centred on screen */
            float promptW = floorf(420.0f * scale);
            float promptH = floorf(140.0f * scale);
            float px = ((float)winW - promptW) * 0.5f;
            float py = ((float)winH - promptH) * 0.5f;

            ImGui::SetNextWindowPos(ImVec2(px, py), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(promptW, promptH), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.92f);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("##distrib_prompt", nullptr, flags)) {
                float fs = floorf(14.0f * scale);
                ImGui::SetWindowFontScale(fs / ImGui::GetFontSize());

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                                   "  SERVER MOD COMPONENTS");
                ImGui::Separator();
                ImGui::Spacing();

                char msg[128];
                snprintf(msg, sizeof(msg),
                         "  This server requires %d mod component%s not installed locally.",
                         st.missing_count, st.missing_count == 1 ? "" : "s");
                ImGui::TextUnformatted(msg);
                ImGui::Spacing();

                float btnW = floorf(120.0f * scale);
                float btnH = floorf(22.0f * scale);

                if (ImGui::Button("Download", ImVec2(btnW, btnH))) {
                    netDistribClientSetTemporary(0);
                    s_PromptChoice = 0;
                    s_PromptShown  = true;
                    s_PromptDone   = true;
                }
                ImGui::SameLine(0.0f, floorf(6.0f * scale));
                if (ImGui::Button("This Session", ImVec2(btnW, btnH))) {
                    netDistribClientSetTemporary(1);
                    s_PromptChoice = 1;
                    s_PromptShown  = true;
                    s_PromptDone   = true;
                }
                ImGui::SameLine(0.0f, floorf(6.0f * scale));
                if (ImGui::Button("Skip", ImVec2(btnW * 0.6f, btnH))) {
                    s_PromptChoice = 2;
                    s_PromptShown  = true;
                    s_PromptDone   = true;
                    /* Caller will send empty diff on next tick */
                }

                ImGui::SetWindowFontScale(1.0f);
            }
            ImGui::End();
        } else {
            /* No missing components or prompt already handled — show spinner */
            float barH = floorf(30.0f * scale);
            ImGui::SetNextWindowPos(ImVec2(0.0f, (float)winH - barH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2((float)winW, barH), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.75f);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("##distrib_diffing", nullptr, flags)) {
                float fs = floorf(13.0f * scale);
                ImGui::SetWindowFontScale(fs / ImGui::GetFontSize());
                ImGui::SetCursorPosY((barH - fs) * 0.5f - 2.0f);
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
                                   "  Comparing mod catalog with server...");
                ImGui::SetWindowFontScale(1.0f);
            }
            ImGui::End();
        }
        return;
    }

    /* -------------------------------------------------------------------
     * Phase 2: RECEIVING — progress bar at bottom of screen
     * ------------------------------------------------------------------- */
    if (st.state == DISTRIB_CSTATE_RECEIVING) {
        float barH = floorf(50.0f * scale);
        ImGui::SetNextWindowPos(ImVec2(0.0f, (float)winH - barH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)winW, barH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.80f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("##distrib_recv", nullptr, flags)) {
            float fs = floorf(13.0f * scale);
            ImGui::SetWindowFontScale(fs / ImGui::GetFontSize());

            float padX = floorf(8.0f * scale);
            float barW = (float)winW - padX * 2.0f;

            /* Top line: component name + counter */
            {
                char left[128], right[64];
                snprintf(left, sizeof(left), "  Downloading: %s", st.current_id);
                snprintf(right, sizeof(right), "[%d/%d]",
                         st.received_count + 1, st.missing_count);
                ImGui::SetCursorPos(ImVec2(padX, floorf(4.0f * scale)));
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", left);
                /* Right-align the counter */
                float rw = ImGui::CalcTextSize(right).x;
                ImGui::SameLine((float)winW - rw - padX * 2.0f);
                ImGui::TextUnformatted(right);
            }

            /* Progress bar */
            float frac = (st.current_bytes_total > 0)
                ? (float)st.current_bytes_received / (float)st.current_bytes_total
                : 0.0f;
            if (frac > 1.0f) frac = 1.0f;

            char brecv[32], btotal[32];
            fmtBytes(st.current_bytes_received, brecv, sizeof(brecv));
            fmtBytes(st.current_bytes_total,    btotal, sizeof(btotal));

            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%s / %s", brecv, btotal);

            ImGui::SetCursorPos(ImVec2(padX, floorf(22.0f * scale)));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
            ImGui::ProgressBar(frac, ImVec2(barW, floorf(16.0f * scale)), overlay);
            ImGui::PopStyleColor();

            ImGui::SetWindowFontScale(1.0f);
        }
        ImGui::End();
    }
}
