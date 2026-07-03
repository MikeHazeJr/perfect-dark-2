/*
 * tests/test_debug_credits_button.cpp
 *
 * Static guard for the dev Settings -> Debug Credits shortcut. This exists
 * so the rendering-regression playtest path stays reachable without forcing
 * a full campaign-complete run.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

static std::string read_text_file_debug_credits(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file.good());

	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

static std::string function_block_debug_credits(const std::string &text, const std::string &name)
{
	const std::string needle = name + "(";
	const size_t signature = text.find(needle);
	REQUIRE(signature != std::string::npos);

	const size_t open = text.find('{', signature);
	REQUIRE(open != std::string::npos);

	size_t depth = 0;
	for (size_t i = open; i < text.size(); ++i) {
		if (text[i] == '{') {
			++depth;
		} else if (text[i] == '}') {
			--depth;
			if (depth == 0) {
				return text.substr(signature, i - signature + 1);
			}
		}
	}

	FAIL("unterminated function block");
	return std::string();
}

} /* namespace */

TEST_CASE("Settings Debug tab exposes a net-safe Credits shortcut",
          "[debug][credits][menu][static]")
{
	const std::string mainmenu =
		read_text_file_debug_credits("port/fast3d/pdgui_menu_mainmenu.cpp");
	const std::string render =
		function_block_debug_credits(mainmenu, "static void renderSettingsDebug");

	REQUIRE(mainmenu.find("#define GRID_STAGE_CREDITS     0x5c") != std::string::npos);
	REQUIRE(mainmenu.find("void mainChangeToStage(s32 stagenum);") != std::string::npos);
	REQUIRE(mainmenu.find("extern s32 g_NetMode;") != std::string::npos);
	/* Shared normal-scroll credits launcher declared for both entry points. */
	REQUIRE(mainmenu.find("void creditsEnterNormalScroll(void);") != std::string::npos);

	REQUIRE(render.find("\"Scene Shortcuts\"") != std::string::npos);
	REQUIRE(render.find("g_NetMode == NETMODE_NONE") != std::string::npos);
	REQUIRE(render.find("ImGui::Button(\"Go to Credits\"") != std::string::npos);
	/* The debug shortcut now uses the shared seeding launcher (numplayers/DIFF_A)
	 * instead of a bare mainChangeToStage, matching the main-menu button. */
	REQUIRE(render.find("creditsEnterNormalScroll()") != std::string::npos);
	REQUIRE(render.find("Disabled during netplay") != std::string::npos);
}

TEST_CASE("Main menu screen exposes a net-safe Credits button",
          "[debug][credits][menu][static]")
{
	const std::string mainmenu =
		read_text_file_debug_credits("port/fast3d/pdgui_menu_mainmenu.cpp");

	/* The primary main-menu screen (not just the debug Settings tab) offers a
	 * Credits entry that launches the normal scrolling credits, disabled in
	 * netplay so a local stage change cannot desync a match. */
	const size_t creditsBtn = mainmenu.find("PdButton(\"Credits\"");
	REQUIRE(creditsBtn != std::string::npos);

	/* The Credits button block calls the shared launcher and guards on
	 * g_NetMode == NETMODE_NONE. */
	const size_t blockStart = mainmenu.rfind("g_NetMode == NETMODE_NONE", creditsBtn);
	REQUIRE(blockStart != std::string::npos);
	const size_t launcher = mainmenu.find("creditsEnterNormalScroll()", creditsBtn);
	REQUIRE(launcher != std::string::npos);
	/* Launcher call is close to the button (same block), not elsewhere. */
	REQUIRE(launcher - creditsBtn < 400);
}
