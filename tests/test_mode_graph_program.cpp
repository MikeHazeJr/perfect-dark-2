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
};

std::string trim(const std::string &text)
{
	const size_t first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return "";
	const size_t last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

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

	// The six rows of g_MpScenarios[], as written.
	const size_t table = scenarios.find("struct mpscenario g_MpScenarios[] = {");
	REQUIRE(table != std::string::npos);
	const size_t table_end = scenarios.find("\n};", table);
	REQUIRE(table_end != std::string::npos);
	std::vector<std::vector<std::string>> rows;
	size_t at = scenarios.find('{', table + std::string("struct mpscenario g_MpScenarios[] = ").size());
	while (true) {
		at = scenarios.find('{', at + 1);
		if (at == std::string::npos || at > table_end) break;
		const size_t close = scenarios.find('}', at);
		REQUIRE(close != std::string::npos);
		std::vector<std::string> fields;
		std::stringstream row(scenarios.substr(at + 1, close - at - 1));
		std::string item;
		while (std::getline(row, item, ',')) {
			// Drop line comments the row may hold.
			std::stringstream lines(item);
			std::string line, kept;
			while (std::getline(lines, line)) {
				const size_t comment = line.find("//");
				kept += comment == std::string::npos ? line : line.substr(0, comment);
			}
			kept = trim(kept);
			if (!kept.empty()) fields.push_back(kept);
		}
		rows.push_back(fields);
		at = close;
	}
	REQUIRE(rows.size() == MODE_RULES_MODE_COUNT);

	for (s32 mode = 0; mode < MODE_RULES_MODE_COUNT; mode++) {
		const std::vector<std::string> &fields = rows[static_cast<size_t>(mode)];
		REQUIRE(fields.size() <= kScenarioFields.size());
		bool has[MODE_SEAM_COUNT] = {};
		bool both_or_none[MODE_SEAM_COUNT][2] = {};
		for (size_t i = 0; i < kScenarioFields.size(); i++) {
			const bool set = i < fields.size() && fields[i] != "NULL";
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
	REQUIRE(modeRulesModuleCount() == 50);

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
	REQUIRE(pdmeta.find("_vehiclegraph_modegraph\"") != std::string::npos);

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
