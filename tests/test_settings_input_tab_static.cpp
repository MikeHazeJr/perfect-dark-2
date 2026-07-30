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
	requireContains(renderer, "handleCaptureInput();");
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
	requireContains(menu, "actionmapLoadProfileFile(path)");
	requireContains(menu, "$S/input-profiles/profile%d.ini");

	requireContains(actionmapH, "actionmapSaveProfileFile");
	requireContains(actionmapH, "actionmapLoadProfileFile");
	requireContains(actionmapC, "buildContextBindStr");
	requireContains(actionmapC, "fsCreateDir(\"$S/input-profiles\")");
	requireContains(actionmapC, "context.P0.ActionName=VK,VK");
	requireContains(actionmapC, "parseBindStr(ctx, 0, action, binds)");
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
