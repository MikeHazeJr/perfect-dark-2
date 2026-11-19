/**
 * test_level_tool.cpp -- the rules of the Modding Hub's Levels tool
 * (port/include/level_tool.h; step F4 of
 * context/designs/modding/forge-mode-scope.md).
 *
 * What a level's placements add up to, how the tool says it in one line, and
 * which names a new level may take: a new level never replaces one the player
 * already has.
 */
#include "catch.hpp"

#include "forge_level_doc.h"
#include "level_tool.h"
#include "mod_author.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

/* Three pads, two starts, a prop on the second pad and a tag that stands
 * nowhere, in the extractor's layout (the fixture of test_forge_level_doc.cpp
 * with the record ids in command order). */
const char *const kToolPads =
	"{\n"
	"  \"schema\": \"pd2.scenario.pads.v1\",\n"
	"  \"rows\": [\n"
	"    { \"pad_ref\": \"pad_0000\", \"room_ref\": \"\", \"liftnum\": 0, \"flags\": \"0x00185\", \"position\": [10.000000, 0.000000, 20.000000], \"up\": [0.000000, 1.000000, 0.000000], \"look\": [0.000000, 0.000000, -1.000000], \"bbox\": { \"min\": [-100.000000, -100.000000, -100.000000], \"max\": [100.000000, 100.000000, 100.000000] } },\n"
	"    { \"pad_ref\": \"pad_0001\", \"room_ref\": \"\", \"liftnum\": 0, \"flags\": \"0x00025\", \"position\": [30.000000, 0.000000, 40.000000], \"up\": [0.000000, 1.000000, 0.000000], \"look\": [1.000000, 0.000000, 0.000000], \"bbox\": { \"min\": [-100.000000, -100.000000, -100.000000], \"max\": [100.000000, 100.000000, 100.000000] } },\n"
	"    { \"pad_ref\": \"pad_0002\", \"room_ref\": \"\", \"liftnum\": 0, \"flags\": \"0x00085\", \"position\": [50.000000, 8.000000, 60.000000], \"up\": [0.000000, 1.000000, 0.000000], \"look\": [0.000000, 0.000000, 1.000000], \"bbox\": { \"min\": [-100.000000, -100.000000, -100.000000], \"max\": [100.000000, 100.000000, 100.000000] } }\n"
	"  ]\n"
	"}\n";

const char *const kToolSpawns =
	"{\n"
	"  \"schema\": \"pd2.scenario.spawns.v1\",\n"
	"  \"rows\": [\n"
	"    { \"spawn_id\": \"spawn_0000\", \"pad_ref\": \"pad_0000\", \"room_ref\": \"\", \"team\": \"any\", \"profile\": \"intro\", \"position\": null, \"look\": null },\n"
	"    { \"spawn_id\": \"spawn_0001\", \"pad_ref\": \"pad_0002\", \"room_ref\": \"\", \"team\": \"any\", \"profile\": \"intro\", \"position\": null, \"look\": null }\n"
	"  ]\n"
	"}\n";

const char *const kToolNoSpawns = "{\n  \"schema\": \"pd2.scenario.spawns.v1\",\n  \"rows\": [\n  ]\n}\n";

const char *const kToolObjects =
	"{\n"
	"  \"schema\": \"pd2.scenario.objects.v1\",\n"
	"  \"rows\": [\n"
	"    {\n"
	"      \"record_id\": \"setup_0000\",\n"
	"      \"kind\": \"prop\",\n"
	"      \"pad_ref\": \"pad_0001\",\n"
	"      \"model_catalog_id\": \"base:model_crate\",\n"
	"      \"weapon_catalog_id\": \"\",\n"
	"      \"flags\": \"0x00000000\"\n"
	"    },\n"
	"    {\n"
	"      \"record_id\": \"setup_0001\",\n"
	"      \"kind\": \"tag\",\n"
	"      \"pad_ref\": \"\",\n"
	"      \"model_catalog_id\": \"\",\n"
	"      \"weapon_catalog_id\": \"\",\n"
	"      \"flags\": \"0x00000000\"\n"
	"    }\n"
	"  ]\n"
	"}\n";

const char *const kToolNoObjects = "{\n  \"schema\": \"pd2.scenario.objects.v1\",\n  \"rows\": [\n  ]\n}\n";

const char *const kToolVolumes =
	"{\n"
	"  \"schema\": \"pd2.scenario.volumes.v1\",\n"
	"  \"rows\": [\n"
	"    { \"volume_id\": \"volume_pad_0000\", \"pad_ref\": \"pad_0000\", \"kind\": \"pad_bounds\", \"room_ref\": \"\", \"shape\": \"aabb\", \"min\": [-90.000000, -100.000000, -80.000000], \"max\": [110.000000, 100.000000, 120.000000] },\n"
	"    { \"volume_id\": \"volume_pad_0001\", \"pad_ref\": \"pad_0001\", \"kind\": \"pad_bounds\", \"room_ref\": \"\", \"shape\": \"aabb\", \"min\": [-70.000000, -100.000000, -60.000000], \"max\": [130.000000, 100.000000, 140.000000] },\n"
	"    { \"volume_id\": \"volume_pad_0002\", \"pad_ref\": \"pad_0002\", \"kind\": \"pad_bounds\", \"room_ref\": \"\", \"shape\": \"aabb\", \"min\": [-50.000000, -92.000000, -40.000000], \"max\": [150.000000, 108.000000, 160.000000] }\n"
	"  ]\n"
	"}\n";

std::string toolFieldRow(const char *record, const char *kind, const char *field, const char *type,
	const std::string &value, const char *ref = "")
{
	return std::string("    {\n      \"record_id\": \"") + record + "\",\n      \"kind\": \"" + kind +
		"\",\n      \"field\": \"" + field + "\",\n      \"type\": \"" + type + "\",\n      \"value\": \"" +
		value + "\",\n      \"catalog_id\": \"\",\n      \"ref_record_id\": \"" + ref + "\"\n    }";
}

std::string toolFields()
{
	return std::string("{\n  \"schema\": \"pd2.scenario.setup.fields.v1\",\n  \"rows\": [\n") +
		toolFieldRow("setup_0000", "prop", "command.order", "s32", "0") + ",\n" +
		toolFieldRow("setup_0000", "prop", "base.pad", "pad_ref", "pad_0001") + ",\n" +
		toolFieldRow("setup_0001", "tag", "command.order", "s32", "1") + ",\n" +
		toolFieldRow("setup_0001", "tag", "tag.target", "s32", "", "setup_0000") +
		"\n  ]\n}\n";
}

const char *const kToolNoFields = "{\n  \"schema\": \"pd2.scenario.setup.fields.v1\",\n  \"rows\": [\n  ]\n}\n";

/* A level's graph cut down to what the document reads: a trigger volume node
 * and a link for each of the three bounds rows, and the counts. */
std::string toolGraph(size_t objects)
{
	std::string nodes = "    { \"id\": \"scenario.load\", \"kind\": \"event.scenario.load\" },\n";
	std::string links;
	for (unsigned i = 0; i < 3; i++) {
		char number[16];
		snprintf(number, sizeof(number), "%04u", i);
		nodes += std::string("    { \"id\": \"trigger.volume.") + number +
			"\", \"kind\": \"scenario.trigger.volume.source\", \"table\": \"volumes.json\", \"volume\": \"volume_pad_" +
			number + "\", \"pad\": \"pad_" + number + "\" },\n";
		links += std::string("    { \"from\": \"scenario.load\", \"to\": \"trigger.volume.") + number + "\" },\n";
	}
	nodes += "    { \"id\": \"scenario.ai.lists\", \"kind\": \"scenario.ai.lists.source\", \"source\": \"ai/ailists.json\" }\n";
	links += "    { \"from\": \"scenario.load\", \"to\": \"scenario.ai.lists\" }\n";
	return "{\n  \"schema\": \"pd2.level.graph.v1\",\n  \"scenario\": \"example:tri_scenario\",\n  \"nodes\": [\n" +
		nodes + "  ],\n  \"links\": [\n" + links +
		"  ],\n  \"counts\": { \"rooms\": 2, \"triangles\": 1, \"pads\": 3, \"volumes\": 3, \"objects\": " +
		std::to_string(objects) + ", \"objectives\": 0 }\n}\n";
}

struct ToolLevel {
	forge_level_doc_t *doc = nullptr;
	std::string error;

	ToolLevel(const std::string &spawns, const std::string &objects, const std::string &fields, size_t objectCount)
	{
		char err[256] = { 0 };
		forge_level_texts_t texts = {};
		const std::string pads = kToolPads;
		const std::string volumes = kToolVolumes;
		const std::string graph = toolGraph(objectCount);
		const std::string *all[FORGE_LEVEL_MEMBER_COUNT] = { &pads, &spawns, &objects, &volumes, &fields, &graph };
		for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
			if (!all[member]) continue;
			texts.text[member] = all[member]->data();
			texts.size[member] = all[member]->size();
		}
		doc = forgeLevelDocRead(&texts, err, sizeof(err));
		error = err;
	}
	~ToolLevel() { forgeLevelDocFree(doc); }
	ToolLevel(const ToolLevel &) = delete;
	ToolLevel &operator=(const ToolLevel &) = delete;
};

std::string describe(const level_tool_summary_t &summary)
{
	char line[256];
	levelToolDescribe(&summary, line, sizeof(line));
	return line;
}

const f32 kToolPlace[3] = { 100.0f, 0.0f, -250.5f };
const f32 kToolFacing[3] = { 0.0f, 0.0f, -4.0f };

} // namespace

TEST_CASE("A level's placements are counted by what a player would see of them", "[level-tool]")
{
	ToolLevel level(kToolSpawns, kToolObjects, toolFields(), 2);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);
	char err[256] = { 0 };
	char id[32] = { 0 };

	level_tool_summary_t summary;
	levelToolSummarise(level.doc, &summary);
	CHECK(summary.pads == 3);
	CHECK(summary.starts == 2);
	CHECK(summary.objects == 2);
	CHECK(summary.props == 1);
	CHECK(summary.other == 1);
	CHECK(summary.weapon_places == 0);
	CHECK(summary.fixed_weapons == 0);
	CHECK(summary.ammo_crates == 0);
	CHECK(summary.left_out == 0);
	CHECK(describe(summary) == "2 starts, 1 prop, 1 other object");

	/* Two weapon places and a crate, made in Forge. */
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 3, kToolPlace, kToolFacing,
		id, sizeof(id), err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0002");
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 4, kToolPlace, kToolFacing,
		id, sizeof(id), err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0003");
	REQUIRE(forgeLevelDocAddObject(level.doc, FORGE_LEVEL_NEW_AMMO_CRATE, 0, kToolPlace, kToolFacing,
		id, sizeof(id), err, sizeof(err)) == 1);
	CHECK(std::string(id) == "setup_0004");
	levelToolSummarise(level.doc, &summary);
	CHECK(summary.pads == 6);
	CHECK(summary.objects == 5);
	CHECK(summary.weapon_places == 2);
	CHECK(summary.fixed_weapons == 0);
	CHECK(summary.ammo_crates == 1);
	CHECK(describe(summary) == "2 starts, 2 weapon places, 1 ammo crate, 1 prop, 1 other object");

	/* One place always gives the same weapon: it is a fixed weapon now, and
	 * no longer one of the match's places. */
	REQUIRE(forgeLevelDocSetPickupWeapon(level.doc, "setup_0003", "base:dragon", "base:model_chrdragon", 256,
		err, sizeof(err)) == 1);
	levelToolSummarise(level.doc, &summary);
	CHECK(summary.weapon_places == 1);
	CHECK(summary.fixed_weapons == 1);
	CHECK(describe(summary) == "2 starts, 1 weapon place, 1 fixed weapon, 1 ammo crate, 1 prop, 1 other object");

	/* An object the level leaves out is not in the level a player sees: it
	 * counts as left out, not under its kind. Its record is still there. */
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0004", 1, err, sizeof(err)) == 1);
	levelToolSummarise(level.doc, &summary);
	CHECK(summary.objects == 5);
	CHECK(summary.ammo_crates == 0);
	CHECK(summary.left_out == 1);
	CHECK(describe(summary) == "2 starts, 1 weapon place, 1 fixed weapon, 1 prop, 1 other object, 1 left out");
	REQUIRE(forgeLevelDocSetObjectRemoved(level.doc, "setup_0004", 0, err, sizeof(err)) == 1);
	levelToolSummarise(level.doc, &summary);
	CHECK(summary.ammo_crates == 1);
	CHECK(summary.left_out == 0);

	/* A third start. */
	REQUIRE(forgeLevelDocAddSpawn(level.doc, kToolPlace, kToolFacing, nullptr, id, sizeof(id), err, sizeof(err)) == 1);
	levelToolSummarise(level.doc, &summary);
	CHECK(summary.starts == 3);
	CHECK(summary.pads == 7);
}

TEST_CASE("An empty level says it has no starts, and nothing is read from no level", "[level-tool]")
{
	ToolLevel level(kToolNoSpawns, kToolNoObjects, kToolNoFields, 0);
	INFO(level.error);
	REQUIRE(level.doc != nullptr);

	level_tool_summary_t summary;
	levelToolSummarise(level.doc, &summary);
	CHECK(summary.pads == 3);
	CHECK(summary.starts == 0);
	CHECK(summary.objects == 0);
	CHECK(describe(summary) == "no starts");

	/* No level: all zeros, and the line still says what a player needs. */
	memset(&summary, 0x55, sizeof(summary));
	levelToolSummarise(nullptr, &summary);
	CHECK(summary.pads == 0);
	CHECK(summary.starts == 0);
	CHECK(summary.objects == 0);
	CHECK(summary.left_out == 0);
	CHECK(describe(summary) == "no starts");
	levelToolSummarise(level.doc, nullptr); /* nowhere to write: nothing happens */

	/* The line is always terminated and cut at its room. */
	level_tool_summary_t big = {};
	big.starts = 12;
	big.weapon_places = 16;
	big.fixed_weapons = 3;
	big.ammo_crates = 9;
	big.props = 140;
	big.other = 77;
	big.left_out = 5;
	CHECK(describe(big) ==
		"12 starts, 16 weapon places, 3 fixed weapons, 9 ammo crates, 140 props, 77 other objects, 5 left out");
	char small[12];
	memset(small, 'x', sizeof(small));
	levelToolDescribe(&big, small, sizeof(small));
	CHECK(std::string(small) == "12 starts, ");
	char one[1] = { 'x' };
	levelToolDescribe(&big, one, sizeof(one));
	CHECK(one[0] == '\0');
	levelToolDescribe(&big, nullptr, 0); /* nowhere to write: nothing happens */
	char none[8] = "before";
	levelToolDescribe(nullptr, none, sizeof(none));
	CHECK(std::string(none).empty());
}

TEST_CASE("A new level takes a name nothing has, and never the place of a level the player has", "[level-tool]")
{
	mod_author_result_t plan;
	memset(&plan, 0, sizeof(plan));
	snprintf(plan.catalog_id, sizeof(plan.catalog_id), "%s", "mod:arena_my_room");
	char why[256] = "before";

	/* A name nothing has: a new level. */
	CHECK(levelToolNewLevelName("My Room", "base:arena_mp_complex", MOD_AUTHOR_OK, &plan, "", why, sizeof(why)) ==
		LEVEL_TOOL_NAME_OK);
	CHECK(std::string(why).empty());

	/* Nothing typed yet: nothing to say. */
	strcpy(why, "before");
	CHECK(levelToolNewLevelName("", "base:arena_mp_complex", MOD_AUTHOR_OK, &plan, "", why, sizeof(why)) ==
		LEVEL_TOOL_NAME_EMPTY);
	CHECK(std::string(why).empty());
	CHECK(levelToolNewLevelName(nullptr, "base:arena_mp_complex", MOD_AUTHOR_OK, &plan, "", why, sizeof(why)) ==
		LEVEL_TOOL_NAME_EMPTY);

	/* A level of the player's already has the name: the mod author would
	 * update it in place, which here would put a fresh copy of the template
	 * over the player's work. Refused, naming the level. */
	plan.updated = 1;
	CHECK(levelToolNewLevelName("My Room", "base:arena_mp_complex", MOD_AUTHOR_OK, &plan, "", why, sizeof(why)) ==
		LEVEL_TOOL_NAME_TAKEN);
	CHECK(std::string(why) ==
		"You already have a level with that name (mod:arena_my_room). Open that level in Forge, or pick another name.");

	/* The template's own name, when the template is a level of the player's. */
	CHECK(levelToolNewLevelName("My Room", "mod:arena_my_room", MOD_AUTHOR_OK, &plan, "", why, sizeof(why)) ==
		LEVEL_TOOL_NAME_TEMPLATE);
	CHECK(std::string(why) == "That is this level's own name. Pick another name for the new level.");
	plan.updated = 0;
	CHECK(levelToolNewLevelName("My Room", "mod:arena_my_room", MOD_AUTHOR_OK, &plan, "", why, sizeof(why)) ==
		LEVEL_TOOL_NAME_TEMPLATE);

	/* The mod author refuses the name: its reason is passed on. */
	CHECK(levelToolNewLevelName("Bad\tName", "base:arena_mp_complex", MOD_AUTHOR_ERR_ARGS, &plan,
		"the display name has a control character", why, sizeof(why)) == LEVEL_TOOL_NAME_REFUSED);
	CHECK(std::string(why) == "That name cannot be used: the display name has a control character.");
	CHECK(levelToolNewLevelName("My Room", "base:arena_mp_complex", MOD_AUTHOR_ERR_EXISTS, &plan, nullptr,
		why, sizeof(why)) == LEVEL_TOOL_NAME_REFUSED);
	CHECK(std::string(why) == "That name cannot be used: the mod author refused it.");
	/* An answer of OK with no plan to read is not an answer. */
	CHECK(levelToolNewLevelName("My Room", "base:arena_mp_complex", MOD_AUTHOR_OK, nullptr, "", why, sizeof(why)) ==
		LEVEL_TOOL_NAME_REFUSED);

	/* A caller that wants only the answer passes no room for the sentence. */
	plan.updated = 1;
	CHECK(levelToolNewLevelName("My Room", "base:arena_mp_complex", MOD_AUTHOR_OK, &plan, "", nullptr, 0) ==
		LEVEL_TOOL_NAME_TAKEN);
	/* No template named: only the "already have it" rule applies. */
	plan.updated = 0;
	CHECK(levelToolNewLevelName("My Room", nullptr, MOD_AUTHOR_OK, &plan, "", why, sizeof(why)) == LEVEL_TOOL_NAME_OK);
	CHECK(levelToolNewLevelName("My Room", "", MOD_AUTHOR_OK, &plan, "", why, sizeof(why)) == LEVEL_TOOL_NAME_OK);
}
