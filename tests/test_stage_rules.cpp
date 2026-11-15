// Decision 3 (Mike, 2026-11-15): stage-wide rules travel packed in the level's
// archive. They live in the stage.json member of each .pdscenario
// (pd2.scenario.stage.v1); the first family is weather, which the game kept in
// g_WeatherConfig (src/game/weather.c).
#include "catch.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

extern "C" {
#include "scenario_stage_rules.h"
#include "modarchive.h"
}

namespace {

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

struct Parsed {
	scenario_stage_rules_t rules = {};
	std::string error;
	bool ok = false;
	explicit Parsed(const std::string &json)
	{
		char err[256] = { 0 };
		ok = scenarioStageRulesParse(json.data(), json.size(), &rules, err, sizeof(err)) == 1;
		error = err;
	}
};

std::string stage(const std::string &body)
{
	return "{ \"schema\": \"pd2.scenario.stage.v1\"" + (body.empty() ? std::string() : ", " + body) + " }";
}

std::string refusal(const std::string &json)
{
	Parsed p(json);
	CHECK_FALSE(p.ok);
	CHECK(p.rules.weather.present == 0);
	return p.error;
}

} // namespace

TEST_CASE("stage.json weather is read into the stage's weather setup", "[stage][scenario][stagerules]")
{
	// The shape the extractor writes for the Air Base row of the table.
	Parsed airbase(stage(R"("note": "x", "weather": { "wind_speed": 5, "y_min": -800, "y_max": 800, "z_max": -2000, "cutscene_only": false, "weather_rooms": [], "wind_direction": { "angle": 1.57079637, "speed_x": 0, "speed_z": -5 } })"));
	INFO(airbase.error);
	REQUIRE(airbase.ok);
	const scenario_stage_weather_t &a = airbase.rules.weather;
	CHECK(a.present == 1);
	CHECK(a.wind_speed == 5.0f);
	CHECK(a.y_min == -800.0f);
	CHECK(a.y_max == 800.0f);
	CHECK(a.z_max == -2000.0f);
	CHECK(a.cutscene_only == 0);
	CHECK(a.rooms_have_weather == 1);
	CHECK(a.room_count == 0);
	CHECK(a.has_wind_direction == 1);
	CHECK(a.wind_angle == 1.57079637f);
	CHECK(a.wind_speed_z == -5.0f);

	// G5 and Chicago: cutscene only; dry rooms.
	Parsed chicago(stage(R"("weather": { "wind_speed": 20, "y_min": -800, "y_max": 800, "z_max": 0, "cutscene_only": true, "dry_rooms": [98, 97, 15] })"));
	INFO(chicago.error);
	REQUIRE(chicago.ok);
	CHECK(chicago.rules.weather.cutscene_only == 1);
	CHECK(chicago.rules.weather.rooms_have_weather == 0);
	REQUIRE(chicago.rules.weather.room_count == 3);
	CHECK(chicago.rules.weather.rooms[0] == 98);
	CHECK(chicago.rules.weather.rooms[2] == 15);
	CHECK(chicago.rules.weather.has_wind_direction == 0);

	// No weather key: no weather rule. z_max may be left out.
	Parsed none(stage(R"("note": "no rules")"));
	REQUIRE(none.ok);
	CHECK(none.rules.weather.present == 0);
	Parsed nozmax(stage(R"("weather": { "wind_speed": 1, "y_min": 0, "y_max": 1, "dry_rooms": [] })"));
	REQUIRE(nozmax.ok);
	CHECK(nozmax.rules.weather.z_max == 0.0f);
}

TEST_CASE("a stage.json the reader cannot use is refused whole", "[stage][scenario][stagerules]")
{
	const std::string ok_weather = R"("wind_speed": 5, "y_min": -800, "y_max": 800, "dry_rooms": [])";
	CHECK(refusal("[]").find("not one JSON object") != std::string::npos);
	CHECK(refusal(R"({ "schema": "pd2.scenario.stage.v2" })").find("must declare schema") != std::string::npos);
	CHECK(refusal(stage(R"("stars": {})")).find("unknown key 'stars'") != std::string::npos);
	CHECK(refusal(stage(R"("note": 3)")).find("note must be text") != std::string::npos);
	CHECK(refusal(stage(R"("weather": [])")).find("weather must be an object") != std::string::npos);
	CHECK(refusal(stage("\"weather\": { " + ok_weather + ", \"rain\": true }")).find("unknown key 'rain'") !=
		std::string::npos);
	CHECK(refusal(stage(R"("weather": { "wind_speed": "fast", "y_min": 0, "y_max": 1, "dry_rooms": [] })"))
		.find("wind_speed must be a number") != std::string::npos);
	CHECK(refusal(stage(R"("weather": { "wind_speed": 1, "y_min": 5, "y_max": 5, "dry_rooms": [] })"))
		.find("y_min must be below") != std::string::npos);
	CHECK(refusal(stage(R"("weather": { "wind_speed": 1, "y_min": 0, "y_max": 1 })"))
		.find("exactly one of weather_rooms and dry_rooms") != std::string::npos);
	CHECK(refusal(stage(R"("weather": { "wind_speed": 1, "y_min": 0, "y_max": 1, "dry_rooms": [], "weather_rooms": [] })"))
		.find("exactly one of") != std::string::npos);
	CHECK(refusal(stage(R"("weather": { "wind_speed": 1, "y_min": 0, "y_max": 1, "dry_rooms": [0] })"))
		.find("not a room number") != std::string::npos);
	CHECK(refusal(stage(R"("weather": { "wind_speed": 1, "y_min": 0, "y_max": 1, "dry_rooms": [40000] })"))
		.find("not a room number") != std::string::npos);
	CHECK(refusal(stage(R"("weather": { "wind_speed": 1, "y_min": 0, "y_max": 1, "dry_rooms": [2.5] })"))
		.find("not a room number") != std::string::npos);
	CHECK(refusal(stage("\"weather\": { " + ok_weather + ", \"cutscene_only\": 1 }"))
		.find("cutscene_only must be true or false") != std::string::npos);
	CHECK(refusal(stage("\"weather\": { " + ok_weather + ", \"wind_direction\": { \"angle\": 1, \"speed_x\": 0 } }"))
		.find("speed_z must be a number") != std::string::npos);
	CHECK(refusal(stage("\"weather\": { " + ok_weather + ", \"wind_direction\": { \"angle\": 1, \"speed_x\": 0, \"speed_z\": 0, \"gust\": 1 } }"))
		.find("unknown key 'gust'") != std::string::npos);
	std::string many = "[";
	for (int i = 1; i <= SCENARIO_STAGE_MAX_WEATHER_ROOMS + 1; i++) many += (i > 1 ? ", " : "") + std::to_string(i);
	many += "]";
	CHECK(refusal(stage("\"weather\": { \"wind_speed\": 1, \"y_min\": 0, \"y_max\": 1, \"dry_rooms\": " + many + " }"))
		.find("more than 127 rooms") != std::string::npos);
}

TEST_CASE("the extractor writes the game's weather table and the weather code reads stage.json",
	"[stage][scenario][stagerules]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string bg = readSource("src/game/bg.c");
	const std::string reset = readSource("src/game/weatherreset.c");
	const std::string tick = readSource("src/game/weathertick.c");
	const std::string reader = readSource("port/src/scenario_stage_rules.cpp");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// One source: the extractor walks g_WeatherConfig by stage number and
	// writes every field of the row; the member is packed in every scenario.
	REQUIRE(extractor.find("static s32 s_buildStageJson(s32 stagenum, pdscenario_textbuf_t *out)") != std::string::npos);
	REQUIRE(extractor.find("for (i = 0; i < WEATHERCFG_MAX_STAGES && g_WeatherConfig[i].stagenum; i++) {") != std::string::npos);
	REQUIRE(extractor.find("(weather->flags & WEATHERFLAG_INCLUDE) ? \"weather_rooms\" : \"dry_rooms\"") != std::string::npos);
	REQUIRE(extractor.find("(weather->flags & WEATHERFLAG_CUTSCENE_ONLY) ? \"true\" : \"false\"") != std::string::npos);
	REQUIRE(extractor.find("if ((weather->flags & WEATHERFLAG_FORCE_WINDDIR) &&") != std::string::npos);
	REQUIRE(extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"stage.json\",") != std::string::npos);
	REQUIRE(extractor.find("s_existingArchiveEntryContains(relpath, \"stage.json\",\n\t\t\t\"ai: dark_room_lists false\") &&") != std::string::npos);
	REQUIRE(extractor.find("_lightrules_sky_stagejson_player_ai\"") != std::string::npos);

	// The stage loads it with its scenario tables, and it is dropped with them.
	REQUIRE(bg.find("scenarioSourceLoadStageRulesForStage(&stage, g_Vars.normmplayerisrunning);") != std::string::npos);
	REQUIRE(runtime.find("memset(&s_StageWideRules, 0, sizeof(s_StageWideRules));") != std::string::npos);
	REQUIRE(runtime.find("ok = scenarioStageRulesParse(text, text_size, &s_StageWideRules, error,") != std::string::npos);

	// The weather code takes the rule for a scenario-source stage and keeps
	// the table and the two stage checks for a ROM stage.
	REQUIRE(reset.find("if (bgUsesScenarioSource()) {\n\t\t// a stage built from scenario source: the weather of its stage.json") != std::string::npos);
	REQUIRE(reset.find("cfg->flags = (rule->rooms_have_weather ? WEATHERFLAG_INCLUDE : 0) |") != std::string::npos);
	REQUIRE(reset.find("if (g_WeatherConfig[i].stagenum == g_Stages[g_StageIndex].id) {") != std::string::npos);
	REQUIRE(tick.find("if ((g_CurWeatherConfig->zmax && g_Vars.currentplayer->cam_pos.z < g_CurWeatherConfig->zmax)") != std::string::npos);
	REQUIRE(tick.find("} else if ((g_StageIndex == STAGEINDEX_AIRBASE && g_Vars.currentplayer->cam_pos.z < -2000.0f)") != std::string::npos);

	// The checker knows exactly the reader's keys.
	auto python_set = [&](const char *name) {
		std::smatch m;
		REQUIRE(std::regex_search(conformance, m, std::regex(std::string(name) + R"( = \{([^}]*)\})")));
		std::set<std::string> out;
		const std::string body = m[1].str();
		const std::regex item("\"([^\"]+)\"");
		for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it) out.insert((*it)[1].str());
		return out;
	};
	auto reader_set = [&](const char *anchor) {
		const size_t at = reader.find(anchor);
		REQUIRE(at != std::string::npos);
		const size_t open = reader.find('{', at + std::strlen(anchor) - 1);
		const size_t close = reader.find('}', open);
		std::set<std::string> out;
		const std::string body = reader.substr(open, close - open);
		const std::regex item("\"([^\"]+)\"");
		for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it) out.insert((*it)[1].str());
		return out;
	};
	CHECK(python_set("STAGE_RULES_KEYS") == reader_set("onlyKeys(root, {"));
	CHECK(python_set("STAGE_WEATHER_KEYS") == reader_set("onlyKeys(weather, {"));
	REQUIRE(conformance.find("result.errors.extend(validate_stage_json(") != std::string::npos);
}

TEST_CASE("the stage.json player rule dresses the player and decides the disguise", "[stage][scenario][stagerules]")
{
	Parsed villa(stage(R"("player": { "perfect_agent_outfit": "negotiator" })"));
	INFO(villa.error);
	REQUIRE(villa.ok);
	CHECK(villa.rules.player.present == 1);
	CHECK(villa.rules.player.perfect_agent_outfit == scenarioStageOutfitFromName("negotiator"));
	CHECK(villa.rules.player.disguise_outfit == -1);
	CHECK(villa.rules.player.disguise_untargetable == 0);

	Parsed rescue(stage(R"("player": { "disguise": { "outfit": "lab", "untargetable": true } })"));
	INFO(rescue.error);
	REQUIRE(rescue.ok);
	CHECK(rescue.rules.player.perfect_agent_outfit == -1);
	CHECK(rescue.rules.player.disguise_outfit == scenarioStageOutfitFromName("lab"));
	CHECK(rescue.rules.player.disguise_untargetable == 1);

	Parsed none(stage(""));
	REQUIRE(none.ok);
	CHECK(none.rules.player.present == 0);
	CHECK(none.rules.player.perfect_agent_outfit == -1);

	CHECK(refusal(stage(R"("player": { "perfect_agent_outfit": "tuxedo" })")).find("is not an outfit ('tuxedo')") !=
		std::string::npos);
	CHECK(refusal(stage(R"("player": { "hat": "x" })")).find("unknown key 'hat'") != std::string::npos);
	CHECK(refusal(stage(R"("player": { "disguise": "lab" })")).find("disguise must be an object") != std::string::npos);
	CHECK(refusal(stage(R"("player": { "disguise": { "untargetable": "yes" } })"))
		.find("untargetable must be true or false") != std::string::npos);

	// The names are the OUTFIT_* constants in order, and the checker has the same list.
	const std::string constants = readSource("src/include/constants.h");
	const std::regex define(R"(#define OUTFIT_([A-Z_]+)\s+0x([0-9a-f]+))");
	int outfits = 0;
	for (std::sregex_iterator it(constants.begin(), constants.end(), define), end; it != end; ++it) {
		std::string name = (*it)[1].str();
		for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		const int value = std::stoi((*it)[2].str(), nullptr, 16);
		INFO(name);
		REQUIRE(scenarioStageOutfitName(value) != nullptr);
		CHECK(name == scenarioStageOutfitName(value));
		outfits++;
	}
	CHECK(outfits == 15);
	CHECK(scenarioStageOutfitName(15) == nullptr);
	const std::string conformance = readSource("tools/asset_archive_conformance.py");
	std::smatch m;
	REQUIRE(std::regex_search(conformance, m, std::regex(R"(STAGE_OUTFITS = \[([^\]]*)\])")));
	const std::string body = m[1].str();
	const std::regex item("\"([^\"]+)\"");
	int index = 0;
	for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it, ++index) {
		CHECK((*it)[1].str() == scenarioStageOutfitName(index));
	}
	CHECK(index == 15);

	// The extractor's rules are the original stage checks, and the game code
	// asks the rule on the scenario-source path.
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string player = readSource("src/game/player.c");
	const std::string bondgun = readSource("src/game/bondgun.c");
	REQUIRE(extractor.find("return stagenum == STAGE_VILLA ? OUTFIT_NEGOTIATOR : -1;") != std::string::npos);
	REQUIRE(extractor.find("case STAGE_RESCUE:  return OUTFIT_LAB;") != std::string::npos);
	REQUIRE(extractor.find("case STAGE_AIRBASE: return OUTFIT_STEWARDESS;") != std::string::npos);
	REQUIRE(extractor.find("return stagenum == STAGE_RESCUE;") != std::string::npos);
	REQUIRE(player.find("if (g_Vars.stagenum == STAGE_VILLA && lvGetDifficulty() >= DIFF_PA) {") != std::string::npos);
	REQUIRE(player.find("case STAGE_RESCUE:  outfit = OUTFIT_LAB; break;") != std::string::npos);
	REQUIRE(player.find("case STAGE_AIRBASE: outfit = OUTFIT_STEWARDESS; break;") != std::string::npos);
	REQUIRE(player.find("if (rule.perfect_agent_outfit >= 0 && lvGetDifficulty() >= DIFF_PA) {") != std::string::npos);
	REQUIRE(player.find("if (g_Vars.currentplayer->disguised && rule.disguise_outfit >= 0) {") != std::string::npos);
	REQUIRE(bondgun.find("if (scenarioSourceStagePlayer(&rule) && rule.disguise_untargetable) {") != std::string::npos);
	REQUIRE(bondgun.find("} else if (g_Vars.stagenum == STAGE_RESCUE) {\n\t\t\t\t\tchr->hidden |= CHRHFLAG_UNTARGETABLE;") !=
		std::string::npos);
}

TEST_CASE("the co-op spawn distance and the dark room rule come from stage.json", "[stage][scenario][stagerules]")
{
	Parsed defection(stage(R"("player": { "coop_spawn_distance": 80 })"));
	INFO(defection.error);
	REQUIRE(defection.ok);
	CHECK(defection.rules.player.coop_spawn_distance == 80.0f);
	CHECK(defection.rules.player.perfect_agent_outfit == -1);
	Parsed crashsite(stage(R"("ai": { "dark_room_lists": false })"));
	INFO(crashsite.error);
	REQUIRE(crashsite.ok);
	CHECK(crashsite.rules.ai.present == 1);
	CHECK(crashsite.rules.ai.no_dark_room_lists == 1);
	Parsed allowed(stage(R"("ai": { "dark_room_lists": true })"));
	REQUIRE(allowed.ok);
	CHECK(allowed.rules.ai.no_dark_room_lists == 0);

	CHECK(refusal(stage(R"("player": { "coop_spawn_distance": 0 })")).find("must be 1..1000") != std::string::npos);
	CHECK(refusal(stage(R"("player": { "coop_spawn_distance": 5000 })")).find("must be 1..1000") != std::string::npos);
	CHECK(refusal(stage(R"("ai": { "dark_room_lists": "no" })")).find("must be true or false") != std::string::npos);
	CHECK(refusal(stage(R"("ai": { "patrols": 1 })")).find("unknown key 'patrols'") != std::string::npos);

	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string chraction = readSource("src/game/chraction.c");
	const std::string chrai = readSource("src/game/chrai.c");
	REQUIRE(extractor.find("return stagenum == STAGE_DEFECTION ? 80.f : 0.f;") != std::string::npos);
	REQUIRE(extractor.find("return stagenum == STAGE_CRASHSITE;") != std::string::npos);
	REQUIRE(chraction.find("} else if (g_Vars.stagenum == STAGE_DEFECTION) {\n\t\tdistance = 80.f;") != std::string::npos);
	REQUIRE(chraction.find("if (scenarioSourceStagePlayer(&rule) && rule.coop_spawn_distance > 0.0f) {") != std::string::npos);
	REQUIRE(chrai.find("&& (bgUsesScenarioSource() ? !chraiStageHasNoDarkRoomLists()\n\t\t\t\t\t: g_Vars.stagenum != STAGE_CRASHSITE)) {") !=
		std::string::npos);
}

// Opt-in sweep (hidden from the default run): every .pdscenario under
// PD2_STAGE_RULES_ROOT, standalone or nested one level in an arena, must carry a
// stage.json the reader accepts. Read-only.
TEST_CASE("every scenario under PD2_STAGE_RULES_ROOT carries a readable stage.json", "[.][stagerules-sweep]")
{
	const char *root = std::getenv("PD2_STAGE_RULES_ROOT");
	if (!root || !root[0]) {
		WARN("PD2_STAGE_RULES_ROOT is not set; nothing to sweep");
		return;
	}
	size_t checked = 0;
	std::set<std::string> with_weather;
	auto check_member = [&](const std::string &label, const char *text, u32 size) {
		Parsed p(std::string(text, size));
		INFO(label << ": " << p.error);
		CHECK(p.ok);
		checked++;
		if (p.rules.weather.present) with_weather.insert(label.substr(label.find_last_of("/\\") + 1));
	};
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file()) continue;
		const std::string path = entry.path().string();
		const std::string ext = entry.path().extension().string();
		if (ext != ".pdscenario" && ext != ".pdarena") continue;
		mod_archive_t *archive = modArchiveOpen(path.c_str());
		REQUIRE(archive != nullptr);
		if (ext == ".pdscenario") {
			const s32 idx = modArchiveFindEntry(archive, "stage.json");
			INFO(path);
			REQUIRE(idx >= 0);
			u32 size = 0;
			char *text = static_cast<char *>(modArchiveExtractAlloc(archive, idx, &size));
			REQUIRE(text != nullptr);
			check_member(path, text, size);
			std::free(text);
		} else {
			for (s32 i = 0; i < modArchiveGetEntryCount(archive); i++) {
				const std::string name = modArchiveGetEntryName(archive, i);
				if (name.size() < 11 || name.compare(name.size() - 11, 11, ".pdscenario") != 0) continue;
				u32 nested_size = 0;
				void *nested = modArchiveExtractAlloc(archive, i, &nested_size);
				REQUIRE(nested != nullptr);
				u32 size = 0;
				char *text = static_cast<char *>(modArchiveExtractMemAlloc(nested, nested_size, "stage.json", &size));
				INFO(path << "::" << name);
				REQUIRE(text != nullptr);
				check_member(path + "::" + name, text, size);
				std::free(text);
				std::free(nested);
			}
		}
		modArchiveClose(archive);
	}
	std::string names;
	for (const std::string &n : with_weather) names += " " + n;
	WARN("read " << checked << " stage.json members under " << root << "; with weather:" << names);
	CHECK(checked > 0);
}
