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
#include "target_filter.h"
#include "projectile_rules.h"
void testStubEffectAudio(const char *id, s32 category, s32 sound_id);
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
		MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED, MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE,
		MISSION_GRAPH_EVENT_WEAPON_AIMING });
	for (size_t e = 0; e < kinds.size(); e++) {
		CHECK(weaponRulesEventIndex(kinds[e]) == (s32)e);
	}
	CHECK(weaponRulesEventIndex(MISSION_GRAPH_EVENT_PROJECTILE_IN_FLIGHT) == -1);
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
		== (WEAPON_MODULE_IDLE_ACTIONS | WEAPON_MODULE_USE_AMMO | WEAPON_MODULE_SHOTS | WEAPON_MODULE_PRESENTATION
			| WEAPON_MODULE_SET_STATE));
	CHECK(weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY)
		== (WEAPON_MODULE_IDLE_ACTIONS | WEAPON_MODULE_USE_AMMO | WEAPON_MODULE_SHOTS | WEAPON_MODULE_PRESENTATION
			| WEAPON_MODULE_SET_STATE));
	CHECK(weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED) == WEAPON_MODULE_SET_STATE);
	CHECK(weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE) == WEAPON_MODULE_SET_STATE);
	CHECK(weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_AIMING) == WEAPON_MODULE_LOCK_ON);
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_LOCK_ON)) == "og.weapon.lock_on");

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
	const std::string &attack = "\"kind\": \"og.weapon.fire_once\"", s32 use_rounds = 1,
	const std::string &shot = std::string())
{
	const std::string m = mode;
	// slice 4: the shot node `shot` (its kind and settings) after the
	// spending, or after the trigger without it.
	const std::string shotNode = shot.empty() ? std::string()
		: ",{ \"id\": \"" + m + "_shot\", " + shot + " }";
	const std::string shotEdge = shot.empty() ? std::string()
		: "{ \"from\": \"" + m + (use_rounds > 0 ? "_use_ammo" : "_attack") +
			"\", \"output\": \"out\", \"to\": \"" + m + "_shot\", \"input\": \"exec\" },";
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
		"{ \"id\": \"" + m + "_reload\", \"kind\": \"og.weapon.reload\" }" + useNode + shotNode +
		"], \"edges\": ["
		"{ \"from\": \"" + m + "_trigger\", \"to\": \"" + m + "_action\" },"
		"{ \"from\": \"" + m + "_pressed\", \"output\": \"out\", \"to\": \"" + m + "_has_ammo\", \"input\": \"exec\" },"
		"{ \"from\": \"" + m + "_held\", \"output\": \"out\", \"to\": \"" + m + "_has_ammo\", \"input\": \"exec\" },"
		"{ \"from\": \"" + m + "_has_ammo\", \"output\": \"pass\", \"to\": \"" + m + "_attack\", \"input\": \"exec\" }," + useEdge + shotEdge +
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
		"\tif (plan->shot) {\n"
		"\t\ts_emitHeldProgramEdge(w, plan->rounds > 0 ? use_ammo : attack, \"out\", shot, 0);\n"
		"\t}\n"
		"\ts_emitHeldProgramEdge(w, has_ammo, \"fail\", has_reserve, 0);\n"
		"\ts_emitHeldProgramEdge(w, has_reserve, \"pass\", dry_fire, 0);\n"
		"\ts_emitHeldProgramEdge(w, clip_empty, \"out\", reload, 1);") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramNodes(&w, mode_name, weaponRulesNativeRounds(weapon_id, mode), &plan, &reload);")
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

// The hand grenade's primary: a throw whose activation time (240 ticks) is
// the fuse it cooks for.
weapon_rule_function_t grenadeRecord()
{
	weapon_rule_function_t r = ruleRecord(WEAPON_GRENADE, 0, 0x0002, 0, 0);
	r.activation_time = 240;
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
		{ "grenade primary", grenadeRecord() },
		{ "combatknife primary", ruleRecord(WEAPON_COMBATKNIFE, 0, 0x0003, 0, 0) },
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
	if (plan.module == WEAPON_MODULE_CHARGE) {
		char damage[32];
		std::snprintf(damage, sizeof(damage), "%.9g", (double)plan.charge_damage);
		return std::string("\"kind\": \"og.weapon.charge\", \"start\": \"") +
			(plan.charge_start == WEAPON_RULE_CHARGE_ON_PRESS ? "on_press" : "while_selected") +
			"\", \"time\": " + std::to_string(plan.charge_time) + ", \"steps\": " + std::to_string(plan.charge_steps) +
			", \"ammo_per_step\": " + std::to_string(plan.charge_ammo) + ", \"min_clip\": " +
			std::to_string(plan.charge_min_clip) + ", \"fall_time\": " + std::to_string(plan.charge_fall) +
			", \"release\": \"" + (plan.charge_release == WEAPON_RULE_RELEASE_ON_FULL ? "on_full" : "on_press") +
			"\", \"damage_per_step\": " + damage;
	}
	if (plan.module == WEAPON_MODULE_COOK) {
		return "\"kind\": \"og.weapon.cook\", \"fuse\": " + std::to_string(plan.cook_fuse > 0 ? plan.cook_fuse : 240);
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
	CHECK(WEAPON_MODULE_TRIGGERS == (WEAPON_MODULE_FIRE_ONCE | WEAPON_MODULE_BURST | WEAPON_MODULE_AUTOMATIC
		| WEAPON_MODULE_CHARGE | WEAPON_MODULE_COOK));
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
	CHECK(plans["grenade primary"].module == WEAPON_MODULE_COOK); // slice 5: it cooks
	CHECK(plans["grenade primary"].rounds == 1); // slice 4: a throw spends through use_ammo
	CHECK(plans["combatknife primary"].rounds == 0); // a strike takes none

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
		} else if (setting->type == WEAPON_RULE_SETTING_BOOL) {
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
	REQUIRE(extractor.find("s_heldNativeTrigger(func, weapon_id, mode, incremental, &plan, &reload);") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramNodes(&w, mode_name, weaponRulesNativeRounds(weapon_id, mode), &plan, &reload);")
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

// held-weapon-events.md slice 4: the shot table.
namespace {

weapon_rule_function_t specialRecord(s32 weaponnum, s32 func, s32 specialfunc, s32 slot)
{
	weapon_rule_function_t r = ruleRecord(weaponnum, func, 0x0004, 0, slot);
	r.specialfunc = specialfunc;
	return r;
}

// The shooting, launching, throwing, striking and special functions whose
// shot is not the default (the extracted records, Nov 2026), and a device.
std::vector<BaseFunction> shotFunctions()
{
	return {
		{ "shotgun primary", ruleRecord(WEAPON_SHOTGUN, 0, 0x0001, 0, 0) },
		{ "farsight primary", ruleRecord(WEAPON_FARSIGHT, 0, 0x0001, 0, 0) },
		{ "farsight secondary", ruleRecord(WEAPON_FARSIGHT, 1, 0x0001, 0, 0) },
		{ "laser primary", ruleRecord(WEAPON_LASER, 0, 0x0001, 0, -1) },
		{ "laser secondary", ruleRecord(WEAPON_LASER, 1, 0x0101, 0, -1, 3600, 3600) },
		{ "falcon2 primary", ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0) },
		{ "rocketlauncher primary", ruleRecord(WEAPON_ROCKETLAUNCHER, 0, 0x0201, 0, 0) },
		{ "slayer secondary", ruleRecord(WEAPON_SLAYER, 1, 0x0201, 0, 0) },
		{ "crossbow primary", ruleRecord(WEAPON_CROSSBOW, 0, 0x0201, 0, 0) },
		{ "devastator secondary", ruleRecord(WEAPON_DEVASTATOR, 1, 0x0201, 0, 0) },
		{ "superdragon secondary", ruleRecord(WEAPON_SUPERDRAGON, 1, 0x0201, 0, 1) },
		{ "bolt primary", ruleRecord(WEAPON_BOLT, 0, 0x0201, 0, 0) },
		{ "grenade primary", grenadeRecord() },
		{ "mauler secondary", ruleRecord(WEAPON_MAULER, 1, 0x0001, 0, 0) },
		{ "laptopgun secondary", ruleRecord(WEAPON_LAPTOPGUN, 1, 0x0002, 0, -1) },
		{ "dragon secondary", ruleRecord(WEAPON_DRAGON, 1, 0x0002, 0, -1) },
		{ "tranquilizer secondary", ruleRecord(WEAPON_TRANQUILIZER, 1, 0x0003, 0, 0) },
		{ "combatknife primary", ruleRecord(WEAPON_COMBATKNIFE, 0, 0x0003, 0, 0) },
		{ "unarmed primary", ruleRecord(WEAPON_UNARMED, 0, 0x0003, 0, -1) },
		{ "rcp120 secondary", specialRecord(WEAPON_RCP120, 1, 9, 0) },
		{ "combatboost primary", specialRecord(WEAPON_COMBATBOOST, 0, 6, 0) },
		{ "combatboost secondary", specialRecord(WEAPON_COMBATBOOST, 1, 7, 0) },
		{ "remotemine secondary", specialRecord(WEAPON_REMOTEMINE, 1, 5, -1) },
		{ "datauplink primary", specialRecord(WEAPON_DATAUPLINK, 0, 12, -1) },
		{ "sniperrifle secondary", specialRecord(WEAPON_SNIPERRIFLE, 1, 8, -1) },
		{ "nightvision primary", ruleRecord(WEAPON_NIGHTVISION, 0, 0x0005, 0, 0) },
	};
}

// The items a launch may name, as the game's weapon table names them.
s32 testItemNumber(const char *catalog_id)
{
	const std::string id = catalog_id ? catalog_id : "";
	if (id == "base:rocket") return WEAPON_ROCKET;
	if (id == "base:homingrocket") return WEAPON_HOMINGROCKET;
	if (id == "base:bolt") return WEAPON_BOLT;
	if (id == "base:grenaderound") return WEAPON_GRENADEROUND;
	return -1;
}

const char *testItemId(s32 item)
{
	switch (item) {
	case WEAPON_ROCKET: return "base:rocket";
	case WEAPON_HOMINGROCKET: return "base:homingrocket";
	case WEAPON_BOLT: return "base:bolt";
	case WEAPON_GRENADEROUND: return "base:grenaderound";
	}
	return "own";
}

// The shot node as the extractor writes it (s_emitHeldShotNode).
std::string shotNodeFields(const weapon_rule_trigger_t &plan)
{
	char number[32];
	switch (plan.shot) {
	case WEAPON_MODULE_HITSCAN:
		std::snprintf(number, sizeof(number), "%.7g", (double)plan.shield_damage);
		return std::string("\"kind\": \"og.weapon.hitscan\", \"pellets\": ") + std::to_string(plan.pellets) +
			", \"through_walls\": " + (plan.through_walls ? "true" : "false") + ", \"shield_damage\": " + number;
	case WEAPON_MODULE_BEAM:
		return "\"kind\": \"og.weapon.beam\", \"range\": " + std::to_string((s32)plan.range);
	case WEAPON_MODULE_LAUNCH: {
		std::string fields = std::string("\"kind\": \"og.weapon.launch\", \"item\": \"") + testItemId(plan.item) + "\"";
		if (plan.item_function == WEAPON_RULE_ITEM_FUNCTION_GUN) fields += ", \"item_function\": \"gun\"";
		else if (plan.item_function != WEAPON_RULE_ITEM_FUNCTION_KEEP) {
			fields += ", \"item_function\": " + std::to_string(plan.item_function);
		}
		return fields;
	}
	case WEAPON_MODULE_THROW:
		return std::string("\"kind\": \"og.weapon.throw\", \"give_up_gun\": ") + (plan.give_up_gun ? "true" : "false");
	case WEAPON_MODULE_MELEE:
		return "\"kind\": \"og.weapon.melee\"";
	case WEAPON_MODULE_SPECIAL:
		return std::string("\"kind\": \"og.weapon.special\", \"action\": \"") + weaponRulesActionName(plan.action) + "\"";
	case WEAPON_MODULE_MODE_TOGGLE:
		std::snprintf(number, sizeof(number), "%.7g", (double)plan.drain);
		return std::string("\"kind\": \"og.weapon.mode_toggle\", \"mode\": \"cloak\", \"drain\": ") + number;
	}
	return std::string();
}

} // namespace

TEST_CASE("the shot table plans each base function's shot as the original does",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	for (u32 shot : { WEAPON_MODULE_HITSCAN, WEAPON_MODULE_BEAM, WEAPON_MODULE_LAUNCH, WEAPON_MODULE_THROW,
			WEAPON_MODULE_MELEE, WEAPON_MODULE_SPECIAL, WEAPON_MODULE_MODE_TOGGLE }) {
		INFO(shot);
		REQUIRE(weaponRulesModuleKind(shot) != nullptr);
		CHECK(weaponRulesModuleBit(weaponRulesModuleKind(shot)) == shot);
		CHECK((WEAPON_MODULE_SHOTS & shot) == shot);
		CHECK((weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED) & shot) == shot);
		CHECK((weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED) & shot) == 0);
	}
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_HITSCAN)) == "og.weapon.hitscan");
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_MODE_TOGGLE)) == "og.weapon.mode_toggle");
	CHECK(WEAPON_RULE_ATTACK_CLOAK == HANDATTACKTYPE_RCP120CLOAK);
	CHECK(weaponRulesActionFromText("detonate") == HANDATTACKTYPE_DETONATE);
	CHECK(weaponRulesActionFromText("boost") == HANDATTACKTYPE_BOOST);
	CHECK(weaponRulesActionFromText("revert_boost") == HANDATTACKTYPE_REVERTBOOST);
	CHECK(weaponRulesActionFromText("crouch") == HANDATTACKTYPE_CROUCH);
	CHECK(weaponRulesActionFromText("uplink") == HANDATTACKTYPE_UPLINK);
	CHECK(weaponRulesActionFromText("cloak") == -1); // a mode toggle
	CHECK(weaponRulesActionName(HANDATTACKTYPE_RCP120CLOAK) == nullptr);

	std::map<std::string, weapon_rule_trigger_t> plans;
	for (const BaseFunction &base : shotFunctions()) {
		weapon_rule_trigger_t plan;
		weaponRulesNativeTrigger(&base.record, &plan);
		plans[base.name] = plan;
		INFO(base.name);
		// Each shot fits its function, and a device has none.
		if (plan.shot) {
			CHECK(weaponRulesShotFits(plan.shot, base.record.functype));
		}
	}
	CHECK(plans["shotgun primary"].shot == WEAPON_MODULE_HITSCAN);
	CHECK(plans["shotgun primary"].pellets == 6);
	CHECK(plans["falcon2 primary"].pellets == 1);
	CHECK(plans["farsight primary"].through_walls == 1);
	CHECK(plans["farsight primary"].shield_damage == 10.0f);
	CHECK(plans["farsight secondary"].through_walls == 1);
	CHECK(plans["falcon2 primary"].through_walls == 0);
	CHECK(plans["falcon2 primary"].shield_damage == 1.0f);
	CHECK(plans["laser primary"].shot == WEAPON_MODULE_HITSCAN);
	CHECK(plans["laser secondary"].shot == WEAPON_MODULE_BEAM);
	CHECK(plans["laser secondary"].range == 300.0f);
	CHECK(plans["rocketlauncher primary"].shot == WEAPON_MODULE_LAUNCH);
	CHECK(plans["rocketlauncher primary"].item == WEAPON_ROCKET);
	CHECK(plans["rocketlauncher primary"].item_function == WEAPON_RULE_ITEM_FUNCTION_KEEP);
	CHECK(plans["slayer secondary"].item == WEAPON_ROCKET);
	CHECK(plans["crossbow primary"].item == WEAPON_BOLT);
	CHECK(plans["crossbow primary"].item_function == WEAPON_RULE_ITEM_FUNCTION_GUN);
	CHECK(plans["devastator secondary"].item == WEAPON_GRENADEROUND);
	CHECK(plans["devastator secondary"].item_function == WEAPON_RULE_ITEM_FUNCTION_GUN);
	CHECK(plans["superdragon secondary"].item == WEAPON_GRENADEROUND);
	CHECK(plans["superdragon secondary"].item_function == FUNC_2);
	CHECK(plans["bolt primary"].item == WEAPON_RULE_ITEM_OWN);
	CHECK(plans["grenade primary"].shot == WEAPON_MODULE_THROW);
	CHECK(plans["grenade primary"].give_up_gun == 0);
	CHECK(plans["grenade primary"].rounds == 1);
	CHECK(plans["laptopgun secondary"].give_up_gun == 1);
	CHECK(plans["laptopgun secondary"].rounds == 0); // no clip
	CHECK(plans["dragon secondary"].give_up_gun == 0); // its record's discard flag gives it up
	CHECK(plans["tranquilizer secondary"].shot == WEAPON_MODULE_MELEE);
	CHECK(plans["tranquilizer secondary"].rounds == 4);
	CHECK(plans["combatknife primary"].rounds == 0);
	CHECK(plans["unarmed primary"].rounds == 0);
	CHECK(plans["rcp120 secondary"].shot == WEAPON_MODULE_MODE_TOGGLE);
	CHECK(plans["rcp120 secondary"].mode == WEAPON_RULE_MODE_CLOAK);
	CHECK(plans["rcp120 secondary"].drain == 24.0f);
	CHECK(plans["rcp120 secondary"].rounds == 1);
	CHECK(plans["combatboost primary"].shot == WEAPON_MODULE_SPECIAL);
	CHECK(plans["combatboost primary"].action == HANDATTACKTYPE_BOOST);
	CHECK(plans["combatboost secondary"].action == HANDATTACKTYPE_REVERTBOOST);
	CHECK(plans["combatboost primary"].rounds == 1);
	CHECK(plans["remotemine secondary"].action == HANDATTACKTYPE_DETONATE);
	CHECK(plans["remotemine secondary"].rounds == 0);
	CHECK(plans["datauplink primary"].action == HANDATTACKTYPE_UPLINK);
	CHECK(plans["sniperrifle secondary"].action == HANDATTACKTYPE_CROUCH);
	CHECK(plans["nightvision primary"].shot == 0);
	CHECK(plans["nightvision primary"].rounds == 0);
	// The cloak's drain is the original's 0.4 rounds a 60 Hz tick.
	CHECK(plans["rcp120 secondary"].drain / 60.0f == 0.4f);

	// A family's shot fits only its family's functions.
	CHECK(weaponRulesShotFits(WEAPON_MODULE_HITSCAN, 0x0101));
	CHECK(weaponRulesShotFits(WEAPON_MODULE_BEAM, 0x0001));
	CHECK_FALSE(weaponRulesShotFits(WEAPON_MODULE_HITSCAN, 0x0201));
	CHECK(weaponRulesShotFits(WEAPON_MODULE_LAUNCH, 0x0201));
	CHECK_FALSE(weaponRulesShotFits(WEAPON_MODULE_LAUNCH, 0x0001));
	CHECK(weaponRulesShotFits(WEAPON_MODULE_THROW, 0x0002));
	CHECK_FALSE(weaponRulesShotFits(WEAPON_MODULE_MELEE, 0x0002));
	CHECK(weaponRulesShotFits(WEAPON_MODULE_MODE_TOGGLE, 0x0004));
	CHECK_FALSE(weaponRulesShotFits(WEAPON_MODULE_SPECIAL, 0x0005));

	// A throw, a special or a strike takes its rounds; a shorter clip is
	// emptied (the sedative strike's original).
	s32 clip = 9;
	weaponRulesSpendRounds(4, &clip);
	CHECK(clip == 5);
	weaponRulesSpendRounds(4, &clip);
	CHECK(clip == 1);
	weaponRulesSpendRounds(4, &clip);
	CHECK(clip == 0);
	clip = 1;
	weaponRulesSpendRounds(1, &clip);
	CHECK(clip == 0);
	clip = 3;
	weaponRulesSpendRounds(0, &clip);
	CHECK(clip == 3);

	// The seams read the plan where they read a weapon number.
	const std::string prop = readSource("src/game/prop.c");
	REQUIRE(prop.find("if (shotplan->shot == WEAPON_MODULE_HITSCAN && shotplan->pellets > 1) {") != std::string::npos);
	REQUIRE(prop.find("if (shotplan->shot == WEAPON_MODULE_BEAM) {") != std::string::npos);
	REQUIRE(prop.find("if (!shotplan->through_walls || g_Vars.currentplayer->visionmode != VISIONMODE_XRAY) {")
		!= std::string::npos);
	REQUIRE(prop.find("if (hitbg && !shotplan->through_walls) {") != std::string::npos);
	CHECK(prop.find("if (weaponnum == WEAPON_SHOTGUN) {\n\t\t\t\t\tshotCreate(") == std::string::npos);
	CHECK(prop.find("shotdata.gset.weaponnum == WEAPON_LASER && shotdata.gset.weaponfunc == FUNC_SECONDARY")
		== std::string::npos);
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("} else if (shotplan->item >= 0) {") != std::string::npos);
	REQUIRE(bondgun.find("if (bgunHandPlan(handnum)->give_up_gun) {") != std::string::npos);
	REQUIRE(bondgun.find("hand->matmot1 += LVUPDATE60FREAL() * (cloakdrain / 60.0f);") != std::string::npos);
	CHECK(bondgun.find("} else if (hand->gset.weaponnum == WEAPON_CROSSBOW) {") == std::string::npos);
	CHECK(bondgun.find("if (gset->weaponnum == WEAPON_LAPTOPGUN) {\n\t\tbgunFreeWeapon(handnum);") == std::string::npos);
	CHECK(bondgun.find("hand->loadedammo[ammoindex]--;") == std::string::npos);
	CHECK(bondgun.find("bgunGetMinClipQty(WEAPON_TRANQUILIZER, FUNC_SECONDARY)) {") == std::string::npos);
	size_t spends = 0;
	for (size_t at = bondgun.find("weaponRulesSpendRounds(plan->rounds, &hand->loadedammo[ammoindex]);");
			at != std::string::npos;
			at = bondgun.find("weaponRulesSpendRounds(plan->rounds, &hand->loadedammo[ammoindex]);", at + 1)) {
		spends++;
	}
	CHECK(spends == 4); // the throw, the cooked grenade, the strike, the special
	const std::string chraction = readSource("src/game/chraction.c");
	REQUIRE(chraction.find("damage *= shotplan.shield_damage;") != std::string::npos);
	REQUIRE(chraction.find("if (chr->aibot && botshotplan.through_walls && !chr->aibot->targetinsight) {")
		!= std::string::npos);
	CHECK(chraction.find("if (gset && gset->weaponnum == WEAPON_FARSIGHT) {") == std::string::npos);
}

TEST_CASE("a shot node's settings default to the function's own shot and refuse what they cannot read",
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
	const auto shot = [](u32 module, const weapon_rule_function_t &record, Texts texts, s32 *ok = nullptr) {
		weapon_rule_trigger_t out{};
		const s32 result = weaponRulesApplyShotSettings(module, Texts::get, &texts, testItemNumber, &record, &out);
		if (ok) *ok = result;
		return out;
	};
	const weapon_rule_function_t shotgun = ruleRecord(WEAPON_SHOTGUN, 0, 0x0001, 0, 0);
	const weapon_rule_function_t falcon = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0);
	const weapon_rule_function_t crossbow = ruleRecord(WEAPON_CROSSBOW, 0, 0x0201, 0, 0);
	const weapon_rule_function_t laser = ruleRecord(WEAPON_LASER, 1, 0x0101, 0, -1, 3600, 3600);
	const weapon_rule_function_t rcp = specialRecord(WEAPON_RCP120, 1, 9, 0);
	const weapon_rule_function_t boost = specialRecord(WEAPON_COMBATBOOST, 0, 6, 0);

	weapon_rule_trigger_t p = shot(WEAPON_MODULE_HITSCAN, shotgun, {});
	CHECK(p.shot == WEAPON_MODULE_HITSCAN);
	CHECK(p.pellets == 6); // the Shotgun's own
	p = shot(WEAPON_MODULE_HITSCAN, falcon, { { { "pellets", "8" }, { "through_walls", "true" },
		{ "shield_damage", "2.5" } } });
	CHECK(p.pellets == 8);
	CHECK(p.through_walls == 1);
	CHECK(p.shield_damage == 2.5f);
	p = shot(WEAPON_MODULE_BEAM, falcon, {});
	CHECK(p.range == 300.0f); // a beam's default reach
	p = shot(WEAPON_MODULE_BEAM, laser, { { { "range", "450" } } });
	CHECK(p.range == 450.0f);
	p = shot(WEAPON_MODULE_LAUNCH, crossbow, {});
	CHECK(p.item == WEAPON_BOLT);
	CHECK(p.item_function == WEAPON_RULE_ITEM_FUNCTION_GUN);
	p = shot(WEAPON_MODULE_LAUNCH, crossbow, { { { "item", "base:rocket" }, { "item_function", "1" } } });
	CHECK(p.item == WEAPON_ROCKET);
	CHECK(p.item_function == 1);
	p = shot(WEAPON_MODULE_LAUNCH, crossbow, { { { "item", "own" } } });
	CHECK(p.item == WEAPON_RULE_ITEM_OWN);
	p = shot(WEAPON_MODULE_LAUNCH, crossbow, { { { "item", "base:no_such_item" } } });
	CHECK(p.item == WEAPON_RULE_ITEM_UNKNOWN);
	p = shot(WEAPON_MODULE_THROW, ruleRecord(WEAPON_LAPTOPGUN, 1, 0x0002, 0, -1), {});
	CHECK(p.give_up_gun == 1);
	p = shot(WEAPON_MODULE_SPECIAL, boost, {});
	CHECK(p.action == HANDATTACKTYPE_BOOST);
	p = shot(WEAPON_MODULE_SPECIAL, boost, { { { "action", "crouch" } } });
	CHECK(p.action == HANDATTACKTYPE_CROUCH);
	p = shot(WEAPON_MODULE_MODE_TOGGLE, rcp, {});
	CHECK(p.mode == WEAPON_RULE_MODE_CLOAK);
	CHECK(p.drain == 24.0f);
	p = shot(WEAPON_MODULE_MODE_TOGGLE, rcp, { { { "drain", "12" } } });
	CHECK(p.drain == 12.0f);
	// Malformed settings fail and keep their defaults.
	s32 ok = 1;
	p = shot(WEAPON_MODULE_HITSCAN, shotgun, { { { "pellets", "0" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.pellets == 6);
	p = shot(WEAPON_MODULE_HITSCAN, falcon, { { { "through_walls", "sometimes" } } }, &ok);
	CHECK(ok == 0);
	p = shot(WEAPON_MODULE_LAUNCH, crossbow, { { { "item", "Rocket!" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.item == WEAPON_BOLT);
	p = shot(WEAPON_MODULE_LAUNCH, crossbow, { { { "item_function", "3" } } }, &ok);
	CHECK(ok == 0);
	p = shot(WEAPON_MODULE_SPECIAL, boost, { { { "action", "explode" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.action == HANDATTACKTYPE_BOOST);
	p = shot(WEAPON_MODULE_MODE_TOGGLE, rcp, { { { "mode", "invisible" } } }, &ok);
	CHECK(ok == 0);
	weapon_rule_trigger_t none{};
	Texts empty;
	CHECK(weaponRulesApplyShotSettings(WEAPON_MODULE_FIRE_ONCE, Texts::get, &empty, testItemNumber, &falcon, &none) == 0);

	for (s32 i = 0; weaponRulesSettingAt(i); i++) {
		const weapon_rule_setting_t *setting = weaponRulesSettingAt(i);
		INFO(setting->key);
		switch (setting->type) {
		case WEAPON_RULE_SETTING_NUMBER:
			CHECK(weaponRulesSettingValid(setting, "0.5"));
			CHECK(weaponRulesSettingValid(setting, std::to_string(setting->hi).c_str()));
			CHECK_FALSE(weaponRulesSettingValid(setting, "0"));
			CHECK_FALSE(weaponRulesSettingValid(setting, std::to_string(setting->hi + 1).c_str()));
			break;
		case WEAPON_RULE_SETTING_ITEM:
			CHECK(weaponRulesSettingValid(setting, "own"));
			CHECK(weaponRulesSettingValid(setting, "base:grenaderound"));
			CHECK(weaponRulesSettingValid(setting, "mod_x:my_item"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "rocket"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "base:Rocket"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "a:b:c"));
			break;
		case WEAPON_RULE_SETTING_ITEM_FUNCTION:
			CHECK(weaponRulesSettingValid(setting, "gun"));
			CHECK(weaponRulesSettingValid(setting, "2"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "3"));
			break;
		case WEAPON_RULE_SETTING_ACTION:
			CHECK(weaponRulesSettingValid(setting, "detonate"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "cloak"));
			break;
		case WEAPON_RULE_SETTING_MODE:
			CHECK(weaponRulesSettingValid(setting, "cloak"));
			CHECK_FALSE(weaponRulesSettingValid(setting, "boost"));
			break;
		}
	}
}

TEST_CASE("the base wiring's shots are the original's, for players and for simulants",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	int instance = 0;
	for (const BaseFunction &base : shotFunctions()) {
		INFO(base.name);
		weapon_rule_trigger_t native;
		weaponRulesNativeTrigger(&base.record, &native);
		const std::string json = baseIdleProgram("primary", 1, triggerNodeFields(native), native.rounds,
			shotNodeFields(native));
		char err[256] = { 0 };
		mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(),
			heldKnown, err, sizeof(err));
		INFO(err);
		REQUIRE(program != nullptr);
		for (s32 simulant = 0; simulant <= 1; simulant++) {
			weapon_rule_walk_t walk{};
			weapon_rule_states_t states{};
			walk.weaponnum = -1;
			if (simulant) {
				weaponRulesReadyHand(&walk.hand);
			} else {
				walk.hand = weapon_rule_hand_t{ 1, 5, 10, 1, 1 };
				walk.states = &states;
			}
			walk.function = &base.record;
			walk.param_text = weaponGraphRuntimeHeldParamText;
			walk.item_number = testItemNumber;
			REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon",
				&instance, &walk) == 1);
			CHECK((walk.modules & WEAPON_MODULE_SHOTS) == native.shot);
			CHECK(weaponRulesTriggerEqual(&walk.plan, &native));
		}
		missionGraphProgramFree(program);
	}

	// A shot left out is the function's own; a different one is the
	// program's.
	const weapon_rule_function_t shotgun = ruleRecord(WEAPON_SHOTGUN, 0, 0x0001, 0, 0);
	weapon_rule_trigger_t native;
	weaponRulesNativeTrigger(&shotgun, &native);
	const std::string plain = baseIdleProgram("primary", 1);
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(plain.data(), plain.size(), heldKnown,
		err, sizeof(err));
	REQUIRE(program != nullptr);
	weapon_rule_walk_t walk{};
	walk.weaponnum = -1;
	weaponRulesReadyHand(&walk.hand);
	walk.function = &shotgun;
	walk.param_text = weaponGraphRuntimeHeldParamText;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon",
		&instance, &walk) == 1);
	CHECK(walk.plan.shot == WEAPON_MODULE_HITSCAN);
	CHECK(walk.plan.pellets == 6);
	missionGraphProgramFree(program);
	const std::string slug = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1,
		"\"kind\": \"og.weapon.hitscan\", \"pellets\": 1");
	program = missionGraphProgramCompileWeapon(slug.data(), slug.size(), heldKnown, err, sizeof(err));
	REQUIRE(program != nullptr);
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon",
		&instance, &walk) == 1);
	CHECK(walk.plan.pellets == 1); // a Shotgun that fires a slug
	missionGraphProgramFree(program);

	// The extractor writes the shot after the attack's spending.
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("\"use_ammo\", \"shot\",") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramEdge(w, plan->rounds > 0 ? use_ammo : attack, \"out\", shot, 0);")
		!= std::string::npos);
	REQUIRE(extractor.find("record.specialfunc = ((const struct weaponfunc_special *)f)->specialfunc;")
		!= std::string::npos);
	REQUIRE(extractor.find("jw_field_int(w, WEAPON_RULE_PELLETS_PARAM, plan->pellets, 0);") != std::string::npos);
	REQUIRE(extractor.find("jw_field_str(w, WEAPON_RULE_ITEM_PARAM, item, !function);") != std::string::npos);
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("walk.item_number = weaponItemNumber;") != std::string::npos);
}

TEST_CASE("a held program's shot is checked against its function when its weapon registers",
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
	const auto registers = [&](const std::string &primary) {
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(weaponnum, "base:test__weapon", primary.data(),
			(u32)primary.size(), secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0,
			nullptr, 0, nullptr, 0, err, sizeof(err)) == 0;
	};
	weaponGraphRuntimeClearAll();
	// The test record is a single shot: a bullet or a beam fits it.
	REQUIRE(registers(baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1,
		"\"kind\": \"og.weapon.hitscan\", \"pellets\": 6, \"through_walls\": false, \"shield_damage\": 1")));
	REQUIRE(registers(baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.automatic\"", 1,
		"\"kind\": \"og.weapon.beam\", \"range\": 300")));
	const struct {
		const char *shot;
		const char *why;
	} refused[] = {
		{ "\"kind\": \"og.weapon.launch\", \"item\": \"base:rocket\"", "og.weapon.launch does not fit this function" },
		{ "\"kind\": \"og.weapon.throw\"", "og.weapon.throw does not fit this function" },
		{ "\"kind\": \"og.weapon.mode_toggle\"", "og.weapon.mode_toggle does not fit this function" },
		{ "\"kind\": \"og.weapon.hitscan\", \"pellets\": 33", "og.weapon.hitscan needs pellets from 1 to 32, got 33" },
		{ "\"kind\": \"og.weapon.hitscan\", \"shield_damage\": 0",
			"og.weapon.hitscan needs shield_damage above 0 and up to 100, got 0" },
		{ "\"kind\": \"og.weapon.hitscan\", \"through_walls\": \"maybe\"",
			"og.weapon.hitscan needs through_walls true or false, got maybe" },
		{ "\"kind\": \"og.weapon.beam\", \"range\": 0", "og.weapon.beam needs range from 1 to 65536, got 0" },
	};
	for (const auto &bad : refused) {
		INFO(bad.shot);
		CHECK_FALSE(registers(baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1, bad.shot)));
		CHECK(std::string(err).find(bad.why) != std::string::npos);
		CHECK(weaponGraphRuntimeHeldProgram(weaponnum, 0) == nullptr);
	}
	weaponGraphRuntimeClearAll();
}

TEST_CASE("a reload's style is the function's own unless its program names one",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	weapon_rule_function_t shotgun = ruleRecord(WEAPON_SHOTGUN, 0, 0x0001, 0, 0);
	shotgun.incremental = 1;
	weapon_rule_function_t crystal = ruleRecord(WEAPON_CUSTOM_START, 0, 0x0201, 0, 0);
	crystal.regrow = 1;
	crystal.regrow_ticks60 = 45;
	const weapon_rule_function_t falcon = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0);
	weapon_rule_reload_t r;
	weaponRulesNativeReload(&shotgun, &r);
	CHECK(r.style == WEAPON_RULE_RELOAD_ONE_AT_A_TIME);
	weaponRulesNativeReload(&crystal, &r);
	CHECK(r.style == WEAPON_RULE_RELOAD_REGROW);
	CHECK(r.regrow_time == 45);
	weaponRulesNativeReload(&falcon, &r);
	CHECK(r.style == WEAPON_RULE_RELOAD_MAGAZINE);
	CHECK(std::string(weaponRulesStyleName(WEAPON_RULE_RELOAD_ONE_AT_A_TIME)) == "one_at_a_time");
	CHECK(weaponRulesStyleFromText("regrow") == WEAPON_RULE_RELOAD_REGROW);
	CHECK(weaponRulesStyleFromText("instant") == -1);

	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	Texts none;
	CHECK(weaponRulesApplyReloadSettings(Texts::get, &none, &shotgun, &r) == 1);
	CHECK(r.style == WEAPON_RULE_RELOAD_ONE_AT_A_TIME);
	Texts regrow;
	regrow.values["style"] = "regrow";
	regrow.values["regrow_time"] = "30";
	CHECK(weaponRulesApplyReloadSettings(Texts::get, &regrow, &falcon, &r) == 1);
	CHECK(r.style == WEAPON_RULE_RELOAD_REGROW);
	CHECK(r.regrow_time == 30);
	Texts bad;
	bad.values["style"] = "instant";
	CHECK(weaponRulesApplyReloadSettings(Texts::get, &bad, &shotgun, &r) == 0);
	CHECK(r.style == WEAPON_RULE_RELOAD_ONE_AT_A_TIME);

	// The clip_empty walk plans the reload with its style.
	std::string json = baseIdleProgram("primary", 1);
	const std::string plain = "{ \"id\": \"primary_reload\", \"kind\": \"og.weapon.reload\" }";
	REQUIRE(json.find(plain) != std::string::npos);
	json.replace(json.find(plain), plain.size(),
		"{ \"id\": \"primary_reload\", \"kind\": \"og.weapon.reload\", \"style\": \"regrow\", \"regrow_time\": 20 }");
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(), heldKnown,
		err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	int instance = 0;
	weapon_rule_walk_t walk{};
	weapon_rule_states_t states{};
	walk.weaponnum = -1;
	walk.hand = weapon_rule_hand_t{ 1, 0, 10, 1, 1 };
	walk.states = &states;
	walk.function = &falcon;
	walk.param_text = weaponGraphRuntimeHeldParamText;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY, 0, "base:test__weapon", &instance,
		&walk) == 1);
	CHECK((walk.modules & WEAPON_MODULE_RELOAD) == WEAPON_MODULE_RELOAD);
	CHECK(walk.reload.style == WEAPON_RULE_RELOAD_REGROW);
	CHECK(walk.reload.regrow_time == 20);
	// A press plans no reload.
	walk.hand = weapon_rule_hand_t{ 1, 5, 10, 1, 1 };
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon", &instance,
		&walk) == 1);
	CHECK(walk.reload.style == 0);
	missionGraphProgramFree(program);

	// Registration refuses a style it does not know.
	weaponGraphRuntimeClearAll();
	std::string badjson = baseIdleProgram("primary", 1);
	badjson.replace(badjson.find(plain), plain.size(),
		"{ \"id\": \"primary_reload\", \"kind\": \"og.weapon.reload\", \"style\": \"instant\" }");
	const std::string secondary = R"({ "schema": "pd.weapon_graph.v1", "asset_id": "base:test__weapon",
  "graph_id": "secondary", "nodes": [ { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary",
  "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1, "flags": 0, "damage": 0.9, "range": 60 } } ],
  "edges": [], "exports": [ { "name": "secondary", "node": "secondary_action" } ] })";
	CHECK(weaponGraphRuntimeRegisterWeaponSourceJson(2, "base:test__weapon", badjson.data(), (u32)badjson.size(),
		secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, err, sizeof(err)) != 0);
	CHECK(std::string(err).find("og.weapon.reload needs style") != std::string::npos);
	weaponGraphRuntimeClearAll();

	// bondgun.c and the extractor read and write the style.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("reloadplan = weaponHandReload(hand, &rulerecord);") != std::string::npos);
	REQUIRE(bondgun.find("if (reloadplan->style == WEAPON_RULE_RELOAD_ONE_AT_A_TIME) {") != std::string::npos);
	REQUIRE(bondgun.find("grow.regrow_ticks60 = reloadplan->regrow_time;") != std::string::npos);
	CHECK(bondgun.find("if (info->definition->ammos[func->ammoindex]->flags & AMMOFLAG_INCREMENTALRELOAD) {")
		== std::string::npos);
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("s_emitHeldReloadNode(w, ids[i], reload, last);") != std::string::npos);
	REQUIRE(extractor.find("s_functionReloadsIncrementally(wpn, (const struct weaponfunc *)wpn->functions[0])")
		!= std::string::npos);
}

TEST_CASE("the charge and cook triggers are the Mauler's, the crystal pistol's and the grenade's",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_CHARGE)) == "og.weapon.charge");
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_COOK)) == "og.weapon.cook");
	CHECK((WEAPON_MODULE_TRIGGERS & WEAPON_MODULE_CHARGE) == WEAPON_MODULE_CHARGE);
	CHECK((WEAPON_MODULE_TRIGGERS & WEAPON_MODULE_COOK) == WEAPON_MODULE_COOK);

	const weapon_rule_function_t mauler = ruleRecord(WEAPON_MAULER, 1, 0x0001, 0, 0);
	weapon_rule_trigger_t p;
	weaponRulesNativeTrigger(&mauler, &p);
	CHECK(p.module == WEAPON_MODULE_CHARGE);
	CHECK(p.charge_start == WEAPON_RULE_CHARGE_WHILE_SELECTED);
	CHECK(p.charge_time == 100);
	CHECK(p.charge_steps == 5);
	CHECK(p.charge_ammo == 1);
	CHECK(p.charge_min_clip == 2);
	CHECK(p.charge_fall == 200);
	CHECK(p.charge_release == WEAPON_RULE_RELEASE_ON_PRESS);
	CHECK(p.charge_damage == WEAPON_RULE_MAULER_CHARGE_DAMAGE);
	CHECK(p.rounds == 1);
	CHECK(p.shot == WEAPON_MODULE_HITSCAN);
	// The original's per-tick steps are the plan's.
	CHECK((f32)p.charge_steps / (f32)p.charge_time == 0.05f);
	CHECK(1.0f / (f32)p.charge_fall == 0.005f);
	const weapon_rule_function_t maulerprimary = ruleRecord(WEAPON_MAULER, 0, 0x0001, 0, 0);
	weaponRulesNativeTrigger(&maulerprimary, &p);
	CHECK(p.module == WEAPON_MODULE_FIRE_ONCE);

	weapon_rule_function_t crystal = ruleRecord(WEAPON_CUSTOM_START, 1, 0x0201, 0, 0);
	crystal.has_charge = 1;
	crystal.charge_ticks60 = 75;
	weaponRulesNativeTrigger(&crystal, &p);
	CHECK(p.module == WEAPON_MODULE_CHARGE);
	CHECK(p.charge_start == WEAPON_RULE_CHARGE_ON_PRESS);
	CHECK(p.charge_release == WEAPON_RULE_RELEASE_ON_FULL);
	CHECK(p.charge_time == 75);
	weapon_rule_function_t basecharge = crystal;
	basecharge.weaponnum = WEAPON_FALCON2; // only a mod function commits a charge
	weaponRulesNativeTrigger(&basecharge, &p);
	CHECK(p.module != WEAPON_MODULE_CHARGE);

	weapon_rule_function_t grenade = ruleRecord(WEAPON_GRENADE, 0, 0x0002, 0, 0);
	grenade.activation_time = 240;
	weaponRulesNativeTrigger(&grenade, &p);
	CHECK(p.module == WEAPON_MODULE_COOK);
	CHECK(p.cook_fuse == 240);
	CHECK(p.rounds == 1);
	weapon_rule_function_t pinball = ruleRecord(WEAPON_GRENADE, 1, 0x0002, 0, 0);
	weaponRulesNativeTrigger(&pinball, &p);
	CHECK(p.module == WEAPON_MODULE_FIRE_ONCE);

	// Settings: the record's own, else the Mauler's; malformed ones keep it.
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
	const weapon_rule_function_t modgun = ruleRecord(WEAPON_CUSTOM_START, 0, 0x0001, 0, 0);
	p = plan(WEAPON_MODULE_CHARGE, falcon, {});
	CHECK(p.charge_steps == 5);
	CHECK(p.charge_damage == WEAPON_RULE_MAULER_CHARGE_DAMAGE);
	p = plan(WEAPON_MODULE_CHARGE, crystal, {});
	CHECK(p.charge_start == WEAPON_RULE_CHARGE_ON_PRESS);
	CHECK(p.charge_time == 75);
	p = plan(WEAPON_MODULE_CHARGE, modgun, { { { "start", "on_press" }, { "release", "on_full" }, { "time", "40" },
		{ "steps", "2" }, { "ammo_per_step", "0" }, { "min_clip", "1" }, { "fall_time", "0" },
		{ "damage_per_step", "0" } } });
	CHECK(p.charge_start == WEAPON_RULE_CHARGE_ON_PRESS);
	CHECK(p.charge_release == WEAPON_RULE_RELEASE_ON_FULL);
	CHECK(p.charge_time == 40);
	CHECK(p.charge_steps == 2);
	CHECK(p.charge_ammo == 0);
	CHECK(p.charge_fall == 0);
	CHECK(p.charge_damage == 0.0f);
	p = plan(WEAPON_MODULE_CHARGE, falcon, { { { "damage_per_step", "3.33333325" } } });
	CHECK(p.charge_damage == WEAPON_RULE_MAULER_CHARGE_DAMAGE); // nine digits read back the same
	s32 ok = 1;
	p = plan(WEAPON_MODULE_CHARGE, falcon, { { { "start", "when_held" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.charge_start == WEAPON_RULE_CHARGE_WHILE_SELECTED);
	p = plan(WEAPON_MODULE_CHARGE, falcon, { { { "steps", "0" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.charge_steps == 5);
	p = plan(WEAPON_MODULE_COOK, grenade, {});
	CHECK(p.cook_fuse == 240);
	p = plan(WEAPON_MODULE_COOK, grenade, { { { "fuse", "120" } } });
	CHECK(p.cook_fuse == 120);
	p = plan(WEAPON_MODULE_COOK, grenade, { { { "fuse", "0" } } }, &ok);
	CHECK(ok == 0);

	// bgunTickMaulerCharge's charging as written (PR 31 review: every whole
	// step a frame crosses is paid for, and the charge stops at the last step
	// its clip could pay).
	const auto chargeTick = [](f32 &level, s32 &clip, const weapon_rule_trigger_t &plan, f32 lv) {
		const s32 oldvalue = level;
		const f32 steps = (f32)plan.charge_steps;
		if (clip >= weaponRulesChargeStepNeeds(&plan) && level < steps) level += lv * (steps / (f32)plan.charge_time);
		if (level > steps) level = steps;
		const s32 newvalue = level;
		for (s32 step = oldvalue; step < newvalue; step++) {
			if (clip < weaponRulesChargeStepNeeds(&plan)) {
				level = (f32)step;
				break;
			}
			weaponRulesSpendRounds(plan.charge_ammo, &clip);
		}
	};
	// The Mauler's charge, the original's arithmetic beside the plan's, over
	// held and switched functions at 1 to 3 ticks a frame.
	weaponRulesNativeTrigger(&mauler, &p);
	for (s32 lvi = 1; lvi <= 3; lvi++) {
		const f32 lv = (f32)lvi;
		f32 a = 0, b = 0;
		s32 clipa = 12, clipb = 12;
		for (s32 frame = 0; frame < 160; frame++) {
			const bool secondary = frame < 120;
			INFO("lv=" << lvi << " frame=" << frame);
			if (secondary) {
				s32 olda = a;
				if (clipa >= 2 && a < 5) a += lv * 0.05f;
				if (a > 5) a = 5;
				if (olda != (s32)a && clipa >= 2) clipa--;
				chargeTick(b, clipb, p, lv);
			} else {
				a -= lv * 0.005f;
				if (a < 0) a = 0;
				b -= lv * (1.0f / (f32)p.charge_fall);
				if (b < 0) b = 0;
			}
			REQUIRE(a == b);
			REQUIRE(clipa == clipb);
		}
	}
	// A charge that crosses several steps in a frame pays for each, and stops
	// at the last step its clip could pay.
	{
		weapon_rule_trigger_t fast = p;
		fast.charge_steps = 5;
		fast.charge_time = 1;
		fast.charge_ammo = 1;
		fast.charge_min_clip = 1;
		f32 level = 0;
		s32 clip = 3;
		chargeTick(level, clip, fast, 1.0f);
		CHECK(level == 3.0f);
		CHECK(clip == 0);
		chargeTick(level, clip, fast, 1.0f); // nothing left to pay with
		CHECK(level == 3.0f);
		level = 0;
		clip = 10;
		chargeTick(level, clip, fast, 1.0f);
		CHECK(level == 5.0f);
		CHECK(clip == 5);
		// Review: a step needs its whole cost in the clip, not only min_clip.
		CHECK(weaponRulesChargeStepNeeds(&p) == 2); // the Mauler's: its min_clip
		fast.charge_min_clip = 1;
		fast.charge_ammo = 2;
		CHECK(weaponRulesChargeStepNeeds(&fast) == 2);
		level = 0;
		clip = 5;
		chargeTick(level, clip, fast, 1.0f);
		CHECK(level == 2.0f); // two steps paid (4 rounds); the third would cost 2 of the 1 left
		CHECK(clip == 1);
		fast.charge_min_clip = 2;
		fast.charge_ammo = 3;
		level = 0;
		clip = 2;
		chargeTick(level, clip, fast, 1.0f);
		CHECK(level == 0.0f); // two rounds never buy a three-round step
		CHECK(clip == 2);
	}
	// The charged shot's damage is the original's expression for the Mauler.
	for (s32 tenth = 0; tenth <= 50; tenth++) {
		const u8 unk063a = (u8)tenth;
		const f32 damage = 1.5f;
		CHECK((unk063a / 3.0f + 1.0f) * damage == (unk063a / 3.0f + 1.0f) * damage);
	}

	// The seams read the plan where they read the Mauler's and the grenade's numbers.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("if (weaponWeaponCharge(player->gunctrl.weaponnum, NULL)) {\n\t\tbgunTickMaulerCharge();")
		!= std::string::npos);
	REQUIRE(bondgun.find("hand->chargelevel += g_Vars.lvupdate60freal * (steps / (f32)charge.charge_time);")
		!= std::string::npos);
	REQUIRE(bondgun.find("if (plan->module == WEAPON_MODULE_COOK\n\t\t\t&& hand->primetimer60 > TICKS(plan->cook_fuse)) {")
		!= std::string::npos);
	REQUIRE(bondgun.find("if (bgunHandPlan(handnum)->module == WEAPON_MODULE_COOK) {") != std::string::npos);
	CHECK(bondgun.find("hand->gset.weaponnum == WEAPON_MAULER") == std::string::npos);
	CHECK(bondgun.find("player->gunctrl.weaponnum == WEAPON_MAULER") == std::string::npos);
	const std::string accessors = readSource("src/game/game_0b0fd0.c");
	REQUIRE(accessors.find("if (charge.charge_damage == WEAPON_RULE_MAULER_CHARGE_DAMAGE) {") != std::string::npos);
	CHECK(accessors.find("if (gset->weaponnum == WEAPON_MAULER) {") == std::string::npos);
	const std::string mod = readSource("src/game/bondgun_mod.c");
	REQUIRE(mod.find("charge->releaseframe60 = g_Vars.lvframe60 + chargetime60;") != std::string::npos);
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("record.activation_time = ((const struct weaponfunc_throw *)f)->activatetime60;")
		!= std::string::npos);
}

// PR reviews of slices 2 to 5: settings read through variables, the held
// rocket, the launch item's function, the shield damage, idle admission and
// the Weapons creator.
namespace {

std::map<std::string, std::string> g_ReviewVariables;

// Resolves "$name" into the caller's buffer, as weaponGraphRuntimeHeldParamText does.
const char *reviewParamText(s32, const mission_graph_params_t *params, const char *key, char *buf, size_t cap)
{
	const char *text = missionGraphParamText(params, key);
	if (!text || text[0] != '$') return text;
	const auto it = g_ReviewVariables.find(text + 1);
	if (it == g_ReviewVariables.end()) return nullptr;
	std::snprintf(buf, cap, "%s", it->second.c_str());
	return buf;
}

s32 reviewItemNumber(const char *catalog_id)
{
	return catalog_id && std::string(catalog_id) == "base:a_launcher_item_with_a_long_catalog_id" ? WEAPON_ROCKET : -1;
}

} // namespace

TEST_CASE("a node's settings read through variables keep every value, however long",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	g_ReviewVariables = { { "projectile", "base:a_launcher_item_with_a_long_catalog_id" }, { "function", "gun" },
		{ "start", "on_press" }, { "release", "on_full" }, { "damage", "2" } };
	REQUIRE(g_ReviewVariables["projectile"].size() > 31);
	const std::string launch = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1,
		"\"kind\": \"og.weapon.launch\", \"item\": \"$projectile\", \"item_function\": \"$function\"");
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(launch.data(), launch.size(), heldKnown,
		err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	const weapon_rule_function_t crossbow = ruleRecord(WEAPON_CROSSBOW, 0, 0x0201, 0, 0);
	int instance = 0;
	weapon_rule_walk_t walk{};
	walk.weaponnum = -1;
	weaponRulesReadyHand(&walk.hand);
	walk.function = &crossbow;
	walk.param_text = reviewParamText;
	walk.item_number = reviewItemNumber;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon", &instance,
		&walk) == 1);
	CHECK(walk.plan.shot == WEAPON_MODULE_LAUNCH);
	CHECK(walk.plan.item == WEAPON_ROCKET); // the whole id, not cut at 31 and not overwritten by the function
	CHECK(walk.plan.item_function == WEAPON_RULE_ITEM_FUNCTION_GUN);
	missionGraphProgramFree(program);

	const std::string charge = baseIdleProgram("primary", 1,
		"\"kind\": \"og.weapon.charge\", \"start\": \"$start\", \"release\": \"$release\", \"damage_per_step\": \"$damage\"", 1);
	program = missionGraphProgramCompileWeapon(charge.data(), charge.size(), heldKnown, err, sizeof(err));
	REQUIRE(program != nullptr);
	const weapon_rule_function_t modgun = ruleRecord(WEAPON_CUSTOM_START, 0, 0x0001, 0, 0); // a press may start its charge
	walk.function = &modgun;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon", &instance,
		&walk) == 1);
	CHECK(walk.plan.charge_start == WEAPON_RULE_CHARGE_ON_PRESS);
	CHECK(walk.plan.charge_release == WEAPON_RULE_RELEASE_ON_FULL);
	CHECK(walk.plan.charge_damage == 2.0f);
	missionGraphProgramFree(program);
	g_ReviewVariables.clear();

	// The seams the reviews named.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("if (hand->rocket && shotplan->item != WEAPON_ROCKET && shotplan->item != WEAPON_HOMINGROCKET) {")
		!= std::string::npos);
	const size_t own = bondgun.find("weapon = weaponCreateProjectileFromGset(projectilemodelnum, &hand->gset, g_Vars.currentplayer->prop->chr);");
	const size_t function = bondgun.find("if (weapon && shotplan->item_function != WEAPON_RULE_ITEM_FUNCTION_KEEP) {");
	REQUIRE(own != std::string::npos);
	REQUIRE(function != std::string::npos);
	CHECK(own < function); // the item's function applies to the gun's own object too
	REQUIRE(bondgun.find("weaponFunctionPlan(info->weaponnum, hand->gset.weaponfunc, &press);") != std::string::npos);
	REQUIRE(bondgun.find("sp34 = sp34 == 1 ? 0 : -1;") != std::string::npos);
	const std::string chraction = readSource("src/game/chraction.c");
	REQUIRE(chraction.find("handplan = bgunHandPlan(h);") != std::string::npos);
	const std::string runtime = readSource("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("char vbuf[WEAPON_RULE_ITEM_LEN]; /* as long as an item's catalog id */") != std::string::npos);
	// The Weapons creator keeps a program's settings and ports and writes v2.
	const std::string editor = readSource("port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	REQUIRE(editor.find("snprintf(node.extra, sizeof(node.extra), \"%s\", extra.c_str());") != std::string::npos);
	REQUIRE(editor.find("copyString(edge.input, sizeof(edge.input), item[\"input\"]);") != std::string::npos);
	const std::string hub = readSource("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(hub.find("program ? \"pd.weapon_graph.v2\" : \"pd.weapon_graph.v1\",") != std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphNodes[i].extra,") != std::string::npos);
}

// PR 31 review, round 2: a charge's start and release, its steps' ammo, the
// committed charge for players and bots, the cooked fuse and the beam's reach.
TEST_CASE("a charge runs as its node says, or its weapon is refused",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	// The two pairs the runtime runs; every other is refused.
	CHECK(weaponRulesChargeUnsupported(WEAPON_RULE_CHARGE_WHILE_SELECTED, WEAPON_RULE_RELEASE_ON_PRESS, 0) == nullptr);
	CHECK(weaponRulesChargeUnsupported(WEAPON_RULE_CHARGE_WHILE_SELECTED, WEAPON_RULE_RELEASE_ON_PRESS, 1) == nullptr);
	CHECK(weaponRulesChargeUnsupported(WEAPON_RULE_CHARGE_ON_PRESS, WEAPON_RULE_RELEASE_ON_FULL, 1) == nullptr);
	CHECK(weaponRulesChargeUnsupported(WEAPON_RULE_CHARGE_ON_PRESS, WEAPON_RULE_RELEASE_ON_FULL, -1) == nullptr);
	const auto why = [](s32 start, s32 release, s32 custom) {
		const char *text = weaponRulesChargeUnsupported(start, release, custom);
		return std::string(text ? text : "");
	};
	CHECK(why(WEAPON_RULE_CHARGE_WHILE_SELECTED, WEAPON_RULE_RELEASE_ON_FULL, 1).find("(release on_press)")
		!= std::string::npos);
	CHECK(why(WEAPON_RULE_CHARGE_ON_PRESS, WEAPON_RULE_RELEASE_ON_PRESS, 1).find("(release on_full)")
		!= std::string::npos);
	CHECK(why(WEAPON_RULE_CHARGE_ON_PRESS, WEAPON_RULE_RELEASE_ON_FULL, 0).find("custom weapon's committed launch")
		!= std::string::npos);
	// A start left out is the function's, a release left out its start's.
	CHECK(weaponRulesChargeStartFromText(nullptr, 1) == WEAPON_RULE_CHARGE_ON_PRESS);
	CHECK(weaponRulesChargeStartFromText(nullptr, 0) == WEAPON_RULE_CHARGE_WHILE_SELECTED);
	CHECK(weaponRulesChargeStartFromText("held", 0) == -1);
	CHECK(weaponRulesChargeReleaseFromText(nullptr, WEAPON_RULE_CHARGE_ON_PRESS) == WEAPON_RULE_RELEASE_ON_FULL);
	CHECK(weaponRulesChargeReleaseFromText(nullptr, WEAPON_RULE_CHARGE_WHILE_SELECTED) == WEAPON_RULE_RELEASE_ON_PRESS);
	CHECK(weaponRulesChargeReleaseFromText("late", WEAPON_RULE_CHARGE_ON_PRESS) == -1);

	// The walk's plan: a pair the runtime cannot run is malformed and keeps one it runs.
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	const auto plan = [](const weapon_rule_function_t &record, Texts texts, s32 *ok) {
		weapon_rule_trigger_t out{};
		*ok = weaponRulesApplySettings(WEAPON_MODULE_CHARGE, Texts::get, &texts, &record, &out);
		return out;
	};
	const weapon_rule_function_t falcon = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0);
	const weapon_rule_function_t modgun = ruleRecord(WEAPON_CUSTOM_START, 0, 0x0001, 0, 0);
	s32 ok = 0;
	weapon_rule_trigger_t p = plan(modgun, { { { "start", "on_press" } } }, &ok);
	CHECK(ok == 1);
	CHECK(p.charge_start == WEAPON_RULE_CHARGE_ON_PRESS);
	CHECK(p.charge_release == WEAPON_RULE_RELEASE_ON_FULL); // its start's, not the Mauler's
	p = plan(modgun, { { { "start", "on_press" }, { "release", "on_press" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.charge_release == WEAPON_RULE_RELEASE_ON_FULL);
	p = plan(falcon, { { { "release", "on_full" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.charge_release == WEAPON_RULE_RELEASE_ON_PRESS);
	p = plan(falcon, { { { "start", "on_press" } } }, &ok);
	CHECK(ok == 0);
	CHECK(p.charge_start == WEAPON_RULE_CHARGE_WHILE_SELECTED); // a base weapon's charge builds while selected
	CHECK(p.charge_release == WEAPON_RULE_RELEASE_ON_PRESS);
	weapon_rule_function_t crystal = ruleRecord(WEAPON_CUSTOM_START, 1, 0x0201, 0, 0);
	crystal.has_charge = 1;
	crystal.charge_ticks60 = 75;
	p = plan(crystal, { { { "time", "30" } } }, &ok);
	CHECK(ok == 1);
	CHECK(p.charge_start == WEAPON_RULE_CHARGE_ON_PRESS); // the record's committed launch, for the node's time
	CHECK(p.charge_release == WEAPON_RULE_RELEASE_ON_FULL);
	CHECK(p.charge_time == 30);

	// Registration refuses the pairs the runtime does not run.
	const auto withAsset = [](std::string json, const std::string &asset) {
		const std::string base = "\"asset_id\": \"base:test__weapon\"";
		const size_t at = json.find(base);
		REQUIRE(at != std::string::npos);
		json.replace(at, base.size(), "\"asset_id\": \"" + asset + "\"");
		return json;
	};
	const std::string secondary = R"({ "schema": "pd.weapon_graph.v1", "asset_id": "base:test__weapon",
  "graph_id": "secondary", "nodes": [ { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary",
  "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1, "flags": 0, "damage": 0.9, "range": 60 } } ],
  "edges": [], "exports": [ { "name": "secondary", "node": "secondary_action" } ] })";
	char err[256] = { 0 };
	const auto registers = [&](s32 weaponnum, const std::string &asset, const std::string &settings) {
		const std::string primary = withAsset(baseIdleProgram("primary", 1,
			"\"kind\": \"og.weapon.charge\"" + settings, 1), asset);
		const std::string second = withAsset(secondary, asset);
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(weaponnum, asset.c_str(), primary.data(),
			(u32)primary.size(), second.data(), (u32)second.size(), nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
			err, sizeof(err)) == 0;
	};
	weaponGraphRuntimeClearAll();
	CHECK(registers(2, "base:test__weapon", "")); // builds while selected, fired by the trigger
	CHECK(registers(2, "base:test__weapon", ", \"start\": \"while_selected\", \"release\": \"on_press\""));
	CHECK_FALSE(registers(2, "base:test__weapon", ", \"start\": \"on_press\""));
	CHECK(std::string(err).find("og.weapon.charge: start on_press is a custom weapon's committed launch")
		!= std::string::npos);
	CHECK_FALSE(registers(2, "base:test__weapon", ", \"release\": \"on_full\""));
	CHECK(std::string(err).find("og.weapon.charge: a charge that builds while_selected is fired by the trigger")
		!= std::string::npos);
	CHECK_FALSE(registers(2, "base:test__weapon", ", \"start\": \"later\""));
	CHECK(std::string(err).find("og.weapon.charge needs start while_selected or on_press, got later")
		!= std::string::npos);
	CHECK_FALSE(registers(2, "base:test__weapon", ", \"release\": \"never\""));
	CHECK(std::string(err).find("og.weapon.charge needs release on_press or on_full, got never") != std::string::npos);
	CHECK_FALSE(registers(2, "base:test__weapon", ", \"damage_per_step\": -1"));
	CHECK(std::string(err).find("og.weapon.charge needs damage_per_step from 0 up to 100, got -1")
		!= std::string::npos);
	INFO(err);
	CHECK(registers(WEAPON_CUSTOM_START, "dev_test:test__weapon", ", \"start\": \"on_press\"")); // goes when full
	CHECK_FALSE(registers(WEAPON_CUSTOM_START, "dev_test:test__weapon",
		", \"start\": \"on_press\", \"release\": \"on_press\""));
	CHECK(std::string(err).find("og.weapon.charge: a charge that a press starts goes by itself when full")
		!= std::string::npos);
	weaponGraphRuntimeClearAll();

	// The seams: the committed charge is the attack's plan (players) and the
	// function's plan (bots, the gun's look), for the plan's time; the level
	// has its own field; each step a frame crosses is paid; a cooked
	// grenade's fuse and the beam's reach are the plan's.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("if (func != NULL && graph != NULL && hand->gset.weaponnum >= WEAPON_CUSTOM_START\n"
		"\t\t\t&& bgunHandPlan(handnum)->module == WEAPON_MODULE_CHARGE") != std::string::npos);
	CHECK(bondgun.find("graph != NULL && graph->has_charge") == std::string::npos);
	REQUIRE(bondgun.find("for (step = oldvalue; step < newvalue; step++) {") != std::string::npos);
	REQUIRE(bondgun.find("hand->chargelevel = step;") != std::string::npos);
	REQUIRE(bondgun.find("u32 weight = hand->chargelevel * 50.0f;") != std::string::npos);
	REQUIRE(bondgun.find("s32 matmot = hand->chargelevel;") != std::string::npos);
	size_t resets = 0;
	for (size_t at = bondgun.find("player->hands[i].chargelevel = 0"); at != std::string::npos;
			at = bondgun.find("player->hands[i].chargelevel = 0", at + 1)) {
		resets++;
	}
	CHECK(resets == 2); // with matmot1 to 3, wherever a weapon change clears them
	{
		const size_t tick = bondgun.find("void bgunTickMaulerCharge(void)\n{");
		REQUIRE(tick != std::string::npos);
		const size_t end = bondgun.find("\n}\n", tick);
		CHECK(bondgun.substr(tick, end - tick).find("matmot1") == std::string::npos);
	}
	REQUIRE(bondgun.find("s32 fuse240 = bgunHandPlan(handnum)->cook_fuse;") != std::string::npos);
	REQUIRE(bondgun.find("weapon->timer240 = fuse240 < 0 ? 0 : bgunGraphClampTicks240(fuse240);") != std::string::npos);
	REQUIRE(bondgun.find("weaponobj->timer240 = bgunGraphClampTicks240(TICKS(activatetime60 * 4));")
		!= std::string::npos);
	// A fired object's authored timer too (the NTSC beta branch is not built).
	REQUIRE(bondgun.find("weapon->timer240 = bgunGraphClampTicks240(TICKS(timer60 * 4));") != std::string::npos);
	CHECK(bondgun.find("TICKS(weaponobj->timer240 * 4)") == std::string::npos);
	REQUIRE(bondgun.find("if (hand->loadedammo[slot] < weaponRulesChargeStepNeeds(&charge)) {") != std::string::npos);
	CHECK(bondgun.find("charge.charge_min_clip") == std::string::npos);
	REQUIRE(bondgun.find("TICKS((plan->module == WEAPON_MODULE_COOK ? plan->cook_fuse : activationtime60)")
		!= std::string::npos);
	const std::string mod = readSource("src/game/bondgun_mod.c");
	REQUIRE(mod.find("static s32 bgunModFunctionCharges(s32 weaponnum, s32 funcnum, s32 *out_time60)")
		!= std::string::npos);
	CHECK(mod.find("->has_charge") == std::string::npos);
	CHECK(mod.find("->charge_time_ticks60") == std::string::npos);
	REQUIRE(mod.find("if (held == NULL || !bgunModFunctionCharges(gset->weaponnum, gset->weaponfunc, &chargetime60)) {")
		!= std::string::npos);
	REQUIRE(mod.find("s32 chargetime = charge->releaseframe60 - charge->startframe60;") != std::string::npos);
	REQUIRE(readSource("src/game/game_0b0fd0.c").find(
		"gset->unk063a = g_Vars.currentplayer->hands[handnum].chargelevel * 10.0f;") != std::string::npos);
	REQUIRE(readSource("src/include/types.h").find("/*0x0dac*/ f32 chargelevel;") != std::string::npos);
	const std::string prop = readSource("src/game/prop.c");
	size_t reach = 0;
	for (size_t at = prop.find("shotdata.hits[i].distance > shotplan->range"); at != std::string::npos;
			at = prop.find("shotdata.hits[i].distance > shotplan->range", at + 1)) {
		reach++;
	}
	CHECK(reach == 2);
	CHECK(prop.find("shotdata.hits[i].distance > 300") == std::string::npos);
	const std::string runtime = readSource("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("heldIrFunctionCommits(&ir, f), heldIrFunctionAmmoSlot(&ir, f), why, sizeof(why))) {")
		!= std::string::npos);
	REQUIRE(runtime.find("problem = weaponRulesChargeUnsupported(start,") != std::string::npos);
}

TEST_CASE("a manual reload and an empty reserve follow the program's reload",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	// PR 31 review: the walk weapon_events.c makes for a function's reload
	// (an idle hand, an empty clip, reserve) plans the program's style, so a
	// manual reload takes it and a regrowing clip needs no reserve.
	std::string json = baseIdleProgram("primary", 1);
	const std::string plain = "{ \"id\": \"primary_reload\", \"kind\": \"og.weapon.reload\" }";
	REQUIRE(json.find(plain) != std::string::npos);
	json.replace(json.find(plain), plain.size(),
		"{ \"id\": \"primary_reload\", \"kind\": \"og.weapon.reload\", \"style\": \"regrow\", \"regrow_time\": 30 }");
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(), heldKnown,
		err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	const weapon_rule_function_t falcon = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0);
	weapon_rule_walk_t walk{};
	int instance = 0;
	walk.weaponnum = -1;
	weaponRulesReadyHand(&walk.hand);
	walk.hand.uses_ammo = 1;
	walk.hand.clipsize = 1;
	walk.hand.reserve = 1;
	walk.function = &falcon;
	walk.param_text = weaponGraphRuntimeHeldParamText;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY, -1, "base:test__weapon", &instance,
		&walk) == 1);
	CHECK((walk.modules & WEAPON_MODULE_RELOAD) == WEAPON_MODULE_RELOAD);
	CHECK(walk.reload.style == WEAPON_RULE_RELOAD_REGROW);
	CHECK(walk.reload.regrow_time == 30);
	missionGraphProgramFree(program);

	// The seams: the hand's reload falls back to that walk, and the native
	// ammunition check and the gates count a regrowing clip as loading itself.
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("if (kept && record && weaponProgramReloadWalk(record->weaponnum, record->func, record, "
		"&kept->reload)) {") != std::string::npos);
	REQUIRE(events.find("weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY, -1, "
		"weaponEventWeaponId(weaponnum), &walk, &walk);") != std::string::npos);
	REQUIRE(events.find("|| weaponProgramRegrows(hand->gset.weaponnum, hand->gset.weaponfunc);") != std::string::npos);
	REQUIRE(events.find("&& reload.style == WEAPON_RULE_RELOAD_REGROW;") != std::string::npos);
	REQUIRE(readSource("src/game/bondgun.c").find("&& !weaponProgramRegrows(info->weaponnum, funcnum);")
		!= std::string::npos);
}

TEST_CASE("a charge fits the shot's byte, and a graph's cloak cloaks the player",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	// PR 31 review: the shot carries the charge as tenths of a step in a
	// byte, so a charge has at most 25 steps.
	CHECK(WEAPON_RULE_MAX_CHARGE_STEPS * 10 <= 255);
	CHECK((WEAPON_RULE_MAX_CHARGE_STEPS + 1) * 10 > 255);
	bool found = false;
	for (s32 i = 0; weaponRulesSettingAt(i); i++) {
		const weapon_rule_setting_t *setting = weaponRulesSettingAt(i);
		if (setting->module == WEAPON_MODULE_CHARGE && std::string(setting->key) == "steps") {
			found = true;
			CHECK(setting->hi == WEAPON_RULE_MAX_CHARGE_STEPS);
		}
	}
	CHECK(found);
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	const weapon_rule_function_t falcon = ruleRecord(WEAPON_FALCON2, 0, 0x0001, 0, 0);
	weapon_rule_trigger_t p{};
	Texts most;
	most.values["steps"] = "25";
	CHECK(weaponRulesApplySettings(WEAPON_MODULE_CHARGE, Texts::get, &most, &falcon, &p) == 1);
	CHECK(p.charge_steps == 25);
	Texts over;
	over.values["steps"] = "26";
	CHECK(weaponRulesApplySettings(WEAPON_MODULE_CHARGE, Texts::get, &over, &falcon, &p) == 0);
	CHECK(p.charge_steps == 5); // the Mauler's, kept

	// A graph-authored cloak (og.weapon.mode_toggle's cloak) cloaks the
	// player and goes off with a dropped gun, as the RCP120's does.
	const std::string chr = readSource("src/game/chr.c");
	REQUIRE(chr.find("|| (weaponCloakToggle(g_Vars.currentplayer->gunctrl.weaponnum, NULL, NULL)\n"
		"\t\t\t\t\t&& (g_Vars.currentplayer->devicesactive & DEVICE_CLOAKRCP120))) {") != std::string::npos);
	CHECK(chr.find("g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_RCP120") == std::string::npos);
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("// RC-P120 and cloaking device: turn off cloak if active\n"
		"\t\t/* (PR 31 review: any weapon with og.weapon.mode_toggle's cloak) */\n"
		"\t\tif (weaponCloakToggle(weaponnum, NULL, NULL)) {") != std::string::npos);
}

TEST_CASE("a special carries out only a special's actions, and a cloak needs rounds",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	// PR 31 review: the shoot, launch, throw and melee attack types read their
	// own families' data, so a special that named one would do nothing or read
	// past its function; every base special's action is a special's.
	for (const char *name : { "detonate", "boost", "revert_boost", "crouch", "uplink" }) {
		INFO(name);
		REQUIRE(weaponRulesActionFromText(name) > 0);
		CHECK(weaponRulesSpecialActionRuns(weaponRulesActionFromText(name)) == 1);
	}
	for (const char *name : { "shoot", "launch", "throw", "melee", "melee_no_uncloak", "cloak" }) {
		INFO(name);
		CHECK(weaponRulesActionFromText(name) == -1);
	}
	for (const BaseFunction &base : baseFunctions()) {
		weapon_rule_trigger_t plan;
		weaponRulesNativeTrigger(&base.record, &plan);
		if (plan.shot != WEAPON_MODULE_SPECIAL) continue;
		INFO(base.name);
		CHECK(weaponRulesSpecialActionRuns(plan.action) == 1);
	}

	// Registration refuses a special that would launch, and a cloak on a
	// function with no ammunition (its drain reads that ammunition's reserve).
	const std::string secondary = R"({ "schema": "pd.weapon_graph.v1", "asset_id": "base:test__weapon",
  "graph_id": "secondary", "nodes": [ { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary",
  "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1, "flags": 0, "damage": 0.9, "range": 60 } } ],
  "edges": [], "exports": [ { "name": "secondary", "node": "secondary_action" } ] })";
	char err[256] = { 0 };
	const auto registers = [&](const std::string &shot, s32 slot) {
		std::string primary = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1, shot);
		const std::string record = "\"kind\": \"fire.hitscan\", \"subgraph\": \"primary\", \"params\": { \"mode\": "
			"\"primary\", \"function_type\": \"shoot_single\", \"ammo_slot\": 0";
		REQUIRE(primary.find(record) != std::string::npos);
		primary.replace(primary.find(record), record.size(), "\"kind\": \"special.weapon_state\", \"subgraph\": "
			"\"primary\", \"params\": { \"mode\": \"primary\", \"function_type\": \"special\", \"ammo_slot\": " +
			std::to_string(slot));
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(2, "base:test__weapon", primary.data(), (u32)primary.size(),
			secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, err,
			sizeof(err)) == 0;
	};
	weaponGraphRuntimeClearAll();
	CHECK(registers("\"kind\": \"og.weapon.special\", \"action\": \"detonate\"", -1));
	INFO(err);
	CHECK_FALSE(registers("\"kind\": \"og.weapon.special\", \"action\": \"launch\"", -1));
	CHECK(std::string(err).find("og.weapon.special needs action detonate, boost, revert_boost, crouch or uplink, got launch")
		!= std::string::npos);
	CHECK(registers("\"kind\": \"og.weapon.mode_toggle\", \"mode\": \"cloak\"", 0));
	CHECK_FALSE(registers("\"kind\": \"og.weapon.mode_toggle\", \"mode\": \"cloak\"", -1));
	CHECK(std::string(err).find("og.weapon.mode_toggle: its cloak drains its function's rounds") != std::string::npos);
	weaponGraphRuntimeClearAll();

	// The seams: an ammunition-free function is no cloak, and the drain never
	// reads a reserve of ammunition type -1.
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("\t\t\tif (record.ammo_slot < 0 || record.ammo_slot > 1) {\n\t\t\t\tcontinue;\n\t\t\t}")
		!= std::string::npos);
	CHECK(events.find("record.ammo_slot >= 0 && record.ammo_slot <= 1 ? record.ammo_slot : 0") == std::string::npos);
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("if (player->gunctrl.ammotypes[cloakslot] < 0\n") != std::string::npos);
	REQUIRE(bondgun.find("if (hand->matmot1 > 1.0f && player->gunctrl.ammotypes[cloakslot] >= 0) {") != std::string::npos);
}

// held-weapon-events.md slice 6a: the target filter, the lock chain first.
TEST_CASE("the look and lock pickers are the target filter's native ones",
	"[mission-graph][weapon-graph][held-weapon][target-filter]")
{
	// lvRender's look test and sightCanTargetProp as they stood (lv.c and
	// sight.c at b66065f8), for a prop the aim ray found.
	const auto originalLook = [](s32 proptype, bool cloaked, bool ir, bool reacts, bool training) -> bool {
		if (proptype == PROPTYPE_CHR || proptype == PROPTYPE_PLAYER) return !(cloaked && !ir);
		if (proptype == PROPTYPE_OBJ || proptype == PROPTYPE_WEAPON || proptype == PROPTYPE_DOOR) return reacts || training;
		return false;
	};
	const auto originalLock = [](s32 proptype, bool reacts, bool rocketlauncher) -> bool {
		if (proptype == PROPTYPE_CHR || proptype == PROPTYPE_PLAYER) return true;
		if ((proptype == PROPTYPE_OBJ || proptype == PROPTYPE_WEAPON || proptype == PROPTYPE_DOOR) && reacts) return true;
		return rocketlauncher;
	};
	// sightTargetFacts' kinds: an object is a vehicle, a sentry or an object.
	const struct {
		s32 proptype;
		u32 kind;
	} props[] = {
		{ PROPTYPE_CHR, TARGET_KIND_GUARD }, { PROPTYPE_CHR, TARGET_KIND_SIMULANT },
		{ PROPTYPE_PLAYER, TARGET_KIND_PLAYER }, { PROPTYPE_OBJ, TARGET_KIND_OBJECT },
		{ PROPTYPE_OBJ, TARGET_KIND_VEHICLE }, { PROPTYPE_OBJ, TARGET_KIND_SENTRY },
		{ PROPTYPE_WEAPON, TARGET_KIND_OBJECT }, { PROPTYPE_DOOR, TARGET_KIND_OBJECT },
		{ PROPTYPE_EXPLOSION, 0 }, { PROPTYPE_SMOKE, 0 },
	};
	target_filter_t look, lock, rocket;
	targetFilterNative(TARGET_PICKER_LOOK, WEAPON_FALCON2, &look);
	targetFilterNative(TARGET_PICKER_LOCK, WEAPON_CMP150, &lock);
	targetFilterNative(TARGET_PICKER_LOCK, WEAPON_ROCKETLAUNCHER, &rocket);
	s32 compared = 0;
	for (const auto &p : props) {
		const bool chrlike = p.proptype == PROPTYPE_CHR || p.proptype == PROPTYPE_PLAYER;
		for (s32 bits = 0; bits < 16; bits++) {
			const bool cloaked = (bits & 1) && chrlike; // only characters cloak
			const bool ir = (bits & 2) != 0;
			const bool reacts = (bits & 4) && !chrlike;
			const bool training = (bits & 8) && !chrlike;
			target_facts_t c{};
			c.kind = p.kind;
			c.reacts_to_sight = reacts;
			c.training_target = training;
			c.relation = TARGET_RELATION_NONE;
			c.alive = 1;
			c.cloaked = cloaked;
			c.owner_ir = ir;
			INFO("proptype " << p.proptype << " kind " << p.kind << " bits " << bits);
			const bool looked = originalLook(p.proptype, cloaked, ir, reacts, training);
			CHECK((targetFilterAccepts(&look, &c) != 0) == looked);
			// The lock only sees what the look let through.
			if (looked) {
				CHECK((targetFilterAccepts(&lock, &c) != 0) == originalLock(p.proptype, reacts, false));
				CHECK((targetFilterAccepts(&rocket, &c) != 0) == originalLock(p.proptype, reacts, true));
			}
			compared++;
		}
	}
	CHECK(compared == 160);

	// The relation of an empty vehicle or an object: nobody's friend.
	CHECK(targetFilterRelationPasses(TARGET_RELATION_ENEMY, TARGET_RELATION_NONE) == 1);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_ANYONE, TARGET_RELATION_NONE) == 1);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_FRIEND, TARGET_RELATION_NONE) == 0);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_OWNER, TARGET_RELATION_NONE) == 0);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_ENEMY, TARGET_RELATION_FRIEND) == 0);
	// Mike's "occupied vehicles only".
	target_filter_t vehicles{};
	vehicles.kinds = TARGET_KIND_VEHICLE;
	vehicles.vehicles_occupied = 1;
	vehicles.relation = TARGET_RELATION_ENEMY;
	target_facts_t bike{};
	bike.kind = TARGET_KIND_VEHICLE;
	bike.relation = TARGET_RELATION_NONE;
	bike.alive = 1;
	CHECK(targetFilterAccepts(&vehicles, &bike) == 0);
	bike.occupied = 1;
	bike.relation = TARGET_RELATION_ENEMY;
	CHECK(targetFilterAccepts(&vehicles, &bike) == 1);
	vehicles.vehicles_occupied = 0; // "any vehicle": the bike its rider left
	bike.occupied = 0;
	bike.relation = TARGET_RELATION_NONE;
	CHECK(targetFilterAccepts(&vehicles, &bike) == 1);

	// The seams: lvRender and sightCanTargetProp ask the filter; the
	// rocket launcher's number is the table's.
	const std::string lv = readSource("src/game/lv.c");
	REQUIRE(lv.find("targetFilterNative(TARGET_PICKER_LOOK, bgunGetWeaponNum(HAND_RIGHT), &look);") != std::string::npos);
	CHECK(lv.find("(obj->flags3 & OBJFLAG3_REACTTOSIGHT) == 0") == std::string::npos);
	const std::string sight = readSource("src/game/sight.c");
	// slice 6b: the lock's filter is the sight's lock's, whose native one
	// is the table's (weaponRulesNativeLock).
	REQUIRE(sight.find("return targetFilterAccepts(filter, &facts) ? true : false;") != std::string::npos);
	CHECK(sight.find("WEAPON_ROCKETLAUNCHER") == std::string::npos);
	REQUIRE(readSource("port/include/weapon_rules.h").find(
		"targetFilterNative(TARGET_PICKER_LOCK, weaponnum, &out->filter);") != std::string::npos);
	REQUIRE(sight.find("void sightTargetFacts(struct prop *prop, struct target_facts *out)") != std::string::npos);
}

TEST_CASE("the Farsight's auto-target is the target filter's native one",
	"[mission-graph][weapon-graph][held-weapon][target-filter]")
{
	// farsightChooseTarget's test as it stood (prop.c at b66065f8), for a
	// character or another player in the chr slots.
	const auto original = [](bool friendly, s32 action, bool cloaked, bool unexplodable, bool playerdead) -> bool {
		return !unexplodable && !friendly && action != ACT_DIE && action != ACT_DRUGGEDDROP
			&& action != ACT_DRUGGEDKO && action != ACT_DEAD && !cloaked && !playerdead;
	};
	target_filter_t farsight, other;
	targetFilterNative(TARGET_PICKER_FARSIGHT, WEAPON_FARSIGHT, &farsight);
	targetFilterNative(TARGET_PICKER_FARSIGHT, WEAPON_FALCON2, &other);
	CHECK(other.kinds == 0); // only the Farsight auto-targets
	s32 compared = 0;
	for (u32 kind : { TARGET_KIND_GUARD, TARGET_KIND_SIMULANT, TARGET_KIND_PLAYER }) {
		for (s32 action : { ACT_STAND, ACT_DIE, ACT_DEAD, ACT_DRUGGEDDROP, ACT_DRUGGEDKO, ACT_ATTACK }) {
			for (s32 bits = 0; bits < 32; bits++) {
				const bool friendly = bits & 1, cloaked = bits & 2, unexplodable = bits & 4, ir = bits & 8;
				const bool playerdead = (bits & 16) && kind == TARGET_KIND_PLAYER;
				target_facts_t c{};
				c.kind = kind;
				c.relation = friendly ? TARGET_RELATION_FRIEND : TARGET_RELATION_ENEMY;
				c.alive = action != ACT_DIE && action != ACT_DEAD && !playerdead;
				c.drugged = action == ACT_DRUGGEDDROP || action == ACT_DRUGGEDKO;
				c.cloaked = cloaked;
				c.owner_ir = ir; // the Farsight ignores the IR scanner
				c.unexplodable = unexplodable;
				INFO("kind " << kind << " action " << action << " bits " << bits);
				CHECK((targetFilterAccepts(&farsight, &c) != 0)
					== original(friendly, action, cloaked, unexplodable, playerdead));
				CHECK(targetFilterAccepts(&other, &c) == 0);
				compared++;
			}
		}
	}
	CHECK(compared == 3 * 6 * 32);

	// The seam: farsightChooseTarget asks the filter; the Farsight's number is the table's.
	const std::string prop = readSource("src/game/prop.c");
	const size_t at = prop.find("void farsightChooseTarget(void)\n{");
	REQUIRE(at != std::string::npos);
	const std::string body = prop.substr(at, prop.find("\n}\n", at) - at);
	REQUIRE(body.find("targetFilterNative(TARGET_PICKER_FARSIGHT, weaponnum, &filter);") != std::string::npos);
	REQUIRE(body.find("if (targetFilterAccepts(&filter, &facts)) {") != std::string::npos);
	CHECK(body.find("WEAPON_FARSIGHT") == std::string::npos);
	CHECK(body.find("CHRCFLAG_UNEXPLODABLE") == std::string::npos);
}

TEST_CASE("the bot Slayer's proximity is the target filter's native one",
	"[mission-graph][weapon-graph][held-weapon][target-filter]")
{
	// rocketTickFbw's test as it stood (propobj.c at b66065f8).
	const auto original = [](bool owner, bool dead, bool enemy, bool invisible) -> bool {
		return !owner && !dead && enemy && !invisible;
	};
	target_filter_t slayer;
	targetFilterNative(TARGET_PICKER_BOT_SLAYER, WEAPON_SKROCKET, &slayer);
	s32 compared = 0;
	for (u32 kind : { TARGET_KIND_GUARD, TARGET_KIND_SIMULANT, TARGET_KIND_PLAYER }) {
		for (s32 bits = 0; bits < 16; bits++) {
			const bool owner = bits & 1, dead = bits & 2, enemy = bits & 4, invisible = bits & 8;
			// rocketFbwTargetFacts: the owner's own chr, else enemy or not.
			target_facts_t c{};
			c.kind = kind;
			c.relation = owner ? TARGET_RELATION_OWNER : (enemy ? TARGET_RELATION_ENEMY : TARGET_RELATION_FRIEND);
			c.alive = !dead;
			c.cloaked = invisible;
			INFO("kind " << kind << " bits " << bits);
			// The original asked COMPARE_ENEMIES only of another chr; the owner is never its own enemy.
			CHECK((targetFilterAccepts(&slayer, &c) != 0) == original(owner, dead, enemy && !owner, invisible));
			compared++;
		}
	}
	CHECK(compared == 48);

	const std::string propobj = readSource("src/game/propobj.c");
	REQUIRE(propobj.find("targetFilterNative(TARGET_PICKER_BOT_SLAYER, rocket->weaponnum, &fbwfilter);") != std::string::npos);
	REQUIRE(propobj.find("static void rocketFbwTargetFacts(struct chrdata *chr, struct chrdata *owner, target_facts_t *out)")
		!= std::string::npos);
	CHECK(propobj.find("&& chrCompareTeams(ownerchr, chr, COMPARE_ENEMIES)\n\t\t\t\t\t&& !botIsTargetInvisible(NULL, chr)) {")
		== std::string::npos);
}

TEST_CASE("the sentry's target is the target filter's native one",
	"[mission-graph][weapon-graph][held-weapon][target-filter]")
{
	// autogunTick's test as it stood (propobj.c at b66065f8), for a chr of a
	// team the autogun attacks.
	const auto original = [](bool hidden, bool cloaked, bool dead) -> bool {
		return !hidden && !cloaked && !dead;
	};
	target_filter_t sentry;
	targetFilterNative(TARGET_PICKER_SENTRY, -1, &sentry);
	s32 compared = 0;
	for (u32 kind : { TARGET_KIND_GUARD, TARGET_KIND_SIMULANT, TARGET_KIND_PLAYER }) {
		for (s32 bits = 0; bits < 8; bits++) {
			const bool hidden = bits & 1, cloaked = bits & 2, dead = bits & 4;
			target_facts_t c{};
			c.kind = kind;
			c.relation = TARGET_RELATION_ENEMY;
			c.alive = !dead;
			c.cloaked = hidden || cloaked;
			INFO("kind " << kind << " bits " << bits);
			CHECK((targetFilterAccepts(&sentry, &c) != 0) == original(hidden, cloaked, dead));
			compared++;
		}
	}
	CHECK(compared == 24);

	const std::string propobj = readSource("src/game/propobj.c");
	REQUIRE(propobj.find("targetFilterNative(TARGET_PICKER_SENTRY, -1, &sentryfilter);") != std::string::npos);
	REQUIRE(propobj.find("static void autogunTargetFacts(struct chrdata *chr, target_facts_t *out)") != std::string::npos);
	CHECK(propobj.find("&& (chr->hidden & CHRHFLAG_CLOAKED) == 0\n\t\t\t\t\t\t&& !chrIsDead(chr)) {") == std::string::npos);
}

TEST_CASE("auto-aim's candidates are the target filter's native ones",
	"[mission-graph][weapon-graph][held-weapon][target-filter]")
{
	// autoaimTick's candidate test as it stood (prop.c at b66065f8), before
	// chrCalculateAutoAim: not a friend, and armed.
	const auto original = [](bool friendly, bool right, bool left, bool force, bool gunprop) -> bool {
		return !friendly && (right || left || force || gunprop);
	};
	target_filter_t aim;
	targetFilterNative(TARGET_PICKER_AUTOAIM, WEAPON_FALCON2, &aim);
	s32 compared = 0;
	for (u32 kind : { TARGET_KIND_GUARD, TARGET_KIND_SIMULANT, TARGET_KIND_PLAYER }) {
		for (s32 bits = 0; bits < 32; bits++) {
			const bool friendly = bits & 1, right = bits & 2, left = bits & 4, force = bits & 8, gunprop = bits & 16;
			target_facts_t c{};
			c.kind = kind;
			c.relation = friendly ? TARGET_RELATION_FRIEND : TARGET_RELATION_ENEMY;
			c.alive = 1;
			c.armed = right || left || force || gunprop;
			INFO("kind " << kind << " bits " << bits);
			CHECK((targetFilterAccepts(&aim, &c) != 0) == original(friendly, right, left, force, gunprop));
			compared++;
		}
	}
	CHECK(compared == 96);

	const std::string prop = readSource("src/game/prop.c");
	REQUIRE(prop.find("targetFilterNative(TARGET_PICKER_AUTOAIM, bgunGetWeaponNum(HAND_RIGHT), &aimfilter);") != std::string::npos);
	REQUIRE(prop.find("if (targetFilterAccepts(&aimfilter, &facts)\n\t\t\t\t\t\t\t&& chrCalculateAutoAim(prop, &sp94, sp8c, sp84)) {")
		!= std::string::npos);
	CHECK(prop.find("|| (chr->chrflags & CHRCFLAG_FORCEAUTOAIM)\n\t\t\t\t\t\t\t\t|| chr->gunprop)") == std::string::npos);
	REQUIRE(readSource("src/game/sight.c").find("|| (chr->chrflags & CHRCFLAG_FORCEAUTOAIM) || chr->gunprop ? 1 : 0;")
		!= std::string::npos);
}

// held-weapon-events.md slice 6b: the filter as settings, on og.projectile.home.
TEST_CASE("a homing rocket's target filter is its home node's settings",
	"[mission-graph][weapon-graph][held-weapon][target-filter]")
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
	// Mike's "Enemy vehicles".
	Texts enemyVehicles;
	enemyVehicles.values = { { "kinds", "vehicle" }, { "vehicles", "occupied" }, { "relation", "enemy" },
		{ "alive", "true" }, { "cloaked", "never" } };
	target_filter_t f;
	const char *bad = nullptr;
	REQUIRE(targetFilterFromSettings(Texts::get, &enemyVehicles, &f, &bad) == 1);
	CHECK(f.kinds == TARGET_KIND_VEHICLE);
	CHECK(f.vehicles_occupied == 1);
	CHECK(f.relation == TARGET_RELATION_ENEMY);
	CHECK(f.alive_only == 1);
	CHECK(f.sees_cloaked == TARGET_SEES_CLOAKED_NEVER);
	CHECK(f.things_must_react == 0); // a hoverbike does not react to sight
	target_facts_t rider{};
	rider.kind = TARGET_KIND_PLAYER;
	rider.relation = TARGET_RELATION_ENEMY;
	rider.alive = 1;
	CHECK(targetFilterAccepts(&f, &rider) == 0); // the bike, never its rider
	target_facts_t bike{};
	bike.kind = TARGET_KIND_VEHICLE;
	bike.occupied = 1;
	bike.relation = TARGET_RELATION_ENEMY;
	bike.alive = 1;
	CHECK(targetFilterAccepts(&f, &bike) == 1);
	bike.occupied = 0;
	bike.relation = TARGET_RELATION_NONE;
	CHECK(targetFilterAccepts(&f, &bike) == 0); // occupied only: the rider bailed out
	// The written settings read back the same.
	char text[256];
	REQUIRE(targetFilterSettingsText(&f, text, sizeof(text)) > 0);
	INFO(text);
	CHECK(std::string(text).find("\"kinds\": \"vehicle\"") != std::string::npos);
	// Kinds, defaults and refusals.
	u32 kinds = 0;
	CHECK(targetFilterKindsFromText("player,simulant", &kinds) == 0);
	CHECK(kinds == (TARGET_KIND_PLAYER | TARGET_KIND_SIMULANT));
	CHECK(targetFilterKindsFromText("characters, vehicle", &kinds) == 0);
	CHECK(kinds == (TARGET_KINDS_CHARACTERS | TARGET_KIND_VEHICLE));
	CHECK(targetFilterKindsFromText("tank", &kinds) == -1);
	CHECK(targetFilterKindsFromText("", &kinds) == -1);
	Texts none;
	CHECK(targetFilterFromSettings(Texts::get, &none, &f, &bad) == 0); // names none: anything
	CHECK(f.kinds == TARGET_KINDS_ALL);
	Texts wrong;
	wrong.values = { { "relation", "rival" } };
	CHECK(targetFilterFromSettings(Texts::get, &wrong, &f, &bad) == -1);
	REQUIRE(bad != nullptr);
	CHECK(std::string(bad) == "relation");
	CHECK(std::string(targetFilterSettingHelp("relation")) == "anyone, enemy, friend, owner, not_owner or not_friend");

	// og.projectile.home: the original's names no filter and writes none.
	projectile_flight_t native;
	projectileRulesNativeFlight(WEAPON_HOMINGROCKET, FUNC_PRIMARY, &native);
	CHECK(native.home_filtered == 0);
	char buf[256];
	CHECK(projectileRulesFlightSettingsText(PROJECTILE_MODULE_HOME, &native, buf, sizeof(buf)) == 0);
	projectile_flight_t authored = native;
	Texts home = enemyVehicles;
	home.values["lost"] = "straight";
	CHECK(projectileRulesFlightSettings(PROJECTILE_MODULE_HOME, Texts::get, &home, &authored, nullptr) == 1);
	CHECK(authored.home_filtered == 1);
	CHECK(authored.home_filter.kinds == TARGET_KIND_VEHICLE);
	CHECK(projectileRulesFlightSame(PROJECTILE_MODULE_HOME, &authored, &native) == 0);
	CHECK(projectileRulesFlightSame(PROJECTILE_MODULE_HOME, &native, &native) == 1);
	REQUIRE(projectileRulesFlightSettingsText(PROJECTILE_MODULE_HOME, &authored, buf, sizeof(buf)) > 0);
	{
		// The written home reads back the same.
		Texts back;
		const std::string s(buf);
		for (const char *key : { "kinds", "vehicles", "relation", "cloaked" }) {
			const std::string needle = std::string("\"") + key + "\": \"";
			const size_t at = s.find(needle);
			REQUIRE(at != std::string::npos);
			back.values[key] = s.substr(at + needle.size(), s.find('"', at + needle.size()) - (at + needle.size()));
		}
		back.values["alive"] = s.find("\"alive\": true") != std::string::npos ? "true" : "false";
		projectile_flight_t again = native;
		CHECK(projectileRulesFlightSettings(PROJECTILE_MODULE_HOME, Texts::get, &back, &again, nullptr) == 1);
		CHECK(projectileRulesFlightSame(PROJECTILE_MODULE_HOME, &again, &authored) == 1);
	}
	Texts badlost;
	badlost.values = { { "lost", "spiral" } };
	const char *badkey = nullptr;
	projectile_flight_t g = native;
	CHECK(projectileRulesFlightSettings(PROJECTILE_MODULE_HOME, Texts::get, &badlost, &g, &badkey) == 0);
	REQUIRE(badkey != nullptr);
	CHECK(std::string(badkey) == "lost");

	// The seams: the homing code re-checks a filtered home's target while it
	// flies, with the facts as the rocket's owner reads them.
	const std::string propobj = readSource("src/game/propobj.c");
	REQUIRE(propobj.find("if (projectile->targetprop && flightset->home_filtered) {") != std::string::npos);
	REQUIRE(propobj.find("sightTargetFactsFor(projectile->targetprop, projectile->ownerprop,\n"
		"\t\t\t\t\t\t\tsightOwnerUsesIr(projectile->ownerprop), &homefacts);") != std::string::npos);
	REQUIRE(propobj.find("if (!targetFilterAccepts(&flightset->home_filter, &homefacts)) {\n"
		"\t\t\t\t\t\t\tprojectile->targetprop = NULL;") != std::string::npos);
	const std::string sight = readSource("src/game/sight.c");
	REQUIRE(sight.find("void sightTargetFactsFor(struct prop *prop, struct prop *ownerprop, s32 owner_ir, struct target_facts *out)")
		!= std::string::npos);
	REQUIRE(sight.find("sightTargetFactsFor(prop, g_Vars.currentplayer->prop, USINGDEVICE(DEVICE_IRSCANNER) ? 1 : 0, out);")
		!= std::string::npos);
}

namespace {

// The base wiring of a function whose weapon has a lock, as the extractor
// writes it (romextract_pdweapon.c, s_emitHeldLockNodes and
// s_emitHeldLockEdge): event.weapon.aiming and og.weapon.lock_on with its
// settings, before the idle wiring's nodes and edges.
std::string withLock(std::string text, const std::string &m, const std::string &settings)
{
	const std::string pressed = "{ \"id\": \"" + m + "_pressed\"";
	text.insert(text.find(pressed), "{ \"id\": \"" + m + "_aiming\", \"kind\": \"event.weapon.aiming\" },"
		"{ \"id\": \"" + m + "_lock\", \"kind\": \"og.weapon.lock_on\"" + settings + " },");
	const std::string firstEdge = "{ \"from\": \"" + m + "_pressed\"";
	text.insert(text.find(firstEdge), "{ \"from\": \"" + m + "_aiming\", \"output\": \"out\", \"to\": \"" + m +
		"_lock\", \"input\": \"exec\" },");
	return text;
}

} // namespace

// held-weapon-events.md slice 6b: the sight's lock is og.weapon.lock_on at
// event.weapon.aiming, and the original's is the weapon's track type.
TEST_CASE("the sight's lock is the original's, and a program's lock_on is its settings",
	"[mission-graph][weapon-graph][held-weapon][target-filter][weapon-rules]")
{
	// The original's: the rocket launcher holds one target, whatever the
	// player looks at; the CMP150 four, things only if the stage marks them,
	// and its secondary aims at them; every other weapon none.
	weapon_rule_lock_t rocket, cmp, cmpsec, falcon, unarmed, mod, threat;
	weaponRulesNativeLock(WEAPON_ROCKETLAUNCHER, FUNC_SECONDARY, SIGHTTRACKTYPE_ROCKETLAUNCHER, &rocket);
	CHECK(rocket.locks == 1);
	CHECK(rocket.aim_follows == 0);
	CHECK(rocket.tracktype == SIGHTTRACKTYPE_ROCKETLAUNCHER);
	CHECK(rocket.filter.things_must_react == 0);
	weaponRulesNativeLock(WEAPON_CMP150, FUNC_PRIMARY, SIGHTTRACKTYPE_FOLLOWLOCKON, &cmp);
	weaponRulesNativeLock(WEAPON_CMP150, FUNC_SECONDARY, SIGHTTRACKTYPE_FOLLOWLOCKON, &cmpsec);
	CHECK(cmp.locks == WEAPON_RULE_MAX_LOCKS);
	CHECK(cmp.aim_follows == 0);
	CHECK(cmpsec.aim_follows == 1);
	CHECK(cmp.tracktype == SIGHTTRACKTYPE_FOLLOWLOCKON);
	CHECK(cmp.filter.things_must_react == 1);
	CHECK(cmp.filter.training_targets == 0);
	weaponRulesNativeLock(WEAPON_FALCON2, FUNC_PRIMARY, SIGHTTRACKTYPE_DEFAULT, &falcon);
	CHECK(falcon.locks == 0);
	CHECK(falcon.tracktype == SIGHTTRACKTYPE_DEFAULT);
	weaponRulesNativeLock(WEAPON_UNARMED, FUNC_PRIMARY, SIGHTTRACKTYPE_NONE, &unarmed);
	CHECK(unarmed.locks == 0);
	CHECK(unarmed.tracktype == SIGHTTRACKTYPE_NONE);
	weaponRulesNativeLock(WEAPON_UNARMED, FUNC_PRIMARY, SIGHTTRACKTYPE_THREATDETECTOR, &threat);
	CHECK(threat.tracktype == SIGHTTRACKTYPE_THREATDETECTOR);
	// A mod weapon whose descriptor names the rocket launcher's track type
	// keeps today's filter: only the rocket launcher's number takes things
	// that do not react.
	weaponRulesNativeLock(WEAPON_CUSTOM_START, FUNC_PRIMARY, SIGHTTRACKTYPE_ROCKETLAUNCHER, &mod);
	CHECK(mod.locks == 1);
	CHECK(mod.filter.things_must_react == 1);
	// A lock's track type: one target the rocket launcher's, more the
	// follow lock-on's numbered boxes; no lock keeps a weapon's own unless it
	// is a lock's.
	weapon_rule_lock_t none{};
	CHECK(weaponRulesLockTrackType(&none, SIGHTTRACKTYPE_ROCKETLAUNCHER) == SIGHTTRACKTYPE_DEFAULT);
	CHECK(weaponRulesLockTrackType(&none, SIGHTTRACKTYPE_NONE) == SIGHTTRACKTYPE_NONE);
	none.locks = 2;
	CHECK(weaponRulesLockTrackType(&none, SIGHTTRACKTYPE_DEFAULT) == SIGHTTRACKTYPE_FOLLOWLOCKON);

	// A node's settings, over one target, no follow and the "anything" filter.
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	Texts empty;
	weapon_rule_lock_t lock;
	CHECK(weaponRulesApplyLockSettings(Texts::get, &empty, &lock) == 1);
	CHECK(lock.locks == 1);
	CHECK(lock.aim_follows == 0);
	CHECK(lock.filter.kinds == TARGET_KINDS_ALL);
	CHECK(weaponRulesLockEqual(&lock, &rocket) == 1); // the rocket launcher's is the default node
	Texts follow;
	follow.values = { { "locks", "4" }, { "aim_follows", "true" }, { "sight_targets_only", "true" } };
	CHECK(weaponRulesApplyLockSettings(Texts::get, &follow, &lock) == 1);
	CHECK(weaponRulesLockEqual(&lock, &cmpsec) == 1);
	CHECK(weaponRulesLockEqual(&lock, &cmp) == 0);
	for (const auto &bad : std::vector<std::pair<std::string, std::string>>{ { "locks", "5" }, { "locks", "0" },
			{ "aim_follows", "yes" }, { "relation", "rival" }, { "kinds", "tank" },
			{ "sight_targets_only", "maybe" } }) {
		INFO(bad.first << "=" << bad.second);
		Texts wrong;
		wrong.values = { bad };
		CHECK(weaponRulesApplyLockSettings(Texts::get, &wrong, &lock) == 0);
	}
	// The sight targets setting is the filter's, on og.projectile.home too.
	target_filter_t marked;
	const char *badkey = nullptr;
	Texts sightTargets;
	sightTargets.values = { { "sight_targets_only", "true" } };
	CHECK(targetFilterFromSettings(Texts::get, &sightTargets, &marked, &badkey) == 1);
	CHECK(marked.things_must_react == 1);
	CHECK(std::string(targetFilterSettingHelp("sight_targets_only")) == "true or false");
	target_filter_t anything;
	targetFilterAnything(&anything);
	CHECK(targetFilterSame(&anything, &rocket.filter) == 1); // the training targets do not matter here
	char text[512];
	REQUIRE(weaponRulesLockSettingsText(&cmpsec, text, (s32)sizeof(text)) > 0);
	INFO(text);
	CHECK(std::string(text).find("\"locks\": 4, \"aim_follows\": true, \"kinds\": \"all\", \"sight_targets_only\": true")
		!= std::string::npos);

	// The base wiring, walked as the seam walks it, holds the original's
	// lock whatever the hand holds.
	int instance = 0;
	const auto walkLock = [&](const std::string &json, const weapon_rule_states_t *with, weapon_rule_hand_t hand,
		weapon_rule_lock_t *out, u32 *modules) -> s32 {
		char err[256] = { 0 };
		mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(),
			heldKnown, err, sizeof(err));
		INFO(err);
		REQUIRE(program != nullptr);
		weapon_rule_walk_t walk{};
		weapon_rule_states_t states = with ? *with : weapon_rule_states_t{};
		walk.weaponnum = -1;
		walk.hand = hand;
		walk.states = &states;
		walk.param_text = weaponGraphRuntimeHeldParamText;
		const s32 handled = weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_AIMING, 0, "base:test__weapon",
			&instance, &walk);
		*out = walk.lock;
		*modules = walk.modules;
		missionGraphProgramFree(program);
		return handled;
	};
	struct Case { s32 weaponnum; s32 func; s32 tracktype; const char *mode; };
	for (const Case &c : { Case{ WEAPON_ROCKETLAUNCHER, FUNC_PRIMARY, SIGHTTRACKTYPE_ROCKETLAUNCHER, "primary" },
			Case{ WEAPON_ROCKETLAUNCHER, FUNC_SECONDARY, SIGHTTRACKTYPE_ROCKETLAUNCHER, "secondary" },
			Case{ WEAPON_CMP150, FUNC_PRIMARY, SIGHTTRACKTYPE_FOLLOWLOCKON, "primary" },
			Case{ WEAPON_CMP150, FUNC_SECONDARY, SIGHTTRACKTYPE_FOLLOWLOCKON, "secondary" } }) {
		weapon_rule_lock_t native;
		weaponRulesNativeLock(c.weaponnum, c.func, c.tracktype, &native);
		char settings[512];
		REQUIRE(weaponRulesLockSettingsText(&native, settings, (s32)sizeof(settings)) > 0);
		const std::string json = withLock(baseIdleProgram(c.mode, 1), c.mode, settings);
		for (const weapon_rule_hand_t hand : { weapon_rule_hand_t{ 1, 0, 8, 0, 0 }, weapon_rule_hand_t{ 1, 8, 8, 1, 1 },
				weapon_rule_hand_t{ 0, 0, 0, 0, 1 } }) {
			weapon_rule_lock_t walked;
			u32 modules = 0;
			INFO("weapon=" << c.weaponnum << " func=" << c.func << " loaded=" << hand.loaded);
			REQUIRE(walkLock(json, nullptr, hand, &walked, &modules) == 1);
			CHECK(modules == WEAPON_MODULE_LOCK_ON);
			walked.tracktype = weaponRulesLockTrackType(&walked, c.tracktype);
			CHECK(weaponRulesLockEqual(&walked, &native) == 1);
		}
	}

	// A gate decides the lock while aiming: a scope that locks only while
	// its state is on; handled without a lock, the box follows the aim.
	std::string scoped = withLock(baseIdleProgram("primary", 1), "primary", ", \"locks\": 2");
	scoped.replace(scoped.find("{ \"from\": \"primary_aiming\", \"output\": \"out\", \"to\": \"primary_lock\""),
		std::string("{ \"from\": \"primary_aiming\", \"output\": \"out\", \"to\": \"primary_lock\"").size(),
		"{ \"from\": \"primary_aiming\", \"output\": \"out\", \"to\": \"primary_scoped\", \"input\": \"exec\" },"
		"{ \"from\": \"primary_scoped\", \"output\": \"pass\", \"to\": \"primary_lock\"");
	scoped.insert(scoped.find("{ \"id\": \"primary_lock\""),
		"{ \"id\": \"primary_scoped\", \"kind\": \"gate.weapon.state_is\", \"key\": \"scoped\", \"equals\": true },");
	weapon_rule_lock_t walked;
	u32 modules = 0;
	REQUIRE(walkLock(scoped, nullptr, weapon_rule_hand_t{ 1, 8, 8, 1, 1 }, &walked, &modules) == 1);
	CHECK(modules == 0);
	CHECK(walked.locks == 0);
	CHECK(weaponRulesLockTrackType(&walked, SIGHTTRACKTYPE_ROCKETLAUNCHER) == SIGHTTRACKTYPE_DEFAULT);
	weapon_rule_states_t on{};
	on.count = 1;
	std::snprintf(on.keys[0], sizeof(on.keys[0]), "scoped");
	on.values[0] = 1;
	REQUIRE(walkLock(scoped, &on, weapon_rule_hand_t{ 1, 8, 8, 1, 1 }, &walked, &modules) == 1);
	CHECK(modules == WEAPON_MODULE_LOCK_ON);
	CHECK(walked.locks == 2);
	CHECK(weaponRulesLockTrackType(&walked, SIGHTTRACKTYPE_DEFAULT) == SIGHTTRACKTYPE_FOLLOWLOCKON);

	// Registration: the lock only at aiming, only the lock there, and its
	// settings values of their kinds ("$name" from the weapon's variables).
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
	const s32 weaponnum = 3;
	char err[256] = { 0 };
	const auto registers = [&](const std::string &primary, const char *variables = nullptr) {
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(weaponnum, "base:test__weapon", primary.data(),
			(u32)primary.size(), secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0,
			variables, variables ? (u32)std::strlen(variables) : 0, nullptr, 0, err, sizeof(err)) == 0;
	};
	weaponGraphRuntimeClearAll();
	const std::string enemyVehicles = withLock(baseIdleProgram("primary", 1), "primary",
		", \"locks\": 1, \"kinds\": \"vehicle\", \"vehicles\": \"occupied\", \"relation\": \"enemy\", "
		"\"alive\": true, \"cloaked\": \"never\"");
	REQUIRE(registers(enemyVehicles));
	const mission_graph_program_t *program = weaponGraphRuntimeHeldProgram(weaponnum, 0);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_WEAPON_AIMING) == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_WEAPON_AIMING, "og.weapon.lock_on") == 1);
	std::string pressLocks = baseIdleProgram("primary", 1);
	pressLocks.replace(pressLocks.find("\"kind\": \"og.weapon.dry_fire\""),
		std::string("\"kind\": \"og.weapon.dry_fire\"").size(), "\"kind\": \"og.weapon.lock_on\"");
	CHECK_FALSE(registers(pressLocks));
	CHECK(std::string(err).find("og.weapon.lock_on has no effect on event.weapon.trigger_pressed") != std::string::npos);
	std::string aimReloads = withLock(baseIdleProgram("primary", 1), "primary", "");
	aimReloads.replace(aimReloads.find("\"kind\": \"og.weapon.lock_on\""),
		std::string("\"kind\": \"og.weapon.lock_on\"").size(), "\"kind\": \"og.weapon.dry_fire\"");
	CHECK_FALSE(registers(aimReloads));
	CHECK(std::string(err).find("og.weapon.dry_fire has no effect on event.weapon.aiming") != std::string::npos);
	for (const auto &bad : std::vector<std::pair<std::string, std::string>>{
			{ ", \"locks\": 5", "og.weapon.lock_on needs locks from 1 to 4, got 5" },
			{ ", \"aim_follows\": \"yes\"", "og.weapon.lock_on needs aim_follows true or false, got yes" },
			{ ", \"relation\": \"rival\"", "og.weapon.lock_on needs relation anyone, enemy, friend, owner, not_owner or not_friend, got rival" },
			{ ", \"kinds\": \"tank\"", "og.weapon.lock_on needs kinds player, simulant, guard, vehicle" },
			{ ", \"sight_targets_only\": \"maybe\"", "og.weapon.lock_on needs sight_targets_only true or false, got maybe" },
			{ ", \"relation\": \"$side\"", "og.weapon.lock_on needs relation anyone, enemy, friend, owner, not_owner or not_friend, got $side" } }) {
		INFO(bad.first);
		CHECK_FALSE(registers(withLock(baseIdleProgram("primary", 1), "primary", bad.first)));
		INFO(err);
		CHECK(std::string(err).find(bad.second) != std::string::npos);
	}
	const char *variables = R"({"schema":"pd.weapon_variables.v1","asset_id":"base:test__weapon",
		"variables":[{"name":"side","value":"enemy"}]})";
	CHECK(registers(withLock(baseIdleProgram("primary", 1), "primary", ", \"relation\": \"$side\""), variables));
	weaponGraphRuntimeClearAll();

	// The seams: the sight takes its lock from weaponSightLock while the
	// player aims, and auto-aim's follow from the same lock.
	const std::string sight = readSource("src/game/sight.c");
	REQUIRE(sight.find("const weapon_rule_lock_t *lock = weaponSightLock(sighton);") != std::string::npos);
	REQUIRE(sight.find("newtracktype = lock->tracktype;") != std::string::npos);
	CHECK(sight.find("gunsettings->tracktype") == std::string::npos);
	const std::string take = "sightCanTargetProp(g_Vars.currentplayer->lookingatprop.prop, lock->locks, &lock->filter)) {\n"
		"\t\t\tindex = sightFindFreeTargetIndex(lock->locks);";
	REQUIRE(sight.find(take) != std::string::npos);
	CHECK(sight.find(take, sight.find(take) + 1) != std::string::npos); // the rocket launcher's and the follow's
	const std::string prop = readSource("src/game/prop.c");
	REQUIRE(prop.find("if (weaponSightLock(0)->aim_follows) {") != std::string::npos);
	CHECK(prop.find("bgunGetWeaponNum(HAND_RIGHT) == WEAPON_CMP150") == std::string::npos);
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_AIMING, g_Vars.currentplayernum,")
		!= std::string::npos);
	REQUIRE(events.find("weaponRulesNativeLock(hand->gset.weaponnum, hand->gset.weaponfunc, tracktype, &native);")
		!= std::string::npos);
	REQUIRE(events.find("if (!weaponRulesLockEqual(out, native)) {") != std::string::npos);
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("weaponRulesNativeLock(weapon_id, mode, tracktype, &lock);") != std::string::npos);
	REQUIRE(extractor.find("if (s_emitHeldLockNodes(&w, mode_name, &lock) != 0) w.error = 1;\n"
		"\t\ts_emitHeldProgramNodes(&w, mode_name,") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldLockEdge(&w, mode_name, &lock);\n\t\ts_emitHeldProgramEdges(&w, mode_name, &plan);")
		!= std::string::npos);
	REQUIRE(extractor.find("if (weaponRulesLockSettingsText(lock, settings, (s32)sizeof(settings)) < 0) return -1;")
		!= std::string::npos);
	const std::string trackTypeArg = "s_weaponTrackType(wpn), s_functionCasing(wpn, ";
	REQUIRE(extractor.find(trackTypeArg) != std::string::npos);
	CHECK(extractor.find(trackTypeArg, extractor.find(trackTypeArg) + 1) != std::string::npos);
}

// held-weapon-events.md slice 6b: what sets a mine off is its arm's filter,
// and a mod mine's record filters are its native one.
TEST_CASE("a mine's arm names what sets it off, and a mod mine's record is its native filter",
	"[mission-graph][held-weapon][target-filter][projectile-rules]")
{
	// The wanted relations "not the owner" and "not a friend".
	CHECK(targetFilterRelationPasses(TARGET_RELATION_NOT_OWNER, TARGET_RELATION_FRIEND) == 1);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_NOT_OWNER, TARGET_RELATION_OWNER) == 0);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_NOT_OWNER, TARGET_RELATION_NONE) == 1);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_NOT_FRIEND, TARGET_RELATION_OWNER) == 1);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_NOT_FRIEND, TARGET_RELATION_ENEMY) == 1);
	CHECK(targetFilterRelationPasses(TARGET_RELATION_NOT_FRIEND, TARGET_RELATION_FRIEND) == 0);

	// bgunModProxyDecision's record filters as they stood (bondgun_mod.c at
	// b66065f8), for a character within the mine's radius: the owner by
	// player number, the team test by COMPARE_ENEMIES, skipped when the owner
	// cannot be found.
	const auto original = [](s32 ownerMode, s32 teamMode, s32 targetMode, bool isOwner, bool ownerKnown,
		bool enemies) -> bool {
		if (ownerMode == 1 && isOwner) return false;
		if (ownerMode == 2 && !isOwner) return false;
		if ((teamMode == 1 || targetMode == 1) && ownerKnown && !isOwner && !enemies) return false;
		return true;
	};
	s32 compared = 0;
	for (s32 ownerMode = 0; ownerMode <= 3; ownerMode++)
	for (s32 teamMode = 0; teamMode <= 1; teamMode++)
	for (s32 targetMode = 0; targetMode <= 1; targetMode++)
	for (s32 bits = 0; bits < 8; bits++) {
		const bool isOwner = bits & 1, ownerKnown = bits & 2, enemies = bits & 4;
		if (isOwner && !ownerKnown) continue; // the owner's own character is always found
		target_filter_t filter;
		targetFilterModMine(ownerMode, teamMode, targetMode, &filter);
		for (u32 kind : { TARGET_KIND_PLAYER, TARGET_KIND_SIMULANT, TARGET_KIND_GUARD }) {
			// The facts as bgunModMineTargetFacts reads them.
			target_facts_t c{};
			c.kind = kind;
			c.alive = 1;
			c.relation = isOwner ? TARGET_RELATION_OWNER
				: ownerKnown && !enemies ? TARGET_RELATION_FRIEND : TARGET_RELATION_ENEMY;
			INFO("owner_mode=" << ownerMode << " team=" << teamMode << " target=" << targetMode << " bits=" << bits);
			CHECK((targetFilterAccepts(&filter, &c) == 1)
				== original(ownerMode, teamMode, targetMode, isOwner, ownerKnown, enemies));
			compared++;
		}
	}
	CHECK(compared == 4 * 2 * 2 * 6 * 3);
	// The record's quirk: hostile only, no owner filter, lets the owner set it off.
	target_filter_t quirk;
	targetFilterModMine(0, 1, 0, &quirk);
	CHECK(quirk.relation == TARGET_RELATION_NOT_FRIEND);

	// The arm's settings: the original names none; arm_proximity takes the
	// filter's; the other arms take none.
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	projectile_arm_t arm;
	projectileRulesNativeArm(WEAPON_PROXIMITYMINE, FUNC_PRIMARY, &arm);
	CHECK(arm.proximity_filtered == 0);
	Texts none;
	CHECK(projectileRulesArmSettings(PROJECTILE_MODULE_ARM_PROXIMITY, Texts::get, &none, &arm, nullptr) == 1);
	CHECK(arm.proximity_filtered == 0);
	Texts enemies;
	enemies.values = { { "kinds", "characters" }, { "relation", "not_friend" } };
	CHECK(projectileRulesArmSettings(PROJECTILE_MODULE_ARM_FUSE, Texts::get, &enemies, &arm, nullptr) == 1);
	CHECK(arm.proximity_filtered == 0);
	CHECK(projectileRulesArmSettings(PROJECTILE_MODULE_ARM_PROXIMITY, Texts::get, &enemies, &arm, nullptr) == 1);
	CHECK(arm.proximity_filtered == 1);
	CHECK(arm.proximity_filter.kinds == TARGET_KINDS_CHARACTERS);
	CHECK(targetFilterSame(&arm.proximity_filter, &quirk) == 1);
	Texts wrong;
	wrong.values = { { "relation", "rival" } };
	const char *bad = nullptr;
	projectile_arm_t untouched;
	projectileRulesNativeArm(WEAPON_PROXIMITYMINE, FUNC_PRIMARY, &untouched);
	CHECK(projectileRulesArmSettings(PROJECTILE_MODULE_ARM_PROXIMITY, Texts::get, &wrong, &untouched, &bad) == 0);
	REQUIRE(bad != nullptr);
	CHECK(std::string(bad) == "relation");
	CHECK(untouched.proximity_filtered == 0);
	CHECK(std::string(projectileRulesFlightSettingHelp("relation")) == "anyone, enemy, friend, owner, not_owner or not_friend");

	// The seams: the sweep judges what came near by the arm's filter, as the
	// mine's owner sees it, and a mod mine's record by its native filter.
	const std::string propobj = readSource("src/game/propobj.c");
	REQUIRE(propobj.find("const target_filter_t *armfilter = projectileArmProximityFilter(&weapon->base);") != std::string::npos);
	REQUIRE(propobj.find("sightTargetFactsFor(candidate, owner, sightOwnerUsesIr(owner), &facts);\n"
		"\t\t\t\tfiltered = candidate && targetFilterAccepts(armfilter, &facts) ? 1 : 0;") != std::string::npos);
	// PR 49 review: "with_ir" there reads the owner's IR scanner, as the
	// sight reads the current player's.
	REQUIRE(readSource("src/game/sight.c").find("return (player->devicesactive & ~player->devicesinhibit & DEVICE_IRSCANNER) ? 1 : 0;")
		!= std::string::npos);
	CHECK(propobj.find("weaponProxyOwnerProp(weapon), 0, &facts)") == std::string::npos);
	REQUIRE(propobj.find("s32 decision = bgunModProxyDecision(weapon, chr, pos, filtered);") != std::string::npos);
	REQUIRE(propobj.find("\t\t\tif (filtered == 0) {\n\t\t\t\t/* Slice 6b: its arm does not listen for this. */\n"
		"\t\t\t\tcontinue;") != std::string::npos);
	REQUIRE(propobj.find("coordTriggerProxiesFor(chr, chr->prop, &pos, true);") != std::string::npos);
	REQUIRE(propobj.find("coordTriggerProxiesBy(obj->prop, &obj->prop->pos, false);") != std::string::npos);
	REQUIRE(readSource("src/game/bondeyespy.c").find("coordTriggerProxiesBy(chr->prop, &chr->prop->pos, true);")
		!= std::string::npos);
	REQUIRE(readSource("src/game/training.c").find("coordTriggerProxiesBy(prop, &prop->pos, true);") != std::string::npos);
	const std::string mod = readSource("src/game/bondgun_mod.c");
	REQUIRE(mod.find("targetFilterModMine(entity->proxy_owner_filter_mode, entity->proxy_team_filter_mode,\n"
		"\t\t\tentity->proxy_target_filter_mode, &filter);") != std::string::npos);
	REQUIRE(mod.find("bgunModMineTargetFacts(chr, ownerchr, index == owner, &facts);") != std::string::npos);
	CHECK(mod.find("ownerchr != chr && !chrCompareTeams") == std::string::npos);
	const std::string events = readSource("port/src/projectile_events.c");
	REQUIRE(events.find("context.arm = &armset;") != std::string::npos);
	REQUIRE(events.find("projectileRulesArmSettings(bit, projectileParamText, (void *)params, rule->context->arm, NULL);")
		!= std::string::npos);
	REQUIRE(readSource("port/src/weapon_graph_runtime.c").find(
		"if (projectileRulesArmSettings(bit, projectileFlightParamText, (void *)params, &arm, &bad)) return 1;")
		!= std::string::npos);
}

// held-weapon-events.md slice 6c: a lock that names vehicles seeks them.
TEST_CASE("a lock that names vehicles seeks them, and the original's locks do not",
	"[mission-graph][held-weapon][target-filter][weapon-rules]")
{
	weapon_rule_lock_t rocket, cmp, falcon;
	weaponRulesNativeLock(WEAPON_ROCKETLAUNCHER, FUNC_PRIMARY, SIGHTTRACKTYPE_ROCKETLAUNCHER, &rocket);
	weaponRulesNativeLock(WEAPON_CMP150, FUNC_SECONDARY, SIGHTTRACKTYPE_FOLLOWLOCKON, &cmp);
	weaponRulesNativeLock(WEAPON_FALCON2, FUNC_PRIMARY, SIGHTTRACKTYPE_DEFAULT, &falcon);
	// The original's locks take what the look offers, which is never a vehicle
	// that does not react to sight.
	CHECK(weaponRulesLockSeeksVehicles(&rocket) == 0);
	CHECK(weaponRulesLockSeeksVehicles(&cmp) == 0);
	CHECK(weaponRulesLockSeeksVehicles(&falcon) == 0);
	CHECK(weaponRulesLockSeeksVehicles(nullptr) == 0);
	// Mike's "Enemy vehicles", and a lock that names vehicles among others.
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	Texts enemyVehicles;
	enemyVehicles.values = { { "kinds", "vehicle" }, { "vehicles", "occupied" }, { "relation", "enemy" },
		{ "alive", "true" }, { "cloaked", "never" } };
	weapon_rule_lock_t lock;
	REQUIRE(weaponRulesApplyLockSettings(Texts::get, &enemyVehicles, &lock) == 1);
	CHECK(weaponRulesLockSeeksVehicles(&lock) == 1);
	Texts mixed;
	mixed.values = { { "kinds", "player, vehicle" }, { "locks", "2" } };
	REQUIRE(weaponRulesApplyLockSettings(Texts::get, &mixed, &lock) == 1);
	CHECK(weaponRulesLockSeeksVehicles(&lock) == 1);
	Texts all;
	all.values = { { "kinds", "all" } };
	REQUIRE(weaponRulesApplyLockSettings(Texts::get, &all, &lock) == 1);
	CHECK(weaponRulesLockSeeksVehicles(&lock) == 0);
	Texts people;
	people.values = { { "kinds", "characters" } };
	REQUIRE(weaponRulesApplyLockSettings(Texts::get, &people, &lock) == 1);
	CHECK(weaponRulesLockSeeksVehicles(&lock) == 0);

	// The seams: a vehicle's facts come from its rider and its damage, and the
	// look offers vehicles and riders' vehicles to a lock that seeks them.
	const std::string sight = readSource("src/game/sight.c");
	REQUIRE(sight.find("struct prop *rider = sightVehicleRider(prop);") != std::string::npos);
	REQUIRE(sight.find("out->occupied = rider || (obj->hidden & OBJHFLAG_MOUNTED) ? 1 : 0;") != std::string::npos);
	REQUIRE(sight.find("out->relation = rider == ownerprop ? TARGET_RELATION_OWNER") != std::string::npos);
	REQUIRE(sight.find("out->alive = objGetDestroyedLevel(obj) == 0 ? 1 : 0;") != std::string::npos);
	REQUIRE(sight.find("vehicleGraphRuntimeSeatOfPlayer(vehicle, vehicle->lifecycle_generation, i) != VEHICLE_GRAPH_SEAT_GUNNER")
		!= std::string::npos);
	REQUIRE(sight.find("return g_Vars.players[playernum]->hoverbike;") != std::string::npos);
	const std::string lv = readSource("src/game/lv.c");
	REQUIRE(lv.find("if (weaponRulesLockSeeksVehicles(lock)) {") != std::string::npos);
	REQUIRE(lv.find("? NULL : sightRiderVehicle(g_Vars.currentplayer->lookingatprop.prop);") != std::string::npos);
	REQUIRE(lv.find("if (facts.kind == TARGET_KIND_VEHICLE && targetFilterAccepts(&lock->filter, &facts)) {\n"
		"\t\t\t\t\t\t\t\tlook.things_must_react = 0;") != std::string::npos);
	// PR 49 review: a weapon whose lock takes targets picks and refreshes
	// them as the aim-tracking base weapons do, and what an authored lock
	// takes passes the native look.
	REQUIRE(lv.find("|| weaponSightLock(0)->locks > 0;") != std::string::npos);
	REQUIRE(lv.find("|| (tracksaim && bmoveIsInSightAimMode())) {") != std::string::npos);
	REQUIRE(lv.find("} else if (tracksaim) {") != std::string::npos);
	REQUIRE(lv.find("&& !(weaponSightLockAuthored() && targetFilterAccepts(&lock->filter, &facts))) {")
		!= std::string::npos);
	REQUIRE(readSource("port/src/weapon_events.c").find("return weaponRulesLockEqual(lock, &native) ? 0 : 1;")
		!= std::string::npos);

	// The smoke's evidence: each target the sight takes is counted, the
	// first ones logged with what it is.
	const std::string take = "g_Vars.currentplayer->targetset[index] = 0;\n"
		"\t\t\t\tweaponSightLockTaken(index, trackedprop->prop);";
	REQUIRE(sight.find(take) != std::string::npos);
	CHECK(sight.find(take, sight.find(take) + 1) != std::string::npos); // the rocket launcher's and the follow's
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("\"SIGHT.LOCK: player=%d weapon=%s func=%s slot=%d of %d kind=%s occupied=%d relation=%s\",")
		!= std::string::npos);
	REQUIRE(events.find("rules.differ=%d aiming=%d locks=%d\",") != std::string::npos);
	CHECK(std::string(targetFilterKindName(TARGET_KIND_VEHICLE)) == "vehicle");
	CHECK(std::string(targetFilterRelationName(TARGET_RELATION_NONE)) == "none");
	CHECK(std::string(targetFilterRelationName(TARGET_RELATION_NOT_FRIEND)) == "not_friend");
}

namespace {

// A shooting function's record with the Falcon 2-like recoil of
// invfunc_00011160 (6, 18, 2, 8; recovery 0; distance 5, angle 2) and the
// default jitter.
weapon_rule_function_t recoilRecord()
{
	weapon_rule_function_t f{};
	f.weaponnum = WEAPON_FALCON2;
	f.func = FUNC_PRIMARY;
	f.functype = 0x0001;
	f.ammo_slot = 0;
	f.recoil_kick = 6;
	f.recoil_return = 18;
	f.recoil_cut_after = 2;
	f.recoil_cut_return = 8;
	f.recoil_recovery = 0;
	f.recoil_distance = 5.0f;
	f.recoil_angle = 2.0f;
	f.recoil_slide = 0.5f;
	f.recoil_has_shake = 1;
	f.recoil_shake[0] = 0.6f;
	f.recoil_shake[1] = 0.6f;
	f.recoil_shake[2] = 0.6f;
	return f;
}

// og.weapon.recoil as the extractor writes it (s_emitHeldRecoilNode: every
// setting, its floats as %.7g).
std::string recoilNodeSettings(const weapon_rule_trigger_t &plan)
{
	char buf[512];
	std::snprintf(buf, sizeof(buf), ", \"kick\": %d, \"return\": %d, \"recovery\": %d, \"refire_early\": %s, "
		"\"cut_after\": %d, \"cut_return\": %d, \"distance\": %.7g, \"angle\": %.7g, \"slide\": %.7g",
		plan.recoil_kick, plan.recoil_return, plan.recoil_recovery, plan.recoil_refire ? "true" : "false",
		plan.recoil_cut_after, plan.recoil_cut_return, (double)plan.recoil_distance, (double)plan.recoil_angle,
		(double)plan.recoil_slide);
	std::string s = buf;
	if (plan.recoil_has_shake) {
		std::snprintf(buf, sizeof(buf), ", \"shake_x\": %.7g, \"shake_y\": %.7g, \"shake_z\": %.7g",
			(double)plan.recoil_shake[0], (double)plan.recoil_shake[1], (double)plan.recoil_shake[2]);
		s += buf;
	}
	if (plan.recoil_has_spin_shake) {
		std::snprintf(buf, sizeof(buf), ", \"spin_shake_from\": %.7g, \"spin_shake_to\": %.7g",
			(double)plan.recoil_spin_from, (double)plan.recoil_spin_to);
		s += buf;
	}
	return s;
}

} // namespace

// held-weapon-events.md slice 7a: a shot's recoil is its program's, else
// its function's own.
TEST_CASE("a shot's recoil is its function's own unless its program's recoil node names it",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	const weapon_rule_function_t record = recoilRecord();
	weapon_rule_trigger_t native;
	weaponRulesNativeTrigger(&record, &native);
	CHECK(native.recoil == 1);
	CHECK(native.recoil_kick == 6);
	CHECK(native.recoil_return == 18);
	CHECK(native.recoil_refire == 1);
	CHECK(native.recoil_cut_after == 2);
	CHECK(native.recoil_cut_return == 8);
	CHECK(weaponRulesRecoilCutAfter(&native) == 2);
	CHECK(weaponRulesRecoilCutReturn(&native) == 8);
	CHECK(native.recoil_distance == 5.0f);
	CHECK(native.recoil_has_shake == 1);
	CHECK(native.recoil_has_spin_shake == 0);
	// The original's "no cut" (unk26 -1 or 0) and "no early refire" (unk27
	// below 0) read back as the seam read them.
	weapon_rule_function_t nocut = record;
	nocut.recoil_cut_after = -1;
	nocut.recoil_cut_return = -1;
	weapon_rule_trigger_t plan;
	weaponRulesNativeTrigger(&nocut, &plan);
	CHECK(plan.recoil_refire == 0);
	CHECK(weaponRulesRecoilCutAfter(&plan) == 0);
	CHECK(weaponRulesRecoilCutReturn(&plan) == -1);
	// A function that does not shoot has none, and a recoil does not fit it.
	weapon_rule_function_t thrown = record;
	thrown.functype = 0x0002;
	weaponRulesNativeTrigger(&thrown, &plan);
	CHECK(plan.recoil == 0);
	CHECK(weaponRulesRecoilFits(0x0002) == 0);
	CHECK(weaponRulesRecoilFits(0x0201) == 1);
	// The recoil is part of the attack's plan.
	plan = native;
	plan.recoil_kick = 7;
	CHECK(weaponRulesTriggerEqual(&plan, &native) == 0);

	// Its settings, over the function's own.
	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	Texts none;
	REQUIRE(weaponRulesApplyRecoilSettings(Texts::get, &none, &record, &plan) == 1);
	CHECK(weaponRulesRecoilEqual(&plan, &native) == 1);
	Texts heavy;
	heavy.values = { { "kick", "12" }, { "refire_early", "false" }, { "slide", "0.0" }, { "shake_z", "2.6" },
		{ "spin_shake_from", "0" } };
	REQUIRE(weaponRulesApplyRecoilSettings(Texts::get, &heavy, &record, &plan) == 1);
	CHECK(plan.recoil_kick == 12);
	CHECK(plan.recoil_return == 18);
	CHECK(plan.recoil_refire == 0);
	CHECK(weaponRulesRecoilCutReturn(&plan) == -1);
	CHECK(plan.recoil_slide == 0.0f);
	CHECK(plan.recoil_shake[0] == 0.6f);
	CHECK(plan.recoil_shake[2] == 2.6f);
	CHECK(plan.recoil_has_spin_shake == 1);
	CHECK(plan.recoil_spin_from == 0.0f);
	CHECK(plan.recoil_spin_to == 1.0f); // the other end left out: no change at full spin
	for (const auto &bad : std::vector<std::pair<std::string, std::string>>{ { "kick", "200" }, { "angle", "-5" },
			{ "refire_early", "yes" }, { "shake_x", "a" }, { "distance", "1001" } }) {
		INFO(bad.first << "=" << bad.second);
		Texts wrong;
		wrong.values = { bad };
		CHECK(weaponRulesApplyRecoilSettings(Texts::get, &wrong, &record, &plan) == 0);
	}
	CHECK(weaponRulesNonNegFromText("0.0", 10.0f, -1.0f) == 0.0f);
	CHECK(weaponRulesNonNegFromText("-0", 10.0f, -1.0f) == -1.0f);

	// The base wiring with its recoil node, as the extractor writes it,
	// plans the original's recoil.
	int instance = 0;
	std::string json = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1,
		"\"kind\": \"og.weapon.hitscan\"");
	json.insert(json.find("], \"edges\""), ",{ \"id\": \"primary_recoil\", \"kind\": \"og.weapon.recoil\"" +
		recoilNodeSettings(native) + " }");
	json.insert(json.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_shot\", \"output\": \"out\", "
		"\"to\": \"primary_recoil\", \"input\": \"exec\" },");
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(), heldKnown, err,
		sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	weapon_rule_walk_t walk{};
	weapon_rule_states_t states{};
	walk.weaponnum = -1;
	walk.hand = weapon_rule_hand_t{ 1, 8, 8, 1, 1 };
	walk.states = &states;
	walk.function = &record;
	walk.param_text = weaponGraphRuntimeHeldParamText;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon", &instance,
		&walk) == 1);
	CHECK((walk.modules & WEAPON_MODULE_RECOIL) != 0);
	CHECK(weaponRulesRecoilEqual(&walk.plan, &native) == 1);
	missionGraphProgramFree(program);
	// Without the node the walk plans the function's own recoil.
	json = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1, "\"kind\": \"og.weapon.hitscan\"");
	program = missionGraphProgramCompileWeapon(json.data(), json.size(), heldKnown, err, sizeof(err));
	REQUIRE(program != nullptr);
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon", &instance,
		&walk) == 1);
	CHECK((walk.modules & WEAPON_MODULE_RECOIL) == 0);
	CHECK(weaponRulesRecoilEqual(&walk.plan, &native) == 1);
	missionGraphProgramFree(program);

	// Registration: a recoil only on a shooting function, its settings values
	// of their kinds.
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
	// A melee function's program that reaches a recoil.
	const std::string meleeRecoil = R"({
  "schema": "pd.weapon_graph.v2",
  "asset_id": "base:test__weapon",
  "graph_id": "secondary",
  "nodes": [
    { "id": "secondary_trigger", "kind": "event.trigger_pressed", "subgraph": "secondary", "params": { "mode": "secondary", "trigger": "pressed" } },
    { "id": "secondary_action", "kind": "melee.strike", "subgraph": "secondary", "params": { "mode": "secondary", "function_type": "melee", "ammo_slot": -1, "flags": 0, "damage": 0.9, "range": 60 } },
    { "id": "secondary_pressed", "kind": "event.weapon.trigger_pressed" },
    { "id": "secondary_attack", "kind": "og.weapon.fire_once" },
    { "id": "secondary_recoil", "kind": "og.weapon.recoil" }
  ],
  "edges": [ { "from": "secondary_trigger", "to": "secondary_action" },
    { "from": "secondary_pressed", "output": "out", "to": "secondary_attack", "input": "exec" },
    { "from": "secondary_attack", "output": "out", "to": "secondary_recoil", "input": "exec" } ],
  "exports": [ { "name": "secondary", "node": "secondary_action" } ]
})";
	const s32 weaponnum = 4;
	const auto registers = [&](const std::string &primary, const std::string &second) {
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(weaponnum, "base:test__weapon", primary.data(),
			(u32)primary.size(), second.data(), (u32)second.size(), nullptr, 0, nullptr, 0,
			nullptr, 0, nullptr, 0, err, sizeof(err)) == 0;
	};
	const auto withRecoil = [&](const std::string &settings) {
		std::string text = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1);
		text.insert(text.find("], \"edges\""), ",{ \"id\": \"primary_recoil\", \"kind\": \"og.weapon.recoil\"" +
			settings + " }");
		text.insert(text.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_use_ammo\", \"output\": "
			"\"out\", \"to\": \"primary_recoil\", \"input\": \"exec\" },");
		return text;
	};
	weaponGraphRuntimeClearAll();
	REQUIRE(registers(withRecoil(", \"kick\": 6, \"slide\": 0.0, \"shake_z\": 2.6"), secondary));
	CHECK_FALSE(registers(withRecoil(""), meleeRecoil));
	INFO(err);
	CHECK(std::string(err).find("og.weapon.recoil fits only a shooting function") != std::string::npos);
	for (const auto &bad : std::vector<std::pair<std::string, std::string>>{
			{ ", \"kick\": 200", "og.weapon.recoil needs kick from 0 to 127, got 200" },
			{ ", \"angle\": -5", "og.weapon.recoil needs angle from 0 up to 360, got -5" },
			{ ", \"refire_early\": \"yes\"", "og.weapon.recoil needs refire_early true or false, got yes" } }) {
		INFO(bad.first);
		CHECK_FALSE(registers(withRecoil(bad.first), secondary));
		INFO(err);
		CHECK(std::string(err).find(bad.second) != std::string::npos);
	}
	weaponGraphRuntimeClearAll();

	// The seams read the attack's plan; the held graph reads the jitter and
	// the Reaper's spin; the extractor writes the node; simulants' interval
	// has an accessor.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("const weapon_rule_trigger_t *plan = bgunHandPlan(handnum);\n\n#if PAL") != std::string::npos);
	CHECK(bondgun.find("graph && graph->has_recoil_anim_unk24 ? graph->recoil_anim_unk24 : func->unk24")
		== std::string::npos);
	REQUIRE(bondgun.find("unk26 = weaponRulesRecoilCutAfter(plan);") != std::string::npos);
	REQUIRE(bondgun.find("unk27 = weaponRulesRecoilCutReturn(plan);") != std::string::npos);
	REQUIRE(bondgun.find("slidemax = bgunHandPlan(handnum)->recoil_slide;") != std::string::npos);
	REQUIRE(bondgun.find("&& bgunHandPlan(handnum)->recoil_has_shake) {") != std::string::npos);
	CHECK(bondgun.find("shootfunc->recoilsettings->xrange") == std::string::npos);
	REQUIRE(bondgun.find("if (spinplan->recoil_has_spin_shake) {") != std::string::npos);
	const std::string runtime = readSource("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("heldRawObjectFloat(rs->value, \"xrange\", &x)") != std::string::npos);
	REQUIRE(runtime.find("heldRawArrayFirstFloat(vs->value, &from) && heldRawArrayFirstFloat(vm->value, &to)")
		!= std::string::npos);
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("\"use_ammo\", \"shot\", \"recoil\",") != std::string::npos);
	REQUIRE(extractor.find("(i >= 10 && i <= 13 && !plan->recoil)") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldRecoilNode(w, ids[i], plan, last);") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramEdge(w, plan->shot ? shot : plan->rounds > 0 ? use_ammo : attack, \"out\", "
		"recoil, 0);") != std::string::npos);
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("return plan.recoil_kick + plan.recoil_return;") != std::string::npos);
}

// held-weapon-events.md slice 7b: a shot's casing and muzzle flash.
TEST_CASE("a shot's casing and flash are its function's own unless its program names them",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	weapon_rule_function_t record = recoilRecord();
	record.casing = CASING_RIFLE + 1;
	weapon_rule_trigger_t native;
	weaponRulesNativeTrigger(&record, &native);
	CHECK(weaponRulesCasingEject(&native) == CASING_RIFLE);
	CHECK(native.flash == 1);
	CHECK(std::string(weaponRulesCasingName(native.casing)) == "rifle");
	// A function flagged for no flash, one with no casing, and one that does
	// not shoot.
	weapon_rule_function_t dark = record;
	dark.flags = WEAPON_RULE_FUNCFLAG_NOMUZZLEFLASH;
	dark.casing = 0;
	weapon_rule_trigger_t plan;
	weaponRulesNativeTrigger(&dark, &plan);
	CHECK(plan.flash == 0);
	CHECK(weaponRulesCasingEject(&plan) == -1);
	weapon_rule_function_t thrown = record;
	thrown.functype = 0x0002;
	weaponRulesNativeTrigger(&thrown, &plan);
	CHECK(plan.casing == 0);
	CHECK(plan.flash == 0);
	// A zeroed plan ejects none.
	weapon_rule_trigger_t zero{};
	CHECK(weaponRulesCasingEject(&zero) == -1);
	// They are part of the attack's plan.
	plan = native;
	plan.casing = CASING_SHOTGUN + 1;
	CHECK(weaponRulesTriggerEqual(&plan, &native) == 0);
	plan = native;
	plan.flash = 0;
	CHECK(weaponRulesTriggerEqual(&plan, &native) == 0);

	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	Texts none;
	REQUIRE(weaponRulesApplyCasingSettings(Texts::get, &none, &record, &plan) == 1);
	CHECK(weaponRulesCasingEject(&plan) == CASING_RIFLE);
	Texts shotgun;
	shotgun.values = { { "type", "shotgun" }, { "on", "false" } };
	REQUIRE(weaponRulesApplyCasingSettings(Texts::get, &shotgun, &record, &plan) == 1);
	CHECK(weaponRulesCasingEject(&plan) == CASING_SHOTGUN);
	REQUIRE(weaponRulesApplyFlashSettings(Texts::get, &shotgun, &record, &plan) == 1);
	CHECK(plan.flash == 0);
	Texts nocase;
	nocase.values = { { "type", "none" } };
	REQUIRE(weaponRulesApplyCasingSettings(Texts::get, &nocase, &record, &plan) == 1);
	CHECK(weaponRulesCasingEject(&plan) == -1);
	Texts wrong;
	wrong.values = { { "type", "brass" }, { "on", "yes" } };
	CHECK(weaponRulesApplyCasingSettings(Texts::get, &wrong, &record, &plan) == 0);
	CHECK(weaponRulesApplyFlashSettings(Texts::get, &wrong, &record, &plan) == 0);
	CHECK(weaponRulesCasingFromText("reaper") == CASING_REAPER + 1);

	// The base wiring's chain (shot, recoil, casing, flash), as the
	// extractor writes it, plans the original's.
	int instance = 0;
	std::string json = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1,
		"\"kind\": \"og.weapon.hitscan\"");
	json.insert(json.find("], \"edges\""), ",{ \"id\": \"primary_recoil\", \"kind\": \"og.weapon.recoil\"" +
		recoilNodeSettings(native) + " },{ \"id\": \"primary_casing\", \"kind\": \"og.weapon.casing\", \"type\": \"" +
		weaponRulesCasingName(native.casing) + "\" },{ \"id\": \"primary_flash\", \"kind\": \"og.weapon.muzzle_flash\", "
		"\"on\": true }");
	json.insert(json.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_shot\", \"output\": \"out\", "
		"\"to\": \"primary_recoil\", \"input\": \"exec\" },{ \"from\": \"primary_recoil\", \"output\": \"out\", "
		"\"to\": \"primary_casing\", \"input\": \"exec\" },{ \"from\": \"primary_casing\", \"output\": \"out\", "
		"\"to\": \"primary_flash\", \"input\": \"exec\" },");
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(), heldKnown, err,
		sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	weapon_rule_walk_t walk{};
	weapon_rule_states_t states{};
	walk.weaponnum = -1;
	walk.hand = weapon_rule_hand_t{ 1, 8, 8, 1, 1 };
	walk.states = &states;
	walk.function = &record;
	walk.param_text = weaponGraphRuntimeHeldParamText;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon", &instance,
		&walk) == 1);
	CHECK((walk.modules & (WEAPON_MODULE_RECOIL | WEAPON_MODULE_CASING | WEAPON_MODULE_MUZZLE_FLASH))
		== (WEAPON_MODULE_RECOIL | WEAPON_MODULE_CASING | WEAPON_MODULE_MUZZLE_FLASH));
	CHECK(weaponRulesTriggerEqual(&walk.plan, &native) == 1);
	missionGraphProgramFree(program);

	// Registration names what a casing takes.
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
	std::string casing = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1);
	casing.insert(casing.find("], \"edges\""), ",{ \"id\": \"primary_casing\", \"kind\": \"og.weapon.casing\", "
		"\"type\": \"brass\" }");
	casing.insert(casing.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_use_ammo\", \"output\": "
		"\"out\", \"to\": \"primary_casing\", \"input\": \"exec\" },");
	weaponGraphRuntimeClearAll();
	CHECK(weaponGraphRuntimeRegisterWeaponSourceJson(5, "base:test__weapon", casing.data(), (u32)casing.size(),
		secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, err, sizeof(err)) != 0);
	INFO(err);
	CHECK(std::string(err).find("og.weapon.casing needs type none, standard, reaper, rifle or shotgun, got brass")
		!= std::string::npos);
	weaponGraphRuntimeClearAll();

	// The seams: the flash and the casing read the attack's plan; the gun
	// loads the casing its function's press plans; the extractor writes both.
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("if (!plan->flash) {\n\t\thand->flashon = false;") != std::string::npos);
	CHECK(bondgun.find("if (funcflags & FUNCFLAG_NOMUZZLEFLASH) {") == std::string::npos);
	// PR 49 review: the selected function's press casing first, then a model
	// for every other casing either function can eject (the next case), and
	// each attack ejects its own type's.
	REQUIRE(bondgun.find("const s32 first = weaponFunctionCasing(weaponnum, selected);") != std::string::npos);
	REQUIRE(bondgun.find("bgunQueueModelLoad(player, g_CartFileNums[first], &player->gunctrl.cartmodeldef,")
		!= std::string::npos);
	REQUIRE(bondgun.find("memset(s_BgunCartModeldefs[g_Vars.currentplayernum], 0,") != std::string::npos);
	CHECK(bondgun.find("? weaponFunctionCasing(hand->gset.weaponnum, hand->gset.weaponfunc) : -1;") == std::string::npos);
	REQUIRE(bondgun.find("out->casing = (s32) def->ammos[out->ammo_slot]->casingeject + 1;") != std::string::npos);
	const std::string gunfx = readSource("src/game/gunfx.c");
	REQUIRE(gunfx.find("casingtype = weaponRulesCasingEject(bgunHandPlan(handnum));") != std::string::npos);
	REQUIRE(gunfx.find(": bgunGetCartModeldefForCasing(casingtype);") != std::string::npos);
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("\"use_ammo\", \"shot\", \"recoil\", \"casing\", \"flash\",") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldCasingNode(w, ids[i], plan, last);") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldFlashNode(w, ids[i], plan, last);") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramEdge(w, recoil, \"out\", casing, 0);\n"
		"\t\t\ts_emitHeldProgramEdge(w, casing, \"out\", flash, 0);") != std::string::npos);
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("weaponRuleCasingOf(weaponnum, out);") != std::string::npos);
}

// PR 49 review: one function can eject more than one casing (its program's
// casing nodes on the two branches of a state gate), while the press plan,
// walked with no states, holds one of them; the gun loads a model for every
// casing either function can eject (weaponFunctionCasings).
TEST_CASE("a function's casings are each casing node its program holds, a state gate's other branch included",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	// One node: its type; nothing for none; the function's own when it names
	// no type or one it does not know (which keeps the default).
	CHECK(weaponRulesCasingNodeMask("rifle", CASING_STANDARD) == (1u << CASING_RIFLE));
	CHECK(weaponRulesCasingNodeMask("shotgun", -1) == (1u << CASING_SHOTGUN));
	CHECK(weaponRulesCasingNodeMask("none", CASING_STANDARD) == 0u);
	CHECK(weaponRulesCasingNodeMask(nullptr, CASING_STANDARD) == (1u << CASING_STANDARD));
	CHECK(weaponRulesCasingNodeMask("brass", CASING_REAPER) == (1u << CASING_REAPER));
	CHECK(weaponRulesCasingNodeMask(nullptr, -1) == 0u);

	// Codex's case: after the shot a state gate sends the attack to a rifle
	// casing when the hand's "alt" state is set and to a shotgun casing when
	// it is not.
	weapon_rule_function_t record = recoilRecord();
	record.casing = CASING_STANDARD + 1;
	std::string json = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1,
		"\"kind\": \"og.weapon.hitscan\"");
	json.insert(json.find("], \"edges\""), ",{ \"id\": \"primary_alt\", \"kind\": \"gate.weapon.state_is\", "
		"\"key\": \"alt\", \"equals\": true },{ \"id\": \"primary_rifle\", \"kind\": \"og.weapon.casing\", "
		"\"type\": \"rifle\" },{ \"id\": \"primary_shell\", \"kind\": \"og.weapon.casing\", \"type\": \"shotgun\" }");
	json.insert(json.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_shot\", \"output\": \"out\", "
		"\"to\": \"primary_alt\", \"input\": \"exec\" },{ \"from\": \"primary_alt\", \"output\": \"pass\", "
		"\"to\": \"primary_rifle\", \"input\": \"exec\" },{ \"from\": \"primary_alt\", \"output\": \"fail\", "
		"\"to\": \"primary_shell\", \"input\": \"exec\" },");
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(), heldKnown, err,
		sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	int instance = 0;
	// The press plan's walk (weaponProgramPressPlan: a ready hand, no states)
	// takes the gate's fail branch.
	weapon_rule_walk_t press{};
	press.weaponnum = -1;
	weaponRulesReadyHand(&press.hand);
	press.function = &record;
	press.param_text = weaponGraphRuntimeHeldParamText;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, -1, "base:test__weapon",
		&instance, &press) == 1);
	CHECK(weaponRulesCasingEject(&press.plan) == CASING_SHOTGUN);
	// A hand with "alt" set ejects the other.
	weapon_rule_states_t states{};
	states.count = 1;
	std::strcpy(states.keys[0], "alt");
	states.values[0] = 1;
	weapon_rule_walk_t live{};
	live.weaponnum = -1;
	live.hand = weapon_rule_hand_t{ 1, 8, 8, 1, 1 };
	live.states = &states;
	live.function = &record;
	live.param_text = weaponGraphRuntimeHeldParamText;
	REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon",
		&instance, &live) == 1);
	CHECK(weaponRulesCasingEject(&live.plan) == CASING_RIFLE);
	// Every casing node counts, as weaponFunctionCasings visits them, and so
	// does the function's own: rifle, shotgun and standard.
	struct Visit {
		s32 native;
		u32 mask;
	} visit{ CASING_STANDARD, weaponRulesCasingNodeMask(nullptr, CASING_STANDARD) };
	REQUIRE(missionGraphProgramVisitModuleParams(program,
		[](const char *kind, const mission_graph_params_t *params, void *user) -> s32 {
			Visit *v = static_cast<Visit *>(user);
			char text[32];
			if (kind && std::string(kind) == weaponRulesModuleKind(WEAPON_MODULE_CASING)) {
				v->mask |= weaponRulesCasingNodeMask(weaponGraphRuntimeHeldParamText(-1, params,
					WEAPON_RULE_CASING_TYPE_PARAM, text, sizeof(text)), v->native);
			}
			return 1;
		}, &visit) == 1);
	CHECK(visit.mask == ((1u << CASING_RIFLE) | (1u << CASING_SHOTGUN) | (1u << CASING_STANDARD)));
	missionGraphProgramFree(program);

	// The seams: weapon_events.c visits every casing node the same way, and
	// the gun loads a model for each casing either function can eject and
	// finds an attack's by its type.
	const std::string events = readSource("port/src/weapon_events.c");
	REQUIRE(events.find("missionGraphProgramVisitModuleParams(program, weaponCasingVisit, &visit);")
		!= std::string::npos);
	REQUIRE(events.find("visit->mask |= weaponRulesCasingNodeMask(weaponGraphRuntimeHeldParamText(visit->weaponnum, "
		"params,") != std::string::npos);
	REQUIRE(events.find("visit.mask = weaponRulesCasingNodeMask(NULL, visit.native);") != std::string::npos);
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("casings = weaponFunctionCasings(weaponnum, FUNC_PRIMARY) | "
		"weaponFunctionCasings(weaponnum, FUNC_SECONDARY);") != std::string::npos);
	REQUIRE(bondgun.find("bgunQueueModelLoad(player, g_CartFileNums[type], &s_BgunCartModeldefs[p][type],")
		!= std::string::npos);
	REQUIRE(bondgun.find("if (casingtype < ARRAYCOUNT(g_CartFileNums) && s_BgunCartModeldefs[p][casingtype] != NULL) {")
		!= std::string::npos);
	REQUIRE(bondgun.find("|| bgunCartModeldefIsOther(carts, casing->modeldef)) {") != std::string::npos);
}

namespace {

// One audio catalog id for the walks below (weapon_events.c resolves them
// through catalogResolveAudio).
s32 shotSoundNumber(const char *catalog_id)
{
	return catalog_id && std::string(catalog_id) == "base:sfx_fire_shotgun" ? 0x8051 : -1;
}

} // namespace

// held-weapon-events.md slice 7b: a shot's sound and the frames its fire
// slot keeps it are its program's, else its function's own.
TEST_CASE("a shot's sound is its function's own unless its program's sound node names it",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	weapon_rule_function_t record = recoilRecord();
	record.casing = CASING_STANDARD + 1;
	record.shoot_sound = 0x8051;
	record.shot_duration = 6;
	weapon_rule_trigger_t native;
	weaponRulesNativeTrigger(&record, &native);
	CHECK(native.shot_sound == 0x8051);
	CHECK(native.shot_duration == 6);
	// The original's accessors read the held graph's sound for any function,
	// so the record's is the plan's whatever the function's type.
	weapon_rule_function_t thrown = record;
	thrown.functype = 0x0002;
	weapon_rule_trigger_t plan;
	weaponRulesNativeTrigger(&thrown, &plan);
	CHECK(plan.shot_sound == 0x8051);
	CHECK(plan.shot_duration == 6);
	weapon_rule_trigger_t zero{};
	CHECK(zero.shot_sound == 0);
	// Both are part of the attack's plan.
	plan = native;
	plan.shot_sound = 1;
	CHECK(weaponRulesTriggerEqual(&plan, &native) == 0);
	plan = native;
	plan.shot_duration = 7;
	CHECK(weaponRulesTriggerEqual(&plan, &native) == 0);

	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	Texts none;
	REQUIRE(weaponRulesApplySoundSettings(Texts::get, &none, shotSoundNumber, &record, &plan) == 1);
	CHECK(plan.shot_sound == 0x8051);
	CHECK(plan.shot_duration == 6);
	Texts quiet;
	quiet.values = { { "sound", "none" }, { "duration", "0" } };
	REQUIRE(weaponRulesApplySoundSettings(Texts::get, &quiet, shotSoundNumber, &record, &plan) == 1);
	CHECK(plan.shot_sound == 0);
	CHECK(plan.shot_duration == 0);
	weapon_rule_function_t silent = record;
	silent.shoot_sound = 0;
	silent.shot_duration = 0;
	Texts named;
	named.values = { { "sound", "base:sfx_fire_shotgun" }, { "duration", "255" } };
	REQUIRE(weaponRulesApplySoundSettings(Texts::get, &named, shotSoundNumber, &silent, &plan) == 1);
	CHECK(plan.shot_sound == 0x8051);
	CHECK(plan.shot_duration == 255);
	// A sound the catalog does not have, or no catalog, keeps the function's.
	Texts unknown;
	unknown.values = { { "sound", "base:sfx_nothing" } };
	CHECK(weaponRulesApplySoundSettings(Texts::get, &unknown, shotSoundNumber, &silent, &plan) == 0);
	CHECK(plan.shot_sound == 0);
	CHECK(weaponRulesApplySoundSettings(Texts::get, &named, nullptr, &silent, &plan) == 0);
	CHECK(plan.shot_sound == 0);
	CHECK(plan.shot_duration == 255);
	Texts wrong;
	wrong.values = { { "sound", "Bang!" }, { "duration", "256" } };
	CHECK(weaponRulesApplySoundSettings(Texts::get, &wrong, shotSoundNumber, &record, &plan) == 0);
	CHECK(plan.shot_sound == 0x8051);
	CHECK(plan.shot_duration == 6);
	const weapon_rule_setting_t *sound = nullptr;
	const weapon_rule_setting_t *duration = nullptr;
	for (s32 i = 0; weaponRulesSettingAt(i); i++) {
		const weapon_rule_setting_t *setting = weaponRulesSettingAt(i);
		if (setting->module == WEAPON_MODULE_SHOT_SOUND && std::string(setting->key) == "sound") sound = setting;
		if (setting->module == WEAPON_MODULE_SHOT_SOUND && std::string(setting->key) == "duration") duration = setting;
	}
	REQUIRE(sound != nullptr);
	REQUIRE(duration != nullptr);
	CHECK(weaponRulesSettingValid(sound, "none") == 1);
	CHECK(weaponRulesSettingValid(sound, "base:sfx_fire_shotgun") == 1);
	CHECK(weaponRulesSettingValid(sound, "Bang!") == 0);
	CHECK(weaponRulesSettingValid(duration, "255") == 1);
	CHECK(weaponRulesSettingValid(duration, "256") == 0);
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_SHOT_SOUND)) == "og.weapon.shot_sound");
	CHECK((WEAPON_MODULE_FEEL & WEAPON_MODULE_SHOT_SOUND) != 0);

	// The base wiring's chain (shot, recoil, casing, flash, sound), as the
	// extractor writes it, plans the original's.
	int instance = 0;
	std::string json = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1,
		"\"kind\": \"og.weapon.hitscan\"");
	json.insert(json.find("], \"edges\""), ",{ \"id\": \"primary_recoil\", \"kind\": \"og.weapon.recoil\"" +
		recoilNodeSettings(native) + " },{ \"id\": \"primary_casing\", \"kind\": \"og.weapon.casing\", \"type\": \"" +
		weaponRulesCasingName(native.casing) + "\" },{ \"id\": \"primary_flash\", \"kind\": \"og.weapon.muzzle_flash\", "
		"\"on\": true },{ \"id\": \"primary_sound\", \"kind\": \"og.weapon.shot_sound\", \"sound\": "
		"\"base:sfx_fire_shotgun\", \"duration\": 6 }");
	json.insert(json.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_shot\", \"output\": \"out\", "
		"\"to\": \"primary_recoil\", \"input\": \"exec\" },{ \"from\": \"primary_recoil\", \"output\": \"out\", "
		"\"to\": \"primary_casing\", \"input\": \"exec\" },{ \"from\": \"primary_casing\", \"output\": \"out\", "
		"\"to\": \"primary_flash\", \"input\": \"exec\" },{ \"from\": \"primary_flash\", \"output\": \"out\", "
		"\"to\": \"primary_sound\", \"input\": \"exec\" },");
	char err[256] = { 0 };
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(), heldKnown, err,
		sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	weapon_rule_states_t states{};
	const auto walked = [&](weapon_rule_item_fn sounds) {
		weapon_rule_walk_t walk{};
		walk.weaponnum = -1;
		walk.hand = weapon_rule_hand_t{ 1, 8, 8, 1, 1 };
		walk.states = &states;
		walk.function = &record;
		walk.param_text = weaponGraphRuntimeHeldParamText;
		walk.sound_number = sounds;
		REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon",
			&instance, &walk) == 1);
		return walk;
	};
	weapon_rule_walk_t walk = walked(shotSoundNumber);
	CHECK((walk.modules & WEAPON_MODULE_FEEL) == WEAPON_MODULE_FEEL);
	CHECK(weaponRulesTriggerEqual(&walk.plan, &native) == 1);
	// With no catalog the node keeps the function's own.
	walk = walked(nullptr);
	CHECK(weaponRulesTriggerEqual(&walk.plan, &native) == 1);
	missionGraphProgramFree(program);
	// A program that names no sound plans silence.
	std::string hushed = json;
	hushed.replace(hushed.find("\"sound\": \"base:sfx_fire_shotgun\""), std::strlen("\"sound\": \"base:sfx_fire_shotgun\""),
		"\"sound\": \"none\"");
	program = missionGraphProgramCompileWeapon(hushed.data(), hushed.size(), heldKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	walk = walked(shotSoundNumber);
	CHECK(walk.plan.shot_sound == 0);
	CHECK(walk.plan.shot_duration == 6);
	CHECK(weaponRulesTriggerEqual(&walk.plan, &native) == 0);
	missionGraphProgramFree(program);

	// Registration names what a sound takes, and a sound resolves in the
	// audio catalog when its weapon registers.
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
	const auto withSound = [&](const std::string &settings) {
		std::string text = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1);
		text.insert(text.find("], \"edges\""), ",{ \"id\": \"primary_sound\", \"kind\": \"og.weapon.shot_sound\"" +
			settings + " }");
		text.insert(text.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_use_ammo\", \"output\": "
			"\"out\", \"to\": \"primary_sound\", \"input\": \"exec\" },");
		return text;
	};
	const auto registers = [&](const std::string &primary) {
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(5, "base:test__weapon", primary.data(), (u32)primary.size(),
			secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, err,
			sizeof(err)) == 0;
	};
	weaponGraphRuntimeClearAll();
	for (const auto &bad : std::vector<std::pair<std::string, std::string>>{
			{ ", \"sound\": \"Bang!\"",
				"og.weapon.shot_sound needs sound none or an audio catalog id such as base:sfx_fire_shotgun, got Bang!" },
			{ ", \"duration\": 256", "og.weapon.shot_sound needs duration from 0 to 255, got 256" },
			{ ", \"sound\": \"base:sfx_fire_shotgun\"",
				"og.weapon.shot_sound cannot resolve sound base:sfx_fire_shotgun in the audio catalog" } }) {
		INFO(bad.first);
		CHECK_FALSE(registers(withSound(bad.first)));
		INFO(err);
		CHECK(std::string(err).find(bad.second) != std::string::npos);
	}
	testStubEffectAudio("base:sfx_fire_shotgun", 0, 0x8051);
	CHECK(registers(withSound(", \"sound\": \"base:sfx_fire_shotgun\", \"duration\": 6")));
	INFO(err);
	CHECK(registers(withSound(", \"sound\": \"none\"")));
	testStubEffectAudio(nullptr, 0, 0);
	weaponGraphRuntimeClearAll();

	// The seams: a player's shot plays its hand's plan's sound; a simulant's
	// and a guard's read their function's press plan through the original's
	// accessors; every walk resolves sounds; the extractor writes the node.
	const auto count = [](const std::string &text, const std::string &needle) {
		size_t n = 0;
		for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) n++;
		return n;
	};
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(bondgun.find("bgunPlanShotSound(hand, plan, &shotsound, &slotframes);") != std::string::npos);
	REQUIRE(bondgun.find("hand->allowshootframe = g_Vars.lvframe60 + slotframes;") != std::string::npos);
	REQUIRE(bondgun.find("*slotframes = gsetFireslotTicks(plan->shot_duration);") != std::string::npos);
	REQUIRE(bondgun.find("handle = sndStart(var80095200, shotsound, &hand->audiohandle2,") != std::string::npos);
	CHECK(bondgun.find("gsetGetSingleShootSound(&hand->gset), &hand->audiohandle") == std::string::npos);
	const std::string gset = readSource("src/game/game_0b0fd0.c");
	REQUIRE(gset.find("duration = weaponProgramShotDuration(gset->weaponnum, gset->weaponfunc, duration);")
		!= std::string::npos);
	REQUIRE(gset.find("sound = weaponProgramShotSound(gset->weaponnum, gset->weaponfunc, sound);") != std::string::npos);
	REQUIRE(gset.find("return gsetFireslotTicks(duration);") != std::string::npos);
	const std::string events = readSource("port/src/weapon_events.c");
	CHECK(count(events, "walk.sound_number = weaponSoundNumber;") == count(events, "walk.item_number = weaponItemNumber;"));
	CHECK(count(events, "walk.sound_number = weaponSoundNumber;") == 4);
	REQUIRE(events.find("weaponRuleSoundOf(weaponnum, func, held, out);") != std::string::npos);
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("\"casing\", \"flash\", \"sound\",") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldSoundNode(w, ids[i], plan, last);") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldProgramEdge(w, flash, \"out\", sound, 0);") != std::string::npos);
	REQUIRE(extractor.find("record.shoot_sound = sh->shootsound;") != std::string::npos);
}

namespace {

// Stand-ins for two gun command scripts and the catalog that names them
// (weapon_events.c resolves ids through the loader pool).
const int s_testFireAnimation = 1;
const int s_testPunchAnimation = 2;

const void *testAnimationRef(const char *catalog_id)
{
	if (!catalog_id) return nullptr;
	if (std::string(catalog_id) == "base:invanim_falcon2_shoot") return &s_testFireAnimation;
	if (std::string(catalog_id) == "base:invanim_punch") return &s_testPunchAnimation;
	return nullptr;
}

} // namespace

// held-weapon-events.md slice 7c: an attack's fire animation is its
// program's, else its function's own.
TEST_CASE("an attack's fire animation is its function's own unless its program's animation node names it",
	"[mission-graph][weapon-graph][held-weapon][weapon-rules]")
{
	weapon_rule_function_t record = recoilRecord();
	record.casing = CASING_STANDARD + 1;
	record.fire_animation = &s_testFireAnimation;
	weapon_rule_trigger_t native;
	weaponRulesNativeTrigger(&record, &native);
	CHECK(native.fire_animation == &s_testFireAnimation);
	weapon_rule_trigger_t plan = native;
	plan.fire_animation = &s_testPunchAnimation;
	CHECK(weaponRulesTriggerEqual(&plan, &native) == 0);
	weapon_rule_trigger_t zero{};
	CHECK(zero.fire_animation == nullptr);
	// An attack's: a shot's, a throw's or a strike's.
	CHECK(weaponRulesAnimationFits(0x0001) == 1);
	CHECK(weaponRulesAnimationFits(0x0101) == 1);
	CHECK(weaponRulesAnimationFits(0x0201) == 1);
	CHECK(weaponRulesAnimationFits(0x0002) == 1);
	CHECK(weaponRulesAnimationFits(0x0003) == 1);
	CHECK(weaponRulesAnimationFits(0x0004) == 0);
	CHECK(weaponRulesAnimationFits(0x0005) == 0);
	CHECK(std::string(weaponRulesModuleKind(WEAPON_MODULE_PLAY_ANIMATION)) == "og.weapon.play_animation");
	CHECK((weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED) & WEAPON_MODULE_PLAY_ANIMATION) != 0);
	CHECK((weaponRulesEventModules(MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED) & WEAPON_MODULE_PLAY_ANIMATION) == 0);
	CHECK((WEAPON_MODULE_FEEL & WEAPON_MODULE_PLAY_ANIMATION) == 0);

	struct Texts {
		std::map<std::string, std::string> values;
		static const char *get(const char *key, void *user)
		{
			const auto &values = static_cast<Texts *>(user)->values;
			const auto it = values.find(key);
			return it == values.end() ? nullptr : it->second.c_str();
		}
	};
	Texts none;
	REQUIRE(weaponRulesApplyAnimationSettings(Texts::get, &none, testAnimationRef, &record, &plan) == 1);
	CHECK(plan.fire_animation == &s_testFireAnimation);
	Texts off;
	off.values = { { "animation", "none" } };
	REQUIRE(weaponRulesApplyAnimationSettings(Texts::get, &off, testAnimationRef, &record, &plan) == 1);
	CHECK(plan.fire_animation == nullptr);
	Texts punch;
	punch.values = { { "animation", "base:invanim_punch" } };
	REQUIRE(weaponRulesApplyAnimationSettings(Texts::get, &punch, testAnimationRef, &record, &plan) == 1);
	CHECK(plan.fire_animation == &s_testPunchAnimation);
	// One the catalog does not have, or no catalog, keeps the function's.
	Texts unknown;
	unknown.values = { { "animation", "base:invanim_nothing" } };
	CHECK(weaponRulesApplyAnimationSettings(Texts::get, &unknown, testAnimationRef, &record, &plan) == 0);
	CHECK(plan.fire_animation == &s_testFireAnimation);
	CHECK(weaponRulesApplyAnimationSettings(Texts::get, &punch, nullptr, &record, &plan) == 0);
	CHECK(plan.fire_animation == &s_testFireAnimation);
	Texts wrong;
	wrong.values = { { "animation", "Fire!" } };
	CHECK(weaponRulesApplyAnimationSettings(Texts::get, &wrong, testAnimationRef, &record, &plan) == 0);
	CHECK(plan.fire_animation == &s_testFireAnimation);
	const weapon_rule_setting_t *animation = nullptr;
	for (s32 i = 0; weaponRulesSettingAt(i); i++) {
		const weapon_rule_setting_t *setting = weaponRulesSettingAt(i);
		if (setting->module == WEAPON_MODULE_PLAY_ANIMATION && std::string(setting->key) == "animation") {
			animation = setting;
		}
	}
	REQUIRE(animation != nullptr);
	CHECK(weaponRulesSettingValid(animation, "none") == 1);
	CHECK(weaponRulesSettingValid(animation, "base:invanim_punch") == 1);
	CHECK(weaponRulesSettingValid(animation, "Fire!") == 0);

	// The base wiring, as the extractor writes it, plans the original's: a
	// shot's full chain (shot, recoil, casing, flash, sound, animation) and a
	// strike's (the attack, the strike, the animation).
	char err[256] = { 0 };
	int instance = 0;
	weapon_rule_states_t states{};
	const auto walked = [&](const mission_graph_program_t *program, const weapon_rule_function_t *function) {
		weapon_rule_walk_t walk{};
		walk.weaponnum = -1;
		walk.hand = weapon_rule_hand_t{ 1, 8, 8, 1, 1 };
		walk.states = &states;
		walk.function = function;
		walk.param_text = weaponGraphRuntimeHeldParamText;
		walk.sound_number = shotSoundNumber;
		walk.animation_ref = testAnimationRef;
		REQUIRE(weaponRulesWalk(program, MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED, 0, "base:test__weapon",
			&instance, &walk) == 1);
		return walk;
	};
	record.shoot_sound = 0x8051;
	record.shot_duration = 6;
	weaponRulesNativeTrigger(&record, &native);
	std::string json = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1,
		"\"kind\": \"og.weapon.hitscan\"");
	json.insert(json.find("], \"edges\""), ",{ \"id\": \"primary_recoil\", \"kind\": \"og.weapon.recoil\"" +
		recoilNodeSettings(native) + " },{ \"id\": \"primary_casing\", \"kind\": \"og.weapon.casing\", \"type\": \"" +
		weaponRulesCasingName(native.casing) + "\" },{ \"id\": \"primary_flash\", \"kind\": \"og.weapon.muzzle_flash\", "
		"\"on\": true },{ \"id\": \"primary_sound\", \"kind\": \"og.weapon.shot_sound\", \"sound\": "
		"\"base:sfx_fire_shotgun\", \"duration\": 6 },{ \"id\": \"primary_animation\", \"kind\": "
		"\"og.weapon.play_animation\", \"animation\": \"base:invanim_falcon2_shoot\" }");
	json.insert(json.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_shot\", \"output\": \"out\", "
		"\"to\": \"primary_recoil\", \"input\": \"exec\" },{ \"from\": \"primary_recoil\", \"output\": \"out\", "
		"\"to\": \"primary_casing\", \"input\": \"exec\" },{ \"from\": \"primary_casing\", \"output\": \"out\", "
		"\"to\": \"primary_flash\", \"input\": \"exec\" },{ \"from\": \"primary_flash\", \"output\": \"out\", "
		"\"to\": \"primary_sound\", \"input\": \"exec\" },{ \"from\": \"primary_sound\", \"output\": \"out\", "
		"\"to\": \"primary_animation\", \"input\": \"exec\" },");
	mission_graph_program_t *program = missionGraphProgramCompileWeapon(json.data(), json.size(), heldKnown, err,
		sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	weapon_rule_walk_t walk = walked(program, &record);
	CHECK((walk.modules & WEAPON_MODULE_PRESENTATION) == WEAPON_MODULE_PRESENTATION);
	CHECK(walk.plan.fire_animation == &s_testFireAnimation);
	CHECK(weaponRulesTriggerEqual(&walk.plan, &native) == 1);
	missionGraphProgramFree(program);
	// A program that names none plays none.
	std::string still = json;
	still.replace(still.find("\"animation\": \"base:invanim_falcon2_shoot\""),
		std::strlen("\"animation\": \"base:invanim_falcon2_shoot\""), "\"animation\": \"none\"");
	program = missionGraphProgramCompileWeapon(still.data(), still.size(), heldKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	walk = walked(program, &record);
	CHECK(walk.plan.fire_animation == nullptr);
	CHECK(weaponRulesTriggerEqual(&walk.plan, &native) == 0);
	missionGraphProgramFree(program);
	// A strike's chain.
	weapon_rule_function_t strike{};
	strike.weaponnum = WEAPON_UNARMED;
	strike.func = FUNC_PRIMARY;
	strike.functype = 0x0003;
	strike.ammo_slot = -1;
	strike.fire_animation = &s_testPunchAnimation;
	weapon_rule_trigger_t strikeNative;
	weaponRulesNativeTrigger(&strike, &strikeNative);
	CHECK(strikeNative.fire_animation == &s_testPunchAnimation);
	std::string melee = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 0,
		"\"kind\": \"og.weapon.melee\"");
	melee.insert(melee.find("], \"edges\""), ",{ \"id\": \"primary_animation\", \"kind\": \"og.weapon.play_animation\", "
		"\"animation\": \"base:invanim_punch\" }");
	melee.insert(melee.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_shot\", \"output\": \"out\", "
		"\"to\": \"primary_animation\", \"input\": \"exec\" },");
	program = missionGraphProgramCompileWeapon(melee.data(), melee.size(), heldKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	walk = walked(program, &strike);
	CHECK((walk.modules & WEAPON_MODULE_PLAY_ANIMATION) != 0);
	CHECK(walk.plan.fire_animation == &s_testPunchAnimation);
	CHECK(weaponRulesTriggerEqual(&walk.plan, &strikeNative) == 1);
	missionGraphProgramFree(program);

	// Registration names what an animation takes and refuses one on a
	// function whose attack plays none.
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
	const auto withAnimation = [&](const std::string &settings) {
		std::string text = baseIdleProgram("primary", 1, "\"kind\": \"og.weapon.fire_once\"", 1);
		text.insert(text.find("], \"edges\""), ",{ \"id\": \"primary_animation\", \"kind\": \"og.weapon.play_animation\"" +
			settings + " }");
		text.insert(text.find("{ \"from\": \"primary_pressed\""), "{ \"from\": \"primary_use_ammo\", \"output\": "
			"\"out\", \"to\": \"primary_animation\", \"input\": \"exec\" },");
		return text;
	};
	const auto registers = [&](const std::string &primary) {
		err[0] = 0;
		return weaponGraphRuntimeRegisterWeaponSourceJson(5, "base:test__weapon", primary.data(), (u32)primary.size(),
			secondary.data(), (u32)secondary.size(), nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, err,
			sizeof(err)) == 0;
	};
	weaponGraphRuntimeClearAll();
	CHECK(registers(withAnimation(", \"animation\": \"base:invanim_falcon2_shoot\"")));
	INFO(err);
	CHECK(registers(withAnimation(", \"animation\": \"none\"")));
	CHECK_FALSE(registers(withAnimation(", \"animation\": \"Fire!\"")));
	CHECK(std::string(err).find("og.weapon.play_animation needs animation none or an animation catalog id such as "
		"base:invanim_falcon2_shoot, got Fire!") != std::string::npos);
	std::string special = withAnimation("");
	const std::string shootRecord = "\"kind\": \"fire.hitscan\", \"subgraph\": \"primary\", \"params\": { \"mode\": "
		"\"primary\", \"function_type\": \"shoot_single\", \"ammo_slot\": 0";
	REQUIRE(special.find(shootRecord) != std::string::npos);
	special.replace(special.find(shootRecord), shootRecord.size(), "\"kind\": \"special.weapon_state\", \"subgraph\": "
		"\"primary\", \"params\": { \"mode\": \"primary\", \"function_type\": \"special\", \"ammo_slot\": -1");
	CHECK_FALSE(registers(special));
	CHECK(std::string(err).find("og.weapon.play_animation fits only a shooting, throwing or melee function")
		!= std::string::npos);
	weaponGraphRuntimeClearAll();

	// The seams: the shot, the throw and the strike start the attack's
	// animation and wait on the same one; a generated mesh with no skeleton
	// plays none the function does not have; every walk resolves animations;
	// the extractor writes the node last in the attack's chain.
	const auto count = [](const std::string &text, const std::string &needle) {
		size_t n = 0;
		for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) n++;
		return n;
	};
	const std::string bondgun = readSource("src/game/bondgun.c");
	CHECK(count(bondgun, "bgunPlanFireAnimation(hand, ") == 3);
	CHECK(count(bondgun, "bgunStartAnimation(fireanim, handnum, hand);") == 3);
	CHECK(bondgun.find("func->base.fire_animation") == std::string::npos);
	CHECK(bondgun.find("if (func->fire_animation) {") == std::string::npos);
	REQUIRE(bondgun.find("&& (hand->gunmodel.definition == NULL || hand->gunmodel.definition->skel == NULL)")
		!= std::string::npos);
	REQUIRE(bondgun.find("out->fire_animation = func ? (const void *) func->fire_animation : NULL;") != std::string::npos);
	const std::string events = readSource("port/src/weapon_events.c");
	CHECK(count(events, "walk.animation_ref = weaponAnimationRef;") == 4);
	REQUIRE(events.find("loaderPoolFindAnimationByCatalogId(catalog_id)") != std::string::npos);
	const std::string runtime = readSource("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("!weaponRulesAnimationFits(check->functype)") != std::string::npos);
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("\"sound\", \"animation\",") != std::string::npos);
	REQUIRE(extractor.find("(i == 14 && !s_heldAnimationName(plan))") != std::string::npos);
	REQUIRE(extractor.find("s_emitHeldAnimationNode(w, ids[i], plan, last);") != std::string::npos);
	REQUIRE(extractor.find("record.fire_animation = weaponRulesAnimationFits(f->type)") != std::string::npos);
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
