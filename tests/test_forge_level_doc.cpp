/**
 * test_forge_level_doc.cpp -- the placements of a level, as Forge edits them
 * (port/include/forge_level_doc.h; step F2 of
 * context/designs/modding/forge-mode-scope.md).
 *
 * The document reads a level's pads, spawns and objects, changes them, and
 * gives the three texts back. The cases check the rules it promises: a row
 * nobody touched keeps its bytes, nothing is repaired, pads are only added,
 * and an id is never handed out twice.
 *
 * With PD2_BASE_TEMPLATE_DIR set (a folder of extracted base levels, local
 * only) the last case reads every base level and writes it back unchanged.
 */
#include "catch.hpp"

#include "forge_level_doc.h"
#include "modarchive.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

/* Three pads, two spawns and three objects, in the extractor's layout. The
 * second spawn and the second object share pad_0001; the third object stands
 * on no pad; every row carries a key the module does not know. */
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
	"      \"kind\": \"weapon\",\n"
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

struct Level {
	forge_level_doc_t *doc = nullptr;
	std::string error;

	Level(const std::string &pads, const std::string &spawns, const std::string &objects)
	{
		char err[256] = { 0 };
		doc = forgeLevelDocRead(pads.data(), pads.size(), spawns.data(), spawns.size(),
			objects.data(), objects.size(), err, sizeof(err));
		error = err;
	}
	Level() : Level(kPads, kSpawns, kObjects) {}
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

/* The lines of a text; rows of the extractor's pads and spawns are one each. */
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

	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetPad(level.doc, 2, &pad) == 1);
	CHECK(std::string(pad.pad_ref) == "pad_0002");
	CHECK(std::string(pad.room_ref) == "room_0003");
	CHECK(pad.position[0] == 50.0f);
	CHECK(pad.position[1] == 8.0f);
	CHECK(pad.look[2] == 1.0f);
	CHECK(pad.added == 0);
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
	CHECK(std::string(object.kind) == "weapon");
	CHECK(std::string(object.weapon_catalog_id) == "base:falcon2");
	REQUIRE(forgeLevelDocGetObject(level.doc, 2, &object) == 1);
	CHECK(std::string(object.pad_ref).empty());

	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		CHECK(forgeLevelDocMemberChanged(level.doc, member) == 0);
	}
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == kPads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == kSpawns);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == kObjects);
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_PADS)) == "pads.json");
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_SPAWNS)) == "spawns.json");
	CHECK(std::string(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_OBJECTS)) == "objects.json");
	CHECK(forgeLevelDocMemberName(FORGE_LEVEL_MEMBER_COUNT) == nullptr);
	CHECK(forgeLevelDocMemberText(level.doc, -1, nullptr) == nullptr);
}

TEST_CASE("A new spawn gets a pad of its own and the next id; every other row keeps its bytes",
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

	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetPad(level.doc, 3, &pad) == 1);
	CHECK(std::string(pad.pad_ref) == "pad_0003");
	CHECK(pad.added == 1);
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

	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_PADS) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_SPAWNS) == 1);
	CHECK(forgeLevelDocMemberChanged(level.doc, FORGE_LEVEL_MEMBER_OBJECTS) == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == kObjects);

	/* What was written is a level the module reads again, the same level. */
	Level again(level.text(FORGE_LEVEL_MEMBER_PADS), level.text(FORGE_LEVEL_MEMBER_SPAWNS),
		level.text(FORGE_LEVEL_MEMBER_OBJECTS));
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(forgeLevelDocPadCount(again.doc) == 4);
	forge_level_spawn_t spawn = {};
	REQUIRE(forgeLevelDocGetSpawn(again.doc, 2, &spawn) == 1);
	CHECK(std::string(spawn.spawn_id) == "spawn_0005");
	CHECK(std::string(spawn.pad_ref) == "pad_0003");
	CHECK(std::string(spawn.team) == "any");
	/* Read back, the pad is the level's own and no longer this document's. */
	REQUIRE(forgeLevelDocGetPad(again.doc, 3, &pad) == 1);
	CHECK(pad.added == 0);
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
	/* The pad the removed spawn stood on stays: pads are only ever added. */
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
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

	/* Removing a spawn from the template, in the middle of the file. */
	REQUIRE(forgeLevelDocRemoveSpawn(level.doc, "spawn_0000", err, sizeof(err)) == 1);
	const std::vector<std::string> lines = linesOf(level.text(FORGE_LEVEL_MEMBER_SPAWNS));
	const std::vector<std::string> original = linesOf(kSpawns);
	REQUIRE(lines.size() == original.size());
	CHECK(lines[3] == original[4] + ",");
	CHECK(lines[4].find("\"spawn_id\": \"spawn_0006\"") != std::string::npos);
	CHECK(forgeLevelDocRemoveSpawn(level.doc, "spawn_0000", err, sizeof(err)) == 0);
	CHECK(std::string(err) == "the level has no spawn \"spawn_0000\"");
}

TEST_CASE("Moving something off a template pad gives it a new pad and leaves the old one",
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

	const std::vector<std::string> pads = linesOf(level.text(FORGE_LEVEL_MEMBER_PADS));
	const std::vector<std::string> padsBefore = linesOf(kPads);
	CHECK(pads[3] == padsBefore[3]);
	CHECK(pads[4] == padsBefore[4]);
	CHECK(pads[6].find("\"position\": [-5.000000, 12.000000, 7.250000]") != std::string::npos);
	CHECK(pads[6].find("\"look\": [1.000000, 0.000000, 0.000000]") != std::string::npos);

	/* Only the spawn's pad name changed in its row; its unknown key is kept. */
	const std::vector<std::string> spawns = linesOf(level.text(FORGE_LEVEL_MEMBER_SPAWNS));
	const std::vector<std::string> spawnsBefore = linesOf(kSpawns);
	CHECK(spawns[3] == spawnsBefore[3]);
	CHECK(spawns[4] == replaced(spawnsBefore[4], "\"pad_0001\"", "\"pad_0003\""));
	CHECK(spawns[4].find("\"weight\": 2") != std::string::npos);

	/* A second move finds a pad this document added and moves that one. */
	const f32 further[3] = { -6.0f, 12.0f, 9.0f };
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, "spawn_0004", further, facing, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocPadCount(level.doc) == 4);
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocFindPad(level.doc, "pad_0003", &pad) == 1);
	CHECK(pad.position[0] == -6.0f);
	CHECK(pad.position[2] == 9.0f);
	const std::vector<std::string> padsMoved = linesOf(level.text(FORGE_LEVEL_MEMBER_PADS));
	CHECK(padsMoved[6].find("\"position\": [-6.000000, 12.000000, 9.000000]") != std::string::npos);
	CHECK(padsMoved[6].find("\"pad_ref\": \"pad_0003\"") != std::string::npos);

	/* An object moves the same way: a new pad, and only its pad name changes. */
	REQUIRE(forgeLevelDocMoveObject(level.doc, "setup_0000", place, facing, err, sizeof(err)) == 1);
	forge_level_object_t gun = {};
	REQUIRE(forgeLevelDocGetObject(level.doc, 0, &gun) == 1);
	CHECK(std::string(gun.pad_ref) == "pad_0004");
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) ==
		replaced(kObjects, "\"pad_ref\": \"pad_0002\"", "\"pad_ref\": \"pad_0004\""));

	/* An object that stands on no pad has no place to move. */
	CHECK(forgeLevelDocMoveObject(level.doc, "setup_0002", place, facing, err, sizeof(err)) == 0);
	CHECK(std::string(err) == "setup_0002 stands on no pad, so it has no place to move");
	CHECK(forgeLevelDocMoveObject(level.doc, "setup_0009", place, facing, err, sizeof(err)) == 0);
	CHECK(std::string(err) == "the level has no object \"setup_0009\"");
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
	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		CHECK(forgeLevelDocMemberChanged(level.doc, member) == 0);
	}
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == kPads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == kSpawns);
}

TEST_CASE("A level that breaks a rule is refused with the reason, never repaired",
          "[forge-level-doc]")
{
	const auto refused = [](const std::string &pads, const std::string &spawns, const std::string &objects) {
		Level level(pads, spawns, objects);
		CHECK(level.doc == nullptr);
		return level.error;
	};
	CHECK(refused("[]", kSpawns, kObjects) == "pads.json is not one JSON object");
	CHECK(refused(kPads, "", kObjects) == "spawns.json is not one JSON object");
	CHECK(refused(kPads, kSpawns, "{ \"schema\": \"pd2.scenario.objects.v1\" }") ==
		"objects.json has no rows array");
	CHECK(refused(replaced(kPads, "pads.v1", "pads.v2"), kSpawns, kObjects) ==
		"pads.json needs \"schema\": \"pd2.scenario.pads.v1\"");
	/* The files are not interchangeable. */
	CHECK(refused(kSpawns, kSpawns, kObjects) == "pads.json needs \"schema\": \"pd2.scenario.pads.v1\"");
	/* A pad's name is its place in the file. */
	CHECK(refused(replaced(kPads, "pad_0001", "pad_0007"), kSpawns, kObjects) ==
		"pads.json row 1 is named \"pad_0007\"; a pad's name is its place in the file (pad_0001)");
	CHECK(refused(replaced(kPads, "\"up\": [0.000000, 1.000000, 0.000000]", "\"up\": [0.000000, 1.000000]"),
		kSpawns, kObjects) == "pads.json pad_0000 needs position, up and look, three numbers each");
	CHECK(refused(kPads, replaced(kSpawns, "spawn_0004", "spawn_0000"), kObjects) ==
		"spawns.json lists spawn_0000 twice");
	CHECK(refused(kPads, replaced(kSpawns, "\"spawn_id\": \"spawn_0000\", ", ""), kObjects) ==
		"spawns.json row 0 has no spawn_id");
	CHECK(refused(kPads, replaced(kSpawns, "\"pad_ref\": \"pad_0001\"", "\"pad_ref\": \"pad_0009\""), kObjects) ==
		"spawns.json spawn_0004 names pad \"pad_0009\", which the level does not have");
	CHECK(refused(kPads, kSpawns, replaced(kObjects, "setup_0001", "setup_0000")) ==
		"objects.json lists setup_0000 twice");
	CHECK(refused(kPads, kSpawns, replaced(kObjects, "\"pad_ref\": \"pad_0002\"", "\"pad_ref\": \"pad_0200\"")) ==
		"objects.json setup_0000 names pad \"pad_0200\", which the level does not have");
	/* Text after the object is not JSON, and is not dropped quietly. */
	CHECK(refused(std::string(kPads) + "x", kSpawns, kObjects) == "pads.json is not one JSON object");
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
	Level level(pads, spawns, objects);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(forgeLevelDocPadCount(level.doc) == 1);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 0);
	CHECK(forgeLevelDocObjectCount(level.doc) == 0);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);

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
	/* The one-line pads file keeps its first row's bytes and gains a second. */
	const std::string padsAfter = level.text(FORGE_LEVEL_MEMBER_PADS);
	const std::string firstRow = pads.substr(pads.find("{\"pad_ref\""), pads.find("}]") + 1 - pads.find("{\"pad_ref\""));
	CHECK(padsAfter.find(firstRow + ", { \"pad_ref\": \"pad_0001\"") != std::string::npos);

	Level again(padsAfter, level.text(FORGE_LEVEL_MEMBER_SPAWNS), level.text(FORGE_LEVEL_MEMBER_OBJECTS));
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(forgeLevelDocPadCount(again.doc) == 2);
	CHECK(forgeLevelDocSpawnCount(again.doc) == 1);
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
	REQUIRE(!pads.empty());
	REQUIRE(!spawns.empty());
	REQUIRE(!objects.empty());

	Level level(pads, spawns, objects);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	CHECK(forgeLevelDocPadCount(level.doc) == 1);
	CHECK(forgeLevelDocSpawnCount(level.doc) == 1);
	CHECK(forgeLevelDocObjectCount(level.doc) == 1);
	CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
	CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);

	/* Its spawn and its prop share the one pad. Moving the spawn leaves the
	 * prop where it was. This file repeats the place in the spawn's row, so
	 * the row's copy is cleared: the pad is what the game reads. */
	char err[200] = { 0 };
	REQUIRE(forgeLevelDocMoveSpawn(level.doc, "spawn_0000", kPlace, kFacing, err, sizeof(err)) == 1);
	CHECK(forgeLevelDocPadCount(level.doc) == 2);
	CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);
	const std::string moved = level.text(FORGE_LEVEL_MEMBER_SPAWNS);
	CHECK(moved.find("\"pad_ref\": \"pad_0001\"") != std::string::npos);
	CHECK(moved.find("\"position\": null") != std::string::npos);
	CHECK(moved.find("\"look\": null") != std::string::npos);
	CHECK(moved.find("\"profile\": \"default\"") != std::string::npos);
	const std::string padsMoved = level.text(FORGE_LEVEL_MEMBER_PADS);
	CHECK(padsMoved.compare(0, pads.rfind('}', pads.rfind(']')) + 1, pads, 0, pads.rfind('}', pads.rfind(']')) + 1) == 0);

	Level again(padsMoved, moved, level.text(FORGE_LEVEL_MEMBER_OBJECTS));
	INFO(again.error);
	REQUIRE(again.doc != nullptr);
	CHECK(forgeLevelDocPadCount(again.doc) == 2);
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
			if (pads.empty() || spawns.empty() || objects.empty()) continue;
			Level level(pads, spawns, objects);
			INFO(entry.path().string() << ": " << level.error);
			REQUIRE(level.doc != nullptr);
			CHECK(level.text(FORGE_LEVEL_MEMBER_PADS) == pads);
			CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
			CHECK(level.text(FORGE_LEVEL_MEMBER_OBJECTS) == objects);

			/* Adding a spawn and removing it again leaves the spawns as they
			 * were, which exercises the rebuilt text on real files. */
			char id[32] = { 0 };
			char err[200] = { 0 };
			REQUIRE(forgeLevelDocAddSpawn(level.doc, kPlace, kFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
			REQUIRE(forgeLevelDocRemoveSpawn(level.doc, id, err, sizeof(err)) == 1);
			CHECK(level.text(FORGE_LEVEL_MEMBER_SPAWNS) == spawns);
			levels++;
			padTotal += forgeLevelDocPadCount(level.doc);
		}
	}
	WARN("base levels read and written back unchanged: " << levels << " (" << padTotal << " pads)");
	CHECK(levels > 0);
}
