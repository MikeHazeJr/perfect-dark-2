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

TEST_CASE("mission flow: the phase reaches the graph and a graph can end the mission",
          "[mission][objectives][missionflow]")
{
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string modules = readSource("port/src/mission_graph_modules.c");
	const std::string objectives = readSource("src/game/objectives.c");
	const std::string player = readSource("src/game/player.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// The phase event is delivered once per change, only from the
	// objectives check, after the objectives' own events.
	REQUIRE(runtime.find("if (phase < 0 || phase == s_ActiveScenarioGraphs.mission_phase_delivered) {") !=
		std::string::npos);
	REQUIRE(countOf(objectives, "scenarioSourceMissionGraphDeliverPhaseEvent();") == 1);
	REQUIRE(countOf(runtime, "scenarioSourceMissionGraphDeliverPhaseEvent()") == 0);

	// og.mission.end_*: the host decides, the first request wins, the outcome
	// is then the graph's, and the stage ends outside the walk.
	REQUIRE(modules.find("{ \"og.mission.end_failed\", s_runEndMissionFailed },") != std::string::npos);
	REQUIRE(modules.find("{ \"og.mission.end_completed\", s_runEndMissionCompleted },") != std::string::npos);
	REQUIRE(modules.find("if (g_NetMode == NETMODE_CLIENT) {") != std::string::npos);
	REQUIRE(modules.find("if (completed && (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0)) {") !=
		std::string::npos);
	REQUIRE(runtime.find("if (s_ActiveScenarioGraphs.mission_forced_outcome) {\n\t\tsysLogPrintf(LOG_NOTE,\n"
		"\t\t\t\"MISSION.GRAPH: og.mission.end_%s ignored:") != std::string::npos);
	REQUIRE(objectives.find("if (scenarioSourceMissionGraphForcedOutcome(&forced_complete)) {\n"
		"\t\treturn forced_complete != 0;") != std::string::npos);
	REQUIRE(player.find("if (scenarioSourceMissionGraphTakePendingEnd() && debugAllowEndLevel()) {\n"
		"\t\t\tfunc0000e990();") != std::string::npos);
	// Never after the end: the runtime refuses the program, the checker too.
	REQUIRE(runtime.find("\"og.mission.end_* runs on an objective or phase event, not after the mission ended\"") !=
		std::string::npos);
	REQUIRE(conformance.find("MISSION_GRAPH_AFTER_END_EVENTS = {\"event.mission.completed\", \"event.mission.continue\"}") !=
		std::string::npos);
	REQUIRE(conformance.find("MISSION_GRAPH_EXEC_GATES = {\"gate.objective.status\", \"gate.mission.phase\",") !=
		std::string::npos);
}

TEST_CASE("mission flow: every campaign fixture requires the outcome parity line",
          "[mission][objectives][missionflow][smoke-fixtures]")
{
	const struct { const char *path; int missions; } fixtures[] = {
		{ "tools/smoke-verify/tests/auto_campaign_release.json", 17 },
		{ "tools/smoke-verify/tests/auto_campaign_release_pa.json", 17 },
		{ "tools/smoke-verify/tests/auto_campaign_release_sa.json", 17 },
		{ "tools/smoke-verify/tests/auto_campaign_soak.json", 17 },
		{ "tools/smoke-verify/tests/auto_campaign_specials.json", 4 },
	};
	for (const auto &fixture : fixtures) {
		INFO(fixture.path);
		const std::string text = readSource(fixture.path);
		const std::string count = "{ \"pattern\": \"CAMPAIGN\\\\.AUTO: outcome parity probe stage_id='base:\", \"min\": " +
			std::to_string(fixture.missions) + ", \"max\": " + std::to_string(fixture.missions) + " }";
		CHECK(text.find(count) != std::string::npos);
		CHECK(text.find("\"outcome parity probe: \",") != std::string::npos);
	}
}

TEST_CASE("mission flow and the AI list bank: the release and special-assignment fixtures pin what play showed",
          "[mission][missionflow][globalailists][smoke-fixtures]")
{
	// CORE's turns on 2026-11-16: every mission delivers its active and
	// complete phases once, and every stage runs the bank's global lists.
	const struct { const char *path; int missions; } fixtures[] = {
		{ "tools/smoke-verify/tests/auto_campaign_release.json", 17 },
		{ "tools/smoke-verify/tests/auto_campaign_specials.json", 4 },
	};
	for (const auto &fixture : fixtures) {
		INFO(fixture.path);
		const std::string text = readSource(fixture.path);
		const std::string n = std::to_string(fixture.missions);
		CHECK(text.find("phase=active\", \"min\": " + n + ", \"max\": " + n + " }") != std::string::npos);
		CHECK(text.find("phase=complete\", \"min\": " + n + ", \"max\": " + n + " }") != std::string::npos);
		CHECK(text.find("runs the 46 global lists of 'base:ai_global'\", \"min\": " + n + " }") != std::string::npos);
	}
}
