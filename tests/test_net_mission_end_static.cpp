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

TEST_CASE("mission end: smokes can end a mission on the host and see its results",
          "[net][coop][mission-end][b1193][smoke]") {
	// --debug-end-mission ends a mission once, never on a client, through the
	// level script's own end
	const std::string main_c = code_only(read_text_file("port/src/main.c"));
	REQUIRE(main_c.find("sysArgGetString(\"--debug-end-mission\")") != std::string::npos);
	const std::string tick = function_body(main_c, "s32 bootDebugEndMissionTick(void)\n{");
	const size_t client = tick.find("g_NetMode == NETMODE_CLIENT");
	const size_t once = tick.find("g_BootDebugEndMissionFired = 1;");
	const size_t end = tick.find("func0000e990();");
	REQUIRE(client != std::string::npos);
	REQUIRE(once != std::string::npos);
	REQUIRE(end != std::string::npos);
	REQUIRE(client < once);
	REQUIRE(once < end);
	const std::string pdmain = code_only(read_text_file("port/src/pdmain.c"));
	REQUIRE(pdmain.find("(void)bootDebugEndMissionTick();") != std::string::npos);

	// endscreen_visible sees a mission's results (the solo type) too
	const std::string harness = code_only(read_text_file("port/src/smoke_harness.c"));
	REQUIRE(harness.find("facts.endscreen_menu_active = menupoolIsActive(MENU_TYPE_ENDSCREEN_MP)\n\t\t|| menupoolIsActive(MENU_TYPE_ENDSCREEN_SOLO);") != std::string::npos);
}

TEST_CASE("mission end: each machine's results show both players with the host's numbers",
          "[net][coop][mission-end][b1193]") {
	// Mike, 2026-11-15: in co-op you want to see stats from all players
	REQUIRE(read_text_file("port/include/net/netmsg.h").find("#define SVC_MISSION_STATS  0x7d") != std::string::npos);
	const std::string net = code_only(read_text_file("port/src/net/net.c"));
	REQUIRE(net.find("case SVC_MISSION_STATS:    rc = netmsgSvcMissionStatsRead(&cl->in, cl); break;") != std::string::npos);
	const std::string final_state = function_body(net, "static bool netServerSendFinalObjectives(u8 room_id, const char *reason)\n{");
	REQUIRE(final_state.find("netmsgSvcMissionStatsWrite(&wire);") != std::string::npos);

	// The writer's count and rows use the same test; the reader reads every
	// row before it applies any (SP-81)
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string write = function_body(msg, "u32 netmsgSvcMissionStatsWrite(struct netbuf *dst)\n{");
	REQUIRE(write.find("g_NetClients[i].state == CLSTATE_GAME\n\t\t\t\t&& netMissionStatsPlayerNum(&g_NetClients[i]) >= 0") != std::string::npos);
	REQUIRE(write.find("cl->state == CLSTATE_GAME ? netMissionStatsPlayerNum(cl) : -1") != std::string::npos);
	const std::string read = function_body(msg, "u32 netmsgSvcMissionStatsRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t rows = read.find("shots[i][r] = netbufReadU16(src);");
	const size_t client = read.find("if (g_NetMode != NETMODE_CLIENT) {");
	const size_t apply = read.find("g_Vars.playerstats[playernum].killcount = kills[i];");
	REQUIRE(rows != std::string::npos);
	REQUIRE(client != std::string::npos);
	REQUIRE(apply != std::string::npos);
	REQUIRE(rows < client);
	REQUIRE(client < apply);
	REQUIRE(read.find("count > MAX_PLAYERS") < rows);

	// The results: own player, then the partner, both by the per-machine flag
	const std::string bridge = code_only(read_text_file("port/fast3d/pdgui_bridge.c"));
	const std::string stats = function_body(bridge, "s32 pdguiEndscreenNetMissionStats(s32 which, s32 *kills, s32 *shots, char *name, s32 namelen)\n{");
	REQUIRE(stats.find("(player->isremote != 0) != (which != 0)") != std::string::npos);
	const std::string es = code_only(read_text_file("port/fast3d/pdgui_menu_endscreen.cpp"));
	const std::string render = function_body(es, "static void renderSoloEndscreen(struct menudialog *dialog, bool completed)\n{");
	REQUIRE(render.find("pdguiEndscreenNetMissionStats(0, &kills, ownShots, NULL, 0)") != std::string::npos);
	REQUIRE(render.find("pdguiEndscreenNetMissionStats(1, &partnerKills, partnerShots,") != std::string::npos);
	REQUIRE(render.find("\"PARTNER: %s\"") != std::string::npos);
}

TEST_CASE("co-op chr spawns: a chr the host's AI makes reaches the client with the host's ids",
          "[net][coop][b1196]") {
	// B-1196: an AI clone or spawn exists only on the host under a dynamic
	// sync id (SP-85); the host announces it before its moves and the client
	// makes a puppet with the same ids
	REQUIRE(read_text_file("port/include/net/netmsg.h").find("#define SVC_NPC_SPAWN      0x7e") != std::string::npos);

	const std::string net = code_only(read_text_file("port/src/net/net.c"));
	REQUIRE(net.find("case SVC_NPC_SPAWN: rc = netmsgSvcNpcSpawnRead(&cl->in, cl); break;") != std::string::npos);
	const std::string end = function_body(net, "void netEndFrame(void)\n{");
	const size_t announce = end.find("netServerNpcSpawnAnnounce(&g_ChrSlots[i]);");
	const size_t move = end.find("netmsgSvcNpcMoveWrite(&g_NetMsg, chr);");
	const size_t state = end.find("netmsgSvcNpcStateWrite(&g_NetMsgRel, chr);");
	REQUIRE(announce != std::string::npos);
	REQUIRE(move != std::string::npos);
	REQUIRE(state != std::string::npos);
	REQUIRE(announce < move);
	REQUIRE(end.rfind("netServerNpcSpawnSettled(chr)", move) > announce);
	REQUIRE(end.rfind("netServerNpcSpawnSettled(chr)", state) > move);
	const std::string settled = function_body(net, "static bool netServerNpcSpawnSettled(const struct chrdata *chr)\n{");
	REQUIRE(settled.find("syncid < g_NetFirstDynamicSyncId") != std::string::npos);
	REQUIRE(settled.find("NET_NPC_SPAWN_SETTLE_TICKS") != std::string::npos);

	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string read = function_body(msg, "u32 netmsgSvcNpcSpawnRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t mode = read.find("g_NetGameMode != NETGAMEMODE_COOP && g_NetGameMode != NETGAMEMODE_ANTI");
	const size_t duplicate = read.find("if (syncid == 0 || netSyncIdLookup(syncid)) {");
	const size_t make = read.find("prop = chrSpawnReplicated(bodynum, headnum, &pos, rooms, angle, noblood != 0);");
	const size_t bind = read.find("netSyncIdMapSet(syncid, prop);");
	REQUIRE(mode != std::string::npos);
	REQUIRE(duplicate != std::string::npos);
	REQUIRE(make != std::string::npos);
	REQUIRE(bind != std::string::npos);
	REQUIRE(mode < make);
	REQUIRE(duplicate < make);
	REQUIRE(make < bind);
	REQUIRE(read.find("chrSetChrnum(chr, chrnum);") != std::string::npos);
	REQUIRE(read.find("netSyncIdMapSet(wsyncid[i], wprops[i]);") != std::string::npos);
	REQUIRE(read.find("return 1") == std::string::npos);

	// The client takes the host's place as it is
	const std::string action = code_only(read_text_file("src/game/chraction.c"));
	const std::string replicated = function_body(action,
		"struct prop *chrSpawnReplicated(s32 bodynum, s32 headnum, struct coord *pos, RoomNum *rooms, f32 angle, bool noblood)\n{");
	REQUIRE(replicated.find("chrAllocate(model, pos, rooms2, angle, ailistFindById(GAILIST_IDLE))") != std::string::npos);
	REQUIRE(replicated.find("chrAdjustPosForSpawn") == std::string::npos);

	// A smoke can make a clone on the host
	const std::string main_c = code_only(read_text_file("port/src/main.c"));
	REQUIRE(main_c.find("sysArgGetString(\"--debug-clone-guard\")") != std::string::npos);
	const std::string tick = function_body(main_c, "s32 bootDebugCloneGuardTick(void)\n{");
	REQUIRE(tick.find("if (g_NetMode == NETMODE_CLIENT") != std::string::npos);
	REQUIRE(tick.find("chrSpawnAtChr(chr, chr->bodynum, -1, chr->chrnum,") != std::string::npos);
	REQUIRE(tick.find("ailistFindById(GAILIST_IDLE)") != std::string::npos);
	REQUIRE(code_only(read_text_file("port/src/pdmain.c")).find("(void)bootDebugCloneGuardTick();") != std::string::npos);
}

TEST_CASE("mission end: a disconnect ends a networked mission without results",
          "[net][coop][b1198]") {
	// B-1198: netDisconnect clears g_NetMode before it ends the stage, so the
	// per-machine results skip no longer applied and both players' results
	// flashed up on the way to CI
	const std::string net = code_only(read_text_file("port/src/net/net.c"));
	REQUIRE(net.find("mainEndStageWithoutResults();") != std::string::npos);
	REQUIRE(read_text_file("src/include/lib/main.h").find("void mainEndStageWithoutResults(void);") != std::string::npos);

	const std::string pdmain = code_only(read_text_file("port/src/pdmain.c"));
	const std::string end = function_body(pdmain, "void mainEndStage(void)\n{");
	const size_t skip = end.find("if (s_MainEndStageWithoutResults");
	const size_t coop = end.find("endscreenPushCoop();");
	const size_t anti = end.find("endscreenPushAnti();");
	const size_t mp = end.find("mpEndMatch();");
	REQUIRE(skip != std::string::npos);
	REQUIRE(coop != std::string::npos);
	REQUIRE(anti != std::string::npos);
	REQUIRE(mp != std::string::npos);
	REQUIRE(skip < coop);
	REQUIRE(skip < anti);
	// A networked Combat match still ends through mpEndMatch
	REQUIRE(end.find("|| !g_Vars.normmplayerisrunning", skip) < coop);
	// The wrapper comes after mainEndStage, so its first mention stays the definition
	REQUIRE(pdmain.find("mainEndStage(") == pdmain.find("void mainEndStage(void)") + 5);
}

TEST_CASE("mission end: a networked mission's agent save takes player 2's controls from the agent's slot",
          "[net][coop][b1199]") {
	// B-1199: slot 1 is the remote partner in a networked mission, whose
	// control mode is CONTROLMODE_NA, and the profile validator refused it
	const std::string save = code_only(read_text_file("port/src/savefile.c"));
	const std::string slot = function_body(save, "static s32 saveAgentPlayer2Slot(void)\n{");
	REQUIRE(slot.find("g_NetMode == NETMODE_NONE ? 1 : 5") != std::string::npos);
	const std::string capture = function_body(save,
		"static void saveCaptureAgentDocument(const char *name,\n\t\tstruct agent_profile_document *document)\n{");
	const std::string commit = function_body(save,
		"static void saveCommitAgentDocument(\n\t\tconst struct agent_profile_document *document)\n{");
	REQUIRE(capture.find("s32 player2 = saveAgentPlayer2Slot();") != std::string::npos);
	REQUIRE(commit.find("s32 player2 = saveAgentPlayer2Slot();") != std::string::npos);
}

TEST_CASE("mission end: a disconnect from the Room leaves the ended stage, frozen, for CI",
          "[net][coop][b1201]") {
	// B-1201: after a networked results exit the ended stage stays loaded
	// behind the Room; a disconnect there left it live offline, and a co-op
	// client's guards (host puppets) ran actions never set up here
	const std::string net = code_only(read_text_file("port/src/net/net.c"));
	const std::string disconnect = function_body(net, "static s32 netDisconnectWithIntent(s32 retain_reconnect)\n{");
	const size_t ended = disconnect.find("const bool leftendedstage = !wasingame && g_MainIsEndscreen");
	const size_t block = disconnect.find("if ((wasingame || leftendedstage) && !g_AppQuitting) {");
	const size_t pause = disconnect.find("lvSetPaused(true);", block);
	const size_t catchup = disconnect.find("g_Vars.props[i].propupdate240 = 0;", block);
	const size_t end = disconnect.find("mainEndStageWithoutResults();", block);
	const size_t ci = disconnect.find("sceneStageChangeTo(STAGE_CITRAINING,", block);
	REQUIRE(ended != std::string::npos);
	REQUIRE(block != std::string::npos);
	REQUIRE(pause != std::string::npos);
	REQUIRE(catchup != std::string::npos);
	REQUIRE(end != std::string::npos);
	REQUIRE(ci != std::string::npos);
	REQUIRE(ended < block);
	REQUIRE(pause < end);
	REQUIRE(catchup < end);
	REQUIRE(end < ci);
}

TEST_CASE("co-op chr removals: a chr the host removes leaves the client too",
          "[net][coop][b1206]") {
	// B-1206: a client runs no NPC AI or actions, so it never removes a chr by
	// itself. On Deep Sea the level script removed 2 guards in the stage's
	// first second and 1 later; the client kept them, and every NPC snapshot
	// after that was refused for its count (883 times in 42 s).
	REQUIRE(read_text_file("port/include/net/netmsg.h").find("#define SVC_NPC_REMOVE     0x7f") != std::string::npos);

	const std::string net = code_only(read_text_file("port/src/net/net.c"));
	REQUIRE(net.find("case SVC_NPC_REMOVE: rc = netmsgSvcNpcRemoveRead(&cl->in, cl); break;") != std::string::npos);

	// The host queues every removal chrTick makes, before it makes it (the chr
	// and its flags are gone after it)
	const std::string chr_c = code_only(read_text_file("src/game/chr.c"));
	const std::string tick = function_body(chr_c, "s32 chrTick(struct prop *prop)\n{");
	const size_t deleting = tick.find("if (chr->hidden & CHRHFLAG_DELETING) {");
	const size_t announce = tick.find("netServerNpcRemoveQueue(chr);", deleting);
	const size_t drop = tick.find("objDropRecursively(prop, true);", deleting);
	const size_t remove = tick.find("chrRemove(prop, true);", deleting);
	REQUIRE(deleting != std::string::npos);
	REQUIRE(announce != std::string::npos);
	REQUIRE(drop != std::string::npos);
	REQUIRE(remove != std::string::npos);
	REQUIRE(announce < drop);
	REQUIRE(drop < remove);
	REQUIRE(tick.rfind("g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI", announce) > deleting);
	// SP-91: nothing written in the game tick, which netEndFrame may discard
	REQUIRE(chr_c.find("netmsgSvcNpcRemoveWrite") == std::string::npos);

	const std::string queue = function_body(net, "void netServerNpcRemoveQueue(struct chrdata *chr)\n{");
	REQUIRE(queue.find("!netNpcIsReplicationReady(chr)") != std::string::npos);
	REQUIRE(queue.find("CHRHFLAG_DROPPINGITEM") != std::string::npos);
	// A chr made after the stage start and never announced is on no client
	REQUIRE(queue.find("syncid >= g_NetFirstDynamicSyncId && netServerNpcSpawnIndex(syncid) < 0") != std::string::npos);
	// A full queue asks for a full NPC snapshot, whose sweep removes the rest
	REQUIRE(queue.find("g_NetPendingResyncFlags |= NET_RESYNC_FLAG_NPCS;") != std::string::npos);

	// The queue goes out from the ACTIVE broadcast, ahead of the frame's spawns
	const std::string end_frame = function_body(net, "void netEndFrame(void)\n{");
	const size_t active = end_frame.find("if (s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE) {");
	const size_t coop = end_frame.find("if (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI) {", active);
	const size_t flush = end_frame.find("netServerNpcRemoveFlush();", coop);
	const size_t spawns = end_frame.find("netServerNpcSpawnAnnounce(&g_ChrSlots[i]);", coop);
	REQUIRE(active != std::string::npos);
	REQUIRE(coop != std::string::npos);
	REQUIRE(flush != std::string::npos);
	REQUIRE(spawns != std::string::npos);
	REQUIRE(flush < spawns);
	REQUIRE(end_frame.find("netServerNpcRemoveFlush();") == flush);
	const std::string flusher = function_body(net, "static void netServerNpcRemoveFlush(void)\n{");
	REQUIRE(flusher.find("netmsgSvcNpcRemoveWrite(&g_NetMsgRel, s_NetNpcRemoveIds[i], s_NetNpcRemoveFlags[i]);") != std::string::npos);
	REQUIRE(flusher.find("s_NetNpcRemoveCount = 0;") != std::string::npos);

	// The release baseline covers what is still queued, and every replication
	// reset (a new stage, a reset, a rolled-back start) empties the queue, so a
	// removal never names a chr of the next stage
	const size_t release = end_frame.find("s_NetStageReplicationPhase = NET_STAGE_REPLICATION_ACTIVE;");
	REQUIRE(release != std::string::npos);
	REQUIRE(end_frame.find("netServerNpcRemoveClear(", release) != std::string::npos);
	size_t resets = 0;
	for (size_t at = net.find("netNpcReplicationReset();"); at != std::string::npos;
			at = net.find("netNpcReplicationReset();", at + 1)) {
		const size_t next = net.find_first_not_of(" \t\r\n", at + std::string("netNpcReplicationReset();").size());
		REQUIRE(net.compare(next, std::string("netServerNpcRemoveClear(").size(), "netServerNpcRemoveClear(") == 0);
		resets++;
	}
	REQUIRE(resets == 3);

	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string write = function_body(msg, "u32 netmsgSvcNpcRemoveWrite(struct netbuf *dst, u32 syncid, u8 flags)\n{");
	REQUIRE(write.find("netbufWriteU8(dst, SVC_NPC_REMOVE);") != std::string::npos);
	REQUIRE(write.find("netbufWriteU32(dst, syncid);") != std::string::npos);
	REQUIRE(write.find("netbufWriteU8(dst, flags);") != std::string::npos);

	// The client removes its copy at once, as the host's chrTick and prop loop do
	const std::string local = function_body(msg,
		"static void netNpcRemoveLocal(struct prop *prop, bool dropitems, const char *why)\n{");
	const size_t ldrop = local.find("objDropRecursively(prop, true);");
	const size_t lremove = local.find("chrRemove(prop, true);");
	const size_t lfree = local.find("propExecuteTickOperation(prop, TICKOP_FREE);");
	REQUIRE(local.find("if (dropitems) {") != std::string::npos);
	REQUIRE(ldrop != std::string::npos);
	REQUIRE(lremove != std::string::npos);
	REQUIRE(lfree != std::string::npos);
	REQUIRE(ldrop < lremove);
	REQUIRE(lremove < lfree);

	const std::string read = function_body(msg, "u32 netmsgSvcNpcRemoveRead(struct netbuf *src, struct netclient *srccl)\n{");
	REQUIRE(read.find("g_NetGameMode != NETGAMEMODE_COOP && g_NetGameMode != NETGAMEMODE_ANTI") != std::string::npos);
	REQUIRE(read.find("prop->chr->aibot") != std::string::npos);
	REQUIRE(read.find("netNpcRemoveLocal(prop,") != std::string::npos);
	REQUIRE(read.find("return 1") == std::string::npos);

	// A snapshot removes what the host no longer names, then counts again
	const std::string resync = function_body(msg, "u32 netmsgSvcNpcResyncRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t name = resync.find("named[namedcount++] = record.prop;");
	const size_t unnamed = resync.find("netNpcRemoveUnnamed(named, namedcount)");
	const size_t recount = resync.find("local_count = netNpcCount();", unnamed);
	const size_t compare = resync.find("if (local_count != npccount) {");
	const size_t reject = resync.find("rejected incomplete snapshot");
	REQUIRE(name != std::string::npos);
	REQUIRE(unnamed != std::string::npos);
	REQUIRE(recount != std::string::npos);
	REQUIRE(compare != std::string::npos);
	REQUIRE(reject != std::string::npos);
	REQUIRE(name < unnamed);
	REQUIRE(unnamed < compare);
	REQUIRE(compare < reject);

	const std::string sweep = function_body(msg,
		"static u32 netNpcRemoveUnnamed(struct prop *const *named, u32 namedcount)\n{");
	REQUIRE(sweep.find("netNpcIsReplicationReady(chr)") != std::string::npos);
	REQUIRE(sweep.find("netNpcRemoveLocal(chr->prop, false,") != std::string::npos);
}

TEST_CASE("co-op chr removals: a client's rooms update moves the prop's room registration",
          "[net][coop][b1270]") {
	// B-1270: the client's NPC resync wrote the host's rooms straight into
	// prop->rooms, so the chr stayed listed in its old rooms. Once the host
	// removed it (SVC_NPC_REMOVE, B-1206) and its prop was freed, the next NPC
	// move's collision query in an old room read a chr prop with no chr:
	// ACCESS_VIOLATION in propIsOfCdType on Deep Sea, as the intro ended.
	const std::string prop_c = code_only(read_text_file("src/game/prop.c"));
	const std::string set = function_body(prop_c, "void propSetRooms(struct prop *prop, const RoomNum *rooms)\n{");
	const size_t registered = set.find("registered = prop->active || propIsListedInRoom(prop, prop->rooms[0]);");
	const size_t leave = set.find("propDeregisterRooms(prop);");
	const size_t copy = set.find("prop->rooms[i] = rooms[i];");
	const size_t stop = set.find("if (rooms[i] < 0) {", copy);
	const size_t join = set.find("propRegisterRooms(prop);");
	REQUIRE(registered != std::string::npos);
	REQUIRE(leave != std::string::npos);
	REQUIRE(copy != std::string::npos);
	REQUIRE(stop != std::string::npos);
	REQUIRE(join != std::string::npos);
	REQUIRE(registered < leave);
	REQUIRE(leave < copy);
	REQUIRE(stop < join);
	// Both ends only for a prop that is in the world
	REQUIRE(set.rfind("if (registered) {", leave) > registered);
	REQUIRE(set.rfind("if (registered) {", join) > stop);

	const std::string listed = function_body(prop_c, "static bool propIsListedInRoom(struct prop *prop, RoomNum room)\n{");
	REQUIRE(listed.find("room < 0 || room >= g_Vars.roomcount") != std::string::npos);
	REQUIRE(listed.find("chunkindex = g_RoomPropListChunkIndexes[room];") != std::string::npos);
	REQUIRE(listed.find("chunkindex = g_RoomPropListChunks[chunkindex].propnums[MAX_PROPSPERROOMCHUNK];") != std::string::npos);
	REQUIRE(read_text_file("src/include/game/prop.h").find("void propSetRooms(struct prop *prop, const RoomNum *rooms);")
		!= std::string::npos);

	// Every rooms list from the host that a client applies goes through it
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const struct {
		const char *signature;
		const char *call;
	} sites[] = {
		{ "static void netmsgNpcResyncRecordApply(\n\t\tconst struct net_npc_resync_record *record)\n{",
			"propSetRooms(prop, record->rooms);" },
		{ "u32 netmsgSvcChrResyncRead(struct netbuf *src, struct netclient *srccl)\n{", "propSetRooms(prop, rooms);" },
		{ "u32 netmsgSvcPropResyncRead(struct netbuf *src, struct netclient *srccl)\n{", "propSetRooms(prop, rooms);" },
		{ "u32 netmsgSvcPropMoveRead(struct netbuf *src, struct netclient *srccl)\n{", "propSetRooms(prop, rooms);" },
		{ "u32 netmsgSvcPropLiftRead(struct netbuf *src, struct netclient *srccl)\n{", "propSetRooms(prop, rooms);" },
	};
	for (const auto &site : sites) {
		INFO(site.signature);
		const std::string body = function_body(msg, site.signature);
		REQUIRE(body.find(site.call) != std::string::npos);
		REQUIRE(body.find("prop->rooms[r] = ") == std::string::npos);
		REQUIRE(body.find("prop->rooms[i] = ") == std::string::npos);
		REQUIRE(body.find("roomsCopy(rooms, prop->rooms);") == std::string::npos);
		REQUIRE(body.find("if (prop->active) {") == std::string::npos);
	}

	// The prop resync names objects only; prop->obj shares storage with
	// prop->chr, so a row whose id names a chr here is skipped
	const std::string prop_resync = function_body(msg, "u32 netmsgSvcPropResyncRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t skip = prop_resync.find("|| prop->type != PROPTYPE_OBJ) {");
	REQUIRE(skip != std::string::npos);
	REQUIRE(skip < prop_resync.find("struct defaultobj *obj = prop->obj;"));
}
