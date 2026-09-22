#include "catch.hpp"
#include "pdgui_endscreen_intent.h"

#include <fstream>
#include <iterator>
#include <string>

namespace {
using Intent = PdguiEndscreenIntent;
std::string endscreenSource()
{
    std::ifstream file(std::string(PD_SOURCE_DIR) + "/port/fast3d/pdgui_menu_endscreen.cpp", std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string functionBody(const std::string &text, const char *signature)
{
    const auto signatureAt = text.find(signature);
    REQUIRE(signatureAt != std::string::npos);
    const auto begin = text.find('{', signatureAt);
    REQUIRE(begin != std::string::npos);
    int depth = 1;
    auto end = begin + 1;
    for (; end < text.size() && depth > 0; ++end) {
        if (text[end] == '{') ++depth;
        else if (text[end] == '}') --depth;
    }
    REQUIRE(depth == 0);
    return text.substr(begin, end - begin);
}

size_t occurrences(const std::string &text, const std::string &needle)
{
    size_t count = 0;
    for (size_t at = 0; (at = text.find(needle, at)) != std::string::npos; at += needle.size()) ++count;
    return count;
}
}

TEST_CASE("Completed results Back beats Next and Retry and dispatches one exit",
          "[input][endscreen][intent]")
{
    PdguiEndscreenDecision decision;
    decision.request(Intent::MainMenu); // owner-filtered Back is captured first
    decision.request(Intent::Continue);
    decision.request(Intent::Retry);
    int callbacks = 0;
    Intent delivered = Intent::None;
    REQUIRE(decision.dispatch([&](Intent intent) { ++callbacks; delivered = intent; }));
    REQUIRE(callbacks == 1);
    REQUIRE(delivered == Intent::MainMenu);
    REQUIRE_FALSE(decision.dispatch([&](Intent) { ++callbacks; }));
    REQUIRE(callbacks == 1);
}

TEST_CASE("Failed Solo and multiplayer Back open one confirmation without forward dispatch",
          "[input][endscreen][intent]")
{
    for (auto confirm : {Intent::ConfirmMainMenu, Intent::ConfirmDisconnect, Intent::ConfirmQuit}) {
        PdguiEndscreenDecision decision;
        decision.request(confirm);
        decision.request(Intent::Continue);
        decision.request(Intent::Retry);
        int callbacks = 0;
        REQUIRE(decision.consume(confirm));
        REQUIRE_FALSE(decision.consume(confirm));
        REQUIRE_FALSE(decision.dispatch([&](Intent) { ++callbacks; }));
        REQUIRE(callbacks == 0);
        // Cancel returns no new intent. An independently accepted modal
        // returns its one graph action after its own balanced EndPopup.
        const auto accepted = confirm == Intent::ConfirmMainMenu ? Intent::MainMenu
            : confirm == Intent::ConfirmDisconnect ? Intent::Disconnect : Intent::Quit;
        decision.request(accepted);
        REQUIRE(decision.dispatch([&](Intent intent) { ++callbacks; REQUIRE(intent == accepted); }));
        REQUIRE(callbacks == 1);
    }
}

TEST_CASE("A results frame retires its selected operation before callback reentry",
          "[input][endscreen][intent]")
{
    PdguiEndscreenDecision decision;
    decision.request(Intent::Continue);
    int callbacks = 0;
    REQUIRE(decision.dispatch([&](Intent intent) {
        ++callbacks;
        REQUIRE(intent == Intent::Continue);
        REQUIRE_FALSE(decision.dispatch([&](Intent) { ++callbacks; }));
    }));
    REQUIRE(callbacks == 1);
}

TEST_CASE("Ordinary results action intents each dispatch once while idle dispatches nothing",
          "[input][endscreen][intent]")
{
    for (auto requested : {Intent::Continue, Intent::Retry, Intent::MainMenu, Intent::Disconnect, Intent::Quit}) {
        PdguiEndscreenDecision decision;
        int callbacks = 0;
        REQUIRE_FALSE(decision.dispatch([&](Intent) { ++callbacks; }));
        REQUIRE_FALSE(decision.consume(Intent::None));
        decision.request(requested);
        REQUIRE(decision.dispatch([&](Intent delivered) { ++callbacks; REQUIRE(delivered == requested); }));
        REQUIRE(callbacks == 1);
    }
}

TEST_CASE("Production results callers capture owned Back and defer one graph dispatch past rendering",
          "[input][endscreen][intent][static]")
{
    const auto source = endscreenSource();
    for (const auto signature : {"static void renderSoloEndscreen(", "static void renderMpEndscreen("}) {
        const auto body = functionBody(source, signature);
        const auto capture = body.find("const bool backPressed = !inputSuppressed && pdguiNavActionAllowed(ACTION_MENU_CANCEL)");
        const auto firstButton = body.find("pdguiActionBarButton(");
        const auto dispatch = body.find("decision.dispatch(");
        REQUIRE(capture != std::string::npos);
        REQUIRE(capture < body.find("if (backPressed) decision.request("));
        REQUIRE(capture < firstButton);
        REQUIRE(body.find("ImGui::BeginDisabled(!parentActionsAllowed || backPressed);") < firstButton);
        REQUIRE(body.find("pdguiNavActionAllowed(ACTION_MENU_ACCEPT)") < firstButton);
        REQUIRE(body.find("pdguiConsumeTitleClose() || pdguiMenuCancelPressed()") != std::string::npos);
        REQUIRE(occurrences(body, "pdguiMenuCancelPressed()") == 1);
        REQUIRE(occurrences(body, "decision.dispatch(") == 1);
        REQUIRE(body.rfind("pdguiEndActionBar();") < body.rfind("ImGui::EndDisabled();"));
        REQUIRE(body.rfind("ImGui::EndDisabled();") < body.rfind("ImGui::End();"));
        REQUIRE(body.rfind("ImGui::End();") < body.rfind("pdguiSetPalette(prevPalette);"));
        REQUIRE(body.rfind("pdguiSetPalette(prevPalette);") < dispatch);
        REQUIRE(body.find("menuGraphFire") > dispatch);
        REQUIRE(body.find("pdguiNavSuppressActivation();", dispatch) < body.find("menuGraphFire"));
        REQUIRE(occurrences(body, "menuGraphFire") == 3);
    }
    const auto solo = functionBody(source, "static void renderSoloEndscreen(");
    REQUIRE(solo.find("completed ? PdguiEndscreenIntent::MainMenu") != std::string::npos);
    REQUIRE(solo.find(": PdguiEndscreenIntent::ConfirmMainMenu") != std::string::npos);
    REQUIRE(solo.find("decision.consume(PdguiEndscreenIntent::ConfirmMainMenu)") < solo.find("ImGui::OpenPopup(sfmmPopupId)"));
    REQUIRE(solo.find("pdguiNavSuppressActivation();") < solo.find("ImGui::OpenPopup(sfmmPopupId)"));
    const auto mp = functionBody(source, "static void renderMpEndscreen(");
    REQUIRE(mp.find("networked ? PdguiEndscreenIntent::ConfirmDisconnect") != std::string::npos);
    REQUIRE(mp.find(": PdguiEndscreenIntent::ConfirmQuit") != std::string::npos);
    REQUIRE(mp.find("decision.consume(PdguiEndscreenIntent::ConfirmDisconnect)") < mp.find("ImGui::OpenPopup(mpDisconnectPopupId)"));
    REQUIRE(mp.find("decision.consume(PdguiEndscreenIntent::ConfirmQuit)") < mp.find("ImGui::OpenPopup(mpQuitPopupId)"));
}
