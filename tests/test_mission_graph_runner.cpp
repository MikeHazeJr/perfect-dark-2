// Decision 2 (Mike, 2026-11-15): build the mission-graph runner. A mission
// graph's execution edges ("input": "exec") compile into a program; a gameplay
// event runs the gates and native modules it reaches, each at most once.
// The first event is event.objective.status_changed and the first native
// module the original "Objective N: Completed" message.
#include "catch.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <filesystem>

extern "C" {
#include "mission_graph_runner.h"
#include "modarchive.h"
#include "weapon_graph_runtime.h"
#include "weapon_rules.h"
}

namespace {

struct Call {
	std::string kind;
	std::string node;
	int status;
	int number;
	int seam;
};

std::vector<Call> g_calls;
std::set<std::string> g_refusing;

s32 knownModule(const char *kind)
{
	const std::string k = kind;
	return k == "og.hud.objective_status_message" || k == "og.test.a" || k == "og.test.b" ||
		k == "og.test.c" || k == "og.mission.start_next_stage" || k == "og.mission.start_credits" ||
		k == "og.progress.unlock_alt_title";
}

s32 runModule(const char *kind, const char *node_id, const mission_graph_params_t *params,
	const mission_graph_event_t *event, void *)
{
	(void)params;
	g_calls.push_back(Call{ kind, node_id, event->status, event->objective_number, event->seam });
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

// Opt-in sweep (hidden from the default run): every .pdmission under
// PD2_MISSION_GRAPH_ROOT must compile with the runner and wire the status
// event to the message module, as the extractor writes it. Read-only.
TEST_CASE("mission events run what a completed mission leads to and what it unlocks",
	"[mission][graph][runner]")
{
	Program p(R"({ "nodes": [
	  { "id": "continue", "kind": "event.mission.continue" },
	  { "id": "next", "kind": "og.mission.start_next_stage" },
	  { "id": "completed", "kind": "event.mission.completed" },
	  { "id": "gate", "kind": "gate.objective.status", "status": "incomplete" },
	  { "id": "alt", "kind": "og.progress.unlock_alt_title" },
	  { "id": "never", "kind": "og.test.a" },
	  { "id": "status", "kind": "event.objective.status_changed" }
	], "edges": [
	  { "from": "continue", "to": "next", "input": "exec" },
	  { "from": "completed", "to": "gate", "input": "exec" },
	  { "from": "gate", "to": "never", "input": "exec", "output": "pass" },
	  { "from": "gate", "to": "alt", "input": "exec", "output": "fail" }
	] })");
	INFO(p.error);
	REQUIRE(p.program != nullptr);
	CHECK(missionGraphProgramHandlesEvent(p.program, MISSION_GRAPH_EVENT_MISSION_CONTINUE) == 1);
	CHECK(missionGraphProgramHandlesEvent(p.program, MISSION_GRAPH_EVENT_MISSION_COMPLETED) == 1);
	CHECK(missionGraphProgramHandlesEvent(p.program, MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED) == 0);
	CHECK(missionGraphProgramEventReachesModule(p.program, MISSION_GRAPH_EVENT_MISSION_CONTINUE,
		"og.mission.start_next_stage") == 1);
	CHECK(missionGraphProgramEventReachesModule(p.program, MISSION_GRAPH_EVENT_MISSION_CONTINUE,
		"og.mission.start_credits") == 0);
	// Both branches of a gate count as reachable.
	CHECK(missionGraphProgramEventReachesModule(p.program, MISSION_GRAPH_EVENT_MISSION_COMPLETED, "og.test.a") == 1);
	CHECK(missionGraphProgramEventReachesModule(p.program, MISSION_GRAPH_EVENT_MISSION_COMPLETED,
		"og.progress.unlock_alt_title") == 1);

	mission_graph_event_t cont = {};
	cont.kind = MISSION_GRAPH_EVENT_MISSION_CONTINUE;
	cont.seam = MISSION_GRAPH_SEAM_ENDSCREEN;
	g_calls.clear();
	CHECK(missionGraphProgramFire(p.program, &cont, runModule, nullptr) == 1);
	REQUIRE(g_calls.size() == 1);
	CHECK(g_calls[0].kind == "og.mission.start_next_stage");
	CHECK(g_calls[0].seam == MISSION_GRAPH_SEAM_ENDSCREEN);
	cont.seam = MISSION_GRAPH_SEAM_COOP_STAGE_END;
	g_calls.clear();
	CHECK(missionGraphProgramFire(p.program, &cont, runModule, nullptr) == 1);
	REQUIRE(g_calls.size() == 1);
	CHECK(g_calls[0].seam == MISSION_GRAPH_SEAM_COOP_STAGE_END);

	// An objective gate never passes for a mission event, so its fail branch runs.
	mission_graph_event_t done = {};
	done.kind = MISSION_GRAPH_EVENT_MISSION_COMPLETED;
	g_calls.clear();
	CHECK(missionGraphProgramFire(p.program, &done, runModule, nullptr) == 1);
	REQUIRE(g_calls.size() == 1);
	CHECK(g_calls[0].kind == "og.progress.unlock_alt_title");

	// Malformed mission events are refused.
	cont.seam = MISSION_GRAPH_SEAM_NONE;
	CHECK(missionGraphProgramFire(p.program, &cont, runModule, nullptr) == -1);
	cont.seam = 7;
	CHECK(missionGraphProgramFire(p.program, &cont, runModule, nullptr) == -1);
	done.seam = MISSION_GRAPH_SEAM_ENDSCREEN;
	CHECK(missionGraphProgramFire(p.program, &done, runModule, nullptr) == -1);
	mission_graph_event_t unknown = {};
	unknown.kind = 99;
	CHECK(missionGraphProgramFire(p.program, &unknown, runModule, nullptr) == -1);

	// A mission with no handler runs nothing on continue: the default flow applies.
	Program none(R"({ "nodes": [ { "id": "continue", "kind": "event.mission.continue" } ] })");
	REQUIRE(none.program != nullptr);
	CHECK(missionGraphProgramHandlesEvent(none.program, MISSION_GRAPH_EVENT_MISSION_CONTINUE) == 0);
	cont.seam = MISSION_GRAPH_SEAM_ENDSCREEN;
	g_calls.clear();
	CHECK(missionGraphProgramFire(none.program, &cont, runModule, nullptr) == 0);
	CHECK(g_calls.empty());

	// A mission event has no objective to filter.
	Program bad(R"({ "nodes": [ { "id": "c", "kind": "event.mission.completed", "objective": "any" } ] })");
	CHECK(bad.program == nullptr);
	CHECK(bad.error.find("has no objective to filter") != std::string::npos);
}

TEST_CASE("mission endings come from the mission graph on a scenario-source stage",
	"[mission][graph][runner]")
{
	const std::string meta = readSource("port/src/romextract_pdmeta.c");
	const std::string endscreen = readSource("src/game/endscreen.c");
	const std::string menutick = readSource("src/game/menutick.c");
	const std::string menu = readSource("src/game/menu.c");
	const std::string menugfx = readSource("src/game/menugfx.c");
	const std::string modules = readSource("port/src/mission_graph_modules.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");
	const std::string bridge = readSource("port/fast3d/pdgui_bridge.c");

	// The extractor writes the endings from the stages the game checks.
	REQUIRE(meta.find("const char *continue_module = stagenum == STAGE_DEEPSEA ? \"og.mission.start_next_stage\"\n"
		"\t\t\t: stagenum == STAGE_SKEDARRUINS ? \"og.mission.start_credits\" : NULL;") != std::string::npos);
	REQUIRE(meta.find(R"({ \"id\": \"mission.completed.alt_title\", \"kind\": \"og.progress.unlock_alt_title\" },)") != std::string::npos);
	REQUIRE(meta.find(R"({ \"from\": \"mission.continue\", \"output\": \"out\", \"to\": \"mission.continue.action\", \"input\": \"exec\" })") != std::string::npos);
	REQUIRE(meta.find("return (s32)g_SoloStages[i].stagenum;") != std::string::npos);

	// The end screen asks the mission graph whether a completed mission leads
	// straight on, fires the continue and completed events, and keeps the
	// original stage checks only for a ROM stage.
	REQUIRE(endscreen.find("\t\tif (endscreenMissionContinuesDirectly()) {\n"
		"\t\t\tif (context == 2 || g_Menus[g_MpPlayerNum].endscreen.isfirstcompletion) {\n"
		"\t\t\t\t// Pressed continue\n"
		"\t\t\t\tendscreenRunMissionContinue();") != std::string::npos);
	REQUIRE(endscreen.find("scenarioSourceMissionGraphMissionEvent(MISSION_GRAPH_EVENT_MISSION_CONTINUE,\n"
		"\t\t\t\tMISSION_GRAPH_SEAM_ENDSCREEN) <= 0) {") != std::string::npos);
	REQUIRE(endscreen.find("scenarioSourceMissionGraphMissionEvent(MISSION_GRAPH_EVENT_MISSION_COMPLETED,\n"
		"\t\t\t\t\t\tMISSION_GRAPH_SEAM_NONE);") != std::string::npos);
	auto count = [](const std::string &text, const std::string &needle) {
		size_t n = 0;
		for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) n++;
		return n;
	};
	CHECK(count(endscreen, "g_Vars.stagenum == STAGE_DEEPSEA || g_Vars.stagenum == STAGE_SKEDARRUINS") == 1);
	CHECK(count(endscreen, "g_MissionConfig.stagenum == STAGE_SKEDARRUINS && g_AltTitleUnlocked == false") == 0);

	// A co-op stage that leads straight on asks the same graph.
	REQUIRE(menutick.find("if (endscreenCoopStageEndStartsNextStage()) {") != std::string::npos);
	REQUIRE(menutick.find("MISSION_GRAPH_SEAM_COOP_STAGE_END);") != std::string::npos);
	REQUIRE(menutick.find("void menuTickCoopStartNextStage(void)") != std::string::npos);
	CHECK(count(menutick, "if (g_Vars.stagenum == STAGE_DEEPSEA) {") == 0);

	// The PC end screen asks the same graph whether the action is Credits,
	// and so does a networked mission's Next Mission (B-1193,
	// pdguiEndscreenNetMissionHasNext).
	CHECK(count(bridge, "STAGE_SKEDARRUINS") == 0);
	CHECK(count(bridge, "endscreenMissionLeadsToCredits()") == 4);
	REQUIRE(endscreen.find("\t\treturn scenarioSourceMissionGraphContinueStartsCredits() != 0;") != std::string::npos);

	// Defense's outcome presentation is one rule, read everywhere it shows.
	CHECK(count(endscreen, "g_StageIndex == STAGEINDEX_DEFENSE") == 1);
	CHECK(count(menu, "STAGEINDEX_DEFENSE") == 0);
	CHECK(count(menugfx, "STAGEINDEX_DEFENSE") == 0);
	CHECK(count(endscreen, "if (endscreenStageOutcomeUnknown()) {") == 2);
	CHECK(count(menu, "endscreenStageOutcomeUnknown()") == 3);
	CHECK(count(menugfx, "if (endscreenStageOutcomeUnknown()) {") == 1);

	// The modules exist, and conformance knows them.
	for (const char *row : { "{ \"og.mission.start_next_stage\", s_runStartNextStage },",
			"{ \"og.mission.start_credits\", s_runStartCredits },",
			"{ \"og.progress.unlock_alt_title\", s_runUnlockAltTitle }," }) {
		INFO(row);
		CHECK(modules.find(row) != std::string::npos);
	}
	for (const char *name : { "\"og.mission.start_next_stage\"", "\"og.mission.start_credits\"",
			"\"og.progress.unlock_alt_title\"", "\"event.mission.continue\"", "\"event.mission.completed\"" }) {
		INFO(name);
		CHECK(conformance.find(name) != std::string::npos);
	}
}

TEST_CASE("every mission graph under PD2_MISSION_GRAPH_ROOT compiles and runs its status message",
	"[.][runner-sweep]")
{
	const char *root = std::getenv("PD2_MISSION_GRAPH_ROOT");
	if (!root || !root[0]) {
		WARN("PD2_MISSION_GRAPH_ROOT is not set; nothing to sweep");
		return;
	}
	size_t checked = 0;
	size_t wired = 0;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".pdmission") continue;
		mod_archive_t *archive = modArchiveOpen(entry.path().string().c_str());
		REQUIRE(archive != nullptr);
		const s32 idx = modArchiveFindEntry(archive, "mission.graph.json");
		u32 size = 0;
		char *text = idx >= 0 ? static_cast<char *>(modArchiveExtractAlloc(archive, idx, &size)) : nullptr;
		modArchiveClose(archive);
		INFO(entry.path().string());
		REQUIRE(text != nullptr);
		Program p(std::string(text, size));
		std::free(text);
		INFO(p.error);
		REQUIRE(p.program != nullptr);
		checked++;
		g_calls.clear();
		const mission_graph_event_t event = statusEvent(MISSION_GRAPH_STATUS_COMPLETE);
		if (missionGraphProgramFire(p.program, &event, runModule, nullptr) == 1 && g_calls.size() == 1 &&
				g_calls[0].kind == "og.hud.objective_status_message") {
			wired++;
		}
	}
	WARN("compiled " << checked << " mission graphs under " << root << ", " << wired << " run the status message");
	CHECK(checked > 0);
	CHECK(wired == checked);
}

// projectile-events.md slice 2: a projectile or entity archive's program.
TEST_CASE("projectile programs take the projectile events, and only they do",
	"[mission-graph][projectile-graph]")
{
	// A v1 projectile graph's data nodes and structural edge, plus a program.
	const std::string projectile = R"({
  "schema": "pd.projectile_graph.v2",
  "asset_id": "base:test__projectile_timed",
  "nodes": [
    { "id": "spawn_state", "kind": "projectile.spawn_state", "params": { "scale": 1 } },
    { "id": "motion", "kind": "projectile.motion", "params": { "motion_kind": "ballistic_throw" } },
    { "id": "fuse", "kind": "event.projectile.timer_expired" },
    { "id": "explode", "kind": "og.test.a" }
  ],
  "edges": [
    { "from": "spawn_state", "to": "motion" },
    { "from": "fuse", "output": "out", "to": "explode", "input": "exec" }
  ]
})";
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileProjectile(projectile.data(),
		projectile.size(), knownModule, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramNodeCount(program) == 2);
	CHECK(missionGraphProgramExecEdgeCount(program) == 1);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) == 1);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_LANDED) == 0);
	CHECK(missionGraphEventIsProjectile(MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED) == 1);
	CHECK(missionGraphEventIsProjectile(MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) == 1);
	CHECK(missionGraphEventIsProjectile(MISSION_GRAPH_EVENT_GLASS_BROKEN) == 0);

	int instance = 0;
	mission_graph_event_t event = {};
	event.kind = MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED;
	event.player = -1;
	g_calls.clear();
	CHECK(missionGraphProgramFire(program, &event, runModule, nullptr) == -1); // no instance
	event.instance = &instance;
	CHECK(missionGraphProgramFire(program, &event, runModule, nullptr) == 1);
	REQUIRE(g_calls.size() == 1);
	CHECK(g_calls[0].node == "explode");
	event.kind = MISSION_GRAPH_EVENT_PROJECTILE_LANDED;
	CHECK(missionGraphProgramFire(program, &event, runModule, nullptr) == 0); // nothing wired
	missionGraphProgramFree(program);

	// A data-only v1 graph is an empty program.
	const std::string dataOnly = R"({ "nodes": [ { "id": "spawn_state", "kind": "projectile.spawn_state" } ], "edges": [] })";
	program = missionGraphProgramCompileProjectile(dataOnly.data(), dataOnly.size(), knownModule, err, sizeof(err));
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramNodeCount(program) == 0);
	missionGraphProgramFree(program);

	// Any other event in a projectile program is refused, and a projectile
	// event in a mission or level graph.
	const std::string objectiveInProjectile = graph(
		R"({ "id": "s", "kind": "event.objective.status_changed" }, { "id": "m", "kind": "og.test.a" })",
		R"({ "from": "s", "output": "out", "to": "m", "input": "exec" })");
	program = missionGraphProgramCompileProjectile(objectiveInProjectile.data(), objectiveInProjectile.size(),
		knownModule, err, sizeof(err));
	CHECK(program == nullptr);
	CHECK(std::string(err).find("is not a projectile event") != std::string::npos);
	missionGraphProgramFree(program);

	const std::string projectileEvent = graph(
		R"({ "id": "f", "kind": "event.projectile.timer_expired" }, { "id": "m", "kind": "og.test.a" })",
		R"({ "from": "f", "output": "out", "to": "m", "input": "exec" })");
	CHECK(refusal(projectileEvent).find("belongs to a projectile or entity graph") != std::string::npos);
	program = missionGraphProgramCompileLevel(projectileEvent.data(), projectileEvent.size(),
		knownModule, err, sizeof(err));
	CHECK(program == nullptr);
	CHECK(std::string(err).find("belongs to a projectile or entity graph") != std::string::npos);
	missionGraphProgramFree(program);
}

// projectile-events.md slice 2: a v2 projectile archive registers its record
// and its program; a program that does not compile refuses the archive.
// Slice 6: a program that wires a fuse arms it (event.projectile.armed).
TEST_CASE("a v2 projectile graph registers its record and its program",
	"[mission-graph][projectile-graph][weapon-graph]")
{
	const std::string v2 = R"({
  "schema": "pd.projectile_graph.v2",
  "asset_id": "base:test__projectile_fuse",
  "graph_id": "test_projectile_fuse",
  "nodes": [
    { "id": "spawn_state", "kind": "projectile.spawn_state", "params": { "scale": 1 } },
    { "id": "motion", "kind": "projectile.motion", "params": { "motion_kind": "ballistic_throw" } },
    { "id": "armed", "kind": "event.projectile.armed" },
    { "id": "armed_arm_fuse", "kind": "og.projectile.arm_fuse" },
    { "id": "fuse", "kind": "event.projectile.timer_expired" },
    { "id": "explode", "kind": "og.projectile.explode" }
  ],
  "edges": [
    { "from": "spawn_state", "to": "motion" },
    { "from": "armed", "output": "out", "to": "armed_arm_fuse", "input": "exec" },
    { "from": "fuse", "output": "out", "to": "explode", "input": "exec" }
  ],
  "exports": [ { "name": "main", "node": "motion" } ]
})";
	// No model: the record resolves a model_catalog_id against the catalog,
	// which pd-tests does not load; the motion makes it a record.
	const char *const id = "base:test__projectile_fuse";
	char err[256] = { 0 };

	weaponGraphRuntimeClearAll();
	const s32 registered = weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id,
		v2.data(), (u32)v2.size(), err, sizeof(err));
	INFO("registration error: " << err);
	REQUIRE(registered == 0);
	const mission_graph_program_t *program = weaponGraphRuntimeProjectileProgram(id);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramNodeCount(program) == 4);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) == 1);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_ARMED) == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_ARMED,
		"og.projectile.arm_fuse") == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED,
		"og.projectile.explode") == 1);

	// A module the projectile table does not know refuses the archive.
	std::string unknown = v2;
	unknown.replace(unknown.find("og.projectile.explode"),
		std::string("og.projectile.explode").size(), "og.projectile.unknown");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, unknown.data(),
		(u32)unknown.size(), err, sizeof(err)) != 0);
	CHECK(std::string(err).find("program refused") != std::string::npos);
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);

	// projectile-events.md slice 4: an explode may name an explosion of the
	// game's table; anything else refuses the archive, with the range named.
	const std::string plain = R"({ "id": "explode", "kind": "og.projectile.explode" })";
	const auto withExplode = [&](const char *node) {
		std::string text = v2;
		text.replace(text.find(plain), plain.size(), node);
		return text;
	};
	const std::string dragon = withExplode(
		R"({ "id": "explode", "kind": "og.projectile.explode", "explosion_type": 23 })");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, dragon.data(),
		(u32)dragon.size(), err, sizeof(err)) == 0);
	CHECK(weaponGraphRuntimeProjectileProgram(id) != nullptr);
	for (const char *node : {
			R"({ "id": "explode", "kind": "og.projectile.explode", "explosion_type": 0 })",
			R"({ "id": "explode", "kind": "og.projectile.explode", "explosion_type": 26 })",
			R"({ "id": "explode", "kind": "og.projectile.explode", "explosion_type": "big" })" }) {
		const std::string bad = withExplode(node);
		INFO(node);
		CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, bad.data(),
			(u32)bad.size(), err, sizeof(err)) != 0);
		CHECK(std::string(err).find("og.projectile.explode needs explosion_type from 1 to 25, or none")
			!= std::string::npos);
		CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);
	}

	// A gate that reads what its event carries is refused at an event that
	// does not carry it; fuse_armed reads the projectile and stands anywhere.
	const auto gated = [&](const char *gate_kind) {
		std::string text = v2;
		const std::string edge = R"({ "from": "fuse", "output": "out", "to": "explode", "input": "exec" })";
		text.replace(text.find(edge), edge.size(),
			R"({ "from": "fuse", "output": "out", "to": "asked", "input": "exec" },
    { "from": "asked", "output": "pass", "to": "explode", "input": "exec" })");
		text.replace(text.find(plain), plain.size(),
			plain + ",\n    { \"id\": \"asked\", \"kind\": \"" + gate_kind + "\" }");
		return text;
	};
	const std::string armed = gated("gate.projectile.fuse_armed");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, armed.data(),
		(u32)armed.size(), err, sizeof(err)) == 0);
	for (const char *gate_kind : { "gate.projectile.trigger_close", "gate.projectile.trigger_character",
			"gate.projectile.damage_not_blast" }) {
		const std::string bad = gated(gate_kind);
		INFO(gate_kind);
		CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, bad.data(),
			(u32)bad.size(), err, sizeof(err)) != 0);
		CHECK(std::string(err).find(std::string(gate_kind) +
			" cannot be answered at event.projectile.timer_expired") != std::string::npos);
		CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);
	}

	// projectile-events.md slice 6: only an arm asks the fuse rule, so a
	// projectile archive's program that wires a fuse and arms nothing would
	// never go off, and the archive is refused.
	const std::string armEdge =
		R"({ "from": "armed", "output": "out", "to": "armed_arm_fuse", "input": "exec" },)";
	const std::string noArm = "event.projectile.timer_expired is wired but "
		"event.projectile.armed reaches no arm that runs a fuse";
	std::string unarmed = v2;
	REQUIRE(unarmed.find(armEdge) != std::string::npos);
	unarmed.replace(unarmed.find(armEdge), armEdge.size(), "");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, unarmed.data(),
		(u32)unarmed.size(), err, sizeof(err)) != 0);
	CHECK(std::string(err).find(noArm) != std::string::npos);
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);
	// The bolt's arm runs no fuse; every other arm does.
	const std::string armFuse = "og.projectile.arm_fuse";
	const auto withArm = [&](const char *arm) {
		std::string text = v2;
		text.replace(text.find(armFuse), armFuse.size(), arm);
		return text;
	};
	const std::string bolt = withArm("og.projectile.arm_bolt");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, bolt.data(),
		(u32)bolt.size(), err, sizeof(err)) != 0);
	CHECK(std::string(err).find(noArm) != std::string::npos);
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);
	for (const char *arm : { "og.projectile.arm_wall_fuse", "og.projectile.arm_struck_fuse",
			"og.projectile.arm_mine_fuse", "og.projectile.arm_remote", "og.projectile.arm_proximity" }) {
		const std::string other = withArm(arm);
		INFO(arm);
		CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, other.data(),
			(u32)other.size(), err, sizeof(err)) == 0);
		const mission_graph_program_t *armedProgram = weaponGraphRuntimeProjectileProgram(id);
		REQUIRE(armedProgram != nullptr);
		CHECK(missionGraphProgramEventReachesModule(armedProgram, MISSION_GRAPH_EVENT_PROJECTILE_ARMED, arm) == 1);
	}
	// An arm is the armed event's choice: wired to another event it would do
	// nothing there, and is refused like any module out of place.
	const std::string fuseEdge = R"({ "from": "fuse", "output": "out", "to": "explode", "input": "exec" })";
	std::string misplaced = v2;
	misplaced.replace(misplaced.find(fuseEdge), fuseEdge.size(), fuseEdge +
		",\n    { \"from\": \"fuse\", \"output\": \"out\", \"to\": \"armed_arm_fuse\", \"input\": \"exec\" }");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, misplaced.data(),
		(u32)misplaced.size(), err, sizeof(err)) != 0);
	CHECK(std::string(err).find("og.projectile.arm_fuse has no effect on event.projectile.timer_expired")
		!= std::string::npos);
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);
	// An entity archive carries its item's fuse alone (the item's projectile
	// archive arms it), so the same fuse with no arm registers there.
	const std::string entity = R"({
  "schema": "pd.entity_graph.v2",
  "asset_id": "base:test__entity_fuse",
  "graph_id": "test_entity_fuse",
  "nodes": [
    { "id": "remote", "kind": "entity.remote_detonatable", "params": { "on_remote_signal": "detonate" } },
    { "id": "fuse", "kind": "event.projectile.timer_expired" },
    { "id": "explode", "kind": "og.projectile.explode" }
  ],
  "edges": [
    { "from": "fuse", "output": "out", "to": "explode", "input": "exec" }
  ],
  "exports": [ { "name": "main", "node": "remote" } ]
})";
	const char *const entityId = "base:test__entity_fuse";
	{
		const s32 entityRegistered = weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_ENTITY, entityId,
			entity.data(), (u32)entity.size(), err, sizeof(err));
		INFO("entity registration error: " << err);
		CHECK(entityRegistered == 0);
		const mission_graph_program_t *entityProgram = weaponGraphRuntimeProjectileProgram(entityId);
		CHECK(entityProgram != nullptr);
		if (entityProgram) {
			CHECK(missionGraphProgramHandlesEvent(entityProgram, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) == 1);
			CHECK(missionGraphProgramHandlesEvent(entityProgram, MISSION_GRAPH_EVENT_PROJECTILE_ARMED) == 0);
		}
	}

	// A v1 graph is records only: no program, and a program node is refused.
	std::string v1 = v2;
	v1.replace(v1.find("pd.projectile_graph.v2"), std::string("pd.projectile_graph.v2").size(),
		"pd.projectile_graph.v1");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, v1.data(),
		(u32)v1.size(), err, sizeof(err)) != 0);
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);

	weaponGraphRuntimeClearAll();
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);
	CHECK(weaponGraphRuntimeProjectileProgram(entityId) == nullptr);
}

// held-weapon-events.md slice 1: a held weapon function graph's program runs
// on this runner, in its own domain (missionGraphProgramCompileWeapon).
TEST_CASE("weapon programs take the weapon events, and only they do",
	"[mission-graph][weapon-graph][held-weapon]")
{
	// The extractor's held function record (romextract_pdweapon.c) plus a
	// program: the record's own event node is inert, its action node is
	// record data.
	const std::string function = R"({
  "schema": "pd.weapon_graph.v2",
  "asset_id": "base:test__weapon",
  "graph_id": "primary",
  "nodes": [
    { "id": "primary_trigger", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary", "trigger": "pressed" } },
    { "id": "primary_action", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_single", "ammo_slot": 0, "flags": 0, "damage": 1 } },
    { "id": "pressed", "kind": "event.weapon.trigger_pressed" },
    { "id": "shot", "kind": "og.test.a" }
  ],
  "edges": [
    { "from": "primary_trigger", "to": "primary_action" },
    { "from": "pressed", "output": "out", "to": "shot", "input": "exec" }
  ],
  "exports": [ { "name": "primary", "node": "primary_action" } ]
})";
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(function.data(), function.size(),
		knownModule, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramNodeCount(program) == 2); // the event and the module; the record is data
	CHECK(missionGraphProgramExecEdgeCount(program) == 1);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED) == 1);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD) == 0);
	CHECK(missionGraphEventIsWeapon(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED) == 1);
	CHECK(missionGraphEventIsWeapon(MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD) == 1);
	CHECK(missionGraphEventIsWeapon(MISSION_GRAPH_EVENT_PROJECTILE_ARMED) == 0);
	CHECK(missionGraphEventIsProjectile(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED) == 0);
	CHECK(missionGraphProgramNodeIndex(program, "shot") == 1);
	CHECK(std::string(missionGraphProgramNodeId(program, 1)) == "shot");
	CHECK(std::string(missionGraphProgramNodeKind(program, 1)) == "og.test.a");
	CHECK(missionGraphProgramNodeIndex(program, "primary_action") == -1);
	CHECK(missionGraphProgramNodeId(program, 2) == nullptr);

	int hand = 0;
	mission_graph_event_t event = {};
	event.kind = MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED;
	event.player = -1;
	g_calls.clear();
	CHECK(missionGraphProgramFire(program, &event, runModule, nullptr) == -1); // no instance
	event.instance = &hand;
	CHECK(missionGraphProgramFire(program, &event, runModule, nullptr) == 1);
	REQUIRE(g_calls.size() == 1);
	CHECK(g_calls[0].node == "shot");
	event.kind = MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD;
	CHECK(missionGraphProgramFire(program, &event, runModule, nullptr) == 0); // nothing wired
	missionGraphProgramFree(program);

	// Every other program's events are refused in a weapon program, and an
	// unknown weapon event; the weapon events are refused everywhere else.
	const auto weaponRefusal = [&](const std::string &json) {
		mission_graph_program_t *refused = missionGraphProgramCompileWeapon(json.data(), json.size(),
			knownModule, err, sizeof(err));
		CHECK(refused == nullptr);
		missionGraphProgramFree(refused);
		return std::string(err);
	};
	for (const char *kind : { "event.projectile.timer_expired", "event.objective.status_changed",
			"event.door.use", "event.weapon.unknown" }) {
		INFO(kind);
		const std::string other = graph(std::string(R"({ "id": "e", "kind": ")") + kind +
			R"(" }, { "id": "m", "kind": "og.test.a" })",
			R"({ "from": "e", "output": "out", "to": "m", "input": "exec" })");
		CHECK(weaponRefusal(other).find("is not a weapon event") != std::string::npos);
	}
	const std::string weaponEvent = graph(
		R"({ "id": "w", "kind": "event.weapon.trigger_pressed" }, { "id": "m", "kind": "og.test.a" })",
		R"({ "from": "w", "output": "out", "to": "m", "input": "exec" })");
	CHECK(refusal(weaponEvent).find("belongs to a weapon function graph") != std::string::npos);
	using Compile = mission_graph_program_t *(*)(const char *, size_t, mission_graph_module_known_fn,
		char *, size_t);
	for (const Compile compile : { Compile(missionGraphProgramCompileLevel),
			Compile(missionGraphProgramCompileProjectile) }) {
		mission_graph_program_t *refused = compile(weaponEvent.data(), weaponEvent.size(), knownModule,
			err, sizeof(err));
		CHECK(refused == nullptr);
		CHECK(std::string(err).find("event") != std::string::npos);
		missionGraphProgramFree(refused);
	}
	// The record's own event node is record data: no execution edge can start there.
	std::string recordExec = function;
	const std::string recordEdge = R"({ "from": "primary_trigger", "to": "primary_action" })";
	recordExec.replace(recordExec.find(recordEdge), recordEdge.size(),
		R"({ "from": "primary_trigger", "output": "out", "to": "shot", "input": "exec" })");
	CHECK(weaponRefusal(recordExec).find("names a missing node ('primary_trigger')") != std::string::npos);
	// So is a v1 record's gate node (gate.cooldown_ready); the weapon gates
	// are the program's.
	const std::string recordGate = graph(
		R"({ "id": "cool", "kind": "gate.cooldown_ready" }, { "id": "w", "kind": "event.weapon.trigger_pressed" })",
		R"({ "from": "w", "output": "out", "to": "cool", "input": "exec" })");
	CHECK(weaponRefusal(recordGate).find("names a missing node ('cool')") != std::string::npos);
}

namespace {

std::vector<std::string> g_trace;
std::map<std::string, s32> g_outcomes; // an asynchronous module's run result, by node

s32 asyncKnown(const char *kind)
{
	const std::string k = kind;
	if (k == "og.test.later") return MISSION_GRAPH_MODULE_ASYNC;
	return k == "og.test.a" || k == "og.test.b" || k == "og.test.c" || k == "gate.weapon.test_open" ||
		k == "gate.weapon.test_broken";
}

s32 asyncRun(const char *kind, const char *node_id, const mission_graph_params_t *,
	const mission_graph_event_t *event, void *)
{
	g_calls.push_back(Call{ kind, node_id, event->status, event->objective_number, event->seam });
	const std::string k = kind;
	if (k == "gate.weapon.test_broken") return MISSION_GRAPH_RUN_GATE_FAILED;
	if (k == "gate.weapon.test_open") return 1;
	if (k == "og.test.later") return g_outcomes[node_id];
	return 1;
}

void traced(const char *node, const char *port, void *)
{
	g_trace.push_back(std::string(node) + "." + port);
}

} // namespace

TEST_CASE("a weapon program's asynchronous modules leave by their outcome, within one activation",
	"[mission-graph][weapon-graph][held-weapon]")
{
	const std::string json = graph(
		R"({ "id": "press", "kind": "event.weapon.trigger_pressed" },
		   { "id": "open", "kind": "gate.weapon.test_open" },
		   { "id": "shot", "kind": "og.test.later" },
		   { "id": "done", "kind": "og.test.a" },
		   { "id": "alternative", "kind": "og.test.b" },
		   { "id": "tidy", "kind": "og.test.c" },
		   { "id": "broken", "kind": "gate.weapon.test_broken" },
		   { "id": "unanswered", "kind": "og.test.a" })",
		R"({ "from": "press", "output": "out", "to": "open", "input": "exec" },
		   { "from": "press", "output": "out", "to": "tidy", "input": "exec" },
		   { "from": "press", "output": "out", "to": "broken", "input": "exec" },
		   { "from": "open", "output": "pass", "to": "shot", "input": "exec" },
		   { "from": "shot", "output": "completed", "to": "done", "input": "exec" },
		   { "from": "shot", "output": "blocked", "to": "alternative", "input": "exec" },
		   { "from": "done", "output": "out", "to": "tidy", "input": "exec" },
		   { "from": "broken", "output": "fail", "to": "alternative", "input": "exec" },
		   { "from": "broken", "output": "failed", "to": "unanswered", "input": "exec" })");
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(),
		asyncKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	mission_graph_activation_t *activation = missionGraphActivationCreate(program);
	REQUIRE(activation != nullptr);
	missionGraphActivationSetTrace(activation, traced, nullptr);

	int hand = 0;
	mission_graph_event_t event = {};
	event.kind = MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED;
	event.player = -1;
	event.instance = &hand;

	// The shot is accepted and goes on: the walk leaves it pending. A gate
	// that cannot be answered leaves by "failed", not by "fail".
	g_calls.clear(); g_trace.clear();
	g_outcomes["shot"] = MISSION_GRAPH_RUN_PENDING;
	CHECK(missionGraphActivationBegin(activation, &event) == 1);
	CHECK(missionGraphActivationWalk(activation, &event, asyncRun, nullptr) == 3); // tidy, shot, unanswered
	std::vector<std::string> nodes;
	for (const Call &call : g_calls) nodes.push_back(call.node);
	CHECK(nodes == std::vector<std::string>{ "open", "tidy", "broken", "shot", "unanswered" });
	CHECK(g_trace == std::vector<std::string>{ "press.out", "open.pass", "tidy.out", "broken.failed",
		"unanswered.out" });

	// It completes later: its outcome joins the same activation, and a node
	// the activation already ran (tidy) does not run again.
	g_calls.clear(); g_trace.clear();
	CHECK(missionGraphActivationRoute(activation, missionGraphProgramNodeIndex(program, "shot"),
		MISSION_GRAPH_RUN_COMPLETED) == 1);
	CHECK(missionGraphActivationWalk(activation, &event, asyncRun, nullptr) == 1);
	REQUIRE(g_calls.size() == 1);
	CHECK(g_calls[0].node == "done");
	CHECK(g_trace == std::vector<std::string>{ "shot.completed", "done.out" });
	// Only a pending asynchronous module routes, and only by an outcome.
	CHECK(missionGraphActivationRoute(activation, missionGraphProgramNodeIndex(program, "done"),
		MISSION_GRAPH_RUN_COMPLETED) == 0);
	CHECK(missionGraphActivationRoute(activation, missionGraphProgramNodeIndex(program, "shot"),
		MISSION_GRAPH_RUN_OUT) == 0);
	CHECK(missionGraphActivationRoute(activation, 99, MISSION_GRAPH_RUN_COMPLETED) == 0);

	// A new activation: every node may run again. A shot blocked at once
	// leaves by "blocked" in the same breadth-first walk.
	g_calls.clear(); g_trace.clear();
	g_outcomes["shot"] = MISSION_GRAPH_RUN_BLOCKED;
	CHECK(missionGraphActivationBegin(activation, &event) == 1);
	CHECK(missionGraphActivationWalk(activation, &event, asyncRun, nullptr) == 4);
	nodes.clear();
	for (const Call &call : g_calls) nodes.push_back(call.node);
	CHECK(nodes == std::vector<std::string>{ "open", "tidy", "broken", "shot", "unanswered", "alternative" });
	CHECK(std::count(nodes.begin(), nodes.end(), "alternative") == 1);
	// The one-shot walk is the same walk.
	g_calls.clear();
	CHECK(missionGraphProgramFire(program, &event, asyncRun, nullptr) == 4);
	CHECK(g_calls.size() == 6);
	// A malformed event walks nothing.
	mission_graph_event_t bare = event;
	bare.instance = nullptr;
	CHECK(missionGraphActivationBegin(activation, &bare) == 0);
	CHECK(missionGraphActivationWalk(activation, &bare, asyncRun, nullptr) == -1);
	missionGraphActivationFree(activation);
	missionGraphProgramFree(program);

	// An asynchronous module leaves by its outcome only, a synchronous one by
	// "out" only, and "failed" is a weapon program's gate port.
	const auto weaponRefusal = [&](const std::string &edge) {
		const std::string bad = graph(
			R"({ "id": "press", "kind": "event.weapon.trigger_pressed" },
			   { "id": "open", "kind": "gate.weapon.test_open" },
			   { "id": "shot", "kind": "og.test.later" },
			   { "id": "done", "kind": "og.test.a" })",
			R"({ "from": "press", "output": "out", "to": "open", "input": "exec" },)" + edge);
		mission_graph_program_t *refused = missionGraphProgramCompileWeapon(bad.data(), bad.size(),
			asyncKnown, err, sizeof(err));
		CHECK(refused == nullptr);
		missionGraphProgramFree(refused);
		return std::string(err);
	};
	CHECK(weaponRefusal(R"({ "from": "shot", "output": "out", "to": "done", "input": "exec" })")
		.find("which finishes later, and needs output") != std::string::npos);
	CHECK(weaponRefusal(R"({ "from": "shot", "to": "done", "input": "exec" })")
		.find("which finishes later, and needs output") != std::string::npos);
	CHECK(weaponRefusal(R"({ "from": "done", "output": "completed", "to": "shot", "input": "exec" })")
		.find("output must be \"out\"") != std::string::npos);
	CHECK(weaponRefusal(R"({ "from": "open", "output": "blocked", "to": "shot", "input": "exec" })")
		.find("needs output \"pass\", \"fail\" or \"failed\"") != std::string::npos);
	const std::string missionGate = graph(
		R"({ "id": "s", "kind": "event.objective.status_changed" }, { "id": "g", "kind": "gate.objective.status", "status": "complete" }, { "id": "m", "kind": "og.test.a" })",
		R"({ "from": "s", "output": "out", "to": "g", "input": "exec" }, { "from": "g", "output": "failed", "to": "m", "input": "exec" })");
	CHECK(refusal(missionGate).find("needs output \"pass\" or \"fail\"") != std::string::npos);
	// Outside a weapon program an asynchronous answer is an ordinary module.
	const std::string projectile = R"({ "nodes": [
		{ "id": "fuse", "kind": "event.projectile.timer_expired" }, { "id": "later", "kind": "og.test.later" },
		{ "id": "next", "kind": "og.test.a" } ], "edges": [
		{ "from": "fuse", "output": "out", "to": "later", "input": "exec" },
		{ "from": "later", "output": "out", "to": "next", "input": "exec" } ] })";
	program = missionGraphProgramCompileProjectile(projectile.data(), projectile.size(), asyncKnown,
		err, sizeof(err));
	INFO(err);
	CHECK(program != nullptr);
	missionGraphProgramFree(program);
}

// held-weapon-events.md slice 1: a held function graph of pd.weapon_graph.v2
// is the v1 record plus a program. The record registers as v1's does; the
// program is the function's, compiled when the weapon registers.
TEST_CASE("a v2 held function graph registers its record and its program",
	"[mission-graph][weapon-graph][held-weapon]")
{
	const std::string primary = R"({
  "schema": "pd.weapon_graph.v2",
  "asset_id": "base:test__weapon",
  "graph_id": "primary",
  "nodes": [
    { "id": "primary_trigger", "kind": "event.trigger_pressed", "subgraph": "primary", "params": { "mode": "primary", "trigger": "pressed" } },
    { "id": "primary_action", "kind": "fire.hitscan", "subgraph": "primary", "params": { "mode": "primary", "function_type": "shoot_single", "ammo_slot": 0, "flags": 0, "damage": 1.5, "spread": 2 } },
    { "id": "pressed", "kind": "event.weapon.trigger_pressed" }
  ],
  "edges": [
    { "from": "primary_trigger", "to": "primary_action" }
  ],
  "exports": [ { "name": "primary", "node": "primary_action" } ]
})";
	const std::string secondary = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "base:test__weapon",
  "graph_id": "secondary",
  "nodes": [
    { "id": "secondary_trigger", "kind": "event.trigger_pressed", "subgraph": "secondary", "params": { "mode": "secondary", "trigger": "pressed" } },
    { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary", "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1, "flags": 0, "damage": 0.9, "range": 60 } }
  ],
  "edges": [ { "from": "secondary_trigger", "to": "secondary_action" } ],
  "exports": [ { "name": "secondary", "node": "secondary_action" } ]
})";
	const s32 weaponnum = 2;
	const char *const id = "base:test__weapon";
	char err[256] = { 0 };
	CHECK(weaponGraphTextIsHeldProgram(primary.data(), (u32)primary.size()) == 1);
	CHECK(weaponGraphTextIsHeldProgram(secondary.data(), (u32)secondary.size()) == 0);

	weaponGraphRuntimeClearAll();
	const s32 registered = weaponGraphRuntimeRegisterWeaponSourceJson(weaponnum, id,
		primary.data(), (u32)primary.size(), secondary.data(), (u32)secondary.size(),
		nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, err, sizeof(err));
	INFO("registration error: " << err);
	REQUIRE(registered == 0);
	const weapon_graph_held_function_t *held = weaponGraphRuntimeGetHeldFunction(weaponnum, 0);
	REQUIRE(held != nullptr);
	CHECK(held->has_damage == 1);
	CHECK(held->damage == Approx(1.5f));
	CHECK(held->has_spread == 1);
	REQUIRE(weaponGraphRuntimeGetHeldFunction(weaponnum, 1) != nullptr);
	const mission_graph_program_t *program = weaponGraphRuntimeHeldProgram(weaponnum, 0);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramNodeCount(program) == 1); // the program's event; the record is data
	CHECK(missionGraphProgramEventNodeCount(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED) == 1);
	CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 1) == nullptr); // a v1 function
	CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 2) == nullptr);

	// The program is part of the weapon's identity: the same record without
	// it, or with another program, digests differently.
	// Each IR is large; keep them off the stack.
	std::unique_ptr<weapon_graph_ir_t> with_ir(new weapon_graph_ir_t{}), without_ir(new weapon_graph_ir_t{}),
		other_ir(new weapon_graph_ir_t{}), refused_ir(new weapon_graph_ir_t{});
	weapon_graph_ir_t &with = *with_ir, &without = *without_ir, &other = *other_ir;
	std::string v1 = primary;
	v1.replace(v1.find("pd.weapon_graph.v2"), std::string("pd.weapon_graph.v2").size(), "pd.weapon_graph.v1");
	v1.replace(v1.find(R"(,
    { "id": "pressed", "kind": "event.weapon.trigger_pressed" })"),
		std::string(R"(,
    { "id": "pressed", "kind": "event.weapon.trigger_pressed" })").size(), "");
	std::string ready = primary;
	ready.replace(ready.find("\"event.weapon.trigger_pressed\""), std::string("\"event.weapon.trigger_pressed\"").size(),
		"\"event.weapon.ready_while_held\"");
	REQUIRE(weaponGraphCompileWeaponSourceJson(id, primary.data(), (u32)primary.size(), secondary.data(),
		(u32)secondary.size(), nullptr, 0, &with, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphCompileWeaponSourceJson(id, v1.data(), (u32)v1.size(), secondary.data(),
		(u32)secondary.size(), nullptr, 0, &without, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphCompileWeaponSourceJson(id, ready.data(), (u32)ready.size(), secondary.data(),
		(u32)secondary.size(), nullptr, 0, &other, err, sizeof(err)) == 0);
	CHECK(with.held_program_json[0] != nullptr);
	CHECK(with.held_program_json[1] == nullptr);
	CHECK(without.held_program_json[0] == nullptr);
	CHECK(with.node_count == without.node_count); // the record is the same
	CHECK(std::string(with.ir_sha256) != without.ir_sha256);
	CHECK(std::string(with.ir_sha256) != other.ir_sha256);
	weaponGraphIrFree(&with);
	weaponGraphIrFree(&without);
	weaponGraphIrFree(&other);

	// A program the held module table cannot run refuses the whole weapon.
	std::string unknown = primary;
	unknown.replace(unknown.find(R"({ "id": "pressed", "kind": "event.weapon.trigger_pressed" })"),
		std::string(R"({ "id": "pressed", "kind": "event.weapon.trigger_pressed" })").size(),
		R"({ "id": "pressed", "kind": "event.weapon.trigger_pressed" }, { "id": "shot", "kind": "og.test.a" })");
	unknown.replace(unknown.find(R"({ "from": "primary_trigger", "to": "primary_action" })"),
		std::string(R"({ "from": "primary_trigger", "to": "primary_action" })").size(),
		R"({ "from": "primary_trigger", "to": "primary_action" }, { "from": "pressed", "output": "out", "to": "shot", "input": "exec" })");
	CHECK(weaponGraphRuntimeRegisterWeaponSourceJson(weaponnum, id, unknown.data(), (u32)unknown.size(),
		secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
		err, sizeof(err)) != 0);
	CHECK(std::string(err).find("base:test__weapon primary program refused: native module 'og.test.a' does not exist")
		!= std::string::npos);
	CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 0) == nullptr);
	CHECK(weaponGraphRuntimeGetHeldFunction(weaponnum, 0) == nullptr);

	// The held_single_shot.v1 profile is the profile executor's, not a record.
	const std::string profile = R"({"schema":"pd.weapon_graph.v2","profile":"held_single_shot.v1",
		"asset_id":"base:test__weapon","graph_id":"primary","mode":"primary","nodes":[
		{"id":"press","kind":"event.trigger_pressed"}],"edges":[],"exports":[{"name":"trigger_pressed","node":"press"}]})";
	CHECK(weaponGraphTextIsHeldProgram(profile.data(), (u32)profile.size()) == 0);
	weapon_graph_ir_t &refused = *refused_ir;
	CHECK(weaponGraphCompileJson(ASSET_WEAPON, profile.data(), (u32)profile.size(), &refused,
		err, sizeof(err)) != 0);
	CHECK(std::string(err).find("profile executor") != std::string::npos);
	// A v2 function graph names its function in graph_id; any other v2
	// weapon graph is a joined one (the Weapons creator's), whose program
	// nodes name theirs in subgraph (slice 2).
	std::string unnamed = primary;
	unnamed.replace(unnamed.find("\"graph_id\": \"primary\""), std::string("\"graph_id\": \"primary\"").size(),
		"\"graph_id\": \"first\"");
	CHECK(weaponGraphCompileJson(ASSET_WEAPON, unnamed.data(), (u32)unnamed.size(), &refused,
		err, sizeof(err)) != 0);
	CHECK(std::string(err).find("names each program node's function in subgraph") != std::string::npos);
	std::string joined = unnamed;
	joined.replace(joined.find(R"({ "id": "pressed", "kind": "event.weapon.trigger_pressed" })"),
		std::string(R"({ "id": "pressed", "kind": "event.weapon.trigger_pressed" })").size(),
		R"({ "id": "pressed", "kind": "event.weapon.trigger_pressed", "subgraph": "secondary" })");
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, joined.data(), (u32)joined.size(), &refused,
		err, sizeof(err)) == 0);
	CHECK(refused.held_program_json[0] == nullptr);
	REQUIRE(refused.held_program_json[1] != nullptr);
	CHECK(std::string(refused.held_program_json[1]).find("event.weapon.trigger_pressed") != std::string::npos);
	weaponGraphIrFree(&refused);

	weaponGraphRuntimeClearAll();
	CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 0) == nullptr);
}

// held-weapon-events.md slice 2: the held weapon rule table.
TEST_CASE("the held weapon rule table names its modules, gates and events",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	for (s32 m = 0; m < WEAPON_MODULE_COUNT; m++) {
		const u32 bit = 1u << m;
		const char *kind = weaponRulesModuleKind(bit);
		REQUIRE(kind != nullptr);
		CHECK(std::string(kind).rfind("og.weapon.", 0) == 0);
		CHECK(weaponRulesModuleBit(kind) == bit);
	}
	CHECK(weaponRulesModuleKind(WEAPON_MODULE_FIRE_ONCE | WEAPON_MODULE_RELOAD) == nullptr);
	CHECK(weaponRulesModuleBit("og.projectile.explode") == 0);
	CHECK(weaponRulesModuleBit(nullptr) == 0);
	for (s32 g = 0; g < WEAPON_GATE_COUNT; g++) {
		const u32 gate = WEAPON_RULE_GATE_HAS_AMMO << g;
		const char *kind = weaponRulesGateKind(gate);
		REQUIRE(kind != nullptr);
		CHECK(std::string(kind).rfind("gate.weapon.", 0) == 0);
		CHECK(weaponRulesGateBit(kind) == gate);
	}
	CHECK(weaponRulesGateBit("gate.projectile.fuse_armed") == 0);
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_FIRE_ONCE)) == "og.weapon.fire_once");
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_DRY_FIRE)) == "og.weapon.dry_fire");
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_RELOAD)) == "og.weapon.reload");
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_SET_STATE)) == "og.weapon.set_state");

	// Every weapon event in kind order, the runner's and the table's names
	// agreeing, with what its seam applies.
	std::vector<s32> kinds;
	for (s32 e = 0; weaponRulesEventAt(e) >= 0; e++) kinds.push_back(weaponRulesEventAt(e));
	CHECK(kinds == std::vector<s32>{ MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED,
		MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD, MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY,
		MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED, MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE });
	for (s32 kind : kinds) {
		INFO(kind);
		CHECK(missionGraphEventIsWeapon(kind) == 1);
		const std::string name = weaponRulesEventName(kind);
		const std::string json = graph("{ \"id\": \"e\", \"kind\": \"" + name + "\" }", "");
		char err[256] = { 0 };
		mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(),
			knownModule, err, sizeof(err));
		INFO(err);
		REQUIRE(program != nullptr);
		CHECK(missionGraphProgramEventNodeCount(program, kind) == 1);
		missionGraphProgramFree(program);
	}
	CHECK(weaponRulesEventName(MISSION_GRAPH_EVENT_PROJECTILE_ARMED) == nullptr);
	CHECK(weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED)
		== (WEAPON_MODULE_IDLE_ACTIONS | WEAPON_MODULE_USE_AMMO | WEAPON_MODULE_SET_STATE));
	CHECK(weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY)
		== (WEAPON_MODULE_IDLE_ACTIONS | WEAPON_MODULE_USE_AMMO | WEAPON_MODULE_SET_STATE));
	CHECK(weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED) == WEAPON_MODULE_SET_STATE);
	CHECK(weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE) == WEAPON_MODULE_SET_STATE);

	// The original's idle rule and its rounds.
	for (s32 kind : { MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD }) {
		CHECK(weaponRulesNativeIdle(kind, 3, 0) == WEAPON_MODULE_FIRE_ONCE);
		CHECK(weaponRulesNativeIdle(kind, 1, 0) == WEAPON_MODULE_FIRE_ONCE);
		CHECK(weaponRulesNativeIdle(kind, 0, 0) == WEAPON_MODULE_DRY_FIRE);
		CHECK(weaponRulesNativeIdle(kind, -1, 0) == 0);
		CHECK(weaponRulesNativeIdle(kind, 1, 1) == 0);
		CHECK(weaponRulesNativeIdle(kind, 0, 1) == WEAPON_MODULE_RELOAD);
	}
	CHECK(weaponRulesNativeIdle(MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY, 0, 0) == WEAPON_MODULE_RELOAD);
	CHECK(weaponRulesNativeIdle(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED, 0, 0) == 0);
	CHECK(weaponRulesNativeIdle(MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE, 1, 0) == 0);
	CHECK(weaponRulesNativeRounds(WEAPON_SHOTGUN, FUNC_SECONDARY) == 2);
	CHECK(weaponRulesNativeRounds(WEAPON_SHOTGUN, FUNC_PRIMARY) == 1);
	CHECK(weaponRulesNativeRounds(WEAPON_TRANQUILIZER, FUNC_SECONDARY) == 4);
	CHECK(weaponRulesNativeRounds(WEAPON_TRANQUILIZER, FUNC_PRIMARY) == 1);
	CHECK(weaponRulesNativeRounds(WEAPON_FALCON2, FUNC_PRIMARY) == 1);
	// The rounds are the ones bondgun.c's ammunition check needs.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("if (info->weaponnum == WEAPON_SHOTGUN && funcnum == FUNC_SECONDARY) {\n"
		"\t\t\t\tminqty = 2;") != std::string::npos);
	REQUIRE(bondgun.find("if (weaponnum == WEAPON_TRANQUILIZER && funcnum == FUNC_SECONDARY) {\n"
		"\t\treturn 4;") != std::string::npos);

	CHECK(weaponRulesRoundsFromText(nullptr) == 1);
	CHECK(weaponRulesRoundsFromText("2") == 2);
	CHECK(weaponRulesRoundsFromText("255") == 255);
	for (const char *bad : { "", "0", "256", "-1", "2.5", "two", "$rounds" }) {
		INFO(bad);
		CHECK(weaponRulesRoundsFromText(bad) == -1);
	}
	CHECK(weaponRulesBoolFromText("true") == 1);
	CHECK(weaponRulesBoolFromText("false") == 0);
	CHECK(weaponRulesBoolFromText("1") == -1);
	CHECK(weaponRulesBoolFromText(nullptr) == -1);
}

namespace {

// The base idle wiring as the extractor writes it (romextract_pdweapon.c,
// s_emitHeldProgramNodes and s_emitHeldProgramEdges) for one function: the
// attack is the trigger node `attack` (its kind and settings) and, when
// use_rounds is above 0, og.weapon.use_ammo with those rounds.
std::string baseIdleProgram(const std::string &mode, s32 rounds,
	const std::string &attack = "\"kind\": \"og.weapon.fire_once\"", s32 use_rounds = 1)
{
	const std::string m = mode;
	const std::string useNode = use_rounds > 0
		? ",{ \"id\": \"" + m + "_use_ammo\", \"kind\": \"og.weapon.use_ammo\", \"rounds\": " +
			std::to_string(use_rounds) + " }"
		: std::string();
	const std::string useEdge = use_rounds > 0
		? "{ \"from\": \"" + m + "_attack\", \"output\": \"out\", \"to\": \"" + m + "_use_ammo\", \"input\": \"exec\" },"
		: std::string();
	return "{ \"schema\": \"pd.weapon_graph.v2\", \"asset_id\": \"base:test__weapon\", \"graph_id\": \"" + m +
		"\", \"nodes\": ["
		"{ \"id\": \"" + m + "_trigger\", \"kind\": \"event.trigger_pressed\", \"subgraph\": \"" + m +
		"\", \"params\": { \"mode\": \"" + m + "\", \"trigger\": \"pressed\" } },"
		"{ \"id\": \"" + m + "_action\", \"kind\": \"fire.hitscan\", \"subgraph\": \"" + m +
		"\", \"params\": { \"mode\": \"" + m + "\", \"function_type\": \"shoot_single\", \"ammo_slot\": 0, \"flags\": 0, \"damage\": 1 } },"
		"{ \"id\": \"" + m + "_pressed\", \"kind\": \"event.weapon.trigger_pressed\" },"
		"{ \"id\": \"" + m + "_held\", \"kind\": \"event.weapon.ready_while_held\" },"
		"{ \"id\": \"" + m + "_has_ammo\", \"kind\": \"gate.weapon.has_ammo\", \"rounds\": " + std::to_string(rounds) + " },"
		"{ \"id\": \"" + m + "_has_reserve\", \"kind\": \"gate.weapon.has_reserve\" },"
		"{ \"id\": \"" + m + "_attack\", " + attack + " },"
		"{ \"id\": \"" + m + "_dry_fire\", \"kind\": \"og.weapon.dry_fire\" },"
		"{ \"id\": \"" + m + "_clip_empty\", \"kind\": \"event.weapon.clip_empty\" },"
		"{ \"id\": \"" + m + "_reload\", \"kind\": \"og.weapon.reload\" }" + useNode +
		"], \"edges\": ["
		"{ \"from\": \"" + m + "_trigger\", \"to\": \"" + m + "_action\" },"
		"{ \"from\": \"" + m + "_pressed\", \"output\": \"out\", \"to\": \"" + m + "_has_ammo\", \"input\": \"exec\" },"
		"{ \"from\": \"" + m + "_held\", \"output\": \"out\", \"to\": \"" + m + "_has_ammo\", \"input\": \"exec\" },"
		"{ \"from\": \"" + m + "_has_ammo\", \"output\": \"pass\", \"to\": \"" + m + "_attack\", \"input\": \"exec\" }," + useEdge +
		"{ \"from\": \"" + m + "_has_ammo\", \"output\": \"fail\", \"to\": \"" + m + "_has_reserve\", \"input\": \"exec\" },"
		"{ \"from\": \"" + m + "_has_reserve\", \"output\": \"pass\", \"to\": \"" + m + "_dry_fire\", \"input\": \"exec\" },"
		"{ \"from\": \"" + m + "_clip_empty\", \"output\": \"out\", \"to\": \"" + m + "_reload\", \"input\": \"exec\" }"
		"], \"exports\": [ { \"name\": \"" + m + "\", \"node\": \"" + m + "_action\" } ] }";
}

s32 heldKnown(const char *kind)
{
	return weaponRulesModuleBit(kind) != 0 || weaponRulesGateBit(kind) != 0;
}

} // namespace

TEST_CASE("the base idle wiring chooses what the original does, for every hand state",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	int instance = 0;
	for (s32 rounds : { 1, 2, 4 }) {
		const std::string json = baseIdleProgram("primary", rounds);
		char err[256] = { 0 };
		mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(),
			heldKnown, err, sizeof(err));
		INFO(err);
		REQUIRE(program != nullptr);
		for (s32 kind : { MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD,
				MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY }) {
			for (s32 uses = 0; uses <= 1; uses++)
			for (s32 clip = 1; clip <= 8; clip++)
			for (s32 loaded = 0; loaded <= clip; loaded++)
			for (s32 reserve = 0; reserve <= 1; reserve++) {
				// The walk the seam runs (weapon_rules.h weaponRulesWalk).
				weapon_rule_walk_t walk{};
				weapon_rule_states_t states{};
				walk.weaponnum = -1;
				walk.hand = weapon_rule_hand_t{ uses, loaded, clip, reserve, 1 };
				walk.states = &states;
				walk.param_text = weaponGraphRuntimeHeldParamText;
				REQUIRE(weaponRulesWalk(program, kind, 0, "base:test__weapon", &instance, &walk) == 1);
				const u32 chosen = walk.modules & WEAPON_MODULE_IDLE_ACTIONS;
				const u32 native = weaponRulesNativeIdle(kind,
					weaponRulesNativeAmmoStatus(&walk.hand, rounds), 0);
				INFO("rounds=" << rounds << " event=" << kind << " uses=" << uses << " clip=" << clip
					<< " loaded=" << loaded << " reserve=" << reserve);
				CHECK(chosen == native);
			}
		}
		missionGraphProgramFree(program);
	}

	// The extractor writes exactly this wiring, from the table's rounds.
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("jw_field_str(&w, \"schema\", \"pd.weapon_graph.v2\", 0);") != std::string::npos);
	REQUIRE(extractor.find("\"pressed\", \"held\", \"has_ammo\", \"has_reserve\", \"attack\", \"dry_fire\", "
		"\"clip_empty\", \"reload\",\n\t\t\"use_ammo\",") != std::string::npos);
	REQUIRE(extractor.find("\"event.weapon.trigger_pressed\", \"event.weapon.ready_while_held\",\n"
		"\t\tWEAPON_GATE_HAS_AMMO_KIND, WEAPON_GATE_HAS_RESERVE_KIND,\n"
		"\t\tNULL, \"og.weapon.dry_fire\",\n"
		"\t\t\"event.weapon.clip_empty\", \"og.weapon.reload\",\n"
		"\t\t\"og.weapon.use_ammo\",") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramEdge(w, pressed, \"out\", has_ammo, 0);\n"
		"\ts_emitHeldProgramEdge(w, held, \"out\", has_ammo, 0);\n"
		"\ts_emitHeldProgramEdge(w, has_ammo, \"pass\", attack, 0);\n"
		"\tif (plan->rounds > 0) {\n"
		"\t\ts_emitHeldProgramEdge(w, attack, \"out\", use_ammo, 0);\n"
		"\t}\n"
		"\ts_emitHeldProgramEdge(w, has_ammo, \"fail\", has_reserve, 0);\n"
		"\ts_emitHeldProgramEdge(w, has_reserve, \"pass\", dry_fire, 0);\n"
		"\ts_emitHeldProgramEdge(w, clip_empty, \"out\", reload, 1);") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramNodes(&w, mode_name, weaponRulesNativeRounds(weapon_id, mode), &plan);")
		!= std::string::npos);
	REQUIRE(extractor.find("const s32 program = func != NULL && func->type != INVENTORYFUNCTYPE_NONE;")
		!= std::string::npos);

	// The idle seam asks the program where the original decided, and keeps
	// the original's branches after it.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("act = weaponIdleModules(event, handnum, hand, sp34, weaponRulesNativeIdle(event, sp34, none),\n"
		"\t\t\t\t\t&record);") != std::string::npos);
	REQUIRE(bondgun.find("event = weaponEventTakePress(handnum) ? MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED\n"
		"\t\t\t\t\t: MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD;") != std::string::npos);
	REQUIRE(bondgun.find("|| (sp34 > 0 && hand->activatesecondary && hand->gset.weaponfunc == FUNC_SECONDARY);")
		!= std::string::npos);
	REQUIRE(bondgun.find("weaponEventTrigger(handnum, hand, on, hand->triggerprev);") != std::string::npos);
	REQUIRE(bondgun.find("if (prev_state == HANDSTATE_RELOAD && state == HANDSTATE_IDLE) {\n"
		"\t\t\tweaponEventReloadDone(handnum, hand);") != std::string::npos);
	// The original's own trigger test is gone from the idle seam.
	CHECK(bondgun.find("if (hand->triggeron || (hand->activatesecondary && hand->gset.weaponfunc == FUNC_SECONDARY)) {")
		== std::string::npos);
	const std::string events = readSource("port/src/projectile_events.c");
	REQUIRE(events.find("weaponEventsReport(why);") != std::string::npos);
}

TEST_CASE("a held program registers only with modules its events apply and settings it can read",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	const std::string secondary = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "base:test__weapon",
  "graph_id": "secondary",
  "nodes": [
    { "id": "secondary_trigger", "kind": "event.trigger_pressed", "subgraph": "secondary", "params": { "mode": "secondary", "trigger": "pressed" } },
    { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary", "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1, "flags": 0, "damage": 0.9, "range": 60 } }
  ],
  "edges": [ { "from": "secondary_trigger", "to": "secondary_action" } ],
  "exports": [ { "name": "secondary", "node": "secondary_action" } ]
})";
	const s32 weaponnum = 2;
	const char *const id = "base:test__weapon";
	char err[256] = { 0 };
	const auto registers = [&](const std::string &primary, const char *variables = nullptr) {
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(weaponnum, id, primary.data(), (u32)primary.size(),
			secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0,
			variables, variables ? (u32)std::strlen(variables) : 0, nullptr, 0, err, sizeof(err)) == 0;
	};

	weaponGraphRuntimeClearAll();
	const std::string base = baseIdleProgram("primary", 2);
	REQUIRE(registers(base));
	const mission_graph_program_t *program = weaponGraphRuntimeHeldProgram(weaponnum, 0);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED) == 1);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY) == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD,
		"og.weapon.dry_fire") == 1);

	// A module wired to an event whose seam does not apply it is refused.
	const auto replaced = [&](const std::string &from, const std::string &to) {
		std::string text = base;
		REQUIRE(text.find(from) != std::string::npos);
		text.replace(text.find(from), from.size(), to);
		return text;
	};
	const std::string releaseFires = replaced("\"kind\": \"event.weapon.clip_empty\"",
		"\"kind\": \"event.weapon.trigger_released\"");
	CHECK_FALSE(registers(releaseFires));
	CHECK(std::string(err).find("og.weapon.reload has no effect on event.weapon.trigger_released") != std::string::npos);
	CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 0) == nullptr);

	// has_ammo's rounds is a whole number from 1 to 255.
	for (const char *rounds : { "0", "256", "\"two\"", "2.5", "true" }) {
		INFO(rounds);
		CHECK_FALSE(registers(replaced("\"rounds\": 2", std::string("\"rounds\": ") + rounds)));
		CHECK(std::string(err).find("gate.weapon.has_ammo needs rounds from 1 to 255") != std::string::npos);
	}
	CHECK(registers(replaced("\"rounds\": 2", "\"rounds\": 255")));
	CHECK(registers(replaced(", \"rounds\": 2", "")));

	// A "$name" setting reads the weapon's variables, and one that names no
	// variable refuses the weapon.
	const std::string fromVariable = replaced("\"rounds\": 2", "\"rounds\": \"$blast_rounds\"");
	const char *variables = R"({"schema":"pd.weapon_variables.v1","asset_id":"base:test__weapon",
		"variables":[{"name":"blast_rounds","value":3}]})";
	CHECK_FALSE(registers(fromVariable));
	CHECK(std::string(err).find("needs rounds") != std::string::npos);
	REQUIRE(registers(fromVariable, variables));
	program = weaponGraphRuntimeHeldProgram(weaponnum, 0);
	REQUIRE(program != nullptr);
	struct ParamProbe {
		std::string rounds;
		static s32 visit(const char *kind, const mission_graph_params_t *params, void *user)
		{
			if (weaponRulesGateBit(kind) == WEAPON_RULE_GATE_HAS_AMMO) {
				char buf[16];
				const char *text = weaponGraphRuntimeHeldParamText(2, params, WEAPON_RULE_ROUNDS_PARAM, buf, sizeof(buf));
				static_cast<ParamProbe *>(user)->rounds = text ? text : "";
			}
			return 1;
		}
	} probe;
	missionGraphProgramVisitModuleParams(program, ParamProbe::visit, &probe);
	CHECK(probe.rounds == "3");

	// State gates and setters name a key and a boolean; a hand holds eight
	// keys.
	const auto withState = [&](const std::string &nodes, const std::string &edges) {
		std::string text = base;
		text.replace(text.find("{ \"id\": \"primary_reload\", \"kind\": \"og.weapon.reload\" }"),
			std::string("{ \"id\": \"primary_reload\", \"kind\": \"og.weapon.reload\" }").size(),
			"{ \"id\": \"primary_reload\", \"kind\": \"og.weapon.reload\" }," + nodes);
		const std::string lastEdge = "\"to\": \"primary_reload\", \"input\": \"exec\" }";
		text.replace(text.find(lastEdge), lastEdge.size(), lastEdge + "," + edges);
		return text;
	};
	const std::string stateNodes =
		"{ \"id\": \"armed\", \"kind\": \"gate.weapon.state_is\", \"key\": \"armed\", \"equals\": true },"
		"{ \"id\": \"arm\", \"kind\": \"og.weapon.set_state\", \"key\": \"armed\", \"value\": true }";
	const std::string stateEdges =
		"{ \"from\": \"primary_clip_empty\", \"output\": \"out\", \"to\": \"armed\", \"input\": \"exec\" },"
		"{ \"from\": \"armed\", \"output\": \"fail\", \"to\": \"arm\", \"input\": \"exec\" }";
	CHECK(registers(withState(stateNodes, stateEdges)));
	CHECK_FALSE(registers(withState(
		"{ \"id\": \"armed\", \"kind\": \"gate.weapon.state_is\", \"equals\": true }",
		"{ \"from\": \"primary_clip_empty\", \"output\": \"out\", \"to\": \"armed\", \"input\": \"exec\" }")));
	CHECK(std::string(err).find("gate.weapon.state_is needs a key") != std::string::npos);
	CHECK_FALSE(registers(withState(
		"{ \"id\": \"arm\", \"kind\": \"og.weapon.set_state\", \"key\": \"armed\", \"value\": \"yes\" }",
		"{ \"from\": \"primary_clip_empty\", \"output\": \"out\", \"to\": \"arm\", \"input\": \"exec\" }")));
	CHECK(std::string(err).find("og.weapon.set_state needs value true or false") != std::string::npos);
	std::string nine, nineEdges;
	for (int k = 0; k < 9; k++) {
		nine += std::string(k ? "," : "") + "{ \"id\": \"s" + std::to_string(k) +
			"\", \"kind\": \"og.weapon.set_state\", \"key\": \"k" + std::to_string(k) + "\", \"value\": true }";
		nineEdges += std::string(k ? "," : "") + "{ \"from\": \"primary_clip_empty\", \"output\": \"out\", \"to\": \"s" +
			std::to_string(k) + "\", \"input\": \"exec\" }";
	}
	CHECK_FALSE(registers(withState(nine, nineEdges)));
	CHECK(std::string(err).find("more than 8 state keys") != std::string::npos);

	weaponGraphRuntimeClearAll();
	CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 0) == nullptr);
}

// held-weapon-events.md slice 3: the trigger table.
namespace {

// A function record as the cadence code reads it.
weapon_rule_function_t ruleRecord(s32 weaponnum, s32 func, s32 functype, u32 flags, s32 slot,
	f32 initial = 0.0f, f32 max = 0.0f, s32 accel = 0, s32 decel = 0)
{
	weapon_rule_function_t r{};
	r.weaponnum = weaponnum;
	r.func = func;
	r.functype = functype;
	r.flags = flags;
	r.ammo_slot = slot;
	r.has_rate = functype == 0x0101 || max > 0.0f;
	r.initial_rpm = initial;
	r.max_rpm = max;
	r.accel = accel;
	r.decel = decel;
	return r;
}

struct BaseFunction {
	const char *name;
	weapon_rule_function_t record;
};

// Every shooting cadence the base weapons have (the extracted records, Nov
// 2026): each automatic with its rates and spin, each burst, the single
// shots, the Laser's two functions, a projectile, a throw and a swing.
std::vector<BaseFunction> baseFunctions()
{
	const u32 B2 = WEAPON_RULE_FUNCFLAG_BURST2, B3 = WEAPON_RULE_FUNCFLAG_BURST3;
	const u32 B50 = WEAPON_RULE_FUNCFLAG_BURST50;
	return {
		{ "ar34 primary", ruleRecord(WEAPON_AR34, 0, 0x0101, B3, 0, 750, 750) },
		{ "k7avenger secondary", ruleRecord(WEAPON_K7AVENGER, 1, 0x0101, B3, 0, 950, 950) },
		{ "laptopgun primary", ruleRecord(WEAPON_LAPTOPGUN, 0, 0x0101, B3, 0, 1000, 1000) },
		{ "reaper primary", ruleRecord(WEAPON_REAPER, 0, 0x0101, B3, 0, 60, 1800, 88, 88) },
		{ "cyclone primary", ruleRecord(WEAPON_CYCLONE, 0, 0x0101, 0, 0, 900, 900) },
		{ "cyclone secondary", ruleRecord(WEAPON_CYCLONE, 1, 0x0101, B50, 0, 2000, 2000) },
		{ "callisto secondary", ruleRecord(WEAPON_CALLISTO, 1, 0x0101, 0, 0, 300, 300) },
		{ "rcp120 primary", ruleRecord(WEAPON_RCP120, 0, 0x0101, 0, 0, 1100, 1100) },
		{ "laser secondary", ruleRecord(WEAPON_LASER, 1, 0x0101, 0, -1, 3600, 3600) },
		{ "magsec secondary", ruleRecord(WEAPON_MAGSEC4, 1, 0x0001, B3, 0) },
		{ "shotgun secondary", ruleRecord(WEAPON_SHOTGUN, 1, 0x0001, B2, 0) },
		{ "shotgun primary", ruleRecord(WEAPON_SHOTGUN, 0, 0x0001, 0, 0) },
		{ "falcon2 primary", ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0) },
		{ "laser primary", ruleRecord(WEAPON_LASER, 0, 0x0001, 0, -1) },
		{ "rocketlauncher primary", ruleRecord(WEAPON_ROCKETLAUNCHER, 0, 0x0201, 0, 0) },
		{ "grenade primary", ruleRecord(WEAPON_GRENADE, 0, 0x0002, 0, 0) },
		{ "combatknife primary", ruleRecord(WEAPON_COMBATKNIFE, 0, 0x0003, 0, -1) },
	};
}

// The original's trigger tick (bondgun.c bgun0f09a3f8 before slice 3),
// kept here to prove weaponRulesCadenceStep is the same code.
struct OrigHand {
	s32 triggeron = 0, fired = 0, aiming = 0, burstbullets = 0, stateframes = 0;
	f32 spin = 0.0f;
	s32 loaded = 0, has_clip = 1;
	s32 shotstotake = 0;
	f32 shotremainder = 0.0f;
	s32 firing = 0;
};

s32 origStep(const weapon_rule_function_t &f, OrigHand &h, f32 lv)
{
	bool burst = false, smallburst = false;
	const u32 funcflags = f.flags;
	const bool legacyauto = (f.functype & 0xff00) == 0x100;
	const bool isauto = f.has_rate ? true : legacyauto;
	const s32 turretaccel = isauto ? f.accel : 0;
	const s32 turretdecel = isauto ? f.decel : 0;

	if ((funcflags & WEAPON_RULE_FUNCFLAG_BURST3) && h.burstbullets < 3) {
		if (!h.aiming || !isauto) smallburst = true;
	}
	if ((funcflags & WEAPON_RULE_FUNCFLAG_BURST2) && h.burstbullets < 2) smallburst = true;
	if ((funcflags & WEAPON_RULE_FUNCFLAG_BURST5) && h.burstbullets < 5) smallburst = true;
	if ((funcflags & WEAPON_RULE_FUNCFLAG_BURST50) && h.burstbullets < 50) burst = true;
	if (smallburst) burst = true;
	if (h.triggeron || !h.fired || burst) {
		if (f.ammo_slot >= 0 && h.loaded == 0 && h.has_clip) return -1;
		if (isauto) {
			if (turretaccel > 0) {
				if (h.spin < 1) {
					h.spin += lv / turretaccel;
					if (h.spin > 1) {
						h.spin = 1;
						return 1;
					}
				}
			} else {
				h.spin = 1;
			}
			return 1;
		}
		h.spin = 1;
		if (smallburst) {
			if (h.burstbullets > 0) {
				s32 delay = 3;
				if (f.weaponnum == WEAPON_SHOTGUN) delay = TICKS(13);
				if (h.stateframes < delay) return 0;
			}
			h.stateframes = 0;
		}
		if ((funcflags & WEAPON_RULE_FUNCFLAG_BURST3) && h.burstbullets == 2) smallburst = false;
		if ((funcflags & WEAPON_RULE_FUNCFLAG_BURST2) && h.burstbullets == 1) smallburst = false;
		if ((funcflags & WEAPON_RULE_FUNCFLAG_BURST5) && h.burstbullets == 4) smallburst = false;
		return smallburst ? 1 : 2;
	}
	if (isauto) {
		if (turretdecel > 0) {
			if (h.spin > 0) {
				h.spin -= lv / turretdecel;
				if (h.spin < 0) {
					h.spin = 0;
					return -1;
				}
				return 1;
			}
		} else {
			h.spin = 0;
		}
		return -1;
	}
	return -1;
}

// The original's shot (bondgun.c bgun0f09a6f8 before slice 3): the rounds
// it takes and what it spends.
void origFire(const weapon_rule_function_t &f, OrigHand &h, f32 lv)
{
	bool usesammo = true;
	const bool isauto = f.has_rate ? true : (f.functype & 0xff00) == 0x100;

	h.firing = 1;
	if (isauto) {
		const f32 tmp = f.initial_rpm + (f.max_rpm - f.initial_rpm) * h.spin;
		const f32 tmp2 = tmp / 60.0f * (lv / 60.0f) + h.shotremainder;
		h.shotstotake = (s32)tmp2;
		h.shotremainder = tmp2 - h.shotstotake;
		if (h.shotstotake <= 0) {
			if (!h.fired) h.shotstotake++;
			else h.firing = 0;
		}
	} else {
		h.shotstotake = 1;
		if (f.weaponnum == WEAPON_LASER) usesammo = false;
	}
	h.burstbullets += h.shotstotake;
	if (h.firing) {
		h.fired = 1;
		if (usesammo && f.ammo_slot >= 0) {
			h.loaded -= h.shotstotake;
			if (h.loaded < 0) {
				h.shotstotake += h.loaded;
				h.loaded = 0;
			}
		}
	}
}

// The same tick as bondgun.c runs it now, from the plan.
s32 planStep(const weapon_rule_trigger_t &plan, const weapon_rule_function_t &f, OrigHand &h, f32 lv)
{
	weapon_rule_cadence_t c{};
	c.triggeron = h.triggeron;
	c.fired = h.fired;
	c.aiming = h.aiming;
	c.burstbullets = h.burstbullets;
	c.stateframes = h.stateframes;
	c.spin = h.spin;
	c.clip_empty = weaponRulesClipEmpty(&plan, f.ammo_slot >= 0 && h.has_clip, f.ammo_slot >= 0 ? h.loaded : 0);
	c.lvupdate60 = lv;
	const s32 result = weaponRulesCadenceStep(&plan, &c);
	h.stateframes = c.stateframes;
	h.spin = c.spin;
	return result;
}

void planFire(const weapon_rule_trigger_t &plan, const weapon_rule_function_t &f, OrigHand &h, f32 lv)
{
	const bool isauto = plan.module == WEAPON_MODULE_AUTOMATIC;

	h.firing = 1;
	if (isauto) {
		const f32 tmp = weaponRulesRateAt(&plan, h.spin);
		const f32 tmp2 = tmp / 60.0f * (lv / 60.0f) + h.shotremainder;
		h.shotstotake = (s32)tmp2;
		h.shotremainder = tmp2 - h.shotstotake;
		if (h.shotstotake <= 0) {
			if (!h.fired) h.shotstotake++;
			else h.firing = 0;
		}
	} else {
		h.shotstotake = 1;
	}
	h.burstbullets += h.shotstotake;
	if (h.firing) {
		h.fired = 1;
		if (plan.rounds > 0 && f.ammo_slot >= 0) {
			h.shotstotake = weaponRulesPayShots(plan.rounds, &h.loaded, h.shotstotake);
		}
	}
}

// The base function's trigger node and use_ammo as the extractor writes them
// (romextract_pdweapon.c s_emitHeldTriggerNode: every setting, rates as
// "%.7g").
std::string triggerNodeFields(const weapon_rule_trigger_t &plan)
{
	char rate[32], start[32];
	std::snprintf(rate, sizeof(rate), "%.7g", (double)plan.rate);
	std::snprintf(start, sizeof(start), "%.7g", (double)plan.start_rate);
	if (plan.module == WEAPON_MODULE_BURST) {
		return "\"kind\": \"og.weapon.burst\", \"count\": " + std::to_string(plan.count) +
			", \"gap\": " + std::to_string(plan.gap);
	}
	if (plan.module == WEAPON_MODULE_AUTOMATIC) {
		return std::string("\"kind\": \"og.weapon.automatic\", \"rate\": ") + rate + ", \"start_rate\": " + start +
			", \"spin_up\": " + std::to_string(plan.spin_up) + ", \"spin_down\": " + std::to_string(plan.spin_down) +
			", \"burst\": " + std::to_string(plan.burst) + ", \"aimed_burst\": " + (plan.aimed_burst ? "true" : "false");
	}
	return "\"kind\": \"og.weapon.fire_once\"";
}

} // namespace

TEST_CASE("the trigger table plans each base function's attack as the original does",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_FIRE_ONCE)) == "og.weapon.fire_once");
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_BURST)) == "og.weapon.burst");
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_AUTOMATIC)) == "og.weapon.automatic");
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_USE_AMMO)) == "og.weapon.use_ammo");
	CHECK(WEAPON_MODULE_TRIGGERS == (WEAPON_MODULE_FIRE_ONCE | WEAPON_MODULE_BURST | WEAPON_MODULE_AUTOMATIC));
	// The flag bits are the game's.
	CHECK(WEAPON_RULE_FUNCFLAG_BURST2 == FUNCFLAG_BURST2);
	CHECK(WEAPON_RULE_FUNCFLAG_BURST3 == FUNCFLAG_BURST3);
	CHECK(WEAPON_RULE_FUNCFLAG_BURST5 == FUNCFLAG_BURST5);
	CHECK(WEAPON_RULE_FUNCFLAG_BURST50 == FUNCFLAG_BURST50);

	std::map<std::string, weapon_rule_trigger_t> plans;
	for (const BaseFunction &base : baseFunctions()) {
		weapon_rule_trigger_t plan;
		weaponRulesNativeTrigger(&base.record, &plan);
		plans[base.name] = plan;
	}
	const auto &ar34 = plans["ar34 primary"];
	CHECK(ar34.module == WEAPON_MODULE_AUTOMATIC);
	CHECK(ar34.rate == 750.0f);
	CHECK(ar34.start_rate == 750.0f);
	CHECK(ar34.spin_up == 0);
	CHECK(ar34.burst == 3);
	CHECK(ar34.aimed_burst == 0); // the 3-round burst gives way while aiming
	CHECK(ar34.rounds == 1);
	const auto &reaper = plans["reaper primary"];
	CHECK(reaper.rate == 1800.0f);
	CHECK(reaper.start_rate == 60.0f);
	CHECK(reaper.spin_up == 88);
	CHECK(reaper.spin_down == 88);
	const auto &cyclone = plans["cyclone secondary"];
	CHECK(cyclone.burst == 50);
	CHECK(cyclone.aimed_burst == 1);
	CHECK(plans["cyclone primary"].burst == 0);
	const auto &magsec = plans["magsec secondary"];
	CHECK(magsec.module == WEAPON_MODULE_BURST);
	CHECK(magsec.count == 3);
	CHECK(magsec.gap == 3);
	const auto &shotgun = plans["shotgun secondary"];
	CHECK(shotgun.module == WEAPON_MODULE_BURST);
	CHECK(shotgun.count == 2);
	CHECK(shotgun.gap == TICKS(13));
	CHECK(plans["shotgun primary"].module == WEAPON_MODULE_FIRE_ONCE);
	CHECK(plans["falcon2 primary"].module == WEAPON_MODULE_FIRE_ONCE);
	CHECK(plans["falcon2 primary"].rounds == 1);
	CHECK(plans["laser primary"].rounds == 0);
	CHECK(plans["laser secondary"].module == WEAPON_MODULE_AUTOMATIC);
	CHECK(plans["laser secondary"].rounds == 0); // no clip
	CHECK(plans["rocketlauncher primary"].module == WEAPON_MODULE_FIRE_ONCE);
	CHECK(plans["rocketlauncher primary"].rounds == 1);
	CHECK(plans["grenade primary"].module == WEAPON_MODULE_FIRE_ONCE);
	CHECK(plans["grenade primary"].rounds == 0); // a throw spends in its own code
	CHECK(plans["combatknife primary"].rounds == 0);

	// The Laser's single shot takes nothing even with a clip.
	weapon_rule_function_t laser = ruleRecord(WEAPON_LASER, 0, 0x0001, 0, 0);
	weapon_rule_trigger_t plan;
	weaponRulesNativeTrigger(&laser, &plan);
	CHECK(plan.rounds == 0);
	// Flags the original reads but that make no burst; combined flags.
	weapon_rule_function_t odd = ruleRecord(WEAPON_FALCON2, 0, 0x0001, WEAPON_RULE_FUNCFLAG_BURST50, 0);
	weaponRulesNativeTrigger(&odd, &plan);
	CHECK(plan.module == WEAPON_MODULE_FIRE_ONCE);
	odd.flags = WEAPON_RULE_FUNCFLAG_BURST2 | WEAPON_RULE_FUNCFLAG_BURST3;
	weaponRulesNativeTrigger(&odd, &plan);
	CHECK(plan.module == WEAPON_MODULE_BURST);
	CHECK(plan.count == 2);
	odd.flags = WEAPON_RULE_FUNCFLAG_BURST3 | WEAPON_RULE_FUNCFLAG_BURST5;
	weaponRulesNativeTrigger(&odd, &plan);
	CHECK(plan.count == 3);
	// A record that names a rate is an automatic, whatever its type.
	weapon_rule_function_t rated = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0, 0, 450);
	weaponRulesNativeTrigger(&rated, &plan);
	CHECK(plan.module == WEAPON_MODULE_AUTOMATIC);
	CHECK(plan.rate == 450.0f);
	weaponRulesNativeTrigger(nullptr, &plan);
	CHECK(plan.module == WEAPON_MODULE_FIRE_ONCE);

	// Ticks per shot for simulants and guards: weaponGetNumTicksPerShot's
	// formula for an automatic, none for the rest; at most a byte's 127.
	for (const BaseFunction &base : baseFunctions()) {
		INFO(base.name);
		weaponRulesNativeTrigger(&base.record, &plan);
		s32 expected = 0;
		if (base.record.has_rate && base.record.max_rpm > 0.0f) {
			expected = (s32)(3600.0f / base.record.max_rpm);
			if (expected > 3) expected = TICKS(expected);
		}
		CHECK(weaponRulesTicksPerShot(&plan) == expected);
	}
	weapon_rule_trigger_t slow{};
	slow.module = WEAPON_MODULE_AUTOMATIC;
	slow.rate = 10.0f;
	CHECK(weaponRulesTicksPerShot(&slow) == 127);
	const std::string ticks = readSource("src/game/game_0b0fd0.c");
	REQUIRE(ticks.find("result = 3600.0f / graph->max_rpm;") != std::string::npos);
	REQUIRE(ticks.find("return weaponProgramTicksPerShot((s32)weaponnum, (s32)funcindex, result);")
		!= std::string::npos);
}

TEST_CASE("the trigger's cadence is the original's, for every base function",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	// Trigger patterns: hold, tap, hold then release, each with and without
	// aiming, at one, two and three 60 Hz ticks a frame, from full and
	// nearly empty clips.
	struct Pattern {
		const char *name;
		s32 hold; // ticks the trigger is down from the start
		s32 aim;
	};
	const Pattern patterns[] = {
		{ "hold", 1000, 0 }, { "hold aiming", 1000, 1 }, { "tap", 1, 0 }, { "tap aiming", 1, 1 },
		{ "short hold", 7, 0 }, { "short hold aiming", 7, 1 }, { "release after 40", 40, 0 },
	};
	s32 cases = 0;
	for (const BaseFunction &base : baseFunctions()) {
		const weapon_rule_function_t &f = base.record;
		if (!weaponRulesFunctionShoots(&f)) continue;
		weapon_rule_trigger_t plan;
		weaponRulesNativeTrigger(&f, &plan);
		for (const Pattern &pattern : patterns)
		for (s32 lvi = 1; lvi <= 3; lvi++)
		for (s32 loaded : { 1, 2, 5, 30 }) {
			const f32 lv = (f32)lvi;
			OrigHand a, b;
			a.loaded = b.loaded = loaded;
			a.has_clip = b.has_clip = 1;
			s32 tick = 0;
			bool stoppedA = false, stoppedB = false;
			for (s32 frame = 0; frame < 200 && !(stoppedA && stoppedB); frame++) {
				a.triggeron = b.triggeron = tick < pattern.hold;
				a.aiming = b.aiming = pattern.aim;
				const s32 ra = stoppedA ? -9 : origStep(f, a, lv);
				const s32 rb = stoppedB ? -9 : planStep(plan, f, b, lv);
				INFO(base.name << " " << pattern.name << " lv=" << lvi << " loaded=" << loaded << " frame=" << frame);
				REQUIRE(ra == rb);
				if (ra > 0) {
					origFire(f, a, lv);
					planFire(plan, f, b, lv);
				}
				if (ra < 0 || ra == 2) {
					stoppedA = stoppedB = true;
				}
				REQUIRE(a.spin == b.spin);
				REQUIRE(a.burstbullets == b.burstbullets);
				REQUIRE(a.stateframes == b.stateframes);
				REQUIRE(a.loaded == b.loaded);
				REQUIRE(a.shotstotake == b.shotstotake);
				REQUIRE(a.shotremainder == b.shotremainder);
				REQUIRE(a.fired == b.fired);
				a.stateframes += lvi;
				b.stateframes += lvi;
				tick += lvi;
			}
			cases++;
		}
	}
	CHECK(cases > 300);

	// A plan the original never had: rounds per shot, a free automatic.
	weapon_rule_trigger_t heavy{};
	heavy.module = WEAPON_MODULE_FIRE_ONCE;
	heavy.rounds = 3;
	CHECK(weaponRulesClipEmpty(&heavy, 1, 2) == 1);
	CHECK(weaponRulesClipEmpty(&heavy, 1, 3) == 0);
	CHECK(weaponRulesClipEmpty(&heavy, 0, 0) == 0);
	s32 clip = 7;
	CHECK(weaponRulesPayShots(3, &clip, 1) == 1);
	CHECK(clip == 4);
	CHECK(weaponRulesPayShots(3, &clip, 2) == 1); // pays for one, keeps one round
	CHECK(clip == 1);
	CHECK(weaponRulesPayShots(0, &clip, 4) == 4); // free
	CHECK(clip == 1);
	heavy.rounds = 0;
	CHECK(weaponRulesClipEmpty(&heavy, 1, 0) == 0);

	// bondgun.c runs these steps, from the plan, and no longer reads the
	// burst flags, the Shotgun's gap or the Laser's number itself.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("result = weaponRulesCadenceStep(plan, &cadence);") != std::string::npos);
	REQUIRE(bondgun.find("tmp = weaponRulesRateAt(plan, hand->gs_float1);") != std::string::npos);
	REQUIRE(bondgun.find("hand->shotstotake = weaponRulesPayShots(plan->rounds, &hand->loadedammo[ammoindex],")
		!= std::string::npos);
	REQUIRE(bondgun.find("plan = weaponHandTrigger(hand, &record);") != std::string::npos);
	CHECK(bondgun.find("FUNCFLAG_BURST") == std::string::npos);
	CHECK(bondgun.find("delay = TICKS(13);") == std::string::npos);
	CHECK(bondgun.find("usesammo = false;") == std::string::npos);
}

TEST_CASE("a trigger node's settings default to the function's record and refuse what they cannot read",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	const auto plan = [](u32 module, const weapon_rule_function_t &record, Texts texts, s32 *ok = nullptr) {
		weapon_rule_trigger_t out{};
		const s32 result = weaponRulesApplySettings(module, Texts::get, &texts, &record, &out);
		if (ok) *ok = result;
		return out;
	};
	const weapon_rule_function_t falcon = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0);
	const weapon_rule_function_t shotgun = ruleRecord(WEAPON_SHOTGUN, 1, 0x0001, WEAPON_RULE_FUNCFLAG_BURST2, 0);
	const weapon_rule_function_t reaper = ruleRecord(WEAPON_REAPER, 0, 0x0101, WEAPON_RULE_FUNCFLAG_BURST3, 0,
		60, 1800, 88, 88);

	// A burst dropped on a pistol: 3 rounds, 3 ticks apart; on the Shotgun's
	// double blast its own 2 rounds, 13 ticks apart.
	weapon_rule_trigger_t p = plan(WEAPON_MODULE_BURST, falcon, {});
	CHECK(p.module == WEAPON_MODULE_BURST);
	CHECK(p.count == 3);
	CHECK(p.gap == 3);
	p = plan(WEAPON_MODULE_BURST, shotgun, {});
	CHECK(p.count == 2);
	CHECK(p.gap == TICKS(13));
	p = plan(WEAPON_MODULE_BURST, falcon, { { { "count", "5" }, { "gap", "0" } } });
	CHECK(p.count == 5);
	CHECK(p.gap == 0);
	// Mike's example: an automatic on the Falcon, 600 a minute, no spin.
	p = plan(WEAPON_MODULE_AUTOMATIC, falcon, {});
	CHECK(p.module == WEAPON_MODULE_AUTOMATIC);
	CHECK(p.rate == 600.0f);
	CHECK(p.start_rate == 600.0f);
	CHECK(p.spin_up == 0);
	CHECK(p.burst == 0);
	CHECK(p.aimed_burst == 1);
	p = plan(WEAPON_MODULE_AUTOMATIC, falcon, { { { "rate", "900" } } });
	CHECK(p.rate == 900.0f);
	CHECK(p.start_rate == 900.0f);
	// The Reaper's own spin, unless the node names a rate of its own.
	p = plan(WEAPON_MODULE_AUTOMATIC, reaper, {});
	CHECK(p.rate == 1800.0f);
	CHECK(p.start_rate == 60.0f);
	CHECK(p.spin_up == 88);
	CHECK(p.spin_down == 88);
	CHECK(p.burst == 3);
	CHECK(p.aimed_burst == 0);
	p = plan(WEAPON_MODULE_AUTOMATIC, reaper, { { { "rate", "1200" } } });
	CHECK(p.start_rate == 1200.0f);
	p = plan(WEAPON_MODULE_AUTOMATIC, reaper, { { { "rate", "1200" }, { "start_rate", "100.5" },
		{ "aimed_burst", "true" } } });
	CHECK(p.start_rate == 100.5f);
	CHECK(p.aimed_burst == 1);
	p = plan(WEAPON_MODULE_FIRE_ONCE, reaper, {});
	CHECK(p.module == WEAPON_MODULE_FIRE_ONCE);
	CHECK(p.burst == 0);
	// use_ammo leaves the trigger alone and names the rounds.
	p = plan(WEAPON_MODULE_BURST, falcon, {});
	Texts three;
	three.values["rounds"] = "3";
	CHECK(weaponRulesApplySettings(WEAPON_MODULE_USE_AMMO, Texts::get, &three, &falcon, &p) == 1);
	CHECK(p.module == WEAPON_MODULE_BURST);
	CHECK(p.rounds == 3);

	// A malformed setting fails and keeps its default.
	s32 ok = 1;
	p = plan(WEAPON_MODULE_BURST, shotgun, { { { "count", "1" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.count == 2);
	p = plan(WEAPON_MODULE_AUTOMATIC, reaper, { { { "spin_up", "-3" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.spin_up == 88);
	p = plan(WEAPON_MODULE_AUTOMATIC, falcon, { { { "rate", "0" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.rate == 600.0f);
	Texts zero;
	zero.values["rounds"] = "0";
	CHECK(weaponRulesApplySettings(WEAPON_MODULE_USE_AMMO, Texts::get, &zero, &falcon, &p) == 0);
	CHECK(p.rounds == 1);
	CHECK(weaponRulesApplySettings(WEAPON_MODULE_RELOAD, Texts::get, &zero, &falcon, &p) == 0);

	// Each setting by its kind.
	for (s32 i = 0; weaponRulesSettingAt(i); i++) {
		const weapon_rule_setting_t *setting = weaponRulesSettingAt(i);
		INFO(setting->key);
		CHECK(weaponRulesModuleKind(setting->module) != nullptr);
		CHECK_FALSE(weaponRulesSettingValid(setting, ""));
		CHECK_FALSE(weaponRulesSettingValid(setting, "maybe"));
		CHECK_FALSE(weaponRulesSettingValid(setting, nullptr));
		if (setting->type == WEAPON_RULE_SETTING_WHOLE) {
			CHECK(weaponRulesSettingValid(setting, std::to_string(setting->lo).c_str()));
			CHECK(weaponRulesSettingValid(setting, std::to_string(setting->hi).c_str()));
			CHECK_FALSE(weaponRulesSettingValid(setting, std::to_string(setting->hi + 1).c_str()));
			CHECK_FALSE(weaponRulesSettingValid(setting, "2.5"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "-1"));
		} else if (setting->type == WEAPON_RULE_SETTING_RATE) {
			CHECK(weaponRulesSettingValid(setting, "750"));
			CHECK(weaponRulesSettingValid(setting, "0.5"));
			CHECK(weaponRulesSettingValid(setting, "3600"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "0"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "3601"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "-5"));
			CHECK_FALSE(weaponRulesSettingValid(setting, " 5"));
		} else {
			CHECK(weaponRulesSettingValid(setting, "true"));
			CHECK(weaponRulesSettingValid(setting, "false"));
		}
	}
}

TEST_CASE("the base wiring starts the original's attack, for players and for simulants",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	int instance = 0;
	for (const BaseFunction &base : baseFunctions()) {
		INFO(base.name);
		weapon_rule_trigger_t native;
		weaponRulesNativeTrigger(&base.record, &native);
		const std::string json = baseIdleProgram("primary", 1, triggerNodeFields(native), native.rounds);
		char err[256] = { 0 };
		mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(),
			heldKnown, err, sizeof(err));
		INFO(err);
		REQUIRE(program != nullptr);

		// A player's press with a loaded clip starts the original's attack.
		weapon_rule_walk_t walk{};
		walk.weaponnum = -1;
		walk.hand = weapon_rule_hand_t{ 1, 5, 10, 1, 1 };
		weapon_rule_states_t states{};
		walk.states = &states;
		walk.function = &base.record;
		walk.param_text = weaponGraphRuntimeHeldParamText;
		REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon",
			&instance, &walk) == 1);
		CHECK((walk.modules & WEAPON_MODULE_IDLE_ACTIONS) == native.module);
		CHECK(weaponRulesTriggerEqual(&walk.plan, &native));

		// A simulant's press (a ready hand, no states) starts the same one,
		// so its ticks per shot are the original's.
		weapon_rule_walk_t bot{};
		bot.weaponnum = -1;
		weaponRulesReadyHand(&bot.hand);
		bot.function = &base.record;
		bot.param_text = weaponGraphRuntimeHeldParamText;
		REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon",
			&instance, &bot) == 1);
		CHECK(weaponRulesTriggerEqual(&bot.plan, &native));
		CHECK(weaponRulesTicksPerShot(&bot.plan) == weaponRulesTicksPerShot(&native));

		// An empty clip with reserve clicks; nothing is planned.
		walk.hand = weapon_rule_hand_t{ 1, 0, 10, 1, 1 };
		REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD, 0, "base:test__weapon",
			&instance, &walk) == 1);
		CHECK((walk.modules & WEAPON_MODULE_IDLE_ACTIONS) == WEAPON_MODULE_DRY_FIRE);
		CHECK(walk.plan.module == 0);
		missionGraphProgramFree(program);
	}

	// A program that swaps the trigger starts its own attack: the Falcon's
	// primary as a machine gun (Mike's example).
	const weapon_rule_function_t falcon = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0);
	const std::string machine = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.automatic\", \"rate\": 600", 1);
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(machine.data(), machine.size(),
		heldKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	weapon_rule_walk_t walk{};
	walk.weaponnum = -1;
	weaponRulesReadyHand(&walk.hand);
	walk.function = &falcon;
	walk.param_text = weaponGraphRuntimeHeldParamText;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon",
		&instance, &walk) == 1);
	CHECK(walk.plan.module == WEAPON_MODULE_AUTOMATIC);
	CHECK(walk.plan.rate == 600.0f);
	CHECK(walk.plan.rounds == 1);
	CHECK(weaponRulesTicksPerShot(&walk.plan) == 6);
	missionGraphProgramFree(program);

	// Without use_ammo the attack is free.
	const std::string free = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 0);
	program = missionGraphProgramCompileWeapon(free.data(), free.size(), heldKnown, err, sizeof(err));
	REQUIRE(program != nullptr);
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon",
		&instance, &walk) == 1);
	CHECK(walk.plan.module == WEAPON_MODULE_FIRE_ONCE);
	CHECK(walk.plan.rounds == 0);
	missionGraphProgramFree(program);

	// The extractor writes this wiring from the table's plan, and the record
	// no longer names the burst's rounds.
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("s_heldNativeTrigger(func, weapon_id, mode, &plan);") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramNodes(&w, mode_name, weaponRulesNativeRounds(weapon_id, mode), &plan);")
		!= std::string::npos);
	REQUIRE(extractor.find("weaponRulesNativeTrigger(&record, out);") != std::string::npos);
	REQUIRE(extractor.find("jw_field_f32(w, WEAPON_RULE_RATE_PARAM, plan->rate, 0);") != std::string::npos);
	REQUIRE(extractor.find("jw_field_bool(w, WEAPON_RULE_AIMED_BURST_PARAM, plan->aimed_burst, 1);")
		!= std::string::npos);
	CHECK(extractor.find("jw_field_int(w, \"burst_count\"") == std::string::npos);
	// The seam keeps the plan, and simulants read the same programs.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("bgunRuleFunctionOf(hand, gsetGetWeaponFunction(&hand->gset), &record);")
		!= std::string::npos);
	REQUIRE(bondgun.find("if (act & WEAPON_MODULE_TRIGGERS) {") != std::string::npos);
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("handled = weaponRulesWalk(program, kind, g_Vars.currentplayernum,") != std::string::npos);
	REQUIRE(events.find("weaponRulesReadyHand(&walk.hand);") != std::string::npos);
	REQUIRE(events.find("cache->generation != generation") != std::string::npos);
}

TEST_CASE("a held program's trigger settings are checked when its weapon registers",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	const std::string secondary = R"({
  "schema": "pd.weapon_graph.v1",
  "asset_id": "base:test__weapon",
  "graph_id": "secondary",
  "nodes": [
    { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary", "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1, "flags": 0, "damage": 0.9, "range": 60 } }
  ],
  "edges": [],
  "exports": [ { "name": "secondary", "node": "secondary_action" } ]
})";
	const s32 weaponnum = 2;
	char err[256] = { 0 };
	const auto registers = [&](const std::string &primary, const char *variables = nullptr) {
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(weaponnum, "base:test__weapon", primary.data(),
			(u32)primary.size(), secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0,
			variables, variables ? (u32)std::strlen(variables) : 0, nullptr, 0, err, sizeof(err)) == 0;
	};
	weaponGraphRuntimeClearAll();
	const u32 before = weaponGraphRuntimeHeldProgramsGeneration();
	REQUIRE(registers(baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.burst\", \"count\": 3, \"gap\": 3", 1)));
	CHECK(weaponGraphRuntimeHeldProgramsGeneration() != before);
	const struct {
		const char *fields;
		const char *why;
	} refused[] = {
		{ "\"kind\": \"og.weapon.burst\", \"count\": 1", "og.weapon.burst needs count from 2 to 50, got 1" },
		{ "\"kind\": \"og.weapon.burst\", \"gap\": \"soon\"", "og.weapon.burst needs gap from 0 to 255, got soon" },
		{ "\"kind\": \"og.weapon.automatic\", \"rate\": 0",
			"og.weapon.automatic needs rate in rounds per minute, above 0 and up to 3600, got 0" },
		{ "\"kind\": \"og.weapon.automatic\", \"start_rate\": 4000",
			"og.weapon.automatic needs start_rate in rounds per minute" },
		{ "\"kind\": \"og.weapon.automatic\", \"aimed_burst\": \"maybe\"",
			"og.weapon.automatic needs aimed_burst true or false, got maybe" },
		{ "\"kind\": \"og.weapon.automatic\", \"spin_down\": 2.5", "og.weapon.automatic needs spin_down from 0 to 10000" },
	};
	for (const auto &bad : refused) {
		INFO(bad.fields);
		CHECK_FALSE(registers(baseIdleProgram("primary", 1, bad.fields, 1)));
		CHECK(std::string(err).find(bad.why) != std::string::npos);
		CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 0) == nullptr);
	}
	std::string heavy = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1);
	heavy.replace(heavy.find("\"kind\": \"og.weapon.use_ammo\", \"rounds\": 1"),
		std::string("\"kind\": \"og.weapon.use_ammo\", \"rounds\": 1").size(),
		"\"kind\": \"og.weapon.use_ammo\", \"rounds\": 0");
	CHECK_FALSE(registers(heavy));
	CHECK(std::string(err).find("og.weapon.use_ammo needs rounds from 1 to 255, got 0") != std::string::npos);

	// A "$name" setting reads the weapon's variables.
	const std::string fromVariable = baseIdleProgram("primary", 1,
		"\"kind\": \"og.weapon.automatic\", \"rate\": \"$fire_rate\"", 1);
	CHECK_FALSE(registers(fromVariable));
	CHECK(std::string(err).find("og.weapon.automatic needs rate") != std::string::npos);
	const char *variables = R"({"schema":"pd.weapon_variables.v1","asset_id":"base:test__weapon",
		"variables":[{"name":"fire_rate","value":950}]})";
	REQUIRE(registers(fromVariable, variables));
	const mission_graph_program_t *program = weaponGraphRuntimeHeldProgram(weaponnum, 0);
	REQUIRE(program != nullptr);
	const weapon_rule_function_t falcon = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0);
	weapon_rule_walk_t walk{};
	walk.weaponnum = weaponnum;
	weaponRulesReadyHand(&walk.hand);
	walk.function = &falcon;
	walk.param_text = weaponGraphRuntimeHeldParamText;
	int instance = 0;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon",
		&instance, &walk) == 1);
	CHECK(walk.plan.module == WEAPON_MODULE_AUTOMATIC);
	CHECK(walk.plan.rate == 950.0f);

	// A program node may not take the id of a record node.
	std::string clash = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1);
	for (size_t at = clash.find("primary_attack"); at != std::string::npos; at = clash.find("primary_attack")) {
		clash.replace(at, std::string("primary_attack").size(), "primary_action");
	}
	CHECK_FALSE(registers(clash));
	const std::string why = err;
	CHECK(why.find("primary_action") != std::string::npos);
	CHECK((why.find("is used twice") != std::string::npos || why.find("duplicate graph node id") != std::string::npos));
	// The runner refuses it by itself, whatever reads the record.
	mission_graph_program_t *clashing = missionGraphProgramCompileWeapon(clash.data(), clash.size(), heldKnown,
		err, sizeof(err));
	CHECK(clashing == nullptr);
	CHECK(std::string(err).find("node id 'primary_action' is used twice") != std::string::npos);
	missionGraphProgramFree(clashing);

	weaponGraphRuntimeClearAll();
	CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 0) == nullptr);
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
	// The marker stays in the meta cache kind (later suffixes may follow it:
	// _vehiclegraph, B-1230).
	REQUIRE(meta.find("_objective_status_event_mission_endings") != std::string::npos);

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
	// The kinds as the sources define them: the runner's event and gate
	// constants, and the native module table's og.* and gate.* rows
	// (object-events.md slice 3 adds native gates there).
	auto kinds_in = [](const std::string &text, const char *pattern) {
		std::set<std::string> out;
		const std::regex re(pattern);
		for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
			out.insert((*it)[1].str());
		}
		return out;
	};
	const std::set<std::string> events = kinds_in(runner, R"re(Event = "(event\.[a-z_.]+)";)re");
	std::set<std::string> gates = kinds_in(runner, R"re(Gate = "(gate\.[a-z_.]+)";)re");
	const std::set<std::string> table_gates = kinds_in(modules, R"re(\{ "(gate\.[a-z_.]+)", s_)re");
	gates.insert(table_gates.begin(), table_gates.end());
	const std::set<std::string> table_modules = kinds_in(modules, R"re(\{ "(og\.[a-z_.]+)", s_)re");
	CHECK(set_of("MISSION_GRAPH_EXEC_EVENTS") == events);
	CHECK(set_of("MISSION_GRAPH_EXEC_GATES") == gates);
	CHECK(set_of("MISSION_GRAPH_NATIVE_MODULES") == table_modules);
	// The mission runner's own kinds are among them.
	for (const char *kind : { "event.objective.status_changed", "event.mission.completed",
			"event.mission.continue", "event.mission.phase_changed", "event.volume.entered",
			"event.volume.left", "event.pickup.collected", "event.pickup.touched", "event.door.use",
			"event.door.approach", "event.lift.called", "event.lift.idle", "event.lift.arrived",
			"event.glass.damaged", "event.glass.broken" }) {
		INFO(kind);
		CHECK(events.count(kind) == 1);
	}
	CHECK(events.count("event.pickup.requested") == 0);
	CHECK(events.count("event.door.open_requested") == 0);
	CHECK(gates == std::set<std::string>{ "gate.objective.status", "gate.mission.phase",
		"gate.object.kind", "gate.player.has_item", "gate.pickup.native_rules",
		"gate.door.native_unlocked", "gate.lift.native_vacant", "gate.lift.native_ready",
		"gate.lift.native_door_unlocked", "gate.glass.native_broken" });
	CHECK(table_modules.count("og.hud.objective_status_message") == 1);
	CHECK(table_modules.count("og.mission.end_completed") == 1);
	CHECK(table_modules.count("og.door.open") == 1);
	REQUIRE(runner.find("const char *const kObjectiveStatusEvent = \"event.objective.status_changed\";") != std::string::npos);
	REQUIRE(runner.find("const char *const kObjectiveStatusGate = \"gate.objective.status\";") != std::string::npos);
	// Mission flow slice 2 (mission-flow.md): the phase event and gate, and the
	// two modules that end a mission.
	REQUIRE(runner.find("const char *const kMissionPhaseEvent = \"event.mission.phase_changed\";") != std::string::npos);
	REQUIRE(runner.find("const char *const kMissionPhaseGate = \"gate.mission.phase\";") != std::string::npos);
	REQUIRE(modules.find("{ \"og.mission.end_failed\", s_runEndMissionFailed },") != std::string::npos);
	REQUIRE(modules.find("{ \"og.mission.end_completed\", s_runEndMissionCompleted },") != std::string::npos);
	REQUIRE(conformance.find("errors.extend(validate_mission_graph_execution(label, graph))") != std::string::npos);
}
