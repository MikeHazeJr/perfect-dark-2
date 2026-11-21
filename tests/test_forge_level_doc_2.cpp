/*
 * Split out of tests/test_forge_level_doc.cpp for build time (T-TOOLING-018):
 * part 2 of 2 of its TEST_CASEs, in their original order, with
 * copies of the internal helpers they use. Link stubs stay in the original.
 */
/**
 * test_forge_level_doc.cpp -- the placements of a level, as Forge edits them
 * (port/include/forge_level_doc.h; step F2 of
 * context/designs/modding/forge-mode-scope.md).
 *
 * The document reads a level's pads, spawns, objects, pad bounds, object
 * fields and graph, changes them, and gives the six texts back. The cases
 * check the rules it promises: a row nobody touched keeps its bytes, nothing
 * is repaired, each new pad comes with its bounds row and
 * that row's node in the level's graph, and an id is never handed out twice.
 * One case saves an edited level through the mod author and checks what the
 * game checks before it loads one: the navigation record's counts and hashes
 * match the saved files, and the graph has one trigger volume node for each
 * bounds row.
 *
 * With PD2_BASE_TEMPLATE_DIR set (a folder of extracted base levels, local
 * only) the last case reads every base level and writes it back unchanged.
 * When PD2_TEST_ARTIFACT_DIR is set, the saved level is copied to
 * <dir>/clones for tools/asset_archive_conformance.py.
 */
#include "catch.hpp"

#include "asset_archive_policy.h"
#include "constants.h"
#include "forge_level_doc.h"
#include "forge_level_live.h"
#include "forge_room_builder.h"
#include "mission_graph_modules.h"
#include "mission_graph_runner.h"
#include "mod_author.h"
#include "modarchive.h"
#include "net/net_scenario_state_wire.h"
#include "scenario_intro.h"
#include "scenario_portal_source.h"
#include "scenario_stage_rules.h"
#include "sha256.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>


namespace {


namespace fs = std::filesystem;

std::string levelReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/* A member of an archive held in memory; empty when it has none by that name. */
std::string levelMember(const std::string &archive, const char *entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(), static_cast<u32>(archive.size()), entry, &size);
	if (!data) return std::string();
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

std::string levelSha(const std::string &bytes)
{
	u8 digest[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	sha256Hash(bytes.data(), bytes.size(), digest);
	sha256ToHex(digest, hex);
	return hex;
}

/* Three pads, two spawns, three objects and three bounds rows, in the
 * extractor's layout. The second spawn and the second object share pad_0001;
 * the third object stands on no pad; rows carry keys the module does not
 * know. */
const char *const kPads =
	"{\n"
	"  \"schema\": \"pd2.scenario.pads.v1\",\n"
	"  \"rows\": [\n"
	"    { \"pad_ref\": \"pad_0000\", \"room_ref\": \"\", \"liftnum\": 0, \"flags\": \"0x00185\", \"position\": [10.000000, 0.000000, 20.000000], \"up\": [0.000000, 1.000000, 0.000000], \"look\": [0.000000, 0.000000, -1.000000], \"bbox\": { \"min\": [-100.000000, -100.000000, -100.000000], \"max\": [100.000000, 100.000000, 100.000000] }, \"note\": \"first\" },\n"
	"    { \"pad_ref\": \"pad_0001\", \"room_ref\": \"\", \"liftnum\": 0, \"flags\": \"0x00025\", \"position\": [30.000000, 0.000000, 40.000000], \"up\": [0.000000, 1.000000, 0.000000], \"look\": [1.000000, 0.000000, 0.000000], \"bbox\": { \"min\": [-100.000000, -100.000000, -100.000000], \"max\": [100.000000, 100.000000, 100.000000] } },\n"
	"    { \"pad_ref\": \"pad_0002\", \"room_ref\": \"room_0003\", \"liftnum\": 0, \"flags\": \"0x00085\", \"position\": [50.000000, 8.000000, 60.000000], \"up\": [0.000000, 1.000000, 0.000000], \"look\": [0.000000, 0.000000, 1.000000], \"bbox\": { \"min\": [-100.000000, -100.000000, -100.000000], \"max\": [100.000000, 100.000000, 100.000000] } }\n"
	"  ]\n"
	"}\n";

const char *const kSpawns =
	"{\n"
	"  \"schema\": \"pd2.scenario.spawns.v1\",\n"
	"  \"rows\": [\n"
	"    { \"spawn_id\": \"spawn_0000\", \"pad_ref\": \"pad_0000\", \"room_ref\": \"\", \"team\": \"any\", \"profile\": \"intro\", \"position\": null, \"look\": null },\n"
	"    { \"spawn_id\": \"spawn_0004\", \"pad_ref\": \"pad_0001\", \"room_ref\": \"\", \"team\": \"team_1\", \"profile\": \"intro\", \"position\": null, \"look\": null, \"weight\": 2 }\n"
	"  ]\n"
	"}\n";

const char *const kObjects =
	"{\n"
	"  \"schema\": \"pd2.scenario.objects.v1\",\n"
	"  \"rows\": [\n"
	"    {\n"
	"      \"record_id\": \"setup_0000\",\n"
	"      \"kind\": \"weapon_pickup\",\n"
	"      \"pad_ref\": \"pad_0002\",\n"
	"      \"model_catalog_id\": \"\",\n"
	"      \"weapon_catalog_id\": \"base:falcon2\",\n"
	"      \"flags\": \"0x00000001\"\n"
	"    },\n"
	"    {\n"
	"      \"record_id\": \"setup_0001\",\n"
	"      \"kind\": \"prop\",\n"
	"      \"pad_ref\": \"pad_0001\",\n"
	"      \"model_catalog_id\": \"base:model_crate\",\n"
	"      \"weapon_catalog_id\": \"\",\n"
	"      \"flags\": \"0x00000000\"\n"
	"    },\n"
	"    {\n"
	"      \"record_id\": \"setup_0002\",\n"
	"      \"kind\": \"tag\",\n"
	"      \"pad_ref\": \"\",\n"
	"      \"model_catalog_id\": \"\",\n"
	"      \"weapon_catalog_id\": \"\",\n"
	"      \"flags\": \"0x00000000\"\n"
	"    }\n"
	"  ]\n"
	"}\n";

const char *const kVolumes =
	"{\n"
	"  \"schema\": \"pd2.scenario.volumes.v1\",\n"
	"  \"rows\": [\n"
	"    { \"volume_id\": \"volume_pad_0000\", \"pad_ref\": \"pad_0000\", \"kind\": \"pad_bounds\", \"room_ref\": \"\", \"shape\": \"aabb\", \"min\": [-90.000000, -100.000000, -80.000000], \"max\": [110.000000, 100.000000, 120.000000] },\n"
	"    { \"volume_id\": \"volume_pad_0001\", \"pad_ref\": \"pad_0001\", \"kind\": \"pad_bounds\", \"room_ref\": \"\", \"shape\": \"aabb\", \"min\": [-70.000000, -100.000000, -60.000000], \"max\": [130.000000, 100.000000, 140.000000] },\n"
	"    { \"volume_id\": \"volume_pad_0002\", \"pad_ref\": \"pad_0002\", \"kind\": \"pad_bounds\", \"room_ref\": \"\", \"shape\": \"aabb\", \"min\": [-50.000000, -92.000000, -40.000000], \"max\": [150.000000, 108.000000, 160.000000] }\n"
	"  ]\n"
	"}\n";

/* One row of setup.fields.json, laid out the way the extractor writes it. */
std::string fieldRow(const char *record, const char *kind, const char *field, const char *type,
	const std::string &value, const char *ref = "")
{
	return std::string("    {\n      \"record_id\": \"") + record + "\",\n      \"kind\": \"" + kind +
		"\",\n      \"field\": \"" + field + "\",\n      \"type\": \"" + type + "\",\n      \"value\": \"" +
		value + "\",\n      \"catalog_id\": \"\",\n      \"ref_record_id\": \"" + ref + "\"\n    }";
}

/* The fields of kObjects' three records: a weapon spot on pad_0002, a crate
 * on `cratePad`, and a tag that stands nowhere and points at the weapon spot.
 * `extra` is appended as further rows (each starting with ",\n"). */
std::string levelFields(const std::string &cratePad = "pad_0001", const std::string &extra = "")
{
	return std::string("{\n  \"schema\": \"pd2.scenario.setup.fields.v1\",\n  \"rows\": [\n") +
		fieldRow("setup_0000", "weapon_pickup", "command.order", "s32", "0") + ",\n" +
		fieldRow("setup_0000", "weapon_pickup", "base.pad", "pad_ref", "pad_0002") + ",\n" +
		fieldRow("setup_0000", "weapon_pickup", "pickup.weapon", "weapon_selector", "mp_location_00") + ",\n" +
		fieldRow("setup_0001", "prop", "command.order", "s32", "1") + ",\n" +
		fieldRow("setup_0001", "prop", "base.pad", "pad_ref", cratePad) + ",\n" +
		fieldRow("setup_0002", "tag", "command.order", "s32", "2") + ",\n" +
		fieldRow("setup_0002", "tag", "tag.target", "s32", "", "setup_0000") + extra +
		"\n  ]\n}\n";
}

/* A level with no field rows at all. */
const char *const kNoFields = "{\n  \"schema\": \"pd2.scenario.setup.fields.v1\",\n  \"rows\": [\n  ]\n}\n";

/* How many times `needle` stands in `text`. */
size_t occurrences(const std::string &text, const char *needle)
{
	size_t count = 0;
	const size_t length = strlen(needle);
	for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + length)) count++;
	return count;
}

/* The text the game counts in a level's graph: one per bounds row, or it refuses the level. */
const char *const kVolumeNode = "\"kind\": \"scenario.trigger.volume.source\"";

/*
 * A level's graph the way the extractor writes one, cut down to what the
 * document reads: a trigger volume node and a link to it for each of
 * `volumes` bounds rows, between other nodes and links, and the counts.
 * Bounds row n is volume_pad_n of pad_n, as in every base level, so the graph
 * of a level that gained a pad is this same text with one more of each.
 */
std::string levelGraph(size_t pads, size_t objects, size_t volumes)
{
	std::string nodes = "    { \"id\": \"scenario.load\", \"kind\": \"event.scenario.load\" },\n"
		"    { \"id\": \"source.scene\", \"kind\": \"scenario.scene.source\", \"source\": \"scene.glb\" },\n";
	std::string links = "    { \"from\": \"scenario.load\", \"to\": \"source.scene\" },\n";
	for (size_t i = 0; i < volumes; i++) {
		char number[16];
		snprintf(number, sizeof(number), "%04u", static_cast<unsigned>(i));
		nodes += std::string("    { \"id\": \"trigger.volume.") + number + "\", " + kVolumeNode +
			", \"table\": \"volumes.json\", \"volume\": \"volume_pad_" + number + "\", \"pad\": \"pad_" +
			number + "\" },\n";
		links += std::string("    { \"from\": \"scenario.load\", \"to\": \"trigger.volume.") + number + "\" },\n";
	}
	nodes += "    { \"id\": \"scenario.ai.lists\", \"kind\": \"scenario.ai.lists.source\", \"source\": \"ai/ailists.json\" }\n";
	links += "    { \"from\": \"scenario.load\", \"to\": \"scenario.ai.lists\" }\n";
	return "{\n  \"schema\": \"pd2.level.graph.v1\",\n  \"scenario\": \"example:tri_scenario\",\n  \"nodes\": [\n" +
		nodes + "  ],\n  \"links\": [\n" + links + "  ],\n  \"counts\": { \"rooms\": 2, \"triangles\": 1, \"pads\": " +
		std::to_string(pads) + ", \"volumes\": " + std::to_string(volumes) + ", \"objects\": " +
		std::to_string(objects) + ", \"objectives\": 0 }\n}\n";
}

struct Level {
	forge_level_doc_t *doc = nullptr;
	std::string error;

	/* Without a graph of its own a level gets the one that fits its tables:
	 * a node per bounds row, and counts that are true. */
	Level(const std::string &pads, const std::string &spawns, const std::string &objects,
		const std::string &volumes, const std::string &fields = kNoFields,
		const std::string &graph = std::string(), const std::string &intro = std::string(),
		const std::string &stage = std::string(), const std::string &covers = std::string(),
		const std::string &paths = std::string(), const std::string &rooms = std::string())
	{
		char err[256] = { 0 };
		forge_level_texts_t texts = {};
		const std::string fitted = graph.empty()
			? levelGraph(occurrences(pads, "\"pad_ref\""), occurrences(objects, "\"record_id\""),
				occurrences(volumes, "\"volume_id\""))
			: graph;
		const std::string *all[FORGE_LEVEL_MEMBER_COUNT] = {
			&pads, &spawns, &objects, &volumes, &fields, &fitted, &intro, &stage, &covers, &paths, &rooms
		};
		for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
			if (forgeLevelDocMemberOptional(member) && all[member]->empty()) continue;
			texts.text[member] = all[member]->data();
			texts.size[member] = all[member]->size();
		}
		doc = forgeLevelDocRead(&texts, err, sizeof(err));
		error = err;
	}
	Level() : Level(kPads, kSpawns, kObjects, kVolumes, levelFields()) {}
	~Level() { forgeLevelDocFree(doc); }
	Level(const Level &) = delete;
	Level &operator=(const Level &) = delete;

	std::string text(s32 member)
	{
		size_t size = 0;
		const char *t = forgeLevelDocMemberText(doc, member, &size);
		return t ? std::string(t, size) : std::string();
	}
};

/* The lines of a text; rows of the extractor's pads, spawns and volumes are one each. */
std::vector<std::string> linesOf(const std::string &text)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= text.size()) {
		const size_t end = text.find('\n', start);
		if (end == std::string::npos) {
			lines.push_back(text.substr(start));
			break;
		}
		lines.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return lines;
}

std::string replaced(std::string text, const std::string &from, const std::string &to)
{
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	return text.replace(at, from.size(), to);
}

const f32 kPlace[3] = { 100.0f, 0.0f, -250.5f };
const f32 kFacing[3] = { 0.0f, 0.0f, -4.0f };


/* kObjects with the second flags word the extractor writes for every object:
 * `weapon` on the weapon spot and `crate` on the crate. The tag keeps none. */
std::string objectsWithFlags(const char *weapon = "0x00000000", const char *crate = "0x00000020")
{
	const std::string withWeapon = replaced(kObjects, "\"flags\": \"0x00000001\"\n",
		std::string("\"flags\": \"0x00000001\",\n      \"flags2\": \"") + weapon + "\"\n");
	/* The first row whose flags are zero is the crate's; the tag's comes after it. */
	return replaced(withWeapon, "\"flags\": \"0x00000000\"\n",
		std::string("\"flags\": \"0x00000000\",\n      \"flags2\": \"") + crate + "\"\n");
}

/* levelFields() with the field that repeats each of those two words. */
std::string fieldsWithFlags(const char *weapon = "0x00000000", const char *crate = "0x00000020")
{
	return levelFields("pad_0001",
		",\n" + fieldRow("setup_0000", "weapon_pickup", "base.flags2", "u32_hex", weapon) +
		",\n" + fieldRow("setup_0001", "prop", "base.flags2", "u32_hex", crate));
}


/* The value of `key = value` in a descriptor's text, or "" when it has none. */
std::string iniValue(const std::string &text, const char *key)
{
	for (const std::string &line : linesOf(text)) {
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		const auto trim = [](std::string part) {
			const char *const space = " \t\r";
			const size_t first = part.find_first_not_of(space);
			if (first == std::string::npos) return std::string();
			return part.substr(first, part.find_last_not_of(space) - first + 1);
		};
		if (trim(line.substr(0, eq)) == key) return trim(line.substr(eq + 1));
	}
	return std::string();
}

/* What a saved level archive says about itself and the scenario inside it. */
struct SavedLevel {
	std::string arenaId;
	std::string arenaName;
	std::string arenaScenario;   /* the scenario id the arena names */
	std::string scenarioId;      /* the id the scenario gives itself */
	std::string scenario;        /* the scenario archive's bytes */
	std::string record;          /* its navigation record */
};

SavedLevel readSavedLevel(const char *archivePath, const char *scenarioMember)
{
	SavedLevel out;
	const std::string arena = levelReadFile(archivePath);
	REQUIRE(!arena.empty());
	const std::string arenaIni = levelMember(arena, "arena.ini");
	out.arenaId = iniValue(arenaIni, "catalog_id");
	out.arenaName = iniValue(arenaIni, "display_name");
	out.arenaScenario = iniValue(arenaIni, "scenario");
	out.scenario = levelMember(arena, scenarioMember);
	REQUIRE(!out.scenario.empty());
	out.scenarioId = iniValue(levelMember(out.scenario, "scenario.ini"), "catalog_id");
	out.record = levelMember(out.scenario, "_meta/generated-navmesh.json");
	return out;
}

TEST_CASE("A saved level is its own template: edited again, it is saved under its name or a new one",
          "[forge-level-doc]")
{
	/* How a level gets made: save it, play it, open it again, change it, save
	 * it again. The second save's template is the level itself. */
	const fs::path templatePath = fs::path(PD_SOURCE_DIR) /
		"examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";
	const char *const scenarioMember = "dependencies/assets/scenarios/tri_scenario.pdscenario";
	const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("pd2-forge-loop-" + std::to_string(stamp));
	fs::create_directories(root);
	const std::string rootText = root.string();
	char err[256] = { 0 };
	char id[32] = { 0 };

	/* Saves `templateFile` plus one more spawn as the level `name`. */
	const auto saveWithOneMoreSpawn = [&](const std::string &templateFile, const char *name,
			mod_author_result_t &result) {
		const std::string arena = levelReadFile(templateFile);
		REQUIRE(!arena.empty());
		const std::string scenario = levelMember(arena, scenarioMember);
		REQUIRE(!scenario.empty());
		/* The level's own graph, never a stand-in: what the document gives
		 * back for it is what the save writes into the level. */
		const std::string graph = levelMember(scenario, "level.graph.json");
		REQUIRE(!graph.empty());
		Level level(levelMember(scenario, "pads.json"), levelMember(scenario, "spawns.json"),
			levelMember(scenario, "objects.json"), levelMember(scenario, "volumes.json"),
			levelMember(scenario, "setup.fields.json"), graph);
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
		forge_level_overrides_t overrides;
		REQUIRE(forgeLevelDocOverrides(level.doc, scenarioMember, &overrides, err, sizeof(err)) == 1);
		REQUIRE(overrides.count == 4);
		mod_author_request_t request = {};
		request.template_path = templateFile.c_str();
		request.mods_root = rootText.c_str();
		request.display_name = name;
		request.tool = "pd-tests";
		request.overrides = overrides.entries;
		request.override_count = overrides.count;
		result = {};
		const s32 code = modAuthorSave(&request, &result, err, sizeof(err));
		INFO("save of '" << name << "' from " << templateFile << ": " << err);
		REQUIRE(code == MOD_AUTHOR_OK);
		char policy[256] = { 0 };
		const s32 valid = assetArchiveValidateFile(result.archive_path, ASSET_ARCHIVE_VALIDATE_RELEASE,
			policy, sizeof(policy));
		INFO("release validation: " << policy);
		CHECK(valid == 0);
	};
	const auto countsOf = [&](const SavedLevel &saved, s32 pads, s32 spawns) {
		/* Read with the saved level's own graph: the document refuses a level
		 * whose graph the game would refuse, so this is also the check that
		 * every save kept one trigger volume node for each bounds row. */
		const std::string graph = levelMember(saved.scenario, "level.graph.json");
		REQUIRE(!graph.empty());
		CHECK(occurrences(graph, kVolumeNode) == static_cast<size_t>(pads));
		CHECK(graph.find("\"pads\": " + std::to_string(pads) + ", \"volumes\": " + std::to_string(pads) +
			", \"objects\": 1,") != std::string::npos);
		Level level(levelMember(saved.scenario, "pads.json"), levelMember(saved.scenario, "spawns.json"),
			levelMember(saved.scenario, "objects.json"), levelMember(saved.scenario, "volumes.json"),
			levelMember(saved.scenario, "setup.fields.json"), graph);
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		CHECK(forgeLevelDocPadCount(level.doc) == pads);
		CHECK(forgeLevelDocVolumeCount(level.doc) == pads);
		CHECK(forgeLevelDocSpawnCount(level.doc) == spawns);
		CHECK(forgeLevelDocObjectCount(level.doc) == 1);
		const std::string counts = "\"source_counts\": { \"pads\": " + std::to_string(pads) +
			", \"volumes\": " + std::to_string(pads) + ", ";
		INFO(saved.record);
		CHECK(saved.record.find(counts) != std::string::npos);
		CHECK(saved.record.find("\"pads.json\": \"" + levelSha(levelMember(saved.scenario, "pads.json")) + "\"") !=
			std::string::npos);
		CHECK(saved.record.find("\"spawns.json\": \"" + levelSha(levelMember(saved.scenario, "spawns.json")) + "\"") !=
			std::string::npos);
	};

	/* First save, from the example level: one pad and one spawn become two. */
	mod_author_result_t first = {};
	saveWithOneMoreSpawn(templatePath.string(), "Forge Loop", first);
	CHECK(first.updated == 0);
	CHECK(std::string(first.catalog_id) == "mod:arena_forge_loop");
	const std::string firstArchive = first.archive_path;
	const SavedLevel one = readSavedLevel(first.archive_path, scenarioMember);
	CHECK(one.arenaId == "mod:arena_forge_loop");
	CHECK(one.arenaName == "Forge Loop");
	/* The scenario inside has an id under the level's, and the arena names it. */
	CHECK(one.scenarioId.rfind("mod:arena_forge_loop__", 0) == 0);
	CHECK(one.arenaScenario == one.scenarioId);
	countsOf(one, 2, 2);

	/* Second save: the level is its own template and keeps its name. It is
	 * updated where it stands, and no id in it changes. */
	mod_author_result_t second = {};
	saveWithOneMoreSpawn(firstArchive, "Forge Loop", second);
	CHECK(second.updated == 1);
	CHECK(std::string(second.catalog_id) == "mod:arena_forge_loop");
	CHECK(std::string(second.archive_path) == firstArchive);
	const SavedLevel two = readSavedLevel(second.archive_path, scenarioMember);
	CHECK(two.arenaId == one.arenaId);
	CHECK(two.arenaName == "Forge Loop");
	CHECK(two.scenarioId == one.scenarioId);
	CHECK(two.arenaScenario == one.scenarioId);
	countsOf(two, 3, 3);
	/* Nothing of the save is left beside it. */
	CHECK(!fs::exists(firstArchive + ASSET_ARCHIVE_CLONE_STAGING_SUFFIX));

	/* Third save: the same level under a new name is a second level. Its
	 * scenario takes the new level's id and keeps the rest of its own. */
	mod_author_result_t third = {};
	saveWithOneMoreSpawn(firstArchive, "Forge Loop Two", third);
	CHECK(third.updated == 0);
	CHECK(std::string(third.catalog_id) == "mod:arena_forge_loop_two");
	CHECK(std::string(third.archive_path) != firstArchive);
	const SavedLevel three = readSavedLevel(third.archive_path, scenarioMember);
	CHECK(three.arenaId == "mod:arena_forge_loop_two");
	CHECK(three.arenaName == "Forge Loop Two");
	CHECK(three.arenaScenario == three.scenarioId);
	CHECK(three.scenarioId == "mod:arena_forge_loop_two__" +
		one.scenarioId.substr(strlen("mod:arena_forge_loop__")));
	countsOf(three, 4, 4);
	/* The level it was made from is as the second save left it. */
	const SavedLevel still = readSavedLevel(firstArchive.c_str(), scenarioMember);
	CHECK(still.scenario == two.scenario);

	const char *artifacts = std::getenv("PD2_TEST_ARTIFACT_DIR");
	if (artifacts && artifacts[0]) {
		std::error_code copyError;
		const fs::path dir = fs::path(artifacts) / "clones";
		fs::create_directories(dir, copyError);
		for (const std::string &archive : { firstArchive, std::string(third.archive_path) }) {
			fs::copy_file(archive, dir / fs::path(archive).filename(),
				fs::copy_options::overwrite_existing, copyError);
		}
	}

	std::error_code ec;
	fs::remove_all(root, ec);
}

TEST_CASE("A level saved over itself is written whole, so an edit taken back since is taken back in the file",
          "[forge-level-doc]")
{
	/* One sitting in the editor: open a level of the player's own, add a
	 * start, save, take the start back out, save again. Both saves keep the
	 * level's name, so each replaces the archive the document was read from. */
	const fs::path templatePath = fs::path(PD_SOURCE_DIR) /
		"examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";
	const char *const scenarioMember = "dependencies/assets/scenarios/tri_scenario.pdscenario";
	const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("pd2-forge-whole-" + std::to_string(stamp));
	fs::create_directories(root);
	const std::string rootText = root.string();
	char err[256] = { 0 };
	char id[32] = { 0 };

	const auto save = [&](const std::string &templateFile, const forge_level_overrides_t &overrides,
			mod_author_result_t &result) {
		mod_author_request_t request = {};
		request.template_path = templateFile.c_str();
		request.mods_root = rootText.c_str();
		request.display_name = "Forge Whole";
		request.tool = "pd-tests";
		request.overrides = overrides.entries;
		request.override_count = overrides.count;
		result = {};
		const s32 code = modAuthorSave(&request, &result, err, sizeof(err));
		INFO("save from " << templateFile << ": " << err);
		REQUIRE(code == MOD_AUTHOR_OK);
	};
	const auto spawnsIn = [&](const std::string &archive) {
		const SavedLevel saved = readSavedLevel(archive.c_str(), scenarioMember);
		Level level(levelMember(saved.scenario, "pads.json"), levelMember(saved.scenario, "spawns.json"),
			levelMember(saved.scenario, "objects.json"), levelMember(saved.scenario, "volumes.json"),
			levelMember(saved.scenario, "setup.fields.json"), levelMember(saved.scenario, "level.graph.json"));
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		return forgeLevelDocSpawnCount(level.doc);
	};

	/* The player's own level, made from the example with nothing changed. */
	forge_level_overrides_t none = {};
	mod_author_result_t made = {};
	save(templatePath.string(), none, made);
	const std::string own = made.archive_path;
	CHECK(std::string(made.catalog_id) == "mod:arena_forge_whole");
	CHECK(spawnsIn(own) == 1);

	/* Opened once, and kept open across both saves, as the editor keeps it. */
	const SavedLevel opened = readSavedLevel(own.c_str(), scenarioMember);
	Level level(levelMember(opened.scenario, "pads.json"), levelMember(opened.scenario, "spawns.json"),
		levelMember(opened.scenario, "objects.json"), levelMember(opened.scenario, "volumes.json"),
		levelMember(opened.scenario, "setup.fields.json"), levelMember(opened.scenario, "level.graph.json"));
	INFO(level.error);
	REQUIRE(level.doc != nullptr);

	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
	forge_level_overrides_t whole;
	REQUIRE(forgeLevelDocOverridesAll(level.doc, scenarioMember, &whole, err, sizeof(err)) == 1);
	/* Every member, in the order of the members, changed or not. */
	REQUIRE(whole.count == FORGE_LEVEL_MEMBER_INTRO); // This template has no optional members.
	for (s32 member = 0; member < whole.count; member++) {
		CHECK(std::string(whole.entries[member].entry) ==
			std::string(scenarioMember) + "::" + forgeLevelDocMemberName(member));
		CHECK(std::string(static_cast<const char *>(whole.entries[member].data), whole.entries[member].len) ==
			level.text(member));
	}
	mod_author_result_t first = {};
	save(own, whole, first);
	CHECK(first.updated == 1);
	CHECK(std::string(first.archive_path) == own);
	CHECK(spawnsIn(own) == 2);

	/* The start is taken back out. The spawns are now the text that was read
	 * when the level was opened, so that member does not count as changed,
	 * and a save of the changed members alone leaves the file's two starts
	 * where they are: the level on disk no longer is what was read. */
	REQUIRE(forgeLevelDocRemoveSpawn(level.doc, id, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SPAWNS) == 0);
	forge_level_overrides_t changed;
	REQUIRE(forgeLevelDocOverrides(level.doc, scenarioMember, &changed, err, sizeof(err)) == 1);
	REQUIRE(changed.count == 3);   /* the pads, the bounds and the graph */
	mod_author_result_t stale = {};
	save(own, changed, stale);
	CHECK(spawnsIn(own) == 2);

	/* Written whole, the file is what the document says: one start. */
	REQUIRE(forgeLevelDocOverridesAll(level.doc, scenarioMember, &whole, err, sizeof(err)) == 1);
	mod_author_result_t second = {};
	save(own, whole, second);
	CHECK(second.updated == 1);
	CHECK(spawnsIn(own) == 1);
	CHECK(forgeLevelDocOverridesAll(nullptr, scenarioMember, &whole, err, sizeof(err)) == 0);
	CHECK(whole.count == 0);

	std::error_code ec;
	fs::remove_all(root, ec);
}


/* One row of objects.json, on one line. */
std::string armouryObject(const char *record, const char *kind, const char *pad, const char *model,
	const char *weapon)
{
	return std::string("    { \"record_id\": \"") + record + "\", \"kind\": \"" + kind + "\", \"pad_ref\": \"" + pad +
		"\", \"model_catalog_id\": \"" + model + "\", \"weapon_catalog_id\": \"" + weapon +
		"\", \"secondary_weapon_catalog_id\": \"\", \"flags\": \"0x00000001\", \"flags2\": \"0x00000000\" }";
}

/* One row of setup.fields.json that names a catalog asset, as the extractor writes it. */
std::string catalogRow(const char *record, const char *kind, const char *field, const char *type,
	const char *catalog)
{
	return std::string("    {\n      \"record_id\": \"") + record + "\",\n      \"kind\": \"" + kind +
		"\",\n      \"field\": \"" + field + "\",\n      \"type\": \"" + type +
		"\",\n      \"value\": \"\",\n      \"catalog_id\": \"" + catalog + "\",\n      \"ref_record_id\": \"\"\n    }";
}

/*
 * Five objects on kPads' three pads, written the two ways base levels write
 * what a pickup gives:
 *   setup_0000  a weapon place of the match (mp_location_03), drawn as the stand-in model
 *   setup_0001  a pickup of a catalog weapon, with that weapon's own model
 *   setup_0002  a plain prop
 *   setup_0003  a door, which stands on no pad
 *   setup_0004  a pickup whose two files disagree about its weapon
 */
std::string armouryObjects()
{
	return std::string("{\n  \"schema\": \"pd2.scenario.objects.v1\",\n  \"rows\": [\n") +
		armouryObject("setup_0000", "weapon_pickup", "pad_0002", "base:model_roofgun", "") + ",\n" +
		armouryObject("setup_0001", "weapon_pickup", "pad_0001", "base:model_chrdragon", "base:dragon") + ",\n" +
		armouryObject("setup_0002", "prop", "pad_0000", "base:model_crate", "") + ",\n" +
		armouryObject("setup_0003", "door", "", "base:model_door", "") + ",\n" +
		armouryObject("setup_0004", "weapon_pickup", "pad_0000", "base:model_roofgun", "base:falcon2") +
		"\n  ]\n}\n";
}

std::string armouryFields()
{
	std::string rows;
	const auto record = [&](const char *id, const char *kind, const char *order, const char *scale,
			const char *model, const char *pad) {
		if (!rows.empty()) rows += ",\n";
		rows += fieldRow(id, kind, "command.order", "s32", order) + ",\n" +
			fieldRow(id, kind, "base.extra_scale", "s32", scale) + ",\n" +
			catalogRow(id, kind, "base.model", "model_catalog_id", model);
		if (pad[0]) rows += ",\n" + fieldRow(id, kind, "base.pad", "pad_ref", pad);
	};
	record("setup_0000", "weapon_pickup", "0", "512", "base:model_roofgun", "pad_0002");
	rows += ",\n" + fieldRow("setup_0000", "weapon_pickup", "pickup.weapon", "weapon_selector", "mp_location_03");
	record("setup_0001", "weapon_pickup", "1", "256", "base:model_chrdragon", "pad_0001");
	rows += ",\n" + catalogRow("setup_0001", "weapon_pickup", "pickup.weapon", "weapon_catalog_id", "base:dragon");
	record("setup_0002", "prop", "2", "256", "base:model_crate", "pad_0000");
	record("setup_0003", "door", "3", "256", "base:model_door", "");
	record("setup_0004", "weapon_pickup", "4", "512", "base:model_roofgun", "pad_0000");
	rows += ",\n" + fieldRow("setup_0004", "weapon_pickup", "pickup.weapon", "weapon_selector", "mp_location_00");
	return "{\n  \"schema\": \"pd2.scenario.setup.fields.v1\",\n  \"rows\": [\n" + rows + "\n  ]\n}\n";
}

/* The object `id` as the document reports it. */
forge_level_object_t armouryFind(const forge_level_doc_t *doc, const char *id)
{
	forge_level_object_t object = {};
	for (s32 i = 0; i < forgeLevelDocObjectCount(doc); i++) {
		REQUIRE(forgeLevelDocGetObject(doc, i, &object) == 1);
		if (std::string(object.record_id) == id) return object;
	}
	FAIL("the level has no " << id);
	return object;
}

TEST_CASE("Moving an object off a pad used by a door trigger relinks the native setup",
          "[forge-level-doc][level][graph][volume]")
{
	const std::string objects = replaced(armouryObjects(),
		armouryObject("setup_0003", "door", "", "base:model_door", ""),
		armouryObject("setup_0003", "door", "pad_0001", "base:model_door", ""));
	const std::string model = catalogRow("setup_0003", "door", "base.model", "model_catalog_id", "base:model_door");
	const std::string fields = replaced(armouryFields(), model, model + ",\n" +
		fieldRow("setup_0003", "door", "base.pad", "pad_ref", "pad_0001"));
	const std::string graph = replaced(levelGraph(3, 5, 3), "  \"counts\":",
		"  \"edges\": [ { \"from\": \"scenario.load\", \"to\": \"source.scene\" } ],\n  \"counts\":");
	Level level(kPads, kSpawns, objects, kVolumes, fields, graph);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);

	char err[256] = {};
	char id[32] = {};
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0,
		kPlace, kFacing, id, sizeof(id), err, sizeof(err)));
	const forge_level_object_t original = armouryFind(level.doc, id);
	const f32 halfSize[] = {25.0f, 40.0f, 60.0f};
	REQUIRE(forgeLevelDocAddDoorTrigger(level.doc, original.pad_ref, "setup_0003",
		halfSize, err, sizeof(err)));
	forge_level_door_trigger_t originalTrigger = {};
	REQUIRE(forgeLevelDocGetDoorTrigger(level.doc, 0, &originalTrigger));

	const f32 position[] = {140.0f, 0.0f, -200.0f};
	REQUIRE(forgeLevelDocMoveObject(level.doc, id, position, kFacing, err, sizeof(err)));
	const forge_level_object_t moved = armouryFind(level.doc, id);
	REQUIRE(std::string(moved.pad_ref) != original.pad_ref);
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocFindPad(level.doc, original.pad_ref, &pad));
	CHECK(pad.position[0] == kPlace[0]);
	REQUIRE(forgeLevelDocFindPad(level.doc, moved.pad_ref, &pad));
	CHECK(pad.position[0] == position[0]);

	Level reopened(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
		level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS), level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(reopened.error);
	REQUIRE(reopened.doc != nullptr);
	const forge_level_object_t saved = armouryFind(reopened.doc, id);
	CHECK(std::string(saved.pad_ref) == moved.pad_ref);
	CHECK(saved.movable == 1);
	forge_level_door_trigger_t trigger = {};
	REQUIRE(forgeLevelDocGetDoorTrigger(reopened.doc, 0, &trigger));
	CHECK(std::string(trigger.pad_ref) == original.pad_ref);
	CHECK(std::string(trigger.volume_id) == originalTrigger.volume_id);
	CHECK(std::string(trigger.door_id) == "setup_0003");
}

TEST_CASE("Grid door triggers round-trip native volumes and run the level graph action",
          "[forge-level-doc][level][graph][volume]")
{
	const std::string objects = replaced(armouryObjects(),
		armouryObject("setup_0003", "door", "", "base:model_door", ""),
		armouryObject("setup_0003", "door", "pad_0001", "base:model_door", ""));
	const std::string model = catalogRow("setup_0003", "door", "base.model", "model_catalog_id", "base:model_door");
	const std::string fields = replaced(armouryFields(), model, model + ",\n" +
		fieldRow("setup_0003", "door", "base.pad", "pad_ref", "pad_0001"));
	const std::string graph = replaced(levelGraph(3, 5, 3), "  \"counts\":",
		"  \"edges\": [ { \"from\": \"scenario.load\", \"to\": \"source.scene\" } ],\n  \"counts\":");
	Level level(kPads, kSpawns, objects, kVolumes, fields, graph);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = {};
	const f32 halfSize[] = {25.0f, 40.0f, 60.0f};
	const f32 invalid[] = {25.0f, 0.0f, 60.0f};
	CHECK(forgeLevelDocAddDoorTrigger(level.doc, "pad_0009", "setup_0003", halfSize, err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddDoorTrigger(level.doc, "pad_0000", "setup_0002", halfSize, err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddDoorTrigger(level.doc, "pad_0000", "setup_0003", invalid, err, sizeof(err)) == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == kVolumes);
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == graph);

	REQUIRE(forgeLevelDocAddDoorTrigger(level.doc, "pad_0000", "setup_0003", halfSize, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocAddDoorTrigger(level.doc, "pad_0000", "setup_0003", halfSize, err, sizeof(err)) == 0);
	REQUIRE(forgeLevelDocDoorTriggerCount(level.doc) == 1);
	forge_level_door_trigger_t trigger = {};
	REQUIRE(forgeLevelDocGetDoorTrigger(level.doc, 0, &trigger) == 1);
	CHECK(std::string(trigger.pad_ref) == "pad_0000");
	CHECK(std::string(trigger.door_id) == "setup_0003");
	const std::string volumes = level.text(FORGE_LEVEL_MEMBER_VOLUMES);
	const std::string authored = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
	CHECK(volumes.find("\"min\": [-15.000000, -40.000000, -40.000000]") != std::string::npos);
	CHECK(volumes.find("\"max\": [35.000000, 40.000000, 80.000000]") != std::string::npos);
	CHECK(occurrences(authored, kVolumeNode) == 4);
	CHECK(authored.find("\"volumes\": 4") != std::string::npos);
	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, nullptr, &overrides, err, sizeof(err)) == 1);
	REQUIRE(overrides.count == 2);
	CHECK(std::string(overrides.entries[0].entry) == "volumes.json");
	CHECK(std::string(overrides.entries[1].entry) == "level.graph.json");

	const auto knowsDoor = [](const char *kind) -> s32 { return strcmp(kind, "og.door.open") == 0; };
	std::unique_ptr<mission_graph_program_t, decltype(&missionGraphProgramFree)> program(
		missionGraphProgramCompileLevel(authored.data(), authored.size(), knowsDoor, err, sizeof(err)),
		missionGraphProgramFree);
	INFO(err);
	REQUIRE(program != nullptr);
	REQUIRE(missionGraphProgramVolumeCount(program.get()) == 1);
	CHECK(std::string(missionGraphProgramVolumeAt(program.get(), 0)) == trigger.volume_id);
	const auto record = [](const char *kind, const char *, const mission_graph_params_t *params,
			const mission_graph_event_t *, void *out) -> s32 {
		if (strcmp(kind, "og.door.open") != 0) return 0;
		const char *door = missionGraphParamText(params, "object");
		if (!door) return 0;
		*static_cast<std::string *>(out) = door;
		return 1;
	};
	mission_graph_event_t event = {};
	event.kind = MISSION_GRAPH_EVENT_VOLUME_ENTERED;
	event.player = 0;
	event.volume = "volume_pad_0000";
	std::string opened;
	CHECK(missionGraphProgramFire(program.get(), &event, record, &opened) == 0);
	event.volume = trigger.volume_id;
	CHECK(missionGraphProgramFire(program.get(), &event, record, &opened) == 1);
	CHECK(opened == "setup_0003");

	Level loaded(kPads, kSpawns, objects, volumes, fields, authored);
	INFO(loaded.error);
	REQUIRE(loaded.doc != nullptr);
	CHECK(forgeLevelDocDoorTriggerCount(loaded.doc) == 1);
	CHECK(forgeLevelDocMemberChanged(loaded.doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == 0);
	Level original(kPads, kSpawns, objects, kVolumes, fields, graph);
	REQUIRE(original.doc != nullptr);
	std::unique_ptr<FILE, decltype(&fclose)> stream(tmpfile(), fclose);
	REQUIRE(stream != nullptr);
	const std::string digest(64, 'b');
	REQUIRE(forgeLevelDocWriteCheckpoint(level.doc, digest.c_str(), stream.get(), err, sizeof(err)) == 1);
	const long size = ftell(stream.get());
	REQUIRE(size > 0);
	rewind(stream.get());
	std::string checkpoint(static_cast<size_t>(size), '\0');
	REQUIRE(fread(checkpoint.data(), 1, checkpoint.size(), stream.get()) == checkpoint.size());
	std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)> recovered(
		forgeLevelDocReadCheckpoint(checkpoint.data(), checkpoint.size(), digest.c_str(),
			original.doc, err, sizeof(err)), forgeLevelDocFree);
	REQUIRE(recovered != nullptr);
	CHECK(forgeLevelDocDoorTriggerCount(recovered.get()) == 1);
	CHECK(forgeLevelDocMemberChanged(recovered.get(), FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == 1);
	CHECK(forgeLevelDocMemberChanged(recovered.get(), FORGE_LEVEL_MEMBER_VOLUMES) == 1);
	REQUIRE(forgeLevelDocRemoveDoorTrigger(loaded.doc, 0, err, sizeof(err)) == 1);
	CHECK(loaded.text(FORGE_LEVEL_MEMBER_VOLUMES) == kVolumes);
	CHECK(loaded.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == graph);
	REQUIRE(forgeLevelDocRemoveDoorTrigger(level.doc, 0, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_VOLUMES) == 0);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == 0);
}

TEST_CASE("Grid fixed pickup triggers preserve a pad and coexist with native door triggers",
	"[forge-level-doc][level][graph][volume]")
{
	constexpr s32 give = FORGE_LEVEL_TRIGGER_GIVE_PICKUP;
	const std::string objects = replaced(armouryObjects(),
		armouryObject("setup_0003", "door", "", "base:model_door", ""),
		armouryObject("setup_0003", "door", "pad_0001", "base:model_door", ""));
	const std::string door = catalogRow("setup_0003", "door", "base.model", "model_catalog_id", "base:model_door");
	const std::string weapon = catalogRow("setup_0001", "weapon_pickup", "pickup.weapon", "weapon_catalog_id", "base:dragon");
	std::string fields = replaced(armouryFields(), door, door + ",\n" +
		fieldRow("setup_0003", "door", "base.pad", "pad_ref", "pad_0001"));
	fields = replaced(fields, weapon, weapon + ",\n" +
		fieldRow("setup_0001", "weapon_pickup", "base.flags2", "u32_hex", "0x00000000"));
	const std::string graph = replaced(levelGraph(3, 5, 3), "  \"counts\":",
		"  \"edges\": [ { \"from\": \"scenario.load\", \"to\": \"source.scene\" } ],\n  \"counts\":");
	Level level(kPads, kSpawns, objects, kVolumes, fields, graph);
	INFO(level.error);
	REQUIRE(level.doc);
	const f32 half[] = {25.0f, 40.0f, 60.0f};
	char err[256] = {}, addedStart[32] = {};
	for (const char *ref : {"setup_0000", "setup_0002", "setup_0003", "setup_0004", "setup_0999"}) {
		CAPTURE(ref);
		CHECK_FALSE(forgeLevelDocAddTrigger(level.doc, give, "pad_0002", ref, half, err, sizeof(err)));
	}
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocAddTrigger(level.doc, give, "pad_0002", "setup_0001", half, err, sizeof(err)));
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 0, err, sizeof(err)));
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == kVolumes);
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == graph);

	REQUIRE(forgeLevelDocAddTrigger(level.doc, give, "pad_0002", "setup_0001", half, err, sizeof(err)));
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES).find(
		"\"pad_ref\": \"pad_0002\", \"kind\": \"pad_bounds\", \"room_ref\": \"room_0003\"") != std::string::npos);
	REQUIRE(forgeLevelDocAddDoorTrigger(level.doc, "pad_0000", "setup_0003", half, err, sizeof(err)));
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, "any", addedStart, sizeof(addedStart), err, sizeof(err)));
	REQUIRE(forgeLevelDocAddTrigger(level.doc, give, "pad_0003", "setup_0001", half, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocAddTrigger(level.doc, give, "pad_0003", "setup_0001", half, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocSetPickupSlot(level.doc, "setup_0001", 0, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocSetPickupWeapon(level.doc, "setup_0001", "base:briefcase2",
		"base:model_roofgun", 256, err, sizeof(err)));
	const f32 moved[] = {kPlace[0] + 80.0f, kPlace[1], kPlace[2]};
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, addedStart, moved, kFacing, err, sizeof(err)));
	forge_level_pad_t anchored = {};
	REQUIRE(forgeLevelDocFindPad(level.doc, "pad_0003", &anchored));
	CHECK(anchored.position[0] == kPlace[0]);

	const std::string authored = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
	Level reopened(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
		level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS), authored);
	INFO(reopened.error);
	REQUIRE(reopened.doc);
	REQUIRE(forgeLevelDocTriggerCount(reopened.doc) == 3);
	REQUIRE(forgeLevelDocDoorTriggerCount(reopened.doc) == 1);
	forge_level_trigger_t trigger = {};
	REQUIRE(forgeLevelDocGetTrigger(reopened.doc, 2, &trigger));
	CHECK(trigger.action == give);
	CHECK(std::string(trigger.pad_ref) == "pad_0003");
	CHECK(std::string(trigger.target_id) == "setup_0001");
	forge_level_door_trigger_t oldDoor = {};
	REQUIRE(forgeLevelDocGetDoorTrigger(reopened.doc, 0, &oldDoor));
	CHECK(std::string(oldDoor.door_id) == "setup_0003");

	const auto known = [](const char *kind) -> s32 {
		return strcmp(kind, "og.pickup.give") == 0 || strcmp(kind, "og.door.open") == 0;
	};
	std::unique_ptr<mission_graph_program_t, decltype(&missionGraphProgramFree)> program(
		missionGraphProgramCompileLevel(authored.data(), authored.size(), known, err, sizeof(err)),
		missionGraphProgramFree);
	REQUIRE(program);
	const auto givePickup = [](const char *kind, const char *, const mission_graph_params_t *params,
			const mission_graph_event_t *event, void *out) -> s32 {
		const char *ref = missionGraphParamText(params, "object");
		if (strcmp(kind, "og.pickup.give") != 0 || !ref || event->player != 1) return 0;
		*static_cast<std::string *>(out) = ref;
		return 1;
	};
	mission_graph_event_t event = {};
	event.kind = MISSION_GRAPH_EVENT_VOLUME_ENTERED;
	event.player = 1;
	event.volume = trigger.volume_id;
	std::string granted;
	CHECK(missionGraphProgramFire(program.get(), &event, givePickup, &granted) == 1);
	CHECK(granted == "setup_0001");
	REQUIRE(forgeLevelDocRemoveTrigger(reopened.doc, 2, err, sizeof(err)));
	REQUIRE(forgeLevelDocRemoveTrigger(reopened.doc, 0, err, sizeof(err)));
	CHECK(forgeLevelDocDoorTriggerCount(reopened.doc) == 1);
	CHECK(forgeLevelDocTriggerCount(reopened.doc) == 1);
	CHECK(reopened.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH).find("og.pickup.give") == std::string::npos);
	REQUIRE(forgeLevelDocSetPickupSlot(reopened.doc, "setup_0001", 0, err, sizeof(err)));
}

TEST_CASE("Grid edits native trigger anchor, size and target without replacing its identity",
	"[forge-level-doc][level][graph][volume]")
{
	const std::string objects = replaced(replaced(armouryObjects(),
		armouryObject("setup_0000", "weapon_pickup", "pad_0002", "base:model_roofgun", ""),
		armouryObject("setup_0000", "weapon_pickup", "pad_0002", "base:model_roofgun", "base:falcon2")),
		armouryObject("setup_0003", "door", "", "base:model_door", ""),
		armouryObject("setup_0003", "door", "pad_0001", "base:model_door", ""));
	const std::string door = catalogRow("setup_0003", "door", "base.model", "model_catalog_id", "base:model_door");
	const std::string pickup = catalogRow("setup_0001", "weapon_pickup", "pickup.weapon", "weapon_catalog_id", "base:dragon");
	std::string fields = replaced(armouryFields(), door, door + ",\n" +
		fieldRow("setup_0003", "door", "base.pad", "pad_ref", "pad_0001"));
	fields = replaced(fields, pickup, pickup + ",\n" +
		fieldRow("setup_0001", "weapon_pickup", "base.flags2", "u32_hex", "0x00000000"));
	fields = replaced(fields, fieldRow("setup_0000", "weapon_pickup", "pickup.weapon", "weapon_selector", "mp_location_03"),
		catalogRow("setup_0000", "weapon_pickup", "pickup.weapon", "weapon_catalog_id", "base:falcon2") + ",\n" +
		fieldRow("setup_0000", "weapon_pickup", "base.flags2", "u32_hex", "0x00000000"));
	const std::string initialGraph = replaced(levelGraph(3, 5, 3), "  \"counts\":",
		"  \"edges\": [ { \"from\": \"scenario.load\", \"to\": \"source.scene\" } ],\n  \"counts\":");
	Level initial(kPads, kSpawns, objects, kVolumes, fields, initialGraph);
	INFO(initial.error);
	REQUIRE(initial.doc);
	char err[256] = {};
	const f32 half[] = {25.0f, 40.0f, 60.0f};
	const f32 bigger[] = {32.0f, 48.0f, 64.0f};
	const f32 invalid[] = {32.0f, std::numeric_limits<f32>::quiet_NaN(), 64.0f};
	REQUIRE(forgeLevelDocAddTrigger(initial.doc, FORGE_LEVEL_TRIGGER_GIVE_PICKUP,
		"pad_0002", "setup_0001", half, err, sizeof(err)));
	forge_level_trigger_t pickupTrigger = {};
	REQUIRE(forgeLevelDocGetTrigger(initial.doc, 0, &pickupTrigger));
	REQUIRE(forgeLevelDocAddDoorTrigger(initial.doc, "pad_0000", "setup_0003", half, err, sizeof(err)));
	char destinationStart[32] = {};
	REQUIRE(forgeLevelDocAddSpawn(initial.doc, kPlace, kFacing, "any", destinationStart, sizeof(destinationStart), err, sizeof(err)));
	REQUIRE(forgeLevelDocAddTrigger(initial.doc, FORGE_LEVEL_TRIGGER_GIVE_PICKUP,
		"pad_0003", "setup_0001", half, err, sizeof(err)));
	const std::string pickupAction = std::string("\"id\": \"grid.trigger.") + pickupTrigger.volume_id +
		".give\", \"kind\": \"og.pickup.give\", \"object\": \"setup_0001\"";
	const std::string originalBox = "\"min\": [25.000000, -32.000000, 0.000000], \"max\": [75.000000, 48.000000, 120.000000]";
	const std::string volumes = replaced(initial.text(FORGE_LEVEL_MEMBER_VOLUMES), originalBox,
		"\"note\": \"keep this volume field\", " + originalBox);
	const std::string graph = replaced(initial.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH), pickupAction,
		pickupAction + ", \"note\": \"keep this action field\"");
	const std::string pads = initial.text(FORGE_LEVEL_MEMBER_PADS);
	const std::string spawns = initial.text(FORGE_LEVEL_MEMBER_SPAWNS);
	Level level(pads, spawns, objects, volumes, fields, graph);
	INFO(level.error);
	REQUIRE(level.doc);
	forge_level_trigger_t doorTrigger = {}, destinationTrigger = {};
	REQUIRE(forgeLevelDocGetTrigger(level.doc, 0, &pickupTrigger));
	REQUIRE(forgeLevelDocGetTrigger(level.doc, 1, &doorTrigger));
	REQUIRE(forgeLevelDocGetTrigger(level.doc, 2, &destinationTrigger));
	REQUIRE(pickupTrigger.editable);
	REQUIRE(doorTrigger.editable);
	CHECK(pickupTrigger.half_size[0] == half[0]);
	const auto volumeRow = [](const std::string &text, const char *id) {
		const size_t start = text.find(std::string("{ \"volume_id\": \"") + id + "\"");
		const size_t end = text.find('}', start);
		return end == std::string::npos ? std::string() : text.substr(start, end - start + 1);
	};
	const std::string previousPrimary = volumeRow(volumes, "volume_pad_0002");
	const std::string destinationPrimary = volumeRow(volumes, "volume_pad_0003");
	const std::string pickupVolume = volumeRow(volumes, pickupTrigger.volume_id);
	REQUIRE_FALSE(previousPrimary.empty());
	REQUIRE_FALSE(destinationPrimary.empty());
	REQUIRE_FALSE(pickupVolume.empty());
	REQUIRE(volumes.find(pickupVolume) < volumes.find(destinationPrimary));
	const auto update = [&](const char *id, const char *pad, const char *target, const f32 size[3]) {
		return forgeLevelDocUpdateTrigger(level.doc, id, pad, target, size, err, sizeof(err));
	};
	CHECK_FALSE(update("volume_pad_0002", "pad_0003", "setup_0000", bigger));
	CHECK_FALSE(update(pickupTrigger.volume_id, "pad_0999", "setup_0000", bigger));
	CHECK_FALSE(update(pickupTrigger.volume_id, "pad_0003", "setup_0003", bigger));
	CHECK_FALSE(update(pickupTrigger.volume_id, "pad_0003", "setup_0001", bigger));
	CHECK_FALSE(update(pickupTrigger.volume_id, "pad_0003", "setup_0000", invalid));
	CHECK_FALSE(update(pickupTrigger.volume_id, pickupTrigger.pad_ref, "setup_0001", half));
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == volumes);
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == graph);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);

	REQUIRE(update(pickupTrigger.volume_id, "pad_0003", "setup_0000", bigger));
	const std::string sourceVolume = std::string("\"volume\": \"") + pickupTrigger.volume_id + "\", \"pad\": \"";
	const std::string editedAction = replaced(pickupAction, "\"object\": \"setup_0001\"", "\"object\": \"setup_0000\"");
	const std::string editedGraph = replaced(replaced(graph, pickupAction, editedAction),
		sourceVolume + "pad_0002\"", sourceVolume + "pad_0003\"");
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == editedGraph);
	/* The first move can shift the following volume rows. Updating the door
	 * by its original ID must address its own bounds. */
	REQUIRE(update(doorTrigger.volume_id, doorTrigger.pad_ref, "setup_0003", bigger));
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == editedGraph);

	const std::string editedVolumes = level.text(FORGE_LEVEL_MEMBER_VOLUMES);
	const std::string movedVolume = volumeRow(editedVolumes, pickupTrigger.volume_id);
	REQUIRE_FALSE(movedVolume.empty());
	CHECK(movedVolume == replaced(replaced(replaced(pickupVolume, originalBox,
		"\"min\": [68.000000, -48.000000, -314.500000], \"max\": [132.000000, 48.000000, -186.500000]"),
		"\"pad_ref\": \"pad_0002\"", "\"pad_ref\": \"pad_0003\""), "\"room_ref\": \"room_0003\"", "\"room_ref\": \"\""));
	CHECK(volumeRow(editedVolumes, "volume_pad_0002") == previousPrimary);
	CHECK(volumeRow(editedVolumes, "volume_pad_0003") == destinationPrimary);
	CHECK(editedVolumes.find(destinationPrimary) < editedVolumes.find(movedVolume));
	Level reopened(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		objects, editedVolumes, fields, level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(reopened.error);
	REQUIRE(reopened.doc);
	CHECK(reopened.text(FORGE_LEVEL_MEMBER_PADS) == pads);
	CHECK(reopened.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
	CHECK(forgeLevelDocVolumeCount(reopened.doc) == 7);
	REQUIRE(forgeLevelDocTriggerCount(reopened.doc) == 3);
	forge_level_pad_t anchor = {};
	REQUIRE(forgeLevelDocFindPad(reopened.doc, "pad_0002", &anchor));
	CHECK(anchor.has_volume);
	CHECK(anchor.position[0] == 50.0f);
	REQUIRE(forgeLevelDocFindPad(reopened.doc, "pad_0003", &anchor));
	CHECK(anchor.has_volume);
	CHECK(std::equal(anchor.position, anchor.position + 3, kPlace));
	forge_level_spawn_t destination = {};
	REQUIRE(forgeLevelDocGetSpawn(reopened.doc, forgeLevelDocSpawnCount(reopened.doc) - 1, &destination));
	CHECK(std::string(destination.spawn_id) == destinationStart);
	CHECK(std::string(destination.pad_ref) == "pad_0003");
	const auto findSaved = [&](const char *id, forge_level_trigger_t &trigger) {
		for (s32 i = 0; i < forgeLevelDocTriggerCount(reopened.doc); i++) {
			if (forgeLevelDocGetTrigger(reopened.doc, i, &trigger) && strcmp(trigger.volume_id, id) == 0) return true;
		}
		return false;
	};
	forge_level_trigger_t saved = {};
	REQUIRE(findSaved(pickupTrigger.volume_id, saved));
	CHECK(saved.editable);
	CHECK(std::string(saved.pad_ref) == "pad_0003");
	CHECK(std::string(saved.target_id) == "setup_0000");
	CHECK(saved.action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP);
	CHECK(std::equal(saved.half_size, saved.half_size + 3, bigger));
	REQUIRE(findSaved(doorTrigger.volume_id, saved));
	CHECK(std::string(saved.pad_ref) == doorTrigger.pad_ref);
	CHECK(std::string(saved.target_id) == doorTrigger.target_id);
	CHECK(saved.action == FORGE_LEVEL_TRIGGER_OPEN_DOOR);
	CHECK(std::equal(saved.half_size, saved.half_size + 3, bigger));
	REQUIRE(findSaved(destinationTrigger.volume_id, saved));
	CHECK(std::string(saved.pad_ref) == "pad_0003");
	CHECK(std::string(saved.target_id) == "setup_0001");
	CHECK(std::equal(saved.half_size, saved.half_size + 3, half));

	Level offset(pads, spawns, objects, replaced(volumes, originalBox,
		"\"min\": [24.000000, -32.000000, 0.000000], \"max\": [75.000000, 48.000000, 120.000000]"), fields, graph);
	REQUIRE(offset.doc);
	REQUIRE(forgeLevelDocGetTrigger(offset.doc, 0, &saved));
	CHECK_FALSE(saved.editable);
	CHECK_FALSE(forgeLevelDocUpdateTrigger(offset.doc, saved.volume_id, "pad_0003", "setup_0000", bigger, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocMemberChanged(offset.doc, FORGE_LEVEL_MEMBER_VOLUMES));
	CHECK_FALSE(forgeLevelDocMemberChanged(offset.doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
}

TEST_CASE("A weapon pickup is given a weapon of the catalog, or made one of the match's weapon places",
          "[forge-level-doc]")
{
	const std::string objectsText = armouryObjects();
	const std::string fieldsText = armouryFields();
	Level level(kPads, kSpawns, objectsText, kVolumes, fieldsText);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };

	/* What was read. */
	forge_level_object_t place = armouryFind(level.doc, "setup_0000");
	CHECK(place.weapon_slot == 3);
	CHECK(place.weapon_settable == 1);
	CHECK(place.model_settable == 1);
	CHECK(std::string(place.weapon_catalog_id).empty());
	forge_level_object_t dragon = armouryFind(level.doc, "setup_0001");
	CHECK(dragon.weapon_slot == -1);
	CHECK(dragon.weapon_settable == 1);
	CHECK(std::string(dragon.weapon_catalog_id) == "base:dragon");
	CHECK(armouryFind(level.doc, "setup_0002").weapon_settable == 0);
	CHECK(armouryFind(level.doc, "setup_0002").model_settable == 1);
	/* Its two files disagree, so there is no telling which to change. */
	CHECK(armouryFind(level.doc, "setup_0004").weapon_settable == 0);
	CHECK(armouryFind(level.doc, "setup_0004").weapon_slot == -1);

	/* The weapon place becomes a pickup of a mod's weapon, with the model that
	 * shows it at life size: three field rows and two values of its object row. */
	REQUIRE(forgeLevelDocSetPickupWeapon(level.doc, "setup_0000", "mod:weapon_crystal_pistol",
		"mod:model_crystal_pistol", 256, err, sizeof(err)) == 1);
	place = armouryFind(level.doc, "setup_0000");
	CHECK(place.weapon_slot == -1);
	CHECK(std::string(place.weapon_catalog_id) == "mod:weapon_crystal_pistol");
	CHECK(std::string(place.model_catalog_id) == "mod:model_crystal_pistol");
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_OBJECTS) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SETUP_FIELDS) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PADS) == 0);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == 0);
	std::string objects = level.text(FORGE_LEVEL_MEMBER_OBJECTS);
	std::string fields = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	CHECK(objects.find(armouryObject("setup_0000", "weapon_pickup", "pad_0002", "mod:model_crystal_pistol",
		"mod:weapon_crystal_pistol")) != std::string::npos);
	CHECK(fields.find(catalogRow("setup_0000", "weapon_pickup", "pickup.weapon", "weapon_catalog_id",
		"mod:weapon_crystal_pistol")) != std::string::npos);
	CHECK(fields.find(catalogRow("setup_0000", "weapon_pickup", "base.model", "model_catalog_id",
		"mod:model_crystal_pistol")) != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "weapon_pickup", "base.extra_scale", "s32", "256")) != std::string::npos);
	/* Every other object keeps its rows byte for byte. */
	CHECK(objects.find(armouryObject("setup_0001", "weapon_pickup", "pad_0001", "base:model_chrdragon",
		"base:dragon")) != std::string::npos);
	CHECK(fields.find(catalogRow("setup_0004", "weapon_pickup", "base.model", "model_catalog_id",
		"base:model_roofgun")) != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0004", "weapon_pickup", "base.extra_scale", "s32", "512")) != std::string::npos);
	CHECK(objects.size() == objectsText.size() + strlen("mod:weapon_crystal_pistol") +
		strlen("mod:model_crystal_pistol") - strlen("base:model_roofgun"));

	/* What was written is a level the document reads the same way: the two
	 * files agree about the new weapon and the new model. */
	{
		Level again(kPads, kSpawns, objects, kVolumes, fields);
		INFO(again.error);
		REQUIRE(again.doc != nullptr);
		const forge_level_object_t read = armouryFind(again.doc, "setup_0000");
		CHECK(read.weapon_settable == 1);
		CHECK(read.weapon_slot == -1);
		CHECK(std::string(read.weapon_catalog_id) == "mod:weapon_crystal_pistol");
		CHECK(std::string(read.model_catalog_id) == "mod:model_crystal_pistol");
	}

	/* The catalog pickup becomes weapon place 7. Its model and size stay: the
	 * match's weapon set gives a place both. */
	REQUIRE(forgeLevelDocSetPickupSlot(level.doc, "setup_0001", 7, err, sizeof(err)) == 1);
	dragon = armouryFind(level.doc, "setup_0001");
	CHECK(dragon.weapon_slot == 7);
	CHECK(std::string(dragon.weapon_catalog_id).empty());
	CHECK(std::string(dragon.model_catalog_id) == "base:model_chrdragon");
	objects = level.text(FORGE_LEVEL_MEMBER_OBJECTS);
	fields = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	CHECK(objects.find(armouryObject("setup_0001", "weapon_pickup", "pad_0001", "base:model_chrdragon", "")) !=
		std::string::npos);
	CHECK(fields.find(fieldRow("setup_0001", "weapon_pickup", "pickup.weapon", "weapon_selector",
		"mp_location_07")) != std::string::npos);

	/* And the first one back to the place it was: its weapon row and its
	 * object row's weapon are the bytes that were read (its model and size
	 * stay what they were made, which a place does not use). */
	REQUIRE(forgeLevelDocSetPickupSlot(level.doc, "setup_0000", 3, err, sizeof(err)) == 1);
	fields = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	CHECK(fields.find(fieldRow("setup_0000", "weapon_pickup", "pickup.weapon", "weapon_selector",
		"mp_location_03")) != std::string::npos);
	CHECK(armouryFind(level.doc, "setup_0000").weapon_slot == 3);
	{
		Level again(kPads, kSpawns, level.text(FORGE_LEVEL_MEMBER_OBJECTS), kVolumes, fields);
		INFO(again.error);
		REQUIRE(again.doc != nullptr);
		CHECK(armouryFind(again.doc, "setup_0000").weapon_slot == 3);
		CHECK(armouryFind(again.doc, "setup_0000").weapon_settable == 1);
		CHECK(armouryFind(again.doc, "setup_0001").weapon_slot == 7);
		CHECK(armouryFind(again.doc, "setup_0001").weapon_settable == 1);
	}
}

TEST_CASE("A change of weapon or model that cannot be written is refused and changes nothing",
          "[forge-level-doc]")
{
	Level level(kPads, kSpawns, armouryObjects(), kVolumes, armouryFields());
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };
	const auto untouched = [&]() {
		return forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_OBJECTS) == 0 &&
			forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SETUP_FIELDS) == 0;
	};

	CHECK(forgeLevelDocSetPickupWeapon(level.doc, "setup_0099", "base:dragon", "base:model_chrdragon", 256,
		err, sizeof(err)) == 0);
	CHECK(std::string(err) == "the level has no object \"setup_0099\"");
	CHECK(forgeLevelDocSetPickupWeapon(level.doc, "setup_0002", "base:dragon", "base:model_chrdragon", 256,
		err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0002 is a prop, not a weapon pickup");
	CHECK(forgeLevelDocSetPickupWeapon(level.doc, "setup_0004", "base:dragon", "base:model_chrdragon", 256,
		err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0004 has no weapon, model and size that both of the level's object files "
		"give, so what it gives cannot be changed");
	CHECK(forgeLevelDocSetPickupSlot(level.doc, "setup_0004", 2, err, sizeof(err)) == 0);
	CHECK(forgeLevelDocSetPickupSlot(level.doc, "setup_0003", 2, err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0003 is a door, not a weapon pickup");

	/* Ids that are not catalog ids, and so are never written between quotes. */
	for (const char *bad : { "", "dragon", "base:", ":dragon", "base:dra:gon", "Base:Dragon", "base:dra gon",
			"base:dra\"gon", "base:0123456789012345678901234567890123456789012345678901234567890123456789" }) {
		CAPTURE(bad);
		CHECK(forgeLevelDocSetPickupWeapon(level.doc, "setup_0000", bad, "base:model_chrdragon", 256,
			err, sizeof(err)) == 0);
		CHECK(std::string(err) == std::string("\"") + bad + "\" is not a weapon's catalog id");
		CHECK(forgeLevelDocSetPickupWeapon(level.doc, "setup_0000", "base:dragon", bad, 256,
			err, sizeof(err)) == 0);
		CHECK(std::string(err) == std::string("\"") + bad + "\" is not a model's catalog id");
		CHECK(forgeLevelDocSetObjectModel(level.doc, "setup_0002", bad, err, sizeof(err)) == 0);
	}
	CHECK(forgeLevelDocSetPickupWeapon(level.doc, "setup_0000", nullptr, "base:model_chrdragon", 256,
		err, sizeof(err)) == 0);
	CHECK(forgeLevelDocSetPickupWeapon(level.doc, "setup_0000", "base:dragon", nullptr, 256,
		err, sizeof(err)) == 0);
	CHECK(forgeLevelDocSetObjectModel(level.doc, "setup_0002", nullptr, err, sizeof(err)) == 0);

	for (const s32 size : { 0, -256, 32768, 70000 }) {
		CAPTURE(size);
		CHECK(forgeLevelDocSetPickupWeapon(level.doc, "setup_0000", "base:dragon", "base:model_chrdragon", size,
			err, sizeof(err)) == 0);
	}
	CHECK(std::string(err) == "a pickup's size is 1 to 32767 (256 is life size), not 70000");
	for (const s32 slot : { -1, 16, 99 }) {
		CAPTURE(slot);
		CHECK(forgeLevelDocSetPickupSlot(level.doc, "setup_0000", slot, err, sizeof(err)) == 0);
	}
	CHECK(std::string(err) == "a match has weapon places 0 to 15, not 99");

	/* A model only for a plain prop: a door needs a model built for one, and
	 * a pickup's model goes with its weapon. */
	CHECK(forgeLevelDocSetObjectModel(level.doc, "setup_0003", "base:model_crate", err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0003 is a door; only a plain prop can be given another model here");
	CHECK(forgeLevelDocSetObjectModel(level.doc, "setup_0000", "base:model_crate", err, sizeof(err)) == 0);
	CHECK(forgeLevelDocSetObjectModel(level.doc, "setup_0099", "base:model_crate", err, sizeof(err)) == 0);
	CHECK(forgeLevelDocSetPickupWeapon(nullptr, "setup_0000", "base:dragon", "base:model_chrdragon", 256,
		err, sizeof(err)) == 0);
	CHECK(forgeLevelDocSetPickupSlot(nullptr, "setup_0000", 1, err, sizeof(err)) == 0);
	CHECK(forgeLevelDocSetObjectModel(nullptr, "setup_0002", "base:model_crate", err, sizeof(err)) == 0);

	CHECK(untouched());
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == armouryObjects());
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == armouryFields());
}

TEST_CASE("A prop is given another model, and a copied pickup is changed without touching the one it came from",
          "[forge-level-doc]")
{
	Level level(kPads, kSpawns, armouryObjects(), kVolumes, armouryFields());
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };
	char id[32] = { 0 };

	REQUIRE(forgeLevelDocSetObjectModel(level.doc, "setup_0002", "mod:prop_maian_crate", err, sizeof(err)) == 1);
	CHECK(std::string(armouryFind(level.doc, "setup_0002").model_catalog_id) == "mod:prop_maian_crate");
	std::string objects = level.text(FORGE_LEVEL_MEMBER_OBJECTS);
	std::string fields = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	CHECK(objects.find(armouryObject("setup_0002", "prop", "pad_0000", "mod:prop_maian_crate", "")) !=
		std::string::npos);
	CHECK(fields.find(catalogRow("setup_0002", "prop", "base.model", "model_catalog_id", "mod:prop_maian_crate")) !=
		std::string::npos);
	CHECK(occurrences(fields, "base:model_crate") == 0);

	/* A copy of the weapon place, then the copy alone is given a weapon. */
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0005");
	forge_level_object_t copy = armouryFind(level.doc, "setup_0005");
	CHECK(copy.weapon_slot == 3);
	CHECK(copy.weapon_settable == 1);
	REQUIRE(forgeLevelDocSetPickupWeapon(level.doc, "setup_0005", "base:dragon", "base:model_chrdragon", 256,
		err, sizeof(err)) == 1);
	CHECK(armouryFind(level.doc, "setup_0005").weapon_slot == -1);
	CHECK(std::string(armouryFind(level.doc, "setup_0005").weapon_catalog_id) == "base:dragon");
	CHECK(armouryFind(level.doc, "setup_0000").weapon_slot == 3);
	CHECK(std::string(armouryFind(level.doc, "setup_0000").model_catalog_id) == "base:model_roofgun");
	fields = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	CHECK(fields.find(fieldRow("setup_0000", "weapon_pickup", "pickup.weapon", "weapon_selector",
		"mp_location_03")) != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "weapon_pickup", "base.extra_scale", "s32", "512")) != std::string::npos);
	CHECK(fields.find(catalogRow("setup_0005", "weapon_pickup", "pickup.weapon", "weapon_catalog_id",
		"base:dragon")) != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0005", "weapon_pickup", "base.extra_scale", "s32", "256")) != std::string::npos);

	/* Read back, the level says the same of all three. */
	Level again(level.text(FORGE_LEVEL_MEMBER_PADS), kSpawns, level.text(FORGE_LEVEL_MEMBER_OBJECTS),
		level.text(FORGE_LEVEL_MEMBER_VOLUMES), fields, level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(armouryFind(again.doc, "setup_0000").weapon_slot == 3);
	CHECK(std::string(armouryFind(again.doc, "setup_0005").weapon_catalog_id) == "base:dragon");
	CHECK(armouryFind(again.doc, "setup_0005").weapon_settable == 1);
	CHECK(std::string(armouryFind(again.doc, "setup_0002").model_catalog_id) == "mod:prop_maian_crate");
}

TEST_CASE("Grid variant placement fallbacks are written to an independent native document",
	"[forge-level-doc][grid-variant-fallback]")
{
	Level seed(kPads, kSpawns, armouryObjects(), kVolumes, armouryFields());
	INFO(seed.error);
	REQUIRE(seed.doc);
	char err[256] = {};
	char bike[32] = {};
	REQUIRE(forgeLevelDocSetPickupWeapon(seed.doc, "setup_0000", "mod:ordinary", "mod:model_ordinary",
		512, err, sizeof(err)));
	REQUIRE(forgeLevelDocSetPickupWeapon(seed.doc, "setup_0001", "mod:power", "mod:model_power",
		768, err, sizeof(err)));
	REQUIRE(forgeLevelDocSetObjectModel(seed.doc, "setup_0002", "mod:model_prop", err, sizeof(err)));
	REQUIRE(forgeLevelDocAddObject(seed.doc, FORGE_LEVEL_NEW_HOVERBIKE, 0, kPlace, kFacing,
		bike, sizeof(bike), err, sizeof(err)));

	const std::string originalObjects = replaced(seed.text(FORGE_LEVEL_MEMBER_OBJECTS),
		"base:model_hovbike", "mod:model_bike");
	const std::string originalFields = replaced(seed.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS),
		"base:model_hovbike", "mod:model_bike");
	Level level(seed.text(FORGE_LEVEL_MEMBER_PADS), kSpawns, originalObjects,
		seed.text(FORGE_LEVEL_MEMBER_VOLUMES), originalFields, seed.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(level.error);
	REQUIRE(level.doc);
	const auto bikeBefore = armouryFind(level.doc, bike);
	const std::array<forge_level_placement_fallback_t, 4> edits = {{
		{ FORGE_LEVEL_FALLBACK_PICKUP, "setup_0000", "mod:ordinary", "base:falcon2", "base:model_chrfalcon2", 256 },
		{ FORGE_LEVEL_FALLBACK_PICKUP, "setup_0001", "mod:power", "base:slayer", "base:model_chrskrocket", 256 },
		{ FORGE_LEVEL_FALLBACK_PROP, "setup_0002", "mod:model_prop", nullptr, nullptr, 0 },
		{ FORGE_LEVEL_FALLBACK_HOVERBIKE, bike, "mod:model_bike", nullptr, nullptr, 0 },
	}};

	const auto text = [](forge_level_doc_t *doc, s32 member) {
		size_t size = 0;
		const char *bytes = forgeLevelDocMemberText(doc, member, &size);
		return bytes ? std::string(bytes, size) : std::string();
	};
	std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)> variant(
		forgeLevelDocCloneWithPlacementFallbacks(level.doc, edits.data(), edits.size(), err, sizeof(err)),
		forgeLevelDocFree);
	INFO(err);
	REQUIRE(variant);
	const std::string objects = text(variant.get(), FORGE_LEVEL_MEMBER_OBJECTS);
	const std::string fields = text(variant.get(), FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	std::string expectedObjects = originalObjects;
	std::string expectedFields = originalFields;
	const char *const originalModels[] = {"mod:model_ordinary", "mod:model_power"};
	const char *const originalScales[] = {"512", "768"};
	for (size_t i = 0; i < 2; i++) {
		const auto &edit = edits[i];
		CHECK(fields.find(catalogRow(edit.record_id, "weapon_pickup", "pickup.weapon", "weapon_catalog_id",
			edit.weapon_catalog_id)) != std::string::npos);
		CHECK(fields.find(catalogRow(edit.record_id, "weapon_pickup", "base.model", "model_catalog_id",
			edit.model_catalog_id)) != std::string::npos);
		const auto fallbackScale = fieldRow(edit.record_id, "weapon_pickup", "base.extra_scale", "s32", "256");
		CHECK(fields.find(fallbackScale) != std::string::npos);
		expectedObjects = replaced(expectedObjects, edit.expected_catalog_id, edit.weapon_catalog_id);
		expectedObjects = replaced(expectedObjects, originalModels[i], edit.model_catalog_id);
		expectedFields = replaced(expectedFields, edit.expected_catalog_id, edit.weapon_catalog_id);
		expectedFields = replaced(expectedFields, originalModels[i], edit.model_catalog_id);
		expectedFields = replaced(expectedFields,
			fieldRow(edit.record_id, "weapon_pickup", "base.extra_scale", "s32", originalScales[i]), fallbackScale);
	}
	CHECK(fields.find(catalogRow("setup_0002", "prop", "base.model", "model_catalog_id", "base:model_crate2_cq"))
		!= std::string::npos);
	expectedObjects = replaced(expectedObjects, "mod:model_prop", "base:model_crate2_cq");
	expectedObjects = replaced(expectedObjects, "mod:model_bike", "base:model_hovbike");
	expectedFields = replaced(expectedFields, "mod:model_prop", "base:model_crate2_cq");
	expectedFields = replaced(expectedFields, "mod:model_bike", "base:model_hovbike");
	CHECK(objects == expectedObjects);
	CHECK(fields == expectedFields);
	CHECK(text(variant.get(), FORGE_LEVEL_MEMBER_PADS) == level.text(FORGE_LEVEL_MEMBER_PADS));
	CHECK(text(variant.get(), FORGE_LEVEL_MEMBER_VOLUMES) == level.text(FORGE_LEVEL_MEMBER_VOLUMES));
	CHECK(text(variant.get(), FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	Level reopened(level.text(FORGE_LEVEL_MEMBER_PADS), kSpawns, objects,
		level.text(FORGE_LEVEL_MEMBER_VOLUMES), fields, level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(reopened.error);
	REQUIRE(reopened.doc);
	CHECK(std::string(armouryFind(reopened.doc, "setup_0000").weapon_catalog_id) == "base:falcon2");
	CHECK(std::string(armouryFind(reopened.doc, "setup_0001").weapon_catalog_id) == "base:slayer");
	CHECK(std::string(armouryFind(reopened.doc, "setup_0002").model_catalog_id) == "base:model_crate2_cq");
	const auto bikeAfter = armouryFind(reopened.doc, bike);
	CHECK(std::string(bikeAfter.kind) == "hoverbike");
	CHECK(std::string(bikeAfter.pad_ref) == bikeBefore.pad_ref);
	CHECK(bikeAfter.order == bikeBefore.order);
	CHECK(std::string(bikeAfter.model_catalog_id) == "base:model_hovbike");

	auto invalid = edits;
	invalid.back().expected_catalog_id = "mod:old_bike";
	CHECK(forgeLevelDocCloneWithPlacementFallbacks(level.doc, invalid.data(), invalid.size(), err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("no longer has") != std::string::npos);
	invalid.back() = edits[0];
	CHECK(forgeLevelDocCloneWithPlacementFallbacks(level.doc, invalid.data(), invalid.size(), err, sizeof(err)) == nullptr);
	invalid.back() = { FORGE_LEVEL_FALLBACK_PROP, "setup_0003", "base:model_door", nullptr, nullptr, 0 };
	CHECK(forgeLevelDocCloneWithPlacementFallbacks(level.doc, invalid.data(), invalid.size(), err, sizeof(err)) == nullptr);
	invalid = edits;
	invalid[1].weapon_catalog_id = "base:dragon";
	CHECK(forgeLevelDocCloneWithPlacementFallbacks(level.doc, invalid.data(), invalid.size(), err, sizeof(err)) == nullptr);
	const auto bikeModel = catalogRow(bike, "hoverbike", "base.model", "model_catalog_id", "mod:model_bike");
	const auto duplicateFields = replaced(originalFields, bikeModel, bikeModel + ",\n" + bikeModel);
	Level ambiguous(level.text(FORGE_LEVEL_MEMBER_PADS), kSpawns, originalObjects,
		level.text(FORGE_LEVEL_MEMBER_VOLUMES), duplicateFields, level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(ambiguous.error);
	REQUIRE(ambiguous.doc);
	CHECK(forgeLevelDocCloneWithPlacementFallbacks(ambiguous.doc, edits.data(), edits.size(), err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("no supported placement fallback") != std::string::npos);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == originalObjects);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == originalFields);
}

TEST_CASE("A start added to a level that had none and taken out again leaves the file as it was read",
          "[forge-level-doc]")
{
	/* Found by the sweep of the base levels in a game turn (2026-11-17): three
	 * arenas have no player start at all, and for them an added start that
	 * was removed again left "rows": [ with a line of spaces in it, not the
	 * bytes that were read. The first row re-lays an empty list out, and
	 * taking the row away did not undo that. */
	const std::string noSpawns = "{\n  \"schema\": \"pd2.scenario.spawns.v1\",\n  \"rows\": [\n  ]\n}\n";
	Level level(kPads, noSpawns, kObjects, kVolumes, levelFields());
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	REQUIRE(forgeLevelDocSpawnCount(level.doc) == 0);
	char err[256] = { 0 };
	char id[32] = { 0 };

	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SPAWNS) == 1);
	const std::string one = level.text(FORGE_LEVEL_MEMBER_SPAWNS);
	CHECK(one.find("  \"rows\": [\n    {") != std::string::npos);
	CHECK(one.rfind("}\n  ]\n}\n") == one.size() - strlen("}\n  ]\n}\n"));

	REQUIRE(forgeLevelDocRemoveSpawn(level.doc, id, err, sizeof(err)) == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == noSpawns);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SPAWNS) == 0);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 0);

	/* A start added after that is laid out as the first was, and reads back. */
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
	const std::string again = level.text(FORGE_LEVEL_MEMBER_SPAWNS);
	CHECK(again.find("  \"rows\": [\n    {") != std::string::npos);
	CHECK(again.rfind("}\n  ]\n}\n") == again.size() - strlen("}\n  ]\n}\n"));
	CHECK(again.size() == one.size());
	Level read(level.text(FORGE_LEVEL_MEMBER_PADS), again, kObjects, level.text(FORGE_LEVEL_MEMBER_VOLUMES),
		levelFields(), level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(read.error);
	REQUIRE(read.doc != nullptr);
	CHECK(forgeLevelDocSpawnCount(read.doc) == 1);

	/* A list that was read with rows and loses them all is not that case:
	 * it keeps the layout its rows had. */
	Level full(kPads, kSpawns, kObjects, kVolumes, levelFields());
	REQUIRE(full.doc != nullptr);
	REQUIRE(forgeLevelDocRemoveSpawn(full.doc, "spawn_0000", err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocRemoveSpawn(full.doc, "spawn_0004", err, sizeof(err)) == 1);
	CHECK(forgeLevelDocSpawnCount(full.doc) == 0);
	Level emptied(kPads, full.text(FORGE_LEVEL_MEMBER_SPAWNS), kObjects, kVolumes, levelFields());
	INFO(emptied.error);
	REQUIRE(emptied.doc != nullptr);
	CHECK(forgeLevelDocSpawnCount(emptied.doc) == 0);
}

TEST_CASE("A level with no objects gains a weapon place and an ammo crate made from nothing",
          "[forge-level-doc]")
{
	/* An empty level: pads, starts and bounds, and no object of any kind to
	 * copy. This is what a level template made from a model starts as. */
	const char *const noObjects = "{\n  \"schema\": \"pd2.scenario.objects.v1\",\n  \"rows\": [\n  ]\n}\n";
	Level level(kPads, kSpawns, noObjects, kVolumes, kNoFields);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	REQUIRE(forgeLevelDocObjectCount(level.doc) == 0);
	char err[256] = { 0 };
	char id[32] = { 0 };
	const f32 nowhere[3] = { 0.0f, 0.0f, 0.0f };
	const f32 broken[3] = { 1.0f, std::numeric_limits<f32>::quiet_NaN(), 3.0f };

	/* What cannot be made changes nothing. */
	CHECK(forgeLevelDocAddObject(level.doc, -1, 0, kPlace, kFacing, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(std::string(err) == "-1 is not a kind of object that can be made here");
	CHECK(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_KIND_COUNT, 0, kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 16, kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 0);
	CHECK(std::string(err) == "a match has weapon places 0 to 15, not 16");
	CHECK(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_WEAPON_PLACE, -1, kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0, broken, kFacing, id, sizeof(id),
		err, sizeof(err)) == 0);
	CHECK(std::string(err) == "a position and a facing need three finite numbers each");
	CHECK(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0, kPlace, nowhere, id, sizeof(id),
		err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddObject(nullptr, FORGE_LEVEL_NEW_AMMO_CRATE, 0, kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 0);
	CHECK(std::string(id).empty());
	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		CAPTURE(member);
		CHECK(forgeLevelDocMemberChanged(level.doc, member) == 0);
	}
	CHECK(forgeLevelDocObjectCount(level.doc) == 0);
	CHECK(forgeLevelDocPadCount(level.doc) == 3);

	/* Weapon place 5 of the match, then a crate. Each takes the next command
	 * and a pad of its own, with the pad's bounds row and graph node. */
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 5, kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0000");
	/* A crate ignores the weapon place it is handed. */
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_AMMO_CRATE, 99, kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0001");
	CHECK(forgeLevelDocObjectCount(level.doc) == 2);
	CHECK(forgeLevelDocPadCount(level.doc) == 5);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 5);
	/* 37 rows for a weapon place and 68 for a crate, as every one of the base
	 * levels has. */
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 37 + 68);

	forge_level_object_t place = armouryFind(level.doc, "setup_0000");
	CHECK(std::string(place.kind) == "weapon_pickup");
	CHECK(std::string(place.pad_ref) == "pad_0003");
	CHECK(std::string(place.model_catalog_id) == "base:model_roofgun");
	CHECK(std::string(place.weapon_catalog_id).empty());
	CHECK(place.order == 0);
	CHECK(place.weapon_slot == 5);
	CHECK(place.movable == 1);
	CHECK(place.removable == 1);
	CHECK(place.removed == 0);
	CHECK(place.model_settable == 1);
	CHECK(place.weapon_settable == 1);
	forge_level_object_t crate = armouryFind(level.doc, "setup_0001");
	CHECK(std::string(crate.kind) == "multi_ammo_crate");
	CHECK(std::string(crate.pad_ref) == "pad_0004");
	CHECK(std::string(crate.model_catalog_id) == "base:model_multi_ammo_crate");
	CHECK(crate.order == 1);
	CHECK(crate.weapon_slot == -1);
	CHECK(crate.movable == 1);
	CHECK(crate.removable == 1);
	CHECK(crate.weapon_settable == 0);

	/* The rows are the extractor's, in its layout. */
	const std::string objects = level.text(FORGE_LEVEL_MEMBER_OBJECTS);
	const std::string fields = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	const std::string graph = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
	CHECK(objects.find("  \"rows\": [\n    {\n      \"record_id\": \"setup_0000\",\n      \"kind\": \"weapon_pickup\",\n"
		"      \"pad_ref\": \"pad_0003\",\n      \"model_catalog_id\": \"base:model_roofgun\",\n"
		"      \"weapon_catalog_id\": \"\",\n      \"secondary_weapon_catalog_id\": \"\",\n"
		"      \"body_catalog_id\": \"\",\n      \"head_catalog_id\": \"\",\n      \"ailist_ref\": \"\",\n"
		"      \"flags\": \"0x00000001\",\n      \"flags2\": \"0x00000000\",\n      \"flags3\": \"0x00000000\"\n"
		"    },\n    {\n      \"record_id\": \"setup_0001\",\n      \"kind\": \"multi_ammo_crate\",\n") !=
		std::string::npos);
	CHECK(objects.rfind("\n    }\n  ]\n}\n") == objects.size() - strlen("\n    }\n  ]\n}\n"));
	CHECK(fields.find("  \"rows\": [\n" + fieldRow("setup_0000", "weapon_pickup", "command.order", "s32", "0") +
		",\n" + fieldRow("setup_0000", "weapon_pickup", "base.extra_scale", "s32", "256") + ",\n" +
		fieldRow("setup_0000", "weapon_pickup", "base.hidden2", "s32", "0") + ",\n" +
		catalogRow("setup_0000", "weapon_pickup", "base.model", "model_catalog_id", "base:model_roofgun") + ",\n" +
		fieldRow("setup_0000", "weapon_pickup", "base.pad", "pad_ref", "pad_0003") + ",\n" +
		fieldRow("setup_0000", "weapon_pickup", "base.flags", "u32_hex", "0x00000001") + ",\n") !=
		std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "weapon_pickup", "pickup.weapon", "weapon_selector",
		"mp_location_05")) != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "weapon_pickup", "pickup.dual_weapon", "weapon_selector", "none")) !=
		std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "weapon_pickup", "pickup.team_or_timer240", "s32", "-1")) !=
		std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "weapon_pickup", "base.rotation[2][2]", "f32", "0")) !=
		std::string::npos);
	CHECK(fields.find(fieldRow("setup_0001", "multi_ammo_crate", "command.order", "s32", "1")) != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0001", "multi_ammo_crate", "base.pad", "pad_ref", "pad_0004")) !=
		std::string::npos);
	CHECK(fields.find(fieldRow("setup_0001", "multi_ammo_crate", "multi_ammo_crate.slots[18].quantity", "s32",
		"0") + "\n  ]\n}\n") != std::string::npos);
	CHECK(occurrences(fields, "\"field\": \"multi_ammo_crate.slots[") == 38);
	CHECK(occurrences(fields, "\"record_id\": \"setup_0000\"") == 37);
	CHECK(occurrences(fields, "\"record_id\": \"setup_0001\"") == 68);
	/* The graph keeps a node for each bounds row and counts the objects. */
	CHECK(occurrences(graph, kVolumeNode) == 5);
	CHECK(graph.find("\"objects\": 2") != std::string::npos);
	CHECK(graph.find("\"pads\": 5") != std::string::npos);

	/* What was written reads back as the same two objects, and they take
	 * every edit an object of a base level takes. */
	{
		Level again(level.text(FORGE_LEVEL_MEMBER_PADS), kSpawns, objects, level.text(FORGE_LEVEL_MEMBER_VOLUMES),
			fields, graph);
		INFO(again.error);
		REQUIRE(again.doc != nullptr);
		CHECK(forgeLevelDocObjectCount(again.doc) == 2);
		const forge_level_object_t read = armouryFind(again.doc, "setup_0000");
		CHECK(read.weapon_slot == 5);
		CHECK(read.weapon_settable == 1);
		CHECK(read.movable == 1);
		CHECK(read.removable == 1);
		CHECK(armouryFind(again.doc, "setup_0001").removable == 1);
		CHECK(armouryFind(again.doc, "setup_0001").movable == 1);
	}
	REQUIRE(forgeLevelDocSetPickupWeapon(level.doc, "setup_0000", "base:dragon", "base:model_chrdragon", 256,
		err, sizeof(err)) == 1);
	CHECK(std::string(armouryFind(level.doc, "setup_0000").weapon_catalog_id) == "base:dragon");
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, err, sizeof(err)) == 1);
	CHECK(armouryFind(level.doc, "setup_0001").removed == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS).find(fieldRow("setup_0001", "multi_ammo_crate",
		"base.flags2", "u32_hex", "0x000000f0")) != std::string::npos);
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 0, err, sizeof(err)) == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS).find(fieldRow("setup_0001", "multi_ammo_crate",
		"base.flags2", "u32_hex", "0x00000000")) != std::string::npos);
	REQUIRE(forgeLevelDocMoveObject(level.doc, "setup_0001", kPlace, kFacing, err, sizeof(err)) == 1);
	/* A pad this document added moves where it stands: no new pad for the move. */
	CHECK(forgeLevelDocPadCount(level.doc) == 5);
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0001", kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0002");
	CHECK(std::string(armouryFind(level.doc, "setup_0002").kind) == "multi_ammo_crate");
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 37 + 68 + 68);
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 0, kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0003");
	CHECK(armouryFind(level.doc, "setup_0003").weapon_slot == 0);
	CHECK(armouryFind(level.doc, "setup_0003").order == 3);
}

TEST_CASE("A level gains a hoverbike made from nothing, with the record every base hoverbike has",
          "[forge-level-doc]")
{
	/* An empty level: the vehicle smokes' own place to drive is made this way
	 * (B-1241), and so is a vehicle a player places in Forge. */
	const char *const noObjects = "{\n  \"schema\": \"pd2.scenario.objects.v1\",\n  \"rows\": [\n  ]\n}\n";
	Level level(kPads, kSpawns, noObjects, kVolumes, kNoFields);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };
	char id[32] = { 0 };

	/* A hoverbike ignores the weapon place it is handed. */
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_HOVERBIKE, 99, kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0000");
	CHECK(forgeLevelDocObjectCount(level.doc) == 1);
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 4);
	/* 63 rows, as each of the five hoverbikes of the base levels has. */
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 63);

	const forge_level_object_t bike = armouryFind(level.doc, "setup_0000");
	CHECK(std::string(bike.kind) == "hoverbike");
	CHECK(std::string(bike.pad_ref) == "pad_0003");
	CHECK(std::string(bike.model_catalog_id) == "base:model_hovbike");
	CHECK(std::string(bike.weapon_catalog_id).empty());
	CHECK(bike.order == 0);
	CHECK(bike.weapon_slot == -1);
	CHECK(bike.movable == 1);
	CHECK(bike.removable == 1);
	CHECK(bike.removed == 0);
	CHECK(bike.model_settable == 1);
	CHECK(bike.weapon_settable == 0);

	/* The rows are the extractor's, with the hoverbike's own flag words. */
	const std::string objects = level.text(FORGE_LEVEL_MEMBER_OBJECTS);
	const std::string fields = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	CHECK(objects.find("      \"kind\": \"hoverbike\",\n      \"pad_ref\": \"pad_0003\",\n"
		"      \"model_catalog_id\": \"base:model_hovbike\",\n") != std::string::npos);
	CHECK(objects.find("      \"flags\": \"0x05120101\",\n      \"flags2\": \"0x00304300\",\n"
		"      \"flags3\": \"0x02000003\"\n    }") != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "hoverbike", "command.order", "s32", "0") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "base.extra_scale", "s32", "256") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "base.hidden2", "s32", "0") + ",\n" +
		catalogRow("setup_0000", "hoverbike", "base.model", "model_catalog_id", "base:model_hovbike") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "base.pad", "pad_ref", "pad_0003") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "base.flags", "u32_hex", "0x05120101") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "base.flags2", "u32_hex", "0x00304300") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "base.flags3", "u32_hex", "0x02000003") + ",\n") != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "hoverbike", "base.geo_count", "s32", "0") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "hover.type", "s32", "1") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "hover.flags", "s32", "1") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "hover.bobycur", "f32", "0") + ",\n") != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "hoverbike", "hover.prevgroundframe60", "s32", "0") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "hoverbike.speed[0]", "f32", "0") + ",\n") != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "hoverbike", "hoverbike.lean_diff", "f32", "0") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "hoverbike.max_speed_time240", "s32", "0") + ",\n" +
		fieldRow("setup_0000", "hoverbike", "hoverbike.relative[0]", "f32", "0") + ",\n") != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "hoverbike", "hoverbike.relative_speed[1]", "f32", "0") +
		"\n  ]\n}\n") != std::string::npos);
	CHECK(occurrences(fields, "\"field\": \"hover.") == 17);
	CHECK(occurrences(fields, "\"field\": \"hoverbike.") == 16);

	/* Left out on every difficulty, and put back with the exact words it had. */
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0000", 1, err, sizeof(err)) == 1);
	CHECK(armouryFind(level.doc, "setup_0000").removed == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS).find("\"flags2\": \"0x003043f0\"") != std::string::npos);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS).find(fieldRow("setup_0000", "hoverbike", "base.flags2",
		"u32_hex", "0x003043f0")) != std::string::npos);
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0000", 0, err, sizeof(err)) == 1);
	CHECK(armouryFind(level.doc, "setup_0000").removed == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);

	/* What was written reads back as the same hoverbike. */
	{
		Level again(level.text(FORGE_LEVEL_MEMBER_PADS), kSpawns, objects, level.text(FORGE_LEVEL_MEMBER_VOLUMES),
			fields, level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
		INFO(again.error);
		REQUIRE(again.doc != nullptr);
		const forge_level_object_t read = armouryFind(again.doc, "setup_0000");
		CHECK(std::string(read.kind) == "hoverbike");
		CHECK(std::string(read.model_catalog_id) == "base:model_hovbike");
		CHECK(read.movable == 1);
		CHECK(read.removable == 1);
		CHECK(read.removed == 0);
		CHECK(read.model_settable == 1);
		CHECK(read.weapon_settable == 0);
		CHECK(read.weapon_slot == -1);
	}

	/* A copy is another hoverbike: the next command, on a pad of its own. */
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0001");
	CHECK(std::string(armouryFind(level.doc, "setup_0001").kind) == "hoverbike");
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 63 + 63);
	CHECK(forgeLevelDocPadCount(level.doc) == 5);
}

TEST_CASE("A facing is turned about the vertical, positive to the right", "[forge-level-doc]")
{
	f32 out[3] = { 9.0f, 9.0f, 9.0f };

	/* Yaw 0 faces +z and its right is -x (the camera's convention): a
	 * quarter turn right faces -x. */
	const f32 ahead[3] = { 0.0f, 0.0f, 1.0f };
	REQUIRE(forgeLevelTurnLook(ahead, 90.0f, out) == 1);
	CHECK(out[0] == Approx(-1.0f));
	CHECK(out[1] == 0.0f);
	CHECK(out[2] == Approx(0.0f).margin(1e-6));
	REQUIRE(forgeLevelTurnLook(ahead, -90.0f, out) == 1);
	CHECK(out[0] == Approx(1.0f));

	/* The same as the camera's yaw growing: from yaw t to yaw t + 45. */
	const f32 t = 217.0f * 3.14159265f / 180.0f;
	const f32 at[3] = { -std::sin(t), 0.0f, std::cos(t) };
	REQUIRE(forgeLevelTurnLook(at, 45.0f, out) == 1);
	const f32 t2 = 262.0f * 3.14159265f / 180.0f;
	CHECK(out[0] == Approx(-std::sin(t2)).margin(1e-5));
	CHECK(out[2] == Approx(std::cos(t2)).margin(1e-5));

	/* Any length and any tilt: the result is flat and of unit length. */
	const f32 tilted[3] = { 3.0f, 7.0f, -4.0f };
	REQUIRE(forgeLevelTurnLook(tilted, 0.0f, out) == 1);
	CHECK(out[0] == Approx(0.6f));
	CHECK(out[1] == 0.0f);
	CHECK(out[2] == Approx(-0.8f));
	REQUIRE(forgeLevelTurnLook(tilted, 360.0f, out) == 1);
	CHECK(out[0] == Approx(0.6f).margin(1e-5));

	/* Nothing to turn. */
	const f32 up[3] = { 0.0f, 1.0f, 0.0f };
	CHECK(forgeLevelTurnLook(up, 45.0f, out) == 0);
	const f32 broken[3] = { std::numeric_limits<f32>::quiet_NaN(), 0.0f, 1.0f };
	CHECK(forgeLevelTurnLook(broken, 45.0f, out) == 0);
	CHECK(forgeLevelTurnLook(nullptr, 45.0f, out) == 0);
}

TEST_CASE("A copy of a document shares nothing with it and goes on exactly as it would",
          "[forge-level-doc]")
{
	/* This is how an edit is undone: the session keeps copies from before
	 * each edit and puts one back. */
	Level level;
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };
	char id[32] = { 0 };
	const auto textOf = [](forge_level_doc_t *doc, s32 member) {
		size_t size = 0;
		const char *t = forgeLevelDocMemberText(doc, member, &size);
		return t ? std::string(t, size) : std::string();
	};

	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
	forge_level_doc_t *copy = forgeLevelDocClone(level.doc);
	REQUIRE(copy != nullptr);
	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		CAPTURE(member);
		CHECK(textOf(copy, member) == level.text(member));
		CHECK(forgeLevelDocMemberChanged(copy, member) == forgeLevelDocMemberChanged(level.doc, member));
	}

	/* An edit of the copy leaves the document as it was. */
	const std::string spawnsBefore = level.text(FORGE_LEVEL_MEMBER_SPAWNS);
	char copyId[32] = { 0 };
	REQUIRE(forgeLevelDocAddSpawn(copy, kPlace, kFacing, nullptr, copyId, sizeof(copyId), err, sizeof(err)) == 1);
	CHECK(forgeLevelDocSpawnCount(copy) == forgeLevelDocSpawnCount(level.doc) + 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawnsBefore);

	/* The same edit of the document gives the same id and the same texts:
	 * the copy took every id still to hand out with it. */
	char sameId[32] = { 0 };
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, sameId, sizeof(sameId), err,
		sizeof(err)) == 1);
	CHECK(std::string(sameId) == copyId);
	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		CAPTURE(member);
		CHECK(textOf(copy, member) == level.text(member));
	}

	/* Freeing the copy leaves the document whole. */
	forgeLevelDocFree(copy);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 4);
	CHECK(forgeLevelDocClone(nullptr) == nullptr);
}

TEST_CASE("A level gains a plain prop made from nothing, drawn with the model it is given",
          "[forge-level-doc]")
{
	const char *const noObjects = "{\n  \"schema\": \"pd2.scenario.objects.v1\",\n  \"rows\": [\n  ]\n}\n";
	Level level(kPads, kSpawns, noObjects, kVolumes, kNoFields);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };
	char id[32] = { 0 };

	/* A model id that cannot be one changes nothing. */
	CHECK(forgeLevelDocAddProp(level.doc, "Base:Crate", kPlace, kFacing, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(std::string(err) == "\"Base:Crate\" is not a model's catalog id");
	CHECK(forgeLevelDocAddProp(level.doc, "", kPlace, kFacing, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddProp(level.doc, nullptr, kPlace, kFacing, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddProp(level.doc, std::string(70, 'a').c_str(), kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddProp(nullptr, "base:model_crate2_cq", kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 0);
	CHECK(std::string(id).empty());
	CHECK(forgeLevelDocObjectCount(level.doc) == 0);
	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		CAPTURE(member);
		CHECK(forgeLevelDocMemberChanged(level.doc, member) == 0);
	}

	REQUIRE(forgeLevelDocAddProp(level.doc, "base:model_crate2_cq", kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0000");
	CHECK(forgeLevelDocObjectCount(level.doc) == 1);
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	/* 30 rows, as every plain prop of the base levels has. */
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 30);

	const forge_level_object_t prop = armouryFind(level.doc, "setup_0000");
	CHECK(std::string(prop.kind) == "prop");
	CHECK(std::string(prop.pad_ref) == "pad_0003");
	CHECK(std::string(prop.model_catalog_id) == "base:model_crate2_cq");
	CHECK(prop.weapon_slot == -1);
	CHECK(prop.movable == 1);
	CHECK(prop.removable == 1);
	CHECK(prop.model_settable == 1);
	CHECK(prop.weapon_settable == 0);

	const std::string objects = level.text(FORGE_LEVEL_MEMBER_OBJECTS);
	const std::string fields = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	CHECK(objects.find("      \"kind\": \"prop\",\n      \"pad_ref\": \"pad_0003\",\n"
		"      \"model_catalog_id\": \"base:model_crate2_cq\",\n") != std::string::npos);
	CHECK(objects.find("      \"flags\": \"0x00000001\",\n      \"flags2\": \"0x00000000\",\n"
		"      \"flags3\": \"0x00000000\"\n    }") != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "prop", "command.order", "s32", "0") + ",\n" +
		fieldRow("setup_0000", "prop", "base.extra_scale", "s32", "256") + ",\n" +
		fieldRow("setup_0000", "prop", "base.hidden2", "s32", "0") + ",\n" +
		catalogRow("setup_0000", "prop", "base.model", "model_catalog_id", "base:model_crate2_cq") + ",\n" +
		fieldRow("setup_0000", "prop", "base.pad", "pad_ref", "pad_0003") + ",\n") != std::string::npos);
	CHECK(fields.find(fieldRow("setup_0000", "prop", "base.geo_count", "s32", "0") + "\n  ]\n}\n") !=
		std::string::npos);

	/* It takes the edits a prop of a base level takes. */
	REQUIRE(forgeLevelDocSetObjectModel(level.doc, "setup_0000", "base:model_barrel", err, sizeof(err)) == 1);
	CHECK(std::string(armouryFind(level.doc, "setup_0000").model_catalog_id) == "base:model_barrel");
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0001");
	CHECK(std::string(armouryFind(level.doc, "setup_0001").model_catalog_id) == "base:model_barrel");
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, err, sizeof(err)) == 1);
	CHECK(armouryFind(level.doc, "setup_0001").removed == 1);
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 60);

	/* What was written reads back as the same props. */
	Level again(level.text(FORGE_LEVEL_MEMBER_PADS), kSpawns, level.text(FORGE_LEVEL_MEMBER_OBJECTS),
		level.text(FORGE_LEVEL_MEMBER_VOLUMES), level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS),
		level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(std::string(armouryFind(again.doc, "setup_0000").model_catalog_id) == "base:model_barrel");
	CHECK(armouryFind(again.doc, "setup_0000").model_settable == 1);
	CHECK(armouryFind(again.doc, "setup_0001").removed == 1);
}

TEST_CASE("Grid Logic Zones bind native events and keep their box through save and reopen", "[forge-level-doc][level][graph][grid-logic-zone]")
{
	const auto templatePath = (fs::path(PD_SOURCE_DIR) / "dev-mods/grid_two_rooms/grid_two_rooms.pdarena").string();
	const char *const scenario = "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario";
	const auto source = readSavedLevel(templatePath.c_str(), scenario);
	Level level(levelMember(source.scenario, "pads.json"), levelMember(source.scenario, "spawns.json"),
		levelMember(source.scenario, "objects.json"), levelMember(source.scenario, "volumes.json"),
		levelMember(source.scenario, "setup.fields.json"), levelMember(source.scenario, "level.graph.json"));
	INFO(level.error);
	REQUIRE(level.doc);
	const auto graphMember = FORGE_LEVEL_MEMBER_LEVEL_GRAPH;
	const auto volumesMember = FORGE_LEVEL_MEMBER_VOLUMES;
	const std::string originalGraph = level.text(graphMember);
	const std::string originalVolumes = level.text(volumesMember);
	const auto volumeRow = [](const std::string &text, const std::string &id) {
		const size_t at = text.find("\"volume_id\": \"" + id + "\"");
		const size_t start = text.rfind('{', at);
		const size_t end = text.find('}', at);
		return at == std::string::npos || start == std::string::npos || end == std::string::npos
			? std::string() : text.substr(start, end - start + 1);
	};
	char err[256] = {}, id[32] = {}, early[32] = {}, leave[32] = {};
	char tiny[] = "!";
	const f32 half[] = {24.0f, 48.0f, 64.0f};
	const f32 resized[] = {32.0f, 60.0f, 72.0f};
	CHECK(forgeLevelDocLogicZoneCount(level.doc) == 0);
	CHECK_FALSE(forgeLevelDocAddLogicZone(level.doc, "pad_0001", half, tiny, sizeof(tiny), err, sizeof(err)));
	CHECK(std::string(tiny) == "!");
	CHECK_FALSE(forgeLevelDocAddLogicZone(level.doc, "pad_9999", half, id, sizeof(id), err, sizeof(err)));
	CHECK(level.text(graphMember) == originalGraph);
	CHECK(level.text(volumesMember) == originalVolumes);
	REQUIRE(forgeLevelDocAddLogicZone(level.doc, "pad_0001", half, id, sizeof(id), err, sizeof(err)));
	CHECK(std::string(id) == "volume_pad_9999");
	forge_level_logic_zone_t zone = {};
	REQUIRE(forgeLevelDocGetLogicZone(level.doc, 0, &zone));
	CHECK(std::string(zone.pad_ref) == "pad_0001");
	CHECK_FALSE(zone.referenced);
	CHECK(forgeLevelDocTriggerCount(level.doc) == 0);
	CHECK(occurrences(level.text(graphMember), kVolumeNode) == occurrences(originalGraph, kVolumeNode) + 1);
	CHECK(occurrences(level.text(graphMember), "\"input\": \"exec\"") == occurrences(originalGraph, "\"input\": \"exec\""));
	CHECK(level.text(graphMember).find("event.volume.entered") == std::string::npos);
	CHECK_FALSE(forgeLevelDocUpdateLogicZone(level.doc, id, "pad_0001", half, err, sizeof(err)));
	CHECK_FALSE(err[0]);
	REQUIRE(forgeLevelDocAddLogicZoneEvent(level.doc, id, FORGE_LEVEL_LOGIC_ZONE_ENTER, early, sizeof(early), err, sizeof(err)));
	REQUIRE(forgeLevelDocAddLogicZoneEvent(level.doc, id, FORGE_LEVEL_LOGIC_ZONE_LEAVE, leave, sizeof(leave), err, sizeof(err)));
	CHECK(std::string(early) == std::string("grid.zone.") + id + ".enter");
	CHECK(std::string(leave) == std::string("grid.zone.") + id + ".leave");
	const std::string withEvents = level.text(graphMember);
	CHECK_FALSE(forgeLevelDocAddLogicZoneEvent(level.doc, id, FORGE_LEVEL_LOGIC_ZONE_ENTER, nullptr, 0, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocRemoveLogicZone(level.doc, id, err, sizeof(err)));
	CHECK(level.text(graphMember) == withEvents);

	char startId[32] = {};
	const f32 startPos[] = {1000.0f, 50.0f, 0.0f};
	REQUIRE(forgeLevelDocAddSpawn(level.doc, startPos, kFacing, "any", startId, sizeof(startId), err, sizeof(err)));
	forge_level_spawn_t start = {};
	REQUIRE(forgeLevelDocGetSpawn(level.doc, forgeLevelDocSpawnCount(level.doc) - 1, &start));
	const std::string primaryId = std::string("volume_") + start.pad_ref;
	const std::string primary = volumeRow(level.text(volumesMember), primaryId);
	REQUIRE_FALSE(primary.empty());
	REQUIRE(forgeLevelDocUpdateLogicZone(level.doc, id, start.pad_ref, resized, err, sizeof(err)));
	const std::string movedVolumes = level.text(volumesMember);
	CHECK(volumeRow(movedVolumes, primaryId) == primary);
	CHECK(movedVolumes.find(primary) < movedVolumes.find(volumeRow(movedVolumes, id)));
	const f32 movedPos[] = {1100.0f, 50.0f, 0.0f};
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, startId, movedPos, kFacing, err, sizeof(err)));
	forge_level_spawn_t moved = {};
	REQUIRE(forgeLevelDocGetSpawn(level.doc, forgeLevelDocSpawnCount(level.doc) - 1, &moved));
	CHECK(std::string(moved.pad_ref) != start.pad_ref);
	REQUIRE(forgeLevelDocGetLogicZone(level.doc, 0, &zone));
	CHECK(std::string(zone.pad_ref) == start.pad_ref);
	REQUIRE(forgeLevelDocUpdateLogicZone(level.doc, id, "pad_0005", resized, err, sizeof(err)));
	CHECK(volumeRow(level.text(volumesMember), id).find("\"room_ref\": \"room_2\"") != std::string::npos);
	CHECK(volumeRow(level.text(volumesMember), "volume_pad_0001") == volumeRow(originalVolumes, "volume_pad_0001"));

	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, scenario, &overrides, err, sizeof(err)));
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	struct Scratch {
		fs::path root;
		~Scratch() { std::error_code ignored; fs::remove_all(root, ignored); }
	} scratch{fs::temp_directory_path() / ("pd2-grid-zone-" + std::to_string(stamp))};
	fs::create_directories(scratch.root);
	const std::string root = scratch.root.string();
	mod_author_request_t request = {};
	request.template_path = templatePath.c_str();
	request.mods_root = root.c_str();
	request.display_name = "Grid Zone";
	request.overrides = overrides.entries;
	request.override_count = overrides.count;
	mod_author_result_t result = {};
	REQUIRE(modAuthorSave(&request, &result, err, sizeof(err)) == MOD_AUTHOR_OK);
	const auto saved = readSavedLevel(result.archive_path, scenario);
	const std::string savedGraph = levelMember(saved.scenario, "level.graph.json");
	Level reopened(levelMember(saved.scenario, "pads.json"), levelMember(saved.scenario, "spawns.json"),
		levelMember(saved.scenario, "objects.json"), levelMember(saved.scenario, "volumes.json"),
		levelMember(saved.scenario, "setup.fields.json"), savedGraph);
	INFO(reopened.error);
	REQUIRE(reopened.doc);
	REQUIRE(forgeLevelDocGetLogicZone(reopened.doc, 0, &zone));
	CHECK(std::string(zone.volume_id) == id);
	CHECK(std::string(zone.pad_ref) == "pad_0005");
	CHECK(std::equal(std::begin(zone.half_size), std::end(zone.half_size), resized));
	CHECK(zone.referenced);
	CHECK(reopened.text(volumesMember) == level.text(volumesMember));
	std::unique_ptr<mission_graph_program_t, decltype(&missionGraphProgramFree)> program(
		missionGraphProgramCompileLevel(savedGraph.data(), savedGraph.size(), missionGraphModuleKnown, err, sizeof(err)),
		missionGraphProgramFree);
	REQUIRE(program);
	REQUIRE(missionGraphProgramVolumeCount(program.get()) == 1);
	CHECK(std::string(missionGraphProgramVolumeAt(program.get(), 0)) == id);
	CHECK(missionGraphProgramEventNodeCount(program.get(), MISSION_GRAPH_EVENT_VOLUME_ENTERED) == 1);
	CHECK(missionGraphProgramEventNodeCount(program.get(), MISSION_GRAPH_EVENT_VOLUME_LEFT) == 1);
	std::string noEvents = savedGraph;
	const auto removeEvent = [&](const char *event, const char *kind) {
		noEvents = replaced(noEvents, std::string(",\n    { \"id\": \"") + event + "\", \"kind\": \"" + kind
			+ "\", \"volume\": \"" + id + "\" }", "");
	};
	removeEvent(early, "event.volume.entered");
	removeEvent(leave, "event.volume.left");
	REQUIRE(forgeLevelDocReplaceLogic(reopened.doc, noEvents.data(), noEvents.size(), err, sizeof(err)));
	REQUIRE(forgeLevelDocGetLogicZone(reopened.doc, 0, &zone));
	CHECK_FALSE(zone.referenced);
	REQUIRE(forgeLevelDocRemoveLogicZone(reopened.doc, id, err, sizeof(err)));
	CHECK(forgeLevelDocLogicZoneCount(reopened.doc) == 0);
	CHECK(reopened.text(volumesMember).find(id) == std::string::npos);
	CHECK(reopened.text(graphMember).find(id) == std::string::npos);
}

TEST_CASE("Grid edits native executable logic without replacing its level data", "[forge-level-doc][level][graph]")
{
	const auto templatePath = (fs::path(PD_SOURCE_DIR) / "dev-mods/grid_two_rooms/grid_two_rooms.pdarena").string();
	const char *const scenario = "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario";
	const auto original = readSavedLevel(templatePath.c_str(), scenario);
	Level level(levelMember(original.scenario, "pads.json"), levelMember(original.scenario, "spawns.json"),
		levelMember(original.scenario, "objects.json"), levelMember(original.scenario, "volumes.json"),
		levelMember(original.scenario, "setup.fields.json"), levelMember(original.scenario, "level.graph.json"));
	INFO(level.error);
	REQUIRE(level.doc);
	char err[256] = {}, pickup[32] = {};
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 0, kPlace, kFacing,
		pickup, sizeof(pickup), err, sizeof(err)));
	REQUIRE(forgeLevelDocSetPickupWeapon(level.doc, pickup, "base:dragon", "base:model_roofgun", 256, err, sizeof(err)));
	const f32 halfSize[] = {25.0f, 40.0f, 60.0f};
	REQUIRE(forgeLevelDocAddTrigger(level.doc, FORGE_LEVEL_TRIGGER_GIVE_PICKUP, "pad_0000", pickup, halfSize, err, sizeof(err)));
	forge_level_trigger_t trigger = {};
	REQUIRE(forgeLevelDocGetTrigger(level.doc, 0, &trigger));
	const auto member = FORGE_LEVEL_MEMBER_LEVEL_GRAPH;
	const std::string before = level.text(member);
	const std::string volumes = level.text(FORGE_LEVEL_MEMBER_VOLUMES);
	const std::string prefix = std::string("grid.trigger.") + trigger.volume_id;
	const std::string oldEdge = "{ \"from\": \"" + prefix + ".enter\", \"to\": \"" + prefix + ".give\", \"input\": \"exec\" }";
	const std::string newEdges = "{ \"from\": \"" + prefix + ".enter\", \"to\": \"grid.logic.carried\", \"input\": \"exec\" },\n"
		"    { \"from\": \"grid.logic.carried\", \"to\": \"" + prefix + ".give\", \"input\": \"exec\", \"output\": \"pass\" }";
	const std::string candidate = replaced(replaced(before, oldEdge, newEdges), "\"nodes\": [",
		"\"nodes\": [\n    { \"id\": \"grid.logic.carried\", \"kind\": \"gate.player.has_item\", \"item\": \"base:falcon2\" },");
	REQUIRE(candidate != before);
	const auto replace = [&](const std::string &graph) {
		return forgeLevelDocReplaceLogic(level.doc, graph.data(), graph.size(), err, sizeof(err));
	};
	CHECK_FALSE(replace(before));
	CHECK_FALSE(err[0]);
	for (const auto &bad : {
			candidate + "{}",
			replaced(candidate, "\"pads\": \"pads.json\"", "\"pads\": \"other.json\""),
			replaced(candidate, "\"file\": \"scene.glb\"", "\"file\": \"other.glb\""),
			replaced(candidate, "\"kind\": \"gate.pickup.native_rules\"", "\"kind\": \"gate.player.has_item\""),
			replaced(candidate, "\"gate.player.has_item\"", "\"gate.does_not_exist\""),
			replaced(candidate, "\"base:falcon2\"", "7"),
			replaced(candidate, "\"volume\": \"" + std::string(trigger.volume_id) + "\" }",
				"\"volume\": \"volume_missing\" }"),
			replaced(candidate, newEdges, replaced(newEdges, "\"output\": \"pass\"", "\"output\": \"out\""))}) {
		CAPTURE(bad.size());
		CHECK_FALSE(replace(bad));
		CHECK(err[0]);
		CHECK(level.text(member) == before);
		CHECK(forgeLevelDocTriggerCount(level.doc) == 1);
	}
	REQUIRE(replace(candidate));
	CHECK(level.text(member) == candidate);
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == volumes);
	CHECK(forgeLevelDocTriggerCount(level.doc) == 0); // the condition belongs to Logic, not the fixed trigger editor
	forge_level_logic_zone_t promoted = {};
	REQUIRE(forgeLevelDocGetLogicZone(level.doc, 0, &promoted));
	CHECK(std::string(promoted.volume_id) == trigger.volume_id);
	CHECK(promoted.referenced);
	CHECK_FALSE(replace(candidate));
	CHECK_FALSE(err[0]);

	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, scenario, &overrides, err, sizeof(err)));
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	struct Scratch {
		fs::path root;
		~Scratch() { std::error_code ignored; fs::remove_all(root, ignored); }
	} scratch{fs::temp_directory_path() / ("pd2-grid-logic-" + std::to_string(stamp))};
	fs::create_directories(scratch.root);
	const std::string root = scratch.root.string();
	mod_author_request_t request = {};
	request.template_path = templatePath.c_str();
	request.mods_root = root.c_str();
	request.display_name = "Grid Logic";
	request.overrides = overrides.entries;
	request.override_count = overrides.count;
	mod_author_result_t result = {};
	REQUIRE(modAuthorSave(&request, &result, err, sizeof(err)) == MOD_AUTHOR_OK);
	const auto saved = readSavedLevel(result.archive_path, scenario);
	const std::string savedGraph = levelMember(saved.scenario, "level.graph.json");
	Level reopened(levelMember(saved.scenario, "pads.json"), levelMember(saved.scenario, "spawns.json"),
		levelMember(saved.scenario, "objects.json"), levelMember(saved.scenario, "volumes.json"),
		levelMember(saved.scenario, "setup.fields.json"), savedGraph);
	INFO(reopened.error);
	REQUIRE(reopened.doc);
	CHECK(forgeLevelDocTriggerCount(reopened.doc) == 0);
	CHECK(reopened.text(FORGE_LEVEL_MEMBER_VOLUMES) == volumes);
	std::unique_ptr<mission_graph_program_t, decltype(&missionGraphProgramFree)> program(
		missionGraphProgramCompileLevel(savedGraph.data(), savedGraph.size(), missionGraphModuleKnown, err, sizeof(err)),
		missionGraphProgramFree);
	REQUIRE(program);
	CHECK(missionGraphProgramHandlesEvent(program.get(), MISSION_GRAPH_EVENT_PICKUP_TOUCHED));
	CHECK(missionGraphProgramHandlesEvent(program.get(), MISSION_GRAPH_EVENT_DOOR_USE));
	CHECK(missionGraphProgramHandlesEvent(program.get(), MISSION_GRAPH_EVENT_DOOR_APPROACH));
	struct Inventory {
		bool hasWeapon;
		std::string granted;
	} inventory{false, {}};
	const auto run = [](const char *kind, const char *, const mission_graph_params_t *params,
			const mission_graph_event_t *, void *user) -> s32 {
		auto &state = *static_cast<Inventory *>(user);
		if (strcmp(kind, "gate.player.has_item") == 0) {
			return state.hasWeapon && strcmp(missionGraphParamText(params, "item"), "base:falcon2") == 0;
		}
		if (strcmp(kind, "og.pickup.give") != 0) return 0;
		state.granted = missionGraphParamText(params, "object");
		return 1;
	};
	mission_graph_event_t entered = {};
	entered.kind = MISSION_GRAPH_EVENT_VOLUME_ENTERED;
	entered.volume = trigger.volume_id;
	entered.player = 0;
	entered.objective_index = -1;
	CHECK(missionGraphProgramFire(program.get(), &entered, run, &inventory) == 0);
	CHECK(inventory.granted.empty());
	inventory.hasWeapon = true;
	CHECK(missionGraphProgramFire(program.get(), &entered, run, &inventory) == 1);
	CHECK(inventory.granted == pickup);
	REQUIRE(replace(before));
	CHECK(forgeLevelDocTriggerCount(level.doc) == 1);
	CHECK(forgeLevelDocLogicZoneCount(level.doc) == 0);
}

TEST_CASE("Grid decorations save as independent native pass-through basic props", "[forge-level-doc][grid-decoration]")
{
	const auto templatePath = (fs::path(PD_SOURCE_DIR) / "dev-mods/blank_room/blank_room.pdarena").string();
	const char *const scenario = "dependencies/assets/scenarios/scenario_blank_room.pdscenario";
	const auto original = readSavedLevel(templatePath.c_str(), scenario);
	Level level(levelMember(original.scenario, "pads.json"), levelMember(original.scenario, "spawns.json"),
		levelMember(original.scenario, "objects.json"), levelMember(original.scenario, "volumes.json"),
		levelMember(original.scenario, "setup.fields.json"), levelMember(original.scenario, "level.graph.json"));
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	const char *const model = "base:model_crate2_cq";
	char err[256] = {}, propId[32] = {}, decorationId[32] = {};
	const f32 place[] = {100.0f, 175.0f, -200.0f};
	const f32 facing[] = {0.0f, 0.0f, 1.0f};
	REQUIRE(forgeLevelDocAddProp(level.doc, model, place, facing, propId, sizeof(propId), err, sizeof(err)));
	REQUIRE(forgeLevelDocAddDecoration(level.doc, model, place, facing, decorationId, sizeof(decorationId), err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocIsDecoration(level.doc, propId));
	CHECK_FALSE(forgeLevelDocIsDecoration(level.doc, "setup_9999"));
	CHECK(forgeLevelDocIsDecoration(level.doc, decorationId));
	const auto decoration = armouryFind(level.doc, decorationId);
	CHECK(std::string(decoration.kind) == "prop");
	CHECK(std::string(decoration.model_catalog_id) == model);
	CHECK(std::string(decoration.weapon_catalog_id).empty());
	forge_level_pad_t pose = {};
	REQUIRE(forgeLevelDocGetDecorationPose(level.doc, decorationId, &pose));
	CHECK(pose.position[1] == place[1]);
	CHECK(pose.look[2] == facing[2]);
	const auto pristinePads = level.text(FORGE_LEVEL_MEMBER_PADS);
	const f32 moved[] = {150.0f, 180.0f, -230.0f};
	CHECK_FALSE(forgeLevelDocMoveDecoration(level.doc, propId, moved, facing, err, sizeof(err)));
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pristinePads);

	constexpr u32 flags = OBJFLAG_INVINCIBLE | OBJFLAG_UNCOLLECTABLE | OBJFLAG_CANNOT_ACTIVATE | OBJFLAG_AISEETHROUGH;
	constexpr u32 flags2 = OBJFLAG2_NOFALL | OBJFLAG2_IMMUNETOGUNFIRE | OBJFLAG2_SHOOTTHROUGH
		| OBJFLAG2_IMMUNETOEXPLOSIONS | OBJFLAG2_THROWTHROUGH;
	CHECK(forgeLevelDecorationFlagsMatch(flags, flags2, OBJFLAG3_WALKTHROUGH));
	CHECK_FALSE(forgeLevelDecorationFlagsMatch(flags | OBJFLAG_FALL, flags2, OBJFLAG3_WALKTHROUGH));
	CHECK_FALSE(forgeLevelDecorationFlagsMatch(flags, flags2 & ~OBJFLAG2_SHOOTTHROUGH, OBJFLAG3_WALKTHROUGH));
	CHECK_FALSE(forgeLevelDecorationFlagsMatch(flags, flags2, OBJFLAG3_WALKTHROUGH | OBJFLAG3_INTERACTABLE));

	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, scenario, &overrides, err, sizeof(err)));
	const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	struct Scratch {
		fs::path root;
		~Scratch() { std::error_code error; fs::remove_all(root, error); }
	} scratch{fs::temp_directory_path() / ("pd2-grid-decoration-" + std::to_string(stamp))};
	REQUIRE(fs::create_directory(scratch.root));
	const std::string root = scratch.root.string();
	mod_author_request_t request = {};
	request.template_path = templatePath.c_str();
	request.mods_root = root.c_str();
	request.display_name = "Grid Decoration";
	request.tool = "pd-tests";
	request.overrides = overrides.entries;
	request.override_count = overrides.count;
	mod_author_result_t result = {};
	const s32 status = modAuthorSave(&request, &result, err, sizeof(err));
	INFO(err);
	REQUIRE(status == MOD_AUTHOR_OK);
	const auto saved = readSavedLevel(result.archive_path, scenario);
	const auto pads = levelMember(saved.scenario, "pads.json");
	const auto spawns = levelMember(saved.scenario, "spawns.json");
	const auto objects = levelMember(saved.scenario, "objects.json");
	const auto volumes = levelMember(saved.scenario, "volumes.json");
	const auto fields = levelMember(saved.scenario, "setup.fields.json");
	const auto graph = levelMember(saved.scenario, "level.graph.json");
	CHECK(objects.find("\"flags\": \"0x06120000\",\n      \"flags2\": \"0x0220c100\",\n"
		"      \"flags3\": \"0x00000400\"") != std::string::npos);
	CHECK(fields.find(fieldRow(decorationId, "prop", "base.flags", "u32_hex", "0x06120000")) != std::string::npos);
	CHECK(fields.find(fieldRow(decorationId, "prop", "base.flags2", "u32_hex", "0x0220c100")) != std::string::npos);
	CHECK(fields.find(fieldRow(decorationId, "prop", "base.flags3", "u32_hex", "0x00000400")) != std::string::npos);
	CHECK(graph.find(decorationId) == std::string::npos);
	Level reopened(pads, spawns, objects, volumes, fields, graph);
	INFO(reopened.error);
	REQUIRE(reopened.doc != nullptr);
	CHECK_FALSE(forgeLevelDocIsDecoration(reopened.doc, propId));
	REQUIRE(forgeLevelDocIsDecoration(reopened.doc, decorationId));
	REQUIRE(forgeLevelDocGetDecorationPose(reopened.doc, decorationId, &pose));
	CHECK(pose.position[1] == place[1]);
	REQUIRE(forgeLevelDocMoveDecoration(reopened.doc, decorationId, moved, facing, err, sizeof(err)));
	REQUIRE(forgeLevelDocGetDecorationPose(reopened.doc, decorationId, &pose));
	CHECK(pose.position[0] == moved[0]);
	CHECK(pose.position[1] == moved[1]);
	CHECK(std::string(armouryFind(reopened.doc, decorationId).record_id) == decorationId);

	Level committed(pads, spawns, objects, volumes, fields, graph);
	Level replay(pads, spawns, objects, volumes, fields, graph);
	REQUIRE(committed.doc != nullptr);
	REQUIRE(replay.doc != nullptr);
	const f32 firstLiveMove[] = {125.0f, place[1], -215.0f};
	const f32 lastLiveMove[] = {moved[0], place[1], moved[2]};
	REQUIRE(forgeLevelDocMoveDecoration(committed.doc, decorationId, firstLiveMove, facing, err, sizeof(err)));
	REQUIRE(forgeLevelDocMoveDecoration(committed.doc, decorationId, lastLiveMove, facing, err, sizeof(err)));
	REQUIRE(forgeLevelDocMoveDecoration(replay.doc, decorationId, lastLiveMove, facing, err, sizeof(err)));
	CHECK(committed.text(FORGE_LEVEL_MEMBER_PADS) != pads);
	CHECK(committed.text(FORGE_LEVEL_MEMBER_VOLUMES) != volumes);
	CHECK(replay.text(FORGE_LEVEL_MEMBER_PADS) == committed.text(FORGE_LEVEL_MEMBER_PADS));
	CHECK(replay.text(FORGE_LEVEL_MEMBER_VOLUMES) == committed.text(FORGE_LEVEL_MEMBER_VOLUMES));
	CHECK(replay.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
	CHECK(replay.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
	CHECK(replay.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);
	CHECK(replay.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == graph);

	const auto linkedFields = replaced(fields, fieldRow(decorationId, "prop", "base.flags3", "u32_hex", "0x00000400"),
		fieldRow(decorationId, "prop", "base.flags3", "u32_hex", "0x00000400", propId));
	Level linked(pads, spawns, objects, volumes, linkedFields, graph);
	INFO(linked.error);
	REQUIRE(linked.doc != nullptr);
	CHECK_FALSE(forgeLevelDocIsDecoration(linked.doc, decorationId));
	CHECK_FALSE(forgeLevelDocGetDecorationPose(linked.doc, decorationId, &pose));
}

TEST_CASE("The Blank Room, a level that holds nothing of the original game, is edited and saved as a level",
          "[forge-level-doc]")
{
	/* dev-mods/blank_room: one room, eight starts, no objects, every byte made
	 * by its builder. It is the template a level that can be shared starts
	 * from, so the level document has to read it as it is, and the mod author
	 * has to make a level from it. */
	const fs::path templatePath = fs::path(PD_SOURCE_DIR) / "dev-mods/blank_room/blank_room.pdarena";
	const char *const scenarioMember = "dependencies/assets/scenarios/scenario_blank_room.pdscenario";
	const std::string templateText = templatePath.string();
	const SavedLevel room = readSavedLevel(templateText.c_str(), scenarioMember);
	CHECK(room.arenaId == "mod_blank_room:arena_blank_room");
	CHECK(room.arenaName == "Blank Room");
	CHECK(room.arenaScenario == "mod_blank_room:scenario_blank_room");
	CHECK(room.scenarioId == room.arenaScenario);

	Level level(levelMember(room.scenario, "pads.json"), levelMember(room.scenario, "spawns.json"),
		levelMember(room.scenario, "objects.json"), levelMember(room.scenario, "volumes.json"),
		levelMember(room.scenario, "setup.fields.json"), levelMember(room.scenario, "level.graph.json"));
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(forgeLevelDocPadCount(level.doc) == 8);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 8);
	CHECK(forgeLevelDocObjectCount(level.doc) == 0);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 8);
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 0);
	CHECK(occurrences(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH), kVolumeNode) == 8);
	/* The graph carries the pickup and door rules a level must have. */
	CHECK(occurrences(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH), "\"kind\": \"event.pickup.touched\"") == 1);
	CHECK(occurrences(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH), "\"edges\": [") == 1);
	/* Every start stands inside the room and faces its middle. */
	for (s32 i = 0; i < forgeLevelDocPadCount(level.doc); i++) {
		forge_level_pad_t pad = {};
		REQUIRE(forgeLevelDocGetPad(level.doc, i, &pad) == 1);
		CAPTURE(i);
		CHECK(std::fabs(pad.position[0]) < 1200.0f);
		CHECK(std::fabs(pad.position[2]) < 1200.0f);
		CHECK(pad.position[1] == 50.0f);
		CHECK(pad.position[0] * pad.look[0] + pad.position[2] * pad.look[2] < 0.0f);
	}

	/* A start is added and the level is saved under a name: only the Blank
	 * Room's own data and the new rows are in it. */
	char err[256] = { 0 };
	char id[32] = { 0 };
	const f32 place[3] = { 300.0f, 50.0f, -700.0f };
	const f32 facing[3] = { 0.0f, 0.0f, 1.0f };
	REQUIRE(forgeLevelDocAddSpawn(level.doc, place, facing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
	CHECK(std::string(id) == "spawn_0008");
	REQUIRE(forgeLevelDocSetCaseHome(level.doc, 0, "pad_0000", err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocAddCaseRespawn(level.doc, 0, "pad_0008", err, sizeof(err)) == 1);
	forge_level_overrides_t overrides;
	REQUIRE(forgeLevelDocOverrides(level.doc, scenarioMember, &overrides, err, sizeof(err)) == 1);
	REQUIRE(overrides.count == 5);   /* pads, spawns, bounds, graph, intro */

	const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("pd2-forge-blank-" + std::to_string(stamp));
	fs::create_directories(root);
	const std::string rootText = root.string();
	mod_author_request_t request = {};
	request.template_path = templateText.c_str();
	request.mods_root = rootText.c_str();
	request.display_name = "Blank Copy";
	request.tool = "pd-tests";
	request.overrides = overrides.entries;
	request.override_count = overrides.count;
	mod_author_result_t result = {};
	const s32 code = modAuthorSave(&request, &result, err, sizeof(err));
	INFO("save error: " << err);
	REQUIRE(code == MOD_AUTHOR_OK);
	CHECK(std::string(result.catalog_id) == "mod:arena_blank_copy");

	const SavedLevel saved = readSavedLevel(result.archive_path, scenarioMember);
	CHECK(saved.arenaId == "mod:arena_blank_copy");
	CHECK(saved.arenaName == "Blank Copy");
	CHECK(saved.scenarioId.rfind("mod:arena_blank_copy__", 0) == 0);
	CHECK(saved.arenaScenario == saved.scenarioId);
	CHECK(saved.record.find("\"source_counts\": { \"pads\": 9, \"volumes\": 9, \"waypoints\": 0, "
		"\"waygroups\": 0, \"covers\": 0, \"paths\": 1 }") != std::string::npos);
	/* Nothing of the template's own id is left in the level's graph. */
	const std::string savedGraph = levelMember(saved.scenario, "level.graph.json");
	CHECK(occurrences(savedGraph, "mod_blank_room:") == 0);
	CHECK(occurrences(savedGraph, kVolumeNode) == 9);
	/* The geometry is the template's, byte for byte. */
	CHECK(levelSha(levelMember(saved.scenario, "scene.glb")) == levelSha(levelMember(room.scenario, "scene.glb")));
	CHECK(levelSha(levelMember(saved.scenario, "collision.obj")) ==
		levelSha(levelMember(room.scenario, "collision.obj")));
	const std::string savedIntro = levelMember(saved.scenario, "intro.json");
	CHECK(savedIntro == level.text(FORGE_LEVEL_MEMBER_INTRO));
	scenario_intro_t compiled = {};
	REQUIRE(scenarioIntroParse(savedIntro.data(), savedIntro.size(), nullptr, &compiled, err, sizeof(err)) == 1);
	const std::vector<s32> expected = { INTROCMD_CASE, 0, 0, INTROCMD_CASERESPAWN, 0, 8 };
	CHECK(std::vector<s32>(compiled.words, compiled.words + compiled.word_count) == expected);
	scenarioIntroFree(&compiled);
	{
		Level again(levelMember(saved.scenario, "pads.json"), levelMember(saved.scenario, "spawns.json"),
			levelMember(saved.scenario, "objects.json"), levelMember(saved.scenario, "volumes.json"),
			levelMember(saved.scenario, "setup.fields.json"), savedGraph, savedIntro);
		INFO(again.error);
		REQUIRE(again.doc != nullptr);
		CHECK(forgeLevelDocPadCount(again.doc) == 9);
		CHECK(forgeLevelDocSpawnCount(again.doc) == 9);
		CHECK(forgeLevelDocCaseMarkerCount(again.doc) == 2);
		CHECK(forgeLevelDocMemberChanged(again.doc, FORGE_LEVEL_MEMBER_INTRO) == 0);
	}

	/* The saved level goes to the archive validator in CI: it names no asset
	 * the validator cannot know. */
	const char *artifacts = std::getenv("PD2_TEST_ARTIFACT_DIR");
	if (artifacts && artifacts[0]) {
		std::error_code copyError;
		const fs::path dir = fs::path(artifacts) / "clones";
		fs::create_directories(dir, copyError);
		fs::copy_file(result.archive_path, dir / fs::path(result.archive_path).filename(),
			fs::copy_options::overwrite_existing, copyError);
	}

	/* And the empty level takes objects made from nothing (not handed to the
	 * validator: they name the game's own stand-in models, which it only
	 * knows beside a base install). */
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 0, place, facing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0000");
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0, place, facing, id, sizeof(id),
		err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0001");
	CHECK(forgeLevelDocObjectCount(level.doc) == 2);
	CHECK(forgeLevelDocPadCount(level.doc) == 11);
	CHECK(occurrences(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH), kVolumeNode) == 11);
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH).find("\"objects\": 2") != std::string::npos);
	Level edited(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
		level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS), level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(edited.error);
	REQUIRE(edited.doc != nullptr);
	CHECK(forgeLevelDocObjectCount(edited.doc) == 2);

	std::error_code ec;
	fs::remove_all(root, ec);
}

TEST_CASE("Every base level is read and written back unchanged (local, needs PD2_BASE_TEMPLATE_DIR)",
          "[forge-level-doc][.local]")
{
	const char *dir = std::getenv("PD2_BASE_TEMPLATE_DIR");
	if (!dir || !dir[0]) {
		WARN("PD2_BASE_TEMPLATE_DIR is not set; the base levels were not read");
		return;
	}
	int levels = 0;
	long padTotal = 0;
	long objectTotal = 0;
	long removableTotal = 0;
	long alreadyOut = 0;
	long graphBytes = 0;
	for (const char *sub : { "arenas", "scenarios" }) {
		const fs::path folder = fs::path(dir) / sub;
		if (!fs::is_directory(folder)) continue;
		for (const auto &entry : fs::directory_iterator(folder)) {
			const std::string ext = entry.path().extension().string();
			if (ext != ".pdarena" && ext != ".pdscenario") continue;
			std::string scenario = levelReadFile(entry.path());
			if (ext == ".pdarena") {
				const std::string stem = entry.path().stem().string();
				/* base_arena_x.pdarena holds dependencies/assets/scenarios/base_scenario_x.pdscenario. */
				std::string inner = "dependencies/assets/scenarios/" + stem + ".pdscenario";
				const size_t at = inner.find("base_arena_");
				if (at != std::string::npos) inner.replace(at, strlen("base_arena_"), "base_scenario_");
				scenario = levelMember(scenario, inner.c_str());
				if (scenario.empty()) continue;
			}
			const std::string pads = levelMember(scenario, "pads.json");
			const std::string spawns = levelMember(scenario, "spawns.json");
			const std::string objects = levelMember(scenario, "objects.json");
			const std::string volumes = levelMember(scenario, "volumes.json");
			const std::string fields = levelMember(scenario, "setup.fields.json");
			const std::string graph = levelMember(scenario, "level.graph.json");
			if (pads.empty() || spawns.empty() || objects.empty() || volumes.empty() || fields.empty() ||
					graph.empty()) {
				continue;
			}
			/* Read with the level's own graph: a base level that the document
			 * refused here would be one whose graph and bounds rows differ. */
			const std::string intro = levelMember(scenario, "intro.json");
			const std::string covers = levelMember(scenario, "navigation/covers.json");
			Level level(pads, spawns, objects, volumes, fields, graph, intro, "", covers);
			INFO(entry.path().string() << ": " << level.error);
			REQUIRE(level.doc != nullptr);
			const std::string portals = levelMember(scenario, "portals.json");
			scenario_source_portal_row_t *portalRows = nullptr;
			s32 portalCount = 0;
			char portalError[200] = {};
			const s32 parsedPortals = scenarioPortalSourceRead(portals.data(), portals.size(),
				&portalRows, &portalCount, portalError, sizeof(portalError));
			std::unique_ptr<scenario_source_portal_row_t, decltype(&std::free)> ownedRows(portalRows, std::free);
			INFO(portalError);
			REQUIRE(parsedPortals);
			CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
			CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
			CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
			CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == volumes);
			CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);
			CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == graph);
			CHECK(level.text(FORGE_LEVEL_MEMBER_COVERS) == covers);
			if (!intro.empty()) {
				CHECK(level.text(FORGE_LEVEL_MEMBER_INTRO) == intro);
			}
			/* Every pad of a base level has its bounds row. */
			CHECK(forgeLevelDocVolumeCount(level.doc) == forgeLevelDocPadCount(level.doc));
			const size_t volumeNodes = occurrences(graph, kVolumeNode);
			CHECK(volumeNodes == static_cast<size_t>(forgeLevelDocVolumeCount(level.doc)));

			/* Adding a spawn and removing it again leaves the spawns as they
			 * were, which exercises the rebuilt text on real files. The pad it
			 * was given stays, and the graph has that pad's node. */
			char id[32] = { 0 };
			char err[200] = { 0 };
			REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
			REQUIRE(forgeLevelDocRemoveSpawn(level.doc, id, err, sizeof(err)) == 1);
			CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
			{
				const std::string edited = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
				CHECK(occurrences(edited, kVolumeNode) == volumeNodes + 1);
				Level grown(level.text(FORGE_LEVEL_MEMBER_PADS), spawns, objects,
					level.text(FORGE_LEVEL_MEMBER_VOLUMES), fields, edited);
				INFO(entry.path().string() << " with one more pad: " << grown.error);
				REQUIRE(grown.doc != nullptr);
				CHECK(forgeLevelDocPadCount(grown.doc) == forgeLevelDocPadCount(level.doc));
				graphBytes += static_cast<long>(graph.size());
			}

			/* Leaving out every object that can be left out gives a level that
			 * reads back, and putting each one back gives both object files
			 * their bytes. */
			std::vector<std::string> leftOut;
			for (s32 i = 0; i < forgeLevelDocObjectCount(level.doc); i++) {
				forge_level_object_t object = {};
				REQUIRE(forgeLevelDocGetObject(level.doc, i, &object) == 1);
				objectTotal++;
				if (object.removed) alreadyOut++;
				if (!object.removable || object.removed) continue;
				REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, object.record_id, 1, err, sizeof(err)) == 1);
				leftOut.push_back(object.record_id);
			}
			if (!leftOut.empty()) {
				CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) != objects);
				Level thinned(pads, spawns, level.text(FORGE_LEVEL_MEMBER_OBJECTS), volumes,
					level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS), graph);
				INFO(entry.path().string() << " with its objects left out: " << thinned.error);
				REQUIRE(thinned.doc != nullptr);
				CHECK(forgeLevelDocObjectCount(thinned.doc) == forgeLevelDocObjectCount(level.doc));
			}
			for (const std::string &record : leftOut) {
				REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, record.c_str(), 0, err, sizeof(err)) == 1);
			}
			CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
			CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);
			removableTotal += static_cast<long>(leftOut.size());
			levels++;
			padTotal += forgeLevelDocPadCount(level.doc);
		}
	}
	WARN("base levels read and written back unchanged: " << levels << " (" << padTotal << " pads, "
		<< objectTotal << " objects, " << removableTotal << " left out and put back, "
		<< alreadyOut << " already left out on every difficulty, " << graphBytes
		<< " bytes of level graph, each graph given one more trigger volume node)");
	CHECK(levels > 0);
}


/* A plan, and the document as it was opened, kept apart from the one edited. */
struct Live {
	forge_level_live_plan_t *plan = static_cast<forge_level_live_plan_t *>(calloc(1, sizeof(forge_level_live_plan_t)));
	forge_level_doc_t *opened = nullptr;
	char err[200] = { 0 };

	explicit Live(const forge_level_doc_t *doc) : opened(forgeLevelDocClone(doc)) {}
	~Live()
	{
		free(plan);
		forgeLevelDocFree(opened);
	}
	Live(const Live &) = delete;
	Live &operator=(const Live &) = delete;

	s32 planFor(const forge_level_doc_t *now) { return forgeLevelLivePlan(opened, now, plan, err, sizeof(err)); }
};

TEST_CASE("The world shows a level as it was loaded until its file changes", "[forge-level-doc][forge-level-live]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	Live live(level.doc);
	REQUIRE(live.plan != nullptr);
	REQUIRE(live.opened != nullptr);
	REQUIRE(live.planFor(level.doc) == 1);
	CHECK(live.plan->hide_count == 0);
	CHECK(live.plan->draw_count == 0);
	CHECK(live.plan->not_shown == 0);

	/* The kinds the world draws stand-ins for, and some it leaves to the markers. */
	CHECK(forgeLevelLiveKindShown("prop") == 1);
	CHECK(forgeLevelLiveKindShown("weapon_pickup") == 1);
	CHECK(forgeLevelLiveKindShown("multi_ammo_crate") == 1);
	CHECK(forgeLevelLiveKindShown("hoverbike") == 1);
	CHECK(forgeLevelLiveKindShown("door") == 0);
	CHECK(forgeLevelLiveKindShown("character_spawn") == 0);
	CHECK(forgeLevelLiveKindShown("tag") == 0);
	CHECK(forgeLevelLiveKindShown(nullptr) == 0);
}

TEST_CASE("A moved object is hidden where the stage has it and drawn where its file puts it",
          "[forge-level-doc][forge-level-live]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	Live live(level.doc);
	const f32 place[3] = { 7.0f, 0.0f, 9.0f };
	const f32 facing[3] = { 0.0f, 0.0f, 2.0f };
	REQUIRE(forgeLevelDocMoveObject(level.doc, "setup_0001", place, facing, live.err, sizeof(live.err)) == 1);
	REQUIRE(live.planFor(level.doc) == 1);
	INFO(live.err);
	REQUIRE(live.plan->hide_count == 1);
	CHECK(std::string(live.plan->hide[0].record_id) == "setup_0001");
	CHECK(std::string(live.plan->hide[0].kind) == "prop");
	CHECK(live.plan->hide[0].command == 1);
	REQUIRE(live.plan->draw_count == 1);
	const forge_level_live_draw_t &crate = live.plan->draw[0];
	CHECK(std::string(crate.record_id) == "setup_0001");
	CHECK(std::string(crate.model_catalog_id) == "base:model_crate");
	CHECK(crate.position[0] == 7.0f);
	CHECK(crate.position[2] == 9.0f);
	CHECK(crate.look[2] > 0.0f);
	CHECK(crate.up[1] > 0.0f);
	CHECK(crate.extra_scale == 256);
	/* Only moved, so it is drawn as the stage drew it. */
	CHECK(crate.looks_like_command == 1);
	CHECK(live.plan->not_shown == 0);

	/* The weapon spot names no model (the match gives it one), so moved it is
	 * drawn with what the stage shows at its place in the command list. */
	REQUIRE(forgeLevelDocMoveObject(level.doc, "setup_0000", kPlace, kFacing, live.err, sizeof(live.err)) == 1);
	REQUIRE(live.planFor(level.doc) == 1);
	REQUIRE(live.plan->hide_count == 2);
	CHECK(std::string(live.plan->hide[0].record_id) == "setup_0000");
	CHECK(live.plan->hide[0].command == 0);
	REQUIRE(live.plan->draw_count == 2);
	CHECK(std::string(live.plan->draw[0].record_id) == "setup_0000");
	CHECK(std::string(live.plan->draw[0].model_catalog_id).empty());
	CHECK(live.plan->draw[0].looks_like_command == 0);
	CHECK(live.plan->draw[0].position[2] == kPlace[2]);

	/* Taken back (the document as opened again), the world is the stage's. */
	forge_level_doc_t *back = forgeLevelDocClone(live.opened);
	REQUIRE(back != nullptr);
	REQUIRE(live.planFor(back) == 1);
	CHECK(live.plan->hide_count == 0);
	CHECK(live.plan->draw_count == 0);
	forgeLevelDocFree(back);
}

TEST_CASE("A new object is drawn, and one with nothing to draw it with is only counted",
          "[forge-level-doc][forge-level-live]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	Live live(level.doc);
	char id[32] = { 0 };
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0001", kPlace, kFacing, id, sizeof(id), live.err,
		sizeof(live.err)) == 1);
	CHECK(std::string(id) == "setup_0003");
	REQUIRE(live.planFor(level.doc) == 1);
	CHECK(live.plan->hide_count == 0);
	REQUIRE(live.plan->draw_count == 1);
	CHECK(std::string(live.plan->draw[0].record_id) == "setup_0003");
	CHECK(std::string(live.plan->draw[0].model_catalog_id) == "base:model_crate");
	CHECK(live.plan->draw[0].looks_like_command == -1);
	CHECK(live.plan->draw[0].position[0] == kPlace[0]);
	CHECK(live.plan->not_shown == 0);

	/* A copy of the weapon spot has no model of its own and nothing on the
	 * stage to look like: it keeps its marker only. */
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, id, sizeof(id), live.err,
		sizeof(live.err)) == 1);
	CHECK(std::string(id) == "setup_0004");
	REQUIRE(live.planFor(level.doc) == 1);
	CHECK(live.plan->hide_count == 0);
	CHECK(live.plan->draw_count == 1);
	CHECK(live.plan->not_shown == 1);
}

TEST_CASE("An object left out is hidden, and one put back that the stage never made is drawn",
          "[forge-level-doc][forge-level-live]")
{
	const std::string objects = objectsWithFlags();
	const std::string fields = fieldsWithFlags();
	{
		Level level(kPads, kSpawns, objects, kVolumes, fields);
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		Live live(level.doc);
		REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, live.err, sizeof(live.err)) == 1);
		REQUIRE(live.planFor(level.doc) == 1);
		REQUIRE(live.plan->hide_count == 1);
		CHECK(std::string(live.plan->hide[0].record_id) == "setup_0001");
		CHECK(live.plan->hide[0].command == 1);
		CHECK(live.plan->draw_count == 0);
		/* Put back, it is the stage's again. */
		REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 0, live.err, sizeof(live.err)) == 1);
		REQUIRE(live.planFor(level.doc) == 1);
		CHECK(live.plan->hide_count == 0);
		CHECK(live.plan->draw_count == 0);
	}
	{
		/* A level that already left the crate out: the stage never made it,
		 * so put back it needs a stand-in and there is nothing to hide. */
		const std::string objectsOut = replaced(objects, "\"flags2\": \"0x00000020\"", "\"flags2\": \"0x000000f0\"");
		const std::string fieldsOut = replaced(fields, "\"value\": \"0x00000020\"", "\"value\": \"0x000000f0\"");
		Level level(kPads, kSpawns, objectsOut, kVolumes, fieldsOut);
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		Live live(level.doc);
		REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 0, live.err, sizeof(live.err)) == 1);
		REQUIRE(live.planFor(level.doc) == 1);
		CHECK(live.plan->hide_count == 0);
		REQUIRE(live.plan->draw_count == 1);
		CHECK(std::string(live.plan->draw[0].record_id) == "setup_0001");
		CHECK(std::string(live.plan->draw[0].model_catalog_id) == "base:model_crate");
		CHECK(live.plan->draw[0].looks_like_command == -1);
	}
}

TEST_CASE("A file that is not the level as opened is refused, and the world shows none of it",
          "[forge-level-doc][forge-level-live]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char id[32] = { 0 };
	char err[200] = { 0 };
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0001", kPlace, kFacing, id, sizeof(id), err, sizeof(err)) == 1);
	/* "Opened" with four objects, now three: rows went missing. */
	Live live(level.doc);
	Level fewer;
	REQUIRE(fewer.doc != nullptr);
	CHECK(live.planFor(fewer.doc) == 0);
	CHECK(std::string(live.err) == "the level has 3 objects and had 4 when it was opened");
	CHECK(live.plan->hide_count == 0);
	CHECK(live.plan->draw_count == 0);
	CHECK(forgeLevelLivePlan(nullptr, level.doc, live.plan, err, sizeof(err)) == 0);
	CHECK(std::string(err) == "no level is open");
	CHECK(forgeLevelLivePlan(live.opened, level.doc, nullptr, err, sizeof(err)) == 0);
}

TEST_CASE("An object is given a size, which the world draws its stand-in at", "[forge-level-doc][forge-level-live]")
{
	const std::string fields = levelFields("pad_0001",
		",\n" + fieldRow("setup_0001", "prop", "base.extra_scale", "s32", "256"));
	Level level(kPads, kSpawns, kObjects, kVolumes, fields);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(forgeLevelDocObjectExtraScale(level.doc, 1) == 256);
	CHECK(forgeLevelDocObjectExtraScale(level.doc, 0) == 256);   /* no row: life size */
	CHECK(forgeLevelDocObjectExtraScale(level.doc, 9) == 256);   /* no such object */
	Live live(level.doc);
	REQUIRE(forgeLevelDocSetObjectScale(level.doc, "setup_0001", 512, live.err, sizeof(live.err)) == 1);
	CHECK(forgeLevelDocObjectExtraScale(level.doc, 1) == 512);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == levelFields("pad_0001",
		",\n" + fieldRow("setup_0001", "prop", "base.extra_scale", "s32", "512")));
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PADS) == 0);

	/* Redrawn: the stage's object is hidden and the stand-in is drawn at the new size. */
	REQUIRE(live.planFor(level.doc) == 1);
	REQUIRE(live.plan->hide_count == 1);
	CHECK(std::string(live.plan->hide[0].record_id) == "setup_0001");
	REQUIRE(live.plan->draw_count == 1);
	CHECK(live.plan->draw[0].extra_scale == 512);
	CHECK(live.plan->draw[0].looks_like_command == -1);
	CHECK(std::string(live.plan->draw[0].model_catalog_id) == "base:model_crate");

	/* Back to life size, it is the file that was read and the stage's again. */
	REQUIRE(forgeLevelDocSetObjectScale(level.doc, "setup_0001", 256, live.err, sizeof(live.err)) == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);
	REQUIRE(live.planFor(level.doc) == 1);
	CHECK(live.plan->hide_count == 0);
	CHECK(live.plan->draw_count == 0);

	/* What cannot be sized, and why; nothing changes. (This fixture's weapon
	 * spot has no size row either: its two files disagree about its weapon.) */
	CHECK(forgeLevelDocSetObjectScale(level.doc, "setup_0000", 512, live.err, sizeof(live.err)) == 0);
	CHECK(std::string(live.err) == "setup_0000 has no field that says its size, so it cannot be changed here");
	CHECK(forgeLevelDocSetObjectScale(level.doc, "setup_0002", 512, live.err, sizeof(live.err)) == 0);
	CHECK(std::string(live.err) == "setup_0002 has no field that says its size, so it cannot be changed here");
	CHECK(forgeLevelDocSetObjectScale(level.doc, "setup_0001", 0, live.err, sizeof(live.err)) == 0);
	CHECK(std::string(live.err) == "a size is 1 to 32767 (256 is life size), not 0");
	CHECK(forgeLevelDocSetObjectScale(level.doc, "setup_0009", 512, live.err, sizeof(live.err)) == 0);
	CHECK(std::string(live.err) == "the level has no object \"setup_0009\"");
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);

	/* One of the match's weapon places is sized by the match; a pickup pinned
	 * to one weapon has a size of its own. */
	Level armoury(kPads, kSpawns, armouryObjects(), kVolumes, armouryFields());
	INFO(armoury.error);
	REQUIRE(armoury.doc != nullptr);
	CHECK(forgeLevelDocSetObjectScale(armoury.doc, "setup_0000", 512, live.err, sizeof(live.err)) == 0);
	CHECK(std::string(live.err) ==
		"setup_0000 is one of the match's weapon places, which the match's weapon set sizes");
	CHECK(forgeLevelDocSetObjectScale(armoury.doc, "setup_0001", 512, live.err, sizeof(live.err)) == 1);
	CHECK(forgeLevelDocObjectExtraScale(armoury.doc, 1) == 512);
}

} // namespace
