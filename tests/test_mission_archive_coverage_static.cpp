// Every campaign stage gets a mission archive.
//
// Mission archives used to be emitted once per arena row in the "Solo
// Missions" category. The arena table lists 14 of the 21 campaign stages, so
// Extraction, Rescue, Escape and the four special assignments had a stage and
// a scenario but no .pdmission, and their objectives stayed on the native path
// while the other 14 ran from mission graph source. The extractor now also
// walks the campaign's own stage table.
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

// Slugs of the campaign stage table: { STAGE_X, ..., "base:slug" } rows of
// g_SoloStages.
static std::set<std::string> campaignStageSlugs()
{
	const std::string source = readSource("src/game/mainmenu.c");
	const size_t begin = source.find("struct solostage g_SoloStages[NUM_SOLOSTAGES] = {");
	REQUIRE(begin != std::string::npos);
	const size_t end = source.find("};", begin);
	REQUIRE(end != std::string::npos);
	const std::string table = source.substr(begin, end - begin);
	const std::regex row(R"re(\{\s*STAGE_[A-Z0-9_]+\s*,[^}]*"base:([a-z0-9_]+)"\s*\})re");
	std::set<std::string> slugs;
	for (auto it = std::sregex_iterator(table.begin(), table.end(), row);
			it != std::sregex_iterator(); ++it) {
		slugs.insert((*it)[1].str());
	}
	return slugs;
}

// Slugs of the arena rows in the "Solo Missions" category.
static std::set<std::string> soloArenaSlugs()
{
	const std::string source = readSource("port/src/arenadata_authored.c");
	const std::regex row(R"re(\{\s*"base:arena_[a-z0-9_]+"\s*,\s*"([a-z0-9_]+)"\s*,\s*"Solo Missions"\s*,)re");
	std::set<std::string> slugs;
	for (auto it = std::sregex_iterator(source.begin(), source.end(), row);
			it != std::sregex_iterator(); ++it) {
		slugs.insert((*it)[1].str());
	}
	return slugs;
}

}

TEST_CASE("the arena table alone does not cover the campaign",
		"[extraction][mission][campaign][t-extraction-001]")
{
	const std::set<std::string> campaign = campaignStageSlugs();
	const std::set<std::string> arenas = soloArenaSlugs();

	REQUIRE(campaign.size() == 21);
	REQUIRE(arenas.size() == 14);

	std::set<std::string> uncovered;
	for (const std::string &slug : campaign) {
		if (!arenas.count(slug)) {
			uncovered.insert(slug);
		}
	}
	const std::set<std::string> expected = {
		"extraction", "rescue", "escape", "mbr", "maiansos", "war", "duel",
	};
	REQUIRE(uncovered == expected);

	// No arena row names a mission the campaign does not have.
	for (const std::string &slug : arenas) {
		REQUIRE(campaign.count(slug) == 1);
	}
}

TEST_CASE("the mission extractor walks the campaign stage table after the arena table",
		"[extraction][mission][campaign][t-extraction-001]")
{
	const std::string meta = readSource("port/src/romextract_pdmeta.c");

	REQUIRE(meta.find("static s32 s_emitMissionArchive(const pdmeta_mission_source_t *a,") !=
		std::string::npos);
	REQUIRE(meta.find("static s32 s_soloStageNeedsOwnMission(s32 solo_index)") !=
		std::string::npos);
	REQUIRE(meta.find("const struct solostage *stage = &g_SoloStages[solo_index];") !=
		std::string::npos);
	REQUIRE(meta.find("\"solostages\", solo_index, -1,") != std::string::npos);
	REQUIRE(meta.find("a->slug, \"arenadata_authored\", arena_index, arena_index,") !=
		std::string::npos);

	// The arena loop runs first, so a stage that has an arena row is always
	// described by it; the campaign loop only fills what is left.
	const size_t arena_loop = meta.find("s32 r = s_emitMission(&g_ArenaData[i], missions_dir, scenarios_dir,");
	const size_t campaign_loop = meta.find("for (s32 i = 0; i < NUM_SOLOSTAGES; i++) {\n\t\tif (!s_soloStageNeedsOwnMission(i)) {");
	REQUIRE(arena_loop != std::string::npos);
	REQUIRE(campaign_loop != std::string::npos);
	REQUIRE(arena_loop < campaign_loop);

	// A mission is still named after its scenario, which the runtime relies
	// on to find the mission for a stage.
	REQUIRE(meta.find("snprintf(mission_id, sizeof(mission_id), \"base:mission_%s\", a->slug);") !=
		std::string::npos);
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	REQUIRE(runtime.find("snprintf(exact_id, sizeof(exact_id), \"base:mission_%s\",") !=
		std::string::npos);
}
