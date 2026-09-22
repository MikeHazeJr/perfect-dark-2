#include "catch.hpp"
#include "pdgui_menu_readiness.h"
#include "../port/fast3d/imgui/imgui.h"

namespace {
struct MenuReadinessContext {
    char editorText[32] = "Profile";
    ImVec2 editorCenter{};
    MenuReadinessContext() {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~MenuReadinessContext() { ImGui::DestroyContext(); }
    void menu(const char *name, bool popup = false) {
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin(name);
        ImGui::SetWindowFocus();
        ImGui::Button("Select");
        ImGui::SetItemDefaultFocus();
        if (popup) ImGui::OpenPopup("Child owner");
        if (ImGui::BeginPopup("Child owner")) {
            ImGui::Button("Child button");
            ImGui::EndPopup();
        }
        ImGui::End();
    }
    void frame(const char *name, bool popup = false) {
        ImGui::NewFrame();
        menu(name, popup);
        ImGui::Render();
    }
    void viewFrame(int view = 0, int tab = -1, bool blocked = false, bool record = true, bool editor = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("##main_menu");
        if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
        ImGui::Button("Play");
        const ImGuiID play = ImGui::GetItemID();
        ImGui::SetItemDefaultFocus();
        ImGui::Button("Settings");
        const ImGuiID settings = ImGui::GetItemID();
        if (editor) {
            ImGui::InputText("Profile name", editorText, sizeof(editorText));
            const ImVec2 first = ImGui::GetItemRectMin(), last = ImGui::GetItemRectMax();
            editorCenter = ImVec2(first.x + 20.0f, (first.y + last.y) * 0.5f);
        }
        if (record) pdguiMenuRecordView(view, tab, play, settings, blocked);
        ImGui::End();
        ImGui::Render();
    }
};
}

TEST_CASE("menu readiness requires the named focused menu to finish rendering",
    "[smoke][readiness][menus][imgui]")
{
    REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    MenuReadinessContext context;
    REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    ImGui::NewFrame();
    context.menu("##agent_select");
    REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    ImGui::Render();
    context.frame("##agent_select");
    REQUIRE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_create", 1));
    REQUIRE_FALSE(pdguiMenuWindowOwnsInput(nullptr, 1));

    context.frame("##agent_create");
    context.frame("##agent_create");
    REQUIRE(pdguiMenuWindowOwnsInput("##agent_create", 1));
    REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    ImGui::NewFrame();
    ImGui::Render();
    REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_create", 1));
}

TEST_CASE("menu readiness denies a popup owner and physical focus loss",
    "[smoke][readiness][menus][imgui]")
{
    MenuReadinessContext context;
    context.frame("##agent_select");
    context.frame("##agent_select");
    REQUIRE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    SECTION("a child popup captures the next menu action") {
        context.frame("##agent_select", true);
        REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    }
    SECTION("application focus must remain with the client") {
        ImGui::GetIO().AddFocusEvent(false);
        for (int frame = 0; frame < 3; ++frame) {
            context.frame("##agent_select");
            // EndFrame cleared the transient flag; the persistent application
            // authority supplied by the production caller still denies input.
            REQUIRE_FALSE(ImGui::GetIO().AppFocusLost);
            REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_select", 0));
        }
        ImGui::GetIO().AddFocusEvent(true);
        context.frame("##agent_select");
        REQUIRE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    }
}

TEST_CASE("manual Agent Select rows admit their focused rendered window without explicit item focus",
    "[smoke][readiness][menus][imgui]")
{
    MenuReadinessContext context;
    /* Match renderAgentSelect: focus the parent, draw the child Selectable,
     * and retain the application's selected row without SetItemDefaultFocus. */
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("##agent_select");
        if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
        ImGui::TextUnformatted("Choose Your Reality");
        if (ImGui::BeginChild("##agent_list", ImVec2(0, 200), true))
            ImGui::Selectable("+ New Agent...", true, ImGuiSelectableFlags_None, ImVec2(0, 40));
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
    }
    REQUIRE(pdguiMenuWindowOwnsInput("##agent_select", 1));
}

TEST_CASE("Main Menu readiness follows native keyboard focus and only fresh observations",
    "[smoke][readiness][menus][imgui]")
{
    MenuReadinessContext context;
    pdgui_menu_view_readiness_t facts{};
    context.viewFrame();
    context.viewFrame();
    REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
    // The first Tab reveals the first item when the default focus is hidden.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    context.viewFrame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    context.viewFrame();
    context.viewFrame();
    REQUIRE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
    REQUIRE(facts.view == 0);
    REQUIRE(facts.play_focused);
    REQUIRE_FALSE(facts.settings_focused);
    // A separate native Tab advances from visible Play focus to Settings.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    context.viewFrame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    context.viewFrame();
    context.viewFrame();
    REQUIRE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
    REQUIRE(facts.settings_focused);
    REQUIRE_FALSE(facts.play_focused);

    SECTION("physical focus and wrong root cannot borrow the observation") {
        REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 0, &facts));
        REQUIRE_FALSE(pdguiMenuViewReadiness("##agent_select", 1, &facts));
        REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 1, nullptr));
    }
    SECTION("a later frame must submit its own observation") {
        context.viewFrame(0, -1, false, false);
        REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
    }
    SECTION("pending transitions and child input ownership block readiness") {
        context.viewFrame(2, 3, true);
        REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
        context.viewFrame(2, 3);
        REQUIRE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
        REQUIRE(facts.view == 2);
        REQUIRE(facts.submitted_tab == 3);
        context.frame("##main_menu", true);
        REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
    }
    SECTION("an unfinished next frame cannot report prior rendered state") {
        ImGui::NewFrame();
        REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
        ImGui::Render();
        REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
    }
    SECTION("a pointer-activated native editor owns the next input") {
        context.viewFrame(2, 3, false, true, true);
        ImGui::GetIO().AddMousePosEvent(context.editorCenter.x, context.editorCenter.y);
        context.viewFrame(2, 3, false, true, true);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        context.viewFrame(2, 3, false, true, true);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        context.viewFrame(2, 3, false, true, true);
        REQUIRE(ImGui::IsAnyItemActive());
        REQUIRE_FALSE(pdguiMenuViewReadiness("##main_menu", 1, &facts));
    }
}
