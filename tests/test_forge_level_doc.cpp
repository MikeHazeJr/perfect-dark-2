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

/* pd-tests links the pure compiler without the live game module dispatch. */
extern "C" s32 missionGraphModuleKnown(const char *kind)
{
	for (const char *name : {"gate.pickup.native_rules", "og.pickup.collect", "gate.door.native_unlocked",
			"og.door.activate", "og.door.locked_message", "og.door.activate_automatic", "og.door.open",
			"og.door.close", "og.door.lock", "og.door.unlock", "og.pickup.give", "gate.object.kind",
			"gate.player.has_item"}) {
		if (kind && strcmp(name, kind) == 0) return 1;
	}
	return 0;
}

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

} // namespace

TEST_CASE("A level's placements are read, and an untouched document gives back its bytes",
          "[forge-level-doc]")
{
	Level level;
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(forgeLevelDocPadCount(level.doc) == 3);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 2);
	CHECK(forgeLevelDocObjectCount(level.doc) == 3);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 3);

	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetPad(level.doc, 2, &pad) == 1);
	CHECK(std::string(pad.pad_ref) == "pad_0002");
	CHECK(std::string(pad.room_ref) == "room_0003");
	CHECK(pad.position[0] == 50.0f);
	CHECK(pad.position[1] == 8.0f);
	CHECK(pad.look[2] == 1.0f);
	CHECK(pad.added == 0);
	CHECK(pad.has_volume == 1);
	CHECK(forgeLevelDocGetPad(level.doc, 3, &pad) == 0);
	CHECK(forgeLevelDocFindPad(level.doc, "pad_0001", &pad) == 1);
	CHECK(pad.position[0] == 30.0f);
	CHECK(forgeLevelDocFindPad(level.doc, "pad_0003", nullptr) == 0);
	CHECK(forgeLevelDocFindPad(level.doc, "pad_1", nullptr) == 0);

	forge_level_spawn_t spawn = {};
	REQUIRE(forgeLevelDocGetSpawn(level.doc, 1, &spawn) == 1);
	CHECK(std::string(spawn.spawn_id) == "spawn_0004");
	CHECK(std::string(spawn.pad_ref) == "pad_0001");
	CHECK(std::string(spawn.team) == "team_1");

	forge_level_object_t object = {};
	REQUIRE(forgeLevelDocGetObject(level.doc, 0, &object) == 1);
	CHECK(std::string(object.record_id) == "setup_0000");
	CHECK(std::string(object.kind) == "weapon_pickup");
	CHECK(std::string(object.weapon_catalog_id) == "base:falcon2");
	REQUIRE(forgeLevelDocGetObject(level.doc, 2, &object) == 1);
	CHECK(std::string(object.pad_ref).empty());

	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		CHECK(forgeLevelDocMemberChanged(level.doc, member) == 0);
	}
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == kPads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == kSpawns);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == kObjects);
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == kVolumes);
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_PADS)) == "pads.json");
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_SPAWNS)) == "spawns.json");
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_OBJECTS)) == "objects.json");
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_VOLUMES)) == "volumes.json");
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_SETUP_FIELDS)) == "setup.fields.json");
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == levelFields());
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_LEVEL_GRAPH)) == "level.graph.json");
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == levelGraph(3, 3, 3));
	CHECK(forgeLevelDocRead(nullptr, nullptr, 0) == nullptr);
	CHECK(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_COUNT) == nullptr);
	CHECK(forgeLevelDocMemberText(level.doc, -1, nullptr) == nullptr);
}

TEST_CASE("Grid cover authoring appends a fitted native row and keeps legacy cover rows",
          "[forge-level-doc][grid-navigation]")
{
	const std::string oldRow = R"({ "cover_ref": "cover_0000", "flags": "0x0020",
	    "position": [10.000000, 0.000000, 20.000000], "look": [1, 0, 0], "note": "legacy cover" })";
	const std::string source = std::string("{\"schema\":\"pd2.scenario.covers.v1\", \"rows\": [") + oldRow
		+ "], \"note\": \"legacy table\"}";
	const std::string oldPosition = "[10.000000, 0.000000, 20.000000]";
	const std::string destination = "[30.000000, 0.00000013, 40.000000]";
	const std::string pads = replaced(replaced(kPads, "\"look\": [0.000000, 0.000000, -1.000000]",
		"\"look\": [3.000000, 4.000000, 0.000000]"), "\"position\": [30.000000, 0.000000, 40.000000]",
		"\"note\": \"position\", \"metadata\": {\"position\": [91, 92, 93]}, \"position\": " + destination);
	const std::string paths = R"({"schema":"pd2.scenario.paths.v1","rows":[{"path_ref":"path_0000","flags":0,"pads":["pad_0000","pad_0001"]}]})";
	Level level(pads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", source, paths);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(level.text(FORGE_LEVEL_MEMBER_COVERS) == source);
	std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)> undo(
		forgeLevelDocClone(level.doc), forgeLevelDocFree);
	REQUIRE(undo != nullptr);

	char id[32] = {};
	char err[256] = {};
	REQUIRE(forgeLevelDocAddCoverAtPad(level.doc, "pad_0000", id, sizeof(id), err, sizeof(err)));
	CHECK(std::string(id) == "cover_0001");
	const std::string authored = level.text(FORGE_LEVEL_MEMBER_COVERS);
	CHECK(authored.find(oldRow) != std::string::npos);
	CHECK(authored.find("\"note\": \"legacy table\"") != std::string::npos);
	CHECK(authored.find("generate_routes") == std::string::npos);
	CHECK(authored.find(R"({ "cover_ref": "cover_0001", "flags": 0, "position": [10.000000, 0.000000, 20.000000], "look": [1.000000, 0.000000, 0.000000] })")
		!= std::string::npos);
	REQUIRE(forgeLevelDocCoverCount(level.doc) == 2);
	CHECK_FALSE(forgeLevelDocMoveCoverToPad(level.doc, "cover_0000", "pad_0000", err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocMoveCoverToPad(level.doc, "cover_0002", "pad_0001", err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocMoveCoverToPad(level.doc, "cover_0000", "pad_9999", err, sizeof(err)));
	CHECK(level.text(FORGE_LEVEL_MEMBER_COVERS) == authored);
	REQUIRE(forgeLevelDocMoveCoverToPad(level.doc, "cover_0000", "pad_0001", err, sizeof(err)));
	const std::string moved = replaced(authored, oldPosition, destination);
	CHECK(level.text(FORGE_LEVEL_MEMBER_COVERS) == moved);
	forge_level_cover_t cover = {};
	REQUIRE(forgeLevelDocGetCover(level.doc, 0, &cover));
	CHECK(std::string(cover.cover_ref) == "cover_0000");
	CHECK(cover.position[1] == 0.00000013f);
	CHECK_FALSE(forgeLevelDocMoveCoverToPad(level.doc, "cover_0000", "pad_0001", err, sizeof(err)));
	CHECK(level.text(FORGE_LEVEL_MEMBER_COVERS) == moved);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PATHS) == paths);
	CHECK_FALSE(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PADS));
	CHECK_FALSE(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PATHS));
	size_t originalSize = 0;
	const char *original = forgeLevelDocMemberText(undo.get(), FORGE_LEVEL_MEMBER_COVERS, &originalSize);
	REQUIRE(original != nullptr);
	CHECK(std::string(original, originalSize) == source);
	CHECK_FALSE(forgeLevelDocMemberChanged(undo.get(), FORGE_LEVEL_MEMBER_COVERS));
}

TEST_CASE("Grid requires a native cover table and a horizontal pad facing",
          "[forge-level-doc][grid-navigation]")
{
	Level missing;
	REQUIRE(missing.doc != nullptr);
	const auto member = FORGE_LEVEL_MEMBER_COVERS;
	CHECK(forgeLevelDocMemberOptional(member));
	size_t absentSize = 1;
	forgeLevelDocMemberText(missing.doc, member, &absentSize);
	CHECK(absentSize == 0);
	char id[32] = {};
	char err[256] = {};
	CHECK_FALSE(forgeLevelDocAddCoverAtPad(missing.doc, "pad_0000", id, sizeof(id), err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocMemberChanged(missing.doc, member));

	const std::string source = R"({"schema":"pd2.scenario.covers.v1", "generate_routes": false,
		"rows": [], "metadata": {"generate_routes": false}})";
	const std::string pads = replaced(kPads, "\"look\": [0.000000, 0.000000, -1.000000]",
		"\"look\": [0.000000, 1.000000, 0.000000]");
	Level level(pads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", source);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK_FALSE(forgeLevelDocAddCoverAtPad(level.doc, "pad_9999", id, sizeof(id), err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocAddCoverAtPad(level.doc, "pad_0000", id, sizeof(id), err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocMemberChanged(level.doc, member));
	CHECK(level.text(member) == source);
	REQUIRE(forgeLevelDocAddCoverAtPad(level.doc, "pad_0001", id, sizeof(id), err, sizeof(err)));
	CHECK(std::string(id) == "cover_0000");
	const std::string authored = level.text(member);
	CHECK(occurrences(authored, "\"generate_routes\": true") == 1);
	CHECK(authored.find("\"metadata\": {\"generate_routes\": false}") != std::string::npos);

	const std::string invalid = R"({"schema":"pd2.scenario.covers.v1","rows":[{"cover_ref":"cover_0001",
		"flags":0,"position":[0,0,0],"look":[1,0,0]}]})";
	Level malformed(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", invalid);
	REQUIRE(malformed.doc != nullptr);
	CHECK(malformed.text(member) == invalid);
	CHECK_FALSE(forgeLevelDocAddCoverAtPad(malformed.doc, "pad_0001", id, sizeof(id), err, sizeof(err)));
	CHECK(std::string(err).find("invalid cover row") != std::string::npos);
	CHECK(malformed.text(member) == invalid);

	const std::string duplicate = replaced(invalid, "\"cover_ref\":\"cover_0001\"",
		"\"cover_ref\":\"cover_0000\",\"cover_ref\":\"cover_0000\"");
	Level ambiguous(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", duplicate);
	REQUIRE(ambiguous.doc != nullptr);
	CHECK(forgeLevelDocCoverCount(ambiguous.doc) == 0);
	CHECK_FALSE(forgeLevelDocMoveCoverToPad(ambiguous.doc, "cover_0000", "pad_0001", err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocMemberChanged(ambiguous.doc, member));
	CHECK(ambiguous.text(member) == duplicate);
}

TEST_CASE("Grid cover authoring stays within the native cover index", "[forge-level-doc][grid-navigation]")
{
	const auto member = FORGE_LEVEL_MEMBER_COVERS;
	std::string rows = R"({"schema":"pd2.scenario.covers.v1","rows":[)";
	for (unsigned i = 0; i <= 256; ++i) {
		char id[32];
		snprintf(id, sizeof(id), "cover_%04u", i);
		if (i > 0) rows += ',';
		rows += std::string("{\"cover_ref\":\"") + id + R"(","flags":0,"position":[0,0,0],"look":[1,0,0]})";
		if (i < 255) continue;

		CAPTURE(i);
		const std::string source = rows + "]}";
		Level level(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", source);
		REQUIRE(level.doc != nullptr);
		char err[256] = {};
		CHECK_FALSE(forgeLevelDocAddCoverAtPad(level.doc, "pad_0001", nullptr, 0, err, sizeof(err)));
		CHECK(level.text(member) == source);
		if (i == 255) {
			REQUIRE(forgeLevelDocCoverCount(level.doc) == 256);
			REQUIRE(forgeLevelDocMoveCoverToPad(level.doc, "cover_0255", "pad_0001", err, sizeof(err)));
		} else {
			CHECK(forgeLevelDocCoverCount(level.doc) == 0);
			CHECK_FALSE(forgeLevelDocMoveCoverToPad(level.doc, "cover_0000", "pad_0001", err, sizeof(err)));
			CHECK_FALSE(forgeLevelDocMemberChanged(level.doc, member));
			CHECK(level.text(member) == source);
		}
	}
}

TEST_CASE("Grid requires a bot waypoint by changing only its native pad flag", "[forge-level-doc][grid-navigation]")
{
	const std::string oldField = "\"flags\": \"0x00185\", ";
	std::string pads = kPads;
	std::string enabled = replaced(pads, oldField, "\"flags\": \"0x00001185\", ");
	std::string disabled = replaced(pads, oldField, "\"flags\": \"0x00000185\", ");
	SECTION("hexadecimal flags preserve the other bits") {}
	SECTION("numeric flags preserve their representation and other bits") {
		pads = replaced(kPads, oldField, "\"flags\": 389, ");
		enabled = replaced(pads, "\"flags\": 389, ", "\"flags\": " + std::to_string(389 | PADFLAG_AIWALKDIRECT) + ", ");
		disabled = pads;
	}
	SECTION("the first change supplies an absent flag field") {
		pads = replaced(kPads, oldField, "");
		const std::string padRef = "\"pad_ref\": \"pad_0000\"";
		enabled = replaced(pads, padRef, "\"flags\": \"0x00001000\", " + padRef);
		disabled = replaced(enabled, "0x00001000", "0x00000000");
	}

	Level level(pads, kSpawns, kObjects, kVolumes, levelFields());
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocFindPad(level.doc, "pad_0000", &pad));
	CHECK_FALSE(pad.walk_direct);
	char err[256] = {};
	CHECK_FALSE(forgeLevelDocSetPadWalkDirect(level.doc, "pad_9999", 1, err, sizeof(err)));
	CHECK(err[0] != '\0');
	CHECK_FALSE(forgeLevelDocSetPadWalkDirect(level.doc, "pad_0000", 0, err, sizeof(err)));
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
	CHECK_FALSE(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PADS));

	REQUIRE(forgeLevelDocSetPadWalkDirect(level.doc, "pad_0000", 1, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocSetPadWalkDirect(level.doc, "pad_0000", 1, err, sizeof(err)));
	REQUIRE(forgeLevelDocFindPad(level.doc, "pad_0000", &pad));
	CHECK(pad.walk_direct);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == enabled);
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == kVolumes);
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == levelGraph(3, 3, 3));

	forge_level_overrides_t save = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, "scenario.pdscenario", &save, err, sizeof(err)));
	REQUIRE(save.count == 1);
	CHECK(std::string(save.entries[0].entry) == "scenario.pdscenario::pads.json");
	const std::string savedPads(static_cast<const char *>(save.entries[0].data), save.entries[0].len);
	CHECK(savedPads == enabled);
	Level reopened(savedPads, kSpawns, kObjects, kVolumes, levelFields());
	INFO(reopened.error);
	REQUIRE(reopened.doc != nullptr);
	REQUIRE(forgeLevelDocFindPad(reopened.doc, "pad_0000", &pad));
	CHECK(pad.walk_direct);
	CHECK(pad.position[0] == 10.0f);
	CHECK(pad.look[2] == -1.0f);
	REQUIRE(forgeLevelDocSetPadWalkDirect(reopened.doc, "pad_0000", 0, err, sizeof(err)));
	CHECK(reopened.text(FORGE_LEVEL_MEMBER_PADS) == disabled);
	REQUIRE(forgeLevelDocFindPad(reopened.doc, "pad_0000", &pad));
	CHECK_FALSE(pad.walk_direct);
}

TEST_CASE("Moving a start preserves a bot waypoint authored at its old position",
	"[forge-level-doc][grid-navigation]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char err[256] = {};
	char id[32] = {};
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)));
	forge_level_spawn_t start = {};
	const s32 index = forgeLevelDocSpawnCount(level.doc) - 1;
	REQUIRE(forgeLevelDocGetSpawn(level.doc, index, &start));
	const std::string waypoint = start.pad_ref;
	REQUIRE(forgeLevelDocSetPadWalkDirect(level.doc, waypoint.c_str(), 1, err, sizeof(err)));

	const f32 position[] = {140.0f, 0.0f, -200.0f};
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, id, position, kFacing, err, sizeof(err)));
	REQUIRE(forgeLevelDocGetSpawn(level.doc, index, &start));
	CHECK(std::string(start.pad_ref) != waypoint);
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocFindPad(level.doc, waypoint.c_str(), &pad));
	CHECK(pad.walk_direct);
	CHECK(pad.position[0] == kPlace[0]);
	REQUIRE(forgeLevelDocFindPad(level.doc, start.pad_ref, &pad));
	CHECK_FALSE(pad.walk_direct);
	CHECK(pad.position[0] == position[0]);
}

TEST_CASE("Grid hills compile through the native intro reader and preserve other commands",
          "[forge-level-doc][intro]")
{
	const std::string intro = R"({
  "schema": "pd2.scenario.intro.v1",
  "note": "Keep authored rows",
  "rows": [
    {"cmd":"watch_time","hours":14,"minutes":37}
  ]
})";
	Level level(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", intro);
	REQUIRE(level.doc != nullptr);
	char err[256] = {};
	CHECK(forgeLevelDocHillCount(level.doc) == 0);
	CHECK(forgeLevelDocSetHill(level.doc, "pad_4321", 1, err, sizeof(err)) == 0);
	REQUIRE(forgeLevelDocSetHill(level.doc, "pad_0000", 1, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocSetHill(level.doc, "pad_0002", 1, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocSetHill(level.doc, "pad_0002", 1, err, sizeof(err)) == 0);
	REQUIRE(forgeLevelDocHillCount(level.doc) == 2);
	char pad[32];
	REQUIRE(forgeLevelDocGetHillPad(level.doc, 1, pad, sizeof(pad)) == 1);
	CHECK(std::string(pad) == "pad_0002");
	const std::string edited = level.text(FORGE_LEVEL_MEMBER_INTRO);
	scenario_intro_t compiled = {};
	REQUIRE(scenarioIntroParse(edited.data(), edited.size(), nullptr, &compiled, err, sizeof(err)) == 1);
	CHECK(compiled.counts.hills == 2);
	CHECK(compiled.counts.watch_times == 1);
	scenarioIntroFree(&compiled);
	CHECK(edited.find("\"note\": \"Keep authored rows\"") != std::string::npos);
	REQUIRE(forgeLevelDocSetHill(level.doc, "pad_0000", 0, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocSetHill(level.doc, "pad_0002", 0, err, sizeof(err)) == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_INTRO) == intro);
}

TEST_CASE("Grid case markers preserve authored intro order and compile to native commands",
          "[forge-level-doc][intro]")
{
	const std::string intro = R"({
  "schema": "pd2.scenario.intro.v1",
  "note": "Keep original formatting",
  "rows": [
    { "cmd": "watch_time", "hours": 3, "minutes": 8 },
    { "cmd": "hill", "pad_ref": "pad_0002" },
    { "cmd": "case", "team": 2, "pad_ref": "pad_0000" },
    { "cmd": "case_respawn", "team": 1, "pad_ref": "pad_0001" },
    { "pad_ref": "pad_0002", "cmd": "case", "team": 2 },
    { "cmd": "outfit", "outfit": "default" }
  ]
})";
	Level level(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", intro);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(level.text(FORGE_LEVEL_MEMBER_INTRO) == intro);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_INTRO) == 0);
	REQUIRE(forgeLevelDocCaseMarkerCount(level.doc) == 3);
	forge_level_case_marker_t marker = {};
	REQUIRE(forgeLevelDocGetCaseMarker(level.doc, 1, &marker) == 1);
	CHECK(marker.team == 1);
	CHECK(marker.respawn == 1);
	CHECK(std::string(marker.pad_ref) == "pad_0001");
	char err[256] = {};
	CHECK(forgeLevelDocSetCaseHome(level.doc, 2, "pad_0002", err, sizeof(err)) == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_INTRO) == intro);

	/* Removing a hill shifts subsequent intro rows, including the effective home. */
	REQUIRE(forgeLevelDocSetHill(level.doc, "pad_0002", 0, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocSetCaseHome(level.doc, 2, "pad_0001", err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocAddCaseRespawn(level.doc, 2, "pad_0002", err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocSetCaseHome(level.doc, 0, "pad_0000", err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocRemoveCaseMarker(level.doc, 1, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocCaseMarkerCount(level.doc) == 4);
	REQUIRE(forgeLevelDocGetCaseMarker(level.doc, 1, &marker) == 1);
	CHECK(marker.team == 2);
	CHECK(marker.respawn == 0);
	CHECK(std::string(marker.pad_ref) == "pad_0001");

	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, "level.pdscenario", &overrides, err, sizeof(err)) == 1);
	REQUIRE(overrides.count == 1);
	CHECK(std::string(overrides.entries[0].entry) == "level.pdscenario::intro.json");
	const std::string edited(static_cast<const char *>(overrides.entries[0].data), overrides.entries[0].len);
	CHECK(edited.find(R"("note": "Keep original formatting")") != std::string::npos);
	CHECK(edited.find(R"({ "cmd": "watch_time", "hours": 3, "minutes": 8 })") != std::string::npos);
	CHECK(edited.find(R"({ "cmd": "case", "team": 2, "pad_ref": "pad_0000" })") != std::string::npos);
	CHECK(edited.find(R"({ "pad_ref": "pad_0001", "cmd": "case", "team": 2 })") != std::string::npos);
	CHECK(edited.find(R"({ "cmd": "outfit", "outfit": "default" })") != std::string::npos);

	scenario_intro_t compiled = {};
	REQUIRE(scenarioIntroParse(edited.data(), edited.size(), nullptr, &compiled, err, sizeof(err)) == 1);
	CHECK(compiled.counts.cases == 3);
	CHECK(compiled.counts.case_respawns == 1);
	const std::vector<s32> expected = {
		INTROCMD_WATCHTIME, 3, 8,
		INTROCMD_CASE, 2, 0,
		INTROCMD_CASE, 2, 1,
		INTROCMD_OUTFIT, OUTFIT_DEFAULT,
		INTROCMD_CASERESPAWN, 2, 2,
		INTROCMD_CASE, 0, 0
	};
	CHECK(std::vector<s32>(compiled.words, compiled.words + compiled.word_count) == expected);
	scenarioIntroFree(&compiled);

	Level reopened(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", edited);
	REQUIRE(reopened.doc != nullptr);
	CHECK(forgeLevelDocCaseMarkerCount(reopened.doc) == 4);
	CHECK(reopened.text(FORGE_LEVEL_MEMBER_INTRO) == edited);
	CHECK(forgeLevelDocMemberChanged(reopened.doc, FORGE_LEVEL_MEMBER_INTRO) == 0);
}

TEST_CASE("Grid case edits use real pads and the native team respawn limit", "[forge-level-doc][intro]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char err[256] = {};
	CHECK(forgeLevelDocSetCaseHome(level.doc, -1, "pad_0000", err, sizeof(err)) == 0);
	CHECK(forgeLevelDocSetCaseHome(level.doc, NET_SCENARIO_CTC_BASES, "pad_0000", err, sizeof(err)) == 0);
	CHECK(forgeLevelDocAddCaseRespawn(level.doc, 0, "pad_9999", err, sizeof(err)) == 0);
	CHECK(forgeLevelDocRemoveCaseMarker(level.doc, 0, err, sizeof(err)) == 0);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_INTRO) == 0);

	/* Seven existing pads let the last add exercise capacity independently of duplicates. */
	for (s32 i = forgeLevelDocPadCount(level.doc); i <= NET_SCENARIO_CTC_SPAWNPADS; ++i) {
		const f32 position[] = { 100.0f * i, 50.0f, 125.0f };
		const f32 look[] = { 0.0f, 0.0f, 1.0f };
		char spawn[32];
		REQUIRE(forgeLevelDocAddSpawn(level.doc, position, look, nullptr, spawn, sizeof(spawn),
			err, sizeof(err)) == 1);
	}
	for (s32 i = 0; i < NET_SCENARIO_CTC_SPAWNPADS; ++i) {
		forge_level_pad_t pad;
		REQUIRE(forgeLevelDocGetPad(level.doc, i, &pad) == 1);
		REQUIRE(forgeLevelDocAddCaseRespawn(level.doc, 0, pad.pad_ref, err, sizeof(err)) == 1);
	}
	CHECK(forgeLevelDocAddCaseRespawn(level.doc, 0, "pad_0000", err, sizeof(err)) == 0);
	CHECK(std::string(err).find("already uses") != std::string::npos);
	forge_level_pad_t other;
	REQUIRE(forgeLevelDocGetPad(level.doc, NET_SCENARIO_CTC_SPAWNPADS, &other) == 1);
	CHECK(forgeLevelDocAddCaseRespawn(level.doc, 0, other.pad_ref, err, sizeof(err)) == 0);
	CHECK(std::string(err).find("respawn limit") != std::string::npos);
	CHECK(forgeLevelDocCaseMarkerCount(level.doc) == NET_SCENARIO_CTC_SPAWNPADS);
	REQUIRE(forgeLevelDocSetCaseHome(level.doc, 0, other.pad_ref, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocAddCaseRespawn(level.doc, 1, "pad_0000", err, sizeof(err)) == 1);
	const s32 markers = forgeLevelDocCaseMarkerCount(level.doc);
	std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)> snapshot(
		forgeLevelDocClone(level.doc), forgeLevelDocFree);
	REQUIRE(snapshot != nullptr);
	for (s32 i = 0; i < markers; ++i) {
		REQUIRE(forgeLevelDocRemoveCaseMarker(level.doc, 0, err, sizeof(err)) == 1);
	}
	CHECK(forgeLevelDocCaseMarkerCount(snapshot.get()) == markers);
	CHECK(forgeLevelDocMemberChanged(snapshot.get(), FORGE_LEVEL_MEMBER_INTRO) == 1);
	CHECK(forgeLevelDocCaseMarkerCount(level.doc) == 0);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_INTRO) == 0);
	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, nullptr, &overrides, err, sizeof(err)) == 1);
	for (size_t i = 0; i < overrides.count; ++i) CHECK(std::string(overrides.entries[i].entry) != "intro.json");
}

TEST_CASE("Level File startup weather preserves sibling source through edits and reopen",
          "[forge-level-doc][stagerules]")
{
	const std::string source = R"({
  "schema": "pd2.scenario.stage.v1",
  "note": "\u0041n arena",
  "weather": { "wind_speed": 5.123456789, "y_min": -400, "y_max": 500, "dry_rooms": [2] },
  "ai": { "dark_room_lists": false }
})";
	Level level(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", source);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	const auto member = FORGE_LEVEL_MEMBER_STAGE;
	char err[256] = {};
	s32 kind = -1, intensity = -1;
	REQUIRE(forgeLevelDocGetWeatherStart(level.doc, &kind, &intensity));
	CHECK(kind == SCENARIO_STAGE_WEATHER_START_NONE);
	CHECK(intensity == 0);

	std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)> before(
		forgeLevelDocClone(level.doc), forgeLevelDocFree);
	REQUIRE(before != nullptr);
	REQUIRE(forgeLevelDocSetWeatherStart(level.doc, SCENARIO_STAGE_WEATHER_START_RAIN, 2, err, sizeof(err)));
	REQUIRE(forgeLevelDocMemberChanged(level.doc, member));
	const std::string edited = level.text(member);
	const size_t appended = edited.find(", \"weather_start\":");
	REQUIRE(appended != std::string::npos);
	CHECK(edited.substr(0, appended) == source.substr(0, source.find_last_of('}') - 1));
	size_t beforeSize = 0;
	const char *beforeText = forgeLevelDocMemberText(before.get(), member, &beforeSize);
	REQUIRE(beforeText != nullptr);
	CHECK(std::string(beforeText, beforeSize) == source);

	CHECK_FALSE(forgeLevelDocSetWeatherStart(level.doc, SCENARIO_STAGE_WEATHER_START_SNOW, 2, err, sizeof(err)));
	CHECK(err[0] != '\0');
	CHECK(level.text(member) == edited);
	CHECK_FALSE(forgeLevelDocSetWeatherStart(level.doc, SCENARIO_STAGE_WEATHER_START_RAIN, 2, err, sizeof(err)));
	CHECK(level.text(member) == edited);

	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, "arena.pdscenario", &overrides, err, sizeof(err)));
	REQUIRE(overrides.count == 1);
	CHECK(std::string(overrides.entries[0].entry) == "arena.pdscenario::stage.json");
	const std::string saved(static_cast<const char *>(overrides.entries[0].data), overrides.entries[0].len);
	CHECK(saved == edited);
	scenario_stage_rules_t rules = {};
	REQUIRE(scenarioStageRulesParse(saved.data(), saved.size(), &rules, err, sizeof(err)));
	CHECK(rules.weather.startup_kind == SCENARIO_STAGE_WEATHER_START_RAIN);
	CHECK(rules.weather.startup_intensity == 2);
	REQUIRE(rules.weather.room_count == 1);
	CHECK(rules.weather.rooms[0] == 2);
	CHECK(rules.ai.no_dark_room_lists == 1);

	Level reopened(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", saved);
	INFO(reopened.error);
	REQUIRE(reopened.doc != nullptr);
	REQUIRE(forgeLevelDocGetWeatherStart(reopened.doc, &kind, &intensity));
	CHECK(kind == SCENARIO_STAGE_WEATHER_START_RAIN);
	CHECK(intensity == 2);
	CHECK_FALSE(forgeLevelDocMemberChanged(reopened.doc, member));
	REQUIRE(forgeLevelDocSetWeatherStart(reopened.doc, SCENARIO_STAGE_WEATHER_START_SNOW, 1, err, sizeof(err)));
	REQUIRE(forgeLevelDocGetWeatherStart(reopened.doc, &kind, &intensity));
	CHECK(kind == SCENARIO_STAGE_WEATHER_START_SNOW);
	CHECK(intensity == 1);
	REQUIRE(forgeLevelDocSetWeatherStart(reopened.doc, SCENARIO_STAGE_WEATHER_START_NONE, 0, err, sizeof(err)));
	CHECK(reopened.text(member) == source);

	REQUIRE(forgeLevelDocSetWeatherStart(level.doc, SCENARIO_STAGE_WEATHER_START_NONE, 0, err, sizeof(err)));
	CHECK(level.text(member) == source);
	CHECK_FALSE(forgeLevelDocMemberChanged(level.doc, member));
}

TEST_CASE("Either Grid atmosphere rule can be removed from a draft whose source had no stage.json",
          "[forge-level-doc][stagerules]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	const auto member = FORGE_LEVEL_MEMBER_STAGE;
	const std::string original = level.text(member);
	char err[256] = {};
	scenario_stage_atmosphere_t ambient = {};
	ambient.has_ambient = 1;
	ambient.ambient_light = 137;
	REQUIRE(forgeLevelDocSetAtmosphere(level.doc, &ambient, err, sizeof(err)));
	const std::string atmosphereOnly = level.text(member);
	REQUIRE(forgeLevelDocSetWeatherStart(level.doc, SCENARIO_STAGE_WEATHER_START_SNOW, 1, err, sizeof(err)));
	REQUIRE(forgeLevelDocSetWeatherStart(level.doc, SCENARIO_STAGE_WEATHER_START_NONE, 0, err, sizeof(err)));
	CHECK(level.text(member) == atmosphereOnly);
	REQUIRE(forgeLevelDocSetWeatherStart(level.doc, SCENARIO_STAGE_WEATHER_START_SNOW, 1, err, sizeof(err)));
	const scenario_stage_atmosphere_t absent = {};
	REQUIRE(forgeLevelDocSetAtmosphere(level.doc, &absent, err, sizeof(err)));
	s32 kind = -1, intensity = -1;
	REQUIRE(forgeLevelDocGetWeatherStart(level.doc, &kind, &intensity));
	CHECK(kind == SCENARIO_STAGE_WEATHER_START_SNOW);
	CHECK(intensity == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, member));
	REQUIRE(forgeLevelDocSetWeatherStart(level.doc, SCENARIO_STAGE_WEATHER_START_NONE, 0, err, sizeof(err)));
	CHECK(level.text(member) == original);
	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(level.doc, nullptr, &overrides, err, sizeof(err)));
	CHECK(overrides.count == 0);
}

TEST_CASE("Grid room brightness saves and reopens the exact native rooms member", "[forge-level-doc][grid-room-brightness]")
{
	const auto seed = fs::path(PD_SOURCE_DIR) / "dev-mods/grid_two_rooms/grid_two_rooms.pdarena";
	const char *scenarioMember = "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario";
	const std::string scenario = levelMember(levelReadFile(seed), scenarioMember);
	REQUIRE(!scenario.empty());
	const auto readDoc = [](const std::string &archive) {
		forge_level_texts_t texts = {};
		std::array<std::string, FORGE_LEVEL_MEMBER_COUNT> source;
		for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; ++i) {
			source[i] = levelMember(archive, forgeLevelDocMemberName(i));
			if (source[i].empty()) continue;
			texts.text[i] = source[i].data();
			texts.size[i] = source[i].size();
		}
		char error[256] = {};
		std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)> doc(
			forgeLevelDocRead(&texts, error, sizeof(error)), forgeLevelDocFree);
		INFO(error);
		REQUIRE(doc);
		return doc;
	};
	auto draft = readDoc(scenario);
	REQUIRE(forgeLevelDocRoomBrightnessCount(draft.get()) == 2);
	forge_level_room_brightness_t brightness = {};
	REQUIRE(forgeLevelDocGetRoomBrightness(draft.get(), 0, &brightness));
	CHECK(brightness.roomnum == 1);
	CHECK(brightness.minimum == 128);
	CHECK(brightness.maximum == 255);
	CHECK_FALSE(brightness.has_base_override);
	char err[256] = {};
	CHECK_FALSE(forgeLevelDocSetRoomBrightness(draft.get(), 1, 128, 255, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocSetRoomBrightness(draft.get(), 1, 128, 112, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocSetRoomBrightness(draft.get(), 1, 48, 256, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocSetRoomBrightness(draft.get(), 1, -1, 112, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocSetRoomBrightness(draft.get(), 0, 48, 112, err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocMemberChanged(draft.get(), FORGE_LEVEL_MEMBER_ROOMS));
	REQUIRE(forgeLevelDocSetRoomBrightness(draft.get(), 1, 48, 112, err, sizeof(err)));
	CHECK(forgeLevelDocMemberChanged(draft.get(), FORGE_LEVEL_MEMBER_ROOMS));

	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(draft.get(), scenarioMember, &overrides, err, sizeof(err)));
	REQUIRE(overrides.count == 1);
	CHECK(std::string(overrides.entries[0].entry) == std::string(scenarioMember) + "::rooms.json");
	const std::string authored = replaced(levelMember(scenario, "rooms.json"),
		"\"brightness\": [128, 255]", "\"brightness\": [48, 112]");
	CHECK(std::string(static_cast<const char *>(overrides.entries[0].data), overrides.entries[0].len) == authored);
	const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	struct Scratch {
		fs::path root;
		~Scratch() { std::error_code error; fs::remove_all(root, error); }
	} scratch{fs::temp_directory_path() / ("pd2-forge-room-brightness-" + std::to_string(stamp))};
	REQUIRE(fs::create_directory(scratch.root));
	const std::string output = (scratch.root / "room_brightness.pdarena").string();
	const auto saved = assetArchiveRebuildFile(seed.string().c_str(), output.c_str(), "mod:arena_forge_brightness",
		"pd-tests", overrides.entries, overrides.count, nullptr, err, sizeof(err));
	INFO(err);
	REQUIRE(saved == ASSET_ARCHIVE_CLONE_OK);
	const std::string savedScenario = levelMember(levelReadFile(output), scenarioMember);
	CHECK(levelMember(savedScenario, "rooms.json") == authored);
	auto reopened = readDoc(savedScenario);
	REQUIRE(forgeLevelDocGetRoomBrightness(reopened.get(), 0, &brightness));
	CHECK(brightness.roomnum == 1);
	CHECK(brightness.minimum == 48);
	CHECK(brightness.maximum == 112);
	REQUIRE(forgeLevelDocGetRoomBrightness(reopened.get(), 1, &brightness));
	CHECK(brightness.roomnum == 2);
	CHECK(brightness.minimum == 128);
	CHECK(brightness.maximum == 255);
}

TEST_CASE("Grid room brightness preserves unsupported room sources and native overrides",
	"[forge-level-doc][grid-room-brightness]")
{
	const std::string source = R"({"schema":"pd2.scenario.rooms.v1","rows":[{"roomnum":1,"min":[0,0,0],"max":[100,80,90],"base_brightness":17,"note":"keep me"},{"roomnum":2,"origin":[12,34,56]}]})";
	Level level(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", "", "", source);
	INFO(level.error);
	REQUIRE(level.doc);
	REQUIRE(forgeLevelDocRoomBrightnessCount(level.doc) == 1);
	forge_level_room_brightness_t brightness = {};
	REQUIRE(forgeLevelDocGetRoomBrightness(level.doc, 0, &brightness));
	CHECK(brightness.roomnum == 1);
	CHECK(brightness.minimum == 255);
	CHECK(brightness.maximum == 255);
	CHECK(brightness.has_base_override);
	char err[256] = {};
	CHECK_FALSE(forgeLevelDocSetRoomBrightness(level.doc, 2, 0, 255, err, sizeof(err)));
	REQUIRE(forgeLevelDocSetRoomBrightness(level.doc, 1, 0, 255, err, sizeof(err)));
	CHECK(level.text(FORGE_LEVEL_MEMBER_ROOMS) == replaced(source, "{\"roomnum\":1",
		"{ \"brightness\": [0, 255],\"roomnum\":1"));
	CHECK(level.text(FORGE_LEVEL_MEMBER_STAGE).find("ambient") == std::string::npos);

	const std::string unsupported[] = {
		"",
		replaced(source, "pd2.scenario.rooms.v1", "pd2.scenario.rooms.v2"),
		replaced(source, "\"roomnum\":2", "\"roomnum\":1"),
		replaced(source, "\"roomnum\":1", "\"roomnum\":32768"),
		replaced(source, "\"roomnum\":1", "\"roomnum\":0"),
		replaced(source, "\"roomnum\":1", "\"roomnum\":1.0"),
		replaced(source, "\"note\":\"keep me\"", "\"brightness\":[0, 255.0]"),
		replaced(source, "\"note\":\"keep me\"", "\"brightness\":[0, 1, 255]"),
		replaced(source, "\"note\":\"keep me\"", "\"metadata\":{\"brightness\":[64, 128]}"),
		replaced(source, "\"note\":\"keep me\"", R"("brightnes\u0073":[64, 128])"),
	};
	for (const auto &text : unsupported) {
		CAPTURE(text);
		Level old(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", "", "", text);
		REQUIRE(old.doc);
		CHECK(forgeLevelDocRoomBrightnessCount(old.doc) == 0);
		CHECK_FALSE(forgeLevelDocSetRoomBrightness(old.doc, 1, 48, 112, err, sizeof(err)));
		CHECK_FALSE(forgeLevelDocMemberChanged(old.doc, FORGE_LEVEL_MEMBER_ROOMS));
		CHECK(old.text(FORGE_LEVEL_MEMBER_ROOMS) == text);
	}
}

TEST_CASE("Private Grid recovery restores edited source members only for the exact template",
          "[forge-level-doc][forge-checkpoint]")
{
	const std::string covers = R"({"schema":"pd2.scenario.covers.v1","rows":[],"note":"opened source"})";
	const std::string paths = R"({"schema":"pd2.scenario.paths.v1","rows":[{"path_ref":"path_0000","flags":0,"pads":["pad_0000","pad_0001"]}]})";
	const std::string rooms = R"({"schema":"pd2.scenario.rooms.v1","rows":[{"roomnum":1,"min":[0,0,0],"max":[5,10,15],"brightness":[32,96]}]})";
	Level opened(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", covers, paths, rooms);
	Level edited;
	REQUIRE(opened.doc != nullptr);
	REQUIRE(edited.doc != nullptr);
	const f32 position[] = { 250.0f, 50.0f, 125.0f };
	const f32 look[] = { 0.0f, 0.0f, 1.0f };
	char spawn[32];
	char err[256] = {};
	REQUIRE(forgeLevelDocAddSpawn(edited.doc, position, look, nullptr, spawn, sizeof(spawn),
		err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocSetCaseHome(edited.doc, 3, "pad_0001", err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocAddCaseRespawn(edited.doc, 3, "pad_0002", err, sizeof(err)) == 1);
	const std::string digest(64, 'a');
	std::unique_ptr<FILE, decltype(&fclose)> stream(tmpfile(), fclose);
	REQUIRE(stream != nullptr);
	REQUIRE(forgeLevelDocWriteCheckpoint(edited.doc, digest.c_str(), stream.get(), err, sizeof(err)) == 1);
	const long size = ftell(stream.get());
	REQUIRE(size > 0);
	rewind(stream.get());
	std::string json(static_cast<size_t>(size), '\0');
	REQUIRE(fread(json.data(), 1, json.size(), stream.get()) == json.size());

	const std::string otherDigest(64, 'b');
	CHECK(forgeLevelDocReadCheckpoint(json.data(), json.size(), otherDigest.c_str(),
		opened.doc, err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("different level revision") != std::string::npos);
	CHECK(forgeLevelDocReadCheckpoint(json.data(), json.size() - 1, digest.c_str(),
		opened.doc, err, sizeof(err)) == nullptr);

	std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)> recovered(
		forgeLevelDocReadCheckpoint(json.data(), json.size(), digest.c_str(), opened.doc, err, sizeof(err)),
		forgeLevelDocFree);
	INFO(err);
	REQUIRE(recovered != nullptr);
	CHECK(forgeLevelDocMemberChanged(recovered.get(), FORGE_LEVEL_MEMBER_INTRO) == 1);
	REQUIRE(forgeLevelDocCaseMarkerCount(recovered.get()) == 2);
	forge_level_case_marker_t marker = {};
	REQUIRE(forgeLevelDocGetCaseMarker(recovered.get(), 1, &marker) == 1);
	CHECK(marker.team == 3);
	CHECK(marker.respawn == 1);
	CHECK(std::string(marker.pad_ref) == "pad_0002");
	CHECK(forgeLevelDocSpawnCount(recovered.get()) == 3);
	CHECK(forgeLevelDocSpawnCount(opened.doc) == 2);
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
		size_t bytes = 0;
		const char *text = forgeLevelDocMemberText(recovered.get(), i, &bytes);
		REQUIRE(text != nullptr);
		const std::string expected = i == FORGE_LEVEL_MEMBER_COVERS ? covers
			: i == FORGE_LEVEL_MEMBER_PATHS ? paths : i == FORGE_LEVEL_MEMBER_ROOMS ? rooms : edited.text(i);
		CHECK(std::string(text, bytes) == expected);
		CHECK(forgeLevelDocMemberChanged(recovered.get(), i) == forgeLevelDocMemberChanged(edited.doc, i));
	}
	REQUIRE(forgeLevelDocAddCoverAtPad(recovered.get(), "pad_0001", nullptr, 0, err, sizeof(err)));
	REQUIRE(forgeLevelDocConnectPads(recovered.get(), "pad_0001", "pad_0003", nullptr, 0, err, sizeof(err)));
	forge_level_room_brightness_t brightness = {};
	REQUIRE(forgeLevelDocGetRoomBrightness(recovered.get(), 0, &brightness));
	CHECK(brightness.roomnum == 1);
	CHECK(brightness.minimum == 32);
	CHECK(brightness.maximum == 96);
}

TEST_CASE("A new spawn gets a pad of its own, the pad's bounds and the next id; other rows keep their bytes",
          "[forge-level-doc]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char id[32] = { 0 };
	char err[200] = { 0 };
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
	/* spawn_0004 is the highest in the file, so the new one is spawn_0005. */
	CHECK(std::string(id) == "spawn_0005");
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 3);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 4);

	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetPad(level.doc, 3, &pad) == 1);
	CHECK(std::string(pad.pad_ref) == "pad_0003");
	CHECK(pad.added == 1);
	CHECK(pad.has_volume == 1);
	CHECK(pad.position[2] == -250.5f);
	/* The facing is kept as a direction of length one. */
	CHECK(pad.look[2] == -1.0f);
	CHECK(pad.up[1] == 1.0f);

	const std::vector<std::string> before = linesOf(kPads);
	const std::vector<std::string> after = linesOf(level.text(FORGE_LEVEL_MEMBER_PADS));
	REQUIRE(after.size() == before.size() + 1);
	/* The first two rows as they were; the old last row gains only its comma. */
	CHECK(after[3] == before[3]);
	CHECK(after[4] == before[4]);
	CHECK(after[5] == before[5] + ",");
	CHECK(after[6] ==
		"    { \"pad_ref\": \"pad_0003\", \"room_ref\": \"\", \"liftnum\": 0, \"flags\": \"0x00000\", "
		"\"position\": [100.000000, 0.000000, -250.500000], \"up\": [0.000000, 1.000000, 0.000000], "
		"\"look\": [0.000000, 0.000000, -1.000000], \"bbox\": { \"min\": [-100.000000, -100.000000, "
		"-100.000000], \"max\": [100.000000, 100.000000, 100.000000] } }");
	CHECK(after[7] == "  ]");

	const std::vector<std::string> spawnsBefore = linesOf(kSpawns);
	const std::vector<std::string> spawnsAfter = linesOf(level.text(FORGE_LEVEL_MEMBER_SPAWNS));
	REQUIRE(spawnsAfter.size() == spawnsBefore.size() + 1);
	CHECK(spawnsAfter[3] == spawnsBefore[3]);
	CHECK(spawnsAfter[4] == spawnsBefore[4] + ",");
	CHECK(spawnsAfter[5] ==
		"    { \"spawn_id\": \"spawn_0005\", \"pad_ref\": \"pad_0003\", \"room_ref\": \"\", "
		"\"team\": \"any\", \"profile\": \"intro\", \"position\": null, \"look\": null }");

	/* The new pad's bounds: the pad's place, a hundred units each way. */
	const std::vector<std::string> volumesBefore = linesOf(kVolumes);
	const std::vector<std::string> volumesAfter = linesOf(level.text(FORGE_LEVEL_MEMBER_VOLUMES));
	REQUIRE(volumesAfter.size() == volumesBefore.size() + 1);
	CHECK(volumesAfter[3] == volumesBefore[3]);
	CHECK(volumesAfter[4] == volumesBefore[4]);
	CHECK(volumesAfter[5] == volumesBefore[5] + ",");
	CHECK(volumesAfter[6] ==
		"    { \"volume_id\": \"volume_pad_0003\", \"pad_ref\": \"pad_0003\", \"kind\": \"pad_bounds\", "
		"\"room_ref\": \"\", \"shape\": \"aabb\", \"min\": [0.000000, -100.000000, -350.500000], "
		"\"max\": [200.000000, 100.000000, -150.500000] }");

	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PADS) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SPAWNS) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_VOLUMES) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_OBJECTS) == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == kObjects);

	/* The level's graph keeps step: the game counts its trigger volume nodes
	 * and refuses a level where that is not the number of bounds rows. The
	 * new row gets its node, the link every such node has, and true counts,
	 * which makes this the graph of a level with four pads, byte for byte. */
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == levelGraph(4, 3, 4));
	CHECK(occurrences(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH), kVolumeNode) == 4);

	/* What was written is a level the module reads again, the same level. */
	Level again(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES), kNoFields,
		level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(forgeLevelDocPadCount(again.doc) == 4);
	CHECK(forgeLevelDocVolumeCount(again.doc) == 4);
	forge_level_spawn_t spawn = {};
	REQUIRE(forgeLevelDocGetSpawn(again.doc, 2, &spawn) == 1);
	CHECK(std::string(spawn.spawn_id) == "spawn_0005");
	CHECK(std::string(spawn.pad_ref) == "pad_0003");
	CHECK(std::string(spawn.team) == "any");
	/* Read back, the pad is the level's own and no longer this document's. */
	REQUIRE(forgeLevelDocGetPad(again.doc, 3, &pad) == 1);
	CHECK(pad.added == 0);
	CHECK(pad.has_volume == 1);
}

TEST_CASE("Large finite spawn coordinates survive adding, moving and reopening native source",
          "[forge-level-doc]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	const f32 maximum = std::numeric_limits<f32>::max();
	const f32 position[] = {1.0e30f, -2.0e30f, maximum};
	const f32 moved[] = {-1.0e30f, -maximum, 3.0e30f};
	const f32 initialFacing[] = {0.0f, 0.0f, -1.0f};
	const f32 facing[] = {1.0f, 0.0f, 0.0f};
	char id[32] = {};
	char err[200] = {};
	const s32 index = forgeLevelDocSpawnCount(level.doc);

	auto checkSavedPose = [&](const f32 expectedPosition[3], const f32 expectedLook[3]) {
		Level reopened(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
			level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
			level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS), level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
		INFO(reopened.error);
		REQUIRE(reopened.doc != nullptr);
		forge_level_spawn_t spawn = {};
		REQUIRE(forgeLevelDocGetSpawn(reopened.doc, index, &spawn));
		CHECK(std::string(spawn.spawn_id) == id);
		forge_level_pad_t pad = {};
		REQUIRE(forgeLevelDocFindPad(reopened.doc, spawn.pad_ref, &pad));
		CHECK(pad.has_volume == 1);
		for (size_t axis = 0; axis < 3; ++axis) {
			CHECK(pad.position[axis] == expectedPosition[axis]);
			CHECK(pad.look[axis] == expectedLook[axis]);
		}
	};

	REQUIRE(forgeLevelDocAddSpawn(level.doc, position, kFacing, nullptr, id, sizeof(id), err, sizeof(err)));
	checkSavedPose(position, initialFacing);
	const s32 pads = forgeLevelDocPadCount(level.doc);
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, id, moved, facing, err, sizeof(err)));
	CHECK(forgeLevelDocPadCount(level.doc) == pads);
	checkSavedPose(moved, facing);
}

TEST_CASE("An id is not handed out twice, and a removed spawn leaves its pad", "[forge-level-doc]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char first[32] = { 0 };
	char second[32] = { 0 };
	char err[200] = { 0 };
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, "team_2", first, sizeof(first), err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocRemoveSpawn(level.doc, first, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 2);
	/* The pad the removed spawn stood on stays, with its bounds: pads are
	 * only ever added. */
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 4);
	/* Added and removed again, the spawns are the bytes they were. */
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == kSpawns);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SPAWNS) == 0);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PADS) == 1);

	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, "team_2", second, sizeof(second), err, sizeof(err)) == 1);
	CHECK(std::string(first) == "spawn_0005");
	CHECK(std::string(second) == "spawn_0006");
	forge_level_spawn_t spawn = {};
	REQUIRE(forgeLevelDocGetSpawn(level.doc, 2, &spawn) == 1);
	CHECK(std::string(spawn.team) == "team_2");
	CHECK(std::string(spawn.pad_ref) == "pad_0004");

	/* Removing a spawn from the template, at the start of the file. */
	REQUIRE(forgeLevelDocRemoveSpawn(level.doc, "spawn_0000", err, sizeof(err)) == 1);
	const std::vector<std::string> lines = linesOf(level.text(FORGE_LEVEL_MEMBER_SPAWNS));
	const std::vector<std::string> original = linesOf(kSpawns);
	REQUIRE(lines.size() == original.size());
	CHECK(lines[3] == original[4] + ",");
	CHECK(lines[4].find("\"spawn_id\": \"spawn_0006\"") != std::string::npos);
	CHECK(forgeLevelDocRemoveSpawn(level.doc, "spawn_0000", err, sizeof(err)) == 0);
	CHECK(std::string(err) == "the level has no spawn \"spawn_0000\"");
}

TEST_CASE("Moving a spawn off a template pad gives it a new pad and leaves the old one",
          "[forge-level-doc]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char err[200] = { 0 };
	const f32 place[3] = { -5.0f, 12.0f, 7.25f };
	const f32 facing[3] = { 3.0f, 0.0f, 0.0f };

	/* spawn_0004 and setup_0001 share pad_0001. Moving the spawn must not move
	 * the crate, so pad_0001 is left as it is and the spawn names a new pad. */
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, "spawn_0004", place, facing, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	forge_level_spawn_t spawn = {};
	REQUIRE(forgeLevelDocGetSpawn(level.doc, 1, &spawn) == 1);
	CHECK(std::string(spawn.pad_ref) == "pad_0003");
	forge_level_object_t crate = {};
	REQUIRE(forgeLevelDocGetObject(level.doc, 1, &crate) == 1);
	CHECK(std::string(crate.pad_ref) == "pad_0001");
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == kObjects);

	const std::vector<std::string> pads = linesOf(level.text(FORGE_LEVEL_MEMBER_PADS));
	const std::vector<std::string> padsBefore = linesOf(kPads);
	CHECK(pads[3] == padsBefore[3]);
	CHECK(pads[4] == padsBefore[4]);
	CHECK(pads[6].find("\"position\": [-5.000000, 12.000000, 7.250000]") != std::string::npos);
	CHECK(pads[6].find("\"look\": [1.000000, 0.000000, 0.000000]") != std::string::npos);
	const std::vector<std::string> volumes = linesOf(level.text(FORGE_LEVEL_MEMBER_VOLUMES));
	const std::vector<std::string> volumesBefore = linesOf(kVolumes);
	/* The template pad's bounds are where they were. */
	CHECK(volumes[4] == volumesBefore[4]);
	CHECK(volumes[6].find("\"pad_ref\": \"pad_0003\"") != std::string::npos);
	CHECK(volumes[6].find("\"min\": [-105.000000, -88.000000, -92.750000]") != std::string::npos);
	CHECK(volumes[6].find("\"max\": [95.000000, 112.000000, 107.250000]") != std::string::npos);

	/* Only the spawn's pad name changed in its row; its unknown key is kept. */
	const std::vector<std::string> spawns = linesOf(level.text(FORGE_LEVEL_MEMBER_SPAWNS));
	const std::vector<std::string> spawnsBefore = linesOf(kSpawns);
	CHECK(spawns[3] == spawnsBefore[3]);
	CHECK(spawns[4] == replaced(spawnsBefore[4], "\"pad_0001\"", "\"pad_0003\""));
	CHECK(spawns[4].find("\"weight\": 2") != std::string::npos);

	/* A second move finds a pad this document added and moves that one, and
	 * the pad's bounds go with it. */
	const f32 further[3] = { -6.0f, 12.0f, 9.0f };
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, "spawn_0004", further, facing, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 4);
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocFindPad(level.doc, "pad_0003", &pad) == 1);
	CHECK(pad.position[0] == -6.0f);
	CHECK(pad.position[2] == 9.0f);
	const std::vector<std::string> padsMoved = linesOf(level.text(FORGE_LEVEL_MEMBER_PADS));
	CHECK(padsMoved[6].find("\"position\": [-6.000000, 12.000000, 9.000000]") != std::string::npos);
	CHECK(padsMoved[6].find("\"pad_ref\": \"pad_0003\"") != std::string::npos);
	const std::vector<std::string> volumesMoved = linesOf(level.text(FORGE_LEVEL_MEMBER_VOLUMES));
	CHECK(volumesMoved[6].find("\"volume_id\": \"volume_pad_0003\"") != std::string::npos);
	CHECK(volumesMoved[6].find("\"min\": [-106.000000, -88.000000, -91.000000]") != std::string::npos);
	CHECK(volumesMoved[6].find("\"max\": [94.000000, 112.000000, 109.000000]") != std::string::npos);
	CHECK(volumesMoved[4] == volumesBefore[4]);
}

TEST_CASE("Moving a newly added start preserves the hill and case markers on its pad",
          "[forge-level-doc][level][markers]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char err[256] = {};
	char id[32] = {};
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)));
	const s32 index = forgeLevelDocSpawnCount(level.doc) - 1;
	forge_level_spawn_t original = {};
	REQUIRE(forgeLevelDocGetSpawn(level.doc, index, &original));
	REQUIRE(forgeLevelDocSetHill(level.doc, original.pad_ref, 1, err, sizeof(err)));
	REQUIRE(forgeLevelDocSetCaseHome(level.doc, 0, original.pad_ref, err, sizeof(err)));
	REQUIRE(forgeLevelDocAddCaseRespawn(level.doc, 1, original.pad_ref, err, sizeof(err)));
	const std::string intro = level.text(FORGE_LEVEL_MEMBER_INTRO);
	const s32 pads = forgeLevelDocPadCount(level.doc);

	const f32 moved[] = {450.0f, 0.0f, -320.0f};
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, id, moved, kFacing, err, sizeof(err)));
	CHECK(forgeLevelDocPadCount(level.doc) == pads + 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_INTRO) == intro);
	forge_level_spawn_t spawn = {};
	REQUIRE(forgeLevelDocGetSpawn(level.doc, index, &spawn));
	CHECK(std::string(spawn.spawn_id) == id);
	REQUIRE(std::string(spawn.pad_ref) != original.pad_ref);
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocFindPad(level.doc, original.pad_ref, &pad));
	CHECK(pad.position[0] == kPlace[0]);
	CHECK(pad.position[2] == kPlace[2]);
	REQUIRE(forgeLevelDocFindPad(level.doc, spawn.pad_ref, &pad));
	CHECK(pad.position[0] == moved[0]);
	CHECK(pad.position[2] == moved[2]);

	Level reopened(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
		level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS), level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH), intro);
	INFO(reopened.error);
	REQUIRE(reopened.doc != nullptr);
	REQUIRE(forgeLevelDocGetSpawn(reopened.doc, index, &spawn));
	CHECK(std::string(spawn.pad_ref) == pad.pad_ref);
	CHECK(forgeLevelDocHasHill(reopened.doc, original.pad_ref));
	REQUIRE(forgeLevelDocCaseMarkerCount(reopened.doc) == 2);
	for (s32 i = 0; i < 2; ++i) {
		forge_level_case_marker_t marker = {};
		REQUIRE(forgeLevelDocGetCaseMarker(reopened.doc, i, &marker));
		CHECK(std::string(marker.pad_ref) == original.pad_ref);
	}
}

TEST_CASE("An object's place is written in two files, and both follow a move", "[forge-level-doc]")
{
	Level level;
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 7);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == levelFields());
	forge_level_object_t object = {};
	REQUIRE(forgeLevelDocGetObject(level.doc, 1, &object) == 1);
	CHECK(object.order == 1);
	CHECK(object.movable == 1);
	REQUIRE(forgeLevelDocGetObject(level.doc, 2, &object) == 1);
	CHECK(object.order == 2);
	CHECK(object.movable == 0);

	/* The crate shares pad_0001 with a spawn. It gets a pad of its own, and
	 * objects.json and setup.fields.json both name it. */
	char err[200] = { 0 };
	const f32 place[3] = { 7.0f, 0.0f, 9.0f };
	const f32 facing[3] = { 0.0f, 0.0f, 2.0f };
	REQUIRE(forgeLevelDocMoveObject(level.doc, "setup_0001", place, facing, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocGetObject(level.doc, 1, &object) == 1);
	CHECK(std::string(object.pad_ref) == "pad_0003");
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) ==
		replaced(kObjects, "\"pad_ref\": \"pad_0001\"", "\"pad_ref\": \"pad_0003\""));
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == levelFields("pad_0003"));
	/* The spawn that shared the old pad is where it was. */
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == kSpawns);

	/* Moved again, the crate's own pad moves and no row that names it changes. */
	const f32 further[3] = { 8.0f, 0.0f, 9.0f };
	REQUIRE(forgeLevelDocMoveObject(level.doc, "setup_0001", further, facing, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == levelFields("pad_0003"));
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocFindPad(level.doc, "pad_0003", &pad) == 1);
	CHECK(pad.position[0] == 8.0f);

	/* What cannot be moved, and why. */
	CHECK(forgeLevelDocMoveObject(level.doc, "setup_0002", place, facing, err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0002 stands on no pad, so it has no place to move");
	CHECK(forgeLevelDocMoveObject(level.doc, "setup_0009", place, facing, err, sizeof(err)) == 0);
	CHECK(std::string(err) == "the level has no object \"setup_0009\"");
	Level bare(kPads, kSpawns, kObjects, kVolumes, kNoFields);
	REQUIRE(bare.doc != nullptr);
	CHECK(forgeLevelDocMoveObject(bare.doc, "setup_0001", place, facing, err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0001 is a prop with no field that says where it stands");
	CHECK(forgeLevelDocMemberChanged(bare.doc, FORGE_LEVEL_MEMBER_PADS) == 0);
}

TEST_CASE("A copy of an object is the next command, with every field of the original", "[forge-level-doc]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char id[32] = { 0 };
	char err[200] = { 0 };
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, id, sizeof(id), err, sizeof(err)) == 1);
	/* Three records hold places 0 to 2, so the copy is setup_0003 at place 3. */
	CHECK(std::string(id) == "setup_0003");
	CHECK(forgeLevelDocObjectCount(level.doc) == 4);
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 4);
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 10);
	forge_level_object_t copy = {};
	REQUIRE(forgeLevelDocGetObject(level.doc, 3, &copy) == 1);
	CHECK(std::string(copy.record_id) == "setup_0003");
	CHECK(std::string(copy.kind) == "weapon_pickup");
	CHECK(std::string(copy.pad_ref) == "pad_0003");
	CHECK(std::string(copy.weapon_catalog_id) == "base:falcon2");
	CHECK(copy.order == 3);
	CHECK(copy.movable == 1);

	/* Its field rows are the original's, with its own id, place and pad. */
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == levelFields("pad_0001",
		",\n" + fieldRow("setup_0003", "weapon_pickup", "command.order", "s32", "3") +
		",\n" + fieldRow("setup_0003", "weapon_pickup", "base.pad", "pad_ref", "pad_0003") +
		",\n" + fieldRow("setup_0003", "weapon_pickup", "pickup.weapon", "weapon_selector", "mp_location_00")));
	/* The original's rows in objects.json are untouched, and the copy follows them. */
	const std::string objects = level.text(FORGE_LEVEL_MEMBER_OBJECTS);
	const std::string original(kObjects);
	const size_t listEnd = original.rfind("\n  ]");
	REQUIRE(listEnd != std::string::npos);
	CHECK(objects.compare(0, listEnd, original, 0, listEnd) == 0);
	CHECK(objects.find("\"record_id\": \"setup_0003\",\n      \"kind\": \"weapon_pickup\",\n"
		"      \"pad_ref\": \"pad_0003\",") != std::string::npos);

	/* A second copy takes the next place; a copy can be copied. */
	char second[32] = { 0 };
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, id, kPlace, kFacing, second, sizeof(second), err, sizeof(err)) == 1);
	CHECK(std::string(second) == "setup_0004");
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 13);

	/* Written out and read again, it is the same level. */
	Level again(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
		level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS));
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(forgeLevelDocObjectCount(again.doc) == 5);
	REQUIRE(forgeLevelDocGetObject(again.doc, 4, &copy) == 1);
	CHECK(std::string(copy.record_id) == "setup_0004");
	CHECK(copy.order == 4);
	CHECK(copy.movable == 1);

	/* What cannot be copied changes nothing. */
	CHECK(forgeLevelDocDuplicateObject(level.doc, "setup_0002", kPlace, kFacing, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0002 stands on no pad, so it has no place to move");
	CHECK(std::string(id).empty());
	const f32 nowhere[3] = { 0.0f, 0.0f, 0.0f };
	CHECK(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, nowhere, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(forgeLevelDocObjectCount(level.doc) == 5);
	CHECK(forgeLevelDocFieldRowCount(level.doc) == 13);
}

TEST_CASE("The two object files must agree, and a record's id is its place", "[forge-level-doc]")
{
	const auto refused = [](const std::string &fields) {
		Level level(kPads, kSpawns, kObjects, kVolumes, fields);
		CHECK(level.doc == nullptr);
		return level.error;
	};
	CHECK(refused(levelFields("pad_0002")) ==
		"objects.json says setup_0001 stands on pad_0001 and setup.fields.json says pad_0002");
	CHECK(refused(replaced(levelFields(), "\"value\": \"1\"", "\"value\": \"5\"")) ==
		"setup.fields.json puts setup_0001 at place 5 of the command list; its id says 1");
	CHECK(refused(replaced(levelFields(), "\"value\": \"2\"", "\"value\": \"two\"")) ==
		"setup.fields.json setup_0002 has a command.order that is not a number");
	CHECK(refused(replaced(levelFields(), "      \"field\": \"pickup.weapon\",\n", "")) ==
		"setup.fields.json row 2 needs record_id, kind, field and type");
	CHECK(refused(replaced(levelFields(), "setup.fields.v1", "setup.fields.v2")) ==
		"setup.fields.json needs \"schema\": \"pd2.scenario.setup.fields.v1\"");

	/* A record the object list does not have (a briefing, say) is carried
	 * along, and its place counts when the next one is handed out. */
	Level level(kPads, kSpawns, kObjects, kVolumes, levelFields("pad_0001",
		",\n" + fieldRow("briefing_0000", "briefing", "command.order", "s32", "3")));
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char id[32] = { 0 };
	char err[200] = { 0 };
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0001", kPlace, kFacing, id, sizeof(id), err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0004");
}

TEST_CASE("A start whose pad field cannot be rewritten leaves the draft intact",
          "[forge-level-doc]")
{
	const std::string spawns = replaced(kSpawns, "\"pad_ref\"", R"("\u0070ad_ref")");
	Level level(kPads, spawns, kObjects, kVolumes, levelFields());
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = {};
	CHECK_FALSE(forgeLevelDocMoveSpawn(level.doc, "spawn_0000", kPlace, kFacing, err, sizeof(err)));
	CHECK(std::string(err).find("could not be rewritten") != std::string::npos);
	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; ++member) {
		CHECK_FALSE(forgeLevelDocMemberChanged(level.doc, member));
	}
}

TEST_CASE("An edit that cannot be made is refused and changes nothing", "[forge-level-doc]")
{
	Level level;
	REQUIRE(level.doc != nullptr);
	char id[32] = { 0 };
	char err[200] = { 0 };
	const f32 nowhere[3] = { 0.0f, 0.0f, 0.0f };
	const f32 broken[3] = { 1.0f, HUGE_VALF, 0.0f };

	CHECK(forgeLevelDocAddSpawn(level.doc, kPlace, nowhere, nullptr, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(std::string(err) == "a facing needs a direction; (0, 0, 0) is none");
	CHECK(forgeLevelDocAddSpawn(level.doc, broken, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(std::string(err) == "a position and a facing need three finite numbers each");
	CHECK(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, "red", id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(std::string(err) == "a spawn's team is any, default or team_N; \"red\" is none of them");
	CHECK(std::string(id).empty());
	CHECK(forgeLevelDocMoveSpawn(level.doc, "spawn_0000", kPlace, nowhere, err, sizeof(err)) == 0);
	CHECK(forgeLevelDocMoveSpawn(level.doc, "spawn_0001", kPlace, kFacing, err, sizeof(err)) == 0);
	CHECK(std::string(err) == "the level has no spawn \"spawn_0001\"");
	CHECK(forgeLevelDocAddSpawn(nullptr, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 0);

	CHECK(forgeLevelDocPadCount(level.doc) == 3);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 2);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 3);
	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		CHECK(forgeLevelDocMemberChanged(level.doc, member) == 0);
	}
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == kPads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == kSpawns);
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == kVolumes);

	/* Spawn numbers stop at four digits, the form the archive rules want. */
	Level full(kPads, replaced(kSpawns, "spawn_0004", "spawn_9999"), kObjects, kVolumes);
	REQUIRE(full.doc != nullptr);
	CHECK(forgeLevelDocAddSpawn(full.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(std::string(err) == "the level has used every spawn number up to spawn_9999");
	CHECK(forgeLevelDocPadCount(full.doc) == 3);

	/* A bounds row already named like the new pad's would be: refused. */
	Level taken(kPads, kSpawns, kObjects, replaced(kVolumes, "volume_pad_0002", "volume_pad_0003"));
	REQUIRE(taken.doc != nullptr);
	CHECK(forgeLevelDocAddSpawn(taken.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 0);
	CHECK(std::string(err) ==
		"volumes.json already has a volume_pad_0003, the name the new pad's bounds would take");
	CHECK(forgeLevelDocPadCount(taken.doc) == 3);
	CHECK(forgeLevelDocMemberChanged(taken.doc, FORGE_LEVEL_MEMBER_PADS) == 0);
}

TEST_CASE("A level that breaks a rule is refused with the reason, never repaired",
          "[forge-level-doc]")
{
	const auto refused = [](const std::string &pads, const std::string &spawns, const std::string &objects,
			const std::string &volumes) {
		Level level(pads, spawns, objects, volumes);
		CHECK(level.doc == nullptr);
		return level.error;
	};
	CHECK(refused("[]", kSpawns, kObjects, kVolumes) == "pads.json is not one JSON object");
	CHECK(refused(kPads, "", kObjects, kVolumes) == "spawns.json is not one JSON object");
	CHECK(refused(kPads, kSpawns, "{ \"schema\": \"pd2.scenario.objects.v1\" }", kVolumes) ==
		"objects.json has no rows array");
	CHECK(refused(replaced(kPads, "pads.v1", "pads.v2"), kSpawns, kObjects, kVolumes) ==
		"pads.json needs \"schema\": \"pd2.scenario.pads.v1\"");
	/* The files are not interchangeable. */
	CHECK(refused(kSpawns, kSpawns, kObjects, kVolumes) ==
		"pads.json needs \"schema\": \"pd2.scenario.pads.v1\"");
	CHECK(refused(kPads, kSpawns, kObjects, kPads) ==
		"volumes.json needs \"schema\": \"pd2.scenario.volumes.v1\"");
	/* A pad's name is its place in the file. */
	CHECK(refused(replaced(kPads, "pad_0001", "pad_0007"), kSpawns, kObjects, kVolumes) ==
		"pads.json row 1 is named \"pad_0007\"; a pad's name is its place in the file (pad_0001)");
	CHECK(refused(replaced(kPads, "\"up\": [0.000000, 1.000000, 0.000000]", "\"up\": [0.000000, 1.000000]"),
		kSpawns, kObjects, kVolumes) == "pads.json pad_0000 needs position, up and look, three numbers each");
	CHECK(refused(kPads, replaced(kSpawns, "spawn_0004", "spawn_0000"), kObjects, kVolumes) ==
		"spawns.json lists spawn_0000 twice");
	CHECK(refused(kPads, replaced(kSpawns, "\"spawn_id\": \"spawn_0000\", ", ""), kObjects, kVolumes) ==
		"spawns.json row 0 has no spawn_id");
	CHECK(refused(kPads, replaced(kSpawns, "\"pad_ref\": \"pad_0001\"", "\"pad_ref\": \"pad_0009\""), kObjects,
		kVolumes) == "spawns.json spawn_0004 names pad \"pad_0009\", which the level does not have");
	CHECK(refused(kPads, kSpawns, replaced(kObjects, "setup_0001", "setup_0000"), kVolumes) ==
		"objects.json lists setup_0000 twice");
	CHECK(refused(kPads, kSpawns, replaced(kObjects, "\"pad_ref\": \"pad_0002\"", "\"pad_ref\": \"pad_0200\""),
		kVolumes) == "objects.json setup_0000 names pad \"pad_0200\", which the level does not have");
	CHECK(refused(kPads, kSpawns, kObjects, replaced(kVolumes, "volume_pad_0001", "volume_pad_0000")) ==
		"volumes.json lists volume_pad_0000 twice");
	CHECK(refused(kPads, kSpawns, kObjects, replaced(kVolumes, "\"pad_ref\": \"pad_0002\"", "\"pad_ref\": \"pad_0005\"")) ==
		"volumes.json volume_pad_0002 names pad \"pad_0005\", which the level does not have");
	CHECK(refused(kPads, kSpawns, kObjects, replaced(kVolumes, "\"volume_id\": \"volume_pad_0000\", ", "")) ==
		"volumes.json row 0 has no volume_id");
	/* Text after the object is not JSON, and is not dropped quietly. */
	CHECK(refused(std::string(kPads) + "x", kSpawns, kObjects, kVolumes) == "pads.json is not one JSON object");
}

TEST_CASE("Rows are found in any layout, and an empty list takes its first row", "[forge-level-doc]")
{
	/* One line, no spaces: the layout a hand or another tool might write. */
	const std::string pads =
		"{\"rows\":[{\"pad_ref\":\"pad_0000\",\"room_ref\":\"\",\"liftnum\":0,\"flags\":\"0x00000\","
		"\"position\":[1,2,3],\"up\":[0,1,0],\"look\":[0,0,1],\"bbox\":{\"min\":[-1,-1,-1],\"max\":[1,1,1]},"
		"\"text\":\"a } and a ] and a \\\" inside a string\"}],\"schema\":\"pd2.scenario.pads.v1\"}";
	const std::string spawns = "{ \"schema\": \"pd2.scenario.spawns.v1\", \"rows\": [] }";
	const std::string objects = "{\n  \"schema\": \"pd2.scenario.objects.v1\",\n  \"rows\": [\n  ]\n}\n";
	/* A level whose one pad has no bounds row. */
	const std::string volumes = "{ \"schema\": \"pd2.scenario.volumes.v1\", \"rows\": [ ] }\n";
	Level level(pads, spawns, objects, volumes);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(forgeLevelDocPadCount(level.doc) == 1);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 0);
	CHECK(forgeLevelDocObjectCount(level.doc) == 0);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 0);
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetPad(level.doc, 0, &pad) == 1);
	CHECK(pad.has_volume == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == volumes);

	char id[32] = { 0 };
	char err[200] = { 0 };
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, "default", id, sizeof(id), err, sizeof(err)) == 1);
	/* No spawn was ever numbered here, so the first is spawn_0000. */
	CHECK(std::string(id) == "spawn_0000");
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) ==
		"{ \"schema\": \"pd2.scenario.spawns.v1\", \"rows\": [\n"
		"    { \"spawn_id\": \"spawn_0000\", \"pad_ref\": \"pad_0001\", \"room_ref\": \"\", "
		"\"team\": \"default\", \"profile\": \"intro\", \"position\": null, \"look\": null }\n"
		"  ] }");
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) ==
		"{ \"schema\": \"pd2.scenario.volumes.v1\", \"rows\": [\n"
		"    { \"volume_id\": \"volume_pad_0001\", \"pad_ref\": \"pad_0001\", \"kind\": \"pad_bounds\", "
		"\"room_ref\": \"\", \"shape\": \"aabb\", \"min\": [0.000000, -100.000000, -350.500000], "
		"\"max\": [200.000000, 100.000000, -150.500000] }\n"
		"  ] }\n");
	/* The one-line pads file keeps its first row's bytes and gains a second. */
	const std::string padsAfter = level.text(FORGE_LEVEL_MEMBER_PADS);
	const std::string firstRow = pads.substr(pads.find("{\"pad_ref\""), pads.find("}]") + 1 - pads.find("{\"pad_ref\""));
	CHECK(padsAfter.find(firstRow + ", { \"pad_ref\": \"pad_0001\"") != std::string::npos);

	/* The graph had no trigger volume node; the first one and its link go
	 * at the end of their lists, and the result is still the level's graph. */
	const std::string graphAfter = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
	CHECK(occurrences(graphAfter, kVolumeNode) == 1);
	CHECK(graphAfter.find("{ \"id\": \"scenario.ai.lists\", \"kind\": \"scenario.ai.lists.source\", "
		"\"source\": \"ai/ailists.json\" },\n    { \"id\": \"trigger.volume.0000\", " + std::string(kVolumeNode) +
		", \"table\": \"volumes.json\", \"volume\": \"volume_pad_0001\", \"pad\": \"pad_0001\" }\n  ],") !=
		std::string::npos);
	CHECK(graphAfter.find("{ \"from\": \"scenario.load\", \"to\": \"scenario.ai.lists\" },\n"
		"    { \"from\": \"scenario.load\", \"to\": \"trigger.volume.0000\" }\n  ],") != std::string::npos);
	CHECK(graphAfter.find("\"pads\": 2, \"volumes\": 1, \"objects\": 0,") != std::string::npos);

	Level again(padsAfter, level.text(FORGE_LEVEL_MEMBER_SPAWNS), level.text(FORGE_LEVEL_MEMBER_OBJECTS),
		level.text(FORGE_LEVEL_MEMBER_VOLUMES), kNoFields, graphAfter);
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(forgeLevelDocPadCount(again.doc) == 2);
	CHECK(forgeLevelDocSpawnCount(again.doc) == 1);
	CHECK(forgeLevelDocVolumeCount(again.doc) == 1);
}

TEST_CASE("The level's graph keeps one trigger volume node for each bounds row", "[forge-level-doc]")
{
	char id[32] = { 0 };
	char err[256] = { 0 };

	/* The game counts the nodes and refuses a level where that is not the
	 * number of bounds rows. So does the document, with the reason. */
	{
		Level fewer(kPads, kSpawns, kObjects, kVolumes, levelFields(), levelGraph(3, 3, 2));
		CHECK(fewer.doc == nullptr);
		CHECK(fewer.error == "level.graph.json has 2 trigger volume nodes and volumes.json has 3 rows; "
			"the game refuses a level where the two differ");
		Level more(kPads, kSpawns, kObjects, kVolumes, levelFields(), levelGraph(3, 3, 4));
		CHECK(more.doc == nullptr);
		Level notGraph(kPads, kSpawns, kObjects, kVolumes, levelFields(), kVolumes);
		CHECK(notGraph.error == "level.graph.json needs \"schema\": \"pd2.level.graph.v1\"");
		Level noNodes(kPads, kSpawns, kObjects, kVolumes, levelFields(),
			"{ \"schema\": \"pd2.level.graph.v1\", \"links\": [] }");
		CHECK(noNodes.error == "level.graph.json has no nodes array");
		Level oddLinks(kPads, kSpawns, kObjects, kVolumes, levelFields(),
			"{ \"schema\": \"pd2.level.graph.v1\", \"nodes\": [], \"links\": {} }");
		CHECK(oddLinks.error == "level.graph.json has links that are not an array");
		Level cut(kPads, kSpawns, kObjects, kVolumes, levelFields(), "{ \"schema\": \"pd2.level.graph.v1\"");
		CHECK(cut.error == "level.graph.json is not one JSON object");
	}

	/* Each pad the document adds brings one node and one link, whatever it
	 * was added for, and the counts follow: two spawns and a copied object
	 * make this the graph of a level with six pads and four objects. */
	{
		Level level;
		REQUIRE(level.doc != nullptr);
		REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
		REQUIRE(forgeLevelDocMoveSpawn(level.doc, "spawn_0004", kPlace, kFacing, err, sizeof(err)) == 1);
		REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, id, sizeof(id),
			err, sizeof(err)) == 1);
		CHECK(forgeLevelDocPadCount(level.doc) == 6);
		CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == levelGraph(6, 4, 6));

		/* What adds no pad leaves the graph alone: a spawn removed, a pad of
		 * the document's own moved again, an object left out. */
		const std::string graph = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
		REQUIRE(forgeLevelDocRemoveSpawn(level.doc, "spawn_0000", err, sizeof(err)) == 1);
		REQUIRE(forgeLevelDocMoveSpawn(level.doc, "spawn_0004", kPlace, kFacing, err, sizeof(err)) == 1);
		CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == graph);

		/* And the whole level reads back with its own graph. */
		Level again(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
			level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
			level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS), graph);
		INFO(again.error);
		REQUIRE(again.doc != nullptr);
		CHECK(forgeLevelDocVolumeCount(again.doc) == 6);
	}

	/* An edit that fails adds nothing to the graph. */
	{
		Level level;
		REQUIRE(level.doc != nullptr);
		const f32 nowhere[3] = { 0.0f, 0.0f, 0.0f };
		CHECK(forgeLevelDocAddSpawn(level.doc, kPlace, nowhere, nullptr, id, sizeof(id), err, sizeof(err)) == 0);
		CHECK(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, "red", id, sizeof(id), err, sizeof(err)) == 0);
		CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == 0);
		CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == levelGraph(3, 3, 3));
	}

	/* A node id already in use is not used again, the link hangs from what
	 * the graph's own links to such nodes hang from, and a count that was
	 * not true when the level was read is not made true. */
	{
		std::string graph = levelGraph(9, 3, 3);   /* says nine pads; the level has three */
		graph = replaced(graph, "\"id\": \"scenario.ai.lists\"", "\"id\": \"trigger.volume.0003\"");
		graph = replaced(graph, "\"to\": \"scenario.ai.lists\"", "\"to\": \"trigger.volume.0003\"");
		graph = replaced(graph, "{ \"from\": \"scenario.load\", \"to\": \"trigger.volume.0002\" }",
			"{ \"from\": \"scenario.volumes\", \"to\": \"trigger.volume.0002\" }");
		Level level(kPads, kSpawns, kObjects, kVolumes, levelFields(), graph);
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
		const std::string after = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
		CHECK(occurrences(after, kVolumeNode) == 4);
		/* trigger.volume.0003 is another node's id here, so the new node is 0004. */
		CHECK(after.find("\"pad\": \"pad_0002\" },\n    { \"id\": \"trigger.volume.0004\", " + std::string(kVolumeNode) +
			", \"table\": \"volumes.json\", \"volume\": \"volume_pad_0003\", \"pad\": \"pad_0003\" },\n") !=
			std::string::npos);
		/* The last link to a trigger volume node is the one to the node that
		 * only shares the name; the new link follows it and copies its source. */
		CHECK(after.find("{ \"from\": \"scenario.load\", \"to\": \"trigger.volume.0003\" },\n"
			"    { \"from\": \"scenario.load\", \"to\": \"trigger.volume.0004\" }\n  ],") != std::string::npos);
		CHECK(after.find("\"pads\": 9, \"volumes\": 4, \"objects\": 3,") != std::string::npos);
	}

	/* A graph with edges and no links, which the game also takes: the node
	 * is added, and nothing is written to a list the graph does not have. */
	{
		std::string graph = levelGraph(3, 3, 3);
		const size_t linksAt = graph.find("  \"links\": [");
		const size_t countsAt = graph.find("  \"counts\"");
		REQUIRE(linksAt != std::string::npos);
		REQUIRE(countsAt != std::string::npos);
		graph.replace(linksAt, countsAt - linksAt, "  \"edges\": [],\n");
		Level level(kPads, kSpawns, kObjects, kVolumes, levelFields(), graph);
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
		const std::string after = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
		CHECK(occurrences(after, kVolumeNode) == 4);
		CHECK(after.find("\"links\"") == std::string::npos);
		CHECK(after.find("  \"edges\": [],\n  \"counts\": { \"rooms\": 2, \"triangles\": 1, \"pads\": 4, "
			"\"volumes\": 4, \"objects\": 3,") != std::string::npos);
	}
}

namespace {

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

} // namespace

TEST_CASE("An object is removed by leaving it out on every difficulty, and put back as it was",
          "[forge-level-doc]")
{
	/* The crate starts left out on one difficulty (0x20), as base levels do
	 * to thin a level. That is not "removed". */
	const std::string objects = objectsWithFlags();
	const std::string fields = fieldsWithFlags();
	Level level(kPads, kSpawns, objects, kVolumes, fields);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };
	forge_level_object_t weapon = {};
	forge_level_object_t crate = {};
	forge_level_object_t tag = {};
	REQUIRE(forgeLevelDocGetObject(level.doc, 0, &weapon) == 1);
	REQUIRE(forgeLevelDocGetObject(level.doc, 1, &crate) == 1);
	REQUIRE(forgeLevelDocGetObject(level.doc, 2, &tag) == 1);
	CHECK(weapon.removable == 1);
	CHECK(weapon.removed == 0);
	CHECK(crate.removable == 1);
	CHECK(crate.removed == 0);
	CHECK(tag.removable == 0);
	CHECK(tag.removed == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);

	/* Removing sets all four bits in both files and changes nothing else:
	 * the record keeps its place, so the tag that points at the weapon spot
	 * and every later record stay where they are. */
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocGetObject(level.doc, 1, &crate) == 1);
	CHECK(crate.removed == 1);
	CHECK(crate.order == 1);
	CHECK(forgeLevelDocObjectCount(level.doc) == 3);
	const std::string objectsOut = replaced(objects, "\"flags2\": \"0x00000020\"", "\"flags2\": \"0x000000f0\"");
	const std::string fieldsOut = replaced(fields, "\"value\": \"0x00000020\"", "\"value\": \"0x000000f0\"");
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objectsOut);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fieldsOut);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_OBJECTS) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SETUP_FIELDS) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PADS) == 0);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SPAWNS) == 0);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_VOLUMES) == 0);

	/* Asking again for the state it is in succeeds and changes nothing. */
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, err, sizeof(err)) == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objectsOut);

	/* The saved texts read back as a level that leaves the crate out. Put
	 * back from there, it has no bit of its own to return to. */
	{
		Level saved(kPads, kSpawns, objectsOut, kVolumes, fieldsOut);
		INFO(saved.error);
		REQUIRE(saved.doc != nullptr);
		forge_level_object_t read = {};
		REQUIRE(forgeLevelDocGetObject(saved.doc, 1, &read) == 1);
		CHECK(read.removed == 1);
		CHECK(read.removable == 1);
		REQUIRE(forgeLevelDocSetObjectRemoved(saved.doc, "setup_0001", 0, err, sizeof(err)) == 1);
		REQUIRE(forgeLevelDocGetObject(saved.doc, 1, &read) == 1);
		CHECK(read.removed == 0);
		CHECK(saved.text(FORGE_LEVEL_MEMBER_OBJECTS) ==
			replaced(objects, "\"flags2\": \"0x00000020\"", "\"flags2\": \"0x00000000\""));
		/* And removed again from there, it is the text that was read. */
		REQUIRE(forgeLevelDocSetObjectRemoved(saved.doc, "setup_0001", 1, err, sizeof(err)) == 1);
		CHECK(saved.text(FORGE_LEVEL_MEMBER_OBJECTS) == objectsOut);
		CHECK(saved.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fieldsOut);
		CHECK(forgeLevelDocMemberChanged(saved.doc, FORGE_LEVEL_MEMBER_OBJECTS) == 0);
	}

	/* Put back in the document that removed it, the one bit it had returns
	 * and both files are the bytes that were read. */
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 0, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocGetObject(level.doc, 1, &crate) == 1);
	CHECK(crate.removed == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_OBJECTS) == 0);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SETUP_FIELDS) == 0);
}

TEST_CASE("A copy has flags of its own, and an object that is left out is not copied",
          "[forge-level-doc]")
{
	Level level(kPads, kSpawns, objectsWithFlags(), kVolumes, fieldsWithFlags());
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };
	char copyId[32] = { 0 };

	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0000", 1, err, sizeof(err)) == 1);
	const std::string objectsBefore = level.text(FORGE_LEVEL_MEMBER_OBJECTS);
	const std::string fieldsBefore = level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS);
	const std::string padsBefore = level.text(FORGE_LEVEL_MEMBER_PADS);
	CHECK(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, copyId, sizeof(copyId),
		err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0000 is left out of the level; put it back before copying it");
	CHECK(copyId[0] == '\0');
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objectsBefore);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fieldsBefore);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == padsBefore);
	/* It can still be moved while it is left out. */
	CHECK(forgeLevelDocMoveObject(level.doc, "setup_0000", kPlace, kFacing, err, sizeof(err)) == 1);

	/* Back in the level, it is copied, and the copy is left out on its own. */
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0000", 0, err, sizeof(err)) == 1);
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, copyId, sizeof(copyId),
		err, sizeof(err)) == 1);
	CHECK(std::string(copyId) == "setup_0003");
	forge_level_object_t copy = {};
	REQUIRE(forgeLevelDocGetObject(level.doc, 3, &copy) == 1);
	CHECK(copy.removable == 1);
	CHECK(copy.removed == 0);
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, copyId, 1, err, sizeof(err)) == 1);

	Level saved(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
		level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS));
	INFO(saved.error);
	REQUIRE(saved.doc != nullptr);
	forge_level_object_t original = {};
	REQUIRE(forgeLevelDocGetObject(saved.doc, 0, &original) == 1);
	REQUIRE(forgeLevelDocGetObject(saved.doc, 3, &copy) == 1);
	CHECK(original.removed == 0);
	CHECK(copy.removed == 1);
	CHECK(std::string(copy.record_id) == "setup_0003");
	CHECK(copy.order == 3);
}

TEST_CASE("What cannot be left out is refused with the reason", "[forge-level-doc]")
{
	char err[256] = { 0 };

	/* A record with no flags word (a tag), and an id the level does not have. */
	{
		const std::string objects = objectsWithFlags();
		const std::string fields = fieldsWithFlags();
		Level level(kPads, kSpawns, objects, kVolumes, fields);
		REQUIRE(level.doc != nullptr);
		CHECK(forgeLevelDocSetObjectRemoved(level.doc, "setup_0002", 1, err, sizeof(err)) == 0);
		CHECK(std::string(err) == "setup_0002 is a tag with no flags word that both of the level's object "
			"files give, so it cannot be left out");
		CHECK(forgeLevelDocSetObjectRemoved(level.doc, "setup_0009", 1, err, sizeof(err)) == 0);
		CHECK(std::string(err) == "the level has no object \"setup_0009\"");
		CHECK(forgeLevelDocSetObjectRemoved(nullptr, "setup_0000", 1, err, sizeof(err)) == 0);
		CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
		CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);
	}

	/* A lift: the game keeps lifts on some levels whatever their flags say. */
	{
		Level level(kPads, kSpawns, replaced(objectsWithFlags(), "\"kind\": \"prop\"", "\"kind\": \"lift\""),
			kVolumes, fieldsWithFlags());
		REQUIRE(level.doc != nullptr);
		forge_level_object_t lift = {};
		REQUIRE(forgeLevelDocGetObject(level.doc, 1, &lift) == 1);
		CHECK(lift.removable == 0);
		CHECK(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, err, sizeof(err)) == 0);
		CHECK(std::string(err) == "setup_0001 is a lift, which the game keeps on some levels whatever "
			"its flags say");
	}

	/* The two files disagree about the word. The level is still read (no
	 * base level is like this), but there is no telling which to change. */
	{
		Level level(kPads, kSpawns, objectsWithFlags("0x00000000", "0x00000020"), kVolumes,
			fieldsWithFlags("0x00000000", "0x00000040"));
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		forge_level_object_t crate = {};
		forge_level_object_t weapon = {};
		REQUIRE(forgeLevelDocGetObject(level.doc, 0, &weapon) == 1);
		REQUIRE(forgeLevelDocGetObject(level.doc, 1, &crate) == 1);
		CHECK(weapon.removable == 1);
		CHECK(crate.removable == 0);
		CHECK(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, err, sizeof(err)) == 0);
		CHECK(std::string(err).find("no flags word that both") != std::string::npos);
	}

	/* A word that is not written as hex is no word, and one written another
	 * way (short, capitals) is read and comes back as it was written. */
	{
		const std::string objects = objectsWithFlags("zero", "0X2A");
		const std::string fields = fieldsWithFlags("zero", "0x0000002a");
		Level level(kPads, kSpawns, objects, kVolumes, fields);
		INFO(level.error);
		REQUIRE(level.doc != nullptr);
		forge_level_object_t crate = {};
		forge_level_object_t weapon = {};
		REQUIRE(forgeLevelDocGetObject(level.doc, 0, &weapon) == 1);
		REQUIRE(forgeLevelDocGetObject(level.doc, 1, &crate) == 1);
		CHECK(weapon.removable == 0);
		CHECK(crate.removable == 1);
		REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 1, err, sizeof(err)) == 1);
		CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) ==
			replaced(objects, "\"flags2\": \"0X2A\"", "\"flags2\": \"0x000000fa\""));
		REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0001", 0, err, sizeof(err)) == 1);
		CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
		CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);
	}
}

TEST_CASE("The example level is read and written back unchanged, then edited", "[forge-level-doc]")
{
	const std::string arena = levelReadFile(fs::path(PD_SOURCE_DIR) /
		"examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena");
	REQUIRE(!arena.empty());
	const std::string scenario = levelMember(arena, "dependencies/assets/scenarios/tri_scenario.pdscenario");
	REQUIRE(!scenario.empty());
	const std::string pads = levelMember(scenario, "pads.json");
	const std::string spawns = levelMember(scenario, "spawns.json");
	const std::string objects = levelMember(scenario, "objects.json");
	const std::string volumes = levelMember(scenario, "volumes.json");
	const std::string fields = levelMember(scenario, "setup.fields.json");
	const std::string graph = levelMember(scenario, "level.graph.json");
	REQUIRE(!pads.empty());
	REQUIRE(!spawns.empty());
	REQUIRE(!objects.empty());
	REQUIRE(!volumes.empty());
	REQUIRE(!fields.empty());
	REQUIRE(!graph.empty());

	Level level(pads, spawns, objects, volumes, fields, graph);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(forgeLevelDocPadCount(level.doc) == 1);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 1);
	CHECK(forgeLevelDocObjectCount(level.doc) == 1);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
	CHECK(level.text(FORGE_LEVEL_MEMBER_VOLUMES) == volumes);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS) == fields);
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH) == graph);
	CHECK(occurrences(graph, kVolumeNode) == 1);
	/* The example also holds a briefing record, which the object list does not. */
	forge_level_object_t prop = {};
	REQUIRE(forgeLevelDocGetObject(level.doc, 0, &prop) == 1);
	CHECK(prop.order == 0);
	CHECK(prop.movable == 1);
	/* The example's field rows hold the prop's first flags word only, not the
	 * second, so the document does not offer to leave the prop out. */
	CHECK(prop.removable == 0);
	CHECK(prop.removed == 0);

	/* Its spawn and its prop share the one pad. Moving the spawn leaves the
	 * prop where it was. This file repeats the place in the spawn's row, so
	 * the row's copy is cleared: the pad is what the game reads. */
	char err[200] = { 0 };
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, "spawn_0000", kPlace, kFacing, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocPadCount(level.doc) == 2);
	CHECK(forgeLevelDocVolumeCount(level.doc) == 2);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
	const std::string moved = level.text(FORGE_LEVEL_MEMBER_SPAWNS);
	CHECK(moved.find("\"pad_ref\": \"pad_0001\"") != std::string::npos);
	CHECK(moved.find("\"position\": null") != std::string::npos);
	CHECK(moved.find("\"look\": null") != std::string::npos);
	CHECK(moved.find("\"profile\": \"default\"") != std::string::npos);
	/* Everything up to the end of the template's one pad row is unchanged. */
	const std::string padsMoved = level.text(FORGE_LEVEL_MEMBER_PADS);
	const size_t rowEnd = pads.rfind('}', pads.rfind(']')) + 1;
	CHECK(padsMoved.compare(0, rowEnd, pads, 0, rowEnd) == 0);

	/* The example's own graph, written the way the extractor writes one,
	 * gains the new bounds row's node after its one trigger volume node, the
	 * link after that node's link, and true counts. Taken back out by hand,
	 * what is left is the graph that was read: nothing else moved. */
	const std::string graphMoved = level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH);
	const std::string newNode = ",\n    { \"id\": \"trigger.volume.0001\", " + std::string(kVolumeNode) +
		", \"table\": \"volumes.json\", \"volume\": \"volume_pad_0001\", \"pad\": \"pad_0001\" }";
	const std::string newLink = ",\n    { \"from\": \"scenario.load\", \"to\": \"trigger.volume.0001\" }";
	CHECK(occurrences(graphMoved, kVolumeNode) == 2);
	CHECK(graphMoved.find("\"volume\": \"volume_pad_0000\", \"pad\": \"pad_0000\" }" + newNode) != std::string::npos);
	CHECK(graphMoved.find("{ \"from\": \"scenario.load\", \"to\": \"trigger.volume.0000\" }" + newLink) !=
		std::string::npos);
	CHECK(replaced(replaced(replaced(graphMoved, newNode, ""), newLink, ""),
		"\"pads\": 2, \"volumes\": 2, \"objects\": 1,", "\"pads\": 1, \"volumes\": 1, \"objects\": 1,") == graph);

	Level again(padsMoved, moved, level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES));
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(forgeLevelDocPadCount(again.doc) == 2);
	CHECK(forgeLevelDocVolumeCount(again.doc) == 2);

	/* A copy of the prop: the briefing holds place 1, so the copy is setup_0002. */
	char copyId[32] = { 0 };
	REQUIRE(forgeLevelDocDuplicateObject(level.doc, "setup_0000", kPlace, kFacing, copyId, sizeof(copyId),
		err, sizeof(err)) == 1);
	CHECK(std::string(copyId) == "setup_0002");
	CHECK(level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH).find("\"pads\": 3, \"volumes\": 3, \"objects\": 2,") !=
		std::string::npos);
	Level whole(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS), level.text(FORGE_LEVEL_MEMBER_VOLUMES),
		level.text(FORGE_LEVEL_MEMBER_SETUP_FIELDS), level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	INFO(whole.error);
	REQUIRE(whole.doc != nullptr);
	CHECK(forgeLevelDocObjectCount(whole.doc) == 2);
	CHECK(forgeLevelDocPadCount(whole.doc) == 3);
}

TEST_CASE("A new Grid pad connects to the native route and keeps its location after the start moves",
	"[forge-level-doc][grid-navigation]")
{
	const auto templatePath = fs::path(PD_SOURCE_DIR) / "dev-mods/grid_two_rooms/grid_two_rooms.pdarena";
	const char *member = "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario";
	const std::string scenario = levelMember(levelReadFile(templatePath), member);
	REQUIRE(!scenario.empty());
	const auto readDoc = [](const std::string &level, const std::string &replacementPaths = {}) {
		forge_level_texts_t texts = {};
		std::array<std::string, FORGE_LEVEL_MEMBER_COUNT> source;
		for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; ++i) {
			source[i] = i == FORGE_LEVEL_MEMBER_PATHS && !replacementPaths.empty()
				? replacementPaths : levelMember(level, forgeLevelDocMemberName(i));
			if (source[i].empty()) continue;
			texts.text[i] = source[i].data();
			texts.size[i] = source[i].size();
		}
		char err[256] = {};
		auto *doc = forgeLevelDocRead(&texts, err, sizeof(err));
		INFO(err);
		REQUIRE(doc);
		return std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)>(doc, forgeLevelDocFree);
	};
	auto draft = readDoc(scenario);
	const std::string originalPaths = levelMember(scenario, "navigation/paths.json");
	REQUIRE(!originalPaths.empty());
	const f32 branch[3] = {-580.0f, 100.0f, 200.0f};
	const f32 moved[3] = {-630.0f, 100.0f, 270.0f};
	char spawn[32] = {}, path[32] = {}, err[256] = {};
	REQUIRE(forgeLevelDocAddSpawn(draft.get(), branch, kFacing, "any", spawn, sizeof(spawn), err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocConnectPads(draft.get(), nullptr, "pad_0008", path, sizeof(path), err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocConnectPads(draft.get(), "pad_0000", "pad_0007", path, sizeof(path), err, sizeof(err)));
	CHECK(std::string(err).find("already connected") != std::string::npos);
	CHECK_FALSE(forgeLevelDocMemberChanged(draft.get(), FORGE_LEVEL_MEMBER_PATHS));
	CAPTURE(err);
	REQUIRE(forgeLevelDocConnectPads(draft.get(), "pad_0008", "pad_0000", path, sizeof(path), err, sizeof(err)));
	CHECK(std::string(path) == "path_0001");
	REQUIRE(forgeLevelDocCanDisconnectPads(draft.get(), "pad_0000", "pad_0008"));
	CHECK_FALSE(forgeLevelDocConnectPads(draft.get(), "pad_0000", "pad_0008", path, sizeof(path), err, sizeof(err)));
	const std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)> disconnected(
		forgeLevelDocClone(draft.get()), forgeLevelDocFree);
	REQUIRE(disconnected);
	REQUIRE(forgeLevelDocDisconnectPads(disconnected.get(), "pad_0000", "pad_0008", path, sizeof(path), err, sizeof(err)));
	CHECK(std::string(path) == "path_0001");
	forge_level_pad_t freedPad = {};
	REQUIRE(forgeLevelDocFindPad(disconnected.get(), "pad_0008", &freedPad));
	CHECK(freedPad.position[0] == branch[0]);
	REQUIRE(forgeLevelDocMoveSpawn(disconnected.get(), spawn, moved, kFacing, err, sizeof(err)));
	CHECK(forgeLevelDocPadCount(disconnected.get()) == 9);
	REQUIRE(forgeLevelDocFindPad(disconnected.get(), "pad_0008", &freedPad));
	CHECK(freedPad.position[0] == moved[0]);
	REQUIRE(forgeLevelDocConnectPads(disconnected.get(), "pad_0008", "pad_0000", path, sizeof(path), err, sizeof(err)));
	CHECK(std::string(path) == "path_0001");
	REQUIRE(forgeLevelDocMoveSpawn(draft.get(), spawn, moved, kFacing, err, sizeof(err)));
	forge_level_spawn_t placed = {};
	REQUIRE(forgeLevelDocGetSpawn(draft.get(), 8, &placed));
	CHECK(std::string(placed.pad_ref) == "pad_0009");
	forge_level_pad_t routePad = {};
	REQUIRE(forgeLevelDocFindPad(draft.get(), "pad_0008", &routePad));
	CHECK(routePad.position[0] == branch[0]);
	CHECK(routePad.position[2] == branch[2]);

	forge_level_overrides_t overrides = {};
	REQUIRE(forgeLevelDocOverrides(draft.get(), member, &overrides, err, sizeof(err)));
	const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("pd2-forge-route-" + std::to_string(stamp));
	fs::create_directories(root);
	const std::string input = templatePath.string();
	const std::string output = (root / "route.pdarena").string();
	REQUIRE(assetArchiveRebuildFile(input.c_str(), output.c_str(), "mod:arena_forge_route",
		"pd-tests", overrides.entries, overrides.count, nullptr, err, sizeof(err)) == ASSET_ARCHIVE_CLONE_OK);
	const std::string saved = levelMember(levelReadFile(output), member);
	REQUIRE(!saved.empty());
	const std::string paths = levelMember(saved, "navigation/paths.json");
	CHECK(paths.find(linesOf(originalPaths)[3]) != std::string::npos);
	CHECK(paths.find("\"path_ref\": \"path_0001\", \"flags\": 0, \"pads\": [\"pad_0008\", \"pad_0000\"]")
		!= std::string::npos);
	CHECK(paths.find("\"grid_link\": \"connect_pads\"") != std::string::npos);
	CHECK(levelMember(saved, "level.graph.json").find("\"paths\": 2") != std::string::npos);
	const std::string metadata = levelMember(saved, "_meta/generated-navmesh.json");
	CHECK(metadata.find("\"paths\": 2") != std::string::npos);
	CHECK(metadata.find(levelSha(paths)) != std::string::npos);
	auto reopened = readDoc(saved);
	REQUIRE(forgeLevelDocFindPad(reopened.get(), "pad_0008", &routePad));
	CHECK(routePad.position[0] == branch[0]);
	CHECK_FALSE(forgeLevelDocConnectPads(reopened.get(), "pad_0008", "pad_0000", path, sizeof(path), err, sizeof(err)));
	CHECK(std::string(err).find("already connected") != std::string::npos);
	REQUIRE(forgeLevelDocCanDisconnectPads(reopened.get(), "pad_0000", "pad_0008"));
	REQUIRE(forgeLevelDocConnectPads(reopened.get(), "pad_0009", "pad_0001", path, sizeof(path), err, sizeof(err)));
	CHECK(std::string(path) == "path_0002");
	char shortPath[1] = {};
	CHECK_FALSE(forgeLevelDocDisconnectPads(reopened.get(), "pad_0000", "pad_0008", shortPath, sizeof(shortPath), err, sizeof(err)));
	CHECK(forgeLevelDocCanDisconnectPads(reopened.get(), "pad_0000", "pad_0008"));
	REQUIRE(forgeLevelDocDisconnectPads(reopened.get(), "pad_0000", "pad_0008", path, sizeof(path), err, sizeof(err)));
	CHECK(std::string(path) == "path_0001");
	size_t remainingSize = 0;
	const char *remaining = forgeLevelDocMemberText(reopened.get(), FORGE_LEVEL_MEMBER_PATHS, &remainingSize);
	REQUIRE(remaining);
	const std::string remainingPaths(remaining, remainingSize);
	CHECK(remainingPaths.find(linesOf(originalPaths)[3]) != std::string::npos);
	CHECK(remainingPaths.find("path_0001") == std::string::npos);
	CHECK(remainingPaths.find("path_0002") != std::string::npos);
	CHECK_FALSE(forgeLevelDocCanDisconnectPads(reopened.get(), "pad_0000", "pad_0008"));
	CHECK(forgeLevelDocCanDisconnectPads(reopened.get(), "pad_0009", "pad_0001"));
	CHECK_FALSE(forgeLevelDocMemberChanged(reopened.get(), FORGE_LEVEL_MEMBER_PADS));
	const char *remainingGraph = forgeLevelDocMemberText(reopened.get(), FORGE_LEVEL_MEMBER_LEVEL_GRAPH, nullptr);
	REQUIRE(remainingGraph);
	CHECK(std::string(remainingGraph).find("\"paths\": 2") != std::string::npos);
	fs::remove_all(root);

	const std::string imported = R"({"schema":"pd2.scenario.paths.v1","rows":[
		{"path_ref":"path_0022","flags":1,"pads":["pad_0000","pad_0001|inward","pad_0002"]},
		{"path_ref":"path_0003","flags":0,"pads":["pad_0000","pad_0003"]},
		{"path_ref":"path_0004","flags":0,"pads":["pad_0004|outward","pad_0005"],"grid_link":"connect_pads"},
		{"path_ref":"path_0005","flags":0,"pads":["pad_0001","pad_0007"],"grid_link":"connect_pads"},
		{"path_ref":"path_0006","flags":0,"pads":["pad_0007","pad_0001"]}]})";
	auto importedDoc = readDoc(scenario, imported);
	CHECK_FALSE(forgeLevelDocCanDisconnectPads(importedDoc.get(), "pad_0000", "pad_0003"));
	CHECK_FALSE(forgeLevelDocDisconnectPads(importedDoc.get(), "pad_0000", "pad_0003", path, sizeof(path), err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocCanDisconnectPads(importedDoc.get(), "pad_0004", "pad_0005"));
	CHECK_FALSE(forgeLevelDocDisconnectPads(importedDoc.get(), "pad_0004", "pad_0005", path, sizeof(path), err, sizeof(err)));
	CHECK_FALSE(forgeLevelDocCanDisconnectPads(importedDoc.get(), "pad_0007", "pad_0001"));
	CHECK_FALSE(forgeLevelDocDisconnectPads(importedDoc.get(), "pad_0007", "pad_0001", path, sizeof(path), err, sizeof(err)));
	CHECK(std::string(err).find("another native path") != std::string::npos);
	CHECK_FALSE(forgeLevelDocMemberChanged(importedDoc.get(), FORGE_LEVEL_MEMBER_PATHS));
}

TEST_CASE("An outer Grid room extends when a shared-style native variant is saved",
          "[forge-level-doc][grid-navigation][grid-structure]")
{
	const auto seedPath = fs::path(PD_SOURCE_DIR) / "dev-mods/grid_two_rooms/grid_two_rooms.pdarena";
	const char *member = "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario";
	const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("pd2-forge-room-extension-" + std::to_string(stamp));
	fs::create_directories(root / "mods");
	const std::string seed = seedPath.string();
	const std::string originalPath = (root / "original.pdarena").string();
	const std::string mods = (root / "mods").string();
	char err[256] = {};

	/* Resize using the same original-room builder as the Levels entry.
	 * Extension must recognize this source with unequal chamber lengths. */
	ForgeRoomSize dimensions;
	dimensions.firstLength = 1850;
	dimensions.secondLength = 1725;
	dimensions.width = 2110;
	dimensions.height = 680;
	dimensions.doorwayWidth = 510;
	dimensions.doorwayHeight = 380;
	ForgeRoomOverrides initial;
	REQUIRE(forgeRoomBuildOverrides(seed.c_str(), dimensions, initial, err, sizeof(err)));
	const std::string waypointMember = std::string(member) + "::navigation/waypoints.json";
	const std::string groupMember = std::string(member) + "::navigation/waygroups.json";
	const std::string unknownWaypointField = R"("author_note": { "future": ["keep the source", 10000000000000001] })";
	const std::string waypointSource = R"({"schema":"pd2.scenario.waypoints.v1","rows":[],"only_linked_pads":false,)"
		+ unknownWaypointField + "}";
	std::vector<asset_archive_clone_override_t> starter(initial.entries.begin(), initial.entries.end());
	starter.push_back({waypointMember.c_str(), waypointSource.data(), static_cast<u32>(waypointSource.size()), 0});
	REQUIRE(assetArchiveRebuildFile(seed.c_str(), originalPath.c_str(), "mod:arena_extension_source",
		"pd-tests", starter.data(), static_cast<u32>(starter.size()),
		nullptr, err, sizeof(err)) == ASSET_ARCHIVE_CLONE_OK);
	const std::string original = levelReadFile(originalPath);
	REQUIRE(!original.empty());
	const std::string scenario = levelMember(original, member);
	REQUIRE(!scenario.empty());
	Level draft(levelMember(scenario, "pads.json"), levelMember(scenario, "spawns.json"),
		levelMember(scenario, "objects.json"), levelMember(scenario, "volumes.json"),
		levelMember(scenario, "setup.fields.json"), levelMember(scenario, "level.graph.json"),
		levelMember(scenario, "intro.json"), levelMember(scenario, "stage.json"),
		levelMember(scenario, "navigation/covers.json"), levelMember(scenario, "navigation/paths.json"));
	INFO(draft.error);
	REQUIRE(draft.doc != nullptr);

	forge_level_spawn_t start = {};
	REQUIRE(forgeLevelDocGetSpawn(draft.doc, 0, &start));
	const f32 start_position[] = { -610.0f, 50.0f, 70.0f };
	REQUIRE(forgeLevelDocMoveSpawn(draft.doc, start.spawn_id, start_position, kFacing, err, sizeof(err)));
	char prop[32] = {};
	const f32 prop_position[] = { 1000.0f, 50.0f, 200.0f };
	REQUIRE(forgeLevelDocAddObject(draft.doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0,
		prop_position, kFacing, prop, sizeof(prop), err, sizeof(err)));

	REQUIRE(forgeLevelDocCoverCount(draft.doc) == 0);
	forge_level_pad_t sourcePad = {}, destinationPad = {};
	REQUIRE(forgeLevelDocFindPad(draft.doc, "pad_0005", &sourcePad));
	REQUIRE(forgeLevelDocFindPad(draft.doc, "pad_0001", &destinationPad));
	REQUIRE(std::string(sourcePad.room_ref) == "room_2");
	REQUIRE(std::string(destinationPad.room_ref) == "room_1");
	CHECK(sourcePad.position[0] > 0.0f);
	CHECK(destinationPad.position[0] < 0.0f);
	const std::string padsBeforeCover = draft.text(FORGE_LEVEL_MEMBER_PADS);
	const std::string pathsBeforeCover = draft.text(FORGE_LEVEL_MEMBER_PATHS);
	char coverRef[32] = {};
	REQUIRE(forgeLevelDocAddCoverAtPad(draft.doc, sourcePad.pad_ref, coverRef, sizeof(coverRef), err, sizeof(err)));
	REQUIRE(std::string(coverRef) == "cover_0000");
	forge_level_cover_t cover = {};
	REQUIRE(forgeLevelDocGetCover(draft.doc, 0, &cover));
	CHECK(std::equal(sourcePad.position, sourcePad.position + 3, cover.position));
	REQUIRE(forgeLevelDocMoveCoverToPad(draft.doc, coverRef, destinationPad.pad_ref, err, sizeof(err)));
	REQUIRE(forgeLevelDocGetCover(draft.doc, 0, &cover));
	CHECK(std::string(cover.cover_ref) == coverRef);
	CHECK(std::equal(destinationPad.position, destinationPad.position + 3, cover.position));
	CHECK(draft.text(FORGE_LEVEL_MEMBER_PADS) == padsBeforeCover);
	CHECK(draft.text(FORGE_LEVEL_MEMBER_PATHS) == pathsBeforeCover);

	using Extension = std::unique_ptr<forge_room_extension_t, decltype(&forgeRoomFreeExtension)>;
	Extension extension(forgeRoomCreateExtension(original.data(), static_cast<u32>(original.size()),
		member, 2, 475, err, sizeof(err)), forgeRoomFreeExtension);
	INFO(err);
	REQUIRE(extension != nullptr);
	u32 count = 0;
	const auto *geometry = forgeRoomExtensionEntries(extension.get(), &count);
	REQUIRE(geometry != nullptr);
	REQUIRE(count == 4);
	const char *files[] = {"scene.glb", "collision.obj", "rooms.json", "navigation/waypoints.json"};
	for (u32 i = 0; i < count; ++i) {
		CHECK(std::string(geometry[i].entry) == std::string(member) + "::" + files[i]);
		CHECK(geometry[i].remove == 0);
	}
	forge_level_overrides_t edits = {};
	REQUIRE(forgeLevelDocOverridesAll(draft.doc, member, &edits, err, sizeof(err)));
	std::vector<asset_archive_clone_override_t> combined(edits.entries, edits.entries + edits.count);
	combined.insert(combined.end(), geometry, geometry + count);
	mod_author_request_t request = {};
	request.template_path = originalPath.c_str();
	request.display_name = "Extended Grid Variant";
	request.mods_root = mods.c_str();
	request.tool = "pd-tests";
	request.overrides = combined.data();
	request.override_count = static_cast<u32>(combined.size());
	mod_author_result_t saved = {};
	REQUIRE(modAuthorSave(&request, &saved, err, sizeof(err)) == MOD_AUTHOR_OK);
	CHECK(levelReadFile(originalPath) == original);

	const std::string savedArena = levelReadFile(saved.archive_path);
	const std::string savedScenario = levelMember(savedArena, member);
	REQUIRE(!savedScenario.empty());
	for (u32 i = 0; i < count; ++i) {
		CHECK(levelMember(savedScenario, files[i]) == std::string(
			static_cast<const char *>(geometry[i].data), geometry[i].len));
	}
	const std::string collision = levelMember(savedScenario, "collision.obj");
	CHECK(collision.find("v 2200 ") != std::string::npos);
	CHECK(collision.find("v 1725 ") == std::string::npos);
	CHECK(collision.find("v -1850 ") != std::string::npos);
	for (const char *unchanged : {"portals.json", "navigation/paths.json", "navigation/waygroups.json"}) {
		REQUIRE(!levelMember(scenario, unchanged).empty());
		CHECK(levelMember(savedScenario, unchanged) == levelMember(scenario, unchanged));
	}
	for (const s32 editable : {FORGE_LEVEL_MEMBER_PADS, FORGE_LEVEL_MEMBER_SPAWNS, FORGE_LEVEL_MEMBER_OBJECTS,
			FORGE_LEVEL_MEMBER_SETUP_FIELDS, FORGE_LEVEL_MEMBER_VOLUMES, FORGE_LEVEL_MEMBER_COVERS}) {
		CHECK(levelMember(savedScenario, forgeLevelDocMemberName(editable)) == draft.text(editable));
	}
	Level reopened(levelMember(savedScenario, "pads.json"), levelMember(savedScenario, "spawns.json"),
		levelMember(savedScenario, "objects.json"), levelMember(savedScenario, "volumes.json"),
		levelMember(savedScenario, "setup.fields.json"), levelMember(savedScenario, "level.graph.json"),
		levelMember(savedScenario, "intro.json"), levelMember(savedScenario, "stage.json"),
		levelMember(savedScenario, "navigation/covers.json"), levelMember(savedScenario, "navigation/paths.json"));
	INFO(reopened.error);
	REQUIRE(reopened.doc != nullptr);
	REQUIRE(forgeLevelDocCoverCount(reopened.doc) == 1);
	REQUIRE(forgeLevelDocGetCover(reopened.doc, 0, &cover));
	CHECK(std::string(cover.cover_ref) == coverRef);
	CHECK(std::equal(destinationPad.position, destinationPad.position + 3, cover.position));
	CHECK(reopened.text(FORGE_LEVEL_MEMBER_PADS) == padsBeforeCover);
	CHECK(reopened.text(FORGE_LEVEL_MEMBER_PATHS) == pathsBeforeCover);
	const std::string record = levelMember(savedScenario, "_meta/generated-navmesh.json");
	const std::string waypoints = levelMember(savedScenario, "navigation/waypoints.json");
	CHECK(occurrences(waypoints, "\"only_linked_pads\"") == 1);
	CHECK(waypoints.find("\"only_linked_pads\": true") != std::string::npos);
	CHECK(waypoints.find("\"rows\":[]") != std::string::npos);
	CHECK(waypoints.find(unknownWaypointField) != std::string::npos);
	CHECK(record.find(levelSha(waypoints)) != std::string::npos);
	CHECK(record.find(levelSha(collision)) != std::string::npos);
	CHECK(record.find(levelSha(levelMember(savedScenario, "scene.glb"))) != std::string::npos);

	Extension next(forgeRoomCreateExtension(savedArena.data(), static_cast<u32>(savedArena.size()),
		member, 1, 100, err, sizeof(err)), forgeRoomFreeExtension);
	INFO(err);
	REQUIRE(next != nullptr);
	const auto *nextEntries = forgeRoomExtensionEntries(next.get(), &count);
	REQUIRE(count == 4);
	CHECK(std::string(static_cast<const char *>(nextEntries[3].data), nextEntries[3].len) == waypoints);
	CHECK(forgeRoomCreateExtension(savedArena.data(), static_cast<u32>(savedArena.size()),
		member, 2, 2000, err, sizeof(err)) == nullptr);
	CHECK(forgeRoomCreateExtension(savedArena.data(), static_cast<u32>(savedArena.size()),
		member, 0, 100, err, sizeof(err)) == nullptr);

	const std::string authoredWaypoints = R"({"schema":"pd2.scenario.waypoints.v1","rows":[{"waypoint_ref":"waypoint_0000","pad_ref":"pad_0000","group_ref":"waygroup_0000","step":0,"neighbours":[]}]})";
	const std::string authoredGroups = R"({"schema":"pd2.scenario.waygroups.v1","rows":[{"waygroup_ref":"waygroup_0000","step":0,"waypoints":["waypoint_0000"],"neighbours":[]}]})";
	const asset_archive_clone_override_t authored[] = {
		{waypointMember.c_str(), authoredWaypoints.data(), static_cast<u32>(authoredWaypoints.size()), 0},
		{groupMember.c_str(), authoredGroups.data(), static_cast<u32>(authoredGroups.size()), 0},
	};
	const std::string authoredPath = (root / "authored.pdarena").string();
	REQUIRE(assetArchiveRebuildFile(originalPath.c_str(), authoredPath.c_str(), "mod:arena_extension_source",
		"pd-tests", authored, 2, nullptr, err, sizeof(err)) == ASSET_ARCHIVE_CLONE_OK);
	const std::string authoredArena = levelReadFile(authoredPath);
	Extension authoredExtension(forgeRoomCreateExtension(authoredArena.data(), static_cast<u32>(authoredArena.size()),
		member, 2, 100, err, sizeof(err)), forgeRoomFreeExtension);
	INFO(err);
	REQUIRE(authoredExtension != nullptr);
	REQUIRE(forgeRoomExtensionEntries(authoredExtension.get(), &count));
	CHECK(count == 3);

	/* A valid source archive with even one different collision vertex
	 * cannot be treated as this fixed room shape. */
	const std::string altered = replaced(levelMember(scenario, "collision.obj"), "v 1725 ", "v 1724 ");
	const std::string alteredEntry = std::string(member) + "::collision.obj";
	const asset_archive_clone_override_t alteration = {alteredEntry.c_str(), altered.data(),
		static_cast<u32>(altered.size()), 0};
	const std::string alteredPath = (root / "altered.pdarena").string();
	REQUIRE(assetArchiveRebuildFile(originalPath.c_str(), alteredPath.c_str(), "mod:arena_extension_source",
		"pd-tests", &alteration, 1, nullptr, err, sizeof(err)) == ASSET_ARCHIVE_CLONE_OK);
	const std::string alteredArena = levelReadFile(alteredPath);
	REQUIRE(!alteredArena.empty());
	CHECK(forgeRoomCreateExtension(alteredArena.data(), static_cast<u32>(alteredArena.size()),
		member, 2, 100, err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("render or collision geometry") != std::string::npos);

	std::error_code ec;
	fs::remove_all(root, ec);
}

TEST_CASE("Grid room extension carries a draft brightness change into one native room table",
	"[forge-level-doc][grid-room-brightness]")
{
	const auto seed = fs::path(PD_SOURCE_DIR) / "dev-mods/grid_two_rooms/grid_two_rooms.pdarena";
	const std::string arena = levelReadFile(seed);
	const char *member = "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario";
	const std::string originalRooms = levelMember(levelMember(arena, member), "rooms.json");
	REQUIRE(!originalRooms.empty());
	Level draft(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", "", "", originalRooms);
	REQUIRE(draft.doc);
	char err[256] = {};
	REQUIRE(forgeLevelDocSetRoomBrightness(draft.doc, 1, 48, 112, err, sizeof(err)));
	const std::string roomDraft = draft.text(FORGE_LEVEL_MEMBER_ROOMS);
	const std::string roomsPath = std::string(member) + "::rooms.json";
	const std::string collisionPath = std::string(member) + "::collision.obj";
	using Extension = std::unique_ptr<forge_room_extension_t, decltype(&forgeRoomFreeExtension)>;
	for (const s32 room : {1, 2}) {
		CAPTURE(room);
		Extension extension(forgeRoomCreateExtensionWithRoomBrightness(arena.data(), static_cast<u32>(arena.size()),
			member, room, 450, roomDraft.data(), roomDraft.size(), err, sizeof(err)), forgeRoomFreeExtension);
		INFO(err);
		REQUIRE(extension);
		u32 count = 0;
		const auto *entries = forgeRoomExtensionEntries(extension.get(), &count);
		REQUIRE(entries);
		std::string rooms, collision;
		s32 roomOverrides = 0;
		for (u32 i = 0; i < count; ++i) {
			const auto &entry = entries[i];
			if (roomsPath == entry.entry) {
				++roomOverrides;
				rooms.assign(static_cast<const char *>(entry.data), entry.len);
			} else if (collisionPath == entry.entry) {
				collision.assign(static_cast<const char *>(entry.data), entry.len);
			}
		}
		REQUIRE(roomOverrides == 1);
		const std::string wall = room == 1 ? "-2000" : "2000";
		CHECK(rooms.find(wall) != std::string::npos);
		CHECK(collision.find("v " + wall + " ") != std::string::npos);
		Level reopened(kPads, kSpawns, kObjects, kVolumes, levelFields(), "", "", "", "", "", rooms);
		REQUIRE(reopened.doc);
		forge_level_room_brightness_t brightness = {};
		REQUIRE(forgeLevelDocGetRoomBrightness(reopened.doc, 0, &brightness));
		CHECK(brightness.roomnum == 1);
		CHECK(brightness.minimum == 48);
		CHECK(brightness.maximum == 112);
		REQUIRE(forgeLevelDocGetRoomBrightness(reopened.doc, 1, &brightness));
		CHECK(brightness.roomnum == 2);
		CHECK(brightness.minimum == 128);
		CHECK(brightness.maximum == 255);
	}
	const std::string altered = replaced(roomDraft, "\"origin\": [-775.000000", "\"origin\": [-776.000000");
	Extension refused(forgeRoomCreateExtensionWithRoomBrightness(arena.data(), static_cast<u32>(arena.size()),
		member, 2, 450, altered.data(), altered.size(), err, sizeof(err)), forgeRoomFreeExtension);
	CHECK_FALSE(refused);
	CHECK(std::string(err).find("pinned layout") != std::string::npos);
}

TEST_CASE("A changed level is saved by the mod author, and its navigation record matches its files",
          "[forge-level-doc]")
{
	/* F2's save path: the level's template archive, plus one override per
	 * member the document changed, through the same mod author every in-game
	 * creator saves with. Nothing here needs the game. */
	const fs::path templatePath = fs::path(PD_SOURCE_DIR) /
		"examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";
	const char *const scenarioMember = "dependencies/assets/scenarios/tri_scenario.pdscenario";
	const std::string arena = levelReadFile(templatePath);
	REQUIRE(!arena.empty());
	const std::string scenario = levelMember(arena, scenarioMember);
	REQUIRE(!scenario.empty());
	const std::string covers = levelMember(scenario, "navigation/covers.json");
	REQUIRE(!covers.empty());
	Level level(levelMember(scenario, "pads.json"), levelMember(scenario, "spawns.json"),
		levelMember(scenario, "objects.json"), levelMember(scenario, "volumes.json"),
		levelMember(scenario, "setup.fields.json"), levelMember(scenario, "level.graph.json"), "", "", covers);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(level.text(FORGE_LEVEL_MEMBER_COVERS) == covers);

	char err[256] = { 0 };
	forge_level_overrides_t overrides;
	/* Nothing changed: nothing to override. */
	REQUIRE(forgeLevelDocOverrides(level.doc, scenarioMember, &overrides, err, sizeof(err)) == 1);
	CHECK(overrides.count == 0);

	char id[32] = { 0 };
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, "team_1", id, sizeof(id), err, sizeof(err)) == 1);
	char cover[32] = {};
	REQUIRE(forgeLevelDocAddCoverAtPad(level.doc, "pad_0001", cover, sizeof(cover), err, sizeof(err)));
	CHECK(std::string(cover) == "cover_0000");
	REQUIRE(forgeLevelDocOverrides(level.doc, scenarioMember, &overrides, err, sizeof(err)) == 1);
	REQUIRE(overrides.count == 5);
	CHECK(std::string(overrides.entries[0].entry) == std::string(scenarioMember) + "::pads.json");
	CHECK(std::string(overrides.entries[1].entry) == std::string(scenarioMember) + "::spawns.json");
	CHECK(std::string(overrides.entries[2].entry) == std::string(scenarioMember) + "::volumes.json");
	CHECK(std::string(overrides.entries[3].entry) == std::string(scenarioMember) + "::level.graph.json");
	CHECK(std::string(overrides.entries[4].entry) == std::string(scenarioMember) + "::navigation/covers.json");
	CHECK(std::string(static_cast<const char *>(overrides.entries[1].data), overrides.entries[1].len) ==
		level.text(FORGE_LEVEL_MEMBER_SPAWNS));
	CHECK(std::string(static_cast<const char *>(overrides.entries[3].data), overrides.entries[3].len) ==
		level.text(FORGE_LEVEL_MEMBER_LEVEL_GRAPH));
	CHECK(overrides.entries[0].remove == 0);
	/* A scenario saved on its own names its members directly. */
	forge_level_overrides_t direct;
	REQUIRE(forgeLevelDocOverrides(level.doc, nullptr, &direct, err, sizeof(err)) == 1);
	REQUIRE(direct.count == 5);
	CHECK(std::string(direct.entries[0].entry) == "pads.json");
	CHECK(std::string(direct.entries[3].entry) == "level.graph.json");
	CHECK(std::string(direct.entries[4].entry) == "navigation/covers.json");
	CHECK(forgeLevelDocOverrides(nullptr, scenarioMember, &direct, err, sizeof(err)) == 0);
	CHECK(direct.count == 0);

	const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("pd2-forge-level-" + std::to_string(stamp));
	fs::create_directories(root);
	const std::string templateText = templatePath.string();
	const std::string rootText = root.string();
	mod_author_request_t request = {};
	request.template_path = templateText.c_str();
	request.mods_root = rootText.c_str();
	request.display_name = "Forge Yard";
	request.tool = "pd-tests";
	request.overrides = overrides.entries;
	request.override_count = overrides.count;
	mod_author_result_t result = {};
	const s32 code = modAuthorSave(&request, &result, err, sizeof(err));
	INFO("save error: " << err);
	REQUIRE(code == MOD_AUTHOR_OK);
	CHECK(std::string(result.catalog_id) == "mod:arena_forge_yard");
	char policy[256] = { 0 };
	const s32 valid = assetArchiveValidateFile(result.archive_path, ASSET_ARCHIVE_VALIDATE_RELEASE,
		policy, sizeof(policy));
	INFO("release validation: " << policy);
	CHECK(valid == 0);

	/* What the saved level holds: the template's scenario with the new pad,
	 * its bounds and the new spawn, read by this module as a level again. */
	const std::string saved = levelReadFile(result.archive_path);
	REQUIRE(!saved.empty());
	const std::string savedScenario = levelMember(saved, scenarioMember);
	REQUIRE(!savedScenario.empty());
	const std::string savedPads = levelMember(savedScenario, "pads.json");
	const std::string savedSpawns = levelMember(savedScenario, "spawns.json");
	const std::string savedVolumes = levelMember(savedScenario, "volumes.json");
	/* The saved level's own graph, with the ids the save gave it: it has the
	 * two trigger volume nodes the game will count against the two bounds
	 * rows, so the document reads the level as the game will. */
	const std::string savedGraph = levelMember(savedScenario, "level.graph.json");
	REQUIRE(!savedGraph.empty());
	CHECK(occurrences(savedGraph, kVolumeNode) == 2);
	CHECK(savedGraph.find("\"pads\": 2, \"volumes\": 2, \"objects\": 1,") != std::string::npos);
	CHECK(savedGraph.find("mod:arena_forge_yard__") != std::string::npos);
	const std::string savedCovers = levelMember(savedScenario, "navigation/covers.json");
	CHECK(savedCovers == level.text(FORGE_LEVEL_MEMBER_COVERS));
	CHECK(savedCovers.find("\"generate_routes\": true") != std::string::npos);
	Level again(savedPads, savedSpawns, levelMember(savedScenario, "objects.json"), savedVolumes,
		levelMember(savedScenario, "setup.fields.json"), savedGraph, "", "", savedCovers);
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(again.text(FORGE_LEVEL_MEMBER_COVERS) == savedCovers);
	CHECK(forgeLevelDocPadCount(again.doc) == 2);
	CHECK(forgeLevelDocSpawnCount(again.doc) == 2);
	CHECK(forgeLevelDocObjectCount(again.doc) == 1);
	CHECK(forgeLevelDocVolumeCount(again.doc) == 2);
	forge_level_spawn_t spawn = {};
	REQUIRE(forgeLevelDocGetSpawn(again.doc, 1, &spawn) == 1);
	CHECK(std::string(spawn.spawn_id) == id);
	CHECK(std::string(spawn.pad_ref) == "pad_0001");
	CHECK(std::string(spawn.team) == "team_1");
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetPad(again.doc, 1, &pad) == 1);
	CHECK(pad.position[0] == kPlace[0]);
	CHECK(pad.position[2] == kPlace[2]);
	CHECK(pad.look[2] == -1.0f);

	/* The game compares the scenario's navigation record with its files
	 * before it uses the level's navigation, and refuses the level when the
	 * counts or the hashes differ. The rebuild brings the record up to date,
	 * in the exact text the game looks for. */
	const std::string record = levelMember(savedScenario, "_meta/generated-navmesh.json");
	REQUIRE(!record.empty());
	INFO(record);
	CHECK(record.find("\"source_counts\": { \"pads\": 2, \"volumes\": 2, \"waypoints\": 0, "
		"\"waygroups\": 0, \"covers\": 1, \"paths\": 1 }") != std::string::npos);
	const std::string hashes = std::string("\"source_hashes\": { \"scene.glb\": \"") +
		levelSha(levelMember(savedScenario, "scene.glb")) + "\", \"collision.obj\": \"" +
		levelSha(levelMember(savedScenario, "collision.obj")) + "\", \"navigation.ini\": \"" +
		levelSha(levelMember(savedScenario, "navigation.ini")) + "\", \"portals.json\": \"" +
		levelSha(levelMember(savedScenario, "portals.json")) + "\", \"pads.json\": \"" +
		levelSha(savedPads) + "\", \"spawns.json\": \"" + levelSha(savedSpawns) +
		"\", \"volumes.json\": \"" + levelSha(savedVolumes) + "\", \"navigation/waypoints.json\": \"" +
		levelSha(levelMember(savedScenario, "navigation/waypoints.json")) +
		"\", \"navigation/waygroups.json\": \"" +
		levelSha(levelMember(savedScenario, "navigation/waygroups.json")) +
		"\", \"navigation/covers.json\": \"" +
		levelSha(savedCovers) +
		"\", \"navigation/paths.json\": \"" +
		levelSha(levelMember(savedScenario, "navigation/paths.json")) + "\" }";
	CHECK(record.find(hashes) != std::string::npos);
	/* The rest of the record is the template's, with the scenario's new id. */
	CHECK(record.find("deterministic.surface_graph.v1") != std::string::npos);
	CHECK(record.find("mod:arena_forge_yard__") != std::string::npos);

	const char *artifacts = std::getenv("PD2_TEST_ARTIFACT_DIR");
	if (artifacts && artifacts[0]) {
		std::error_code copyError;
		const fs::path dir = fs::path(artifacts) / "clones";
		fs::create_directories(dir, copyError);
		fs::copy_file(result.archive_path, dir / fs::path(result.archive_path).filename(),
			fs::copy_options::overwrite_existing, copyError);
	}

	std::error_code ec;
	fs::remove_all(root, ec);
}

namespace {

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

} // namespace

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

namespace {

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

/* ------------------------------------------------------------------------ */
/* What the world shows of the open level while the editor camera flies     */
/* (forge_level_live.h)                                                      */
/* ------------------------------------------------------------------------ */

namespace {

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

} // namespace

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
