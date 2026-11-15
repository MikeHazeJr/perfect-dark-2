/*
 * tests/test_mp_chrnum_static.cpp -- static guards for B-1136: an mpchrnum
 * (players 0..MAX_PLAYERS-1, then simulants) resolves to its slot in
 * g_MpAllChrConfigPtrs with the port's player count, not the N64's 4.
 *
 * func0f18d0e8 did the lookup with the N64 boundary, so with MAX_PLAYERS at 8
 * every simulant looked up the config four slots further on. Pop a Cap and
 * Hacker Central score through it, so a simulant's survival, victim-kill and
 * download points never counted, and players 5 to 8 took a simulant's. The
 * lookup needs the live participant tables, so the guards pin its shape in
 * the source, matched with comments blanked out.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read_text_file(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	INFO("source file " << path);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

/* The text with comments replaced by spaces; strings are left alone. */
std::string code_only(const std::string &text)
{
	std::string out = text;
	bool line = false;
	bool block = false;
	bool str = false;

	for (size_t pos = 0; pos < text.size(); pos++) {
		const char ch = text[pos];
		const char next = pos + 1 < text.size() ? text[pos + 1] : '\0';

		if (line) {
			if (ch == '\n') {
				line = false;
			} else {
				out[pos] = ' ';
			}
		} else if (block) {
			if (ch == '*' && next == '/') {
				out[pos] = ' ';
				out[pos + 1] = ' ';
				pos++;
				block = false;
			} else if (ch != '\n') {
				out[pos] = ' ';
			}
		} else if (str) {
			if (ch == '\\') {
				pos++;
			} else if (ch == '"') {
				str = false;
			}
		} else if (ch == '/' && next == '/') {
			out[pos] = ' ';
			out[pos + 1] = ' ';
			pos++;
			line = true;
		} else if (ch == '/' && next == '*') {
			out[pos] = ' ';
			out[pos + 1] = ' ';
			pos++;
			block = true;
		} else if (ch == '"') {
			str = true;
		}
	}

	return out;
}

std::string function_body(const std::string &code, const std::string &signature)
{
	const size_t begin = code.find(signature);
	INFO("function " << signature);
	REQUIRE(begin != std::string::npos);

	const size_t brace = code.find('{', begin);
	REQUIRE(brace != std::string::npos);

	size_t depth = 0;
	for (size_t pos = brace; pos < code.size(); pos++) {
		if (code[pos] == '{') {
			depth++;
		} else if (code[pos] == '}') {
			depth--;
			if (depth == 0) {
				return code.substr(brace, pos - brace + 1);
			}
		}
	}

	FAIL("function body not closed: " << signature);
	return std::string();
}

} // namespace

TEST_CASE("mpchrnum lookup: players end and simulants begin at MAX_PLAYERS",
          "[mplayer][scenario][b1136]") {
	const std::string mplayer = code_only(read_text_file("src/game/mplayer/mplayer.c"));
	const std::string lookup = function_body(mplayer, "s32 func0f18d0e8(s32 arg0)\n{");
	const std::string inverse = function_body(mplayer, "s32 func0f18d074(s32 index)\n{");

	REQUIRE(lookup.find("if (arg0 < 0 || arg0 >= MAX_MPCHRS) {") != std::string::npos);
	REQUIRE(lookup.find("if (arg0 < MAX_PLAYERS) {") != std::string::npos);
	REQUIRE(lookup.find("&g_PlayerConfigsArray[arg0].base") != std::string::npos);
	REQUIRE(lookup.find("&g_BotConfigsArray[arg0 - MAX_PLAYERS].base") != std::string::npos);
	REQUIRE(lookup.find("arg0 < 4") == std::string::npos);
	REQUIRE(lookup.find("arg0 - 4") == std::string::npos);

	// The inverse and the MPCHR macro use the same boundary
	REQUIRE(inverse.find("return i + MAX_PLAYERS;") != std::string::npos);
	const std::string constants = read_text_file("src/include/constants.h");
	REQUIRE(constants.find("#define MPCHR(index)        ((index) < MAX_PLAYERS ? "
		"&g_PlayerConfigsArray[index].base : &g_BotConfigsArray[(index) - MAX_PLAYERS].base)")
		!= std::string::npos);
}

TEST_CASE("mpchrnum lookup: the scenario scores pass an mpchrnum to it",
          "[mplayer][scenario][b1136]") {
	struct ScoreSite {
		const char *path;
		const char *signature;
		const char *points;
	};
	const ScoreSite sites[] = {
		{"src/game/mplayer/scenarios/popacap.inc",
			"void pacCalculatePlayerScore(struct mpchrconfig *mpchr, s32 mpchrnum, s32 *score, s32 *arg3)\n{",
			"g_ScenarioData.pac.survivalcounts[index]"},
		{"src/game/mplayer/scenarios/hackthatmac.inc",
			"void htmCalculatePlayerScore(struct mpchrconfig *mpchr, s32 mpchrnum, s32 *score, s32 *deaths)\n{",
			"g_ScenarioData.htm.numpoints[index]"},
	};

	for (const ScoreSite &site : sites) {
		INFO("score " << site.signature);
		const std::string body = function_body(code_only(read_text_file(site.path)), site.signature);
		const size_t lookup = body.find("index = func0f18d0e8(mpchrnum);");
		const size_t guard = body.find("if (index >= 0) {", lookup);
		const size_t points = body.find(site.points, guard);
		REQUIRE(lookup != std::string::npos);
		REQUIRE(guard != std::string::npos);
		REQUIRE(points != std::string::npos);
	}
}
