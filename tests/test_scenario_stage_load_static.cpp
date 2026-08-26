/*
 * tests/test_scenario_stage_load_static.cpp
 *
 * Static guards for the MP scenario stage-load boundary. The live reset path
 * is too coupled for pd-tests, so this pins the architectural contract that
 * solo mission loads must not parse MP scenario intro commands, while MP
 * loads remain keyed to the match setup stage for drop-in/drop-out.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string read_text_file_scenario_stage(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file.good());

	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

static std::string function_block_scenario_stage(const std::string &text, const std::string &name)
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

TEST_CASE("scenario stage load reset is match-stage owned",
          "[scenario][stage-load][static]")
{
	const std::string scenarios = read_text_file_scenario_stage("src/game/mplayer/scenarios.c");
	const std::string helper = function_block_scenario_stage(scenarios, "static bool scenarioStageLoadOwnsMpSetup");
	const std::string wrapper = function_block_scenario_stage(scenarios, "void scenarioResetForStageLoad");

	REQUIRE(helper.find("scenarioIndexIsValid(g_MpSetup.scenario)") != std::string::npos);
	REQUIRE(helper.find("(s32)g_MpSetup.stagenum == stagenum") != std::string::npos);
	REQUIRE(wrapper.find("scenarioStageLoadOwnsMpSetup(stagenum)") != std::string::npos);
	REQUIRE(wrapper.find("scenarioReset();") != std::string::npos);

	REQUIRE(wrapper.find("!g_Vars.normmplayerisrunning") == std::string::npos);
	REQUIRE(wrapper.find("g_Vars.normmplayerisrunning == false") == std::string::npos);
}

TEST_CASE("lvReset uses scenario stage-load wrapper",
          "[scenario][stage-load][static]")
{
	const std::string lv = read_text_file_scenario_stage("src/game/lv.c");
	const std::string reset = function_block_scenario_stage(lv, "bool lvReset");

	const size_t setup_load = reset.find("setupLoadFiles(stagenum)");
	const size_t scenario_load = reset.find("scenarioResetForStageLoad(stagenum)", setup_load);
	const size_t raw_reset = reset.find("scenarioReset();", setup_load);
	const size_t vars_reset = reset.find("varsReset()", scenario_load);

	REQUIRE(setup_load != std::string::npos);
	REQUIRE(scenario_load != std::string::npos);
	REQUIRE(raw_reset == std::string::npos);
	REQUIRE(vars_reset != std::string::npos);
	REQUIRE(setup_load < scenario_load);
	REQUIRE(scenario_load < vars_reset);
}
