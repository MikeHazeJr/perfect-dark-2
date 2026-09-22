/* T-MENUS-004: actual ImGui + production modal/input bridge behavior.
 * Presentation/audio and action-map edges are narrow test adapters below;
 * focus, input queuing, buttons, popup lifetime and decisions are production.
 */
#include "catch.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "pdgui_audio.h"
#include "pdgui_glyphs.h"
#include "pdgui_layout.h"
#include "pdgui_nav.h"
#include "pdgui_nav_input.h"
#include "pdgui_style.h"

#include <cstdio>
#include <vector>

namespace {
bool s_AcceptEdge;
bool s_CancelEdge;
int s_Palette;
std::vector<InputAction> s_GlyphRequests;

struct ModalInput {
    bool keyboardAccept = false;
    bool keyboardRight = false;
    bool keyboardDown = false;
    bool controllerAccept = false;
    bool controllerRight = false;
    bool controllerDown = false;
    bool cancel = false;
    bool mouseDown = false;
    ImVec2 mouse = ImVec2(-100.0f, -100.0f);
};

class ConfirmHarness {
public:
    ConfirmHarness() : previous(ImGui::GetCurrentContext())
    {
        context = ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1920.0f, 1080.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard |
                          ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        unsigned char *pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        s_AcceptEdge = s_CancelEdge = false;
        s_Palette = 1;
        s_GlyphRequests.clear();
    }

    ~ConfirmHarness()
    {
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous);
        s_AcceptEdge = s_CancelEdge = false;
    }

    int frame(const ModalInput &input = ModalInput(), bool open = false)
    {
        beginFrame(input);
        ImGui::Begin("Confirmation test parent", nullptr,
                     ImGuiWindowFlags_NoSavedSettings);
        if (open) {
            ImGui::OpenPopup(popupId);
            openFrame = ImGui::GetFrameCount();
        }
        const int result = pdguiRenderConfirmModal(popupId, "Delete item?",
            "Delete the selected item?", "Delete", &openFrame);
        ImGui::End();
        ImGui::Render();
        return result;
    }

    int actionBarFrame(const ModalInput &input = ModalInput(),
                       bool focusBody = false, bool open = false)
    {
        beginFrame(input);
        ImGui::SetNextWindowSize(ImVec2(700.0f, 500.0f));
        ImGui::Begin("Action bar test parent", nullptr,
                     ImGuiWindowFlags_NoSavedSettings);
        if (focusBody) {
            // Model an already navigated body control for this isolation test.
            ImGui::SetKeyboardFocusHere();
            ImGui::SetNavCursorVisible(true);
        }
        ImGui::Checkbox("Body option", &bodyValue);
        if (pdguiBeginActionBar("##behavior_action_bar")) {
            if (pdguiActionBarButton("Primary", 1, 150.0f)) ++firstActivations;
            ImGui::SameLine();
            if (pdguiActionBarButton("Back", 0, 150.0f)) ++secondActivations;
        }
        pdguiEndActionBar();
        if (open) {
            ImGui::OpenPopup(popupId);
            openFrame = ImGui::GetFrameCount();
        }
        const int result = pdguiRenderConfirmModal(popupId, "Delete item?",
            "Delete the selected item?", "Delete", &openFrame);
        ImGui::End();
        ImGui::Render();
        return result;
    }

    void openSettled()
    {
        REQUIRE(frame(ModalInput(), true) == PDGUI_CONFIRM_PENDING);
        for (int i = 0; i < 7; ++i)
            REQUIRE(frame() == PDGUI_CONFIRM_PENDING);
        REQUIRE(context->NavWindow != nullptr);
        REQUIRE(context->NavId == context->NavWindow->GetID("Cancel##pdgui_confirm_cancel"));
        REQUIRE(context->NavCursorVisible);
    }

    void focusConfirm(bool controller)
    {
        ModalInput right;
        right.controllerRight = controller;
        right.keyboardRight = !controller;
        REQUIRE(frame(right) == PDGUI_CONFIRM_PENDING);
        REQUIRE(frame() == PDGUI_CONFIRM_PENDING);
        REQUIRE(context->NavWindow != nullptr);
        REQUIRE(context->NavId == context->NavWindow->GetID("Delete##pdgui_confirm_ok"));
    }

    ImVec2 focusedCenter() const
    {
        const ImGuiWindow *window = context->NavWindow;
        REQUIRE(window != nullptr);
        const ImRect rect = window->NavRectRel[ImGuiNavLayer_Main];
        return ImVec2(window->Pos.x + (rect.Min.x + rect.Max.x) * 0.5f,
                      window->Pos.y + (rect.Min.y + rect.Max.y) * 0.5f);
    }

    s32 openFrame = -1;
    bool bodyValue = false;
    int firstActivations = 0;
    int secondActivations = 0;

private:
    void beginFrame(const ModalInput &input)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Enter, input.keyboardAccept);
        io.AddKeyEvent(ImGuiKey_RightArrow, input.keyboardRight);
        io.AddKeyEvent(ImGuiKey_DownArrow, input.keyboardDown);
        io.AddMousePosEvent(input.mouse.x, input.mouse.y);
        io.AddMouseButtonEvent(0, input.mouseDown);
        PdguiNavInput nav{};
        nav.active = true;
        nav.accept = input.controllerAccept;
        nav.cancel = input.cancel;
        nav.right = input.controllerRight;
        nav.down = input.controllerDown;
        pdguiSubmitNavInput(nav);
        s_AcceptEdge = input.keyboardAccept || input.controllerAccept;
        s_CancelEdge = input.cancel;
        ImGui::NewFrame();
    }

    const char *popupId = "##confirmation_behavior_test";
    ImGuiContext *previous;
    ImGuiContext *context;
};
} // namespace

extern "C" {
float videoGetUiScaleMult(void) { return 1.0f; }
void pdguiDrawPdDialog(float, float, float, float, const char *, s32) {}
void pdguiDrawItemHighlight(float, float, float, float) {}
void pdguiDrawButtonEdgeGlow(f32, f32, f32, f32, s32) {}
void pdguiDrawTextGlow(f32, f32, f32, f32) {}
void pdguiSetCursorBelowTitle(float titleHeight)
{
    ImGui::SetCursorPosY(titleHeight + ImGui::GetStyle().WindowPadding.y);
}
void pdguiSetPalette(s32 index) { s_Palette = index; }
s32 pdguiGetPalette(void) { return s_Palette; }
u32 pdguiPalImU32(s32, s32 alpha) { return IM_COL32(25, 25, 25, alpha); }
void pdguiPlaySound(int) {}
s32 pdguiMenuAcceptPressed(void) { return s_AcceptEdge ? 1 : 0; }
s32 pdguiMenuCancelPressed(void) { return s_CancelEdge ? 1 : 0; }
s32 pdguiGlyphGetActionLabel(InputAction action, char *out, s32 capacity)
{
    s_GlyphRequests.push_back(action);
    return std::snprintf(out, static_cast<size_t>(capacity), "%s",
        action == ACTION_MENU_ACCEPT ? "BoundAccept" : "BoundCancel");
}
}

TEST_CASE("confirmation safe default accepts Cancel through keyboard and controller",
          "[input][menus][confirmation][imgui][t-menus-004]")
{
    for (bool controller : {false, true}) {
        CAPTURE(controller);
        ConfirmHarness ui;
        ui.openSettled();
        ModalInput accept;
        accept.controllerAccept = controller;
        accept.keyboardAccept = !controller;
        REQUIRE(ui.frame(accept) == PDGUI_CONFIRM_CANCEL);
        REQUIRE(ui.openFrame == -1);
        REQUIRE(s_Palette == 1);
        REQUIRE(s_GlyphRequests.size() >= 2);
        REQUIRE(s_GlyphRequests[s_GlyphRequests.size() - 2] == ACTION_MENU_ACCEPT);
        REQUIRE(s_GlyphRequests.back() == ACTION_MENU_CANCEL);
    }
}

TEST_CASE("confirmation requires navigation to the destructive button",
          "[input][menus][confirmation][imgui][t-menus-004]")
{
    for (bool controller : {false, true}) {
        CAPTURE(controller);
        ConfirmHarness ui;
        ui.openSettled();
        ui.focusConfirm(controller);
        ModalInput accept;
        accept.controllerAccept = controller;
        accept.keyboardAccept = !controller;
        REQUIRE(ui.frame(accept) == PDGUI_CONFIRM_OK);
        REQUIRE(ui.openFrame == -1);
    }
}

TEST_CASE("confirmation opening debounce and simultaneous Back remain safe",
          "[input][menus][confirmation][imgui][t-menus-004]")
{
    SECTION("opening activation and Back cannot dismiss the new modal") {
        ConfirmHarness ui;
        REQUIRE(ui.frame(ModalInput(), true) == PDGUI_CONFIRM_PENDING);
        ModalInput accept;
        accept.controllerAccept = true;
        accept.cancel = true;
        REQUIRE(ui.frame(accept) == PDGUI_CONFIRM_PENDING);
        REQUIRE(ui.openFrame >= 0);
        for (int i = 0; i < 7; ++i)
            REQUIRE(ui.frame() == PDGUI_CONFIRM_PENDING);
        ModalInput back;
        back.cancel = true;
        REQUIRE(ui.frame(back) == PDGUI_CONFIRM_CANCEL);
    }
    SECTION("Back outranks an activation on the destructive button") {
        ConfirmHarness ui;
        ui.openSettled();
        ui.focusConfirm(true);
        ModalInput simultaneous;
        simultaneous.controllerAccept = true;
        simultaneous.cancel = true;
        REQUIRE(ui.frame(simultaneous) == PDGUI_CONFIRM_CANCEL);
        REQUIRE(ui.openFrame == -1);
    }
}

TEST_CASE("confirmation mouse activation follows the clicked button",
          "[input][menus][confirmation][imgui][t-menus-004]")
{
    for (bool confirm : {false, true}) {
        CAPTURE(confirm);
        ConfirmHarness ui;
        ui.openSettled();
        if (confirm) ui.focusConfirm(false);
        ModalInput pointer;
        pointer.mouse = ui.focusedCenter();
        REQUIRE(ui.frame(pointer) == PDGUI_CONFIRM_PENDING);
        pointer.mouseDown = true;
        REQUIRE(ui.frame(pointer) == PDGUI_CONFIRM_PENDING);
        pointer.mouseDown = false;
        REQUIRE(ui.frame(pointer) == (confirm ? PDGUI_CONFIRM_OK : PDGUI_CONFIRM_CANCEL));
    }
}

TEST_CASE("action bar legacy focus hints cannot activate another control",
          "[input][menus][confirmation][imgui][t-menus-004]")
{
    for (bool controller : {false, true}) {
        CAPTURE(controller);
        ConfirmHarness ui;
        REQUIRE(ui.actionBarFrame(ModalInput(), true) == PDGUI_CONFIRM_PENDING);
        for (int i = 0; i < 3; ++i)
            REQUIRE(ui.actionBarFrame() == PDGUI_CONFIRM_PENDING);

        ModalInput accept;
        accept.controllerAccept = controller;
        accept.keyboardAccept = !controller;
        REQUIRE(ui.actionBarFrame(accept) == PDGUI_CONFIRM_PENDING);
        REQUIRE(ui.bodyValue);
        REQUIRE(ui.firstActivations == 0);
        REQUIRE(ui.secondActivations == 0);
        REQUIRE(ui.actionBarFrame() == PDGUI_CONFIRM_PENDING);

        ModalInput down;
        down.controllerDown = controller;
        down.keyboardDown = !controller;
        REQUIRE(ui.actionBarFrame(down) == PDGUI_CONFIRM_PENDING);
        REQUIRE(ui.actionBarFrame() == PDGUI_CONFIRM_PENDING);
        ModalInput right;
        right.controllerRight = controller;
        right.keyboardRight = !controller;
        REQUIRE(ui.actionBarFrame(right) == PDGUI_CONFIRM_PENDING);
        REQUIRE(ui.actionBarFrame() == PDGUI_CONFIRM_PENDING);
        REQUIRE(ui.actionBarFrame(accept) == PDGUI_CONFIRM_PENDING);
        REQUIRE(ui.firstActivations == 0);
        REQUIRE(ui.secondActivations == 1);
    }
}

TEST_CASE("a popup owns Accept over the background action bar",
          "[input][menus][confirmation][imgui][t-menus-004]")
{
    ConfirmHarness ui;
    REQUIRE(ui.actionBarFrame(ModalInput(), true) == PDGUI_CONFIRM_PENDING);
    REQUIRE(ui.actionBarFrame(ModalInput(), false, true) == PDGUI_CONFIRM_PENDING);
    for (int i = 0; i < 7; ++i)
        REQUIRE(ui.actionBarFrame() == PDGUI_CONFIRM_PENDING);
    ModalInput accept;
    accept.controllerAccept = true;
    REQUIRE(ui.actionBarFrame(accept) == PDGUI_CONFIRM_CANCEL);
    REQUIRE(ui.firstActivations == 0);
    REQUIRE(ui.secondActivations == 0);
}
