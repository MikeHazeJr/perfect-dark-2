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
			"gate.player.has_item",
			/* object-events.md slice 5: the lift and glass rules the example
			 * level's graph wires (mission_graph_modules.c has them). */
			"gate.lift.native_vacant", "og.lift.go_to_stop", "gate.lift.native_ready",
			"og.lift.go_to_next_stop", "gate.lift.native_door_unlocked", "og.lift.open_door",
			"gate.glass.native_broken", "og.glass.break"}) {
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
