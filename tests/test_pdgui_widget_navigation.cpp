#include "catch.hpp"
#include "pdgui_nav_input.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {
struct WidgetHarness {
    enum Kind { Slider, Drag, Text, Combo, Repeat } kind;
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGuiContext *context = ImGui::CreateContext();
    int value = 10;
    int selected = 0;
    int repeatCount = 0;
    ImGuiID widget = 0;
    bool popupOpen = false;
    bool parentActionAllowed = true;
    char text[64] = "abc";

    explicit WidgetHarness(Kind k) : kind(k)
    {
        auto &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 720);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        unsigned char *pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        frame(); frame();
        frame({true, false, false, false, true, false, false});
        frame(); frame();
    }
    ~WidgetHarness()
    {
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous);
    }
    void frame(PdguiNavInput input = {true, false, false, false, false, false, false},
               bool opening = false, bool focusWidget = false)
    {
        pdguiNavCaptureOwners();
        pdguiSubmitNavInput(input);
        ImGui::NewFrame();
        pdguiNavFinishOwners();
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(500, 400));
        ImGui::Begin("Actual widgets", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (opening) pdguiNavSuppressOpeningGesture();
        parentActionAllowed = pdguiNavActionAllowed();
        ImGui::Button("Start");
        ImGui::SetItemDefaultFocus();
        if (focusWidget) ImGui::SetKeyboardFocusHere();
        switch (kind) {
        case Slider: ImGui::SliderInt("Value", &value, 0, 100); break;
        case Drag: ImGui::DragInt("Value", &value, 1.0f, 0, 100); break;
        case Text: ImGui::InputText("Value", text, sizeof(text)); break;
        case Combo:
            popupOpen = ImGui::BeginCombo("Value", selected ? "Second" : "First");
            widget = ImGui::GetItemID();
            if (popupOpen) {
                if (ImGui::Selectable("First", selected == 0)) selected = 0;
                if (ImGui::Selectable("Second", selected == 1)) selected = 1;
                ImGui::EndCombo();
            }
            break;
        case Repeat:
            ImGui::PushButtonRepeat(true);
            if (ImGui::Button("Value")) ++repeatCount;
            ImGui::PopButtonRepeat();
            break;
        }
        if (kind != Combo) widget = ImGui::GetItemID();
        ImGui::End();
        ImGui::Render();
    }
    void accept() { frame({true, true, false, false, false, false, false}); }
    void cancel() { frame({true, false, true, false, false, false, false}); }
};
}

TEST_CASE("Controller Activate tweaks numeric widgets without entering temporary text", "[input][menus][imgui][widgets]")
{
    for (auto kind : {WidgetHarness::Slider, WidgetHarness::Drag}) {
        WidgetHarness h(kind);
        REQUIRE(GImGui->NavId == h.widget);
        h.accept();
        REQUIRE(GImGui->ActiveId == h.widget);
        REQUIRE_FALSE(ImGui::TempInputIsActive(h.widget));
        h.frame();
        const int before = h.value;
        for (int n = 0; n < 8; ++n)
            h.frame({true, false, false, false, false, false, true});
        REQUIRE(h.value > before);
        h.frame();
        h.accept();
        REQUIRE(GImGui->ActiveId == 0);
        REQUIRE_FALSE(h.parentActionAllowed);
    }
}

TEST_CASE("Physical Enter retains numeric text entry while mapped neutral leaves it owned", "[input][menus][imgui][widgets]")
{
    WidgetHarness h(WidgetHarness::Slider);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    h.frame();
    REQUIRE(ImGui::TempInputIsActive(h.widget));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    h.frame();
    ImGui::GetIO().AddInputCharactersUTF8("42");
    h.frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    h.frame();
    REQUIRE(h.value == 42);
    REQUIRE_FALSE(h.parentActionAllowed);
}

TEST_CASE("Controller Activate enters text and edits through native character validation", "[input][menus][imgui][widgets]")
{
    WidgetHarness h(WidgetHarness::Text);
    h.accept();
    REQUIRE(GImGui->ActiveId == h.widget);
    h.frame();
    ImGui::GetIO().AddInputCharactersUTF8("hello");
    h.frame();
    REQUIRE(std::strcmp(h.text, "hello") == 0);
    h.cancel();
    REQUIRE(std::strcmp(h.text, "abc") == 0);
    REQUIRE(GImGui->ActiveId == 0);
    REQUIRE_FALSE(h.parentActionAllowed);
}

TEST_CASE("Native combo consumes Back for the complete parent frame", "[input][menus][imgui][widgets]")
{
    WidgetHarness h(WidgetHarness::Combo);
    h.accept(); h.frame();
    REQUIRE(h.popupOpen);
    REQUIRE_FALSE(h.parentActionAllowed);
    h.cancel();
    REQUIRE_FALSE(h.popupOpen);
    REQUIRE_FALSE(h.parentActionAllowed);
    h.frame();
    REQUIRE(h.parentActionAllowed);
}

TEST_CASE("Mapped controller Activate retains ImGui held button repeat", "[input][menus][imgui][widgets]")
{
    WidgetHarness h(WidgetHarness::Repeat);
    h.accept();
    REQUIRE(h.repeatCount == 1);
    for (int n = 0; n < 45; ++n) h.accept();
    REQUIRE(h.repeatCount > 1);
}

TEST_CASE("A new popup owner cannot reuse its opening activation", "[input][menus][imgui][widgets]")
{
    WidgetHarness h(WidgetHarness::Text);
    pdguiNavCaptureOwners();
    pdguiSubmitNavInput({true, true, false, false, false, false, false});
    ImGui::NewFrame();
    pdguiNavFinishOwners();
    ImGui::Begin("New owner");
    pdguiNavSuppressActivation();
    REQUIRE_FALSE(pdguiNavActionAllowed());
    REQUIRE(GImGui->NavActivateId == 0);
    REQUIRE(GImGui->NavActivatePressedId == 0);
    REQUIRE_FALSE(ImGui::Button("First action"));
    ImGui::End();
    ImGui::Render();
}


TEST_CASE("Paired Back and Activate cancel numeric editing without reactivation", "[input][menus][imgui][widgets]")
{
    for (auto kind : {WidgetHarness::Slider, WidgetHarness::Drag}) {
        for (bool keyboard : {false, true}) {
            CAPTURE(kind, keyboard);
            WidgetHarness h(kind);
            h.accept(); h.frame();
            REQUIRE(GImGui->ActiveId == h.widget);
            const int before = h.value;
            if (keyboard) {
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
                h.frame();
            } else {
                h.frame({true, true, true, false, false, false, false});
            }
            REQUIRE(GImGui->ActiveId == 0);
            REQUIRE(h.value == before);
            REQUIRE_FALSE(h.parentActionAllowed);
            REQUIRE(GImGui->NavActivateId == 0);
            REQUIRE(GImGui->NavActivatePressedId == 0);
            if (keyboard) {
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
            }
            h.frame();
            REQUIRE(h.parentActionAllowed);
            h.accept();
            REQUIRE(GImGui->ActiveId == h.widget);
        }
    }
}

TEST_CASE("Paired Back and Activate close a native combo without reopening it", "[input][menus][imgui][widgets]")
{
    for (bool keyboard : {false, true}) {
        CAPTURE(keyboard);
        WidgetHarness h(WidgetHarness::Combo);
        h.accept(); h.frame();
        REQUIRE(h.popupOpen);
        const int before = h.selected;
        if (keyboard) {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
            h.frame();
        } else {
            h.frame({true, true, true, false, false, false, false});
        }
        REQUIRE_FALSE(h.popupOpen);
        REQUIRE(h.selected == before);
        REQUIRE_FALSE(h.parentActionAllowed);
        REQUIRE(GImGui->OpenPopupStack.empty());
        if (keyboard) {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        }
        h.frame();
        REQUIRE(h.parentActionAllowed);
        h.accept();
        REQUIRE(h.popupOpen);
    }
}

TEST_CASE("An active native editor owns its keys while higher-level commands remain available", "[input][menus][imgui][widgets]")
{
    for (auto kind : {WidgetHarness::Slider, WidgetHarness::Drag, WidgetHarness::Text}) {
        WidgetHarness h(kind);
        h.accept(); h.frame();
        REQUIRE(GImGui->ActiveId == h.widget);
        REQUIRE_FALSE(pdguiNavActionAllowed(ACTION_MENU_ACCEPT));
        REQUIRE_FALSE(pdguiNavActionAllowed(ACTION_MENU_CANCEL));
        REQUIRE_FALSE(pdguiNavActionAllowed(ACTION_MENU_LEFT));
        REQUIRE_FALSE(pdguiNavActionAllowed(ACTION_MENU_RIGHT));
        REQUIRE(pdguiNavActionAllowed(ACTION_MENU_TAB_PREV));
        REQUIRE(pdguiNavActionAllowed(ACTION_MENU_TAB_NEXT));
        REQUIRE(pdguiNavActionAllowed(ACTION_MENU_SECONDARY));
        REQUIRE(pdguiNavActionAllowed(ACTION_MENU_TERTIARY));
        if (kind == WidgetHarness::Text)
            REQUIRE_FALSE(pdguiNavActionAllowed(ACTION_MENU_DELETE));
    }
}


TEST_CASE("Text Back wins simultaneous validation through keyboard and controller", "[input][menus][imgui][widgets]")
{
    for (bool keyboard : {false, true}) {
        CAPTURE(keyboard);
        WidgetHarness h(WidgetHarness::Text);
        h.accept(); h.frame();
        ImGui::GetIO().AddInputCharactersUTF8("hello");
        h.frame();
        REQUIRE(std::strcmp(h.text, "hello") == 0);
        if (keyboard) {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
            h.frame();
        } else {
            h.frame({true, true, true, false, false, false, false});
        }
        REQUIRE(std::strcmp(h.text, "abc") == 0);
        REQUIRE(GImGui->ActiveId == 0);
        REQUIRE_FALSE(h.parentActionAllowed);
    }
}

TEST_CASE("Opening Accept stays physically held but cannot activate or repeat until release", "[input][menus][imgui][widgets]")
{
    for (bool keyboard : {false, true}) {
        CAPTURE(keyboard);
        WidgetHarness h(WidgetHarness::Repeat);
        if (keyboard) ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        h.frame({true, !keyboard, false, false, false, false, false}, true);
        REQUIRE(h.repeatCount == 0);
        for (int n = 0; n < 45; ++n) {
            h.frame({true, !keyboard, false, false, false, false, false});
            REQUIRE_FALSE(pdguiNavActionAllowed(ACTION_MENU_ACCEPT));
        }
        REQUIRE(h.repeatCount == 0);
        ImGuiContext &g = *GImGui;
        const ImGuiKey heldKey = keyboard ? ImGuiKey_Enter : ImGuiKey_NavGamepadActivate;
        REQUIRE(ImGui::GetKeyData(heldKey)->Down);
        if (keyboard) ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        h.frame();
        h.accept();
        REQUIRE(h.repeatCount == 1);
        for (int n = 0; n < 45; ++n) h.accept();
        REQUIRE(h.repeatCount > 1);
    }
}

TEST_CASE("Opening Back remains owned by the opening window until real release", "[input][menus][imgui][widgets]")
{
    for (bool keyboard : {false, true}) {
        CAPTURE(keyboard);
        WidgetHarness h(WidgetHarness::Repeat);
        if (keyboard) ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        h.frame({true, false, !keyboard, false, false, false, false}, true);
        for (int n = 0; n < 45; ++n) {
            h.frame({true, false, !keyboard, false, false, false, false});
            REQUIRE_FALSE(pdguiNavActionAllowed(ACTION_MENU_CANCEL));
        }
        ImGuiContext &g = *GImGui;
        const ImGuiKey heldKey = keyboard ? ImGuiKey_Escape : ImGuiKey_NavGamepadCancel;
        REQUIRE(ImGui::GetKeyData(heldKey)->Down);
        if (keyboard) ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        h.frame();
        REQUIRE(pdguiNavActionAllowed(ACTION_MENU_CANCEL));
    }
}

TEST_CASE("A newly focused text field cannot reclaim opening Enter and submit on repeat", "[input][menus][imgui][widgets]")
{
    WidgetHarness h(WidgetHarness::Text);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    h.frame({true, false, false, false, false, false, false}, true, true);
    for (int n = 0; n < 45; ++n) h.frame();
    REQUIRE(ImGui::GetKeyData(ImGuiKey_Enter)->Down);
    REQUIRE(GImGui->ActiveId == h.widget);
    REQUIRE_FALSE(pdguiNavActionAllowed(ACTION_MENU_ACCEPT));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    h.frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    h.frame();
    REQUIRE(GImGui->ActiveId == 0);
}

TEST_CASE("Menu opening callers retain the real gesture and Agent Create gives Back priority", "[input][menus][static][widgets]")
{
    for (const char *path : {"port/fast3d/pdgui_menu_mainmenu.cpp",
                            "port/fast3d/pdgui_menu_moddinghub.cpp"}) {
        CAPTURE(path);
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.good());
        std::ostringstream buffer;
        buffer << file.rdbuf();
        const std::string source = buffer.str();
        REQUIRE(source.find("pdguiNavSuppressOpeningGesture();") != std::string::npos);
        REQUIRE(source.find("nio.AddKeyEvent(") == std::string::npos);
        REQUIRE(source.find("actionmapFlushActionSet(menuOpenFlushActions") == std::string::npos);
    }
    std::ifstream file("port/fast3d/pdgui_menu_agentcreate.cpp", std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    REQUIRE(buffer.str().find("if (doCreate && nameValid && !doCancel)") != std::string::npos);
}


TEST_CASE("Backend and C menu queries share native owner and mouse Back policy", "[input][menus][static][widgets]")
{
    std::ifstream backendFile("port/fast3d/pdgui_backend.cpp", std::ios::binary);
    REQUIRE(backendFile.good());
    std::ostringstream backendBuffer;
    backendBuffer << backendFile.rdbuf();
    const std::string backend = backendBuffer.str();
    REQUIRE(backend.find("pdguiNavActionAllowed(action)") != std::string::npos);
    const size_t capture = backend.find("pdguiNavCaptureOwners();");
    const size_t newFrame = backend.find("ImGui::NewFrame();", capture);
    const size_t finish = backend.find("pdguiNavFinishOwners();", newFrame);
    REQUIRE(capture != std::string::npos);
    REQUIRE(capture < newFrame);
    REQUIRE(newFrame < finish);
    REQUIRE(backend.find("pdguiNavSetFrameCancel(mouseCancel ? 1 : 0);") != std::string::npos);
    std::ifstream navFile("port/src/pdgui_nav.c", std::ios::binary);
    REQUIRE(navFile.good());
    std::ostringstream navBuffer;
    navBuffer << navFile.rdbuf();
    const std::string nav = navBuffer.str();
    REQUIRE(nav.find("s_ActionFilter(action)") != std::string::npos);
    REQUIRE(nav.find("action == ACTION_MENU_CANCEL && s_FrameCancel") != std::string::npos);
}
