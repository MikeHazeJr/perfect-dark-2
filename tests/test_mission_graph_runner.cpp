// Decision 2 (Mike, 2026-11-15): build the mission-graph runner. A mission
// graph's execution edges ("input": "exec") compile into a program; a gameplay
// event runs the gates and native modules it reaches, each at most once.
// The first event is event.objective.status_changed and the first native
// module the original "Objective N: Completed" message.
#include "catch.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "mission_graph_runner.h"
}

namespace {

struct Call {
	std::string kind;
	std::string node;
	int status;
	int number;
};

std::vector<Call> g_calls;
std::set<std::string> g_refusing;

s32 knownModule(const char *kind)
{
	const std::string k = kind;
	return k == "og.hud.objective_status_message" || k == "og.test.a" || k == "og.test.b" ||
		k == "og.test.c";
}

s32 runModule(const char *kind, const char *node_id, const mission_graph_event_t *event, void *)
{
	g_calls.push_back(Call{ kind, node_id, event->status, event->objective_number });
	return g_refusing.count(node_id) ? 0 : 1;
}

struct Program {
	mission_graph_program_t *program = nullptr;
	std::string error;
	explicit Program(const std::string &json)
	{
		char err[256] = { 0 };
		program = missionGraphProgramCompile(json.data(), json.size(), knownModule, err, sizeof(err));
		error = err;
	}
	~Program() { missionGraphProgramFree(program); }
};

mission_graph_event_t statusEvent(int status, const char *node = nullptr, int number = 1)
{
	mission_graph_event_t event = {};
	event.kind = MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED;
	event.objective_index = number - 1;
	event.objective_number = number;
	event.status = status;
	event.previous_status = MISSION_GRAPH_STATUS_INCOMPLETE;
	event.objective_node = node;
	return event;
}

// The shape the mission extractor writes (romextract_pdmeta.c): data nodes
// with structural edges, plus the event, the module and one execution edge.
const char *const kBaseShape = R"({
  "schema": "pd2.mission.graph.v1",
  "catalog_id": "base:mission_test",
  "nodes": [
    { "id": "mission.load", "kind": "event.mission.load", "scenario": "base:scenario_test" },
    { "id": "mission.phase.load", "kind": "mission.phase.source", "phase": "load" },
    { "id": "mission.objectives", "kind": "mission.objectives.source", "file": "objectives.json" },
    { "id": "mission.objective.0", "kind": "mission.objective.source" },
    { "id": "objective.status_changed", "kind": "event.objective.status_changed", "objective": "any" },
    { "id": "objective.status_message", "kind": "og.hud.objective_status_message" },
    { "id": "mission.parity_backend", "kind": "mission.behavior.parity_backend", "module": "og.mission.test" }
  ],
  "edges": [
    { "from": "mission.load", "to": "mission.phase.load" },
    { "from": "mission.phase.load", "to": "mission.objectives" },
    { "from": "mission.objectives", "to": "mission.objective.0" },
    { "from": "objective.status_changed", "output": "out", "to": "objective.status_message", "input": "exec" }
  ]
})";

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

std::string refusal(const std::string &json)
{
	Program p(json);
	CHECK(p.program == nullptr);
	return p.error;
}

std::string graph(const std::string &nodes, const std::string &edges)
{
	return "{ \"schema\": \"pd2.mission.graph.v1\", \"nodes\": [" + nodes + "], \"edges\": [" + edges + "] }";
}

} // namespace

TEST_CASE("the extracted mission graph shape compiles and its status event runs the message module",
	"[mission][graph][runner]")
{
	Program p(kBaseShape);
	INFO(p.error);
	REQUIRE(p.program != nullptr);
	CHECK(missionGraphProgramNodeCount(p.program) == 7);
	CHECK(missionGraphProgramExecEdgeCount(p.program) == 1);
	CHECK(missionGraphProgramEventNodeCount(p.program, MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED) == 1);
	CHECK(missionGraphProgramModuleNodeCount(p.program) == 1);

	for (int status : { MISSION_GRAPH_STATUS_COMPLETE, MISSION_GRAPH_STATUS_INCOMPLETE, MISSION_GRAPH_STATUS_FAILED }) {
		g_calls.clear();
		const mission_graph_event_t event = statusEvent(status, "mission.objective.0", 3);
		CHECK(missionGraphProgramFire(p.program, &event, runModule, nullptr) == 1);
		REQUIRE(g_calls.size() == 1);
		CHECK(g_calls[0].kind == "og.hud.objective_status_message");
		CHECK(g_calls[0].node == "objective.status_message");
		CHECK(g_calls[0].status == status);
		CHECK(g_calls[0].number == 3);
	}

	// A malformed event runs nothing.
	g_calls.clear();
	mission_graph_event_t bad = statusEvent(7);
	CHECK(missionGraphProgramFire(p.program, &bad, runModule, nullptr) == -1);
	bad = statusEvent(MISSION_GRAPH_STATUS_COMPLETE);
	bad.kind = 99;
	CHECK(missionGraphProgramFire(p.program, &bad, runModule, nullptr) == -1);
	CHECK(g_calls.empty());
}

TEST_CASE("gates follow pass or fail, and an objective filter narrows events and gates",
	"[mission][graph][runner]")
{
	Program p(graph(
		R"({ "id": "e", "kind": "event.objective.status_changed" },
		   { "id": "g", "kind": "gate.objective.status", "status": "complete" },
		   { "id": "done", "kind": "og.test.a" },
		   { "id": "other", "kind": "og.test.b" },
		   { "id": "only1", "kind": "event.objective.status_changed", "objective": "mission.objective.1" },
		   { "id": "first", "kind": "og.test.c" },
		   { "id": "g1", "kind": "gate.objective.status", "status": "failed", "objective": "mission.objective.1" })",
		R"({ "from": "e", "to": "g", "input": "exec" },
		   { "from": "g", "output": "pass", "to": "done", "input": "exec" },
		   { "from": "g", "output": "fail", "to": "other", "input": "exec" },
		   { "from": "only1", "to": "first", "input": "exec" },
		   { "from": "e", "to": "g1", "input": "exec" },
		   { "from": "g1", "output": "pass", "to": "other", "input": "exec" })"));
	INFO(p.error);
	REQUIRE(p.program != nullptr);
	CHECK(missionGraphProgramEventNodeCount(p.program, MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED) == 2);

	g_calls.clear();
	mission_graph_event_t event = statusEvent(MISSION_GRAPH_STATUS_COMPLETE, "mission.objective.0");
	CHECK(missionGraphProgramFire(p.program, &event, runModule, nullptr) == 1);
	REQUIRE(g_calls.size() == 1);
	CHECK(g_calls[0].node == "done");

	g_calls.clear();
	event = statusEvent(MISSION_GRAPH_STATUS_FAILED, "mission.objective.0");
	CHECK(missionGraphProgramFire(p.program, &event, runModule, nullptr) == 1);
	REQUIRE(g_calls.size() == 1);
	CHECK(g_calls[0].node == "other");

	// Objective 1 also reaches its own event; "other" is reached twice (gate g
	// fails, gate g1 passes) and still runs once.
	g_calls.clear();
	event = statusEvent(MISSION_GRAPH_STATUS_FAILED, "mission.objective.1");
	CHECK(missionGraphProgramFire(p.program, &event, runModule, nullptr) == 2);
	std::multiset<std::string> ran;
	for (const Call &c : g_calls) ran.insert(c.node);
	CHECK(ran == std::multiset<std::string>{ "first", "other" });
}

TEST_CASE("each reached node runs once per event, also around a cycle, and a refusing module is reported",
	"[mission][graph][runner]")
{
	Program p(graph(
		R"({ "id": "e", "kind": "event.objective.status_changed" },
		   { "id": "a", "kind": "og.test.a" },
		   { "id": "b", "kind": "og.test.b" },
		   { "id": "c", "kind": "og.test.c" })",
		R"({ "from": "e", "to": "a", "input": "exec" },
		   { "from": "a", "to": "b", "output": "out", "input": "exec" },
		   { "from": "b", "to": "a", "input": "exec" },
		   { "from": "e", "to": "c", "input": "exec" })"));
	INFO(p.error);
	REQUIRE(p.program != nullptr);
	g_calls.clear();
	const mission_graph_event_t event = statusEvent(MISSION_GRAPH_STATUS_COMPLETE);
	CHECK(missionGraphProgramFire(p.program, &event, runModule, nullptr) == 3);
	REQUIRE(g_calls.size() == 3);
	// Breadth first in authored order: a and c from the event, then b.
	CHECK(g_calls[0].node == "a");
	CHECK(g_calls[1].node == "c");
	CHECK(g_calls[2].node == "b");

	// A module that refuses stops its own successors; the others still run.
	g_calls.clear();
	g_refusing = { "a" };
	CHECK(missionGraphProgramFire(p.program, &event, runModule, nullptr) == -1);
	g_refusing.clear();
	REQUIRE(g_calls.size() == 2);
	CHECK(g_calls[0].node == "a");
	CHECK(g_calls[1].node == "c");
}

TEST_CASE("a mission graph the runner cannot execute is refused when it compiles",
	"[mission][graph][runner]")
{
	const std::string event = R"({ "id": "e", "kind": "event.objective.status_changed" })";
	const std::string module = R"({ "id": "m", "kind": "og.test.a" })";
	const std::string data = R"({ "id": "d", "kind": "mission.phase.source" })";

	CHECK(refusal("[]").find("not one JSON object") != std::string::npos);
	CHECK(refusal("{ \"nodes\": 3 }").find("no nodes array") != std::string::npos);
	CHECK(refusal(graph(event + ", " + R"({ "id": "x", "kind": "og.nope" })", ""))
		.find("native module 'og.nope' does not exist") != std::string::npos);
	CHECK(refusal(graph(event + ", " + R"({ "id": "x", "kind": "gate.nope" })", ""))
		.find("gate kind 'gate.nope' is not executable") != std::string::npos);
	CHECK(refusal(graph(R"({ "id": "g", "kind": "gate.objective.status", "status": "won" })", ""))
		.find("needs status") != std::string::npos);
	CHECK(refusal(graph(event + ", " + event, "")).find("used twice") != std::string::npos);
	CHECK(refusal(graph(R"({ "id": "", "kind": "og.test.a" })", "")).find("non-empty id") != std::string::npos);
	CHECK(refusal(graph(event + ", " + module, R"({ "from": "e", "to": "missing" })"))
		.find("missing node ('missing')") != std::string::npos);
	CHECK(refusal(graph(event + ", " + module, R"({ "from": "e", "to": "m", "input": "data" })"))
		.find("input must be \"exec\"") != std::string::npos);
	CHECK(refusal(graph(event + ", " + module, R"({ "from": "e", "to": "m", "output": "out" })"))
		.find("not an execution edge") != std::string::npos);
	CHECK(refusal(graph(event + ", " + data, R"({ "from": "e", "to": "d", "input": "exec" })"))
		.find("not a gate or a native module") != std::string::npos);
	CHECK(refusal(graph(data + ", " + module, R"({ "from": "d", "to": "m", "input": "exec" })"))
		.find("cannot execute") != std::string::npos);
	CHECK(refusal(graph(R"({ "id": "l", "kind": "event.mission.load" })" + std::string(", ") + module,
			R"({ "from": "l", "to": "m", "input": "exec" })"))
		.find("cannot execute") != std::string::npos);
	CHECK(refusal(graph(R"({ "id": "g", "kind": "gate.objective.status", "status": "complete" })"
			+ std::string(", ") + module, R"({ "from": "g", "to": "m", "input": "exec" })"))
		.find("needs output \"pass\" or \"fail\"") != std::string::npos);
	CHECK(refusal(graph(event + ", " + module, R"({ "from": "e", "to": "m", "output": "pass", "input": "exec" })"))
		.find("output must be \"out\"") != std::string::npos);

	std::string many;
	for (int i = 0; i <= MISSION_GRAPH_MAX_NODES; i++) {
		many += (i ? ", " : "") + std::string("{ \"id\": \"n") + std::to_string(i) +
			"\", \"kind\": \"mission.phase.source\" }";
	}
	CHECK(refusal(graph(many, "")).find("more than") != std::string::npos);

	// Structural edges between data nodes and an inert event are fine.
	Program ok(graph(R"({ "id": "l", "kind": "event.mission.load" })" + std::string(", ") + data,
		R"({ "from": "l", "to": "d" })"));
	CHECK(ok.program != nullptr);
	CHECK(missionGraphProgramExecEdgeCount(ok.program) == 0);
}

TEST_CASE("the extractor, the objective check, the runtime and conformance agree on the runner",
	"[mission][graph][runner]")
{
	const std::string meta = readSource("port/src/romextract_pdmeta.c");
	const std::string objectives = readSource("src/game/objectives.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string modules = readSource("port/src/mission_graph_modules.c");
	const std::string runner = readSource("port/src/mission_graph_runner.cpp");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// Every base mission graph gets the event, the module and the execution
	// edge, and older archives are not clean (marker and cache kind).
	REQUIRE(meta.find(R"({ \"id\": \"objective.status_changed\", \"kind\": \"event.objective.status_changed\", \"objective\": \"any\" },)") != std::string::npos);
	REQUIRE(meta.find(R"({ \"id\": \"objective.status_message\", \"kind\": \"og.hud.objective_status_message\" },)") != std::string::npos);
	REQUIRE(meta.find(R"({ \"from\": \"objective.status_changed\", \"output\": \"out\", \"to\": \"objective.status_message\", \"input\": \"exec\" })") != std::string::npos);
	REQUIRE(meta.find("\"\\\"input\\\": \\\"exec\\\"\") &&") != std::string::npos);
	REQUIRE(meta.find("_objective_status_event\"") != std::string::npos);

	// The objective check fires the event when the mission graph owns it and
	// shows the message directly otherwise; the message is one function.
	REQUIRE(objectives.find("if (scenarioSourceMissionGraphOwnsObjectiveEvents()) {\n"
		"\t\t\t\t\t\tscenarioSourceMissionGraphObjectiveStatusChanged(i,\n"
		"\t\t\t\t\t\t\tavailableindex + 1, status, previous);\n"
		"\t\t\t\t\t} else {\n"
		"\t\t\t\t\t\tobjectivesShowStatusMessage(availableindex + 1, status);") != std::string::npos);
	REQUIRE(objectives.find("void objectivesShowStatusMessage(s32 number, s32 status)") != std::string::npos);
	size_t messages = 0;
	for (size_t at = objectives.find("strcat(buffer, langGet(L_MISC_045)); // \"Completed\"");
			at != std::string::npos; at = objectives.find("strcat(buffer, langGet(L_MISC_045)); // \"Completed\"", at + 1)) {
		messages++;
	}
	CHECK(messages == 2); // the NTSC 1.0 and the beta branch of the one function
	// The forced completion goes through the status change first.
	REQUIRE(objectives.find("g_DebugForceCompleteCurrentMissionObjectives = true;\n\n"
		"\t/* The check first,") != std::string::npos);

	// The runtime compiles the program at activation against the module table,
	// frees it at reset, and a refused program still owns the events.
	REQUIRE(runtime.find("s_ActiveScenarioGraphs.mission_program = missionGraphProgramCompile(text,\n"
		"\t\ttext_size, missionGraphModuleKnown,") != std::string::npos);
	REQUIRE(runtime.find("missionGraphProgramFree(s_ActiveScenarioGraphs.mission_program);") != std::string::npos);
	REQUIRE(runtime.find("ran = missionGraphProgramFire(s_ActiveScenarioGraphs.mission_program,\n"
		"\t\t&event, missionGraphModuleRun, NULL);") != std::string::npos);
	REQUIRE(runtime.find("s_ActiveScenarioGraphs.mission_program_refused);") != std::string::npos);
	REQUIRE(modules.find("{ \"og.hud.objective_status_message\", s_runObjectiveStatusMessage },") != std::string::npos);

	// Conformance knows exactly the runner's event, gate and module kinds.
	auto set_of = [&](const char *name) {
		std::smatch m;
		const std::regex re(std::string(name) + R"( = \{([^}]*)\})");
		REQUIRE(std::regex_search(conformance, m, re));
		std::set<std::string> out;
		const std::string body = m[1].str();
		const std::regex item("\"([^\"]+)\"");
		for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it) {
			out.insert((*it)[1].str());
		}
		return out;
	};
	CHECK(set_of("MISSION_GRAPH_EXEC_EVENTS") == std::set<std::string>{ "event.objective.status_changed" });
	CHECK(set_of("MISSION_GRAPH_EXEC_GATES") == std::set<std::string>{ "gate.objective.status" });
	CHECK(set_of("MISSION_GRAPH_NATIVE_MODULES") == std::set<std::string>{ "og.hud.objective_status_message" });
	REQUIRE(runner.find("const char *const kObjectiveStatusEvent = \"event.objective.status_changed\";") != std::string::npos);
	REQUIRE(runner.find("const char *const kObjectiveStatusGate = \"gate.objective.status\";") != std::string::npos);
	REQUIRE(conformance.find("errors.extend(validate_mission_graph_execution(label, graph))") != std::string::npos);
}
