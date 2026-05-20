/*
 * tests/test_debug_campaign_complete_hotkey.cpp
 *
 * Static guards for the dev F6 campaign-complete path. F6 historically
 * froze MP bots; in solo campaign it must instead force the current loaded
 * mission's objectives complete and drive the normal mission-success
 * end-stage flow. The freeze fallback is dev-only in production code and
 * must be restricted to Combat Simulator.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

static std::string read_text_file_debug_hotkey(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file.good());

	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

static std::string function_block_debug_hotkey(const std::string &text, const std::string &name)
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

TEST_CASE("F6 completes loaded solo campaign objectives before Combat Sim bot-freeze fallback",
          "[debug][campaign][f6][static]")
{
	const std::string sched = read_text_file_debug_hotkey("port/src/pdsched.c");
	const std::string helper = function_block_debug_hotkey(
		sched, "static s32 schedDebugCompleteCampaignObjectivesIfActive");
	const std::string freeze_helper = function_block_debug_hotkey(
		sched, "static s32 schedDebugCanFreezeCombatSimBots");
	const std::string end_frame = function_block_debug_hotkey(sched, "void schedEndFrame");

	REQUIRE(helper.find("g_NetMode == NETMODE_NONE") != std::string::npos);
	REQUIRE(helper.find("!g_Vars.normmplayerisrunning") != std::string::npos);
	REQUIRE(helper.find("STAGE_IS_GAMEPLAY(g_Vars.stagenum)") != std::string::npos);
	REQUIRE(helper.find("objectiveGetCount() > 0") != std::string::npos);
	REQUIRE(helper.find("objectivesDebugCompleteCurrentMission()") != std::string::npos);
	REQUIRE(helper.find("g_Vars.bond->isdead = false") != std::string::npos);
	REQUIRE(helper.find("g_Vars.bond->aborted = false") != std::string::npos);
	REQUIRE(helper.find("mainEndStage()") != std::string::npos);
	REQUIRE(freeze_helper.find("g_Vars.normmplayerisrunning") != std::string::npos);

	const size_t action = end_frame.find("actionPressed(0, ACTION_DEBUG_BOT_FREEZE)");
	const size_t complete = end_frame.find("schedDebugCompleteCampaignObjectivesIfActive()", action);
	const size_t freeze_gate = end_frame.find("schedDebugCanFreezeCombatSimBots()", action);
	const size_t freeze = end_frame.find("botToggleUpdatesDisabled()", action);
	const size_t dev_gate = end_frame.rfind("#if defined(PD_DEV_BUILD)", action);

	REQUIRE(action != std::string::npos);
	REQUIRE(complete != std::string::npos);
	REQUIRE(freeze_gate != std::string::npos);
	REQUIRE(freeze != std::string::npos);
	REQUIRE(dev_gate != std::string::npos);
	REQUIRE(dev_gate < action);
	REQUIRE(complete < freeze);
	REQUIRE(freeze_gate < freeze);
}

TEST_CASE("forced current-mission objective completion resets with objectives",
          "[debug][campaign][objectives][static]")
{
	const std::string objectives = read_text_file_debug_hotkey("src/game/objectives.c");
	const std::string reset = read_text_file_debug_hotkey("src/game/objectivesreset.c");

	const std::string check = function_block_debug_hotkey(objectives, "s32 objectiveCheck");
	const std::string complete = function_block_debug_hotkey(
		objectives, "s32 objectivesDebugCompleteCurrentMission");
	const std::string reset_fn = function_block_debug_hotkey(reset, "void objectivesReset");

	REQUIRE(objectives.find("bool g_DebugForceCompleteCurrentMissionObjectives = false") != std::string::npos);
	REQUIRE(check.find("g_DebugForceCompleteCurrentMissionObjectives") != std::string::npos);
	REQUIRE(check.find("return OBJECTIVE_COMPLETE") != std::string::npos);
	REQUIRE(complete.find("g_DebugForceCompleteCurrentMissionObjectives = true") != std::string::npos);
	REQUIRE(complete.find("g_ObjectiveStatuses[i] = OBJECTIVE_COMPLETE") != std::string::npos);
	REQUIRE(complete.find("objectivesCheckAll()") != std::string::npos);
	REQUIRE(reset_fn.find("g_DebugForceCompleteCurrentMissionObjectives = false") != std::string::npos);
}

TEST_CASE("ImGui endscreen routes final campaign advance to credits",
          "[debug][campaign][f6][endscreen][static]")
{
	const std::string bridge = read_text_file_debug_hotkey("port/fast3d/pdgui_bridge.c");
	const std::string endscreen = read_text_file_debug_hotkey("port/fast3d/pdgui_menu_endscreen.cpp");

	const std::string next = function_block_debug_hotkey(
		bridge, "void pdguiEndscreenNextMission");
	const std::string has_next = function_block_debug_hotkey(
		bridge, "s32 pdguiEndscreenHasNextMission");
	const std::string label = function_block_debug_hotkey(
		bridge, "const char *pdguiEndscreenNextMissionLabel");
	const std::string render = function_block_debug_hotkey(
		endscreen, "static void renderSoloEndscreen");

	const size_t final_stage = next.find("g_Vars.stagenum == STAGE_SKEDARRUINS");
	const size_t credits = next.find("endscreenContinue(2)", final_stage);
	const size_t advance = next.find("endscreenAdvance()");

	REQUIRE(final_stage != std::string::npos);
	REQUIRE(credits != std::string::npos);
	REQUIRE(advance != std::string::npos);
	REQUIRE(credits < advance);
	REQUIRE(next.find("sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL") != std::string::npos);
	REQUIRE(next.find("!pdguiEndscreenHasNextMission()") != std::string::npos);

	REQUIRE(has_next.find("g_Vars.stagenum == STAGE_SKEDARRUINS") != std::string::npos);
	REQUIRE(has_next.find("SOLOSTAGEINDEX_SKEDARRUINS") != std::string::npos);
	REQUIRE(has_next.find("NUM_SOLOSTAGES") == std::string::npos);

	REQUIRE(label.find("\"Credits\"") != std::string::npos);
	REQUIRE(label.find("\"Next Mission\"") != std::string::npos);
	REQUIRE(render.find("pdguiEndscreenNextMissionLabel()") != std::string::npos);
	REQUIRE(render.find("pdguiActionBarButton(nextLabel") != std::string::npos);
}
