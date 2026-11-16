/*
 * tests/test_mission_graph_volume.cpp -- trigger-volume events
 * (context/designs/modding/trigger-volume-events.md): event.volume.entered
 * and event.volume.left compile with one named volume each, refuse what they
 * cannot honour, list their volumes, and route by volume; plus pins on the
 * runtime's binding and delivery and on conformance.
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

struct Ran {
	std::vector<std::string> nodes;
	std::vector<s32> players;
};

s32 runModule(const char *kind, const char *node_id, const mission_graph_event_t *event, void *user)
{
	(void)kind;
	static_cast<Ran *>(user)->nodes.push_back(node_id);
	static_cast<Ran *>(user)->players.push_back(event->player);
	return 1;
}

mission_graph_program_t *compile(const std::string &json, std::string *error = nullptr)
{
	char err[256] = {};
	mission_graph_program_t *program = missionGraphProgramCompile(json.c_str(), json.size(), knownModule,
		err, sizeof(err));
	if (error) *error = err;
	return program;
}

std::string refusal(const std::string &json)
{
	std::string error;
	mission_graph_program_t *program = compile(json, &error);
	missionGraphProgramFree(program);
	return program ? std::string("(compiled)") : error;
}

mission_graph_event_t volumeEvent(s32 kind, const char *volume, s32 player)
{
	mission_graph_event_t event;
	std::memset(&event, 0, sizeof(event));
	event.kind = kind;
	event.objective_index = -1;
	event.volume = volume;
	event.player = player;
	return event;
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

const char *const kVolumeGraph = R"({ "schema": "pd2.mission.graph.v1", "nodes": [
	{ "id": "exit_in", "kind": "event.volume.entered", "volume": "volume_exit" },
	{ "id": "exit_out", "kind": "event.volume.left", "volume": "volume_exit" },
	{ "id": "lab_in", "kind": "event.volume.entered", "volume": "volume_lab" },
	{ "id": "phase", "kind": "gate.mission.phase", "phase": "active" },
	{ "id": "status", "kind": "gate.objective.status", "status": "complete" },
	{ "id": "on_exit", "kind": "og.test.exit" },
	{ "id": "on_left", "kind": "og.test.left" },
	{ "id": "on_lab", "kind": "og.test.lab" },
	{ "id": "phase_failed", "kind": "og.test.phase_failed" },
	{ "id": "status_failed", "kind": "og.test.status_failed" } ],
  "edges": [
	{ "from": "exit_in", "to": "on_exit", "input": "exec" },
	{ "from": "exit_out", "to": "on_left", "input": "exec" },
	{ "from": "lab_in", "to": "on_lab", "input": "exec" },
	{ "from": "lab_in", "to": "phase", "input": "exec" },
	{ "from": "phase", "to": "phase_failed", "input": "exec", "output": "fail" },
	{ "from": "lab_in", "to": "status", "input": "exec" },
	{ "from": "status", "to": "status_failed", "input": "exec", "output": "fail" } ] })";

} // namespace

TEST_CASE("Mission graph volume events: compile, list their volumes and route by volume",
	"[mission][graph][volume]")
{
	mission_graph_program_t *program = compile(kVolumeGraph);
	REQUIRE(program != nullptr);
	REQUIRE(missionGraphProgramEventNodeCount(program, MISSION_GRAPH_EVENT_VOLUME_ENTERED) == 2);
	REQUIRE(missionGraphProgramEventNodeCount(program, MISSION_GRAPH_EVENT_VOLUME_LEFT) == 1);
	// The distinct volumes, in authored order.
	REQUIRE(missionGraphProgramVolumeCount(program) == 2);
	REQUIRE(std::string(missionGraphProgramVolumeAt(program, 0)) == "volume_exit");
	REQUIRE(std::string(missionGraphProgramVolumeAt(program, 1)) == "volume_lab");
	REQUIRE(missionGraphProgramVolumeAt(program, 2) == nullptr);
	REQUIRE(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_VOLUME_LEFT) == 1);

	// Entering the exit runs its module only, with the player.
	Ran ran;
	mission_graph_event_t event = volumeEvent(MISSION_GRAPH_EVENT_VOLUME_ENTERED, "volume_exit", 1);
	REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == 1);
	REQUIRE(ran.nodes == std::vector<std::string>{"on_exit"});
	REQUIRE(ran.players == std::vector<s32>{1});

	// Leaving it runs the left handler; the volume id is compared as text.
	ran = Ran();
	const std::string copy = "volume_exit";
	event = volumeEvent(MISSION_GRAPH_EVENT_VOLUME_LEFT, copy.c_str(), 0);
	REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == 1);
	REQUIRE(ran.nodes == std::vector<std::string>{"on_left"});

	// A volume no node names runs nothing.
	ran = Ran();
	event = volumeEvent(MISSION_GRAPH_EVENT_VOLUME_ENTERED, "volume_elsewhere", 0);
	REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == 0);
	REQUIRE(ran.nodes.empty());

	// Phase and objective gates never pass for a volume event.
	ran = Ran();
	event = volumeEvent(MISSION_GRAPH_EVENT_VOLUME_ENTERED, "volume_lab", 0);
	REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == 3);
	REQUIRE(ran.nodes == std::vector<std::string>{"on_lab", "phase_failed", "status_failed"});

	// A volume event needs its volume and a player.
	event = volumeEvent(MISSION_GRAPH_EVENT_VOLUME_ENTERED, nullptr, 0);
	REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == -1);
	event = volumeEvent(MISSION_GRAPH_EVENT_VOLUME_ENTERED, "", 0);
	REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == -1);
	event = volumeEvent(MISSION_GRAPH_EVENT_VOLUME_ENTERED, "volume_exit", -1);
	REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == -1);
	missionGraphProgramFree(program);
}

TEST_CASE("Mission graph volume events: what the compiler refuses", "[mission][graph][volume]")
{
	const std::string head = R"({ "schema": "pd2.mission.graph.v1", "nodes": [ )";
	REQUIRE(refusal(head + R"({ "id": "in", "kind": "event.volume.entered" } ] })")
		.find("needs \"volume\": the volume_id of a volumes.json row") != std::string::npos);
	REQUIRE(refusal(head + R"({ "id": "in", "kind": "event.volume.left", "volume": "any" } ] })")
		.find("needs \"volume\"") != std::string::npos);
	REQUIRE(refusal(head + R"({ "id": "in", "kind": "event.volume.entered", "volume": 3 } ] })")
		.find("needs \"volume\"") != std::string::npos);
	REQUIRE(refusal(head + R"({ "id": "in", "kind": "event.volume.entered", "volume": "v",
		"objective": "objective.0" } ] })").find("has no objective to filter") != std::string::npos);

	// More distinct volumes than the runtime tracks.
	std::string many = head;
	for (int i = 0; i <= MISSION_GRAPH_MAX_VOLUMES; i++) {
		many += (i ? ", " : "") + std::string("{ \"id\": \"in") + std::to_string(i)
			+ "\", \"kind\": \"event.volume.entered\", \"volume\": \"v" + std::to_string(i) + "\" }";
	}
	REQUIRE(refusal(many + " ] }").find("volumes in volume events") != std::string::npos);
	// The same volume named twice counts once.
	REQUIRE(refusal(head + R"({ "id": "a", "kind": "event.volume.entered", "volume": "v" },
		{ "id": "b", "kind": "event.volume.left", "volume": "v" } ] })") == "(compiled)");
}

TEST_CASE("Mission graph volume events: the runtime binds and delivers them",
	"[mission][graph][volume][static]")
{
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string objectives = readSource("src/game/objectives.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// Bound at activation, refused when a volume is not a box of volumes.json.
	REQUIRE(runtime.find("static s32 s_missionGraphBindVolumes(char *error, size_t error_size)")
		!= std::string::npos);
	REQUIRE(runtime.find("\"event.volume.* names volume '%s', which is not a box of the stage's volumes.json\"")
		!= std::string::npos);
	REQUIRE(runtime.find("if (!row || strcmp(row->shape, \"aabb\") != 0) {") != std::string::npos);
	const size_t compiled = runtime.find("s_ActiveScenarioGraphs.mission_program = missionGraphProgramCompile(text,");
	const size_t bound = runtime.find("\tif (s_ActiveScenarioGraphs.mission_program && !s_missionGraphBindVolumes(");
	REQUIRE(compiled != std::string::npos);
	REQUIRE(bound != std::string::npos);
	REQUIRE(compiled < bound);

	// Delivered on a change of inside/outside per player, from the objectives
	// check after the phase event, not on a network client.
	REQUIRE(runtime.find("if (inside == ((bound[v].inside & bit) != 0)) {")
		!= std::string::npos);
	REQUIRE(runtime.find("event.kind = inside ? MISSION_GRAPH_EVENT_VOLUME_ENTERED : MISSION_GRAPH_EVENT_VOLUME_LEFT;")
		!= std::string::npos);
	const size_t phase = objectives.find("\t\tscenarioSourceMissionGraphDeliverPhaseEvent();\n");
	const size_t volumes = objectives.find("scenarioSourceMissionGraphDeliverVolumeEvents(positions, present, count);");
	REQUIRE(phase != std::string::npos);
	REQUIRE(volumes != std::string::npos);
	REQUIRE(phase < volumes);
	REQUIRE(objectives.find("\t\tif (g_NetMode != NETMODE_CLIENT) {\n\t\t\tstruct coord positions[MAX_PLAYERS];")
		!= std::string::npos);

	// Conformance knows the kinds and requires the volume.
	REQUIRE(conformance.find("MISSION_GRAPH_VOLUME_EVENTS = {\"event.volume.entered\", \"event.volume.left\"}")
		!= std::string::npos);
	REQUIRE(conformance.find("volume event {node_id!r} needs") != std::string::npos);
}
