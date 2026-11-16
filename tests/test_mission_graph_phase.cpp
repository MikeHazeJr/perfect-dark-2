/*
 * tests/test_mission_graph_phase.cpp -- mission flow slice 2, the runner part
 * (context/designs/modding/mission-flow.md): event.mission.phase_changed and
 * gate.mission.phase compile, refuse what they cannot honour, and route.
 */

#include "catch.hpp"

#include <cstring>
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
};

s32 runModule(const char *kind, const char *node_id, const mission_graph_params_t *params,
	const mission_graph_event_t *event, void *user)
{
	(void)params;
	(void)kind;
	(void)event;
	static_cast<Ran *>(user)->nodes.push_back(node_id);
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

mission_graph_event_t phaseEvent(s32 phase)
{
	mission_graph_event_t event;
	std::memset(&event, 0, sizeof(event));
	event.kind = MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED;
	event.objective_index = -1;
	event.phase = phase;
	return event;
}

const char *const kPhaseGraph = R"({ "schema": "pd2.mission.graph.v1", "nodes": [
	{ "id": "phase", "kind": "event.mission.phase_changed" },
	{ "id": "failed", "kind": "gate.mission.phase", "phase": "failed" },
	{ "id": "on_failed", "kind": "og.test.failed" },
	{ "id": "otherwise", "kind": "og.test.other" },
	{ "id": "status", "kind": "event.objective.status_changed" },
	{ "id": "status_gate", "kind": "gate.mission.phase", "phase": "complete" },
	{ "id": "never", "kind": "og.test.never" } ],
  "edges": [
	{ "from": "phase", "to": "failed", "input": "exec" },
	{ "from": "failed", "to": "on_failed", "input": "exec", "output": "pass" },
	{ "from": "failed", "to": "otherwise", "input": "exec", "output": "fail" },
	{ "from": "status", "to": "status_gate", "input": "exec" },
	{ "from": "status_gate", "to": "never", "input": "exec", "output": "pass" } ] })";

} // namespace

TEST_CASE("mission flow: a phase event routes through a phase gate", "[mission][runner][missionflow]")
{
	CHECK(missionGraphPhaseFromText("active") == MISSION_GRAPH_PHASE_ACTIVE);
	CHECK(missionGraphPhaseFromText("complete") == MISSION_GRAPH_PHASE_COMPLETE);
	CHECK(missionGraphPhaseFromText("failed") == MISSION_GRAPH_PHASE_FAILED);
	CHECK(missionGraphPhaseFromText("load") == -1);
	CHECK(missionGraphPhaseFromText("end") == -1);
	CHECK(missionGraphPhaseFromText(nullptr) == -1);

	std::string error;
	mission_graph_program_t *program = compile(kPhaseGraph, &error);
	INFO(error);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED) == 1);
	CHECK(missionGraphProgramEventNodeCount(program, MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED) == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED,
		"og.test.failed") == 1);

	Ran failed;
	mission_graph_event_t event = phaseEvent(MISSION_GRAPH_PHASE_FAILED);
	CHECK(missionGraphProgramFire(program, &event, runModule, &failed) == 1);
	CHECK(failed.nodes == std::vector<std::string>{ "on_failed" });

	Ran complete;
	event = phaseEvent(MISSION_GRAPH_PHASE_COMPLETE);
	CHECK(missionGraphProgramFire(program, &event, runModule, &complete) == 1);
	CHECK(complete.nodes == std::vector<std::string>{ "otherwise" });

	// A phase gate never passes for an objective event.
	Ran status;
	mission_graph_event_t objective;
	std::memset(&objective, 0, sizeof(objective));
	objective.kind = MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED;
	objective.objective_number = 1;
	objective.status = MISSION_GRAPH_STATUS_COMPLETE;
	CHECK(missionGraphProgramFire(program, &objective, runModule, &status) == 0);
	CHECK(status.nodes.empty());

	// Malformed phase events are refused.
	Ran none;
	event = phaseEvent(0);
	CHECK(missionGraphProgramFire(program, &event, runModule, &none) == -1);
	event = phaseEvent(MISSION_GRAPH_PHASE_FAILED + 1);
	CHECK(missionGraphProgramFire(program, &event, runModule, &none) == -1);
	event = phaseEvent(MISSION_GRAPH_PHASE_ACTIVE);
	event.seam = MISSION_GRAPH_SEAM_ENDSCREEN;
	CHECK(missionGraphProgramFire(program, &event, runModule, &none) == -1);
	CHECK(none.nodes.empty());

	missionGraphProgramFree(program);
}

TEST_CASE("mission flow: phase nodes refuse what they cannot honour", "[mission][runner][missionflow]")
{
	const std::string head = R"({ "schema": "pd2.mission.graph.v1", "nodes": [ )";
	CHECK(refusal(head + R"({ "id": "g", "kind": "gate.mission.phase" } ] })")
		.find("needs phase \"active\", \"complete\" or \"failed\"") != std::string::npos);
	CHECK(refusal(head + R"({ "id": "g", "kind": "gate.mission.phase", "phase": "end" } ] })")
		.find("needs phase") != std::string::npos);
	CHECK(refusal(head + R"({ "id": "g", "kind": "gate.mission.phase", "phase": "load" } ] })")
		.find("needs phase") != std::string::npos);
	CHECK(refusal(head + R"({ "id": "g", "kind": "gate.mission.phase", "phase": "failed", "objective": "x" } ] })")
		.find("is a phase gate; it has no objective to filter") != std::string::npos);
	CHECK(refusal(head + R"({ "id": "e", "kind": "event.mission.phase_changed", "objective": "x" } ] })")
		.find("has no objective to filter") != std::string::npos);
	CHECK(refusal(head + R"({ "id": "g", "kind": "gate.mission.phase", "phase": "active" } ] })") == "(compiled)");
}
