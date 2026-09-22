#include "catch.hpp"
#include "pdgui_nav_input.h"
#include "../port/fast3d/imgui/imgui.h"

#include <string>

namespace {
struct TextNavigationContext {
    char text[64] = "";
    bool nameFocused = false;
    bool nameActive = false;

    TextNavigationContext()
    {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    ~TextNavigationContext() { ImGui::DestroyContext(); }

    void frame(bool accept = false, bool cancel = false, bool down = false)
    {
        pdguiSubmitNavInput({true, accept, cancel, false, down, false, false});
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(500, 400));
        ImGui::Begin("Settings", nullptr,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::Button("Before");
        ImGui::SetItemDefaultFocus();
        ImGui::InputText("Name", text, sizeof(text));
        nameFocused = ImGui::IsItemFocused();
        nameActive = ImGui::IsItemActive();
        ImGui::Button("After");
        ImGui::End();
        ImGui::EndFrame();
    }

    void navigateToName()
    {
        // Initialize default navigation on Before, then move with the same
        // mapped direction API as the client. No direct text activation.
        frame();
        frame();
        frame(false, false, true);
        frame();
        REQUIRE(nameFocused);
        REQUIRE_FALSE(nameActive);
    }
};
}

TEST_CASE("Mapped accept activates a navigated text field and preserves keyboard editing",
          "[input][menus][settings][t-menus-003][imgui][text]")
{
    TextNavigationContext ctx;
    ctx.navigateToName();
    ctx.frame(true);
    REQUIRE(ctx.nameActive);
    ctx.frame(); // release accept before typing
    REQUIRE(ctx.nameActive);

    ImGuiIO &io = ImGui::GetIO();
    io.AddInputCharactersUTF8("abc");
    ctx.frame();
    REQUIRE(std::string(ctx.text) == "abc");

    io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    ctx.frame();
    io.AddKeyEvent(ImGuiKey_LeftArrow, false);
    ctx.frame();
    io.AddInputCharactersUTF8("Z");
    ctx.frame();
    REQUIRE(std::string(ctx.text) == "abZc");
    REQUIRE(ctx.nameActive);

    SECTION("keyboard cancel reverts text and retires editing") {
        io.AddKeyEvent(ImGuiKey_Escape, true);
        ctx.frame();
        REQUIRE_FALSE(ctx.nameActive);
        REQUIRE(std::string(ctx.text).empty());
        io.AddKeyEvent(ImGuiKey_Escape, false);
        ctx.frame();
    }
    SECTION("controller cancel reverts text and retires editing") {
        ctx.frame(false, true);
        REQUIRE_FALSE(ctx.nameActive);
        REQUIRE(std::string(ctx.text).empty());
        ctx.frame();
    }
    REQUIRE_FALSE(ctx.nameActive);
}
