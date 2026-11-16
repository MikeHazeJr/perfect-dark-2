/*
 * tests/test_global_ailists_bank_static.cpp -- global AI lists slice 2, step 2
 * (context/designs/modding/global-ai-lists.md): the extractor writes the
 * binary's lists as base:ai_global, and the round trip also compiles the rows
 * of that extracted archive back to the binary's bytes.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

} // namespace

TEST_CASE("global AI lists: the extractor writes base:ai_global and the round trip reads it back",
          "[ai][globalailists][extract]")
{
	const std::string writer = readSource("port/src/romextract_pdai.c");
	const std::string header = readSource("port/include/romextract_pd.h");
	const std::string boot = readSource("port/src/main.c");
	const std::string check = readSource("port/src/scenario_global_ailists.c");
	const std::string walker = readSource("port/src/loader_walker_meta.c");

	// One archive in the data folder's ai/, the names the meta walker and the
	// public descriptor require (kind ai_lists, the rows member).
	REQUIRE(writer.find("#define PDAI_OUT_DIR \"ai\"") != std::string::npos);
	REQUIRE(writer.find("#define PDAI_ARCHIVE_NAME \"base_ai_global.pdai\"") != std::string::npos);
	REQUIRE(writer.find("#define PDAI_CATALOG_ID \"base:ai_global\"") != std::string::npos);
	REQUIRE(writer.find("#define PDAI_ROWS_MEMBER \"ai/global_ailists.json\"") != std::string::npos);
	REQUIRE(writer.find("\\\"pd_kind\\\": \\\"ai_lists\\\"") != std::string::npos);
	REQUIRE(writer.find("assetArchiveWriterInit(&writer, aw, \"ai_lists\", PDAI_CATALOG_ID)") != std::string::npos);
	REQUIRE(walker.find("\"ai/global_ailists.json\"") != std::string::npos);
	// The rows are the ones the slice 1 round trip proved exact.
	REQUIRE(writer.find("char *json = romExtractGlobalAiListsJson(&lists, &commands);") != std::string::npos);
	// SP-86: the kind names the writer's output and moves with it.
	REQUIRE(writer.find("#define PDAI_FAST_CACHE_KIND \"pdai_global_ailists_rows_v1\"") != std::string::npos);
	REQUIRE(writer.find("romExtractPdFastCacheKindMismatch(PDAI_FAST_CACHE_KIND, ai_dir)") != std::string::npos);
	REQUIRE(writer.find("romExtractPdFastCacheWrite(PDAI_FAST_CACHE_KIND, ai_dir, \".pdai\");") != std::string::npos);

	REQUIRE(header.find("s32 romExtractAllPdai(s32 force_rewrite);") != std::string::npos);
	REQUIRE(boot.find("bootRecordAssetPhase(\"pdai\", romExtractAllPdai(0));") != std::string::npos);

	// The check compiles the writer's rows and then the extracted archive's,
	// and a missing bank fails it.
	REQUIRE(check.find("bank = assetCatalogResolve(\"base:ai_global\");") != std::string::npos);
	REQUIRE(check.find("\"AI.GLOBAL: base:ai_global round trip %d/%d lists exact, %u bytes\"") != std::string::npos);
	REQUIRE(check.find("\"AI.GLOBAL: base:ai_global is not in the catalog as an AI list bank\"") != std::string::npos);
}

TEST_CASE("global AI lists: a stage built from scenario source runs the bank's lists",
          "[ai][globalailists][runtime]")
{
	const std::string bind = readSource("port/src/scenario_global_ailists.c");
	const std::string setup = readSource("src/game/setup.c");

	// The bank is compiled from its public rows once per change and must
	// carry exactly the game's list ids; the table keeps its ids and order.
	REQUIRE(bind.find("s32 scenarioGlobalAiListsBindBank(char *reason, u32 reason_size)") != std::string::npos);
	REQUIRE(bind.find("if (bank != s_Bank.entry || strcmp(bank->ext.ai_lists.lists_file, s_Bank.lists_file) != 0") !=
		std::string::npos);
	REQUIRE(bind.find("\"base:ai_global has list 0x%04x, which is not one of the game's global lists\"") !=
		std::string::npos);
	REQUIRE(bind.find("\"base:ai_global has no global list 0x%04x\"") != std::string::npos);
	REQUIRE(bind.find("g_GlobalAilists[i].list = s_Bank.lists[b].bytes;") != std::string::npos);
	REQUIRE(bind.find("g_GlobalAilists[i].id = ") == std::string::npos);

	// setupLoadFiles binds after sorting the global lists and refuses the
	// stage when the bank cannot supply them, never falling back.
	const size_t sort = setup.find("// Sort the global AI lists by ID asc");
	const size_t call = setup.find("if (!scenarioGlobalAiListsBindBank(reason, sizeof(reason))) {");
	REQUIRE(sort != std::string::npos);
	REQUIRE(call != std::string::npos);
	CHECK(sort < call);
	REQUIRE(setup.find("g_Vars.normmplayerisrunning, \"global ai lists\", -1, reason);") != std::string::npos);
}
