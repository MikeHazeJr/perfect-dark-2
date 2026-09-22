#include "pdgui_nav_input.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <array>

namespace {
ImGuiContext *s_OwnerContext = nullptr;
int s_OwnerFrame = -1;
ImGuiID s_PopupOwner = 0;
ImGuiID s_WidgetOwner = 0;
unsigned int s_WidgetNavDirections = 0;
bool s_WidgetOwnsDelete = false;
bool s_WidgetOwnsAllKeys = false;
ImGuiContext *s_SuppressedContext = nullptr;
int s_SuppressedFrame = -1;

std::array<ImGuiKey, 5> openingAcceptKeys()
{
    // ImGui's navigation aliases resolve against the current context's config.
    ImGuiContext &g = *GImGui;
    return {ImGuiKey_NavGamepadActivate, ImGuiKey_NavGamepadInput,
            ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space};
}

std::array<ImGuiKey, 2> openingCancelKeys()
{
    ImGuiContext &g = *GImGui;
    return {ImGuiKey_NavGamepadCancel, ImGuiKey_Escape};
}

bool openingKeyLocked(ImGuiKey key)
{
    const ImGuiKeyOwnerData *owner = ImGui::GetKeyOwnerData(GImGui, key);
    return owner->LockUntilRelease && owner->OwnerCurr == ImGuiKeyOwner_Any;
}

bool openingActionLocked(InputAction action)
{
    if (action == ACTION_MENU_ACCEPT) {
        for (ImGuiKey key : openingAcceptKeys())
            if (openingKeyLocked(key)) return true;
    } else if (action == ACTION_MENU_CANCEL) {
        for (ImGuiKey key : openingCancelKeys())
            if (openingKeyLocked(key)) return true;
    } else if (action == ACTION_PAUSE) {
        return openingKeyLocked(ImGuiKey_GamepadStart);
    }
    return false;
}

void lockHeldOpeningKey(ImGuiKey key)
{
    if (ImGui::GetKeyData(key)->Down)
        ImGui::SetKeyOwner(key, ImGuiKeyOwner_Any, ImGuiInputFlags_LockUntilRelease);
}

bool capturedPopupRetained(const ImGuiContext &g)
{
    if (!s_PopupOwner) return true;
    for (const ImGuiPopupData &popup : g.OpenPopupStack)
        if (popup.PopupId == s_PopupOwner) return true;
    return false;
}

bool capturedWidgetOwnsAction(InputAction action)
{
    if (!s_WidgetOwner) return false;
    if (s_WidgetOwnsAllKeys) return true;
    switch (action) {
    case ACTION_MENU_ACCEPT:
    case ACTION_MENU_CANCEL:
        return true;
    case ACTION_MENU_LEFT:
        return (s_WidgetNavDirections & (1u << ImGuiDir_Left)) != 0;
    case ACTION_MENU_RIGHT:
        return (s_WidgetNavDirections & (1u << ImGuiDir_Right)) != 0;
    case ACTION_MENU_UP:
        return (s_WidgetNavDirections & (1u << ImGuiDir_Up)) != 0;
    case ACTION_MENU_DOWN:
        return (s_WidgetNavDirections & (1u << ImGuiDir_Down)) != 0;
    case ACTION_MENU_DELETE:
        return s_WidgetOwnsDelete;
    default:
        // An active value editor does not own unrelated higher-level commands.
        return false;
    }
}
}

void pdguiNavCaptureOwners()
{
    ImGuiContext &g = *GImGui;
    s_OwnerContext = &g;
    s_OwnerFrame = g.FrameCount + 1;
    s_PopupOwner = g.OpenPopupStack.empty() ? 0 : g.OpenPopupStack.back().PopupId;
    s_WidgetOwner = g.ActiveId;
    s_WidgetNavDirections = g.ActiveIdUsingNavDirMask;
    s_WidgetOwnsDelete = s_WidgetOwner != 0 &&
        ImGui::GetKeyOwner(ImGuiKey_Delete) == s_WidgetOwner;
    s_WidgetOwnsAllKeys = g.ActiveIdUsingAllKeyboardKeys;
    s_SuppressedContext = nullptr;
    s_SuppressedFrame = -1;
}

void pdguiNavFinishOwners()
{
    ImGuiContext *context = ImGui::GetCurrentContext();
    if (!context || context != s_OwnerContext || context->FrameCount != s_OwnerFrame)
        return;
    // NewFrame processes native Cancel before generating activation. Prevent
    // simultaneous Back+Accept from closing then reopening a combo or slider.
    // Without queued activation, retain action-specific ownership below so
    // ordinary focus movement does not swallow unrelated higher-level commands.
    const bool activationQueued = context->NavActivateId != 0 ||
        context->NavActivateDownId != 0 || context->NavActivatePressedId != 0;
    const bool ownerConsumed =
        (s_WidgetOwner && context->ActiveId != s_WidgetOwner) ||
        !capturedPopupRetained(*context);
    if (activationQueued && ownerConsumed) pdguiNavSuppressActivation();
}

bool pdguiNavActionAllowed(InputAction action)
{
    ImGuiContext *context = ImGui::GetCurrentContext();
    if (!context) return true;
    const ImGuiContext &g = *context;
    if (openingActionLocked(action)) return false;
    if (s_SuppressedContext == context && s_SuppressedFrame == g.FrameCount)
        return false;
    if (s_OwnerContext == context && s_OwnerFrame == g.FrameCount) {
        // Only the widget's native actions stay owned through completion.
        if (capturedWidgetOwnsAction(action)) return false;
        if (!capturedPopupRetained(g)) return false;
    }
    if (!g.OpenPopupStack.empty()) {
        const ImGuiID owner = g.OpenPopupStack.back().PopupId;
        for (const ImGuiPopupData &popup : g.BeginPopupStack)
            if (popup.PopupId == owner) return true;
        return false;
    }
    return true;
}

void pdguiNavSuppressActivation()
{
    ImGuiContext &g = *GImGui;
    s_SuppressedContext = &g;
    s_SuppressedFrame = g.FrameCount;
    g.NavActivateId = 0;
    g.NavActivateDownId = 0;
    g.NavActivatePressedId = 0;
}

void pdguiNavSuppressOpeningGesture()
{
    pdguiNavSuppressActivation();
    for (ImGuiKey key : openingAcceptKeys()) lockHeldOpeningKey(key);
    for (ImGuiKey key : openingCancelKeys()) lockHeldOpeningKey(key);
    lockHeldOpeningKey(ImGuiKey_GamepadStart);
}

void pdguiSubmitNavInput(const PdguiNavInput &input)
{
    ImGuiIO &io = ImGui::GetIO();
    /* The action map supplies this virtual navigation device even when the
     * selected binding is a keyboard key. The SDL gamepad poller is disabled. */
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    /* Activate gives sliders directional tweaking and standard button repeat.
     * The local InputTextEx policy also accepts gamepad Activate for editing;
     * physical Enter keeps ImGui's numeric PreferInput behavior. */
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, input.active && input.accept);
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, input.active && input.cancel);
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, input.active && input.up);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, input.active && input.down);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, input.active && input.left);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, input.active && input.right);
}
