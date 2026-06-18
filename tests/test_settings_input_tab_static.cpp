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
