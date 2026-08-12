#include <PR/ultratypes.h>
#include <stdio.h>

#include "imgui/imgui.h"
#include "inputctx.h"
#include "menupool.h"
#include "net/netdistrib.h"
#include "pdgui_crash_recovery.h"
#include "pdgui_scaling.h"
#include "system.h"

namespace {
static bool s_OpenRequested;
static bool s_LayoutLogged;
static bool s_PointerLogged;
static char s_Error[192];

static void applyRecovery(s32 action)
{
    if (netCrashRecoveryApply(action)) {
        sysLogPrintf(LOG_NOTE,
            "DISTRIB.RECOVERY.UI.PASS: action=%d", action);
        s_Error[0] = '\0';
        s_OpenRequested = false;
        s_LayoutLogged = false;
        s_PointerLogged = false;
        menupoolRelease(MENU_TYPE_CRASH_RECOVERY);
        ImGui::CloseCurrentPopup();
        return;
    }
    snprintf(s_Error, sizeof(s_Error),
        "Recovery could not complete safely. The temporary files remain quarantined; see the log for the rejected path.");
    sysLogPrintf(LOG_WARNING,
        "DISTRIB.RECOVERY.UI: action=%d failed; decision remains pending", action);
}
}

extern "C" s32 pdguiCrashRecoveryIsActive(void)
{
    return netCrashRecoveryPending();
}

extern "C" void pdguiCrashRecoveryRender(s32 width, s32 height)
{
    crash_recovery_state_t state;
    if (!netCrashRecoveryGetPending(&state)) {
        s_OpenRequested = false;
        s_LayoutLogged = false;
        s_PointerLogged = false;
        s_Error[0] = '\0';
        menupoolRelease(MENU_TYPE_CRASH_RECOVERY);
        return;
    }

    menupoolAcquire(MENU_TYPE_CRASH_RECOVERY, NULL, &g_CtxImGuiMenu);

    const char *popup = "Temporary Content Recovery##pd2_recovery";
    if (!s_OpenRequested || !ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None)) {
        ImGui::OpenPopup(popup);
        s_OpenRequested = true;
        s_LayoutLogged = false;
        s_PointerLogged = false;
    }

    const float scale = pdguiScaleFactor();
    const float modalW = 660.0f * scale;
    const float modalH = 330.0f * scale;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetWorkCenter(),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(modalW, modalH), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal(popup, NULL,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::SetWindowPos(ImVec2(
        viewport->GetWorkCenter().x - modalW * 0.5f,
        viewport->GetWorkCenter().y - modalH * 0.5f), ImGuiCond_Always);

    ImGui::TextWrapped(
        "Perfect Dark closed before its temporary network content was cleaned up. "
        "The files are quarantined and have not been loaded.");
    ImGui::Spacing();
    ImGui::Text("Temporary groups: %d", state.temp_component_count);
    ImGui::Text("Interrupted launches: %d", state.crash_count);
    if (state.suspect_id[0]) ImGui::Text("Last component: %s", state.suspect_id);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped(
        "Keep and Load verifies every received public source before re-admitting it. "
        "Keep Disabled preserves the files without registering them. Discard retires "
        "all temporary runtime state before removing the quarantined tree.");
    if (s_Error[0]) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", s_Error);
    }

    ImGui::SetCursorPosY(modalH - 58.0f * scale);
    const float buttonW = 190.0f * scale;
    const bool keepClicked = ImGui::Button("Keep and Load", ImVec2(buttonW, 36.0f * scale));
    const bool keepHovered = ImGui::IsItemHovered();
    const ImVec2 keepMin = ImGui::GetItemRectMin();
    const ImVec2 keepMax = ImGui::GetItemRectMax();
    ImGui::SameLine();
    const bool disableClicked = ImGui::Button("Keep Disabled", ImVec2(buttonW, 36.0f * scale));
    const bool disableHovered = ImGui::IsItemHovered();
    const ImVec2 disableMin = ImGui::GetItemRectMin();
    const ImVec2 disableMax = ImGui::GetItemRectMax();
    ImGui::SameLine();
    const bool discardClicked = ImGui::Button("Discard", ImVec2(buttonW, 36.0f * scale));
    const bool discardHovered = ImGui::IsItemHovered();
    const ImVec2 discardMin = ImGui::GetItemRectMin();
    const ImVec2 discardMax = ImGui::GetItemRectMax();

    if (!s_LayoutLogged) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 windowPos = ImGui::GetWindowPos();
        sysLogPrintf(LOG_NOTE,
            "DISTRIB.RECOVERY.UI.LAYOUT: framebuffer=%dx%d viewport=(%.1f,%.1f)+(%.1f,%.1f) window=(%.1f,%.1f) scale=%.3f mouse=(%.1f,%.1f) keep=(%.1f,%.1f)-(%.1f,%.1f) disable=(%.1f,%.1f)-(%.1f,%.1f) discard=(%.1f,%.1f)-(%.1f,%.1f)",
            width, height, viewport->WorkPos.x, viewport->WorkPos.y,
            viewport->WorkSize.x, viewport->WorkSize.y, windowPos.x, windowPos.y,
            scale, mouse.x, mouse.y,
            keepMin.x, keepMin.y, keepMax.x, keepMax.y,
            disableMin.x, disableMin.y, disableMax.x, disableMax.y,
            discardMin.x, discardMin.y, discardMax.x, discardMax.y);
        s_LayoutLogged = true;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if ((!s_PointerLogged && mouse.x >= 0.0f && mouse.y >= 0.0f)
            || mouseClicked || mouseReleased) {
        sysLogPrintf(LOG_NOTE,
            "DISTRIB.RECOVERY.UI.INPUT: mouse=(%.1f,%.1f) down=%d clicked=%d released=%d hovered=%d/%d/%d window_hovered=%d",
            mouse.x, mouse.y, ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 1 : 0,
            mouseClicked ? 1 : 0, mouseReleased ? 1 : 0,
            keepHovered ? 1 : 0, disableHovered ? 1 : 0, discardHovered ? 1 : 0,
            ImGui::IsWindowHovered() ? 1 : 0);
        s_PointerLogged = true;
    }

    if (keepClicked) applyRecovery(0);
    else if (disableClicked) applyRecovery(1);
    else if (discardClicked) applyRecovery(2);
    ImGui::EndPopup();
}
