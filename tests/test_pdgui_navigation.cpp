#include "catch.hpp"
#include "pdgui_nav_input.h"
#include "../port/fast3d/imgui/imgui.h"

namespace {
struct NavigationContext {
    NavigationContext() {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        unsigned char *pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~NavigationContext() { ImGui::DestroyContext(); }
    void frame(const PdguiNavInput &input) {
        pdguiSubmitNavInput(input);
        ImGui::NewFrame();
    }
    void end() { ImGui::EndFrame(); }
};
}

TEST_CASE("Mapped neutral navigation preserves held physical keyboard keys",
          "[input][menus][t-menus-003][imgui]")
{
    NavigationContext ctx;
    auto &io = ImGui::GetIO();
    for (auto key : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Enter, ImGuiKey_Escape}) {
        io.AddKeyEvent(key, true);
        ctx.frame({true, false, false, false, false, false, false});
        REQUIRE(ImGui::IsKeyDown(key));
        ctx.end();
        io.AddKeyEvent(key, false);
        ctx.frame({true, false, false, false, false, false, false});
        REQUIRE_FALSE(ImGui::IsKeyDown(key));
        ctx.end();
    }
}

TEST_CASE("Controller release and navigation teardown preserve simultaneous keyboard input",
          "[input][menus][t-menus-003][imgui]")
{
    NavigationContext ctx;
    auto &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_DownArrow, true);
    ctx.frame({true, true, true, false, true, false, false});
    REQUIRE(ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown));
    REQUIRE(ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
    ctx.end();
    ctx.frame({false, true, true, false, true, false, false});
    REQUIRE_FALSE(ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown));
    REQUIRE_FALSE(ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
    REQUIRE_FALSE(ImGui::IsKeyDown(ImGuiKey_GamepadFaceRight));
    REQUIRE(ImGui::IsKeyDown(ImGuiKey_DownArrow));
    ctx.end();
}

TEST_CASE("Text and binding capture own presses but always permit action releases",
          "[input][menus][t-menus-003]")
{
    REQUIRE(pdguiKeyboardActionAllowed(true, false, false, false));
    REQUIRE_FALSE(pdguiKeyboardActionAllowed(true, true, false, false));
    REQUIRE_FALSE(pdguiKeyboardActionAllowed(true, false, true, false));
    REQUIRE_FALSE(pdguiKeyboardActionAllowed(true, false, false, true));
    for (int mask = 0; mask < 8; ++mask)
        REQUIRE(pdguiKeyboardActionAllowed(false, mask & 1, mask & 2, mask & 4));
}

TEST_CASE("Middle mouse Back commits a click only after release and never after pan",
          "[input][menus][t-menus-003]")
{
    PdguiMouseBackGesture gesture;
    REQUIRE_FALSE(gesture.update(true, true, 10, 20, 6));
    REQUIRE_FALSE(gesture.update(true, true, 12, 21, 6));
    REQUIRE(gesture.update(true, false, 12, 21, 6));
    REQUIRE_FALSE(gesture.update(true, false, 12, 21, 6));
    REQUIRE_FALSE(gesture.update(true, true, 10, 20, 6));
    REQUIRE_FALSE(gesture.update(true, true, 30, 20, 6));
    REQUIRE_FALSE(gesture.update(true, false, 10, 20, 6));
    REQUIRE_FALSE(gesture.update(true, true, 10, 20, 6));
    REQUIRE_FALSE(gesture.update(false, true, 10, 20, 6));
    REQUIRE_FALSE(gesture.update(true, false, 10, 20, 6));
}

TEST_CASE("Captured presses remain owned through review until release or axis neutral",
          "[input][menus][t-menus-003]")
{
    PdguiCaptureLatch latch;
    REQUIRE(latch.block(true, 10, true));
    REQUIRE(latch.block(false, 10, true)); // repeat while review is visible
    REQUIRE_FALSE(latch.block(false, 20, true)); // independent fresh control
    REQUIRE(latch.block(false, 10, false));
    REQUIRE_FALSE(latch.block(false, 10, true)); // fresh press after release
    REQUIRE(latch.block(true, 30, true)); // deflected axis captured
    REQUIRE(latch.block(false, 30, true)); // changing deflection is same gesture
    REQUIRE(latch.block(false, 30, false)); // neutral retires capture
    REQUIRE_FALSE(latch.block(false, 30, true));
}

TEST_CASE("Capture lifecycle clears lost releases and retains other devices",
          "[input][menus][t-menus-003]")
{
    PdguiCaptureLatch latch;
    const uint64_t rightY = (uint64_t(5) << 56) | (uint64_t(17) << 16) | 3;
    const uint64_t otherButton = (uint64_t(3) << 56) | (uint64_t(18) << 16) | 1;
    REQUIRE(latch.block(true, rightY, true));
    REQUIRE(latch.ownsMenuScroll());
    REQUIRE(latch.block(true, otherButton, true));
    latch.removeDevice(17);
    REQUIRE_FALSE(latch.ownsMenuScroll());
    REQUIRE(latch.block(false, otherButton, true));
    latch.clear();
    REQUIRE_FALSE(latch.block(false, otherButton, true));
    REQUIRE(latch.block(true, rightY, true));
    REQUIRE(latch.block(false, rightY, false));
    REQUIRE_FALSE(latch.ownsMenuScroll());
}
