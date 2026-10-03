#include "pdgui_menu_readiness.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace {
ImGuiID observationKey(ImGuiWindow *window, const char *name)
{
    return ImHashStr(name, 0, window->ID);
}
}

void pdguiMenuRecordView(int view, int submitted_tab, unsigned int play_item,
    unsigned int settings_item, int blocked)
{
    ImGuiContext *context = ImGui::GetCurrentContext();
    if (!context || !context->WithinFrameScope || !context->CurrentWindow) return;
    ImGuiWindow *window = context->CurrentWindow;
    ImGuiStorage &storage = window->StateStorage;
    storage.SetInt(observationKey(window, "pdgui.readiness.frame"), context->FrameCount);
    storage.SetInt(observationKey(window, "pdgui.readiness.view"), view);
    storage.SetInt(observationKey(window, "pdgui.readiness.tab"), submitted_tab);
    storage.SetInt(observationKey(window, "pdgui.readiness.blocked"), blocked != 0);
    storage.SetInt(observationKey(window, "pdgui.readiness.play"), (int)play_item);
    storage.SetInt(observationKey(window, "pdgui.readiness.settings"), (int)settings_item);
}

int pdguiMenuViewReadiness(const char *window_name, int application_focused,
    pdgui_menu_view_readiness_t *out)
{
    if (!out) return 0;
    *out = {-1, -1, 0, 0};
    if (!pdguiMenuWindowOwnsInput(window_name, application_focused)) return 0;
    ImGuiContext &context = *ImGui::GetCurrentContext();
    ImGuiWindow *window = ImGui::FindWindowByName(window_name);
    ImGuiStorage &storage = window->StateStorage;
    if (context.WithinFrameScope || context.FrameCount != context.FrameCountRendered ||
        storage.GetInt(observationKey(window, "pdgui.readiness.frame"), -1) != context.FrameCountRendered ||
        storage.GetInt(observationKey(window, "pdgui.readiness.blocked"), 1) ||
        context.ActiveId != 0 || context.NavId == 0 || !context.NavCursorVisible)
        return 0;
    out->view = storage.GetInt(observationKey(window, "pdgui.readiness.view"), -1);
    out->submitted_tab = storage.GetInt(observationKey(window, "pdgui.readiness.tab"), -1);
    const ImGuiID play = (ImGuiID)storage.GetInt(observationKey(window, "pdgui.readiness.play"));
    const ImGuiID settings = (ImGuiID)storage.GetInt(observationKey(window, "pdgui.readiness.settings"));
    out->play_focused = play != 0 && context.NavId == play;
    out->settings_focused = settings != 0 && context.NavId == settings;
    return out->view >= 0;
}

pdgui_menu_input_observation_t pdguiMenuObserveInput(
    const char *window_name, int application_focused)
{
    pdgui_menu_input_observation_t observation{};
    ImGuiContext *context = ImGui::GetCurrentContext();
    if (!application_focused) observation.rejection_mask |= PDGUI_MENU_INPUT_FOCUS_LOST;
    if (!context) observation.rejection_mask |= PDGUI_MENU_INPUT_NO_CONTEXT;
    if (!window_name || !window_name[0]) observation.rejection_mask |= PDGUI_MENU_INPUT_NO_NAME;
    if (!context || !window_name || !window_name[0]) return observation;
    ImGuiWindow *window = ImGui::FindWindowByName(window_name);
    if (!window) {
        observation.rejection_mask |= PDGUI_MENU_INPUT_NO_WINDOW;
        return observation;
    }
    if (!window->Active) observation.rejection_mask |= PDGUI_MENU_INPUT_INACTIVE;
    if (window->Hidden) observation.rejection_mask |= PDGUI_MENU_INPUT_HIDDEN;
    if (window->Collapsed) observation.rejection_mask |= PDGUI_MENU_INPUT_COLLAPSED;
    if (window->SkipItems) observation.rejection_mask |= PDGUI_MENU_INPUT_SKIP_ITEMS;
    /* Pool acquisition or Begin() alone is not a completed visible frame.
     * Cached windows from a prior menu visit must fail this freshness check. */
    if (context->FrameCountRendered < 1 ||
        context->FrameCountRendered != context->FrameCountEnded ||
        window->LastFrameActive != context->FrameCountRendered)
        observation.rejection_mask |= PDGUI_MENU_INPUT_RENDER_STALE;
    if (context->OpenPopupStack.Size != 0) observation.rejection_mask |= PDGUI_MENU_INPUT_POPUP;
    if (context->NavWindowingTarget) observation.rejection_mask |= PDGUI_MENU_INPUT_WINDOW_SWITCH;
    if (!context->NavWindow) observation.rejection_mask |= PDGUI_MENU_INPUT_NO_NAV_WINDOW;
    /* Agent Select handles its clamped row index through mapped menu actions.
     * Its focused root owns input even before a widget NavId is initialized. */
    if (context->NavWindow && context->NavWindow->RootWindow != window->RootWindow)
        observation.rejection_mask |= PDGUI_MENU_INPUT_ROOT_MISMATCH;
    return observation;
}

int pdguiMenuWindowOwnsInput(const char *window_name, int application_focused)
{
    return pdguiMenuObserveInput(window_name, application_focused).rejection_mask == 0;
}
