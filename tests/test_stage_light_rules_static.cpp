// Per-stage room and light rules move from code into scenario source
// (decision 1, CORE, 2026-11-15).
//
// The game kept a handful of stage checks in its light code: rooms that are
// always drawn (a sky, a moon, a shaft, a planet), one room forced dark,
// firing-range and comms-hub lights that cannot be shot out, and two portals
// that light must not flood through, one that counts as open for light. The
// extractor now writes them as optional fields on each stage's rooms.json and
// portals.json rows, a stage built from scenario source reads them there, and
// the stage checks stay only for a stage built from the ROM background.
#include "catch.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

static std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

static std::string between(const std::string &text, const char *from, const char *to)
{
	const size_t start = text.find(from);
	REQUIRE(start != std::string::npos);
	const size_t end = text.find(to, start);
	REQUIRE(end != std::string::npos);
	return text.substr(start, end - start);
}

static std::set<std::string> tokens(const std::string &text, const char *pattern)
{
	std::set<std::string> out;
	const std::regex re(pattern);
	for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
		out.insert((*it)[1].str());
	}
	return out;
}

}

TEST_CASE("the extractor writes each stage's room and light rules onto its rows",
		"[extraction][scenario][lightrules]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");

	// The original conditions: always-drawn rooms by stage table index, the
	// rest by stage number.
	REQUIRE(extractor.find("&& roomnum == 0x000f) /* freight elevator shaft */") != std::string::npos);
	REQUIRE(extractor.find("&& roomnum == 0x0001) /* moon */") != std::string::npos);
	REQUIRE(extractor.find("&& roomnum == 0x0002) /* fake sky */") != std::string::npos);
	REQUIRE(extractor.find("(stage_idx == STAGEINDEX_TEST_OLD && roomnum == 0x0001)") != std::string::npos);
	REQUIRE(extractor.find("(stage_idx == STAGEINDEX_ATTACKSHIP && roomnum == 0x0071); /* planet */") !=
		std::string::npos);
	REQUIRE(extractor.find("&& roomnum == 0x003d) { /* near the top comms hub */") != std::string::npos);
	REQUIRE(extractor.find("&& roomnum == 0x000a; /* firing range */") != std::string::npos);
	REQUIRE(extractor.find("&& roomnum == 0x003e) { /* top comms hub: its first two lights */") !=
		std::string::npos);
	REQUIRE(extractor.find("return 0x3u;") != std::string::npos);
	REQUIRE(extractor.find("&& (portalnum == 98 || portalnum == 100);") != std::string::npos);
	REQUIRE(extractor.find("return stagenum == STAGE_INVESTIGATION && portalnum == 0;") != std::string::npos);

	// The fields, on the room's and the portal's own rows.
	REQUIRE(extractor.find("\", \\\"render_always\\\": true\"") != std::string::npos);
	REQUIRE(extractor.find("\", \\\"base_brightness\\\": %d\"") != std::string::npos);
	REQUIRE(extractor.find("\", \\\"lights_unshootable\\\": true\"") != std::string::npos);
	REQUIRE(extractor.find("\", \\\"unshootable_lights\\\": [\"") != std::string::npos);
	REQUIRE(extractor.find("\", \\\"light_flood_blocked\\\": true\"") != std::string::npos);
	REQUIRE(extractor.find("\", \\\"open_to_light\\\": true\"") != std::string::npos);
	REQUIRE(extractor.find("s_appendRoomRuleFields(out, stagenum, stage_idx, r) != 0") != std::string::npos);
	REQUIRE(extractor.find("s_buildBgPortalsJson(bg_data, bg_size, spec->stagenum,") != std::string::npos);
	REQUIRE(extractor.find("s_buildBgRoomTableJson(bg_data, bg_size, spec->stagenum,") != std::string::npos);
	REQUIRE(extractor.find("light rule for portal %u dropped") != std::string::npos);

	// Archives extracted before the rules are not clean, and the cache kind
	// moved, so existing installs are re-extracted.
	REQUIRE(extractor.find("\"Optional rules on a row: render_always\")") != std::string::npos);
	REQUIRE(extractor.find("\"Optional light rules on a row\")") != std::string::npos);
	REQUIRE(extractor.find("_roomtable_b1113_b1114_b1115_lightrules\"") != std::string::npos);
}

TEST_CASE("the extracted rules name the same stages, rooms and portals as the ROM-path checks",
		"[extraction][scenario][lightrules]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string dlights = readSource("src/game/dlights.c");
	const std::string rules = between(extractor, "static s32 s_stageRoomRenderAlways(",
		"static s32 s_appendRoomRuleFields(");

	// Every stage check left in the light code is a ROM-path one, and the
	// extractor's rules use exactly those stages and rooms.
	REQUIRE(tokens(rules, "\\b(STAGEINDEX_[A-Z0-9_]+)\\b") ==
		tokens(dlights, "\\b(STAGEINDEX_[A-Z0-9_]+)\\b"));
	REQUIRE(tokens(rules, "\\b(STAGE_[A-Z0-9_]+)\\b") == tokens(dlights, "\\b(STAGE_[A-Z0-9_]+)\\b"));
	REQUIRE(tokens(rules, "roomnum == (0x[0-9a-f]{4})") == tokens(dlights, "roomnum == (0x[0-9a-f]{4})"));
	REQUIRE(tokens(dlights, "var80061438\\[([0-9]+)\\] = false") == std::set<std::string>{"98", "100"});
	REQUIRE(dlights.find("var8009cad8[0] = 1;") != std::string::npos);
	REQUIRE(dlights.find("if (roomnum == 0x003e && (i == 0 || i == 1)) { // top comms hub") !=
		std::string::npos);
}

TEST_CASE("a scenario-source stage takes its rules from its rows and a ROM stage keeps the checks",
		"[scenario][bg][lightrules]")
{
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string runtime_h = readSource("port/include/scenario_source_runtime.h");
	const std::string dlights = readSource("src/game/dlights.c");
	const std::string bg = readSource("src/game/bg.c");
	const std::string bg_h = readSource("src/include/game/bg.h");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	for (const char *query : {"s32 scenarioSourceRoomRenderAlways(s32 roomnum);",
			"s32 scenarioSourceRoomBaseBrightness(s32 roomnum, s32 *out_level);",
			"s32 scenarioSourceRoomLightUnshootable(s32 roomnum, s32 lightindex);",
			"s32 scenarioSourcePortalBlocksLightFlood(s32 portalnum);",
			"s32 scenarioSourcePortalOpenToLight(s32 portalnum);",
			"void scenarioSourceStageRulesReset(void);"}) {
		INFO(query);
		REQUIRE(runtime_h.find(query) != std::string::npos);
	}
	REQUIRE(bg_h.find("s32 bgUsesScenarioSource(void);") != std::string::npos);

	// The rows' fields are read, and each table clears its stage's rules first.
	for (const char *key : {"\"render_always\"", "\"base_brightness\"", "\"lights_unshootable\"",
			"\"unshootable_lights\"", "\"light_flood_blocked\"", "\"open_to_light\""}) {
		INFO(key);
		REQUIRE(runtime.find(key) != std::string::npos);
	}
	const std::string portals = between(runtime, "struct bgportal *scenarioSourceLoadPortalsForStage(",
		"u8 *scenarioSourceLoadPadsForStage(");
	REQUIRE(portals.find("s_stagePortalRulesClear();") != std::string::npos);
	REQUIRE(portals.find("s_StagePortalRules[i] = rows[i].light_rules;") != std::string::npos);
	const std::string table = between(runtime, "s32 scenarioSourceApplyRoomTableForStage(",
		"\n}\n");
	REQUIRE(table.find("s_stageRoomRulesClear();") != std::string::npos);
	REQUIRE(table.find("s_readRoomRuleFields(obj_open, obj_close,") != std::string::npos);

	// Activating a stage's scenario background and stopping it drop the rules.
	const std::string activate = between(bg, "static s32 bgTryActivateScenarioSourceBackground(",
		"scenario = scenarioSourceFindEntryForStage(stage, prefer_mp);");
	REQUIRE(activate.find("scenarioSourceStageRulesReset();") != std::string::npos);
	const std::string stop = between(bg, "void bgStop(void)", "\n}\n");
	REQUIRE(stop.find("scenarioSourceStageRulesReset();") != std::string::npos);

	// The light code asks the scenario rules for a scenario-source stage and
	// keeps the stage checks for a ROM-background stage, at all five places.
	REQUIRE(dlights.find("if (scenarioSourceRoomBaseBrightness(roomnum, &level)) {") != std::string::npos);
	REQUIRE(dlights.find("renderalways = scenarioSourceRoomRenderAlways(roomnum);") != std::string::npos);
	REQUIRE(dlights.find("if (scenarioSourceRoomLightUnshootable(roomnum, i)) {") != std::string::npos);
	REQUIRE(dlights.find("if (scenarioSourcePortalBlocksLightFlood(i)) {") != std::string::npos);
	REQUIRE(dlights.find("if (scenarioSourcePortalOpenToLight(i)) {") != std::string::npos);
	size_t guards = 0;
	for (size_t at = dlights.find("if (bgUsesScenarioSource()) {"); at != std::string::npos;
			at = dlights.find("if (bgUsesScenarioSource()) {", at + 1)) {
		guards++;
	}
	REQUIRE(guards == 4);
	REQUIRE(dlights.find("} else if (g_Vars.stagenum == STAGE_EXTRACTION || g_Vars.stagenum == STAGE_DEFECTION) {") !=
		std::string::npos);
	REQUIRE(dlights.find("} else if (g_Vars.stagenum == STAGE_INVESTIGATION) {") != std::string::npos);

	// The validator knows the portal rule fields.
	REQUIRE(conformance.find("for rule_key in (\"light_flood_blocked\", \"open_to_light\"):") != std::string::npos);
}
