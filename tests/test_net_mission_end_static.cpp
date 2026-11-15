/*
 * tests/test_net_mission_end_static.cpp -- static guards for B-1193: the host
 * decides what follows a networked co-op or counter-op mission (Mike,
 * 2026-11-15). Each machine shows only its own player's results; the host's
 * stage end returns the partner to the lobby; the host's Next Mission, Retry
 * Mission and Return to Room act for both players; the other player waits,
 * and cannot end the mission itself. The flow needs a live session, so these
 * guards pin its shape in the source (comments blanked out) and
 * network_coop_mission_end_smoke runs it.
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

TEST_CASE("mission end: each machine pushes only its own player's results",
          "[net][coop][mission-end][b1193]") {
	const std::string pdmain = code_only(read_text_file("port/src/pdmain.c"));
	const std::string end = function_body(pdmain, "void mainEndStage(void)\n{");
	const char *const pushes[] = { "endscreenPushCoop();", "endscreenPushAnti();" };
	size_t from = 0;
	for (const char *push : pushes) {
		INFO(push);
		const size_t at = end.find(push, from);
		REQUIRE(at != std::string::npos);
		const size_t skip = end.rfind("g_Vars.players[i]->isremote", at);
		REQUIRE(skip != std::string::npos);
		REQUIRE(skip > from);
		REQUIRE(end.find("continue;", skip) < at);
		from = at;
	}

	// The two-player results are registered, so the lobby UI and the smoke
	// harness see them open
	const std::string pool = code_only(read_text_file("port/src/menupool.c"));
	for (const char *def : { "g_2PMissionEndscreenCompletedHMenuDialog", "g_2PMissionEndscreenFailedHMenuDialog",
			"g_2PMissionEndscreenCompletedVMenuDialog", "g_2PMissionEndscreenFailedVMenuDialog" }) {
		INFO(def);
		REQUIRE(pool.find(std::string("REG(&") + def + ",") != std::string::npos);
	}
}

TEST_CASE("mission end: the host's stage end returns the partner to the lobby",
          "[net][coop][mission-end][b1193]") {
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string commit = function_body(msg, "void netmsgSvcStageEndCommit(u8 room_id, u8 mode)\n{");
	const size_t coop = commit.find("if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {");
	const size_t mp = commit.find("} else {", coop);
	REQUIRE(coop != std::string::npos);
	REQUIRE(mp != std::string::npos);
	const std::string branch = commit.substr(coop, mp - coop);
	REQUIRE(branch.find("ncl->state = CLSTATE_LOBBY;") != std::string::npos);

	// The final objective statuses go out before the terminal packet, and the
	// counter-op client is kept for the host's Next Mission or Retry
	const std::string net = code_only(read_text_file("port/src/net/net.c"));
	const std::string flush = function_body(net, "static bool netServerFlushPendingStageEnd(const char *reason)\n{");
	const size_t objectives = flush.find("netServerSendFinalObjectives(");
	const size_t terminal = flush.find("netServerFlushCutsceneAuthorityPacket(");
	REQUIRE(objectives != std::string::npos);
	REQUIRE(terminal != std::string::npos);
	REQUIRE(objectives < terminal);
	const std::string commitnet = function_body(net, "static void netServerCommitPendingStageEnd(void)\n{");
	const size_t keep = commitnet.find("g_NetLastCounterOpClientId = g_NetCounterOpClientId;");
	const size_t clear = commitnet.find("g_NetCounterOpClientId = NET_NULL_CLIENT;");
	REQUIRE(keep != std::string::npos);
	REQUIRE(clear != std::string::npos);
	REQUIRE(keep < clear);
}

TEST_CASE("mission end: the host's choice acts for both players",
          "[net][coop][mission-end][b1193]") {
	const std::string bridge = code_only(read_text_file("port/fast3d/pdgui_bridge.c"));
	const std::string start = function_body(bridge, "void pdguiEndscreenNetMissionStart(s32 next)\n{");
	REQUIRE(start.find("netLobbyRequestStart(NETGAMEMODE_COOP, stage_id, difficulty)") != std::string::npos);
	REQUIRE(start.find("netLobbyRequestStartWithSims(NETGAMEMODE_ANTI, stage_id, difficulty,") != std::string::npos);
	REQUIRE(start.find("g_NetLastCounterOpClientId") != std::string::npos);
	REQUIRE(start.find("g_NetMode != NETMODE_SERVER") != std::string::npos);
	const std::string room = function_body(bridge, "void pdguiEndscreenNetMissionReturnToRoom(void)\n{");
	REQUIRE(room.find("netServerSendMissionReturn(room);") != std::string::npos);

	// The other player follows Return to Room through SVC_MISSION_RETURN
	REQUIRE(read_text_file("port/include/net/netmsg.h").find("#define SVC_MISSION_RETURN 0x7c") != std::string::npos);
	const std::string net = code_only(read_text_file("port/src/net/net.c"));
	REQUIRE(net.find("case SVC_MISSION_RETURN:   rc = netmsgSvcMissionReturnRead(&cl->in, cl); break;") != std::string::npos);
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string read = function_body(msg, "u32 netmsgSvcMissionReturnRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t consumed = read.find("netbufReadU8(src)");
	const size_t lobby = read.find("g_NetLocalClient->state = CLSTATE_LOBBY;");
	REQUIRE(consumed != std::string::npos);
	REQUIRE(lobby != std::string::npos);
	REQUIRE(consumed < lobby);
	REQUIRE(read.find("pdguiSetInRoom(1);", lobby) != std::string::npos);
	REQUIRE(read.find("netSendLobbyResync();", lobby) != std::string::npos);

	// The results screen: the host gets the three choices, the other player a
	// waiting note and Disconnect, and the choices go to the networked ops
	const std::string es = code_only(read_text_file("port/fast3d/pdgui_menu_endscreen.cpp"));
	const std::string render = function_body(es, "static void renderSoloEndscreen(struct menudialog *dialog, bool completed)\n{");
	REQUIRE(render.find("if (netMission && netHost) {") != std::string::npos);
	REQUIRE(render.find("pdguiActionBarButton(\"Next Mission\"") != std::string::npos);
	REQUIRE(render.find("pdguiActionBarButton(\"Retry Mission\", hasNext ? 0 : 1") != std::string::npos);
	REQUIRE(render.find("pdguiActionBarButton(\"Return to Room\"") != std::string::npos);
	REQUIRE(render.find("\"Waiting for the host to choose what comes next\"") != std::string::npos);
	REQUIRE(render.find("netMission ? endscreenGraphNetMissionNext : endscreenGraphNextMission") != std::string::npos);
	REQUIRE(render.find("netMission ? endscreenGraphNetMissionRetry : endscreenGraphRetryMission") != std::string::npos);
	REQUIRE(render.find("menuGraphFireSceneOp(MENU_TYPE_ENDSCREEN_SOLO, \"room\"") != std::string::npos);
	const std::string graph = code_only(read_text_file("port/src/menugraph.c"));
	REQUIRE(graph.find("EDGE_SCENE(\"room\", ACTION_MENU_ACCEPT, \"Return to Room\", SCENE_EVENT_STAGE_TEARDOWN)") != std::string::npos);
}

TEST_CASE("mission end: a client cannot end the mission itself",
          "[net][coop][mission-end][b1193]") {
	// Both players dead (co-op) or Bond dead (counter-op): the host ends it
	const std::string player = code_only(read_text_file("src/game/player.c"));
	const size_t coop = player.find("} else if (g_Vars.coopplayernum >= 0) {\n\t\t\t\tif (g_Vars.currentplayer == g_Vars.bond");
	REQUIRE(coop != std::string::npos);
	const size_t guard = player.find("if (g_NetMode != NETMODE_CLIENT) {", coop);
	const size_t end = player.find("mainEndStage();", coop);
	REQUIRE(guard != std::string::npos);
	REQUIRE(guard < end);
	const size_t anti = player.find("} else if (g_Vars.antiplayernum >= 0 && g_Vars.currentplayer == g_Vars.bond) {", coop);
	REQUIRE(anti != std::string::npos);
	REQUIRE(player.find("if (g_NetMode != NETMODE_CLIENT) {", anti) < player.find("mainEndStage();", anti));

	// End Game on a client leaves the session
	const std::string pause = code_only(read_text_file("port/fast3d/pdgui_menu_pausemenu.cpp"));
	const std::string endmission = function_body(pause, "static s32 pauseGraphEndMission(void *userdata)\n{");
	const size_t client = endmission.find("if (pauseIsNetMissionClient()) {");
	const size_t leave = endmission.find("netDisconnect();", client);
	const size_t local = endmission.find("mainEndStage();");
	REQUIRE(client != std::string::npos);
	REQUIRE(leave != std::string::npos);
	REQUIRE(leave < local);

	// The lobby UI and the rematch re-arm wait while these results are open
	const std::string lobby = code_only(read_text_file("port/fast3d/pdgui_lobby.cpp"));
	REQUIRE(lobby.find("|| menupoolIsActive(MENU_TYPE_ENDSCREEN_SOLO) != 0;") != std::string::npos);
	const std::string main_c = code_only(read_text_file("port/src/main.c"));
	REQUIRE(main_c.find("|| menupoolIsActive(MENU_TYPE_ENDSCREEN_SOLO) || !pdguiIsInRoom()) {") != std::string::npos);
}
