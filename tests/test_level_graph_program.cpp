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

s32 runModule(const char *kind, const char *node_id, const mission_graph_params_t *params,
	const mission_graph_event_t *event, void *user)
{
	(void)params;
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
	REQUIRE(conformance.find("def validate_mission_graph_execution(label: str, graph: dict, level: bool = False,\n"
		"                                     rule_events: tuple[str, ...] | None = None) -> list[str]:")
		!= std::string::npos);
	REQUIRE(conformance.find("validate_mission_graph_execution(\n"
		"                                label, graph, level=True,") != std::string::npos);
}

namespace {

mission_graph_event_t objectEvent(s32 kind, const char *object)
{
	mission_graph_event_t event;
	std::memset(&event, 0, sizeof(event));
	event.kind = kind;
	event.objective_index = -1;
	event.object = object;
	event.tag = -1;
	event.object_kind = "door";
	event.player = -1;
	return event;
}

} // namespace

TEST_CASE("Level graph program: object events run on one object or on any",
	"[level][graph][program][objects]")
{
	const std::string json = R"({ "nodes": [
	{ "id": "vault_opened", "kind": "event.door.opened", "object": "setup_0042" },
	{ "id": "any_closed", "kind": "event.door.closed", "object": "any" },
	{ "id": "key_taken", "kind": "event.pickup.collected" },
	{ "id": "used", "kind": "event.object.used", "object": "setup_0007" },
	{ "id": "on_vault", "kind": "og.test.vault" },
	{ "id": "on_closed", "kind": "og.test.closed" },
	{ "id": "on_key", "kind": "og.test.key" },
	{ "id": "on_used", "kind": "og.test.used" } ],
  "edges": [
	{ "from": "vault_opened", "to": "on_vault", "input": "exec" },
	{ "from": "any_closed", "to": "on_closed", "input": "exec" },
	{ "from": "key_taken", "to": "on_key", "input": "exec" },
	{ "from": "used", "to": "on_used", "input": "exec" } ] })";
	std::string error;
	mission_graph_program_t *program = compileLevel(json, &error);
	INFO(error);
	REQUIRE(program != nullptr);
	REQUIRE(missionGraphProgramEventNodeCount(program, MISSION_GRAPH_EVENT_DOOR_OPENED) == 1);
	REQUIRE(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PICKUP_COLLECTED) == 1);

	struct Case {
		s32 kind;
		const char *object;
		std::vector<std::string> ran;
	};
	const Case cases[] = {
		{MISSION_GRAPH_EVENT_DOOR_OPENED, "setup_0042", {"on_vault"}},
		{MISSION_GRAPH_EVENT_DOOR_OPENED, "setup_0043", {}},
		{MISSION_GRAPH_EVENT_DOOR_CLOSED, "setup_0099", {"on_closed"}},
		{MISSION_GRAPH_EVENT_DOOR_CLOSED, "", {"on_closed"}},
		{MISSION_GRAPH_EVENT_PICKUP_COLLECTED, "setup_0001", {"on_key"}},
		{MISSION_GRAPH_EVENT_OBJECT_USED, "setup_0007", {"on_used"}},
		{MISSION_GRAPH_EVENT_OBJECT_USED, "", {}},
	};
	for (const Case &c : cases) {
		INFO("kind " << c.kind << " object " << c.object);
		const mission_graph_event_t event = objectEvent(c.kind, c.object);
		std::vector<std::string> ran;
		REQUIRE(missionGraphProgramFire(program, &event, runModule, &ran) == (s32)c.ran.size());
		REQUIRE(ran == c.ran);
	}
	// An object event needs its object (the empty string for none).
	mission_graph_event_t missing = objectEvent(MISSION_GRAPH_EVENT_DOOR_OPENED, nullptr);
	std::vector<std::string> ran;
	REQUIRE(missionGraphProgramFire(program, &missing, runModule, &ran) == -1);
	missionGraphProgramFree(program);

	// What the compiler refuses.
	REQUIRE(compileLevel(R"({ "nodes": [ { "id": "e", "kind": "event.door.opened", "objective": "x" } ] })",
		&error) == nullptr);
	REQUIRE(error.find("object event 'e' has no objective to filter") != std::string::npos);
	REQUIRE(compileLevel(R"({ "nodes": [ { "id": "e", "kind": "event.object.used", "object": 7 } ] })",
		&error) == nullptr);
	REQUIRE(error.find("object event 'e' object must be a string") != std::string::npos);
}

TEST_CASE("Level graph program: the native seams fire the object events, recorded per stage",
	"[level][graph][program][objects][static]")
{
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string pdmain = readSource("port/src/pdmain.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// The payload: the record id (the setup command index), the tag, the
	// type and the catalog id; fired on the host only.
	REQUIRE(propobj.find("snprintf(out->object, sizeof(out->object), \"setup_%04u\", (unsigned)cmdindex);")
		!= std::string::npos);
	REQUIRE(propobj.find("out->tag = objGetTagNum(obj);") != std::string::npos);
	REQUIRE(propobj.find("out->catalog_id = g_WeaponDataCatalogIds[weaponnum];") != std::string::npos);
	REQUIRE(propobj.find("\tif (g_NetMode == NETMODE_CLIENT || ev->type < 0) {") != std::string::npos);
	// The empty record id is the character '\0', not a literal NUL byte (a
	// collapsed escape made propobj.c a binary file to grep).
	REQUIRE(propobj.find("\tout->object[0] = '\\0';") != std::string::npos);
	REQUIRE(propobj.find('\0') == std::string::npos);

	// B-1259: the setup index comes from the object's own prop field. The
	// N64 read the prop pointer as the word cmd[5]; on the 64-bit port that
	// word is the padding before the 8-byte pointer, so it never matched.
	// Only object commands are compared.
	const std::string setuputils = readSource("src/game/setuputils.c");
	REQUIRE(setuputils.find("(struct prop *)cmd[5]") == std::string::npos);
	REQUIRE(setuputils.find("\t\t\tstruct defaultobj *obj = setupCmdObj(cmd);\n\n"
		"\t\t\tif (obj && obj->prop == prop) {\n\t\t\t\treturn cmdindex;") != std::string::npos);
	REQUIRE(setuputils.find("struct defaultobj *setupGetObjByCmdIndex(u32 cmdindex)\n{\n"
		"\treturn setupCmdObj(setupGetCmdByIndex(cmdindex));\n}") != std::string::npos);

	// Pickups after the give or the free; doors at every start; the use
	// button once handled.
	const size_t free_at = propobj.find("\t\tobjFree(obj, false, obj->hidden2 & OBJH2FLAG_CANREGEN);\n"
		"\t\tobjGraphEventFire(MISSION_GRAPH_EVENT_PICKUP_COLLECTED, &collected, g_Vars.currentplayernum);");
	REQUIRE(free_at != std::string::npos);
	REQUIRE(propobj.find("\t\t\tinvGiveProp(prop);\n\t\t}\n"
		"\t\tobjGraphEventFire(MISSION_GRAPH_EVENT_PICKUP_COLLECTED, &collected, g_Vars.currentplayernum);")
		!= std::string::npos);
	REQUIRE(propobj.find("objGraphEventFire(MISSION_GRAPH_EVENT_DOOR_OPENED, &opened, -1);") != std::string::npos);
	REQUIRE(propobj.find("objGraphEventFire(MISSION_GRAPH_EVENT_DOOR_CLOSED, &closed, -1);") != std::string::npos);
	REQUIRE(propobj.find("objGraphEventFire(MISSION_GRAPH_EVENT_OBJECT_USED, &used, g_Vars.currentplayernum);")
		!= std::string::npos);

	// The runtime sends each to the level program and the mission program,
	// logs the first of each kind and the totals once per stage.
	REQUIRE(runtime.find("s32 scenarioSourceGraphObjectEvent(s32 kind, const char *object, s32 tag, s32 objtype,")
		!= std::string::npos);
	REQUIRE(runtime.find("level_ran = missionGraphProgramFire(s_ActiveScenarioGraphs.level_program, &event,")
		!= std::string::npos);
	REQUIRE(runtime.find("mission_ran = missionGraphProgramFire(s_ActiveScenarioGraphs.mission_program, &event,")
		!= std::string::npos);
	REQUIRE(runtime.find("\"SCENARIO.GRAPH: object events '%s' pickup.collected=%d door.opened=%d door.closed=%d object.used=%d glass.broken=%d pickup.touched=%d door.use=%d door.approach=%d lift.called=%d lift.idle=%d lift.arrived=%d glass.damaged=%d rules.compared=%d rules.differ=%d reason=%s backend=graph.level.runner\"")
		!= std::string::npos);
	REQUIRE(runtime.find("static void s_resetActiveScenarioGraphs(void)\n{\n\ts_reportObjectEvents(\"next_stage\");")
		!= std::string::npos);
	REQUIRE(pdmain.find("\tscenarioSourceGraphObjectEventsReport(\"stage_end\");") != std::string::npos);
	// The payload's kind is the objects.json kind of the object's type.
	REQUIRE(runtime.find("static const char *s_setupObjTypeToKind(u8 type)") != std::string::npos);

	REQUIRE(conformance.find("MISSION_GRAPH_OBJECT_EVENTS = {\"event.pickup.collected\", \"event.door.opened\",")
		!= std::string::npos);
}

namespace {

// Slice 3: a module table that knows two native gates and records each
// module with the parameters it was given.
s32 knownWithGates(const char *kind)
{
	return std::strncmp(kind, "og.test.", 8) == 0 || std::strcmp(kind, "gate.test.param") == 0;
}

struct Recorded {
	std::vector<std::string> ran;
	std::vector<std::string> objects;
};

s32 runWithGates(const char *kind, const char *node_id, const mission_graph_params_t *params,
	const mission_graph_event_t *event, void *user)
{
	(void)event;
	Recorded *recorded = static_cast<Recorded *>(user);
	if (std::strcmp(kind, "gate.test.param") == 0) {
		// Passes when its "pass" parameter is the boolean true.
		const char *pass = missionGraphParamText(params, "pass");
		return pass && std::strcmp(pass, "true") == 0 ? 1 : 0;
	}
	recorded->ran.push_back(node_id);
	const char *object = missionGraphParamText(params, "object");
	recorded->objects.push_back(object ? object : "-");
	return 1;
}

} // namespace

TEST_CASE("Level graph program: modules get their node's parameters, native gates decide",
	"[level][graph][program][objects]")
{
	const std::string json = R"({ "nodes": [
	{ "id": "touched", "kind": "event.pickup.touched", "object": "setup_0042" },
	{ "id": "yes", "kind": "gate.test.param", "pass": true },
	{ "id": "no", "kind": "gate.test.param", "pass": false },
	{ "id": "open", "kind": "og.test.open", "object": "setup_0007", "bits": 4, "note": "x" },
	{ "id": "refuse", "kind": "og.test.refuse" },
	{ "id": "never", "kind": "og.test.never" } ],
  "edges": [
	{ "from": "touched", "to": "yes", "input": "exec" },
	{ "from": "touched", "to": "no", "input": "exec" },
	{ "from": "yes", "to": "open", "input": "exec", "output": "pass" },
	{ "from": "yes", "to": "never", "input": "exec", "output": "fail" },
	{ "from": "no", "to": "refuse", "input": "exec", "output": "fail" },
	{ "from": "no", "to": "never", "input": "exec", "output": "pass" } ] })";
	char err[256] = {};
	mission_graph_program_t *program = missionGraphProgramCompileLevel(json.c_str(), json.size(),
		knownWithGates, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	REQUIRE(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PICKUP_TOUCHED) == 1);
	REQUIRE(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_DOOR_USE) == 0);

	mission_graph_event_t event;
	std::memset(&event, 0, sizeof(event));
	event.kind = MISSION_GRAPH_EVENT_PICKUP_TOUCHED;
	event.objective_index = -1;
	event.object = "setup_0042";
	event.tag = -1;
	event.player = 0;
	Recorded recorded;
	REQUIRE(missionGraphProgramFire(program, &event, runWithGates, &recorded) == 2);
	REQUIRE(recorded.ran == std::vector<std::string>({"open", "refuse"}));
	REQUIRE(recorded.objects == std::vector<std::string>({"setup_0007", "-"}));

	// Another object's touch runs nothing.
	event.object = "setup_0043";
	Recorded other;
	REQUIRE(missionGraphProgramFire(program, &event, runWithGates, &other) == 0);
	missionGraphProgramFree(program);

	// A gate the table does not know is refused when it compiles.
	const std::string unknown = R"({ "nodes": [ { "id": "g", "kind": "gate.test.nope" } ] })";
	REQUIRE(missionGraphProgramCompileLevel(unknown.c_str(), unknown.size(), knownWithGates, err,
		sizeof(err)) == nullptr);
	REQUIRE(std::string(err).find("gate kind 'gate.test.nope' is not executable") != std::string::npos);

	// The parameters as text: strings as they are, whole numbers in decimal,
	// booleans as true or false; id and kind are not parameters.
	REQUIRE(missionGraphParamText(nullptr, "object") == nullptr);
}

TEST_CASE("Level graph program: the object modules and the native gates are wired",
	"[level][graph][program][objects][static]")
{
	const std::string modules = readSource("port/src/mission_graph_modules.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// The table: the door and pickup modules and the gates (slice 3); the
	// queries' refusals are gone (slice 4).
	for (const char *row : { "{ \"og.door.open\", s_runDoorOpen },",
			"{ \"og.door.close\", s_runDoorClose },", "{ \"og.door.lock\", s_runDoorLock },",
			"{ \"og.door.unlock\", s_runDoorUnlock },", "{ \"og.pickup.give\", s_runPickupGive },",
			"{ \"gate.object.kind\", s_gateObjectKind },",
			"{ \"gate.player.has_item\", s_gatePlayerHasItem, kHasItemParam, ASSET_WEAPON }," }) {
		INFO(row);
		REQUIRE(modules.find(row) != std::string::npos);
	}
	REQUIRE(modules.find("og.pickup.refuse") == std::string::npos);
	REQUIRE(modules.find("og.door.refuse") == std::string::npos);
	// An object module finds its object by the record id, the setup index.
	REQUIRE(modules.find("if (!ref || sscanf(ref, \"setup_%u%c\", &index, &tail) != 1) {") != std::string::npos);
	REQUIRE(modules.find("obj = setupGetObjByCmdIndex(index);") != std::string::npos);
	// The doors act as the AI's door commands do.
	REQUIRE(modules.find("\tif (!doorCallLift(door->base.prop, false)) {\n\t\tdoorsRequestMode(door, DOORMODE_OPENING);")
		!= std::string::npos);
	REQUIRE(modules.find("\tdoor->keyflags |= (u8)bits;") != std::string::npos);
	REQUIRE(modules.find("\tdoor->keyflags &= (u8)~bits;") != std::string::npos);

	// The runtime cuts a chain of object events past its depth.
	REQUIRE(runtime.find("#define SCENARIO_OBJECT_EVENT_MAX_DEPTH 4") != std::string::npos);
	REQUIRE(runtime.find("scenarioSourceGraphObjectQuery") == std::string::npos);

	REQUIRE(conformance.find("MISSION_GRAPH_OBJECT_MODULES = {\"og.door.open\", \"og.door.close\", \"og.door.lock\",")
		!= std::string::npos);
	REQUIRE(conformance.find("\"gate.object.kind\", \"gate.player.has_item\",") != std::string::npos);
}

namespace {

// Slice 4: the rule wiring as the extractor writes it into every level graph
// (port/src/romextract_pdarena.c), with a module table whose rule gates the
// test decides and whose modules it records.
const char *const kRuleWiring = R"({ "nodes": [
	{ "id": "scenario.load", "kind": "event.scenario.load" },
	{ "id": "object.rules.pickup.touched", "kind": "event.pickup.touched" },
	{ "id": "object.rules.pickup.native_rules", "kind": "gate.pickup.native_rules" },
	{ "id": "object.rules.pickup.collect", "kind": "og.pickup.collect" },
	{ "id": "object.rules.door.use", "kind": "event.door.use" },
	{ "id": "object.rules.door.use_unlocked", "kind": "gate.door.native_unlocked" },
	{ "id": "object.rules.door.activate", "kind": "og.door.activate" },
	{ "id": "object.rules.door.locked_message", "kind": "og.door.locked_message" },
	{ "id": "object.rules.door.approach", "kind": "event.door.approach" },
	{ "id": "object.rules.door.approach_unlocked", "kind": "gate.door.native_unlocked" },
	{ "id": "object.rules.door.activate_automatic", "kind": "og.door.activate_automatic" },
	{ "id": "object.rules.lift.called", "kind": "event.lift.called" },
	{ "id": "object.rules.lift.called_vacant", "kind": "gate.lift.native_vacant" },
	{ "id": "object.rules.lift.go_to_stop", "kind": "og.lift.go_to_stop" },
	{ "id": "object.rules.lift.idle", "kind": "event.lift.idle" },
	{ "id": "object.rules.lift.idle_ready", "kind": "gate.lift.native_ready" },
	{ "id": "object.rules.lift.go_to_next_stop", "kind": "og.lift.go_to_next_stop" },
	{ "id": "object.rules.lift.arrived", "kind": "event.lift.arrived" },
	{ "id": "object.rules.lift.arrival_door_unlocked", "kind": "gate.lift.native_door_unlocked" },
	{ "id": "object.rules.lift.open_door", "kind": "og.lift.open_door" },
	{ "id": "object.rules.glass.damaged", "kind": "event.glass.damaged" },
	{ "id": "object.rules.glass.damaged_broken", "kind": "gate.glass.native_broken" },
	{ "id": "object.rules.glass.break", "kind": "og.glass.break" },
	{ "id": "source.scene", "kind": "scenario.scene.source", "file": "scene.glb" } ],
  "links": [ { "from": "scenario.load", "to": "source.scene" } ],
  "edges": [
	{ "from": "object.rules.pickup.touched", "to": "object.rules.pickup.native_rules", "input": "exec", "output": "out" },
	{ "from": "object.rules.pickup.native_rules", "to": "object.rules.pickup.collect", "input": "exec", "output": "pass" },
	{ "from": "object.rules.door.use", "to": "object.rules.door.use_unlocked", "input": "exec", "output": "out" },
	{ "from": "object.rules.door.use_unlocked", "to": "object.rules.door.activate", "input": "exec", "output": "pass" },
	{ "from": "object.rules.door.use_unlocked", "to": "object.rules.door.locked_message", "input": "exec", "output": "fail" },
	{ "from": "object.rules.door.approach", "to": "object.rules.door.approach_unlocked", "input": "exec", "output": "out" },
	{ "from": "object.rules.door.approach_unlocked", "to": "object.rules.door.activate_automatic", "input": "exec", "output": "pass" },
	{ "from": "object.rules.lift.called", "to": "object.rules.lift.called_vacant", "input": "exec", "output": "out" },
	{ "from": "object.rules.lift.called_vacant", "to": "object.rules.lift.go_to_stop", "input": "exec", "output": "pass" },
	{ "from": "object.rules.lift.idle", "to": "object.rules.lift.idle_ready", "input": "exec", "output": "out" },
	{ "from": "object.rules.lift.idle_ready", "to": "object.rules.lift.go_to_next_stop", "input": "exec", "output": "pass" },
	{ "from": "object.rules.lift.arrived", "to": "object.rules.lift.arrival_door_unlocked", "input": "exec", "output": "out" },
	{ "from": "object.rules.lift.arrival_door_unlocked", "to": "object.rules.lift.open_door", "input": "exec", "output": "pass" },
	{ "from": "object.rules.glass.damaged", "to": "object.rules.glass.damaged_broken", "input": "exec", "output": "out" },
	{ "from": "object.rules.glass.damaged_broken", "to": "object.rules.glass.break", "input": "exec", "output": "pass" } ] })";

s32 knownRules(const char *kind)
{
	for (const char *known : { "gate.pickup.native_rules", "og.pickup.collect", "gate.door.native_unlocked",
			"og.door.activate", "og.door.locked_message", "og.door.activate_automatic",
			"gate.lift.native_vacant", "og.lift.go_to_stop", "gate.lift.native_ready",
			"og.lift.go_to_next_stop", "gate.lift.native_door_unlocked", "og.lift.open_door",
			"gate.glass.native_broken", "og.glass.break" }) {
		if (std::strcmp(kind, known) == 0) return 1;
	}
	return 0;
}

struct RuleFake {
	bool allow = true;    // gate.pickup.native_rules
	bool unlocked = true; // gate.door.native_unlocked
	std::vector<std::string> ran;
};

s32 runRules(const char *kind, const char *node_id, const mission_graph_params_t *params,
	const mission_graph_event_t *event, void *user)
{
	(void)node_id;
	(void)params;
	(void)event;
	RuleFake *fake = static_cast<RuleFake *>(user);
	if (std::strcmp(kind, "gate.pickup.native_rules") == 0) return fake->allow ? 1 : 0;
	if (std::strcmp(kind, "gate.door.native_unlocked") == 0) return fake->unlocked ? 1 : 0;
	if (std::strncmp(kind, "gate.lift.", 10) == 0 || std::strcmp(kind, "gate.glass.native_broken") == 0) {
		return fake->allow ? 1 : 0;
	}
	fake->ran.push_back(kind);
	return 1;
}

std::vector<std::string> fireRule(const mission_graph_program_t *program, s32 kind, bool allow,
	bool unlocked)
{
	mission_graph_event_t event;
	std::memset(&event, 0, sizeof(event));
	event.kind = kind;
	event.objective_index = -1;
	event.object = "setup_0042";
	event.tag = -1;
	event.player = 0;
	RuleFake fake;
	fake.allow = allow;
	fake.unlocked = unlocked;
	const s32 ran = missionGraphProgramFire(program, &event, runRules, &fake);
	REQUIRE(ran == static_cast<s32>(fake.ran.size()));
	return fake.ran;
}

} // namespace

TEST_CASE("Level graph program: the level's pickup and door rules are its program",
	"[level][graph][program][objects][rules]")
{
	char err[256] = {};
	mission_graph_program_t *program = missionGraphProgramCompileLevel(kRuleWiring,
		std::strlen(kRuleWiring), knownRules, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	REQUIRE(missionGraphProgramNodeCount(program) == 23);
	REQUIRE(missionGraphProgramExecEdgeCount(program) == 15);
	REQUIRE(missionGraphProgramModuleNodeCount(program) == 8);
	for (s32 kind : { MISSION_GRAPH_EVENT_PICKUP_TOUCHED, MISSION_GRAPH_EVENT_DOOR_USE,
			MISSION_GRAPH_EVENT_DOOR_APPROACH, MISSION_GRAPH_EVENT_LIFT_CALLED,
			MISSION_GRAPH_EVENT_LIFT_IDLE, MISSION_GRAPH_EVENT_LIFT_ARRIVED,
			MISSION_GRAPH_EVENT_GLASS_DAMAGED }) {
		REQUIRE(missionGraphEventIsObjectRule(kind) == 1);
		REQUIRE(missionGraphProgramHandlesEvent(program, kind) == 1);
	}
	REQUIRE(missionGraphEventIsObjectRule(MISSION_GRAPH_EVENT_PICKUP_COLLECTED) == 0);

	// A touched pickup is collected when the original rules allow it.
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_PICKUP_TOUCHED, true, true)
		== std::vector<std::string>({ "og.pickup.collect" }));
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_PICKUP_TOUCHED, false, true).empty());
	// A used door activates when unlocked, else shows the locked refusal.
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_DOOR_USE, true, true)
		== std::vector<std::string>({ "og.door.activate" }));
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_DOOR_USE, true, false)
		== std::vector<std::string>({ "og.door.locked_message" }));
	// An approached automatic door opens when unlocked, else stays.
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_DOOR_APPROACH, true, true)
		== std::vector<std::string>({ "og.door.activate_automatic" }));
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_DOOR_APPROACH, true, false).empty());
	// Slice 5: a called lift goes to the door's stop when vacant; an idle
	// lift moves on when its stop is ready; an arriving lift opens an
	// unlocked door; damaged glass breaks at its limit. Each stays put when
	// its gate fails.
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_LIFT_CALLED, true, true)
		== std::vector<std::string>({ "og.lift.go_to_stop" }));
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_LIFT_IDLE, true, true)
		== std::vector<std::string>({ "og.lift.go_to_next_stop" }));
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_LIFT_ARRIVED, true, true)
		== std::vector<std::string>({ "og.lift.open_door" }));
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_GLASS_DAMAGED, true, true)
		== std::vector<std::string>({ "og.glass.break" }));
	for (s32 kind : { MISSION_GRAPH_EVENT_LIFT_CALLED, MISSION_GRAPH_EVENT_LIFT_IDLE,
			MISSION_GRAPH_EVENT_LIFT_ARRIVED, MISSION_GRAPH_EVENT_GLASS_DAMAGED }) {
		REQUIRE(fireRule(program, kind, false, true).empty());
	}
	// A record event reaches none of the rule modules.
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_PICKUP_COLLECTED, true, true).empty());
	REQUIRE(fireRule(program, MISSION_GRAPH_EVENT_GLASS_BROKEN, true, true).empty());
	REQUIRE(missionGraphEventIsObjectRule(MISSION_GRAPH_EVENT_GLASS_BROKEN) == 0);
	missionGraphProgramFree(program);

	// A mission graph does not take the rule events.
	const std::string mission = R"({ "nodes": [
	{ "id": "touched", "kind": "event.pickup.touched" },
	{ "id": "collect", "kind": "og.pickup.collect" } ],
  "edges": [ { "from": "touched", "to": "collect", "input": "exec" } ] })";
	REQUIRE(missionGraphProgramCompile(mission.c_str(), mission.size(), knownRules, err, sizeof(err))
		== nullptr);
	REQUIRE(std::string(err).find("rule event 'touched' (event.pickup.touched) belongs to the level graph")
		!= std::string::npos);
}

TEST_CASE("Level graph program: the rule wiring is written, required and decided at the seams",
	"[level][graph][program][objects][rules][static]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string packer = readSource("port/src/modpack_pdmod.c");
	const std::string builder = readSource("tools/build_typed_pdxxx_examples.py");
	const std::string modules = readSource("port/src/mission_graph_modules.c");
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");
	const std::string campaign = readSource("port/src/autocampaign.c");
	const std::string release = readSource("tools/smoke-verify/tests/auto_campaign_release.json");

	// The extractor, the mod packer's template and the example builder write
	// the same nodes and execution edges (kRuleWiring above).
	for (const std::string *source : { &extractor, &packer, &builder }) {
		REQUIRE(source->find(R"({ \"id\": \"object.rules.pickup.touched\", \"kind\": \"event.pickup.touched\" })")
			!= std::string::npos);
		REQUIRE(source->find(R"({ \"id\": \"object.rules.door.activate_automatic\", \"kind\": \"og.door.activate_automatic\" })")
			!= std::string::npos);
		REQUIRE(source->find(R"({ \"from\": \"object.rules.door.use_unlocked\", \"to\": \"object.rules.door.locked_message\", \"input\": \"exec\", \"output\": \"fail\" })")
			!= std::string::npos);
		REQUIRE(source->find(R"({ \"from\": \"object.rules.door.approach_unlocked\", \"to\": \"object.rules.door.activate_automatic\", \"input\": \"exec\", \"output\": \"pass\" })")
			!= std::string::npos);
	}
	// Slice 5: the lift and glass wiring is written too, and an archive
	// without it is stale.
	for (const std::string *source : { &extractor, &packer, &builder }) {
		REQUIRE(source->find(R"({ \"id\": \"object.rules.lift.called\", \"kind\": \"event.lift.called\" })")
			!= std::string::npos);
		REQUIRE(source->find(R"({ \"from\": \"object.rules.glass.damaged_broken\", \"to\": \"object.rules.glass.break\", \"input\": \"exec\", \"output\": \"pass\" })")
			!= std::string::npos);
	}
	REQUIRE(extractor.find("s_existingArchiveEntryContains(check, \"level.graph.json\",\n"
		"\t\t\t\"\\\"object.rules.glass.break\\\"\") &&") != std::string::npos);
	for (const char *row : { "{ \"gate.lift.native_vacant\", s_gateLiftNativeVacant },",
			"{ \"og.lift.go_to_stop\", s_runLiftGoToStop },",
			"{ \"gate.lift.native_ready\", s_gateLiftNativeReady },",
			"{ \"og.lift.go_to_next_stop\", s_runLiftGoToNextStop },",
			"{ \"gate.lift.native_door_unlocked\", s_gateLiftNativeDoorUnlocked },",
			"{ \"og.lift.open_door\", s_runLiftOpenDoor },",
			"{ \"gate.glass.native_broken\", s_gateGlassNativeBroken },",
			"{ \"og.glass.break\", s_runGlassBreak }," }) {
		INFO(row);
		REQUIRE(modules.find(row) != std::string::npos);
	}
	// The seams: a lift door's call, the idle lift, the arrival and glass
	// damage go to the level program on a scenario-source stage.
	REQUIRE(propobj.find("\t\t\t\t\t} else if (scenarioSourceGraphObjectRuleHandled(MISSION_GRAPH_EVENT_LIFT_CALLED)) {\n"
		"\t\t\t\t\t\t/* object-events.md slice 5: the level program decides. */\n"
		"\t\t\t\t\t\tliftCalled(link->lift, link->stopnum);") != std::string::npos);
	REQUIRE(propobj.find("\t\t\t\t&& scenarioSourceGraphObjectRuleHandled(MISSION_GRAPH_EVENT_LIFT_IDLE)) {\n"
		"\t\t\t/* object-events.md slice 5: the level program decides. */\n"
		"\t\t\tliftIdled(prop);\n\t\t\treturn;") != std::string::npos);
	REQUIRE(propobj.find("\t\t\t\t\t\t&& scenarioSourceGraphObjectRuleHandled(MISSION_GRAPH_EVENT_LIFT_ARRIVED)) {\n"
		"\t\t\t\t\t/* object-events.md slice 5: the level program decides. */\n"
		"\t\t\t\t\tliftArrived(prop);") != std::string::npos);
	REQUIRE(propobj.find("\t\t\t\t\t&& scenarioSourceGraphObjectRuleHandled(MISSION_GRAPH_EVENT_GLASS_DAMAGED)) {\n"
		"\t\t\t\t/* object-events.md slice 5: the level program decides. */\n"
		"\t\t\t\tglassDamaged(obj);") != std::string::npos);
	// Each seam keeps the game's own rule behind it, for a level whose
	// program does not handle the event (one authored before slice 5).
	REQUIRE(propobj.find("\t\t\t\t\t\tliftCalled(link->lift, link->stopnum);\n"
		"\t\t\t\t\t} else if (liftIsVacant((struct liftobj *)link->lift->obj)) {") != std::string::npos);
	REQUIRE(propobj.find("\t\t\t\t\tliftArrived(prop);\n"
		"\t\t\t\t} else if (door && door->keyflags == 0) {") != std::string::npos);
	REQUIRE(propobj.find("\t\t\t\tglassDamaged(obj);\n"
		"\t\t\t} else if (obj->damage >= obj->maxdamage) {") != std::string::npos);
	REQUIRE(propobj.find("\tobjGraphEventFire(MISSION_GRAPH_EVENT_GLASS_BROKEN, &broken, -1);")
		!= std::string::npos);
	for (const char *kind : { "MISSION_GRAPH_EVENT_LIFT_CALLED", "MISSION_GRAPH_EVENT_LIFT_IDLE",
			"MISSION_GRAPH_EVENT_LIFT_ARRIVED", "MISSION_GRAPH_EVENT_GLASS_DAMAGED" }) {
		INFO(kind);
		REQUIRE(propobj.find(std::string("\tscenarioSourceGraphObjectRuleCompared(") + kind) != std::string::npos);
		REQUIRE(runtime.find(std::string("\t\t{ ") + kind + ", \"event.") != std::string::npos);
	}
	// B-1260: an objects.json weapon pickup names its weapons as
	// setup.fields.json does (the catalog, then the game's weapon table).
	REQUIRE(extractor.find("\t\t\t\tweapon_ref = s_nonnullCatalogId(\n"
		"\t\t\t\t\ts_setupWeaponCatalogIdByRuntime(w->weaponnum));") != std::string::npos);
	REQUIRE(extractor.find("catalogWeaponIdByRuntimeWeaponNum(w->weaponnum)") == std::string::npos);
	// An archive without the wiring is stale for the extractor.
	REQUIRE(extractor.find("s_existingArchiveEntryContains(check, \"level.graph.json\",\n"
		"\t\t\t\"\\\"object.rules.door.activate_automatic\\\"\") &&") != std::string::npos);

	// The rule gates and modules, each only inside its own rule events.
	for (const char *row : { "{ \"gate.pickup.native_rules\", s_gatePickupNativeRules },",
			"{ \"og.pickup.collect\", s_runPickupCollect },",
			"{ \"gate.door.native_unlocked\", s_gateDoorNativeUnlocked },",
			"{ \"og.door.activate\", s_runDoorActivate },",
			"{ \"og.door.locked_message\", s_runDoorLockedMessage },",
			"{ \"og.door.activate_automatic\", s_runDoorActivateAutomatic }," }) {
		INFO(row);
		REQUIRE(modules.find(row) != std::string::npos);
	}
	REQUIRE(modules.find("\tif (!s_Rule || !s_Rule->prop || (event->kind != kind && event->kind != other)) {")
		!= std::string::npos);
	REQUIRE(modules.find("\t\trule->tickop = propPickupByPlayer(rule->prop, true);") != std::string::npos);
	REQUIRE(modules.find("\treturn s_ruleDoor(rule) && doorIsUnlocked(rule->user, rule->prop) ? 1 : 0;")
		!= std::string::npos);

	// The seams: on a scenario-source stage the level program decides, after
	// the native decision is computed for the parity probe.
	REQUIRE(propobj.find("\tif (scenarioSourceGraphObjectRulesActive()) {\n"
		"\t\treturn objPickupIsTouching(prop) ? objPickupTouched(prop) : TICKOP_NONE;\n\t}") != std::string::npos);
	REQUIRE(propobj.find("\tif (!objPickupRulesAllow(prop) || !objPickupIsTouching(prop)) {\n"
		"\t\treturn TICKOP_NONE;\n\t}\n\treturn propPickupByPlayer(prop, true);") != std::string::npos);
	REQUIRE(propobj.find("\tif (g_NetMode != NETMODE_CLIENT && scenarioSourceGraphObjectRulesActive()) {")
		!= std::string::npos);
	REQUIRE(propobj.find("\t\t\t\t\tif (graphrules) {\n\t\t\t\t\t\tdoorApproached(doorprop);\n"
		"\t\t\t\t\t} else {\n\t\t\t\t\t\tdoorOpenAutomatic(door);") != std::string::npos);
	REQUIRE(propobj.find("objGraphQueryRefuses") == std::string::npos);
	for (const char *kind : { "MISSION_GRAPH_EVENT_PICKUP_TOUCHED", "MISSION_GRAPH_EVENT_DOOR_USE",
			"MISSION_GRAPH_EVENT_DOOR_APPROACH" }) {
		INFO(kind);
		REQUIRE(propobj.find(std::string("\tscenarioSourceGraphObjectRuleCompared(") + kind) != std::string::npos);
	}

	// Levels authored before slice 5 still load: the activation check
	// requires the pickup and door rules only and notes unwired lift and
	// glass rules, which the game's own rules then decide; conformance holds
	// a level of an older kind to the pickup and door rules.
	{
		const size_t required = runtime.find("static s32 s_levelProgramHandlesRules(");
		const size_t noted = runtime.find("static void s_levelProgramNoteUnwiredRules(");
		REQUIRE(required != std::string::npos);
		REQUIRE(noted != std::string::npos);
		REQUIRE(required < noted);
		const std::string requiredBlock = runtime.substr(required, noted - required);
		CHECK(requiredBlock.find("MISSION_GRAPH_EVENT_PICKUP_TOUCHED") != std::string::npos);
		CHECK(requiredBlock.find("MISSION_GRAPH_EVENT_DOOR_APPROACH") != std::string::npos);
		CHECK(requiredBlock.find("MISSION_GRAPH_EVENT_LIFT_CALLED") == std::string::npos);
		CHECK(requiredBlock.find("MISSION_GRAPH_EVENT_GLASS_DAMAGED") == std::string::npos);
		REQUIRE(runtime.find("\ts_levelProgramNoteUnwiredRules(s_ActiveScenarioGraphs.level_program, scenario->id);")
			!= std::string::npos);
		REQUIRE(runtime.find("s32 scenarioSourceGraphObjectRuleHandled(s32 kind)\n{\n"
			"\treturn scenarioSourceGraphObjectRulesActive()\n"
			"\t\t&& missionGraphProgramHandlesEvent(s_ActiveScenarioGraphs.level_program, kind);\n}")
			!= std::string::npos);
		REQUIRE(conformance.find("ARENA_SCENARIO_GRAPH_CACHE_KINDS_STILL_LOADED = (") != std::string::npos);
		REQUIRE(conformance.find("LEVEL_RULE_EVENTS_BEFORE_LIFTGLASS = (\"event.pickup.touched\", \"event.door.use\",")
			!= std::string::npos);
		REQUIRE(conformance.find("and not descriptor_value(text, \"catalog_id\").lower().startswith(\"base:\"))")
			!= std::string::npos);
	}

	// The runtime refuses a level program without the wiring, fires the rule
	// events into the level program only, and counts the probe.
	REQUIRE(runtime.find("\t\t\t|| !s_levelProgramHandlesRules(s_ActiveScenarioGraphs.level_program,")
		!= std::string::npos);
	REQUIRE(runtime.find("\"it handles no %s: the level's pickup and door rules are not wired\"")
		!= std::string::npos);
	REQUIRE(runtime.find("\tran = missionGraphProgramFire(s_ActiveScenarioGraphs.level_program, &event,\n"
		"\t\tmissionGraphModuleRun, NULL);") != std::string::npos);
	REQUIRE(runtime.find("\"SCENARIO.GRAPH: object rule differs: event.%s '%s' object=%s native=%s graph=%s backend=graph.level.runner\"")
		!= std::string::npos);
	REQUIRE(conformance.find("errors.append(f\"{where} handles no {rule}: the level's pickup and door rules \"")
		!= std::string::npos);

	// The campaign runner fails a run on a difference; its fixtures pin it.
	REQUIRE(campaign.find("ACLOG(\"object rule parity stage_id='%s' compared=%d differ=%d\", stage_id, compared, differ);")
		!= std::string::npos);
	REQUIRE(campaign.find("\t\tif (probeObjectRuleParity(mission->stage_id) < 0) {") != std::string::npos);
	REQUIRE(release.find("\"object rule parity: \",") != std::string::npos);
	REQUIRE(release.find("object rule parity stage_id='base:[^']+' compared=[0-9]+ differ=0\", \"min\": 17, \"max\": 17")
		!= std::string::npos);
}

TEST_CASE("Grid layout changes retain the same executable level graph", "[grid-layout-apply]")
{
	const auto graph = [](const std::string &scenario, const std::string &object) {
		return R"({"scenario":")" + scenario + R"(","nodes":[{"id":"scenario.global.settings",)"
			R"("kind":"scenario.global.settings.source","scenario":")" + scenario
			+ R"("},{"id":"pickup","kind":"og.pickup.give","object":")" + object + R"("}],"edges":[]})";
	};
	const auto old_graph = graph("mod:scenario_old", "setup_0000");
	const auto new_graph = graph("mod:scenario_extended", "setup_0000");
	const auto new_action = graph("mod:scenario_extended", "setup_0001");
	CHECK(missionGraphLevelLayoutCompatible(old_graph.data(), old_graph.size(), "mod:scenario_old",
		new_graph.data(), new_graph.size(), "mod:scenario_extended"));
	CHECK_FALSE(missionGraphLevelLayoutCompatible(old_graph.data(), old_graph.size(), "mod:scenario_old",
		new_action.data(), new_action.size(), "mod:scenario_extended"));
	CHECK_FALSE(missionGraphLevelLayoutCompatible(old_graph.data(), old_graph.size(), "mod:scenario_old",
		new_graph.data(), new_graph.size(), "mod:another_scenario"));
}
