/*
 * tests/test_host_autostart_rematch_static.cpp -- static guards for
 * --host-autostart-matches (finding 4 of the multiplayer week plan: a second
 * match in the same room on a listen host).
 *
 * The re-arm lives in port/src/main.c's boot ticks, which need the net layer,
 * the room table and the menu pool, so these guards pin its shape in the
 * source (comments blanked out) and mp_combat_net_rematch_smoke runs it.
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

TEST_CASE("host autostart rematch: the start re-arms only once the match is over and the host is back in the room",
          "[net][rematch][finding4]") {
	const std::string main_c = code_only(read_text_file("port/src/main.c"));
	const std::string rearm = function_body(main_c, "static void bootHostAutostartRearmTick(void)\n{");

	// The match room is released and the end screen is gone, the host is in the room UI
	REQUIRE(rearm.find("g_NetMatchRoomId != 0xFF") != std::string::npos);
	REQUIRE(rearm.find("menupoolIsActive(MENU_TYPE_ENDSCREEN_MP)") != std::string::npos);
	REQUIRE(rearm.find("!pdguiIsInRoom()") != std::string::npos);
	// The room accepts a start again
	REQUIRE(rearm.find("room->state != ROOM_STATE_LOBBY") != std::string::npos);
	// The room is reused: phase 1, not room creation
	REQUIRE(rearm.find("g_BootHostAutostartPhase = 1;") != std::string::npos);
	REQUIRE(rearm.find("g_BootHostAutostartFired = 0;") != std::string::npos);
}

TEST_CASE("host autostart rematch: one match stays the default and every attempt counts",
          "[net][rematch][finding4]") {
	const std::string main_c = code_only(read_text_file("port/src/main.c"));
	const std::string tick = function_body(main_c, "s32 bootHostAutostartTick(void)\n{");

	REQUIRE(main_c.find("static s32         g_BootHostAutostartMatches = 1;") != std::string::npos);
	// Without --host-autostart-matches above 1 the re-arm never runs
	REQUIRE(tick.find("g_BootHostAutostartMatches > 1") != std::string::npos);
	// The counter goes up where the start is committed, before any early return
	const size_t fired = tick.find("g_BootHostAutostartFired = 1;");
	const size_t counted = tick.find("g_BootHostAutostartStarted++;");
	const size_t first_return = tick.find("return 1;", fired);
	REQUIRE(fired != std::string::npos);
	REQUIRE(counted != std::string::npos);
	REQUIRE(fired < counted);
	REQUIRE(counted < first_return);
}

TEST_CASE("host autostart rematch: a listen host's end screen has dialogs only for its own players",
          "[net][rematch][finding4]") {
	const std::string menutick = code_only(read_text_file("src/game/menutick.c"));

	// The "match is ending" branch skips remote participants for the
	// multiplayer end screen, and still counts them as player numbers
	const size_t ending = menutick.find("g_MenuData.prevmenuroot == -6");
	REQUIRE(ending != std::string::npos);
	const size_t remote = menutick.find("mpGetParticipant(i)->type == PARTICIPANT_REMOTE", ending);
	const size_t push = menutick.find("mpPushEndscreenDialog(playernum, i);", remote);
	const size_t next = menutick.find("playernum++;", push);
	REQUIRE(remote != std::string::npos);
	REQUIRE(push != std::string::npos);
	REQUIRE(next != std::string::npos);
	const size_t later = menutick.find("g_MenuData.prevmenuroot == -7", ending);
	REQUIRE(later != std::string::npos);
	REQUIRE(next < later);
}
