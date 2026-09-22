/*
 * test_settings_input_tab_static.cpp -- Settings Input tab rebuild pins.
 *
 * @SYNC port/fast3d/pdgui_menu_mainmenu.cpp
 * @SYNC port/include/input.h
 * @SYNC port/src/input.c
 * @SYNC port/include/actionmap.h
 * @SYNC port/src/actionmap.cpp
 */

#include "catch.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

std::string readTextFile(const char *path)
{
	std::ifstream f(path, std::ios::in | std::ios::binary);
	if (!f) {
		return {};
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

void requireContains(const std::string &text, const char *needle)
{
	INFO(needle);
	REQUIRE(text.find(needle) != std::string::npos);
}

void requireNotContains(const std::string &text, const char *needle)
{
	INFO(needle);
	REQUIRE(text.find(needle) == std::string::npos);
}

std::string sliceBetween(const std::string &text, const char *startNeedle, const char *endNeedle)
{
	const size_t start = text.find(startNeedle);
	if (start == std::string::npos) {
		return {};
	}
	const size_t end = text.find(endNeedle, start + 1);
	if (end == std::string::npos) {
		return text.substr(start);
	}
	return text.substr(start, end - start);
}

} /* namespace */

TEST_CASE("Settings exposes the rebuilt Input tab instead of the old Controls tab",
          "[input][settings][static][c3819]")
{
	const std::string menu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	REQUIRE(!menu.empty());

	requireContains(menu, "ImGui::BeginTabItem(\"Input\"");
	requireContains(menu, "renderSettingsInput(scale)");
	requireContains(menu, "##settings_scroll_input");
	requireNotContains(menu, "ImGui::BeginTabItem(\"Controls\"");
	requireNotContains(menu, "renderSettingsControls(scale)");
}

TEST_CASE("Settings Input tab uses one scheme selector and one device selector",
          "[input][settings][static][c3819]")
{
	const std::string menu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	REQUIRE(!menu.empty());

	const std::string renderer = sliceBetween(menu, "static void renderSettingsInput", "static void renderSettingsGame");
	REQUIRE(!renderer.empty());

	requireContains(renderer, "renderInputProfilesSection();");
	requireContains(renderer, "renderInputDevicesSection();");
	requireContains(renderer, "renderInputBindingsSection(scale);");
	requireContains(renderer, "renderControlsGlobalMouse();");
    requireNotContains(renderer, "handleCaptureInput();");
	requireNotContains(renderer, "##controls_imc_tabs");
	requireNotContains(renderer, "renderImcTabBody");
}

TEST_CASE("Settings Input tab names controllers and assigns binding profiles",
          "[input][settings][static][c3819]")
{
	const std::string menu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	const std::string inputH = readTextFile("port/include/input.h");
	const std::string inputC = readTextFile("port/src/input.c");
	REQUIRE(!menu.empty());
	REQUIRE(!inputH.empty());
	REQUIRE(!inputC.empty());

	requireContains(menu, "inputGetConnectedInputDevices(ids)");
	requireContains(menu, "inputGetConnectedInputDeviceStableKey(ids[i])");
	requireContains(menu, "InputUiDeviceRule");
	requireContains(menu, "rule->alias");
	requireContains(menu, "inputUiProfileCombo(\"##profile\"");
	requireContains(menu, "inputProfilesSetDeviceRulesIni(rules)");

	requireContains(inputH, "inputGetConnectedInputDevices");
	requireContains(inputH, "inputGetConnectedInputDeviceStableKey");
	requireContains(inputH, "inputProfilesSetDeviceRulesIni");
	requireContains(inputC, "Input.ProfileNames");
	requireContains(inputC, "Input.DeviceProfiles");
	requireContains(inputC, "SDL_JoystickNameForIndex");
	requireContains(inputC, "actionmapClassifyDeviceName");
}

TEST_CASE("Input profiles save and load real action-map bindings",
          "[input][settings][static][c3819]")
{
	const std::string menu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	const std::string actionmapH = readTextFile("port/include/actionmap.h");
	const std::string actionmapC = readTextFile("port/src/actionmap.cpp");
	REQUIRE(!menu.empty());
	REQUIRE(!actionmapH.empty());
	REQUIRE(!actionmapC.empty());

	requireContains(menu, "actionmapSaveProfileFile(path)");
	requireContains(menu, "actionmapLoadProfileFileAsCurrent(path, profile)");
	requireContains(menu, "$S/input-profiles/profile%d.ini");

	requireContains(actionmapH, "actionmapSaveProfileFile");
	requireContains(actionmapH, "actionmapLoadProfileFile");
	requireContains(actionmapC, "s_ProfileStore.loadProfile(actionmapProfileRegistry()");
	requireContains(actionmapC, "fsCreateDir(\"$S/input-profiles\")");
	requireContains(actionmapC, "$S/input-bindings.ini");
	requireContains(actionmapC, "s_ProfileStore.initialize(actionmapProfileRegistry()");
	requireContains(actionmapC, "actionmap_profile::write(registry");
    const std::string profiles = sliceBetween(menu, "static void inputUiLoadProfile", "static void renderInputDevicesSection");
    requireNotContains(profiles, "inputProfilesSetActive(");
    requireContains(profiles, "inputUiProfileCombo(\"Saved profile\", &selected)");
    requireContains(menu, "if (!actionmapSaveBinds()) return false;");
    requireContains(menu, "inputUiBindingPersistenceStatus();");
}

TEST_CASE("Settings Input binding table stays on the action map capture path",
          "[input][settings][static][c3819]")
{
	const std::string menu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	REQUIRE(!menu.empty());

	const std::string bindings = sliceBetween(menu, "static void renderInputBindingsSection", "static void renderSettingsInput");
	REQUIRE(!bindings.empty());

	requireContains(bindings, "s_ImcTabs[s_InputSchemeIndex]");
	requireContains(bindings, "PdCombo(\"Input\"");
	requireContains(bindings, "resetTabDeviceToDefaults(tab, s_InputDeviceColumn)");
	requireContains(bindings, "renderBindTable(s_InputDeviceColumn");
	requireNotContains(bindings, "renderControllerVisualMapper");

	const std::string bindTable = sliceBetween(menu, "static void renderBindTable", "static void resetTabDeviceToDefaults");
	REQUIRE(!bindTable.empty());
	requireContains(bindTable, "s_BindSearch");
	requireContains(bindTable, "ImGui::InputTextWithHint");
	requireContains(bindTable, "stringIContains(s_BindableActions[row].name, s_BindSearch)");
}

TEST_CASE("Settings Input surfaces every user-bindable action",
          "[input][settings][static][b967]")
{
	const std::string menu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	const std::string actionmapH = readTextFile("port/include/actionmap.h");
	REQUIRE(!menu.empty());
	REQUIRE(!actionmapH.empty());

	const std::string enumBlock =
		sliceBetween(actionmapH, "typedef enum InputAction {", "ACTION_COUNT");
	const std::string table =
		sliceBetween(menu, "static const BindableAction s_BindableActions[] = {",
		             "#define NUM_BINDABLE_ACTIONS");
	REQUIRE(!enumBlock.empty());
	REQUIRE(!table.empty());

	const std::regex enumRow("^\\s*(ACTION_[A-Z0-9_]+)(?:\\s*=.*)?\\s*,");
	const std::regex tableRow("^\\s*\\{\\s*(ACTION_[A-Z0-9_]+)\\s*,");
	std::set<std::string> declared;
	std::set<std::string> surfaced;
	std::string line;
	std::smatch match;
	std::istringstream enumLines(enumBlock);
	while (std::getline(enumLines, line)) {
		if (std::regex_search(line, match, enumRow)) {
			declared.insert(match[1].str());
		}
	}
	std::istringstream tableLines(table);
	while (std::getline(tableLines, line)) {
		if (std::regex_search(line, match, tableRow)) {
			surfaced.insert(match[1].str());
		}
	}

	/* These are derived continuous channels, not independently bindable
	 * controls. Their producers are the surfaced digital direction rows and
	 * the Settings -> Input global stick-layout/deadzone/sensitivity UI. */
	const char *derivedAnalog[] = {
		"ACTION_AXIS_MOVE_X",
		"ACTION_AXIS_MOVE_Y",
		"ACTION_AXIS_AIM_X",
		"ACTION_AXIS_AIM_Y",
	};
	for (const char *action : derivedAnalog) {
		REQUIRE(declared.erase(action) == 1);
	}

	INFO("Every non-derived InputAction must have at least one Settings -> Input row");
	REQUIRE(declared == surfaced);
	requireContains(menu, "renderControlsGlobalSticks();");
	requireContains(menu, "Move stick");
	requireContains(menu, "Look stick");
	requireContains(menu, "Move deadzone");
	requireContains(menu, "Look deadzone");
}

TEST_CASE("Settings capture previews every input before committing and owns parent navigation",
          "[input][settings][capture][static]")
{
    const std::string menu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    REQUIRE(!menu.empty());
    const std::string capture = sliceBetween(menu, "static void handleCaptureInput(void)",
                                            "static void renderInputCaptureModal(void)");
    requireContains(capture, "inputGetLastKey()");
    requireContains(capture, "s_CaptureCandidate = (u32)newKey;");
    requireNotContains(capture, "actionmapBind(");
    requireNotContains(capture, "newKey == PD_VK_ESCAPE");

    const std::string modal = sliceBetween(menu, "static void renderInputCaptureModal(void)",
                                          "/* Controller diagram:");
    requireContains(modal, "ImGui::OpenPopup(popupId)");
    requireContains(modal, "ImGui::BeginPopupModal(popupId");
    requireContains(modal, "handleCaptureInput();");
    requireContains(modal, "else if (ImGui::IsItemActive())");
    REQUIRE(modal.find("if (ImGui::Button(\"Cancel\"))") < modal.find("handleCaptureInput();"));
    REQUIRE(modal.find("else if (ImGui::IsItemActive())") < modal.find("handleCaptureInput();"));
    requireContains(modal, "ImGui::Button(\"Try Again\")");
    requireContains(modal, "ImGui::Button(\"Apply\") && s_CaptureImc && s_CaptureCandidate");
    requireContains(modal, "actionmapBind(s_CaptureImc, 0, s_CaptureAction, s_CaptureBind, s_CaptureCandidate)");
    requireContains(modal, "ready && pdguiMenuCancelPressed()");
    requireContains(menu, "!s_CaptureActive && pdguiMenuTabPrevPressed()");
    requireContains(menu, "!s_CaptureActive && pdguiMenuTabNextPressed()");
    requireContains(menu, "if (oldView == 2 && view != 2) inputUiCancelCapture();");
    requireContains(menu, "s_CaptureActive && !menupoolIsActive(MENU_TYPE_MAIN_SETTINGS_VIEW)");
    requireContains(menu, "&& !menupoolIsActive(MENU_TYPE_CI_OPTIONS)");

    /* Both ordinary Main Menu Settings and the legacy CI redirect render the
     * modal from their owning window after the scroll child has ended. */
    const size_t first = menu.find("\n        renderInputCaptureModal();");
    REQUIRE(first != std::string::npos);
    REQUIRE(menu.find("\n    renderInputCaptureModal();", first + 1) != std::string::npos);
}

TEST_CASE("Settings shows every trigger and supports focused clearing without slot eviction",
          "[input][settings][capture][static]")
{
    const std::string menu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
    REQUIRE(!menu.empty());
    const std::string table = sliceBetween(menu, "static void renderBindTable", "static void resetTabDeviceToDefaults");
    requireContains(table, "1 + ACTIONMAP_MAX_TRIGGERS");
    requireContains(table, "mkbVKs[ACTIONMAP_MAX_TRIGGERS]");
    requireContains(table, "ctrlVKs, ctrlSlots, &ctrlCount, ACTIONMAP_MAX_TRIGGERS");
    requireContains(table, "binding < ACTIONMAP_MAX_TRIGGERS");

    const std::string freeSlot = sliceBetween(menu, "static s32 findFreeTriggerSlot", "/* Trigger slot index");
    requireContains(freeSlot, "return -1;");
    requireNotContains(freeSlot, "return 0;");
    const std::string cell = sliceBetween(menu, "static void renderBindButton", "/* Find the first row");
    requireContains(cell, "ImGui::BeginDisabled(!hasSlot || s_CaptureActive)");
    requireContains(cell, "pdguiMenuSecondaryPressed() || pdguiMenuDeletePressed()");
    requireContains(cell, "!s_CaptureActive && (ImGui::IsItemClicked(ImGuiMouseButton_Right) || focusedClear)");
    requireNotContains(cell, "useSlot = (otherSlot == 0) ? 1 : 0");
}

TEST_CASE("Vehicle direct-use and Forge camera keys have distinct production contexts",
          "[input][vehicle][forge][static][b967]")
{
	const std::string actionmap = readTextFile("port/src/actionmap.cpp");
	const std::string bondmove = readTextFile("src/game/bondmove.c");
	REQUIRE(!actionmap.empty());
	REQUIRE(!bondmove.empty());

	const std::string gameplay =
		sliceBetween(actionmap, "static void setupGameplayDefaults", "static void setupMissionDefaults");
	const std::string forge =
		sliceBetween(actionmap, "static void setupForgeDefaults", "static void setupObserverDefaults");
	REQUIRE(!gameplay.empty());
	REQUIRE(!forge.empty());

	requireContains(gameplay, "addBind(imc, ACTION_VEHICLE_USE,    VKL_E)");
	requireNotContains(gameplay, "addBind(imc, ACTION_FORGE_ASCEND,    VKL_E)");
	requireNotContains(gameplay, "addBind(imc, ACTION_FORGE_DESCEND,   VKL_Q)");
	requireContains(forge, "addBind(imc, ACTION_FORGE_ASCEND,           VKL_E)");
	requireContains(forge, "addBind(imc, ACTION_FORGE_DESCEND,          VKL_Q)");
	requireContains(bondmove, "actionPressed(actionPlayer, ACTION_VEHICLE_USE)");
	requireContains(bondmove, "bmoveHandleActivate();");
}
