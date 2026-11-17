/*
 * tests/test_collision_obstacle_static.cpp -- static guards for B-1133: the
 * collision obstacle's geometry is recorded by every query whose hit is
 * classified by geometry, and reading it before any query has recorded it
 * cannot crash.
 *
 * The "Exam" queries in src/lib/collision.c record the obstacle's geometry;
 * the "Test" queries record only its prop. The PC capsule sweep
 * (src/lib/capsule.c) classifies a stage 1 hit by cdGetGeoFlags() right after
 * cdTestVolume, a Test query, so it read whatever geometry an earlier Exam
 * query left: NULL before the first one, which crashed a networked host 2.5 s
 * into a Hold the Briefcase match, and some other surface's flags after it.
 * collision.c needs the stage tile data and the prop tables, so these guards
 * pin the source shape (comments blanked out) instead of running it.
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
	bool str = false;

	for (size_t pos = brace; pos < code.size(); pos++) {
		const char ch = code[pos];

		if (str) {
			if (ch == '\\') {
				pos++;
			} else if (ch == '"') {
				str = false;
			}
			continue;
		}

		if (ch == '"') {
			str = true;
		} else if (ch == '{') {
			depth++;
		} else if (ch == '}') {
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

TEST_CASE("collision obstacle: cdTestVolume records the geometry it hit",
          "[collision][b1133]") {
	const std::string code = code_only(read_text_file("src/lib/collision.c"));
	const std::string body = function_body(code,
		"s32 cdTestVolume(struct coord *pos, f32 width, RoomNum *rooms, s32 types, bool checkvertical, f32 ymax, f32 ymin)\n{");

	const size_t prop = body.find("cdSetObstacleProp(collisions[0].prop);");
	const size_t geo = body.find("g_CdObstacleGeo = collisions[0].geo;");
	REQUIRE(prop != std::string::npos);
	REQUIRE(geo != std::string::npos);
	// After cdSetObstacleProp, which would otherwise be the last writer of the obstacle
	REQUIRE(prop < geo);
}

TEST_CASE("collision obstacle: the geometry readers answer when nothing is recorded",
          "[collision][b1133]") {
	const std::string code = code_only(read_text_file("src/lib/collision.c"));
	const std::string flags = function_body(code, "u32 cdGetGeoFlags(void)\n{");
	const std::string normal = function_body(code, "void cdGetObstacleNormal(struct coord *normal)\n{");
	const std::string missing = function_body(code, "static s32 cdObstacleGeoMissing(const char *what)\n{");

	REQUIRE(missing.find("g_CdObstacleGeo != NULL") != std::string::npos);

	const size_t flagsguard = flags.find("cdObstacleGeoMissing(\"flags\")");
	const size_t flagsread = flags.find("g_CdObstacleGeo->type");
	REQUIRE(flagsguard != std::string::npos);
	REQUIRE(flagsread != std::string::npos);
	REQUIRE(flagsguard < flagsread);

	const size_t normalguard = normal.find("cdObstacleGeoMissing(\"normal\")");
	const size_t normalread = normal.find("cdGetGeoNormal(g_CdObstacleGeo, normal);");
	REQUIRE(normalguard != std::string::npos);
	REQUIRE(normalread != std::string::npos);
	REQUIRE(normalguard < normalread);
}

TEST_CASE("collision obstacle: the capsule sweep classifies its stage 1 hit after cdTestVolume",
          "[collision][b1133]") {
	// If the capsule's stage 1 moves to another Test query, that query must
	// record the geometry too; this pin makes the move visible.
	const std::string capsule = code_only(read_text_file("src/lib/capsule.c"));
	const size_t test = capsule.find("cdTestVolume(&testpos, cast->radius, testrooms,");
	const size_t flags = capsule.find("cast->hitgeoflags = cdGetGeoFlags();");

	REQUIRE(test != std::string::npos);
	REQUIRE(flags != std::string::npos);
	REQUIRE(test < flags);
}
