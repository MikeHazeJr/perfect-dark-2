// The stage intro (CORE, 2026-11-15): intro.json (pd2.scenario.intro.v1)
// carries the setup intro's commands other than the spawn pads, so a stage
// built from scenario source starts the player with the mission's weapons, ammo
// and outfit, and an arena has its cases and hills. A multiplayer scenario is
// built from the stage's multiplayer setup file, the one a match reads.
#include "catch.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "scenario_intro.h"
#include "modarchive.h"
#include "constants.h"
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

// A stand-in for the catalog: base weapon ids map to fixed numbers.
s32 testResolveWeapon(const char *catalog_id, s32 *out)
{
	static const std::map<std::string, s32> known = {
		{ "base:weapon_falcon2", WEAPON_FALCON2 },
		{ "base:weapon_falcon2_silencer", WEAPON_FALCON2_SILENCER },
		{ "base:weapon_unarmed", WEAPON_UNARMED },
		{ "base:weapon_cmp150", WEAPON_CMP150 },
	};
	const auto it = known.find(catalog_id ? catalog_id : "");
	if (it == known.end()) return 0;
	*out = it->second;
	return 1;
}

// For the sweep: any non-empty catalog id resolves.
s32 anyWeapon(const char *catalog_id, s32 *out)
{
	if (!catalog_id || !catalog_id[0]) return 0;
	*out = WEAPON_FALCON2;
	return 1;
}

struct Parsed {
	scenario_intro_t intro = {};
	std::string error;
	bool ok = false;
	explicit Parsed(const std::string &json, scenario_intro_weapon_resolver_t resolve = testResolveWeapon)
	{
		char err[256] = { 0 };
		ok = scenarioIntroParse(json.data(), json.size(), resolve, &intro, err, sizeof(err)) == 1;
		error = err;
	}
	~Parsed() { scenarioIntroFree(&intro); }
	std::vector<s32> words() const { return std::vector<s32>(intro.words, intro.words + intro.word_count); }
};

std::string introDoc(const std::string &rows)
{
	return "{ \"schema\": \"pd2.scenario.intro.v1\", \"note\": \"test\", \"rows\": [" + rows + "] }";
}

std::string refusal(const std::string &json)
{
	Parsed p(json);
	CHECK_FALSE(p.ok);
	CHECK(p.intro.word_count == 0);
	CHECK(p.intro.words == nullptr);
	return p.error;
}

} // namespace

TEST_CASE("intro.json rows become the game's intro commands in order", "[scenario][intro]")
{
	Parsed p(introDoc(
		"{ \"cmd\": \"outfit\", \"outfit\": \"snow\" },"
		"{ \"cmd\": \"weapon\", \"weapon_catalog_id\": \"base:weapon_falcon2\", \"player\": 0 },"
		"{ \"cmd\": \"ammo\", \"ammo_type\": 1, \"quantity\": 50, \"player\": 0 },"
		"{ \"cmd\": \"weapon\", \"weapon_catalog_id\": \"base:weapon_cmp150\","
		" \"left_weapon_catalog_id\": \"base:weapon_cmp150\", \"player\": 1 },"
		"{ \"cmd\": \"weapon\", \"weapon_selector\": \"none\", \"player\": 0 },"
		"{ \"cmd\": \"watch_time\", \"hours\": 23, \"minutes\": 59 },"
		"{ \"cmd\": \"case\", \"team\": 0, \"pad_ref\": \"pad_0154\" },"
		"{ \"cmd\": \"case_respawn\", \"team\": 3, \"pad_ref\": \"pad_0129\" },"
		"{ \"cmd\": \"hill\", \"pad_ref\": \"pad_0102\" },"
		"{ \"cmd\": \"legacy_3\", \"words\": [1, 2, 3, 4, 5, 6, -7] },"
		"{ \"cmd\": \"legacy_4\", \"words\": [9] },"
		"{ \"cmd\": \"legacy_6\", \"words\": [1, 2, 3, 4, 5, 6, 7, 8, 9] }"));
	INFO(p.error);
	REQUIRE(p.ok);
	const std::vector<s32> expected = {
		INTROCMD_OUTFIT, 9,
		INTROCMD_WEAPON, WEAPON_FALCON2, -1, 0,
		INTROCMD_AMMO, 1, 50, 0,
		INTROCMD_WEAPON, WEAPON_CMP150, WEAPON_CMP150, 1,
		INTROCMD_WEAPON, WEAPON_NONE, -1, 0,
		INTROCMD_WATCHTIME, 23, 59,
		INTROCMD_CASE, 0, 154,
		INTROCMD_CASERESPAWN, 3, 129,
		INTROCMD_HILL, 102,
		INTROCMD_3, 1, 2, 3, 4, 5, 6, -7,
		INTROCMD_4, 9,
		INTROCMD_6, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	};
	CHECK(p.words() == expected);
	CHECK(p.intro.row_count == 12);
	CHECK(p.intro.counts.outfits == 1);
	CHECK(p.intro.counts.weapons == 3);
	CHECK(p.intro.counts.ammo == 1);
	CHECK(p.intro.counts.watch_times == 1);
	CHECK(p.intro.counts.cases == 1);
	CHECK(p.intro.counts.case_respawns == 1);
	CHECK(p.intro.counts.hills == 1);
	CHECK(p.intro.counts.legacy == 3);

	// Each command is exactly as long as the game's readers step over.
	size_t at = 0;
	while (at < expected.size()) {
		const s32 words = scenarioIntroCommandWords(expected[at]);
		REQUIRE(words > 0);
		at += static_cast<size_t>(words);
	}
	CHECK(at == expected.size());

	Parsed empty(introDoc(""));
	CHECK(empty.ok);
	CHECK(empty.intro.word_count == 0);
	CHECK(empty.intro.row_count == 0);
}

TEST_CASE("an intro.json the reader cannot use is refused whole", "[scenario][intro]")
{
	CHECK(refusal("") .find("not one JSON object") != std::string::npos);
	CHECK(refusal("[1]").find("not one JSON object") != std::string::npos);
	CHECK(refusal("{ \"schema\": \"pd2.scenario.intro.v2\", \"rows\": [] }").find("must declare schema") != std::string::npos);
	CHECK(refusal("{ \"schema\": \"pd2.scenario.intro.v1\" }").find("rows must be a list") != std::string::npos);
	CHECK(refusal("{ \"schema\": \"pd2.scenario.intro.v1\", \"rows\": [], \"extra\": 1 }").find("unknown key 'extra'") != std::string::npos);
	CHECK(refusal("{ \"schema\": \"pd2.scenario.intro.v1\", \"rows\": [], \"note\": 3 }").find("note must be text") != std::string::npos);
	CHECK(refusal(introDoc("3")).find("must be an object") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"spawn\", \"pad_ref\": \"pad_0001\" }")).find("not an intro command") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"credit_offset\" }")).find("not an intro command") != std::string::npos);
	CHECK(refusal(introDoc("{ \"weapon_catalog_id\": \"base:weapon_falcon2\" }")).find(".cmd must be text") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"weapon\", \"weapon_catalog_id\": \"base:weapon_falcon2\" }")).find(".player must be") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"weapon\", \"weapon_catalog_id\": \"base:weapon_nope\", \"player\": 0 }")).find("not a known weapon") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"weapon\", \"player\": 0 }")).find("exactly one of") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"weapon\", \"weapon_selector\": \"none\", \"weapon_catalog_id\": \"base:weapon_falcon2\", \"player\": 0 }")).find("exactly one of") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"weapon\", \"weapon_selector\": \"fists\", \"player\": 0 }")).find("must be \"none\"") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"weapon\", \"weapon_catalog_id\": \"base:weapon_falcon2\", \"left_weapon_catalog_id\": \"\", \"player\": 0 }")).find("not a known weapon") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"weapon\", \"weapon_catalog_id\": \"base:weapon_falcon2\", \"player\": 0, \"ammo\": 3 }")).find("unknown key 'ammo'") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"ammo\", \"ammo_type\": 33, \"quantity\": 1, \"player\": 0 }")).find("ammo_type must be a whole number 0..32") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"ammo\", \"ammo_type\": 1.5, \"quantity\": 1, \"player\": 0 }")).find("ammo_type") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"ammo\", \"ammo_type\": 1, \"quantity\": -1, \"player\": 0 }")).find("quantity") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"outfit\", \"outfit\": \"tuxedo\" }")).find("not an outfit") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"watch_time\", \"hours\": 24, \"minutes\": 0 }")).find("hours") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"watch_time\", \"hours\": 1, \"minutes\": 60 }")).find("minutes") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"case\", \"team\": 4, \"pad_ref\": \"pad_0001\" }")).find("team must be a whole number 0..3") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"case\", \"team\": 0, \"pad_ref\": \"pad_x1\" }")).find("pad_NNNN") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"case_respawn\", \"team\": 0, \"pad_ref\": \"pad_40000\" }")).find("outside 0..32767") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"hill\", \"pad_ref\": \"\" }")).find("pad_NNNN") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"hill\", \"pad_ref\": \"pad_0001\", \"team\": 0 }")).find("unknown key 'team'") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"legacy_3\", \"words\": [1, 2] }")).find("exactly 7 numbers") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"legacy_4\", \"words\": [\"a\"] }")).find("whole 32-bit number") != std::string::npos);
	CHECK(refusal(introDoc("{ \"cmd\": \"legacy_6\", \"words\": 9 }")).find("must be a list") != std::string::npos);

	std::string many;
	for (int i = 0; i <= SCENARIO_INTRO_MAX_ROWS; i++) {
		many += std::string(i ? "," : "") + "{ \"cmd\": \"hill\", \"pad_ref\": \"pad_0001\" }";
	}
	CHECK(refusal(introDoc(many)).find("more than 1024 rows") != std::string::npos);

	// One bad row refuses the rows before it too.
	CHECK(refusal(introDoc("{ \"cmd\": \"hill\", \"pad_ref\": \"pad_0001\" }, { \"cmd\": \"nope\" }"))
		.find("rows[1]") != std::string::npos);
}

TEST_CASE("the extractor and the runtime carry the whole intro and the multiplayer setup", "[scenario][intro]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string reader = readSource("port/src/scenario_intro.cpp");

	// A multiplayer scenario reads the file a match reads.
	CHECK(extractor.find("const u16 setup_fileid = use_mp_setup ? st->mpsetupfileid : st->setupfileid;") != std::string::npos);
	CHECK(extractor.find("s_loadStageFilePreprocessed(setup_fileid,") != std::string::npos);
	CHECK(extractor.find("s_loadStageFilePreprocessed(st->setupfileid,") == std::string::npos);
	const std::string setup_load = readSource("src/game/setup.c");
	CHECK(setup_load.find("filenum = (u16)stage.mpsetupfileid;") != std::string::npos);

	// Every scenario gets the member, and an archive without it is not clean.
	CHECK(extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"intro.json\",") != std::string::npos);
	CHECK(extractor.find("s_existingArchiveEntryContains(check, \"intro.json\",\n\t\t\t\"pd2.scenario.intro.v1\")") != std::string::npos);
	CHECK(extractor.find("_intro_mpsetup_ending_hub_stars_dyntex_ammo_objrules_sparks_gas_mplifts_aiedges_simtwins_typedargs_catalogargs_assetargs_ruleevents_liftglass\"") != std::string::npos);
	CHECK(extractor.find("intro command %d is not carried by intro.json") != std::string::npos);

	// The runtime rebuilds the intro: spawns, then intro.json, then the end.
	CHECK(runtime.find("scenarioIntroParse(intro_text, text_size,\n\t\t\t\ts_setupResolveWeaponnum, &intro,") != std::string::npos);
	CHECK(runtime.find("s_setupBuildStageBlock(&table, &spawn_table, &intro, &ai_table,") != std::string::npos);
	const size_t spawns_at = runtime.find("cmd[0] = INTROCMD_SPAWN;");
	const size_t intro_at = runtime.find("memcpy(data + cursor, intro->words, (size_t)intro_words * sizeof(s32));");
	const size_t end_at = runtime.find("s32 intro_end = INTROCMD_END;");
	REQUIRE(spawns_at != std::string::npos);
	REQUIRE(intro_at != std::string::npos);
	REQUIRE(end_at != std::string::npos);
	CHECK(spawns_at < intro_at);
	CHECK(intro_at < end_at);
	CHECK(runtime.find("SCENARIO.SOURCE: invalid public intro.json for '%s': %s") != std::string::npos);

	// The extractor's command sizes are the reader's.
	const std::regex size_row("([0-9]+),\\s*/\\* (INTROCMD_[A-Z0-9_]+) \\*/");
	const std::map<std::string, s32> ids = {
		{ "INTROCMD_SPAWN", INTROCMD_SPAWN }, { "INTROCMD_WEAPON", INTROCMD_WEAPON },
		{ "INTROCMD_AMMO", INTROCMD_AMMO }, { "INTROCMD_3", INTROCMD_3 }, { "INTROCMD_4", INTROCMD_4 },
		{ "INTROCMD_OUTFIT", INTROCMD_OUTFIT }, { "INTROCMD_6", INTROCMD_6 },
		{ "INTROCMD_WATCHTIME", INTROCMD_WATCHTIME }, { "INTROCMD_CREDITOFFSET", INTROCMD_CREDITOFFSET },
		{ "INTROCMD_CASE", INTROCMD_CASE }, { "INTROCMD_CASERESPAWN", INTROCMD_CASERESPAWN },
		{ "INTROCMD_HILL", INTROCMD_HILL },
	};
	const size_t table_at = extractor.find("static s32 s_introCommandWordCount(s32 type)");
	REQUIRE(table_at != std::string::npos);
	const std::string table = extractor.substr(table_at, 900);
	int matched = 0;
	for (std::sregex_iterator it(table.begin(), table.end(), size_row), end; it != end; ++it) {
		const auto found = ids.find((*it)[2].str());
		if (found == ids.end()) continue;
		INFO((*it)[2].str());
		CHECK(scenarioIntroCommandWords(found->second) == std::stoi((*it)[1].str()));
		matched++;
	}
	CHECK(matched == 12);
	CHECK(reader.find("case INTROCMD_END: return 1;") != std::string::npos);
}

// Opt-in sweep (hidden from the default run): every .pdscenario under
// PD2_INTRO_ROOT, standalone or nested one level in an arena, must carry an
// intro.json the reader accepts. Catalog ids are not resolved here (any id is
// taken); the game resolves them. Read-only.
TEST_CASE("every scenario under PD2_INTRO_ROOT carries a readable intro.json", "[.][intro-sweep]")
{
	const char *root = std::getenv("PD2_INTRO_ROOT");
	if (!root || !root[0]) {
		WARN("PD2_INTRO_ROOT is not set; nothing to sweep");
		return;
	}
	size_t checked = 0;
	scenario_intro_counts_t total = {};
	std::string with_cases;
	auto check_member = [&](const std::string &label, const char *text, u32 size) {
		Parsed p(std::string(text, size), anyWeapon);
		INFO(label << ": " << p.error);
		CHECK(p.ok);
		checked++;
		total.weapons += p.intro.counts.weapons;
		total.ammo += p.intro.counts.ammo;
		total.outfits += p.intro.counts.outfits;
		total.watch_times += p.intro.counts.watch_times;
		total.cases += p.intro.counts.cases;
		total.case_respawns += p.intro.counts.case_respawns;
		total.hills += p.intro.counts.hills;
		total.legacy += p.intro.counts.legacy;
		if (p.intro.counts.cases) with_cases += " " + label.substr(label.find_last_of("/\\") + 1);
	};
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file()) continue;
		const std::string path = entry.path().string();
		const std::string ext = entry.path().extension().string();
		if (ext != ".pdscenario" && ext != ".pdarena") continue;
		mod_archive_t *archive = modArchiveOpen(path.c_str());
		REQUIRE(archive != nullptr);
		if (ext == ".pdscenario") {
			const s32 idx = modArchiveFindEntry(archive, "intro.json");
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
				char *text = static_cast<char *>(modArchiveExtractMemAlloc(nested, nested_size, "intro.json", &size));
				INFO(path << "::" << name);
				REQUIRE(text != nullptr);
				check_member(path + "::" + name, text, size);
				std::free(text);
				std::free(nested);
			}
		}
		modArchiveClose(archive);
	}
	WARN("read " << checked << " intro.json members under " << root << ": weapons=" << total.weapons
		<< " ammo=" << total.ammo << " outfits=" << total.outfits << " watch_times=" << total.watch_times
		<< " cases=" << total.cases << " case_respawns=" << total.case_respawns << " hills=" << total.hills
		<< " legacy=" << total.legacy << "; with cases:" << with_cases);
	CHECK(checked > 0);
}
