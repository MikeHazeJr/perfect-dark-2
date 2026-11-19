// SP-79: in port/src/scenario_source_runtime.c the AI graph twins answer 1
// for "handled" and 0 for "let native run", so the failure logger
// s_aiGraphRuntimeFailure returns 1. The requirement helpers use the other
// sense (1 = the requirement holds), so `return s_aiGraphRuntimeFailure(...)`
// inside one of them logged a refusal and then reported success. These pins
// keep that class closed: no requirement helper returns the logger, the two
// branches that keep going on purpose say why, and the B-1167 spots (twins
// that crashed where native does not) stay fixed.
#include "catch.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readSource(const std::string &path)
{
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

// Removes /* */ and // comments so a function named in a comment above a
// signature is not taken for the signature.
std::string withoutComments(const std::string &text)
{
	std::string out;
	for (size_t i = 0; i < text.size();) {
		if (text.compare(i, 2, "/*") == 0) {
			const size_t end = text.find("*/", i + 2);
			i = end == std::string::npos ? text.size() : end + 2;
		} else if (text.compare(i, 2, "//") == 0) {
			const size_t end = text.find('\n', i);
			i = end == std::string::npos ? text.size() : end;
		} else {
			out.push_back(text[i++]);
		}
	}
	return out;
}

// Function bodies in this file open with "{" and close with "}" in column 0.
// The signature runs from the previous function's closing "}" (or blank line)
// to the body's "{"; with comments removed, its first identifier followed by
// "(" is the function's name.
struct Enclosing {
	std::string name;
	size_t body_start = std::string::npos;
};

Enclosing enclosingFunction(const std::string &text, size_t at)
{
	Enclosing out;
	const size_t open = text.rfind("\n{\n", at);
	if (open == std::string::npos) return out;
	const size_t close = text.find("\n}\n", open);
	if (close == std::string::npos || close < at) return out;
	size_t sig = 0;
	const size_t blank = text.rfind("\n\n", open);
	const size_t previous = text.rfind("\n}\n", open);
	if (blank != std::string::npos) sig = blank + 2;
	if (previous != std::string::npos && previous + 3 > sig) sig = previous + 3;
	static const std::regex name("([A-Za-z_][A-Za-z_0-9]*)\\(");
	std::smatch m;
	const std::string signature = withoutComments(text.substr(sig, open - sig));
	if (std::regex_search(signature, m, name)) out.name = m[1];
	out.body_start = open;
	return out;
}

bool isRequirementHelper(const std::string &name)
{
	static const std::regex ready("^s_ai[A-Za-z]*GraphReady$");
	return name.rfind("s_aiGraphRequire", 0) == 0
		|| name.rfind("s_aiGraphResolve", 0) == 0
		|| std::regex_match(name, ready);
}

std::string functionBody(const std::string &text, const std::string &definition)
{
	const size_t start = text.find(definition);
	REQUIRE(start != std::string::npos);
	const size_t end = text.find("\n}\n", start);
	REQUIRE(end != std::string::npos);
	return text.substr(start, end + 2 - start);
}

} // namespace

TEST_CASE("SP-79 no graph requirement helper returns the failure logger",
		"[sp79][graph][static]")
{
	const std::string text = readSource("port/src/scenario_source_runtime.c");
	const std::string needle = "return s_aiGraphRuntimeFailure(";
	int returns = 0;
	int in_twins = 0;
	std::vector<std::string> offenders;
	for (size_t at = text.find(needle); at != std::string::npos;
			at = text.find(needle, at + 1)) {
		++returns;
		const Enclosing fn = enclosingFunction(text, at);
		INFO("failure return at offset " << at);
		REQUIRE_FALSE(fn.name.empty());
		if (isRequirementHelper(fn.name)) {
			offenders.push_back(fn.name);
		} else {
			++in_twins;
		}
	}
	INFO("helpers still returning the logger: " << offenders.size());
	for (const auto &name : offenders) UNSCOPED_INFO(name);
	REQUIRE(offenders.empty());
	// The twins keep using it to refuse; make sure the scan saw them.
	REQUIRE(returns > 200);
	REQUIRE(in_twins == returns);
}

TEST_CASE("SP-79 the two branches that keep going say why", "[sp79][graph][static]")
{
	const std::string text = readSource("port/src/scenario_source_runtime.c");
	const std::string pointer = functionBody(text,
		"static s32 s_aiGraphRequireRuntimeCharacterPointer(const char *action,\n");
	const size_t provenance = pointer.find("\"runtime character pointer is not source-derived\");");
	REQUIRE(provenance != std::string::npos);
	const std::string after = pointer.substr(provenance);
	REQUIRE(after.find("Co-op AI buddies and tiny-mode") < after.find("return 1;"));
	REQUIRE(pointer.find("\"missing runtime character\");\n\t\treturn 0;") != std::string::npos);

	const std::string spawn = functionBody(text,
		"static s32 s_aiGraphRegisterSourceSpawnedChr(const char *action,\n");
	const size_t full = spawn.find("\"source-spawned character provenance table is full\");");
	REQUIRE(full != std::string::npos);
	const std::string tail = spawn.substr(full);
	REQUIRE(tail.find("which would spawn it a second time") < tail.find("return 1;"));
}

TEST_CASE("SP-79 three-way and -1 node gates are read as refusals",
		"[sp79][graph][static]")
{
	const std::string text = readSource("port/src/scenario_source_runtime.c");
	const std::string target = functionBody(text,
		"static s32 s_aiGraphRequireTargetMovementNode(");
	REQUIRE(target.find("return -1;") != std::string::npos);
	// The -1 helpers' callers may not test them with `!`.
	static const std::regex misread(
		"if \\(!s_aiGraphRequire(ListControl|BasicMotion|Animation)Node\\(");
	REQUIRE_FALSE(std::regex_search(text, misread));
	size_t gates = 0;
	for (size_t at = text.find("\tif (node_gate <= 0) {\n\t\treturn node_gate < 0;\n");
			at != std::string::npos;
			at = text.find("\tif (node_gate <= 0) {\n\t\treturn node_gate < 0;\n", at + 1)) {
		++gates;
	}
	REQUIRE(gates == 10);
}

TEST_CASE("B-1167 graph twins act like native where native has no crash",
		"[sp79][b1167][graph][static]")
{
	const std::string text = readSource("port/src/scenario_source_runtime.c");
	for (const char *twin : {
			"s32 scenarioSourceAiGraphExecuteChrBeginOrEndTeleport(struct chrdata *basechr,\n",
			"s32 scenarioSourceAiGraphExecuteIfChrTeleportFullWhite(struct chrdata *basechr,\n"}) {
		INFO(twin);
		const std::string body = functionBody(text, twin);
		const size_t fallback = body.find("playernum = (s32)prevplayernum;");
		const size_t player = body.find("playernum = playermgrGetPlayerNumByProp(chr->prop);");
		const size_t slot = body.find("s_aiGraphRequireRuntimePlayerSlot(");
		REQUIRE(fallback != std::string::npos);
		REQUIRE(fallback < player);
		REQUIRE(player < slot);
	}

	const std::string death = functionBody(text,
		"s32 scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation(");
	REQUIRE(death.find("\tif (chr) {\n\t\tchr->specialdie = animation;\n\t}") != std::string::npos);

	const std::string hover = functionBody(text,
		"s32 scenarioSourceAiGraphExecuteIfHoverbotNextStep(\n");
	const size_t guard = hover.find("if (hovercar->path) {");
	REQUIRE(guard != std::string::npos);
	REQUIRE(guard < hover.find("s_aiGraphRequireRuntimePathPointer(\"if_hoverbot_next_step\""));
	REQUIRE(guard < hover.find("hovercar->path->len"));
	REQUIRE(hover.find("nextstep = hovercar->nextstep;") != std::string::npos);
}
