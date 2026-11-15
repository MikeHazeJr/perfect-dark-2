/*
 * tests/test_mp_chrnum_static.cpp -- static guards for multiplayer
 * participants. B-1136: an mpchrnum (players 0..MAX_PLAYERS-1, then
 * simulants) resolves to its slot in g_MpAllChrConfigPtrs with the port's
 * player count, not the N64's 4. Finding 10: a challenge started outside the
 * Room's match start still has the local player as a participant.
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

TEST_CASE("challenge launch: the local player joins before the simulants",
          "[mplayer][challenges][finding10]") {
	const std::string body = function_body(code_only(read_text_file("port/src/net/matchsetup.c")),
		"s32 matchStartFromChallenge(s32 slot)\n{");

	const size_t apply = body.find("challengeSetCurrentBySlot(slot);");
	const size_t clear = body.find("mpClearAllParticipants();", apply);
	const size_t player = body.find("mpAddParticipantAt(0, PARTICIPANT_LOCAL, 0, 0, 0);", clear);
	const size_t simulants = body.find("challengePerformSanityChecks();", player);
	const size_t start = body.find("mpStartMatch();", simulants);
	REQUIRE(apply != std::string::npos);
	REQUIRE(clear != std::string::npos);
	REQUIRE(player != std::string::npos);
	REQUIRE(simulants != std::string::npos);
	REQUIRE(start != std::string::npos);
}

TEST_CASE("team spawns: every team gets its own sector, numbered among the active teams",
          "[mplayer][spawn][sp82]") {
	const std::string orch = code_only(read_text_file("src/game/mplayer/mpspawn_orchestrate.c"));
	REQUIRE(orch.find("#define ORCH_MAX_SECTORS MAX_TEAMS") != std::string::npos);
	REQUIRE(orch.find("static struct coord s_Anchors[ORCH_MAX_SECTORS];") != std::string::npos);
	REQUIRE(orch.find("b < 4") == std::string::npos);
	REQUIRE(orch.find("i < 4") == std::string::npos);
	REQUIRE(orch.find("num_sectors > 4") == std::string::npos);

	const std::string player = code_only(read_text_file("src/game/player.c"));
	const std::string resolve = function_body(player,
		"static void playerMpResolveTeamSpawnParams(struct prop *selfprop, s32 *team, s32 *num_teams)\n{");
	REQUIRE(resolve.find("i < 4") == std::string::npos);
	REQUIRE(resolve.find("for (i = 0; i < MAX_TEAMS; i++) {") != std::string::npos);
	// The sector is the team's place among the active teams, not its number
	const size_t skip = resolve.find("if (!(active_teams & (1u << i))) {");
	const size_t take = resolve.find("*team = sector;", skip);
	const size_t next = resolve.find("sector++;", take);
	REQUIRE(skip != std::string::npos);
	REQUIRE(take != std::string::npos);
	REQUIRE(next != std::string::npos);
	REQUIRE(resolve.find("*team = i;") == std::string::npos);
}

TEST_CASE("simulant render smoke hooks: face with the engine's yaw and place in sight",
          "[mplayer][smoke][finding3]") {
	const std::string main_c = code_only(read_text_file("port/src/main.c"));
	const std::string face = function_body(main_c, "s32 bootDebugFaceNearestBotTick(void)\n{");
	const std::string place = function_body(main_c,
		"s32 bootDebugPlaceBotNearPlayerPreRenderTick(void)\n{");

	// chrGetAngleToPos: a player faces (360 - vv_theta) degrees in atan2f(dx, dz) terms
	REQUIRE(face.find("360.0f - atan2f(dx, dz) * (180.0f / 3.14159265f)") != std::string::npos);
	REQUIRE(face.find("pl->vv_theta = theta;") != std::string::npos);
	REQUIRE(face.find("g_BootDebugFaceBotProp = best->prop;") != std::string::npos);
	const std::string chraction = code_only(read_text_file("src/game/chraction.c"));
	const std::string angle = function_body(chraction, "f32 chrGetAngleToPos(struct chrdata *chr, struct coord *pos)\n{");
	REQUIRE(angle.find("(360 - g_Vars.players[playernum]->vv_theta) * (M_BADTAU / 360)") != std::string::npos);
	REQUIRE(angle.find("atan2f(pos->x - prop->pos.x, pos->z - prop->pos.z)") != std::string::npos);

	// Visible placement: a spot is taken only with a clear line of sight, and the
	// hold keeps the first direction so a facing peer does not move the bot
	const size_t los = place.find("cdTestLos06(&losfrom, playerprop->rooms, &losto,");
	const size_t accept = place.find("&& losclear) {", los);
	REQUIRE(los != std::string::npos);
	REQUIRE(accept != std::string::npos);
	REQUIRE(place.find("forward_x = s_VisibleForwardX;") != std::string::npos);
	REQUIRE(place.find("? (s32)(sizeof(visibleturns) / sizeof(visibleturns[0])) : 1;") != std::string::npos);

	// The render audit follows the faced simulant as well as the placed one
	const std::string audit = function_body(main_c,
		"s32 bootDebugPlaceBotNearPlayerIsAuditProp(const struct prop *prop)\n{");
	REQUIRE(audit.find("prop == g_BootDebugFaceBotProp") != std::string::npos);
}
