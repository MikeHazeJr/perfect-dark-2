#include "catch.hpp"

#include <cstring>
#include <string>

extern "C" {
#include "scenario_codec.h"
}

namespace {

std::string v3Document()
{
	return R"({"version":3,"name":"Transaction","arenaId":"base:arena_mp_complex","scenarioId":"base:combat","timelimit":9,"scorelimit":9,"teamscorelimit":400,"options":0,"weaponset":-1,"weapon_id0":"base:falcon2","weapon_id1":"","weapon_id2":"","weapon_id3":"","weapon_id4":"","weapon_id5":"","spawnWeaponId":"","spawnWeaponMode":1,"bots":[]})";
}

scenario_codec_status_e parse(const std::string &json,
		scenario_document_t &document)
{
	char detail[192]{};
	return scenarioDocumentParse(json.data(), json.size(), &document, detail,
		sizeof(detail));
}

} // namespace

TEST_CASE("saved Scenario v3 parses as typed immutable candidate",
	"[v006][b1068][scenario][parse]")
{
	scenario_document_t document{};
	REQUIRE(parse(v3Document(), document) == SCENARIO_CODEC_OK);
	REQUIRE(document.version == 3);
	REQUIRE(std::string(document.arena_id) == "base:arena_mp_complex");
	REQUIRE(std::string(document.scenario_id) == "base:combat");
	REQUIRE(std::string(document.weapon_ids[0]) == "base:falcon2");
	REQUIRE(document.weapon_id_present[5] == 1);
	REQUIRE(document.spawn_weapon_mode == 1);
}

TEST_CASE("saved Scenario parser rejects malformed candidates without output",
	"[v006][b1068][scenario][rollback]")
{
	const std::string valid = v3Document();
	const std::string truncated = valid.substr(0, valid.size() - 2);
	scenario_document_t document;
	std::memset(&document, 0xa5, sizeof(document));

	REQUIRE(parse(truncated, document) == SCENARIO_CODEC_INVALID_JSON);
	REQUIRE(document.version == 0);
	REQUIRE(document.bot_count == 0);
	REQUIRE(document.arena_id[0] == '\0');
}

TEST_CASE("saved Scenario parser rejects wrong types duplicates and trailing data",
	"[v006][b1068][scenario][parse]")
{
	scenario_document_t document{};
	std::string wrongType = v3Document();
	const auto limit = wrongType.find("\"timelimit\":9");
	REQUIRE(limit != std::string::npos);
	wrongType.replace(limit, std::string("\"timelimit\":9").size(),
		"\"timelimit\":\"9\"");
	REQUIRE(parse(wrongType, document) == SCENARIO_CODEC_WRONG_TYPE);

	std::string duplicate = v3Document();
	duplicate.insert(1, "\"name\":\"duplicate\",");
	REQUIRE(parse(duplicate, document) == SCENARIO_CODEC_DUPLICATE_FIELD);

	REQUIRE(parse(v3Document() + "x", document)
		== SCENARIO_CODEC_INVALID_JSON);
}

TEST_CASE("saved Scenario parser rejects oversized and version-mismatched input",
	"[v006][b1068][scenario][bounds]")
{
	scenario_document_t document{};
	std::string oversized(SCENARIO_CODEC_MAX_BYTES + 1, 'x');
	REQUIRE(parse(oversized, document) == SCENARIO_CODEC_TOO_LARGE);

	std::string future = v3Document();
	const auto version = future.find("\"version\":3");
	future.replace(version, std::string("\"version\":3").size(),
		"\"version\":4");
	REQUIRE(parse(future, document) == SCENARIO_CODEC_UNSUPPORTED_VERSION);
}

TEST_CASE("saved Scenario accepts the exact prior shipping v3 hybrid shape",
	"[v006][b1068][scenario][authority][migration]")
{
	/* Field order and companion completeness mirror the fadf9ff6 writer. */
	const std::string priorV3 = R"({
  "version": 3,
  "name": "Prior shipping v3",
  "arena": 31,
  "arenaId": "base:arena_mp_complex",
  "scenario": 0,
  "scenarioId": "base:combat",
  "timelimit": 60,
  "scorelimit": 9,
  "teamscorelimit": 400,
  "options": 0,
  "weaponset": -1,
  "weapon_id0": "base:falcon2",
  "weapon0": 1,
  "weapon_id1": "",
  "weapon1": 0,
  "weapon_id2": "",
  "weapon2": 0,
  "weapon_id3": "",
  "weapon3": 0,
  "weapon_id4": "",
  "weapon4": 0,
  "weapon_id5": "",
  "weapon5": 0,
  "spawnWeaponId": "",
  "spawnWeaponMode": 1,
  "bots": []
})";
	scenario_document_t document{};
	REQUIRE(parse(priorV3, document) == SCENARIO_CODEC_OK);
	REQUIRE(document.version == 3);
	REQUIRE(document.legacy_arena == 31);
	REQUIRE(document.legacy_weapon_present[5] == 1);
	REQUIRE(std::string(document.arena_id) == "base:arena_mp_complex");
}

TEST_CASE("saved Scenario v3 rejects partial hybrid and numeric bot identity",
	"[v006][b1068][scenario][authority][rollback]")
{
	scenario_document_t document{};
	std::string numeric = v3Document();
	numeric.insert(1, "\"arena\":31,");
	REQUIRE(parse(numeric, document) == SCENARIO_CODEC_MISSING_FIELD);

	numeric = v3Document();
	const auto botArray = numeric.find("\"bots\":[]");
	REQUIRE(botArray != std::string::npos);
	numeric.replace(botArray, std::string("\"bots\":[]").size(),
		"\"bots\":[{\"name\":\"Bot\",\"profileId\":\"base:normal\","
		"\"difficulty\":2,\"bodyId\":\"base:dark_combat\","
		"\"headId\":\"base:head_dark_combat\",\"body\":0}]");
	REQUIRE(parse(numeric, document) == SCENARIO_CODEC_UNSUPPORTED_VERSION);
}

TEST_CASE("saved Scenario v2 retains typed migration input without profile ID",
	"[v006][b1068][scenario][migration]")
{
	const std::string v2 = R"({"version":2,"name":"Legacy typed","arenaId":"base:arena_mp_complex","scenarioId":"base:combat","arena":31,"scenario":0,"timelimit":9,"scorelimit":9,"teamscorelimit":400,"options":0,"weaponset":-1,"weapon_id0":"base:falcon2","weapon_id1":"","weapon_id2":"","weapon_id3":"","weapon_id4":"","weapon_id5":"","weapon0":1,"weapon1":0,"weapon2":0,"weapon3":0,"weapon4":0,"weapon5":0,"spawnWeaponId":"","spawnWeaponMode":1,"bots":[{"name":"NormalSim","difficulty":2,"bodyId":"base:dark_combat","headId":"base:head_dark_combat","body":0,"head":0}]})";
	scenario_document_t document{};
	REQUIRE(parse(v2, document) == SCENARIO_CODEC_OK);
	REQUIRE(document.version == 2);
	REQUIRE(document.bot_count == 1);
	REQUIRE((document.bots[0].present & SCENARIO_BOT_PROFILE_ID) == 0);
	REQUIRE((document.bots[0].present & SCENARIO_BOT_BODY_ID) != 0);
}

TEST_CASE("saved Scenario v2 admits only deliberate identity and companion shapes",
	"[v006][b1068][scenario][migration][authority]")
{
	const std::string numericOnly = R"({"version":2,"name":"Numeric migration","arenaId":"base:arena_mp_complex","arena":31,"scenario":0,"timelimit":9,"scorelimit":9,"teamscorelimit":400,"options":0,"weaponset":0,"bots":[{"name":"NormalSim","difficulty":2,"body":0,"head":0}]})";
	scenario_document_t document{};
	REQUIRE(parse(numericOnly, document) == SCENARIO_CODEC_OK);
	REQUIRE((document.bots[0].present & SCENARIO_BOT_BODY_ID) == 0);
	REQUIRE((document.bots[0].present & SCENARIO_BOT_LEGACY_BODY) != 0);

	std::string partialIdentity = numericOnly;
	const auto body = partialIdentity.find("\"body\":0");
	REQUIRE(body != std::string::npos);
	partialIdentity.replace(body, std::string("\"body\":0").size(),
		"\"body\":0,\"bodyId\":\"base:dark_combat\"");
	REQUIRE(parse(partialIdentity, document) == SCENARIO_CODEC_MISSING_FIELD);

	std::string profile = numericOnly;
	const auto bot = profile.find("\"name\":\"NormalSim\"");
	REQUIRE(bot != std::string::npos);
	profile.insert(bot, "\"profileId\":\"base:normal\",");
	REQUIRE(parse(profile, document) == SCENARIO_CODEC_UNSUPPORTED_VERSION);

	std::string partialWeapons = numericOnly;
	const auto bots = partialWeapons.find("\"bots\":");
	REQUIRE(bots != std::string::npos);
	partialWeapons.insert(bots, "\"weapon0\":1,");
	REQUIRE(parse(partialWeapons, document) == SCENARIO_CODEC_MISSING_FIELD);
}

TEST_CASE("saved Scenario v1 accepts only bounded legacy migration inputs",
	"[v006][b1068][scenario][migration]")
{
	const std::string v1 = R"({"version":1,"name":"Legacy numeric","arena":31,"scenario":0,"timelimit":9,"scorelimit":9,"teamscorelimit":400,"options":0,"weaponset":0,"bots":[{"name":"NormalSim","difficulty":2,"body":0,"head":0}]})";
	scenario_document_t document{};
	REQUIRE(parse(v1, document) == SCENARIO_CODEC_OK);
	REQUIRE(document.version == 1);
	REQUIRE((document.present & SCENARIO_DOC_ARENA_ID) == 0);
	REQUIRE(document.legacy_arena == 31);

	std::string invalid = v1;
	const auto difficulty = invalid.find("\"difficulty\":2");
	invalid.replace(difficulty, std::string("\"difficulty\":2").size(),
		"\"difficulty\":2147483648");
	REQUIRE(parse(invalid, document) == SCENARIO_CODEC_VALUE_OUT_OF_RANGE);

	invalid = v1;
	invalid.insert(1, "\"arenaId\":\"base:arena_mp_complex\",");
	REQUIRE(parse(invalid, document) == SCENARIO_CODEC_UNSUPPORTED_VERSION);
}
