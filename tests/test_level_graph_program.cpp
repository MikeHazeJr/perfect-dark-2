/*
 * tests/test_level_graph_program.cpp -- a level graph's own program
 * (context/designs/modding/object-events.md, slice 1): the runner compiles
 * only a level graph's event.*, gate.* and og.* nodes (its AI lists, setup
 * and rooms are level data, however many), refuses an execution edge into
 * level data, and runs volume events on it; plus pins on the runtime, which
 * compiles it for every stage and delivers its volume events from lvTick,
 * and on conformance.
 */

#include "catch.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mission_graph_runner.h"

namespace {

s32 knownModule(const char *kind)
{
	return std::strncmp(kind, "og.test.", 8) == 0;
}

s32 runModule(const char *kind, const char *node_id, const mission_graph_event_t *event, void *user)
{
	(void)kind;
	(void)event;
	static_cast<std::vector<std::string> *>(user)->push_back(node_id);
	return 1;
}

mission_graph_program_t *compileLevel(const std::string &json, std::string *error = nullptr)
{
	char err[256] = {};
	mission_graph_program_t *program = missionGraphProgramCompileLevel(json.c_str(), json.size(),
		knownModule, err, sizeof(err));
	if (error) *error = err;
	return program;
}

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	std::string text = out.str();
	text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
	return text;
}

// A level graph as the extractor writes it: the one inert scenario.load
// event, its data nodes (here more than MISSION_GRAPH_MAX_NODES) and their
// links, plus the program an author added: a volume event that runs a module.
std::string levelGraph(int data_nodes, const std::string &extra_edges = "")
{
	std::string json = R"({ "schema": "pd2.scenario.graph.v1", "nodes": [
	{ "id": "scenario.load", "kind": "event.scenario.load" })";
	for (int i = 0; i < data_nodes; i++) {
		json += ",\n\t{ \"id\": \"scenario.ai.list_" + std::to_string(i) +
			"\", \"kind\": \"scenario.ai.command\" }";
	}
	json += R"(,
	{ "id": "vault_in", "kind": "event.volume.entered", "volume": "volume_vault" },
	{ "id": "open_vault", "kind": "og.test.open_vault" } ],
  "links": [ { "from": "scenario.load", "to": "scenario.ai.list_0" } ],
  "edges": [ { "from": "vault_in", "to": "open_vault", "input": "exec" },
	{ "from": "scenario.load", "to": "scenario.ai.list_1" })" + extra_edges + " ] }";
	return json;
}

} // namespace

TEST_CASE("Level graph program: only the event, gate and module nodes are the program",
	"[level][graph][program]")
{
	const std::string json = levelGraph(MISSION_GRAPH_MAX_NODES + 10);
	std::string error;
	mission_graph_program_t *program = compileLevel(json, &error);
	INFO(error);
	REQUIRE(program != nullptr);
	// scenario.load (inert), the volume event and the module; no data node.
	REQUIRE(missionGraphProgramNodeCount(program) == 3);
	REQUIRE(missionGraphProgramExecEdgeCount(program) == 1);
	REQUIRE(missionGraphProgramModuleNodeCount(program) == 1);
	REQUIRE(missionGraphProgramVolumeCount(program) == 1);
	REQUIRE(std::string(missionGraphProgramVolumeAt(program, 0)) == "volume_vault");
	REQUIRE(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_VOLUME_ENTERED) == 1);

	mission_graph_event_t event;
	std::memset(&event, 0, sizeof(event));
	event.kind = MISSION_GRAPH_EVENT_VOLUME_ENTERED;
	event.objective_index = -1;
	event.volume = "volume_vault";
	std::vector<std::string> ran;
	REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == 1);
	REQUIRE(ran == std::vector<std::string>{"open_vault"});
	missionGraphProgramFree(program);

	// The same text as a mission graph is refused for its size, as before.
	char err[256] = {};
	mission_graph_program_t *mission = missionGraphProgramCompile(json.c_str(), json.size(),
		knownModule, err, sizeof(err));
	REQUIRE(mission == nullptr);
	REQUIRE(std::string(err).find("more than 4096 nodes") != std::string::npos);
}

TEST_CASE("Level graph program: a base level graph compiles to an empty program",
	"[level][graph][program]")
{
	const std::string json = R"({ "schema": "pd2.scenario.graph.v1", "nodes": [
	{ "id": "scenario.load", "kind": "event.scenario.load" },
	{ "id": "scenario.pads", "kind": "scenario.pads" } ],
  "links": [ { "from": "scenario.load", "to": "scenario.pads" } ] })";
	mission_graph_program_t *program = compileLevel(json);
	REQUIRE(program != nullptr);
	REQUIRE(missionGraphProgramNodeCount(program) == 1);
	REQUIRE(missionGraphProgramExecEdgeCount(program) == 0);
	REQUIRE(missionGraphProgramVolumeCount(program) == 0);
	missionGraphProgramFree(program);
}

TEST_CASE("Level graph program: what the compiler refuses", "[level][graph][program]")
{
	std::string error;
	// An execution edge into level data.
	REQUIRE(compileLevel(levelGraph(3,
		R"(, { "from": "vault_in", "to": "scenario.ai.list_2", "input": "exec" })"), &error) == nullptr);
	REQUIRE(error.find("names a missing node ('scenario.ai.list_2')") != std::string::npos);
	// A module the game does not have, and a gate it cannot execute.
	const std::string unknown = R"({ "nodes": [ { "id": "m", "kind": "og.nope" } ] })";
	REQUIRE(compileLevel(unknown, &error) == nullptr);
	REQUIRE(error.find("native module 'og.nope' does not exist") != std::string::npos);
	const std::string gate = R"({ "nodes": [ { "id": "g", "kind": "gate.nope" } ] })";
	REQUIRE(compileLevel(gate, &error) == nullptr);
	REQUIRE(error.find("gate kind 'gate.nope' is not executable") != std::string::npos);
	// Not a level graph at all.
	REQUIRE(compileLevel("[]", &error) == nullptr);
	REQUIRE(error.find("level.graph.json is not one JSON object") != std::string::npos);
}

TEST_CASE("Level graph program: the runtime compiles it for every stage and runs its volume events",
	"[level][graph][program][static]")
{
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string header = readSource("port/include/scenario_source_runtime.h");
	const std::string lv = readSource("src/game/lv.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// Compiled at activation, before the level graph is active; a program
	// that does not compile, or names no box of volumes.json, refuses it.
	const size_t compiled = runtime.find("\ts_ActiveScenarioGraphs.level_program = missionGraphProgramCompileLevel(text, text_size,");
	const size_t bound = runtime.find("\t\t\t|| !s_graphBindVolumes(s_ActiveScenarioGraphs.level_program,");
	const size_t active = runtime.find("\ts_ActiveScenarioGraphs.level_graph_active = 1;");
	REQUIRE(compiled != std::string::npos);
	REQUIRE(bound != std::string::npos);
	REQUIRE(active != std::string::npos);
	REQUIRE(compiled < bound);
	REQUIRE(bound < active);
	REQUIRE(runtime.find("snprintf(reason, sizeof(reason), \"level program refused: %s\",") != std::string::npos);
	REQUIRE(runtime.find("\"SCENARIO.GRAPH: level program '%s' nodes=%d exec_edges=%d modules=%d volumes=%d backend=graph.level.runner\"")
		!= std::string::npos);
	REQUIRE(runtime.find("\tmissionGraphProgramFree(s_ActiveScenarioGraphs.level_program);\n") != std::string::npos);

	// One binding and one delivery for the mission and the level programs.
	REQUIRE(runtime.find("static s32 s_graphBindVolumes(const mission_graph_program_t *program,") != std::string::npos);
	REQUIRE(runtime.find("static s32 s_graphDeliverVolumeEvents(const mission_graph_program_t *program,") != std::string::npos);
	REQUIRE(runtime.find("s32 scenarioSourceLevelGraphDeliverVolumeEvents(const struct coord *positions,") != std::string::npos);
	REQUIRE(header.find("s32 scenarioSourceLevelGraphDeliverVolumeEvents(const struct coord *positions,") != std::string::npos);
	REQUIRE(runtime.find("s_ActiveScenarioGraphs.scenario_id, \"graph.level.runner\", positions, present, count);") != std::string::npos);

	// Delivered every frame on every stage after scenarioTick, on the host.
	const size_t tick = lv.find("\t\tscenarioTick();\n");
	const size_t level = lv.find("\t\t\tscenarioSourceLevelGraphDeliverVolumeEvents(positions, present, count);");
	REQUIRE(tick != std::string::npos);
	REQUIRE(level != std::string::npos);
	REQUIRE(tick < level);
	REQUIRE(lv.find("\t\tif (!g_MainIsEndscreen && g_NetMode != NETMODE_CLIENT) {") != std::string::npos);

	// Conformance checks a level graph's program as the runner compiles it.
	REQUIRE(conformance.find("def validate_mission_graph_execution(label: str, graph: dict, level: bool = False) -> list[str]:")
		!= std::string::npos);
	REQUIRE(conformance.find("validate_mission_graph_execution(label, graph, level=True))") != std::string::npos);
}
