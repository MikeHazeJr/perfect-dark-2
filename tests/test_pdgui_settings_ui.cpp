#include "catch.hpp"
#include "pdgui_settings_ui.h"
#include "pdgui_nav_input.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <string>
#include <vector>

namespace {
struct Registry {
    std::vector<std::string> ids, names;
    explicit Registry(int count = 32)
    {
        for (int i = 0; i < count; ++i) {
            ids.push_back("custom:style_" + std::to_string(i));
            names.push_back("Style " + std::to_string(i));
        }
    }
    PdguiSettingsRegistry view()
    {
        return { (int)ids.size(),
            [](int i, void *p) { return static_cast<Registry *>(p)->ids.at(i).c_str(); },
            [](int i, void *p) { return static_cast<Registry *>(p)->names.at(i).c_str(); }, this };
    }
};

struct Input {
    bool accept = false, cancel = false, down = false;
    bool enter = false, escape = false, keyboardDown = false;
    bool mouseDown = false;
    ImVec2 mouse = ImVec2(-100, -100);
};

struct SettingsHarness {
    enum Kind { Actions, Styles, Dependencies, Editor } kind;
    enum Focus { None, Main, Text, Nested } focus = None;
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGuiContext *context = ImGui::CreateContext();
    Registry registry;
    bool enabled = false, staticEnabled = true, editorVisible = true;
    bool nestedOpen = false, actionsOpen = false;
    int chosen = -2;
    PdguiSettingsThemeAction action = PDGUI_SETTINGS_THEME_NONE;
    std::string activeId = registry.ids.back();
    std::string acceptGlyph = "Cross", cancelGlyph = "Circle", logged;
    char text[64] = "original";
    float width = 420;
    ImGuiID mainId = 0, textId = 0;
    ImVec2 mainPointerPoint;
    ImRect closeRect;
    float contentRight = 0;

    explicit SettingsHarness(Kind k, int registryCount = 32) : kind(k), registry(registryCount)
    {
        auto &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 700);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        unsigned char *pixels; int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        frame(); frame();
        focus = Main; frame(); frame();
        REQUIRE(context->NavCursorVisible);
    }
    ~SettingsHarness()
    {
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous);
    }
    void frame(const Input &input = Input())
    {
        pdguiNavCaptureOwners();
        PdguiNavInput nav{};
        nav.active = true;
        nav.accept = input.accept;
        nav.cancel = input.cancel;
        nav.down = input.down;
        pdguiSubmitNavInput(nav);
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Enter, input.enter);
        io.AddKeyEvent(ImGuiKey_Escape, input.escape);
        io.AddKeyEvent(ImGuiKey_DownArrow, input.keyboardDown);
        io.AddMousePosEvent(input.mouse.x, input.mouse.y);
        io.AddMouseButtonEvent(0, input.mouseDown);
        ImGui::NewFrame();
        pdguiNavFinishOwners();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(width, 600));
        ImGui::Begin("Settings helper parent", nullptr, ImGuiWindowFlags_NoSavedSettings);
        // Match the production menu's visible focus policy. FocusApi alone uses
        // NoSetNavCursorVisible, which leaves a cold headless context unable to
        // activate its otherwise correctly focused button with Enter/gamepad A.
        if (focus != None) ImGui::SetNavCursorVisible(true);
        action = PDGUI_SETTINGS_THEME_NONE;
        ImGui::LogToBuffer();
        if (kind == Actions) {
            ImGui::Button(enabled ? "Theme" : "Disabled theme");
            ImGui::OpenPopupOnItemClick("##theme_actions");
            if (focus == Main) ImGui::SetKeyboardFocusHere();
            mainId = ImGui::GetID("Actions##theme_actions_button");
            const ImVec2 start = ImGui::GetCursorScreenPos();
            mainPointerPoint = ImVec2(start.x + 10.0f, start.y + 15.0f);
            action = pdguiSettingsThemeActions(enabled, acceptGlyph.c_str(), 900, 30);
            actionsOpen = ImGui::IsPopupOpen("##theme_actions");
        } else if (kind == Styles || kind == Dependencies) {
            if (focus == Main) ImGui::SetKeyboardFocusHere();
            const char *label = kind == Styles ? "Menu Style" : "Embedded dependency";
            ImGui::PushID(label); mainId = ImGui::GetID("##style"); ImGui::PopID();
            if (kind == Styles)
                pdguiSettingsStyleCombo(label, registry.view(), staticEnabled, activeId.c_str(), &chosen);
            else
                pdguiSettingsRegistryCombo(label, registry.view(), activeId.c_str(), "(none)", &chosen);
        } else if (editorVisible) {
            if (!ImGui::IsPopupOpen("Editor")) ImGui::OpenPopup("Editor");
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowSize(ImVec2(width, 560));
            if (ImGui::BeginPopupModal("Editor", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
                if (focus == Main) ImGui::SetKeyboardFocusHere();
                mainId = ImGui::GetID("Close##settings_editor_close");
                const ImVec2 closeStart = ImGui::GetCursorScreenPos();
                const float closeWidth = ImGui::GetContentRegionAvail().x;
                contentRight = closeStart.x + closeWidth;
                const bool close = pdguiSettingsEditorClose(cancelGlyph.c_str(), input.cancel || input.escape, 30);
                // The first native control consumes this submitted row. A
                // retained navigation rectangle may belong to an earlier row
                // or layout and cannot be used as a pointer hit rectangle.
                closeRect = ImRect(closeStart, ImVec2(closeStart.x + closeWidth, closeStart.y + 30));
                if (close) {
                    editorVisible = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    ImGui::BeginChild("Editor content", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                    if (focus == Text) ImGui::SetKeyboardFocusHere();
                    ImGui::InputText("Name", text, sizeof(text)); textId = ImGui::GetItemID();
                    if (focus == Nested) ImGui::SetKeyboardFocusHere();
                    nestedOpen = ImGui::BeginCombo("Nested", "First");
                    if (nestedOpen) { ImGui::Selectable("First"); ImGui::EndCombo(); }
                    for (int i = 0; i < 40; ++i) ImGui::Text("Scrollable content %d", i);
                    ImGui::EndChild();
                }
                ImGui::EndPopup();
            }
        }
        logged = context->LogBuffer.c_str();
        ImGui::LogFinish();
        focus = None;
        ImGui::End();
        ImGui::Render();
    }
    void accept(bool keyboard = false)
    {
        Input input; input.accept = !keyboard; input.enter = keyboard; frame(input);
    }
    void cancel(bool keyboard = false)
    {
        Input input; input.cancel = !keyboard; input.escape = keyboard; frame(input);
    }
    void down(bool keyboard = false)
    {
        Input input; input.down = !keyboard; input.keyboardDown = keyboard; frame(input);
    }
};
}

TEST_CASE("Custom theme Actions works for disabled and enabled themes through native navigation", "[menus][settings-ui][imgui]")
{
    for (bool keyboard : {false, true}) {
        for (bool enabled : {false, true}) {
            CAPTURE(keyboard, enabled);
            SettingsHarness h(SettingsHarness::Actions); h.enabled = enabled;
            REQUIRE(h.context->NavId == h.mainId);
            h.accept(keyboard);
            REQUIRE(h.actionsOpen);
            REQUIRE(h.action == PDGUI_SETTINGS_THEME_NONE);
            for (int held = 0; held < 25; ++held) {
                h.accept(keyboard);
                REQUIRE(h.action == PDGUI_SETTINGS_THEME_NONE);
            }
            h.frame(); h.frame();
            h.accept(keyboard);
            REQUIRE(h.action == (enabled ? PDGUI_SETTINGS_THEME_DISABLE : PDGUI_SETTINGS_THEME_ENABLE));
        }
    }
}

TEST_CASE("Custom theme Actions exposes Delete intent and native Back cancels the popup", "[menus][settings-ui][imgui]")
{
    SettingsHarness h(SettingsHarness::Actions);
    h.accept(); h.frame(); h.frame();
    h.cancel();
    REQUIRE_FALSE(h.actionsOpen);
    REQUIRE(h.action == PDGUI_SETTINGS_THEME_NONE);
    h.frame(); h.accept(); h.frame(); h.frame();
    h.down(); h.frame(); h.accept();
    REQUIRE(h.action == PDGUI_SETTINGS_THEME_DELETE);
    // The production caller owns the existing shared destructive confirmation;
    // this helper returns intent and never performs the deletion itself.
}

TEST_CASE("Actions pointer activation and live device hint retain native controls", "[menus][settings-ui][imgui]")
{
    SettingsHarness h(SettingsHarness::Actions);
    REQUIRE(h.logged.find("Cross") != std::string::npos);
    h.acceptGlyph = "A"; h.frame();
    REQUIRE(h.logged.find("Select: A") != std::string::npos);
    const ImVec2 center = h.mainPointerPoint;
    Input click; click.mouse = center;
    h.frame(click); // Let native hover consume movement before the press.
    CAPTURE(center.x, center.y, h.context->NavId, h.mainId,
        h.context->IO.MousePos.x, h.context->IO.MousePos.y,
        h.context->HoveredId, h.context->NavHighlightItemUnderNav);
    INFO("hovered window=" << (h.context->HoveredWindow ? h.context->HoveredWindow->Name : "none"));
    click.mouseDown = true; h.frame(click);
    CAPTURE(h.context->ActiveId, h.context->IO.MouseClicked[0], h.context->IO.MouseDown[0]);
    REQUIRE(h.context->ActiveId == h.mainId);
    click.mouseDown = false; h.frame(click);
    REQUIRE(h.actionsOpen);
    REQUIRE(h.action == PDGUI_SETTINGS_THEME_NONE);
}

TEST_CASE("Style preview follows the authoritative ID including the last entry and unavailable identity", "[menus][settings-ui]")
{
    Registry registry(97);
    auto view = registry.view();
    REQUIRE(pdguiSettingsStylePreview(view, true, registry.ids.back().c_str()) == registry.names.back());
    REQUIRE(pdguiSettingsStylePreview(view, false, registry.ids.back().c_str()) == "Procedural (built-in)");
    REQUIRE(pdguiSettingsStylePreview(view, true, "missing:style") == "Unavailable style: missing:style");
    registry.names.back() = registry.names.front();
    REQUIRE(pdguiSettingsStylePreview(view, true, registry.ids.back().c_str()) == registry.names.front());
    Registry empty(0);
    REQUIRE(pdguiSettingsStylePreview(empty.view(), true, "custom:old") == "Unavailable style: custom:old");
}

TEST_CASE("Native Style combo reaches the 32nd registry entry with controller and keyboard", "[menus][settings-ui][imgui]")
{
    for (bool keyboard : {false, true}) {
        SettingsHarness h(SettingsHarness::Styles);
        REQUIRE(h.context->NavId == h.mainId);
        h.accept(keyboard); h.frame(); h.frame();
        REQUIRE(h.context->OpenPopupStack.Size == 1);
        REQUIRE(h.logged.find("Style 31") != std::string::npos);
        h.accept(keyboard);
        REQUIRE(h.chosen == 31);
    }
}

TEST_CASE("Editor Back closes only after native text and nested popup owners finish", "[menus][settings-ui][imgui]")
{
    for (bool keyboard : {false, true}) {
        SettingsHarness h(SettingsHarness::Editor);
        h.focus = SettingsHarness::Text; h.frame(); h.frame();
        REQUIRE(h.context->ActiveId == h.textId);
        h.cancel(keyboard);
        REQUIRE(h.editorVisible);
        REQUIRE(h.context->ActiveId == 0);
        h.frame(); h.cancel(keyboard);
        REQUIRE_FALSE(h.editorVisible);
    }
    SettingsHarness h(SettingsHarness::Editor);
    h.focus = SettingsHarness::Nested; h.frame(); h.frame();
    h.accept(); h.frame(); h.frame(); REQUIRE(h.nestedOpen);
    h.cancel(); REQUIRE(h.editorVisible);
    REQUIRE(h.context->OpenPopupStack.Size == 1);
    h.frame(); h.cancel(); REQUIRE_FALSE(h.editorVisible);
}

TEST_CASE("Editor Close remains native and inside narrow content while the body scrolls", "[menus][settings-ui][imgui]")
{
    SettingsHarness h(SettingsHarness::Editor);
    h.width = 180; h.cancelGlyph = "Keyboard Escape / Controller Circle";
    h.frame(); h.frame();
    REQUIRE(h.context->NavId == h.mainId);
    REQUIRE(h.closeRect.Max.x <= h.contentRight);
    REQUIRE(h.closeRect.Min.x >= 10);
    REQUIRE(h.logged.find("Back: Keyboard Escape / Controller Circle") != std::string::npos);
    ImGuiWindow *body = nullptr;
    for (auto *window : h.context->Windows)
        if (window->Flags & ImGuiWindowFlags_ChildWindow) body = window;
    REQUIRE(body != nullptr);
    REQUIRE(body->ScrollMax.y > 0);
    Input click; click.mouse = ImVec2(h.closeRect.Min.x + 30, h.closeRect.Min.y + 15);
    h.frame(click); // Establish hover before the ordinary press/release gesture.
    click.mouseDown = true; h.frame(click);
    REQUIRE(h.context->ActiveId == h.mainId);
    click.mouseDown = false; h.frame(click);
    REQUIRE_FALSE(h.editorVisible);
}

TEST_CASE("Native dependency combo preserves selections and preview beyond 64 eligible entries", "[menus][settings-ui][imgui]")
{
    for (bool keyboard : {false, true}) {
        SettingsHarness h(SettingsHarness::Dependencies, 97);
        REQUIRE(h.context->NavId == h.mainId);
        REQUIRE(pdguiSettingsRegistryPreview(h.registry.view(), h.activeId.c_str(), "(none)") == "Style 96");
        h.accept(keyboard); h.frame(); h.frame();
        REQUIRE(h.context->OpenPopupStack.Size == 1);
        REQUIRE(h.logged.find("Style 96") != std::string::npos);
        h.accept(keyboard);
        REQUIRE(h.chosen == 96);
        REQUIRE(h.registry.ids.at(h.chosen) == h.activeId);
    }
    Registry choices(97);
    REQUIRE(pdguiSettingsRegistryPreview(choices.view(), "missing:typed_archive", "(none)") ==
        "Unavailable: missing:typed_archive");
    REQUIRE(pdguiSettingsRegistryPreview(choices.view(), "", "(none)") == "(none)");
}
