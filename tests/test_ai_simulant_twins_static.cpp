/*
 * tests/test_ai_simulant_twins_static.cpp -- AI lists as graphs
 * (context/designs/modding/ai-list-graphs.md): the two simulant commands that
 * had no graph twin, if_bot_respawning (0x0176) and mp_init_simulants
 * (0x0185), now have one, so every AI command but the interpreter's own
 * control flow runs through the level graph on a scenario-source stage.
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
	// The checkout may use CRLF; the pins below are written with LF.
	std::string text = out.str();
	std::string lf;
	lf.reserve(text.size());
	for (char c : text) {
		if (c != '\r') {
			lf.push_back(c);
		}
	}
	return lf;
}

std::string functionBody(const std::string &text, const std::string &signature)
{
	const size_t start = text.find(signature);
	REQUIRE(start != std::string::npos);
	const size_t end = text.find("\n}\n", start);
	REQUIRE(end != std::string::npos);
	return text.substr(start, end - start);
}

} // namespace

TEST_CASE("AI simulant twins: the handlers call the graph twins and keep today's play",
          "[ai][graph][simulant]")
{
	const std::string handlers = readSource("src/game/mplayer/mpaicommands.c");
	const std::string header = readSource("port/include/scenario_source_runtime.h");

	REQUIRE(handlers.find("#include \"scenario_source_runtime.h\"") != std::string::npos);
	REQUIRE(header.find("s32 scenarioSourceAiGraphExecuteIfBotRespawning(struct chrdata *chr, s32 label);")
		!= std::string::npos);
	REQUIRE(header.find("s32 scenarioSourceAiGraphExecuteMpInitSimulants(void);") != std::string::npos);

	// mp_init_simulants stays on its command while the wave is incomplete:
	// it ends the tick without moving (B-1255; returning false without
	// moving would be skipped by the B-949 guard), and goes on once the
	// wave is complete, on the twin's path and the native one.
	const std::string init = functionBody(handlers, "bool aiMpInitSimulants(void)");
	const size_t call = init.find("s32 graph_result = scenarioSourceAiGraphExecuteMpInitSimulants();");
	const size_t stay = init.find("if (graph_result == 2\n\t\t\t|| (graph_result == 0 && botSpawnAll() != BOT_SPAWN_WAVE_COMPLETE)) {");
	const size_t stop = init.find("return true;\n\t}");
	const size_t on = init.find("g_Vars.aioffset += 2;\n\treturn false;");
	REQUIRE(call != std::string::npos);
	REQUIRE(stay != std::string::npos);
	REQUIRE(stop != std::string::npos);
	REQUIRE(on != std::string::npos);
	REQUIRE(call < stay);
	REQUIRE(stay < stop);
	REQUIRE(stop < on);

	// if_bot_respawning: the twin first; B-1254, the native path returns on
	// a chr without an aibot instead of reading through it.
	const std::string resp = functionBody(handlers, "bool aiIfBotRespawning(void)");
	const size_t twin = resp.find("if (scenarioSourceAiGraphExecuteIfBotRespawning(g_Vars.chrdata, cmd[2])) {\n\t\treturn false;\n\t}");
	const size_t guard = resp.find("if (!g_Vars.chrdata || !g_Vars.chrdata->aibot) {\n\t\tg_Vars.aioffset += 3;\n\t\treturn false;\n\t}");
	const size_t read = resp.find("aibot = g_Vars.chrdata->aibot;");
	REQUIRE(twin != std::string::npos);
	REQUIRE(guard != std::string::npos);
	REQUIRE(read != std::string::npos);
	REQUIRE(twin < guard);
	REQUIRE(guard < read);
	REQUIRE(resp.find("//return false;") == std::string::npos);
}

TEST_CASE("AI simulant twins: the runtime requires both module nodes and runs them",
          "[ai][graph][simulant]")
{
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");

	REQUIRE(runtime.find("#include \"game/bot.h\"") != std::string::npos);
	REQUIRE(runtime.find("\"\\\"kind\\\": \\\"scenario.ai.condition.if_bot_respawning\\\"\"") != std::string::npos);
	REQUIRE(runtime.find("\"\\\"kind\\\": \\\"scenario.ai.action.mp_init_simulants\\\"\"") != std::string::npos);
	REQUIRE(runtime.find("if (level_ai_if_bot_respawning_node_count != 1 ||\n"
		"\t\t\tlevel_ai_mp_init_simulants_node_count != 1) {\n"
		"\t\ts_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,\n"
		"\t\t\t\"missing executable AI simulant source nodes\");") != std::string::npos);
	REQUIRE(runtime.find("\"SCENARIO.GRAPH: AI simulant commands '%s' if_bot_respawning=%d mp_init_simulants=%d "
		"backend=graph.ai.simulant+ai/ailists.json\"") != std::string::npos);

	const std::string resp = functionBody(runtime,
		"s32 scenarioSourceAiGraphExecuteIfBotRespawning(struct chrdata *chr, s32 label)");
	REQUIRE(resp.find("if (!s_ActiveScenarioGraphs.level_graph_active) {\n\t\treturn 0;\n\t}") != std::string::npos);
	REQUIRE(resp.find("level_ai_if_bot_respawning_node_count != 1") != std::string::npos);
	// No chr, or a chr that is not a simulant, is false; the flag clears
	// when the branch is taken; the false path skips the 3-byte command.
	REQUIRE(resp.find("branch_taken = chr && chr->aibot && chr->aibot->respawning == true;") != std::string::npos);
	REQUIRE(resp.find("chr->aibot->respawning = false;") != std::string::npos);
	REQUIRE(resp.find("s_aiGraphApplyBranch(branch_taken, label, 3);") != std::string::npos);
	REQUIRE(resp.find("\"SCENARIO.GRAPH: AI condition if_bot_respawning chr=%d simulant=%d label=%d branch=%d "
		"source=%s backend=graph.ai.condition.simulant+ai/ailists.json\"") != std::string::npos);

	const std::string init = functionBody(runtime, "s32 scenarioSourceAiGraphExecuteMpInitSimulants(void)");
	REQUIRE(init.find("level_ai_mp_init_simulants_node_count != 1") != std::string::npos);
	REQUIRE(init.find("wave = botSpawnAll();") != std::string::npos);
	REQUIRE(init.find("\"SCENARIO.GRAPH: AI action mp_init_simulants wave=%s "
		"source=%s backend=graph.ai.action.simulant+ai/ailists.json\"") != std::string::npos);
	// The twin never moves the cursor: 1 goes on, 2 stays.
	REQUIRE(init.find("aioffset") == std::string::npos);
	REQUIRE(init.find("return wave == BOT_SPAWN_WAVE_COMPLETE ? 1 : 2;") != std::string::npos);
}

TEST_CASE("AI simulant twins: every level graph carries both module nodes",
          "[ai][graph][simulant][extract]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");
	const std::string examples = readSource("tools/build_typed_pdxxx_examples.py");

	for (const std::string *text : { &extractor, &examples }) {
		REQUIRE(text->find("\\\"id\\\": \\\"scenario.ai.condition.if_bot_respawning\\\", "
			"\\\"kind\\\": \\\"scenario.ai.condition.if_bot_respawning\\\", \\\"source\\\": \\\"ai/ailists.json\\\", "
			"\\\"target\\\": \\\"chr.aibot.respawning\\\", \\\"opcode\\\": \\\"0x0176\\\"") != std::string::npos);
		REQUIRE(text->find("\\\"id\\\": \\\"scenario.ai.action.mp_init_simulants\\\", "
			"\\\"kind\\\": \\\"scenario.ai.action.mp_init_simulants\\\", \\\"source\\\": \\\"ai/ailists.json\\\", "
			"\\\"target\\\": \\\"simulants.spawn_wave\\\", \\\"opcode\\\": \\\"0x0185\\\"") != std::string::npos);
		REQUIRE(text->find("{ \\\"from\\\": \\\"scenario.ai.lists\\\", \\\"to\\\": \\\"scenario.ai.condition.if_bot_respawning\\\" }")
			!= std::string::npos);
		REQUIRE(text->find("{ \\\"from\\\": \\\"scenario.ai.lists\\\", \\\"to\\\": \\\"scenario.ai.action.mp_init_simulants\\\" }")
			!= std::string::npos);
	}
	// The fast cache treats an archive without them as stale.
	REQUIRE(extractor.find("s_existingArchiveEntryContains(check, \"level.graph.json\",\n"
		"\t\t\t\"scenario.ai.condition.if_bot_respawning\") &&") != std::string::npos);
	REQUIRE(extractor.find("s_existingArchiveEntryContains(check, \"level.graph.json\",\n"
		"\t\t\t\"scenario.ai.action.mp_init_simulants\") &&") != std::string::npos);
	// SP-86: the scenario kind moved with the extractor's output.
	REQUIRE(extractor.find("_aiedges_simtwins") != std::string::npos);
	REQUIRE(conformance.find("_aiedges_simtwins") != std::string::npos);
	REQUIRE(examples.find("_aiedges_simtwins") != std::string::npos);
	// Conformance requires both nodes and links in every level graph.
	REQUIRE(conformance.find("\"scenario.ai.condition.if_bot_respawning\": \"AI if_bot_respawning condition\"")
		!= std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.mp_init_simulants\": \"AI mp_init_simulants action\"")
		!= std::string::npos);
	REQUIRE(conformance.find("(\"scenario.ai.lists\", \"scenario.ai.condition.if_bot_respawning\"),")
		!= std::string::npos);
	REQUIRE(conformance.find("(\"scenario.ai.lists\", \"scenario.ai.action.mp_init_simulants\"),")
		!= std::string::npos);
}

TEST_CASE("AI simulant twins: the smoke harness kills a simulant through the damage path",
	"[ai][graph][simulants][static]")
{
	const std::string harness = readSource("port/src/smoke_harness.c");
	const std::string schema = readSource("port/src/smoke_fixture_schema.c");

	// kill_simulant is a fixture event with the common fields only.
	REQUIRE(schema.find("\"kill_player\", \"kill_simulant\",") != std::string::npos);
	REQUIRE(schema.find("|| !strcmp(type, \"kill_simulant\")\n") != std::string::npos);
	// use_door (object-events.md slice 4) is one too.
	REQUIRE(schema.find("\"kill_player\", \"kill_simulant\", \"use_door\",") != std::string::npos);
	REQUIRE(schema.find("|| !strcmp(type, \"use_door\")) {") != std::string::npos);
	REQUIRE(harness.find("if (!strcmp(type_str, \"use_door\")) {") != std::string::npos);
	REQUIRE(harness.find("\t\t\t\tpropdoorInteract(door->base.prop);") != std::string::npos);
	REQUIRE(harness.find("if (!strcmp(type_str, \"kill_simulant\")) {") != std::string::npos);
	// The first live simulant takes lethal damage the way play deals it, so
	// it dies, runs its dead list and respawns.
	REQUIRE(harness.find("if (chr != NULL && chr->prop != NULL && chr->aibot != NULL && !chrIsDead(chr)) {")
		!= std::string::npos);
	REQUIRE(harness.find("chrDamageByMisc(victim, 1000.0f, &vector, NULL, NULL);") != std::string::npos);
	REQUIRE(harness.find("\"SMOKE.KILL_SIMULANT: slot=%d chr=%d died=%d at_ms=%d\"") != std::string::npos);
}
