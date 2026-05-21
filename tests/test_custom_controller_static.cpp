/*
 * test_custom_controller_static.cpp -- custom/accessibility controller pins.
 *
 * @SYNC port/include/actionmap.h
 * @SYNC port/src/actionmap.cpp
 * @SYNC port/src/input.c
 * @SYNC port/fast3d/pdgui_glyphs.cpp
 * @SYNC port/src/presence.c
 * @SYNC port/fast3d/pdgui_friends.cpp
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

} /* namespace */

TEST_CASE("custom controller input classes are public and privacy-safe",
          "[input][custom-controller][static][c3816]")
{
	const std::string header = readTextFile("port/include/actionmap.h");
	REQUIRE(!header.empty());

	requireContains(header, "ACTIONMAP_INPUT_CLASS_MKB");
	requireContains(header, "ACTIONMAP_INPUT_CLASS_CONTROLLER");
	requireContains(header, "ACTIONMAP_INPUT_CLASS_CUSTOM");
	requireContains(header, "ACTIONMAP_INPUT_CLASS_ACCESSIBILITY");
	requireContains(header, "ACTIONMAP_INPUT_CLASS_HOTAS");
	requireContains(header, "ACTIONMAP_INPUT_CLASS_HOSAS");
	requireContains(header, "actionmapGetLastInputClass");
	requireContains(header, "actionmapInputClassLabel");
	requireContains(header, "actionmapClassifyDeviceName");
	requireContains(header, "do not expose GUID/vendor");
}

TEST_CASE("raw joystick devices route through the action map bridge",
          "[input][custom-controller][static][c3816]")
{
	const std::string actionmap = readTextFile("port/src/actionmap.cpp");
	const std::string input = readTextFile("port/src/input.c");
	REQUIRE(!actionmap.empty());
	REQUIRE(!input.empty());

	requireContains(actionmap, "SDL_JOYBUTTONDOWN");
	requireContains(actionmap, "SDL_JOYAXISMOTION");
	requireContains(actionmap, "SDL_GameControllerFromInstanceID(ev->jbutton.which)");
	requireContains(actionmap, "SDL_GameControllerFromInstanceID(ev->jaxis.which)");
	requireContains(actionmap, "JOY_BTN(0, (u32)ev->jbutton.button)");
	requireContains(actionmap, "handleAxisDigital(0, ev->jaxis.value");
	requireContains(actionmap, "handleTriggerDigital(0, ev->jaxis.value");
	requireContains(actionmap, "s_LastInputClass");

	requireContains(input, "rawJoysticks");
	requireContains(input, "inputOpenRawJoystick");
	requireContains(input, "SDL_JOYBUTTONDOWN");
	requireContains(input, "SDL_JOYAXISMOTION");
	requireContains(input, "SDL_GameControllerFromInstanceID(event->jbutton.which)");
	requireContains(input, "SDL_GameControllerFromInstanceID(event->jaxis.which)");
}

TEST_CASE("custom-class glyphs do not pretend to be Xbox buttons",
          "[input][custom-controller][static][c3816]")
{
	const std::string glyphs = readTextFile("port/fast3d/pdgui_glyphs.cpp");
	REQUIRE(!glyphs.empty());

	requireContains(glyphs, "const s32 inputClass = actionmapGetLastInputClass()");
	requireContains(glyphs, "inputClass != ACTIONMAP_INPUT_CLASS_CONTROLLER");
	requireContains(glyphs, "inputClass != ACTIONMAP_INPUT_CLASS_MKB");
	requireContains(glyphs, "Btn%u");
	requireContains(glyphs, "Axis1-");
	requireContains(glyphs, "Axis6+");
}

TEST_CASE("social presence carries privacy-safe input class",
          "[input][custom-controller][static][c3816]")
{
	const std::string presenceH = readTextFile("port/include/presence.h");
	const std::string presenceC = readTextFile("port/src/presence.c");
	const std::string friends = readTextFile("port/fast3d/pdgui_friends.cpp");
	REQUIRE(!presenceH.empty());
	REQUIRE(!presenceC.empty());
	REQUIRE(!friends.empty());

	requireContains(presenceH, "input_class");
	requireContains(presenceC, "PRESENCE_VERSION          3");
	requireContains(presenceC, "pd-presence-v3");
	requireContains(presenceC, "actionmapGetLastInputClass");
	requireContains(presenceC, "input_class");
	requireContains(friends, "Input: %s");
	requireContains(friends, "actionmapInputClassLabel(peer->input_class)");
}
