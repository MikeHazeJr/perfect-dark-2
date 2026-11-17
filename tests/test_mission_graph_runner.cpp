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

#include <cstdlib>
#include <filesystem>

extern "C" {
#include "mission_graph_runner.h"
#include "modarchive.h"
#include "weapon_graph_runtime.h"
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

	// The PC end screen asks the same graph whether the action is Credits.
	CHECK(count(bridge, "STAGE_SKEDARRUINS") == 0);
	CHECK(count(bridge, "endscreenMissionLeadsToCredits()") == 3);
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
TEST_CASE("a v2 projectile graph registers its record and its program",
	"[mission-graph][projectile-graph][weapon-graph]")
{
	const std::string v2 = R"({
  "schema": "pd.projectile_graph.v2",
  "asset_id": "base:test__projectile_fuse",
  "graph_id": "test_projectile_fuse",
  "nodes": [
    { "id": "spawn_state", "kind": "projectile.spawn_state",
      "params": { "model_catalog_id": "base:model_chrgrenade", "scale": 1 } },
    { "id": "motion", "kind": "projectile.motion", "params": { "motion_kind": "ballistic_throw" } },
    { "id": "fuse", "kind": "event.projectile.timer_expired" },
    { "id": "explode", "kind": "og.projectile.fuse_expired" }
  ],
  "edges": [
    { "from": "spawn_state", "to": "motion" },
    { "from": "fuse", "output": "out", "to": "explode", "input": "exec" }
  ],
  "exports": [ { "name": "main", "node": "motion" } ]
})";
	const char *const id = "base:test__projectile_fuse";
	char err[256] = { 0 };

	weaponGraphRuntimeClearAll();
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, v2.data(),
		(u32)v2.size(), err, sizeof(err)) == 0);
	const mission_graph_program_t *program = weaponGraphRuntimeProjectileProgram(id);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramNodeCount(program) == 2);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED,
		"og.projectile.fuse_expired") == 1);

	// A module the projectile table does not know refuses the archive.
	std::string unknown = v2;
	unknown.replace(unknown.find("og.projectile.fuse_expired"),
		std::string("og.projectile.fuse_expired").size(), "og.projectile.unknown");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, unknown.data(),
		(u32)unknown.size(), err, sizeof(err)) != 0);
	CHECK(std::string(err).find("program refused") != std::string::npos);
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);

	// A v1 graph is records only: no program, and a program node is refused.
	std::string v1 = v2;
	v1.replace(v1.find("pd.projectile_graph.v2"), std::string("pd.projectile_graph.v2").size(),
		"pd.projectile_graph.v1");
	CHECK(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id, v1.data(),
		(u32)v1.size(), err, sizeof(err)) != 0);
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);

	weaponGraphRuntimeClearAll();
	CHECK(weaponGraphRuntimeProjectileProgram(id) == nullptr);
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
