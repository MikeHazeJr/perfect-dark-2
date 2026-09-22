#include "catch.hpp"
#include "pdgui_settings_save_status.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct Saves {
    int machineCalls = 0;
    int agentCalls = 0;
    bool machineOk = true;
    bool agentOk = true;
    std::vector<std::string> agents;
    static bool machine(void *p) {
        auto &s = *static_cast<Saves *>(p);
        ++s.machineCalls;
        return s.machineOk;
    }
    static bool agent(const char *name, void *p) {
        auto &s = *static_cast<Saves *>(p);
        ++s.agentCalls;
        s.agents.emplace_back(name);
        return s.agentOk;
    }
    PdguiSettingsSaveCallbacks callbacks() { return { machine, agent, this }; }
};
}

TEST_CASE("Settings coalesces machine requests and never retries a failed channel on redraw",
          "[settings-save]")
{
    PdguiSettingsSaveStatus state;
    Saves save;
    save.machineOk = false;
    for (int i = 0; i < 20; ++i) state.requestMachineSave();
    state.poll("", save.callbacks());
    REQUIRE(save.machineCalls == 1);
    REQUIRE(state.machineFailed());
    REQUIRE(state.machinePending());
    for (int i = 0; i < 300; ++i) {
        state.requestMachineSave(); // later edits still require an explicit Retry
        state.poll(nullptr, save.callbacks());
    }
    REQUIRE(save.machineCalls == 1);
    REQUIRE(save.agentCalls == 0);
    state.retry("", save.callbacks());
    REQUIRE(save.machineCalls == 2);
    REQUIRE(state.machineFailed());
    save.machineOk = true;
    state.retry("", save.callbacks());
    REQUIRE(save.machineCalls == 3);
    REQUIRE_FALSE(state.hasUnsaved());
    state.poll("", save.callbacks());
    REQUIRE(save.machineCalls == 3);
}

TEST_CASE("Settings retains independent Agent and machine failure outcomes",
          "[settings-save]")
{
    PdguiSettingsSaveStatus state;
    Saves save;
    save.agentOk = false;
    state.requestMachineSave();
    state.poll("Joanna", save.callbacks());
    REQUIRE(save.machineCalls == 1);
    REQUIRE(save.agentCalls == 1);
    REQUIRE_FALSE(state.machinePending());
    REQUIRE(state.agentFailed());
    for (int i = 0; i < 300; ++i) state.poll("Joanna", save.callbacks());
    REQUIRE(save.agentCalls == 1);
    save.agentOk = true;
    state.retry("Joanna", save.callbacks());
    REQUIRE(save.agentCalls == 2);
    REQUIRE(save.machineCalls == 1); // successful channel was not rewritten
    REQUIRE_FALSE(state.hasUnsaved());

    save.machineOk = false;
    state.requestMachineSave();
    state.poll("Joanna", save.callbacks());
    REQUIRE(state.machineFailed());
    REQUIRE_FALSE(state.agentFailed());
    const int agentCalls = save.agentCalls;
    state.retry("Joanna", save.callbacks());
    REQUIRE(save.agentCalls == agentCalls);
    REQUIRE(state.hasUnsaved());
}

TEST_CASE("Settings departure Stay and Leave preserve failures without hidden writes",
          "[settings-save]")
{
    PdguiSettingsSaveStatus state;
    Saves save;
    REQUIRE(state.requestDeparture());
    REQUIRE(save.machineCalls == 0);
    save.machineOk = false;
    state.requestMachineSave();
    state.poll("", save.callbacks());
    REQUIRE_FALSE(state.requestDeparture());
    REQUIRE(state.leavePending());
    for (int i = 0; i < 50; ++i) state.poll("", save.callbacks());
    REQUIRE(save.machineCalls == 1);
    REQUIRE_FALSE(state.chooseDeparture(PdguiSettingsLeaveChoice::Stay, "", save.callbacks()));
    REQUIRE_FALSE(state.leavePending());
    REQUIRE(state.machineFailed());
    REQUIRE_FALSE(state.requestDeparture());
    REQUIRE(state.chooseDeparture(PdguiSettingsLeaveChoice::LeaveWithoutSaving, "", save.callbacks()));
    REQUIRE_FALSE(state.leavePending());
    REQUIRE(state.machineFailed());
    state.cancelDeparture(); // another Settings owner/reentry retires intent, not error
    for (int i = 0; i < 50; ++i) state.poll("", save.callbacks());
    REQUIRE(save.machineCalls == 1);
    REQUIRE(state.machineFailed());
}

TEST_CASE("Settings Retry and leave waits for both independent commits",
          "[settings-save]")
{
    PdguiSettingsSaveStatus state;
    Saves save;
    save.machineOk = false;
    save.agentOk = false;
    state.requestMachineSave();
    state.poll("Joanna", save.callbacks());
    REQUIRE_FALSE(state.requestDeparture());
    save.machineOk = true;
    REQUIRE_FALSE(state.chooseDeparture(PdguiSettingsLeaveChoice::Retry, "Joanna", save.callbacks()));
    REQUIRE(state.leavePending());
    REQUIRE_FALSE(state.machinePending());
    REQUIRE(state.agentFailed());
    REQUIRE(save.machineCalls == 2);
    REQUIRE(save.agentCalls == 2);
    save.agentOk = true;
    REQUIRE(state.chooseDeparture(PdguiSettingsLeaveChoice::Retry, "Joanna", save.callbacks()));
    REQUIRE_FALSE(state.leavePending());
    REQUIRE_FALSE(state.hasUnsaved());
    REQUIRE(save.machineCalls == 2);
    REQUIRE(save.agentCalls == 3);
    // The accepted departure is an edge; a second consume does nothing.
    REQUIRE_FALSE(state.chooseDeparture(PdguiSettingsLeaveChoice::Retry, "Joanna", save.callbacks()));
    REQUIRE(save.agentCalls == 3);
}

TEST_CASE("Settings Agent identity transition never retries another Agent's failed values",
          "[settings-save]")
{
    PdguiSettingsSaveStatus state;
    Saves save;
    save.agentOk = false;
    state.poll("Joanna", save.callbacks());
    REQUIRE_FALSE(state.requestDeparture());
    save.agentOk = true;
    REQUIRE_FALSE(state.chooseDeparture(PdguiSettingsLeaveChoice::Retry, "Elvis", save.callbacks()));
    REQUIRE(save.agents == std::vector<std::string>{"Joanna"});
    REQUIRE(state.previousAgentFailures() == std::vector<std::string>{"Joanna"});
    REQUIRE_FALSE(state.leavePending());
    state.poll("Elvis", save.callbacks());
    REQUIRE((save.agents == std::vector<std::string>{"Joanna", "Elvis"}));
    REQUIRE_FALSE(state.agentFailed());
    state.poll("", save.callbacks());
    REQUIRE(save.agentCalls == 2);
    REQUIRE(state.previousAgentFailures() == std::vector<std::string>{"Joanna"});
    state.dismissPreviousAgentFailures();
    REQUIRE(state.previousAgentFailures().empty());
}

TEST_CASE("Settings missing adapters fail closed and cancellation never invokes Retry",
          "[settings-save]")
{
    PdguiSettingsSaveStatus state;
    const PdguiSettingsSaveCallbacks unavailable{nullptr, nullptr, nullptr};
    state.requestMachineSave();
    state.poll("Joanna", unavailable);
    REQUIRE(state.machineFailed());
    REQUIRE(state.agentFailed());
    REQUIRE_FALSE(state.requestDeparture());
    Saves save;
    REQUIRE_FALSE(state.chooseDeparture(PdguiSettingsLeaveChoice::Stay, "Joanna", save.callbacks()));
    REQUIRE(save.machineCalls == 0);
    REQUIRE(save.agentCalls == 0);
    REQUIRE(state.hasUnsaved());
}

TEST_CASE("Both Settings owners connect status and deferred departure to the checked adapters",
          "[settings-save][caller-contract]")
{
    std::ifstream input(std::string(PD_SETTINGS_SOURCE_ROOT) + "/port/fast3d/pdgui_menu_mainmenu.cpp");
    REQUIRE(input.good());
    const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    REQUIRE(source.find("return configSave(CONFIG_PATH) == 1;") != std::string::npos);
    REQUIRE(source.find("strcmp(active, expectedAgent) == 0 && prefsAgentSave() == 0") != std::string::npos);
    REQUIRE(source.find("configSave(\"pd.ini\")") == std::string::npos);
    REQUIRE(source.find("const bool retrySave = renderSettingsSaveStatus();") != std::string::npos);
    REQUIRE(source.find("s_SettingsSaveStatus.poll(prefsAgentGetActive(), settingsSaveCallbacks())") != std::string::npos);
    REQUIRE(source.find("if (requestBack) settingsLeave = s_SettingsSaveStatus.requestDeparture();") != std::string::npos);
    REQUIRE(source.find("((requestBack && backView != 2) || settingsLeave)") != std::string::npos);
    const auto ciBegin = source.find("static s32 renderCiSettingsRedirect(");
    const auto ciEnd = source.find("static s32 renderCiDeadPlayer2(", ciBegin);
    REQUIRE(ciBegin != std::string::npos);
    const std::string ci = source.substr(ciBegin, ciEnd - ciBegin);
    REQUIRE(ci.find("bool wantBack =") < ci.find("renderSettingsView(scale, contentH)"));
    REQUIRE(ci.find("bool leave = wantBack && s_SettingsSaveStatus.requestDeparture();") != std::string::npos);
    REQUIRE(ci.rfind("ImGui::End();") < ci.find("menuGraphFirePop(MENU_TYPE_CI_OPTIONS"));
    const auto popupBegin = source.find("static bool renderSettingsSaveDeparture()");
    const auto popupEnd = source.find("static bool PdButton(", popupBegin);
    const std::string popup = source.substr(popupBegin, popupEnd - popupBegin);
    REQUIRE(popup.find("const bool back = pdguiMenuCancelPressed()") < popup.find("ImGui::Button"));
    REQUIRE(popup.find("ImGui::BeginDisabled(back)") != std::string::npos);
    REQUIRE(popup.find("ImGui::SetNavCursorVisible(true)") != std::string::npos);
    REQUIRE(popup.find("pdguiNavSuppressOpeningGesture();") != std::string::npos);
    REQUIRE(popup.find("PdguiSettingsLeaveChoice::Stay") < popup.find("ImGui::Button"));
}
