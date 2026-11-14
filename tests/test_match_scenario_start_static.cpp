/*
 * tests/test_match_scenario_start_static.cpp
 *
 * Static guards for scenario selection rules and match-start scenario
 * initialization. matchsetup.c, main.c, the Room screen and mpStartMatch are
 * too coupled to link into pd-tests, so this pins the wiring contract:
 *
 *   1. mpStartMatch() runs scenarioInit() for every start path (offline Room,
 *      challenge, preset, listen authority, receiving client) before it asks
 *      for the stage. Without it King of the Hill, Capture the Case and Pop a
 *      Cap start with stale shared scenario state.
 *   2. matchConfigApplyScenarioRules() is the one selection-time owner: it
 *      derives the runtime mode from the primary scenario catalog ID and
 *      turns the user's Teams option on for team-only game modes. It never
 *      turns Teams off and never moves a human to another team.
 *   3. Every path that requests a start applies those rules before options
 *      and the mode are read: matchStart() before its preflight, boot
 *      automation before bots are seated and before the autostart request,
 *      the Room at selection time and before a networked start request.
 *
 * These are source-shape pins, not runtime proof.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string read_text_file_scenario_start(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file.good());

	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

static std::string function_block_scenario_start(const std::string &text, const std::string &name)
{
	const std::string needle = name + "(";
	const size_t signature = text.find(needle);
	REQUIRE(signature != std::string::npos);

	const size_t open = text.find('{', signature);
	REQUIRE(open != std::string::npos);

	size_t depth = 0;
	for (size_t i = open; i < text.size(); ++i) {
		if (text[i] == '{') {
			++depth;
		} else if (text[i] == '}') {
			--depth;
			if (depth == 0) {
				return text.substr(signature, i - signature + 1);
			}
		}
	}

	FAIL("unterminated function block");
	return std::string();
}

static size_t require_position(const std::string &text, const std::string &needle)
{
	const size_t position = text.find(needle);
	INFO("missing: " << needle);
	REQUIRE(position != std::string::npos);
	return position;
}

TEST_CASE("mpStartMatch initializes the scenario on every start path",
          "[scenario][match-start][static]")
{
	const std::string mplayer = read_text_file_scenario_start("src/game/mplayer/mplayer.c");
	const std::string start = function_block_scenario_start(mplayer, "void mpStartMatch");

	const std::string init_call = "\tscenarioInit();";
	const size_t network_branch = require_position(start, "immutable network launch");
	const size_t init = require_position(start, init_call);
	const size_t stage_request = require_position(start, "mainChangeToStage(stagenum);");

	/* Outside and after the offline-only convenience block and its network
	 * else-branch, so both prepared network launches and offline starts run
	 * it, and before the stage is requested. */
	REQUIRE(network_branch < init);
	REQUIRE(init < stage_request);

	/* Exactly one call: a second one inside the offline branch would hide a
	 * missing call on the network paths. */
	REQUIRE(start.find("scenarioInit();", init + init_call.size()) == std::string::npos);
}

TEST_CASE("scenario rules owner derives the mode and only ever enables teams",
          "[scenario][match-start][static]")
{
	const std::string matchsetup = read_text_file_scenario_start("port/src/net/matchsetup.c");
	const std::string rules = function_block_scenario_start(matchsetup,
		"s32 matchConfigApplyScenarioRules");

	/* The primary ID is the source; the integer mode is derived from it. */
	REQUIRE(rules.find("assetCatalogResolve(g_MatchConfig.scenario_id)") != std::string::npos);
	REQUIRE(rules.find("entry->type != ASSET_GAMEMODE") != std::string::npos);
	REQUIRE(rules.find("g_MatchConfig.scenario = (u8)entry->ext.gamemode.mode_id;") != std::string::npos);

	/* Team requirement comes from the public game mode entry and sets the
	 * user's option, so the offline plan and the network request both see it. */
	REQUIRE(rules.find("entry->ext.gamemode.team_based") != std::string::npos);
	REQUIRE(rules.find("matchConfigSetUserOption(MPOPTION_TEAMSENABLED, 1);") != std::string::npos);

	/* Never turns Teams off, never rewrites the whole option set, and never
	 * reseats a human. */
	REQUIRE(rules.find("matchConfigSetUserOption(MPOPTION_TEAMSENABLED, 0)") == std::string::npos);
	REQUIRE(rules.find("matchConfigReplaceUserOptions") == std::string::npos);
	REQUIRE(rules.find("SLOT_PLAYER") == std::string::npos);

	/* Bots are only redistributed when this call is what enabled Teams, and
	 * with the same balancer new bots use. */
	const size_t guard = require_position(rules, "if (teams_enabled_here) {");
	const size_t reseat = require_position(rules, "matchConfigChooseBotTeam(2)");
	REQUIRE(guard < reseat);
}

TEST_CASE("offline match start applies scenario rules before the preflight",
          "[scenario][match-start][static]")
{
	const std::string matchsetup = read_text_file_scenario_start("port/src/net/matchsetup.c");
	const std::string start = function_block_scenario_start(matchsetup, "s32 matchStart");

	const size_t rules = require_position(start, "matchConfigApplyScenarioRules(\"matchStart\")");
	const size_t preflight = require_position(start, "matchStartPrepare(&plan)");
	REQUIRE(rules < preflight);
}

TEST_CASE("boot automation applies scenario rules before seating bots and before autostart",
          "[scenario][match-start][static]")
{
	const std::string main_c = read_text_file_scenario_start("port/src/main.c");

	const std::string room = function_block_scenario_start(main_c, "static void bootApplyLaunchMpRoom");
	const size_t scenario_copy = require_position(room, "g_MatchConfig.scenario_id, g_BootLaunchMpScenario");
	const size_t rules = require_position(room, "matchConfigApplyScenarioRules(\"launch-mp-room\")");
	const size_t seat_bots = require_position(room, "matchConfigAddBot(");
	REQUIRE(scenario_copy < rules);
	REQUIRE(rules < seat_bots);

	const std::string autostart = function_block_scenario_start(main_c, "s32 bootHostAutostartTick");
	const size_t autostart_rules = require_position(autostart,
		"matchConfigApplyScenarioRules(\"host-autostart\")");
	/* The function also carries a local extern declaration of the request
	 * function, so pin the call itself. */
	const size_t request = require_position(autostart, "s32 rc = netLobbyRequestStartWithSims(");
	REQUIRE(autostart_rules < request);
}

TEST_CASE("Room applies scenario rules at selection and before a networked start",
          "[scenario][match-start][static]")
{
	const std::string room = read_text_file_scenario_start("port/fast3d/pdgui_menu_room.cpp");

	/* Selection: after the primary ID is written, and the settings are
	 * marked dirty afterwards so the leader's change is broadcast. */
	const size_t id_write = require_position(room,
		"strncpy(g_MatchConfig.scenario_id, sid,");
	const size_t select_rules = require_position(room,
		"matchConfigApplyScenarioRules(\"room-select\")");
	REQUIRE(id_write < select_rules);
	REQUIRE(room.find("s_RoomSettingsDirty = true;", select_rules) != std::string::npos);

	const std::string start = function_block_scenario_start(room, "static s32 roomGraphStartMatch");
	const size_t net_rules = require_position(start,
		"matchConfigApplyScenarioRules(\"room-net-start\")");
	const size_t request = require_position(start, "netLobbyRequestStartWithSims(");
	REQUIRE(net_rules < request);

	/* The offline branch goes through matchStart(), which applies the rules
	 * itself. */
	REQUIRE(start.find("matchStart()") != std::string::npos);
}

TEST_CASE("match end logs the result every peer is about to show",
          "[match][witness][static]")
{
	const std::string mplayer = read_text_file_scenario_start("src/game/mplayer/mplayer.c");
	const std::string result = function_block_scenario_start(mplayer, "static void mpLogMatchResult");
	const std::string end = function_block_scenario_start(mplayer, "void mpEndMatch");

	/* Scores go through the scenario's own score callback, so the same lines
	 * cover Combat and the objective scenarios. */
	REQUIRE(result.find("mpGetPlayerRankings(rankings)") != std::string::npos);
	REQUIRE(result.find("scenarioCalculatePlayerScore(") != std::string::npos);
	REQUIRE(result.find("\"MATCH.RESULT: scenario=%d teams=%d chrs=%d netmode=%d\"") != std::string::npos);
	REQUIRE(result.find("\"MATCH.RESULT.CHR: rank=%d chr=%u team=%u score=%d deaths=%d\"") != std::string::npos);
	REQUIRE(result.find("\"MATCH.RESULT.TEAM: rank=%d team=%u score=%d\"") != std::string::npos);

	/* Logged on host, client and offline alike, before the end screen line
	 * the existing match smokes already wait for. */
	const size_t log_result = require_position(end, "mpLogMatchResult();");
	const size_t endscreen = require_position(end, "\"MATCH: endscreen shown\"");
	REQUIRE(log_result < endscreen);
	const size_t client_gate = end.find("if (g_NetMode != NETMODE_CLIENT)");
	REQUIRE(client_gate != std::string::npos);
	REQUIRE(client_gate < log_result);
}

TEST_CASE("score limit endings log their reason before ending the stage",
          "[match][witness][static]")
{
	const std::string lv = read_text_file_scenario_start("src/game/lv.c");

	const size_t time_reason = require_position(lv, "\"MATCH: stage end reason=timelimit\"");
	const size_t score_flag = require_position(lv, "scorelimit_reached = true;");
	const size_t team_flag = require_position(lv, "teamscorelimit_reached = true;");
	const size_t client_reset = require_position(lv, "g_NumReasonsToEndMpMatch = 0;\n\t\t\t}\n\n\t\t\tif (g_NumReasonsToEndMpMatch > 0 && numdying == 0)");
	const size_t reason = require_position(lv,
		"\"MATCH: stage end reason=%s scorelimit=%d teamscorelimit=%d\"");
	const size_t end_stage = lv.find("mainEndStage();", reason);

	REQUIRE(time_reason < score_flag);
	REQUIRE(score_flag < team_flag);
	REQUIRE(team_flag < client_reset);
	REQUIRE(client_reset < reason);
	REQUIRE(end_stage != std::string::npos);
	/* Nothing between the witness and the stage end but the call itself. */
	REQUIRE(end_stage - reason < 400);
}

TEST_CASE("boot automation can seed real score limits for a launched match",
          "[match][witness][static]")
{
	const std::string main_c = read_text_file_scenario_start("port/src/main.c");
	const std::string room = function_block_scenario_start(main_c, "static void bootApplyLaunchMpRoom");

	REQUIRE(main_c.find("sysArgGetString(\"--match-scorelimit\")") != std::string::npos);
	REQUIRE(main_c.find("sysArgGetString(\"--match-teamscorelimit\")") != std::string::npos);
	REQUIRE(main_c.find("v >= 1 && v <= 100") != std::string::npos);
	REQUIRE(main_c.find("v >= 1 && v <= 400") != std::string::npos);

	/* Stored limits are "limit minus one"; mpApplyLimits adds it back. */
	const size_t init = require_position(room, "matchConfigInit();");
	const size_t score = require_position(room,
		"g_MatchConfig.scorelimit = (u8)(g_BootMatchScoreLimit - 1);");
	const size_t team = require_position(room,
		"g_MatchConfig.teamscorelimit = (u16)(g_BootMatchTeamScoreLimit - 1);");
	REQUIRE(init < score);
	REQUIRE(init < team);

	const std::string mplayer = read_text_file_scenario_start("src/game/mplayer/mplayer.c");
	const std::string limits = function_block_scenario_start(mplayer, "void mpApplyLimits");
	REQUIRE(limits.find("lvSetMpScoreLimit(g_MpSetup.scorelimit + 1);") != std::string::npos);
	REQUIRE(limits.find("lvSetMpTeamScoreLimit(mpCalculateTeamScoreLimit() + 1);") != std::string::npos);
}
