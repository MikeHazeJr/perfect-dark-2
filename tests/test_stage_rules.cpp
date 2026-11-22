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

TEST_CASE("stage.json atmosphere has independent validated settings", "[stage][scenario][stagerules]")
{
	Parsed stageLook(stage(R"("atmosphere": {
	    "sky_rgb": [15, 35, 75], "fog": {"start": 120, "end": 480},
	    "ambient_light": 104, "primary_music": "base:music_defection"
	})"));
	INFO(stageLook.error);
	REQUIRE(stageLook.ok);
	const auto &look = stageLook.rules.atmosphere;
	CHECK(look.present);
	CHECK(look.has_sky);
	CHECK(look.sky_rgb[2] == 75);
	CHECK(look.has_fog);
	CHECK(look.fog_start == 120);
	CHECK(look.fog_end == 480);
	CHECK(look.has_ambient);
	CHECK(look.ambient_light == 104);
	CHECK(std::string(look.primary_music) == "base:music_defection");

	Parsed onlyAmbient(stage(R"("atmosphere": {"ambient_light": 0})"));
	REQUIRE(onlyAmbient.ok);
	CHECK(onlyAmbient.rules.atmosphere.has_ambient);
	CHECK_FALSE(onlyAmbient.rules.atmosphere.has_sky);
	CHECK_FALSE(onlyAmbient.rules.atmosphere.has_fog);
	Parsed absent(stage(""));
	REQUIRE(absent.ok);
	CHECK_FALSE(absent.rules.atmosphere.present);

	for (const char *wrong : {R"({})", R"({"sky_rgb":[0,0,256]})", R"({"sky_rgb":[1.5,0,0]})",
			R"({"fog":{"start":200,"end":200}})", R"({"ambient_light":true})",
			R"({"primary_music":13})", R"({"primary_music":"base:music:path"})"}) {
		Parsed rejected(stage(std::string("\"atmosphere\": ") + wrong));
		CHECK_FALSE(rejected.ok);
		CHECK_FALSE(rejected.rules.atmosphere.present);
	}
}

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
	CHECK(refusal(stage(R"("fog": {})")).find("unknown key 'fog'") != std::string::npos);
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

TEST_CASE("Weather startup selects a native kind and intensity without replacing room rules",
	"[stage][scenario][stagerules]")
{
	Parsed snow(stage(R"("weather_start": {"kind": "snow", "intensity": 1})"));
	INFO(snow.error);
	REQUIRE(snow.ok);
	CHECK(snow.rules.weather.present == 1);
	CHECK(snow.rules.weather.startup_kind == SCENARIO_STAGE_WEATHER_START_SNOW);
	CHECK(snow.rules.weather.startup_intensity == 1);
	CHECK(snow.rules.weather.wind_speed == 20.0f);
	CHECK(snow.rules.weather.y_min == -800.0f);
	CHECK(snow.rules.weather.y_max == 800.0f);
	CHECK(snow.rules.weather.rooms_have_weather == 0);
	CHECK(snow.rules.weather.room_count == 0);
	CHECK(snow.rules.weather.cutscene_only == 0);
	CHECK(snow.rules.weather.z_max == 0.0f);

	const std::string roomRules = R"("weather": {
		"wind_speed": 5.125, "y_min": -400, "y_max": 500, "weather_rooms": [1, 2],
		"z_max": -60, "cutscene_only": true, "wind_direction": {"angle": 1, "speed_x": 4, "speed_z": 3}
	})";
	Parsed legacy(stage(roomRules));
	REQUIRE(legacy.ok);
	CHECK(legacy.rules.weather.startup_kind == SCENARIO_STAGE_WEATHER_START_NONE);
	CHECK(legacy.rules.weather.startup_intensity == 0);

	const std::string rain = R"(, "weather_start": {"kind": "rain", "intensity": 3})";
	Parsed configured(stage(roomRules + rain));
	INFO(configured.error);
	REQUIRE(configured.ok);
	const auto &weather = configured.rules.weather;
	CHECK(weather.startup_kind == SCENARIO_STAGE_WEATHER_START_RAIN);
	CHECK(weather.startup_intensity == 3);
	CHECK(weather.wind_speed == 5.125f);
	CHECK(weather.y_min == -400.0f);
	CHECK(weather.y_max == 500.0f);
	CHECK(weather.z_max == -60.0f);
	CHECK(weather.cutscene_only == 1);
	CHECK(weather.rooms_have_weather == 1);
	REQUIRE(weather.room_count == 2);
	CHECK(weather.rooms[0] == 1);
	CHECK(weather.rooms[1] == 2);
	CHECK(weather.has_wind_direction == 1);
	CHECK(weather.wind_angle == 1.0f);
	CHECK(weather.wind_speed_x == 4.0f);
	CHECK(weather.wind_speed_z == 3.0f);

	const char *invalid[] = {
		R"("snow")",
		R"({"kind": "fog", "intensity": 1})",
		R"({"kind": "snow", "intensity": 2})",
		R"({"kind": "rain", "intensity": 4})",
		R"({"kind": "rain", "intensity": 0})",
		R"({"kind": "rain", "intensity": 1.5})",
		R"({"kind": "rain", "intensity": true})",
		R"({"kind": "snow"})",
		R"({"kind": "rain", "intensity": 1, "room": 1})",
	};
	for (const char *entry : invalid) {
		INFO(entry);
		CHECK_FALSE(refusal(stage(roomRules + ", \"weather_start\": " + entry)).empty());
	}
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
	REQUIRE(extractor.find("s_existingArchiveEntryContains(check, \"stage.json\",\n\t\t\t\"stars: the starfield of a stage\") &&") != std::string::npos);
	REQUIRE(extractor.find("_lightrules_sky_stagejson_player_ai_intro_mpsetup_ending_hub_stars_dyntex_ammo_objrules_sparks_gas_mplifts_aiedges_simtwins_typedargs_catalogargs_assetargs_ruleevents_liftglass_textbank\"") != std::string::npos);

	// The stage loads it with its scenario tables, and it is dropped with them.
	REQUIRE(bg.find("scenarioSourceLoadStageRulesForStage(&stage, g_Vars.normmplayerisrunning);") != std::string::npos);
	REQUIRE(runtime.find("memset(&s_StageWideRules, 0, sizeof(s_StageWideRules));") != std::string::npos);
	REQUIRE(runtime.find("ok = scenarioStageRulesParse(text, text_size, &s_StageWideRules, error,") != std::string::npos);

	// The weather code takes the rule for a scenario-source stage and keeps
	// the table and the two stage checks for a ROM stage.
	REQUIRE(reset.find("if (bgUsesScenarioSource()) {\n\t\t// a stage built from scenario source: the weather of its stage.json") != std::string::npos);
	REQUIRE(reset.find("cfg->flags = (rule->rooms_have_weather ? WEATHERFLAG_INCLUDE : 0) |") != std::string::npos);
	REQUIRE(reset.find("if (g_WeatherConfig[i].stagenum == g_Stages[g_StageIndex].id) {") != std::string::npos);
	const size_t stageWeather = reset.find("if (scenarioSourceStageWeather(&rule)) {");
	REQUIRE(stageWeather != std::string::npos);
	const size_t startupValue = reset.find("startup_intensity = rule.startup_intensity;", stageWeather);
	REQUIRE(startupValue != std::string::npos);
	const size_t weatherReady = reset.find("g_WeatherActive = true;", startupValue);
	REQUIRE(weatherReady != std::string::npos);
	REQUIRE(reset.find("weatherConfigureRain(startup_intensity);", weatherReady) != std::string::npos);
	REQUIRE(reset.find("weatherConfigureSnow(startup_intensity);", weatherReady) != std::string::npos);
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
	CHECK(python_set("STAGE_WEATHER_START_KEYS") == reader_set("onlyKeys(startup, {"));
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

TEST_CASE("the stage.json player rule names the weapons the unlimited ammo cheats leave alone",
          "[stage][scenario][stagerules]")
{
	Parsed chicago(stage(R"("player": { "unlimited_ammo_excludes": ["base:remotemine"] })"));
	INFO(chicago.error);
	REQUIRE(chicago.ok);
	CHECK(chicago.rules.player.present == 1);
	REQUIRE(chicago.rules.player.unlimited_ammo_exclude_count == 1);
	CHECK(std::string(chicago.rules.player.unlimited_ammo_excludes[0]) == "base:remotemine");
	CHECK(chicago.rules.player.perfect_agent_outfit == -1);

	Parsed none(stage(""));
	REQUIRE(none.ok);
	CHECK(none.rules.player.unlimited_ammo_exclude_count == 0);

	CHECK(refusal(stage(R"("player": { "unlimited_ammo_excludes": "base:remotemine" })"))
		.find("must be a list of weapon catalog ids") != std::string::npos);
	CHECK(refusal(stage(R"("player": { "unlimited_ammo_excludes": [] })")).find("names 1 to 8 weapons") !=
		std::string::npos);
	CHECK(refusal(stage(R"("player": { "unlimited_ammo_excludes": ["remotemine"] })"))
		.find("not a weapon catalog id") != std::string::npos);
	CHECK(refusal(stage(R"("player": { "unlimited_ammo_excludes": [1] })")).find("not a weapon catalog id") !=
		std::string::npos);
	CHECK(refusal(stage(R"("player": { "unlimited_ammo_excludes": ["base:remotemine", "base:remotemine"] })"))
		.find("names base:remotemine twice") != std::string::npos);

	// The extractor writes the original's two stage checks as the weapon whose
	// primary ammo it is; the game asks the rule on a scenario-source stage and
	// keeps the checks for a ROM stage; the runtime refuses a name that is not a
	// weapon; the checker knows the key.
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string bondgun = readSource("src/game/bondgun.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");
	REQUIRE(extractor.find("case STAGE_CHICAGO:     return AMMOTYPE_REMOTE_MINE;") != std::string::npos);
	REQUIRE(extractor.find("case STAGE_AIRFORCEONE: return AMMOTYPE_TIMED_MINE;") != std::string::npos);
	REQUIRE(extractor.find("wpn->ammos[FUNC_PRIMARY]->type == ammotype") != std::string::npos);
	REQUIRE(bondgun.find("if (bgunStageExcludesUnlimitedAmmo(ammotype)) {") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetAmmoTypeForWeapon(weapon.weapon_num, FUNC_PRIMARY) == ammotype") !=
		std::string::npos);
	REQUIRE(bondgun.find("(ammotype == AMMOTYPE_REMOTE_MINE && g_Vars.stagenum == STAGE_CHICAGO)") !=
		std::string::npos);
	REQUIRE(bondgun.find("(ammotype == AMMOTYPE_TIMED_MINE && g_Vars.stagenum == STAGE_AIRFORCEONE)") !=
		std::string::npos);
	REQUIRE(runtime.find("player.unlimited_ammo_excludes names %s, which is not a weapon") != std::string::npos);
	REQUIRE(conformance.find("\"unlimited_ammo_excludes\"") != std::string::npos);
}

TEST_CASE("object rules travel on objects.json rows and replace the original's object checks",
          "[stage][scenario][objectrules]")
{
	// object-rules.md: hover cars left out of co-op and counter-op, the Enemy
	// Rockets exemption, the Extraction chopper's hums and fall.
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string setup = readSource("src/game/setup.c");
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// The extractor writes the original's conditions on the rows.
	REQUIRE(extractor.find("(stagenum == STAGE_EXTRACTION || stagenum == STAGE_DEFECTION)) {\n\t\tsnprintf(out, out_n, \",\\n      \\\"coop\\\": false\");") !=
		std::string::npos);
	REQUIRE(extractor.find("((const struct weaponobj *)ptr)->weaponnum == WEAPON_K7AVENGER &&\n\t\t\tstagenum == STAGE_INVESTIGATION) {") !=
		std::string::npos);
	REQUIRE(extractor.find("catalogReadableSoundRefId(SFX_SHIP_HUM, engine, sizeof(engine));") != std::string::npos);
	REQUIRE(extractor.find("catalogReadableSoundRefId(SFX_810D, thrust, sizeof(thrust));") != std::string::npos);
	REQUIRE(extractor.find("use_mp_setup ? -1 : (s32)spec->stagenum") != std::string::npos);
	REQUIRE(extractor.find("if ((type == OBJTYPE_LIFT || type == OBJTYPE_ESCASTEP) &&\n\t\t\t(stagenum == STAGE_CITRAINING || stagenum == STAGE_CHICAGO ||\n\t\t\tstagenum == STAGE_VILLA || stagenum == STAGE_INFILTRATION ||\n\t\t\tstagenum == STAGE_G5BUILDING || stagenum == STAGE_PELAGIC)) {\n\t\tsnprintf(out, out_n, \",\\n      \\\"multiplayer\\\": true\");") !=
		std::string::npos);

	// The runtime allows each field only on its kind and keeps the rows by
	// setup command index.
	REQUIRE(runtime.find("if (record->type != OBJTYPE_HOVERCAR ||") != std::string::npos);
	REQUIRE(runtime.find("if (record->type != OBJTYPE_WEAPON ||") != std::string::npos);
	REQUIRE(runtime.find("if (record->type != OBJTYPE_CHOPPER) {") != std::string::npos);
	REQUIRE(runtime.find("if ((record->type != OBJTYPE_LIFT && record->type != OBJTYPE_ESCASTEP) ||") != std::string::npos);
	REQUIRE(runtime.find("rules->multiplayer_kept = b != 0;") != std::string::npos);
	REQUIRE(runtime.find("s_ObjectRuleRows[s_ObjectRuleRowCount].index = i;") != std::string::npos);
	REQUIRE(runtime.find("s_setupResolveHumSound(table, text, &rules->engine_hum_sound)") != std::string::npos);
	REQUIRE(runtime.find("s_setupResolveHumSound(table, text, &rules->thrust_hum_sound)") != std::string::npos);

	// The game asks the rule on a stage built from scenario source and keeps
	// the original checks for a ROM stage.
	REQUIRE(setup.find("if (setupHovercarWanted(index, withhovercars) && withobjs") != std::string::npos);
	REQUIRE(setup.find("if (!bgUsesScenarioSource()) {\n\t\treturn withhovercars;") != std::string::npos);
	REQUIRE(setup.find("&& !setupWeaponKeptUnderEnemyRockets(cmdindex)) {") != std::string::npos);
	REQUIRE(setup.find("|| g_Vars.stagenum != STAGE_INVESTIGATION || lvGetDifficulty() != DIFF_PA) {") != std::string::npos);
	REQUIRE(setup.find("chopperSetRules(chopper, rules.engine_hum_sound,") != std::string::npos);
	// B-228's list: the row rule decides on a scenario-source stage in both
	// transport cases, the list and the SP-in-MP overlay stay a ROM stage's
	// (B-1253: on scenario source the overlay made every lift twice).
	REQUIRE(setup.find("by_rule = scenarioSourceSetupRecordRules(index, &rules) && rules.multiplayer_kept;\n\t\tfilter = by_rule ? 0 : diffflag;") !=
		std::string::npos);
	{
		size_t uses = 0;
		for (size_t at = setup.find("if (withobjs && setupTransportWanted(obj, index, diffflag,");
				at != std::string::npos;
				at = setup.find("if (withobjs && setupTransportWanted(obj, index, diffflag,", at + 1)) {
			uses++;
		}
		CHECK(uses == 2);
	}
	REQUIRE(setup.find("(obj->flags2 & mptransport_diffflag) == 0") == std::string::npos);
	REQUIRE(setup.find("if (g_Vars.mplayerisrunning && !bgUsesScenarioSource()) {\n\t\t\t\tswitch (g_Vars.stagenum) {") !=
		std::string::npos);
	REQUIRE(setup.find("if (g_Vars.mplayerisrunning && !bgUsesScenarioSource() && (\n\t\t\t\t\tstagenum == STAGE_CITRAINING ||") !=
		std::string::npos);
	REQUIRE(propobj.find("soundnum = chopperThrustHum(chopper);") != std::string::npos);
	REQUIRE(propobj.find("soundnum = chopperEngineHum(chopper);") != std::string::npos);
	REQUIRE(propobj.find("if (chopperFallsAsDrop(chopper)) {") != std::string::npos);
	REQUIRE(propobj.find("return g_Vars.stagenum == STAGE_EXTRACTION ? SFX_SHIP_HUM : SFX_810F;") != std::string::npos);
	REQUIRE(propobj.find("return g_Vars.stagenum == STAGE_EXTRACTION ? SFX_810D : SFX_8110;") != std::string::npos);
	REQUIRE(propobj.find("return stageGetIndex(g_Vars.stagenum) == STAGEINDEX_EXTRACTION;") != std::string::npos);
	// No stage check is left where the hums are chosen.
	REQUIRE(propobj.find("soundnum = g_Vars.stagenum == STAGE_EXTRACTION") == std::string::npos);

	// The checker knows the fields.
	REQUIRE(conformance.find("def validate_object_rule_fields(") != std::string::npos);
	REQUIRE(conformance.find("multiplayer needs a lift or escalator_step row and true or false") != std::string::npos);
	REQUIRE(conformance.find("OBJECT_RULE_DIFFICULTIES = [\"agent\", \"special_agent\", \"perfect_agent\", \"perfect_dark\"]") !=
		std::string::npos);
}

TEST_CASE("the stage.json sparks rule decides whether environmental sparks flash the lights",
          "[stage][scenario][stagerules]")
{
	Parsed crashsite(stage(R"("sparks": { "flash_lights": false })"));
	INFO(crashsite.error);
	REQUIRE(crashsite.ok);
	CHECK(crashsite.rules.sparks.present == 1);
	CHECK(crashsite.rules.sparks.no_light_flash == 1);

	Parsed flashing(stage(R"("sparks": { "flash_lights": true })"));
	REQUIRE(flashing.ok);
	CHECK(flashing.rules.sparks.no_light_flash == 0);

	Parsed none(stage(""));
	REQUIRE(none.ok);
	CHECK(none.rules.sparks.present == 0);

	CHECK(refusal(stage(R"("sparks": {})")).find("sparks.flash_lights must be true or false") != std::string::npos);
	CHECK(refusal(stage(R"("sparks": { "flash_lights": "no" })")).find("must be true or false") != std::string::npos);
	CHECK(refusal(stage(R"("sparks": { "colour": 1 })")).find("unknown key 'colour'") != std::string::npos);
	CHECK(refusal(stage(R"("sparks": true)")).find("sparks must be an object") != std::string::npos);

	// The extractor writes the original's Crash Site check, the game asks the
	// rule on a scenario-source stage and keeps the check for a ROM stage.
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string sparks = readSource("src/game/sparks.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");
	REQUIRE(extractor.find("static s32 s_stageSparksNoLightFlash(s32 stagenum)\n{\n\treturn stagenum == STAGE_CRASHSITE;") !=
		std::string::npos);
	REQUIRE(sparks.find("return !(scenarioSourceStageSparks(&rule) && rule.no_light_flash);") != std::string::npos);
	REQUIRE(sparks.find("return g_Vars.stagenum != STAGE_CRASHSITE;") != std::string::npos);
	REQUIRE(sparks.find("if (g_Vars.stagenum != STAGE_CRASHSITE) {") == std::string::npos);
	REQUIRE(conformance.find("sparks.flash_lights must be true or false") != std::string::npos);
}

TEST_CASE("the stage.json gas rule places a gas cloud and the ROM path uses the same rule",
          "[stage][scenario][stagerules]")
{
	Parsed escape(stage(R"("gas": { "rooms": [146, 147, 145], "sphere": { "center": [-1473, -308, -13660],
		"radius": 1328, "door_tag": 50 }, "door_rooms": [ { "room": 145, "door_tags": [48, 49], "add": 0.2 } ],
		"cutscene_fade": { "start_frame": 2180, "full_frame": 2600 } })"));
	INFO(escape.error);
	REQUIRE(escape.ok);
	const scenario_stage_gas_t &gas = escape.rules.gas;
	CHECK(gas.present == 1);
	REQUIRE(gas.room_count == 3);
	CHECK(gas.rooms[2] == 145);
	CHECK(gas.has_sphere == 1);
	CHECK(gas.sphere_center[2] == -13660.0f);
	CHECK(gas.sphere_radius == 1328.0f);
	CHECK(gas.sphere_door_tag == 50);
	REQUIRE(gas.door_room_count == 1);
	CHECK(gas.door_rooms[0].room == 145);
	REQUIRE(gas.door_rooms[0].tag_count == 2);
	CHECK(gas.door_rooms[0].tags[1] == 49);
	CHECK(gas.door_rooms[0].add == Approx(0.2f));
	CHECK(gas.has_cutscene_fade == 1);
	CHECK(gas.fade_start == 2180);
	CHECK(gas.fade_full == 2600);

	Parsed sphere_only(stage(R"("gas": { "sphere": { "center": [0, 0, 0], "radius": 10 } })"));
	REQUIRE(sphere_only.ok);
	CHECK(sphere_only.rules.gas.sphere_door_tag == -1);
	CHECK(sphere_only.rules.gas.room_count == 0);

	CHECK(refusal(stage(R"("gas": {})")).find("gas needs rooms or a sphere") != std::string::npos);
	CHECK(refusal(stage(R"("gas": { "rooms": [0] })")).find("not a room number") != std::string::npos);
	CHECK(refusal(stage(R"("gas": { "sphere": { "center": [0, 0], "radius": 1 } })")).find("center must be [x, y, z]") !=
		std::string::npos);
	CHECK(refusal(stage(R"("gas": { "sphere": { "center": [0, 0, 0], "radius": 0 } })")).find("radius must be more than 0") !=
		std::string::npos);
	CHECK(refusal(stage(R"("gas": { "rooms": [1], "door_rooms": [ { "room": 1, "door_tags": [] } ] })"))
		.find("door_tags names 1 to 4 doors") != std::string::npos);
	CHECK(refusal(stage(R"("gas": { "rooms": [1], "cutscene_fade": { "start_frame": 10, "full_frame": 5 } })"))
		.find("full_frame must come after start_frame") != std::string::npos);
	CHECK(refusal(stage(R"("gas": { "rooms": [1], "smell": 1 })")).find("unknown key 'smell'") != std::string::npos);

	// One rule struct: a ROM stage uses g_EscapeGasRule, the extractor writes it,
	// and the game draws the gas wherever the stage has a rule.
	const std::string nbomb = readSource("src/game/nbomb.c");
	const std::string player = readSource("src/game/player.c");
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	REQUIRE(nbomb.find(".sphere_center = { -1473.0f, -308.0f, -13660.0f },") != std::string::npos);
	REQUIRE(nbomb.find(".door_rooms = { { .room = 0x91, .tag_count = 2, .tags = { 0x30, 0x31 }, .add = 0.2f } },") !=
		std::string::npos);
	REQUIRE(nbomb.find(".fade_start = 2180,") != std::string::npos);
	REQUIRE(nbomb.find("if (g_Vars.stagenum == STAGE_ESCAPE) {\n\t\t*out = g_EscapeGasRule;") != std::string::npos);
	REQUIRE(nbomb.find("cutsceneframe60 < 2180") == std::string::npos);
	REQUIRE(player.find("if (gasStageHasGas()) {\n\t\t\tgdl = gasRender(gdl);") != std::string::npos);
	REQUIRE(player.find("if (g_Vars.stagenum == STAGE_ESCAPE) {\n\t\t\tgdl = gasRender(gdl);") == std::string::npos);
	REQUIRE(extractor.find("stagenum == STAGE_ESCAPE ? &g_EscapeGasRule : NULL;") != std::string::npos);
}

TEST_CASE("B-1250 and B-1251: cache kinds fit their stamps and packed hum sounds resolve",
          "[stage][scenario][objectrules][b1250][b1251]")
{
	// B-1250: the stamp readers take PDEXTRACT_CACHE_KIND_MAX - 1 chars; the
	// writer refuses a longer kind, and the scenario and arena kinds fit.
	const std::string cache = readSource("port/src/romextract_pd_cache.c");
	REQUIRE(cache.find("#define PDEXTRACT_CACHE_KIND_MAX 2048") != std::string::npos);
	REQUIRE(cache.find("#define PDEXTRACT_CACHE_KIND_SCANF \"2047\"") != std::string::npos);
	REQUIRE(cache.find("if (strlen(kind) >= PDEXTRACT_CACHE_KIND_MAX) {") != std::string::npos);
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	for (const char *name : { "ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND", "ROMEXTRACT_PDARENA_FAST_CACHE_KIND" }) {
		std::smatch m;
		const std::regex define(std::string("#define ") + name + " \"([^\"]+)\"");
		INFO(name);
		REQUIRE(std::regex_search(extractor, m, define));
		CHECK(m[1].str().size() < 2048);
	}

	// B-1251: a configured sound's packed number (top bit set) is accepted.
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const size_t pack = runtime.find("static s32 s_setupPackHumSound(");
	const size_t resolve = runtime.find("static s32 s_setupResolveHumSound(");
	const size_t readRules = runtime.find("static s32 s_setupReadObjectRules(");
	REQUIRE(readRules != std::string::npos);
	REQUIRE(pack < resolve);
	REQUIRE(resolve < readRules);
	const std::string packHum = runtime.substr(pack, resolve - pack);
	const size_t range = packHum.find("sound_id <= 0 || sound_id > 0xffff");
	const size_t cast = packHum.find("ref.packed = (s16)sound_id;");
	const size_t category = packHum.find("category != AUDIO_CAT_SFX && !(category == AUDIO_CAT_VOICE && ref.hasconfig)");
	const size_t output = packHum.find("*out = ref.packed;");
	REQUIRE(output != std::string::npos);
	REQUIRE(range < cast);
	REQUIRE(cast < category);
	REQUIRE(category < output);
	REQUIRE(packHum.find("sound_id > 0x7fff") == std::string::npos);
	const std::string resolveHum = runtime.substr(resolve, readRules - resolve);
	REQUIRE(resolveHum.find("catalogResolveAudio(id, &audio) && s_setupPackHumSound(audio.sound_id, audio.category, out)") !=
		std::string::npos);
	REQUIRE(resolveHum.find("s_setupPackHumSound(entry->ext.audio.sound_id, entry->ext.audio.category, out)") !=
		std::string::npos);
}

TEST_CASE("the stage.json ending rule decides how a stage presents its outcome", "[stage][scenario][stagerules]")
{
	Parsed unknown(stage(R"("ending": { "outcome": "unknown" })"));
	INFO(unknown.error);
	REQUIRE(unknown.ok);
	CHECK(unknown.rules.ending.present == 1);
	CHECK(unknown.rules.ending.outcome_unknown == 1);
	Parsed known(stage(R"("ending": { "outcome": "known" })"));
	REQUIRE(known.ok);
	CHECK(known.rules.ending.present == 1);
	CHECK(known.rules.ending.outcome_unknown == 0);
	Parsed empty(stage(R"("ending": {})"));
	REQUIRE(empty.ok);
	CHECK(empty.rules.ending.outcome_unknown == 0);
	Parsed none(stage(""));
	REQUIRE(none.ok);
	CHECK(none.rules.ending.present == 0);

	CHECK(refusal(stage(R"("ending": [])")).find("ending must be an object") != std::string::npos);
	CHECK(refusal(stage(R"("ending": { "outcome": "maybe" })")).find("ending.outcome must be") != std::string::npos);
	CHECK(refusal(stage(R"("ending": { "outcome": 1 })")).find("ending.outcome must be") != std::string::npos);
	CHECK(refusal(stage(R"("ending": { "result": "unknown" })")).find("unknown key 'result'") != std::string::npos);

	// The extractor writes it from the game's own check, and the native sites
	// read the rule on a scenario-source stage and the check on a ROM stage.
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string endscreen = readSource("src/game/endscreen.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	REQUIRE(extractor.find("return stageGetIndex(stagenum) == STAGEINDEX_DEFENSE;") != std::string::npos);
	REQUIRE(extractor.find(R"(s_textbufAppend(out, ",\n  \"ending\": { \"outcome\": \"unknown\" }"))") != std::string::npos);
	REQUIRE(extractor.find("s_existingArchiveEntryContains(check, \"stage.json\",\n\t\t\t\"stars: the starfield of a stage\") &&") != std::string::npos);
	REQUIRE(runtime.find("s32 scenarioSourceStageEnding(scenario_stage_ending_t *out)") != std::string::npos);
	REQUIRE(endscreen.find("\tif (bgUsesScenarioSource()) {\n"
		"\t\tscenario_stage_ending_t rule;\n\n"
		"\t\treturn scenarioSourceStageEnding(&rule) && rule.outcome_unknown;\n"
		"\t}\n\n"
		"\treturn g_StageIndex == STAGEINDEX_DEFENSE;") != std::string::npos);
}

TEST_CASE("the stage.json stars rule carries the starfield", "[stage][scenario][stagerules]")
{
	Parsed night(stage(R"("stars": { "count": 200, "grid": 2 })"));
	INFO(night.error);
	REQUIRE(night.ok);
	CHECK(night.rules.stars.present == 1);
	CHECK(night.rules.stars.count == 200);
	CHECK(night.rules.stars.grid == 2);
	CHECK(night.rules.stars.below_horizon == 0);
	Parsed space(stage(R"("stars": { "count": 1200, "grid": 3, "below_horizon": true })"));
	REQUIRE(space.ok);
	CHECK(space.rules.stars.below_horizon == 1);
	Parsed none(stage(""));
	REQUIRE(none.ok);
	CHECK(none.rules.stars.present == 0);
	CHECK(refusal(stage(R"("stars": true)")).find("stars must be an object") != std::string::npos);
	CHECK(refusal(stage(R"("stars": { "grid": 2 })")).find("stars.count must be a whole number") != std::string::npos);
	CHECK(refusal(stage(R"("stars": { "count": 0, "grid": 2 })")).find("stars.count must be a whole number from 1 to 8192") != std::string::npos);
	CHECK(refusal(stage(R"("stars": { "count": 8193, "grid": 2 })")).find("from 1 to 8192") != std::string::npos);
	CHECK(refusal(stage(R"("stars": { "count": 2.5, "grid": 2 })")).find("stars.count must be a whole number") != std::string::npos);
	CHECK(refusal(stage(R"("stars": { "count": 200, "grid": 9 })")).find("stars.grid must be a whole number from 1 to 8") != std::string::npos);
	CHECK(refusal(stage(R"("stars": { "count": 200, "grid": 2, "below_horizon": 1 })")).find("stars.below_horizon must be true or false") != std::string::npos);
	CHECK(refusal(stage(R"("stars": { "count": 200, "grid": 2, "colour": 1 })")).find("stars has an unknown key 'colour'") != std::string::npos);

	// The extractor writes it from starsReset's table for the stages lvReset
	// gives a starfield; the game reads the rule on a scenario-source stage.
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string lv = readSource("src/game/lv.c");
	const std::string stars = readSource("src/game/stars.c");
	const std::string bg = readSource("src/game/bg.c");
	REQUIRE(extractor.find("static s32 s_stageStars(s32 stagenum, s32 *count, s32 *grid, s32 *below_horizon)") != std::string::npos);
	REQUIRE(extractor.find("\tcase STAGE_ATTACKSHIP:\n\t\t*count = 1200;\n\t\t*grid = 3;\n\t\t*below_horizon = 1;") != std::string::npos);
	REQUIRE(extractor.find("\"below_horizon\\\": %s }\",") != std::string::npos);
	REQUIRE(lv.find("if (bgUsesScenarioSource()) {\n\t\tif (scenarioSourceStageStars(NULL)) {\n\t\t\tstarsReset();") != std::string::npos);
	REQUIRE(stars.find("if (bgUsesScenarioSource() && scenarioSourceStageStars(&rule)) {\n\t\t\tg_StarCount = rule.count;") != std::string::npos);
	REQUIRE(bg.find("? scenarioSourceStageStars(NULL) != 0") != std::string::npos);
}

TEST_CASE("the stage.json hub rule marks the stage between missions", "[stage][scenario][stagerules]")
{
	Parsed hub(stage(R"("hub": true)"));
	INFO(hub.error);
	REQUIRE(hub.ok);
	CHECK(hub.rules.hub.present == 1);
	CHECK(hub.rules.hub.hub == 1);
	Parsed not_hub(stage(R"("hub": false)"));
	REQUIRE(not_hub.ok);
	CHECK(not_hub.rules.hub.present == 1);
	CHECK(not_hub.rules.hub.hub == 0);
	Parsed none(stage(""));
	REQUIRE(none.ok);
	CHECK(none.rules.hub.present == 0);
	CHECK(refusal(stage(R"("hub": 1)")).find("hub must be true or false") != std::string::npos);
	CHECK(refusal(stage(R"("hub": { "training": true })")).find("hub must be true or false") != std::string::npos);

	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string lv = readSource("src/game/lv.c");
	REQUIRE(extractor.find("static s32 s_stageIsHub(s32 stagenum)\n{\n\treturn stagenum == STAGE_CITRAINING;\n}") != std::string::npos);
	REQUIRE(extractor.find(R"(if (is_hub && s_textbufAppend(out, ",\n  \"hub\": true") != 0) {)") != std::string::npos);
	REQUIRE(lv.find("bool lvStageIsHub(void)\n{\n\tif (bgUsesScenarioSource()) {\n"
		"\t\treturn scenarioSourceStageIsHub() != 0;\n\t}\n\n"
		"\treturn g_Vars.stagenum == STAGE_CITRAINING;\n}") != std::string::npos);

	// Every hub rule asks the one function; the training content and the main
	// menu stage keep their own checks.
	auto count = [](const std::string &text, const std::string &needle) {
		size_t n = 0;
		for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) n++;
		return n;
	};
	const struct { const char *file; size_t uses; } sites[] = {
		{ "src/game/cheats.c", 1 }, { "src/game/hudmsg.c", 1 }, { "src/game/chraction.c", 2 },
		{ "src/game/corpsestore.c", 1 }, { "src/game/player.c", 3 }, { "src/game/endscreen.c", 3 },
	};
	for (const auto &site : sites) {
		INFO(site.file);
		CHECK(count(readSource(site.file), "lvStageIsHub()") == site.uses);
	}
	CHECK(count(readSource("src/game/cheats.c"), "STAGE_CITRAINING") == 0);
	CHECK(count(readSource("src/game/corpsestore.c"), "STAGE_CITRAINING") == 0);
	CHECK(count(readSource("src/game/endscreen.c"), "STAGE_CITRAINING") == 0);
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

TEST_CASE("Stage text banks: a stage names its own language bank in its archive",
          "[stagerules][text-bank][static]")
{
	const std::string lang = readSource("src/game/lang.c");
	const std::string setup = readSource("src/game/setup.c");
	const std::string lv = readSource("src/game/lv.c");
	const std::string walker = readSource("port/src/loader_walker_scenario.c");
	const std::string scanner = readSource("port/src/assetcatalog_scanner.c");
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// The stage's catalog row carries the bank its archive names, read at
	// registration so the briefing has it before the stage loads.
	REQUIRE(walker.find("loaderWalkerEnvelopeStrCopy(manifest, manifest_len, \"text_bank\",") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"text_bank\", \"\")") != std::string::npos);

	// The three places that asked lang.c's stage table ask the stage now.
	REQUIRE(setup.find("briefing->langbank = langGetStageBank(stagenum, 0);") != std::string::npos);
	REQUIRE(setup.find("s32 stagebank = langGetStageBank(stagenum, g_Vars.normmplayerisrunning);") != std::string::npos);
	REQUIRE(lv.find("s32 bank = langGetStageBank(g_Vars.stagenum, g_Vars.normmplayerisrunning);") != std::string::npos);
	REQUIRE(setup.find("langGetLangBankIndexFromStagenum(") == std::string::npos);
	REQUIRE(lv.find("langGetLangBankIndexFromStagenum(") == std::string::npos);

	// The archive's bank wins; a stage that names none keeps the table; a
	// named id that is not a language bank fails loudly.
	const size_t resolver = lang.find("s32 langGetStageBank(s32 stagenum, s32 prefer_mp)");
	REQUIRE(resolver != std::string::npos);
	const std::string body = lang.substr(resolver, 2600);
	const size_t named = body.find("scenario->ext.scenario.text_bank[0]");
	const size_t lang_type = body.find("bank->type == ASSET_LANG");
	const size_t table = body.find("result = (s32)langGetLangBankIndexFromStagenum(stagenum);");
	REQUIRE(named != std::string::npos);
	REQUIRE(lang_type != std::string::npos);
	REQUIRE(table != std::string::npos);
	REQUIRE(named < table);
	REQUIRE(body.find("sysLoudFailf(\"LANG.STAGE\",") != std::string::npos);

	// The extractor writes the bank from the game's table into every base
	// scenario, an archive without it is rebuilt, and the kind moved.
	REQUIRE(extractor.find("\",\\n  \\\"text_bank\\\": \\\"%s\\\"\", text_bank);") != std::string::npos);
	REQUIRE(extractor.find("\"\\\"text_bank\\\": \\\"\") &&") != std::string::npos);
	REQUIRE(extractor.find("_ruleevents_liftglass_textbank\"") != std::string::npos);
	REQUIRE(conformance.find("SCENARIO_TEXT_BANK_RE") != std::string::npos);
}

TEST_CASE("stage text banks: the bank is bounded, and a level's own scenario reads its manifest",
	"[stage-rules][text-bank][static]")
{
	const std::string lang = readSource("src/game/lang.c");
	const std::string scanner = readSource("port/src/assetcatalog_scanner.c");

	// Review of PR 28: a named bank must index g_LangBanks (registration does
	// not bound an INI entry's bank_id), and clearing a bank outside the array
	// is refused, so stage teardown cannot write past it.
	REQUIRE(lang.find("&& bank->ext.lang.bank_id < (s32)ARRAYCOUNT(g_LangBanks)) {") != std::string::npos);
	const size_t clear = lang.find("void langClearBank(s32 bank)");
	REQUIRE(clear != std::string::npos);
	const std::string clear_body = lang.substr(clear, 260);
	REQUIRE(clear_body.find("if (bank < 0 || bank >= (s32)ARRAYCOUNT(g_LangBanks)) {") != std::string::npos);

	// A scenario that exists only inside a mod level is registered through its
	// scenario.ini; its bank comes from the archive's manifest, as the walker
	// reads it for every other scenario.
	REQUIRE(scanner.find("loaderWalkerEnvelopeStrCopy(manifest, manifest_size, \"text_bank\", out, out_n)")
		!= std::string::npos);
	const size_t embedded = scanner.find("static s32 registerArenaEmbeddedScenario(");
	REQUIRE(embedded != std::string::npos);
	REQUIRE(scanner.find("scenarioArchiveManifestTextBank(bytes, size, text_bank, sizeof(text_bank))", embedded)
		!= std::string::npos);
}
