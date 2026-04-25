/**
 * pdgui_menu_stack_debug.cpp -- Read-only overlay: input context stack, menu pool,
 * legacy dialog chain. Does not push input contexts or capture ImGui focus
 * (NoInputs | NoNav | NoBringToFrontOnFocus).
 */

#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "inputctx.h"
#include "menupool.h"
#include "pdgui_menu_stack_debug.h"
#include "pdgui_scaling.h"

extern "C" {
void pdguiDebugFormatLegacyMenuInfo(char *buf, size_t bufSz);
}

static bool s_MenuStackOverlayOpen = false;

void pdguiMenuStackOverlayToggle(void)
{
    s_MenuStackOverlayOpen = !s_MenuStackOverlayOpen;
}

s32 pdguiMenuStackOverlayGetOpen(void)
{
    return s_MenuStackOverlayOpen ? 1 : 0;
}

void pdguiMenuStackOverlayRender(s32 winW, s32 winH)
{
    (void)winW;
    (void)winH;
    if (!s_MenuStackOverlayOpen) {
        return;
    }

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs
                        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::SetNextWindowPos(ImVec2(pdguiScale(12.0f), pdguiScale(48.0f)), ImGuiCond_FirstUseEver);
    bool open = s_MenuStackOverlayOpen;
    if (ImGui::Begin("Menu / input diagnostics (F9)##pdgui_menu_stack_ov", &open, wf)) {
        ImGui::TextDisabled("Read-only snapshot. Does not push input contexts.");
        ImGui::Separator();

        InputCtxDebugAuthority auth;
        inputCtxDebugSnapshotAuthority(&auth);
        ImGui::Text("Input authority");
        ImGui::BulletText("Physical stack depth: %d", (int)auth.stack_depth);
        ImGui::BulletText("Effective top: \"%s\"%s", auth.effective_top_name,
                          auth.effective_top_is_gameplay ? " (gameplay)" : "");
        ImGui::BulletText("Gameplay would suppress: %s", auth.gameplay_would_suppress ? "yes" : "no");
        ImGui::BulletText("Window focus lost: %s", auth.window_focus_lost ? "yes" : "no");
        ImGui::BulletText("Focus settle remaining: %u ms", (unsigned)auth.focus_settle_remaining_ms);

        ImGui::Separator();
        ImGui::Text("Input context stack (bottom -> top)");
        InputCtxDebugEntry st[INPUTCTX_MAX_STACK];
        s32 n = inputCtxDebugCopyStack(st, INPUTCTX_MAX_STACK);
        for (s32 i = 0; i < n; i++) {
            ImGui::BulletText("[%d] %s%s", (int)i, st[i].name ? st[i].name : "?",
                              st[i].marked_for_removal ? " [marked defer-pop]" : "");
        }
        if (n <= 0) {
            ImGui::TextDisabled("(empty)");
        }

        ImGui::Separator();
        ImGui::Text("Menu pool (active types)");
        MenupoolDebugEntry pool[64];
        s32 pn = menupoolDebugCopyActive(pool, 64);
        if (pn == 0) {
            ImGui::TextDisabled("(no active pool slots)");
        }
        for (s32 i = 0; i < pn; i++) {
            ImGui::BulletText("%s gen=%u ctx=%s def=%p", pool[i].type_name,
                              (unsigned)pool[i].generation, pool[i].owned_ctx_name,
                              (const void *)pool[i].def_ptr);
        }

        ImGui::Separator();
        ImGui::Text("Legacy menu stack (g_Menus)");
        char leg[4096];
        leg[0] = '\0';
        pdguiDebugFormatLegacyMenuInfo(leg, sizeof(leg));
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + pdguiScale(720.0f));
        ImGui::TextUnformatted(leg[0] ? leg : "(no data)");
        ImGui::PopTextWrapPos();

        /* Issue 11 (2026-04-24): push / pop history ring.  Last 8 ctx
         * transitions so quick push-then-pop (a class of input-routing
         * bugs like Mike's Issue 3 "pause menu input doesn't work") is
         * visible in real time.  Events:
         *   P -- push new ctx
         *   X -- resurrect (push on marked-for-removal entry)
         *   M -- mark for deferred pop (endFrame will collapse)
         *   R -- real pop (immediate or deferred cleanup) */
        ImGui::Separator();
        ImGui::Text("Ctx push/pop history (last 8, oldest first)");
        InputCtxDebugHistoryEntry hist[INPUTCTX_DEBUG_HISTORY_MAX];
        s32 hn = inputCtxDebugCopyHistory(hist, 8);
        if (hn <= 0) {
            ImGui::TextDisabled("(no transitions recorded yet)");
        } else {
            for (s32 i = 0; i < hn; i++) {
                ImGui::BulletText("t=%u  %c  %s  depth=%d",
                                  (unsigned)hist[i].timestamp_ms,
                                  hist[i].event,
                                  hist[i].name ? hist[i].name : "?",
                                  (int)hist[i].depth_after);
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("F9 close  |  F10 mesh debug  |  See input-authority ADR in context/designs/");
    }
    ImGui::End();
    s_MenuStackOverlayOpen = open;
}
