// Combat Simulator mode programs (context/designs/modding/gamemode-graphs.md,
// slice 2): the runner's mode domain, the mode module table against the
// original g_MpScenarios[], the base programs the extractor writes, and
// registration's refusals.
#include "catch.hpp"

#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "constants.h"
#include "mission_graph_runner.h"
#include "modarchive.h"
#include "mode_rules.h"
}

namespace {

// One member of a typed archive, as text.
std::string archiveMemberText(const char *archive_path, const char *member)
{
	mod_archive_t *archive = modArchiveOpen(archive_path);
	REQUIRE(archive != nullptr);
	const s32 idx = modArchiveFindEntry(archive, member);
	u32 size = 0;
	void *bytes = idx >= 0 ? modArchiveExtractAlloc(archive, idx, &size) : nullptr;
	modArchiveClose(archive);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

std::string readRepoFile(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

struct CompiledProgram {
	mission_graph_program_t *program = nullptr;
	std::string err;
	~CompiledProgram() { missionGraphProgramFree(program); }
};

void compileMode(CompiledProgram &out, const std::string &json)
{
	char err[320] = {};
	out.program = missionGraphProgramCompileMode(json.data(), json.size(), modeRulesModuleKnown,
		err, sizeof(err));
	out.err = err;
}

std::string checkError(const mission_graph_program_t *program, s32 mode)
{
	char err[320] = {};
	return modeRulesCheckProgram(program, mode, err, sizeof(err)) ? std::string() : std::string(err);
}

std::string baseProgram(s32 mode, const char *catalog_id)
{
	char json[8192];
	const s32 len = modeRulesBaseProgramJson(mode, catalog_id, json, sizeof(json));
	REQUIRE(len > 0);
	return std::string(json, static_cast<size_t>(len));
}

// A mode graph with one event wired to one module.
std::string oneEdge(const char *event_kind, const char *module_kind)
{
	return std::string("{\"schema\":\"pd.gamemode_graph.v1\",\"catalog_id\":\"mod:m\",\"nodes\":[")
		+ "{\"id\":\"e\",\"kind\":\"" + event_kind + "\"},"
		+ "{\"id\":\"m\",\"kind\":\"" + module_kind + "\"},"
		+ "{\"id\":\"sp\",\"kind\":\"event.mode.spawn\"},"
		+ "{\"id\":\"sd\",\"kind\":\"og.mode.default.spawn\"}],"
		+ "\"edges\":[{\"from\":\"e\",\"to\":\"m\",\"input\":\"exec\"},"
		+ "{\"from\":\"sp\",\"to\":\"sd\",\"input\":\"exec\"}]}";
}

// The callbacks of struct mpscenario in declaration order and the seam each
// one is (-1: not a rule: the options dialog and the unused slot).
const std::vector<std::pair<const char *, s32>> kScenarioFields = {
	{ "optionsdialog", -1 },
	{ "initfunc", MODE_SEAM_INIT },
	{ "numpropsfunc", MODE_SEAM_NUM_PROPS },
	{ "initpropsfunc", MODE_SEAM_INIT_PROPS },
	{ "tickfunc", MODE_SEAM_TICK },
	{ "tickchrfunc", MODE_SEAM_TICK_CHR },
	{ "hudfunc", MODE_SEAM_HUD },
	{ "calcscorefunc", MODE_SEAM_SCORE },
	{ "radarextrafunc", MODE_SEAM_RADAR_EXTRA },
	{ "radarchrfunc", MODE_SEAM_RADAR_CHR },
	{ "highlightpropfunc", MODE_SEAM_HIGHLIGHT_PROP },
	{ "spawnfunc", MODE_SEAM_SPAWN },
	{ "maxteamsfunc", MODE_SEAM_MAX_TEAMS },
	{ "isroomhighlightedfunc", MODE_SEAM_ROOM_LIGHTS },
	{ "highlightroomfunc", MODE_SEAM_ROOM_LIGHTS },
	{ "unk3c", -1 },
	{ "readsavefunc", MODE_SEAM_SAVE },
	{ "writesavefunc", MODE_SEAM_SAVE },
	// Slice 4: the mode code that sat outside the table.
	{ "resetfunc", MODE_SEAM_STAGE_RESET },
	{ "intropadfunc", MODE_SEAM_INTRO_PAD },
	{ "pickuptokenfunc", MODE_SEAM_TOKEN_PICKED_UP },
	{ "droppedtokenfunc", MODE_SEAM_TOKEN_DROPPED },
	{ "activatedpropfunc", MODE_SEAM_PROP_ACTIVATED },
	{ "playerdiedfunc", MODE_SEAM_PLAYER_DIED },
};

// The rows of g_MpScenarios[], by their designators, in mode order.
const char *const kScenarioRows[MODE_RULES_MODE_COUNT] = {
	"[MPSCENARIO_COMBAT] = {", "[MPSCENARIO_HOLDTHEBRIEFCASE] = {", "[MPSCENARIO_HACKERCENTRAL] = {",
	"[MPSCENARIO_POPACAP] = {", "[MPSCENARIO_KINGOFTHEHILL] = {", "[MPSCENARIO_CAPTURETHECASE] = {",
};

} // namespace

TEST_CASE("mode programs compile in the runner's mode domain and only there",
	"[gamemode-graphs][slice2][runner]")
{
	CompiledProgram ok;
	compileMode(ok, oneEdge("event.mode.tick", "og.mode.htb.tick"));
	INFO(ok.err);
	REQUIRE(ok.program != nullptr);
	REQUIRE(missionGraphProgramNodeCount(ok.program) == 4);
	REQUIRE(missionGraphProgramExecEdgeCount(ok.program) == 2);
	REQUIRE(missionGraphProgramModuleNodeCount(ok.program) == 2);
	REQUIRE(missionGraphProgramHandlesEvent(ok.program, MISSION_GRAPH_EVENT_MODE_TICK) == 1);
	REQUIRE(missionGraphProgramEventReachesModule(ok.program, MISSION_GRAPH_EVENT_MODE_TICK,
		"og.mode.htb.tick") == 1);

	// The twelve events, in slice 1's order, and nothing else is a mode event.
	REQUIRE(modeRulesEventAt(0) == MISSION_GRAPH_EVENT_MODE_CHOSEN);
	REQUIRE(modeRulesEventAt(11) == MISSION_GRAPH_EVENT_MODE_PLAYER_DIED);
	REQUIRE(modeRulesEventAt(12) == -1);
	for (s32 i = 0; modeRulesEventAt(i) >= 0; i++) {
		const s32 kind = modeRulesEventAt(i);
		REQUIRE(kind == MISSION_GRAPH_EVENT_MODE_CHOSEN + i);
		REQUIRE(missionGraphEventIsMode(kind) == 1);
		const char *name = modeRulesEventName(kind);
		REQUIRE(name != nullptr);
		// Every name the table knows compiles as that event in the runner.
		CompiledProgram one;
		compileMode(one, std::string("{\"nodes\":[{\"id\":\"e\",\"kind\":\"") + name +
			"\"},{\"id\":\"m\",\"kind\":\"og.mode.default.spawn\"}],\"edges\":[{\"from\":\"e\","
			"\"to\":\"m\",\"input\":\"exec\"}]}");
		INFO(name << ": " << one.err);
		REQUIRE(one.program != nullptr);
		REQUIRE(missionGraphProgramHandlesEvent(one.program, kind) == 1);
	}
	REQUIRE(missionGraphEventIsMode(MISSION_GRAPH_EVENT_MODE_CHOSEN - 1) == 0);
	REQUIRE(missionGraphEventIsMode(MISSION_GRAPH_EVENT_MODE_PLAYER_DIED + 1) == 0);
	REQUIRE(missionGraphEventIsMode(MISSION_GRAPH_EVENT_PROJECTILE_ARMED) == 0);

	// A mode program has the mode events and no other.
	for (const char *other : { "event.objective.status_changed", "event.mission.completed",
			"event.volume.entered", "event.pickup.touched", "event.projectile.landed",
			"event.mode.unknown" }) {
		CompiledProgram refused;
		compileMode(refused, oneEdge(other, "og.mode.htb.tick"));
		INFO(other);
		REQUIRE(refused.program == nullptr);
		REQUIRE(refused.err.find("is not a mode event") != std::string::npos);
	}

	// A mode event filters nothing.
	CompiledProgram filtered;
	compileMode(filtered, "{\"nodes\":[{\"id\":\"e\",\"kind\":\"event.mode.tick\",\"objective\":\"any\"}]}");
	REQUIRE(filtered.program == nullptr);
	REQUIRE(filtered.err.find("has no objective to filter") != std::string::npos);

	// The mode modules are a mode program's: another table does not know them.
	CompiledProgram unknown;
	compileMode(unknown, oneEdge("event.mode.tick", "og.mode.htb.fly"));
	REQUIRE(unknown.program == nullptr);
	REQUIRE(unknown.err.find("does not exist") != std::string::npos);

	// No other program has the mode events.
	const std::string with_mode_event =
		"{\"nodes\":[{\"id\":\"e\",\"kind\":\"event.mode.tick\"}]}";
	char err[320] = {};
	REQUIRE(missionGraphProgramCompile(with_mode_event.data(), with_mode_event.size(), nullptr,
		err, sizeof(err)) == nullptr);
	REQUIRE(std::string(err).find("belongs to a mode archive's mode.graph.json") != std::string::npos);
	REQUIRE(missionGraphProgramCompileLevel(with_mode_event.data(), with_mode_event.size(), nullptr,
		err, sizeof(err)) == nullptr);
	REQUIRE(std::string(err).find("belongs to a mode archive's mode.graph.json") != std::string::npos);
	REQUIRE(missionGraphProgramCompileProjectile(with_mode_event.data(), with_mode_event.size(),
		nullptr, err, sizeof(err)) == nullptr);
	REQUIRE(std::string(err).find("belongs to a mode archive's mode.graph.json") != std::string::npos);

	// A mode graph's other nodes are data, skipped and not counted.
	CompiledProgram data;
	compileMode(data, "{\"nodes\":[{\"id\":\"note\",\"kind\":\"mode.note\"},"
		"{\"id\":\"e\",\"kind\":\"event.mode.spawn\"},{\"id\":\"m\",\"kind\":\"og.mode.default.spawn\"}],"
		"\"edges\":[{\"from\":\"note\",\"to\":\"e\"},{\"from\":\"e\",\"to\":\"m\",\"input\":\"exec\"}]}");
	INFO(data.err);
	REQUIRE(data.program != nullptr);
	REQUIRE(missionGraphProgramNodeCount(data.program) == 2);
}

TEST_CASE("the mode module table is the original mode table",
	"[gamemode-graphs][slice2][table][static]")
{
	const std::string scenarios = readRepoFile("src/game/mplayer/scenarios.c");
	REQUIRE(!scenarios.empty());

	// struct mpscenario's callbacks, in the order the table's rows fill them.
	const size_t decl = scenarios.find("struct mpscenario {");
	REQUIRE(decl != std::string::npos);
	const size_t decl_end = scenarios.find("};", decl);
	const std::string body = scenarios.substr(decl, decl_end - decl);
	size_t from = 0;
	for (const auto &field : kScenarioFields) {
		const size_t at = body.find(field.first, from);
		INFO(field.first);
		REQUIRE(at != std::string::npos);
		from = at + std::string(field.first).size();
	}

	// The six rows of g_MpScenarios[], by their designators and fields.
	const size_t table = scenarios.find("struct mpscenario g_MpScenarios[] = {");
	REQUIRE(table != std::string::npos);
	const size_t table_end = scenarios.find("\n};", table);
	REQUIRE(table_end != std::string::npos);

	for (s32 mode = 0; mode < MODE_RULES_MODE_COUNT; mode++) {
		INFO(kScenarioRows[mode]);
		const size_t row = scenarios.find(kScenarioRows[mode], table);
		REQUIRE(row != std::string::npos);
		REQUIRE(row < table_end);
		const size_t row_end = scenarios.find("\n\t}", row);
		REQUIRE(row_end != std::string::npos);
		const std::string fields = scenarios.substr(row, row_end - row);
		REQUIRE(fields.find("NULL") == std::string::npos);
		bool has[MODE_SEAM_COUNT] = {};
		bool both_or_none[MODE_SEAM_COUNT][2] = {};
		for (size_t i = 0; i < kScenarioFields.size(); i++) {
			const bool set = fields.find(std::string("\t.") + kScenarioFields[i].first + " = ") !=
				std::string::npos;
			const s32 seam = kScenarioFields[i].second;
			if (seam < 0) continue;
			if (seam == MODE_SEAM_ROOM_LIGHTS || seam == MODE_SEAM_SAVE) {
				both_or_none[seam][std::string(kScenarioFields[i].first).find("isroom") == 0
					|| std::string(kScenarioFields[i].first).find("read") == 0 ? 0 : 1] = set;
			}
			has[seam] = has[seam] || set;
		}
		for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
			INFO("mode " << mode << " seam " << modeRulesSeamName(seam));
			REQUIRE((modeRulesNativeHas(mode, seam) != 0) == has[seam]);
			if (seam == MODE_SEAM_ROOM_LIGHTS || seam == MODE_SEAM_SAVE) {
				// The pair is one seam: a mode has both callbacks or neither.
				REQUIRE(both_or_none[seam][0] == both_or_none[seam][1]);
			}
			const char *kind = modeRulesModuleKind(mode, seam);
			if (kind) {
				REQUIRE(std::string(kind) == std::string("og.mode.") + modeRulesModeName(mode) + "." +
					modeRulesSeamName(seam));
			}
		}
	}

	// Combat has no modules of its own; the defaults are the dispatchers'.
	for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
		REQUIRE(modeRulesModuleKind(MPSCENARIO_COMBAT, seam) == nullptr);
	}
	REQUIRE(std::string(modeRulesModuleKind(MODE_RULES_DEFAULT, MODE_SEAM_SCORE)) ==
		"og.mode.default.score");
	REQUIRE(std::string(modeRulesModuleKind(MODE_RULES_DEFAULT, MODE_SEAM_SPAWN)) ==
		"og.mode.default.spawn");
	REQUIRE(modeRulesModuleCount() == 64);

	// Every module's seam is chosen by one event.
	for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
		REQUIRE(missionGraphEventIsMode(modeRulesSeamEvent(seam)) == 1);
	}
	REQUIRE(modeRulesSeamEvent(MODE_SEAM_TICK) == MISSION_GRAPH_EVENT_MODE_TICK);
	REQUIRE(modeRulesSeamEvent(MODE_SEAM_HUD) == MISSION_GRAPH_EVENT_MODE_CHOSEN);
	REQUIRE(modeRulesSeamEvent(MODE_SEAM_INIT_PROPS) == MISSION_GRAPH_EVENT_MODE_PROPS_INIT);
	REQUIRE(modeRulesSeamEvent(MODE_SEAM_COUNT) == -1);
}

TEST_CASE("each base mode's program wires exactly its own callbacks",
	"[gamemode-graphs][slice2][base]")
{
	for (s32 mode = 0; mode < MODE_RULES_MODE_COUNT; mode++) {
		INFO("mode " << mode);
		const std::string json = baseProgram(mode, "base:test");
		REQUIRE(json.find("\"schema\": \"pd.gamemode_graph.v1\"") != std::string::npos);
		REQUIRE(json.find("\"catalog_id\": \"base:test\"") != std::string::npos);
		CompiledProgram compiled;
		compileMode(compiled, json);
		INFO(compiled.err);
		REQUIRE(compiled.program != nullptr);
		REQUIRE(checkError(compiled.program, mode).empty());

		s32 modules = 0;
		for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
			const char *native[2] = {};
			const s32 n = modeRulesNativeModules(mode, seam, native);
			modules += n;
			std::set<std::string> wired(native, native + n);
			for (s32 m = 0; m < modeRulesModuleCount(); m++) {
				const char *kind = nullptr;
				s32 owner = 0, module_seam = 0;
				REQUIRE(modeRulesModuleAt(m, &kind, &owner, &module_seam) == 1);
				if (module_seam != seam) continue;
				INFO(kind);
				REQUIRE((missionGraphProgramEventReachesModule(compiled.program,
					modeRulesSeamEvent(seam), kind) != 0) == (wired.count(kind) != 0));
			}
		}
		REQUIRE(missionGraphProgramModuleNodeCount(compiled.program) == modules);
		REQUIRE(missionGraphProgramExecEdgeCount(compiled.program) == modules);
	}

	// What each mode's dispatchers do today, as programs.
	CompiledProgram combat;
	compileMode(combat, baseProgram(MPSCENARIO_COMBAT, "base:combat"));
	REQUIRE(missionGraphProgramNodeCount(combat.program) == 4);
	REQUIRE(missionGraphProgramEventReachesModule(combat.program, MISSION_GRAPH_EVENT_MODE_SCORE,
		"og.mode.default.score") == 1);
	REQUIRE(missionGraphProgramHandlesEvent(combat.program, MISSION_GRAPH_EVENT_MODE_TICK) == 0);
	CompiledProgram ctc;
	compileMode(ctc, baseProgram(MPSCENARIO_CAPTURETHECASE, "base:capture_the_case"));
	REQUIRE(missionGraphProgramEventReachesModule(ctc.program, MISSION_GRAPH_EVENT_MODE_SPAWN,
		"og.mode.ctc.spawn") == 1);
	REQUIRE(missionGraphProgramEventReachesModule(ctc.program, MISSION_GRAPH_EVENT_MODE_SPAWN,
		"og.mode.default.spawn") == 1);
	REQUIRE(missionGraphProgramEventReachesModule(ctc.program, MISSION_GRAPH_EVENT_MODE_SCORE,
		"og.mode.default.score") == 0);
	CompiledProgram koh;
	compileMode(koh, baseProgram(MPSCENARIO_KINGOFTHEHILL, "base:king_of_the_hill"));
	REQUIRE(missionGraphProgramEventReachesModule(koh.program, MISSION_GRAPH_EVENT_MODE_CHOSEN,
		"og.mode.koh.save") == 1);
	REQUIRE(missionGraphProgramEventReachesModule(koh.program, MISSION_GRAPH_EVENT_MODE_CHOSEN,
		"og.mode.koh.room_lights") == 1);

	// Out of range, or an id the JSON cannot carry as it is.
	char out[64];
	REQUIRE(modeRulesBaseProgramJson(6, "base:x", out, sizeof(out)) == -1);
	REQUIRE(modeRulesBaseProgramJson(0, "base:\"x", out, sizeof(out)) == -1);
	REQUIRE(modeRulesBaseProgramJson(0, "base:x", out, sizeof(out)) == -1); // too small
}

TEST_CASE("registration refuses what a mode cannot run",
	"[gamemode-graphs][slice2][registration]")
{
	// A module on an event that does not choose it would never run.
	CompiledProgram wrong_event;
	compileMode(wrong_event, oneEdge("event.mode.tick", "og.mode.htb.score"));
	REQUIRE(wrong_event.program != nullptr);
	const std::string why = checkError(wrong_event.program, MPSCENARIO_HOLDTHEBRIEFCASE);
	REQUIRE(why.find("og.mode.htb.score has no effect on event.mode.tick") != std::string::npos);
	REQUIRE(why.find("chosen by event.mode.score") != std::string::npos);

	// Reached through a gate, both branches count.
	CompiledProgram gated;
	compileMode(gated, "{\"nodes\":[{\"id\":\"e\",\"kind\":\"event.mode.tick\"},"
		"{\"id\":\"g\",\"kind\":\"gate.mission.phase\",\"phase\":\"active\"},"
		"{\"id\":\"m\",\"kind\":\"og.mode.htb.init\"},"
		"{\"id\":\"sp\",\"kind\":\"event.mode.spawn\"},{\"id\":\"sd\",\"kind\":\"og.mode.default.spawn\"}],"
		"\"edges\":[{\"from\":\"e\",\"to\":\"g\",\"input\":\"exec\"},"
		"{\"from\":\"g\",\"output\":\"fail\",\"to\":\"m\",\"input\":\"exec\"},"
		"{\"from\":\"sp\",\"to\":\"sd\",\"input\":\"exec\"}]}");
	INFO(gated.err);
	REQUIRE(gated.program != nullptr);
	REQUIRE(checkError(gated.program, MPSCENARIO_HOLDTHEBRIEFCASE).find("has no effect") !=
		std::string::npos);

	// Another mode's module: the modes share one state until slice 5.
	CompiledProgram foreign;
	compileMode(foreign, oneEdge("event.mode.tick", "og.mode.koh.tick"));
	REQUIRE(foreign.program != nullptr);
	REQUIRE(checkError(foreign.program, MPSCENARIO_HOLDTHEBRIEFCASE).find(
		"module 'og.mode.koh.tick' is another mode's") != std::string::npos);
	REQUIRE(checkError(foreign.program, MPSCENARIO_KINGOFTHEHILL).empty());

	// A mode must place its players.
	CompiledProgram no_spawn;
	compileMode(no_spawn, "{\"nodes\":[{\"id\":\"e\",\"kind\":\"event.mode.tick\"},"
		"{\"id\":\"m\",\"kind\":\"og.mode.htb.tick\"}],"
		"\"edges\":[{\"from\":\"e\",\"to\":\"m\",\"input\":\"exec\"}]}");
	REQUIRE(no_spawn.program != nullptr);
	REQUIRE(checkError(no_spawn.program, MPSCENARIO_HOLDTHEBRIEFCASE).find(
		"event.mode.spawn reaches no spawn module") != std::string::npos);

	// Out of the six modes there is nothing to check against yet (slice 6).
	CompiledProgram combat;
	compileMode(combat, baseProgram(MPSCENARIO_COMBAT, "base:combat"));
	REQUIRE(checkError(combat.program, 6).find("not one of the six base modes") != std::string::npos);
	REQUIRE(checkError(nullptr, 0) == "no program");
}

TEST_CASE("the extractor, the catalog and registration carry the mode program",
	"[gamemode-graphs][slice2][static]")
{
	const std::string pdmeta = readRepoFile("port/src/romextract_pdmeta.c");
	REQUIRE(pdmeta.find("modeRulesBaseProgramJson(e->ext.gamemode.mode_id, e->id,") != std::string::npos);
	REQUIRE(pdmeta.find("assetArchiveWriterAddPublicMem(&writer, MODE_GRAPH_MEMBER,") != std::string::npos);
	REQUIRE(pdmeta.find("s_existingArchiveHasEntry(relpath, MODE_GRAPH_MEMBER)") != std::string::npos);
	REQUIRE(pdmeta.find("\"mode_graph_file = \" MODE_GRAPH_MEMBER") != std::string::npos);
	REQUIRE(pdmeta.find("_vehiclegraph_modegraph_outside\"") != std::string::npos);

	const std::string base = readRepoFile("port/src/assetcatalog_base_extended.c");
	REQUIRE(base.find("\"mode.graph.json\", e->ext.gamemode.mode_graph_file") != std::string::npos);
	const std::string walker = readRepoFile("port/src/loader_walker_meta.c");
	REQUIRE(walker.find("\"mode_graph_file\",\n\t\t\tarchive_path, entry->ext.gamemode.mode_graph_file")
		!= std::string::npos);
	const std::string scanner = readRepoFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("iniGet(ini, \"mode_graph_file\", \"\")") != std::string::npos);
	const std::string network = readRepoFile("port/src/net/netdistrib.c");
	REQUIRE(network.find("iniGet(ini, \"mode_graph_file\", \"\")") != std::string::npos);
	const std::string paths = readRepoFile("port/include/asset_path_contract.h");
	REQUIRE(paths.find("\"mode_graph_file\"") != std::string::npos);

	const std::string runtime = readRepoFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("missionGraphProgramCompileMode(json, size, modeRulesModuleKnown,") !=
		std::string::npos);
	REQUIRE(runtime.find("modeRulesCheckProgram(program, mode, err, sizeof(err))") != std::string::npos);
	REQUIRE(runtime.find("MODE.GRAPH: refused the program of '%s': %s") != std::string::npos);
	REQUIRE(runtime.find("ok = s_compileModeGraph(binding, mode);") != std::string::npos);

	// The archive validator mirrors the table: every module and event by name.
	const std::string conformance = readRepoFile("tools/asset_archive_conformance.py");
	REQUIRE(conformance.find("MODE_GRAPH_EVENT_MODULES = {") != std::string::npos);
	for (s32 m = 0; m < modeRulesModuleCount(); m++) {
		const char *kind = nullptr;
		REQUIRE(modeRulesModuleAt(m, &kind, nullptr, nullptr) == 1);
		INFO(kind);
		REQUIRE(conformance.find(std::string("\"") + kind + "\"") != std::string::npos);
	}
	for (s32 i = 0; modeRulesEventAt(i) >= 0; i++) {
		REQUIRE(conformance.find(std::string("\"") + modeRulesEventName(modeRulesEventAt(i)) + "\"") !=
			std::string::npos);
	}
}

TEST_CASE("the example mode carries Combat's base program",
	"[gamemode-graphs][slice2][examples]")
{
	const std::string example = archiveMemberText(
		"examples/modding/typed-pdxxx-basic/gamemodes/tri_gamemode.pdgamemode", "mode.graph.json");
	REQUIRE(example == baseProgram(MPSCENARIO_COMBAT, "example:tri_gamemode"));
	const std::string ini = archiveMemberText(
		"examples/modding/typed-pdxxx-basic/gamemodes/tri_gamemode.pdgamemode", "gamemode.ini");
	REQUIRE(ini.find("mode_graph_file = mode.graph.json") != std::string::npos);
}

TEST_CASE("each base mode's plan is the original table's",
	"[gamemode-graphs][slice3][plan]")
{
	for (s32 mode = 0; mode < MODE_RULES_MODE_COUNT; mode++) {
		INFO("mode " << mode);
		CompiledProgram compiled;
		compileMode(compiled, baseProgram(mode, "base:test"));
		REQUIRE(compiled.program != nullptr);
		mode_rules_plan_t plan, native;
		REQUIRE(modeRulesPlanFromProgram(compiled.program, &plan) == 1);
		modeRulesNativePlan(mode, &native);
		REQUIRE(plan.modules == native.modules);
		REQUIRE(plan.modules == missionGraphProgramModuleNodeCount(compiled.program));
		for (s32 seam = 0; seam < MODE_SEAM_COUNT; seam++) {
			INFO("seam " << modeRulesSeamName(seam));
			REQUIRE(modeRulesPlanSeamEqual(&plan, &native, seam) == 1);
		}
	}

	// Capture the Case places its players with its own chooser first, then
	// the general one; Combat scores and spawns with the defaults alone.
	mode_rules_plan_t ctc;
	modeRulesNativePlan(MPSCENARIO_CAPTURETHECASE, &ctc);
	REQUIRE(ctc.owner[MODE_SEAM_SPAWN][0] == MPSCENARIO_CAPTURETHECASE);
	REQUIRE(ctc.owner[MODE_SEAM_SPAWN][1] == MODE_RULES_DEFAULT);
	REQUIRE(ctc.owner[MODE_SEAM_SCORE][0] == MPSCENARIO_CAPTURETHECASE);
	REQUIRE(ctc.owner[MODE_SEAM_SCORE][1] == MODE_PLAN_NONE);
	mode_rules_plan_t combat;
	modeRulesNativePlan(MPSCENARIO_COMBAT, &combat);
	REQUIRE(combat.modules == 2);
	REQUIRE(combat.owner[MODE_SEAM_SCORE][0] == MODE_RULES_DEFAULT);
	REQUIRE(combat.owner[MODE_SEAM_SPAWN][0] == MODE_RULES_DEFAULT);
	REQUIRE(modeRulesPlanRuns(&combat, MODE_SEAM_TICK, MPSCENARIO_COMBAT) == 0);

	// An empty plan, and no plan without a program.
	mode_rules_plan_t empty;
	REQUIRE(modeRulesPlanFromProgram(nullptr, &empty) == 0);
	REQUIRE(empty.modules == 0);
	REQUIRE(empty.owner[MODE_SEAM_SPAWN][0] == MODE_PLAN_NONE);
}

TEST_CASE("a plan keeps the walk order, each module once, at most two per seam",
	"[gamemode-graphs][slice3][plan]")
{
	// The general chooser first: the plan runs it before the mode's own.
	CompiledProgram reversed;
	compileMode(reversed, "{\"nodes\":[{\"id\":\"e\",\"kind\":\"event.mode.spawn\"},"
		"{\"id\":\"d\",\"kind\":\"og.mode.default.spawn\"},{\"id\":\"c\",\"kind\":\"og.mode.ctc.spawn\"},"
		"{\"id\":\"d2\",\"kind\":\"og.mode.default.spawn\"}],"
		"\"edges\":[{\"from\":\"e\",\"to\":\"d\",\"input\":\"exec\"},"
		"{\"from\":\"e\",\"to\":\"c\",\"input\":\"exec\"},{\"from\":\"e\",\"to\":\"d2\",\"input\":\"exec\"}]}");
	INFO(reversed.err);
	REQUIRE(reversed.program != nullptr);
	mode_rules_plan_t plan;
	REQUIRE(modeRulesPlanFromProgram(reversed.program, &plan) == 1);
	REQUIRE(plan.owner[MODE_SEAM_SPAWN][0] == MODE_RULES_DEFAULT);
	REQUIRE(plan.owner[MODE_SEAM_SPAWN][1] == MPSCENARIO_CAPTURETHECASE);
	REQUIRE(plan.modules == 2);

	// The walk fires each choosing event: a mode program's events are valid.
	mission_graph_event_t event = {};
	event.kind = MISSION_GRAPH_EVENT_MODE_SPAWN;
	event.player = -1;
	REQUIRE(missionGraphProgramFire(reversed.program, &event,
		[](const char *, const char *, const mission_graph_params_t *, const mission_graph_event_t *,
			void *) -> s32 { return 1; }, nullptr) == 3);
	event.seam = MISSION_GRAPH_SEAM_ENDSCREEN;
	REQUIRE(missionGraphProgramFire(reversed.program, &event,
		[](const char *, const char *, const mission_graph_params_t *, const mission_graph_event_t *,
			void *) -> s32 { return 1; }, nullptr) == -1);
}

TEST_CASE("the mode dispatchers run the program's plan and compare it with the original table",
	"[gamemode-graphs][slice3][static]")
{
	const std::string scenarios = readRepoFile("src/game/mplayer/scenarios.c");
	REQUIRE(!scenarios.empty());
	REQUIRE(scenarios.find("#include \"mode_rules.h\"") != std::string::npos);
	REQUIRE(scenarios.find("scenarioPlanMake(scenario, \"chosen\");") != std::string::npos);
	REQUIRE(scenarios.find("scenarioPlanMake(scenario, \"stage_reset\");") != std::string::npos);
	REQUIRE(scenarios.find("sysLoudFailf(\"MODE.PLAN\"") != std::string::npos);
	REQUIRE(scenarios.find("modeRulesPlanFromProgram(program, &s_ScenarioPlans[scenario])") !=
		std::string::npos);
	REQUIRE(scenarios.find("\"MODE.PARITY: differ scenario=%d seam=%s native=%d,%d program=%d,%d\"") !=
		std::string::npos);

	// Every seam is compared where it is dispatched.
	for (const char *seam : { "MODE_SEAM_INIT", "MODE_SEAM_NUM_PROPS", "MODE_SEAM_INIT_PROPS",
			"MODE_SEAM_TICK", "MODE_SEAM_TICK_CHR", "MODE_SEAM_HUD", "MODE_SEAM_SCORE",
			"MODE_SEAM_RADAR_EXTRA", "MODE_SEAM_RADAR_CHR", "MODE_SEAM_HIGHLIGHT_PROP",
			"MODE_SEAM_SPAWN", "MODE_SEAM_MAX_TEAMS", "MODE_SEAM_ROOM_LIGHTS", "MODE_SEAM_SAVE",
			"MODE_SEAM_STAGE_RESET", "MODE_SEAM_INTRO_PAD", "MODE_SEAM_TOKEN_PICKED_UP",
			"MODE_SEAM_TOKEN_DROPPED", "MODE_SEAM_PROP_ACTIVATED", "MODE_SEAM_PLAYER_DIED" }) {
		INFO(seam);
		REQUIRE(scenarios.find(std::string("plan, ") + seam + ");") != std::string::npos);
		REQUIRE(scenarios.find(std::string("case ") + seam + ":") != std::string::npos);
	}

	// No dispatcher calls a mode's callback past its plan: the table is read
	// by owner (the plan's), and by mode only for the options dialog, the
	// first-tick and spawn logs, and the parity probe's row.
	size_t at = 0;
	while ((at = scenarios.find("g_MpScenarios[scenario].", at)) != std::string::npos) {
		const std::string use = scenarios.substr(at, 40);
		INFO(use);
		REQUIRE((use.find(".optionsdialog") != std::string::npos ||
			use.find(".tickfunc);") != std::string::npos ||
			use.find(".spawnfunc)") != std::string::npos));
		at += 1;
	}
	REQUIRE(scenarios.find("g_MpScenarios[g_MpSetup.scenario].") == std::string::npos);
	REQUIRE(scenarios.find("g_MpScenarios[owner].calcscorefunc(mpchr, chrnum, score, deaths);") !=
		std::string::npos);
	REQUIRE(scenarios.find("const struct mpscenario *row = &g_MpScenarios[scenario];") != std::string::npos);

	const std::string events = readRepoFile("port/src/mode_events.c");
	REQUIRE(events.find("\"MODE.PARITY: %s mode=%s compared=%d differ=%d\"") != std::string::npos);
	REQUIRE(events.find("void modeParityRecord(s32 differ)") != std::string::npos);
}

TEST_CASE("the local mode smokes require the plan and no parity difference",
	"[gamemode-graphs][slice3][smokes][static]")
{
	const std::vector<std::pair<std::string, std::string>> smokes = {
		{ "mp_combat_local_scorelimit_smoke", "base:combat scenario=0" },
		{ "mp_hold_the_briefcase_local_smoke", "base:hold_the_briefcase scenario=1" },
		{ "mp_hacker_central_local_smoke", "base:hacker_central scenario=2" },
		{ "mp_pop_a_cap_local_smoke", "base:pop_a_cap scenario=3" },
		{ "mp_king_of_the_hill_local_smoke", "base:king_of_the_hill scenario=4" },
		{ "mp_capture_the_case_local_smoke", "base:capture_the_case scenario=5" },
	};
	for (const auto &smoke : smokes) {
		INFO(smoke.first);
		const std::string fixture = readRepoFile(("tools/smoke-verify/tests/" + smoke.first + ".json").c_str());
		REQUIRE(!fixture.empty());
		// The fixture's JSON text writes each regex backslash twice.
		REQUIRE(fixture.find("MODE\\\\.PLAN: stage_reset mode=" + smoke.second + " modules=[1-9][0-9]*") !=
			std::string::npos);
		const std::string mode = smoke.second.substr(0, smoke.second.find(' '));
		REQUIRE(fixture.find("MODE\\\\.PARITY: stage_end mode=" + mode +
			" compared=[1-9][0-9]* differ=0\\\\b") != std::string::npos);
		REQUIRE(fixture.find("\"MODE\\\\.PARITY: differ\"") != std::string::npos);
		REQUIRE(fixture.find("\"LOUDFAIL\\\\.MODE\\\\.PLAN\"") != std::string::npos);
	}
}

TEST_CASE("the code outside the mode table runs through the plan",
	"[gamemode-graphs][slice4][static]")
{
	const std::string scenarios = readRepoFile("src/game/mplayer/scenarios.c");
	REQUIRE(!scenarios.empty());

	// The table names its rows and fields.
	REQUIRE(scenarios.find("[MPSCENARIO_CAPTURETHECASE] = {") != std::string::npos);
	REQUIRE(scenarios.find("\t\t.droppedtokenfunc = ctcDropToken,") != std::string::npos);
	REQUIRE(scenarios.find("\t\t.playerdiedfunc = pacHandleDeath,") != std::string::npos);
	REQUIRE(scenarios.find("\t\t.activatedpropfunc = htmActivateProp,") != std::string::npos);

	// scenarioReset asks the plan; the mode switch is gone.
	const size_t reset = scenarios.find("void scenarioReset(void)\n{");
	REQUIRE(reset != std::string::npos);
	const std::string reset_body = scenarios.substr(reset, scenarios.find("\n}\n", reset) - reset);
	REQUIRE(reset_body.find("switch (scenario)") == std::string::npos);
	REQUIRE(reset_body.find("g_MpScenarios[owner].resetfunc();") != std::string::npos);
	REQUIRE(reset_body.find("scenarioRunIntroPad(scenario, plan, cmd);") != std::string::npos);
	REQUIRE(reset_body.find("ctcAddPad") == std::string::npos);
	REQUIRE(reset_body.find("kohAddHill") == std::string::npos);

	// Each token routine runs the plan's module for its own token kind.
	REQUIRE(scenarios.find("return scenarioPlanPickUpToken(chr, prop, WEAPON_BRIEFCASE2);") != std::string::npos);
	REQUIRE(scenarios.find("return scenarioPlanPickUpToken(chr, prop, WEAPON_DATAUPLINK);") != std::string::npos);
	REQUIRE(scenarios.find("scenarioTokenWeaponNum(owner) == weaponnum") != std::string::npos);
	REQUIRE(scenarios.find("g_MpScenarios[owner].droppedtokenfunc(chr, prop);") != std::string::npos);
	REQUIRE(scenarios.find("g_MpScenarios[owner].activatedpropfunc(chr, prop);") != std::string::npos);
	REQUIRE(scenarios.find("g_MpScenarios[owner].playerdiedfunc(aplayernum, vplayernum);") != std::string::npos);
	REQUIRE(scenarios.find("g_MpSetup.scenario == MPSCENARIO_") == std::string::npos);

	// The death hook asks the scenarios, not the mode number.
	const std::string mpstats = readRepoFile("src/game/mpstats.c");
	REQUIRE(mpstats.find("scenarioHandlePlayerDied(aplayernum, vplayernum);") != std::string::npos);
	REQUIRE(mpstats.find("pacHandleDeath(") == std::string::npos);
	const std::string setup_h = readRepoFile("src/include/game/mplayer/setup.h");
	REQUIRE(setup_h.find("void scenarioHandlePlayerDied(s32 aplayernum, s32 vplayernum);") != std::string::npos);

	// The new seams' modules are each mode's own.
	REQUIRE(std::string(modeRulesModuleKind(MPSCENARIO_POPACAP, MODE_SEAM_PLAYER_DIED)) == "og.mode.pac.player_died");
	REQUIRE(std::string(modeRulesModuleKind(MPSCENARIO_HACKERCENTRAL, MODE_SEAM_PROP_ACTIVATED)) ==
		"og.mode.htm.prop_activated");
	REQUIRE(modeRulesModuleKind(MPSCENARIO_POPACAP, MODE_SEAM_STAGE_RESET) == nullptr);
	REQUIRE(modeRulesSeamEvent(MODE_SEAM_TOKEN_DROPPED) == MISSION_GRAPH_EVENT_MODE_TOKEN_DROPPED);
	REQUIRE(modeRulesSeamEvent(MODE_SEAM_INTRO_PAD) == MISSION_GRAPH_EVENT_MODE_INTRO_PAD);
}

TEST_CASE("a plan names the mode states its modules use",
	"[gamemode-graphs][slice5][plan]")
{
	for (s32 mode = 0; mode < MODE_RULES_MODE_COUNT; mode++) {
		mode_rules_plan_t plan;
		modeRulesNativePlan(mode, &plan);
		// A base mode uses its own state alone; Combat uses none (the defaults
		// keep no state).
		REQUIRE(modeRulesPlanStates(&plan) == (mode == MPSCENARIO_COMBAT ? 0u : 1u << mode));
	}

	// A program that runs King of the Hill's hill and Hold the Briefcase's
	// token uses both states (registration refuses it until slice 6).
	CompiledProgram mixed;
	compileMode(mixed, "{\"nodes\":[{\"id\":\"t\",\"kind\":\"event.mode.tick\"},"
		"{\"id\":\"k\",\"kind\":\"og.mode.koh.tick\"},{\"id\":\"h\",\"kind\":\"og.mode.htb.tick\"}],"
		"\"edges\":[{\"from\":\"t\",\"to\":\"k\",\"input\":\"exec\"},{\"from\":\"t\",\"to\":\"h\",\"input\":\"exec\"}]}");
	INFO(mixed.err);
	REQUIRE(mixed.program != nullptr);
	mode_rules_plan_t plan;
	REQUIRE(modeRulesPlanFromProgram(mixed.program, &plan) == 1);
	REQUIRE(modeRulesPlanStates(&plan) ==
		((1u << MPSCENARIO_KINGOFTHEHILL) | (1u << MPSCENARIO_HOLDTHEBRIEFCASE)));
	REQUIRE(modeRulesPlanStates(nullptr) == 0);
}

TEST_CASE("the mode states sit side by side and travel as one section each",
	"[gamemode-graphs][slice5][static]")
{
	const std::string types = readRepoFile("src/include/types.h");
	const size_t data = types.find("struct scenariodata {");
	REQUIRE(data != std::string::npos);
	const std::string body = types.substr(data, types.find("};", data) - data);
	REQUIRE(body.find("union") == std::string::npos);
	for (const char *state : { "struct scenariodata_htb htb;", "struct scenariodata_htm htm;",
			"struct scenariodata_pac pac;", "struct scenariodata_koh koh;", "struct scenariodata_ctc ctc;" }) {
		REQUIRE(body.find(state) != std::string::npos);
	}

	const std::string scenarios = readRepoFile("src/game/mplayer/scenarios.c");
	REQUIRE(scenarios.find("states = modeRulesPlanStates(plan);") != std::string::npos);
	REQUIRE(scenarios.find("section->scenario = (u8)state;") != std::string::npos);
	REQUIRE(scenarios.find("static s32 scenarioNetTakePending(u8 state, net_scenario_state_t *out)") !=
		std::string::npos);
	REQUIRE(scenarios.find("if (states == 0 || received != states) {") != std::string::npos);
	REQUIRE(scenarios.find("state.scenario = (u8)g_MpSetup.scenario;") == std::string::npos);
	for (const char *take : { "scenarioNetTakePending(NET_SCENARIO_STATE_CAPTURETHECASE, &state)",
			"scenarioNetTakePending(NET_SCENARIO_STATE_HACKERCENTRAL, &state)",
			"scenarioNetTakePending(NET_SCENARIO_STATE_HOLDTHEBRIEFCASE, &state)",
			"scenarioNetTakePending(NET_SCENARIO_STATE_KINGOFTHEHILL, &state)",
			"scenarioNetTakePending(NET_SCENARIO_STATE_POPACAP, &state)" }) {
		INFO(take);
		bool found = false;
		for (const char *inc : { "capturethecase", "hackthatmac", "holdthebriefcase", "kingofthehill", "popacap" }) {
			const std::string text = readRepoFile((std::string("src/game/mplayer/scenarios/") + inc + ".inc").c_str());
			found = found || text.find(take) != std::string::npos;
		}
		REQUIRE(found);
	}

	const std::string netmsg = readRepoFile("port/src/net/netmsg.c");
	REQUIRE(netmsg.find("netScenarioStateListWrite(dst, list)") != std::string::npos);
	REQUIRE(netmsg.find("status = netScenarioStateListRead(src, &state);") != std::string::npos);
	REQUIRE(netmsg.find("SCENARIO.STATE: sent scenario=%u rev=%u reason=baseline sections=%u") !=
		std::string::npos);
	const std::string net = readRepoFile("port/src/net/net.c");
	REQUIRE(net.find("static net_scenario_state_list_t s_NetScenarioLastSent;") != std::string::npos);
	const std::string net_h = readRepoFile("port/include/net/net.h");
	REQUIRE(net_h.find("#define NET_PROTOCOL_VER 67") != std::string::npos);
}
