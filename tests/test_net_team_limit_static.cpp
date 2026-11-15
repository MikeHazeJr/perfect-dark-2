/*
 * tests/test_net_team_limit_static.cpp -- static guards for team play
 * (finding 8 of the multiplayer week plan).
 *
 * B-1132: Capture the Case has 4 bases and 4-entry team arrays, but the
 * port's in-match team switch offered all 8 teams and the host accepted
 * them. And Capture the Case teams start at their base: the original spawned
 * players on their base's pads, while the PC's match-start orchestrator
 * spread its team regions without looking at the bases. The menu, the host
 * check and the orchestrator are too coupled to the menu system, the client
 * table and the live spawn pool for pd-tests to run, so these guards pin
 * their shape in the source, matched with comments blanked out.
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

TEST_CASE("team limit: the in-match team switch offers only the scenario's teams",
          "[net][teams][b1132]") {
	const std::string ingame = code_only(read_text_file("src/game/mplayer/ingame.c"));
	const std::string handler = function_body(ingame,
		"MenuItemHandlerResult menuhandlerNetTeamSwitch(s32 operation, struct menuitem *item, union handlerdata *data)\n{");

	REQUIRE(handler.find("numteams = scenarioGetMaxTeams();") != std::string::npos);
	REQUIRE(handler.find("data->dropdown.value = numteams;") != std::string::npos);
	REQUIRE(handler.find("data->dropdown.value < numteams") != std::string::npos);
	REQUIRE(handler.find("data->dropdown.value = MAX_TEAMS;") == std::string::npos);
}

TEST_CASE("team limit: the host keeps a client's team when it asks past the scenario's teams",
          "[net][teams][b1132]") {
	const std::string netmsg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string sanitize = function_body(netmsg,
		"static u8 netmsgSanitizeClientTeam(struct netclient *srccl, u8 wireTeam)\n{");

	const size_t limit = sanitize.find("wireTeam >= scenarioGetMaxTeams()");
	REQUIRE(limit != std::string::npos);
	REQUIRE(sanitize.find("g_Vars.normmplayerisrunning", 0) < limit);
	REQUIRE(sanitize.find("return fallback;", limit) != std::string::npos);
}

TEST_CASE("team limit: scenario callback lookups check the scenario index",
          "[net][teams][b1132]") {
	const std::string scenarios = code_only(read_text_file("src/game/mplayer/scenarios.c"));
	const char *signatures[] = {
		"s32 scenarioGetMaxTeams(void)\n{",
		"bool scenarioIsRoomHighlighted(RoomNum room)\n{",
		"void scenarioHighlightRoom(RoomNum room, s32 *arg1, s32 *arg2, s32 *arg3)\n{",
	};

	for (const char *signature : signatures) {
		const std::string body = function_body(scenarios, signature);

		INFO("function " << signature);
		REQUIRE(body.find("scenarioIndexIsValid(g_MpSetup.scenario)") != std::string::npos);
	}
}

TEST_CASE("team start: the match-start orchestrator anchors Capture the Case teams on their bases",
          "[net][teams][finding8]") {
	const std::string orch = code_only(read_text_file("src/game/mplayer/mpspawn_orchestrate.c"));
	const std::string scenarios = code_only(read_text_file("src/game/mplayer/scenarios.c"));
	const std::string ctc = code_only(read_text_file("src/game/mplayer/scenarios/capturethecase.inc"));

	// The base anchors replace the spread anchors, before the Voronoi regions are drawn
	const size_t spread = orch.find("orch_compute_team_anchors(pool, &center, k_anchors, s_Anchors);");
	const size_t base = orch.find("scenarioGetTeamSpawnAnchor(b, &base)");
	const size_t voronoi = orch.find("s_VoronoiOwner[pi] = orch_voronoi_nearest_anchor(");
	REQUIRE(spread != std::string::npos);
	REQUIRE(base != std::string::npos);
	REQUIRE(voronoi != std::string::npos);
	REQUIRE(spread < base);
	REQUIRE(base < voronoi);

	const std::string dispatch = function_body(scenarios, "s32 scenarioGetTeamSpawnAnchor(s32 team, struct coord *out)\n{");
	REQUIRE(dispatch.find("MPSCENARIO_CAPTURETHECASE") != std::string::npos);
	REQUIRE(dispatch.find("ctcGetTeamSpawnAnchor(team, out)") != std::string::npos);

	const std::string anchor = function_body(ctc, "s32 ctcGetTeamSpawnAnchor(s32 team, struct coord *out)\n{");
	REQUIRE(anchor.find("g_ScenarioData.ctc.teamindexes[team]") != std::string::npos);
	REQUIRE(anchor.find("scenarioPadIsInRoom(") != std::string::npos);
}
