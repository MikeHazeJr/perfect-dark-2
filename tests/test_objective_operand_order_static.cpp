// B-1111: objective tags and stage flags must reach public scenario source,
// and come back from it, in the same host order the game reads them in.
//
// The setup loader converts the second word of a destroy, collect, throw,
// complete-flags or fail-flags command to host order when the stage loads.
// The extractor used to swap that word again, so the public files said
// tag_100663296 for tag 6 and stage_flag_0x00200000 for flag 0x00002000. The
// setup rebuild swapped its copy back, which hid the wrong numbers, and the
// mission graph path read the objectives member as written, so graph-backed
// missions tested the wrong flags and looked up tags that do not exist.
#include "catch.hpp"

#include <fstream>
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

}

TEST_CASE("objective operands keep host order from stage load to public source and back",
		"[objectives][extraction][scenario][b-1111]")
{
	const std::string loader = readSource("port/src/preprocess/filesetup.c");
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string objectives = readSource("src/game/objectives.c");

	// The loader is where the one and only swap happens.
	REQUIRE(loader.find("PD_CONV_VAL(dstobj->cmd1, srcobj->cmd1);") != std::string::npos);

	// The game reads the word as it is.
	REQUIRE(objectives.find("objFindByTagId(cmd[1])") != std::string::npos);
	REQUIRE(objectives.find("chrHasStageFlag(NULL, cmd[1])") != std::string::npos);

	// So does the extractor, for both members it writes.
	REQUIRE(extractor.find("PD_BE32(words[1])") == std::string::npos);
	REQUIRE(extractor.find("PD_BE32(((const u32 *)ptr)[1])") == std::string::npos);
	REQUIRE(extractor.find("u32 tag_id = words[1];") != std::string::npos);
	REQUIRE(extractor.find("u32 flag_mask = words[1];") != std::string::npos);
	REQUIRE(extractor.find("u32 tag_id = ((const u32 *)ptr)[1];") != std::string::npos);
	REQUIRE(extractor.find("u32 flag_mask = ((const u32 *)ptr)[1];") != std::string::npos);

	// And the setup rebuild writes what the public source says.
	REQUIRE(runtime.find("be = PD_BE32((u32)ivalue);") == std::string::npos);
	REQUIRE(runtime.find("return s_setupWriteU32(record, sizeof(u32), (u32)ivalue);") !=
		std::string::npos);
	REQUIRE(runtime.find("return s_setupWriteU32(record, sizeof(u32), uvalue);") !=
		std::string::npos);

	// Installs extracted before the correction must be re-extracted.
	REQUIRE(extractor.find("_iafix_b945_objoperands_b1111\"") != std::string::npos);
}

TEST_CASE("the graph objective path refuses a criterion that disagrees with the setup command",
		"[objectives][scenario][b-1111]")
{
	const std::string objectives = readSource("src/game/objectives.c");

	REQUIRE(objectives.find("static bool objectiveGraphOperandMatchesCommand(") !=
		std::string::npos);
	REQUIRE(objectives.find("return (u32)operand->tag_id == cmd[1];") != std::string::npos);
	REQUIRE(objectives.find("return operand->stage_flag_mask == cmd[1];") != std::string::npos);

	// The check sits between fetching the operand and evaluating it.
	const size_t fetch = objectives.find("\"evaluate\", \"missing criteria graph operand\"");
	const size_t check = objectives.find("if (!objectiveGraphOperandMatchesCommand(&operand, cmd)) {");
	const size_t evaluate = objectives.find("objectiveEvaluateGraphRequirement(&operand));");
	REQUIRE(fetch != std::string::npos);
	REQUIRE(check != std::string::npos);
	REQUIRE(evaluate != std::string::npos);
	REQUIRE(fetch < check);
	REQUIRE(check < evaluate);
	REQUIRE(objectives.find("criteria tag or stage flag differs from the setup command") !=
		std::string::npos);
}

TEST_CASE("base scenario collision source names no material library",
		"[extraction][scenario][b-1145]")
{
	// scenario.mtl is a forbidden scenario member, so a line that names it can
	// never resolve and fails release validation of every base arena.
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	REQUIRE(extractor.find("\"mtllib scenario.mtl\\n\"") == std::string::npos);
	REQUIRE(extractor.find("\"usemtl collision\\n\"") != std::string::npos);
}
