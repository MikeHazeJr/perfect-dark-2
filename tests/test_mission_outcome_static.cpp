/*
 * tests/test_mission_outcome_static.cpp -- mission flow slice 1
 * (context/designs/modding/mission-flow.md): required objectives are data on
 * the mission's objectives.json rows, the outcome aggregates the required
 * ones, and the campaign runner compares the outcome with the original rule.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

size_t countOf(const std::string &text, const std::string &needle)
{
	size_t n = 0;
	for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
		n++;
	}
	return n;
}

} // namespace

TEST_CASE("mission flow: required objectives are mission data and decide the outcome",
          "[mission][objectives][missionflow]")
{
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string header = readSource("port/include/scenario_source_runtime.h");
	const std::string objectives = readSource("src/game/objectives.c");
	const std::string runner = readSource("port/src/autocampaign.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// The runtime reads "required" on a row of kind objective only, as a
	// boolean, and answers per objective; an objective it does not know is
	// required (it stays incomplete, so the mission cannot complete).
	REQUIRE(runtime.find("s32 optional; /* mission-flow.md: the row's \"required\": false */") != std::string::npos);
	REQUIRE(runtime.find("if (strcmp(kind, \"objective\") != 0 || objective_count <= 0 ||\n"
		"\t\t\t!s_jsonReadBoolValue(object_text, end, \"required\", &required)) {") != std::string::npos);
	REQUIRE(runtime.find("objectives[objective_count - 1].optional = !required;") != std::string::npos);
	REQUIRE(runtime.find("if (!s_missionObjectiveReadRequired(object_text, objective_id,") != std::string::npos);
	REQUIRE(runtime.find("\treturn !s_ActiveScenarioGraphs.mission_objectives[index].optional;") != std::string::npos);
	REQUIRE(header.find("s32 scenarioSourceMissionObjectiveRequired(s32 index);") != std::string::npos);

	// The one outcome seam and the phase aggregate both leave an optional
	// objective out; nothing else in objectives.c aggregates objectives.
	REQUIRE(objectives.find("if ((1 << lvGetDifficulty() & diffbits)\n"
		"\t\t\t\t&& scenarioSourceMissionObjectiveRequired(i)) {") != std::string::npos);
	REQUIRE(countOf(objectives, "scenarioSourceMissionObjectiveRequired(i)") == 2);

	// The campaign runner compares the graph's outcome with the original rule
	// on every mission it probes.
	REQUIRE(runner.find("if (scenarioSourceMissionObjectiveRequired(i) && graph != OBJECTIVE_COMPLETE) {") !=
		std::string::npos);
	REQUIRE(runner.find("\"outcome parity probe: '%s' graph=%s native=%s\"") != std::string::npos);
	REQUIRE(runner.find("ACLOG(\"outcome parity probe stage_id='%s' outcome=%s\",") != std::string::npos);

	// The checker allows "required" on the mission's objective rows only and
	// refuses it on a graph node (one source).
	REQUIRE(conformance.find("required needs an objective row and true or false") != std::string::npos);
	REQUIRE(conformance.find("required belongs to the mission's objectives.json") != std::string::npos);
	REQUIRE(conformance.find("objective's objectives.json row (mission-flow.md)") != std::string::npos);
}
