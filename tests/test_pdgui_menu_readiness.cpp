#include "catch.hpp"
#include "pdgui_menu_readiness.h"
#include "../port/fast3d/imgui/imgui.h"
#include "../port/fast3d/imgui/imgui_internal.h"

namespace {
/* Independent pre-diagnostic admission oracle: keep the original predicates
 * so reason reporting cannot silently relax or strengthen the old result. */
int originalMenuWindowOwnsInput(const char *name, int focused)
{
    ImGuiContext *context = ImGui::GetCurrentContext();
    if (!focused || !context || !name || !name[0]) return 0;
    ImGuiWindow *window = ImGui::FindWindowByName(name);
    if (!window || !window->Active || window->Hidden || window->Collapsed || window->SkipItems)
        return 0;
    if (context->FrameCountRendered < 1 ||
        context->FrameCountRendered != context->FrameCountEnded ||
        window->LastFrameActive != context->FrameCountRendered) return 0;
    if (context->OpenPopupStack.Size != 0 || context->NavWindowingTarget || !context->NavWindow)
        return 0;
    return context->NavWindow->RootWindow == window->RootWindow;
}

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

TEST_CASE("menu ownership diagnostics safely distinguish unavailable observations",
    "[smoke][readiness][menus][imgui]")
{
    REQUIRE(ImGui::GetCurrentContext() == nullptr);
    REQUIRE(pdguiMenuObserveInput("##agent_select", 1).rejection_mask == PDGUI_MENU_INPUT_NO_CONTEXT);
    REQUIRE(pdguiMenuObserveInput(nullptr, 0).rejection_mask ==
        (PDGUI_MENU_INPUT_NO_CONTEXT | PDGUI_MENU_INPUT_NO_NAME | PDGUI_MENU_INPUT_FOCUS_LOST));
    MenuReadinessContext context;
    REQUIRE(pdguiMenuObserveInput(nullptr, 1).rejection_mask == PDGUI_MENU_INPUT_NO_NAME);
    REQUIRE(pdguiMenuObserveInput("", 1).rejection_mask == PDGUI_MENU_INPUT_NO_NAME);
    REQUIRE(pdguiMenuObserveInput("##agent_select", 1).rejection_mask == PDGUI_MENU_INPUT_NO_WINDOW);
    REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_select", 1));
    REQUIRE_FALSE(originalMenuWindowOwnsInput("##agent_select", 1));
}

TEST_CASE("menu ownership reasons preserve original admission and report simultaneous blockers",
    "[smoke][readiness][menus][imgui]")
{
    MenuReadinessContext fixture;
    fixture.frame("##other_menu");
    fixture.frame("##agent_select");
    fixture.frame("##agent_select");
    ImGuiContext &context = *ImGui::GetCurrentContext();
    ImGuiWindow *window = ImGui::FindWindowByName("##agent_select");
    REQUIRE(window != nullptr);
    unsigned int expected = 0;
    int focused = 1;

    SECTION("focused valid window") {
        REQUIRE(originalMenuWindowOwnsInput("##agent_select", focused));
    }
    SECTION("persistent application focus loss") {
        focused = 0;
        expected = PDGUI_MENU_INPUT_FOCUS_LOST;
    }
    SECTION("inactive") { window->Active = false; expected = PDGUI_MENU_INPUT_INACTIVE; }
    SECTION("hidden") { window->Hidden = true; expected = PDGUI_MENU_INPUT_HIDDEN; }
    SECTION("collapsed") { window->Collapsed = true; expected = PDGUI_MENU_INPUT_COLLAPSED; }
    SECTION("skipped items") { window->SkipItems = true; expected = PDGUI_MENU_INPUT_SKIP_ITEMS; }
    SECTION("no completed render") {
        context.FrameCountRendered = 0;
        expected = PDGUI_MENU_INPUT_RENDER_STALE;
    }
    SECTION("render and ended frame differ") {
        --context.FrameCountEnded;
        expected = PDGUI_MENU_INPUT_RENDER_STALE;
    }
    SECTION("cached window from earlier render") {
        --window->LastFrameActive;
        expected = PDGUI_MENU_INPUT_RENDER_STALE;
    }
    SECTION("native popup") {
        fixture.frame("##agent_select", true);
        const unsigned int mask = pdguiMenuObserveInput("##agent_select", focused).rejection_mask;
        REQUIRE((mask & PDGUI_MENU_INPUT_POPUP) != 0);
        REQUIRE_FALSE(pdguiMenuWindowOwnsInput("##agent_select", focused));
        REQUIRE_FALSE(originalMenuWindowOwnsInput("##agent_select", focused));
        return;
    }
    SECTION("window switching owns navigation") {
        context.NavWindowingTarget = window;
        expected = PDGUI_MENU_INPUT_WINDOW_SWITCH;
    }
    SECTION("navigation window unavailable") {
        context.NavWindow = nullptr;
        expected = PDGUI_MENU_INPUT_NO_NAV_WINDOW;
    }
    SECTION("another root owns navigation") {
        context.NavWindow = ImGui::FindWindowByName("##other_menu");
        REQUIRE(context.NavWindow != nullptr);
        expected = PDGUI_MENU_INPUT_ROOT_MISMATCH;
    }
    SECTION("focus loss does not hide independent ImGui blockers") {
        focused = 0;
        window->Hidden = true;
        window->SkipItems = true;
        context.NavWindow = nullptr;
        expected = PDGUI_MENU_INPUT_FOCUS_LOST | PDGUI_MENU_INPUT_HIDDEN |
            PDGUI_MENU_INPUT_SKIP_ITEMS | PDGUI_MENU_INPUT_NO_NAV_WINDOW;
    }
    const int rendered = context.FrameCountRendered;
    const int ended = context.FrameCountEnded;
    const int active = window->LastFrameActive;
    const bool visible_flags[] = {window->Active, window->Hidden, window->Collapsed, window->SkipItems};
    ImGuiWindow *nav = context.NavWindow;
    const ImGuiID nav_id = context.NavId;
    const unsigned int mask = pdguiMenuObserveInput("##agent_select", focused).rejection_mask;
    REQUIRE(mask == expected);
    REQUIRE(pdguiMenuWindowOwnsInput("##agent_select", focused) == (mask == 0));
    REQUIRE(pdguiMenuWindowOwnsInput("##agent_select", focused) ==
        originalMenuWindowOwnsInput("##agent_select", focused));
    REQUIRE(context.FrameCountRendered == rendered);
    REQUIRE(context.FrameCountEnded == ended);
    REQUIRE(window->LastFrameActive == active);
    REQUIRE(window->Active == visible_flags[0]);
    REQUIRE(window->Hidden == visible_flags[1]);
    REQUIRE(window->Collapsed == visible_flags[2]);
    REQUIRE(window->SkipItems == visible_flags[3]);
    REQUIRE(context.NavWindow == nav);
    REQUIRE(context.NavId == nav_id);
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
