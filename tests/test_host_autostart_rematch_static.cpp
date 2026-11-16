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
	// multiplayer end screen, and still counts them as player numbers. Not by
	// the player's isremote: the host's stage end commit clears it first
	const size_t ending = menutick.find("} else if (g_MenuData.prevmenuroot == -6) {");
	REQUIRE(ending != std::string::npos);
	const size_t remote = menutick.find("mpGetParticipant(i)->type == PARTICIPANT_REMOTE", ending);
	const size_t push = menutick.find("mpPushEndscreenDialog(playernum, i);", remote);
	REQUIRE(menutick.find("->isremote", ending) > menutick.find("g_MenuData.prevmenuroot == -7", ending));

	// B-1190: participant types are per machine on a client too: its stage
	// start marks its own player local after the mask decode (all remote)
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const size_t decode = msg.find("mpParticipantsDecodeActiveMask(plan.active_mask);");
	const size_t mine = msg.find("if (&g_NetClients[plan.clients[i].id] == g_NetLocalClient) {", decode);
	const size_t mark = msg.find("local->type = PARTICIPANT_LOCAL;", mine);
	REQUIRE(decode != std::string::npos);
	REQUIRE(mine != std::string::npos);
	REQUIRE(mark != std::string::npos);
	REQUIRE(mark - decode < 800);
	const size_t next = menutick.find("playernum++;", push);
	REQUIRE(remote != std::string::npos);
	REQUIRE(push != std::string::npos);
	REQUIRE(next != std::string::npos);
	const size_t later = menutick.find("g_MenuData.prevmenuroot == -7", ending);
	REQUIRE(later != std::string::npos);
	REQUIRE(next < later);
}

TEST_CASE("host autostart rematch: the lobby UI does not draw the Room over an open results screen",
          "[net][rematch][finding4][b1192]") {
	const std::string lobby = code_only(read_text_file("port/fast3d/pdgui_lobby.cpp"));
	const std::string render = function_body(lobby, "void pdguiLobbyRender(s32 winW, s32 winH)\n{");

	// B-1192: in the lobby state, the results screen check comes before the
	// Room and the social lobby, so neither draws (and takes focus) while it is open
	const size_t in_lobby = render.find("if (netLocalClientInLobby())");
	// (B-1193 adds the solo type for co-op and counter-op results)
	const size_t open = render.find("const bool resultsOpen = menupoolIsActive(MENU_TYPE_ENDSCREEN_MP) != 0", in_lobby);
	const size_t results = render.find("if (resultsOpen || s_ResultsPending) {", open);
	const size_t room = render.find("} else if (s_InRoom) {", results);
	REQUIRE(open != std::string::npos);
	const size_t room_render = render.find("pdguiRoomScreenRender(winW, winH);", room);
	const size_t social = render.find("pdguiLobbyScreenRender(winW, winH);", room_render);
	REQUIRE(in_lobby != std::string::npos);
	REQUIRE(results != std::string::npos);
	REQUIRE(room != std::string::npos);
	REQUIRE(room_render != std::string::npos);
	REQUIRE(social != std::string::npos);
	// Nothing between the check and the Room branch draws a screen
	const std::string gated = render.substr(results, room - results);
	REQUIRE(gated.find("Render(") == std::string::npos);
}

TEST_CASE("results for every player: a client shows its results screen before the Room",
          "[net][rematch][finding4][b1190]") {
	// SVC_STAGE_END holds the lobby UI back before the client's lobby state is
	// set and before mainEndStage queues the end screen
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string end = function_body(msg, "u32 netmsgSvcStageEndRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t mp = end.find("} else {");
	const size_t hold = end.find("pdguiLobbyHoldForResults();", mp);
	const size_t lobby = end.find("g_NetLocalClient->state = CLSTATE_LOBBY;", hold);
	const size_t stage = end.find("mainEndStage();", lobby);
	REQUIRE(mp != std::string::npos);
	REQUIRE(hold != std::string::npos);
	REQUIRE(lobby != std::string::npos);
	REQUIRE(stage != std::string::npos);

	// The hold ends once the results screen has opened (then the open screen
	// holds the Room), after a bounded wait, at the next match, or on disconnect
	const std::string lobbyui = code_only(read_text_file("port/fast3d/pdgui_lobby.cpp"));
	const std::string render = function_body(lobbyui, "void pdguiLobbyRender(s32 winW, s32 winH)\n{");
	const size_t open = render.find("if (resultsOpen) {");
	REQUIRE(open != std::string::npos);
	REQUIRE(render.find("s_ResultsPending = false;", open) != std::string::npos);
	REQUIRE(render.find("++s_ResultsPendingFrames > PDGUI_RESULTS_PENDING_MAX_FRAMES") != std::string::npos);
	const size_t disconnect = render.find("if (s_LastMode != NETMODE_NONE && mode == NETMODE_NONE) {");
	REQUIRE(disconnect != std::string::npos);
	REQUIRE(render.find("s_ResultsPending = false;", disconnect) < render.find("s_LastMode = mode;"));
	const size_t game = render.find("menupoolRelease(MENU_TYPE_ROOM);");
	REQUIRE(game != std::string::npos);
	REQUIRE(render.find("s_ResultsPending = false;", game) != std::string::npos);
}

TEST_CASE("results for every player: the room shows who is still reviewing match stats",
          "[net][rematch][finding4][b1190]") {
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));

	// The host marks everyone in the room at stage end...
	const std::string commit = function_body(msg, "void netmsgSvcStageEndCommit(u8 room_id, u8 mode)\n{");
	const size_t mp = commit.find("ncl->state = CLSTATE_LOBBY;");
	REQUIRE(mp != std::string::npos);
	REQUIRE(commit.find("ncl->reviewing_results = 1;", mp) != std::string::npos);

	// ...and clears a player on its Continue (a client's lobby resync, or the
	// listen host's local one) or when the next match takes it along
	const std::string resync = function_body(msg, "u32 netmsgClcLobbyResyncRead(struct netbuf *src, struct netclient *srccl)\n{");
	REQUIRE(resync.find("srccl->reviewing_results = 0;") != std::string::npos);
	const std::string local = function_body(msg, "void netSendLobbyResync(void)\n{");
	REQUIRE(local.find("g_NetLocalClient->reviewing_results = 0;") != std::string::npos);
	const size_t start = msg.find("player->client->state = CLSTATE_PREPARING;");
	REQUIRE(start != std::string::npos);
	const size_t along = msg.find("player->client->reviewing_results = 0;", start);
	REQUIRE(along != std::string::npos);
	REQUIRE(along - start < 400);

	// The roster carries it only in the lobby state, and both lists show it
	const std::string lobby = code_only(read_text_file("port/src/net/netlobby.c"));
	REQUIRE(lobby.find("lp->reviewing = (cl->reviewing_results && cl->state == CLSTATE_LOBBY) ? 1 : 0;") != std::string::npos);
	REQUIRE(read_text_file("port/fast3d/pdgui_menu_room.cpp").find("\"reviewing match stats\"") != std::string::npos);
	REQUIRE(read_text_file("port/fast3d/pdgui_menu_lobby.cpp").find("\"Reviewing match stats\"") != std::string::npos);
}

TEST_CASE("results for every player: a client keeps its own player linked at a Combat stage end",
          "[net][rematch][finding4][b1190]") {
	// B-1190 part 2: unlinking the local client too left no local player, so
	// the whole lvTick (and menuTick's results push) was deferred
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string end = function_body(msg, "u32 netmsgSvcStageEndRead(struct netbuf *src, struct netclient *srccl)\n{");
	const size_t loop = end.find("u8 local_room = g_NetLocalClient ? g_NetLocalClient->room_id : 0xFF;");
	const size_t skip = end.find("if (ncl == g_NetLocalClient) {", loop);
	const size_t unlink = end.find("ncl->player = NULL;", loop);
	REQUIRE(loop != std::string::npos);
	REQUIRE(skip != std::string::npos);
	REQUIRE(unlink != std::string::npos);
	REQUIRE(skip < unlink);
	REQUIRE(end.find("continue;", skip) < unlink);
}

TEST_CASE("results for every player: the listen host keeps its own player linked at a Combat stage end",
          "[net][rematch][finding4][b1195]") {
	// B-1195: the host's stage-end commit unlinked its own player too, so its
	// global tick was deferred under the results, the pause never applied and
	// simulant AI ran from the render pass until it divided by zero
	const std::string msg = code_only(read_text_file("port/src/net/netmsg.c"));
	const std::string commit = function_body(msg, "void netmsgSvcStageEndCommit(u8 room_id, u8 mode)\n{");
	const size_t review = commit.find("ncl->reviewing_results = 1;");
	const size_t skip = commit.find("if (ncl == g_NetLocalClient) {", review);
	const size_t unlink = commit.find("ncl->player = NULL;", review);
	REQUIRE(review != std::string::npos);
	REQUIRE(skip != std::string::npos);
	REQUIRE(unlink != std::string::npos);
	REQUIRE(skip < unlink);
	REQUIRE(commit.find("continue;", skip) < unlink);
}

TEST_CASE("results for every player: simulants of a retired match do not think",
          "[net][rematch][finding4][b1195]") {
	// The results exit to the Room retires the chr table (B-368) while the
	// ended arena stays loaded; the simulant AI divides by g_MpNumChrs
	const std::string bot = code_only(read_text_file("src/game/bot.c"));
	const std::string tick = function_body(bot, "s32 botTick(struct prop *prop)\n{");
	const size_t puppet = tick.find("return puppetresult;");
	const size_t retired = tick.find("if (g_MpNumChrs <= 0) {", puppet);
	const size_t spawn = tick.find("wave_result = botSpawnAll();");
	const size_t think = tick.find("botTickUnpaused(chr);");
	REQUIRE(puppet != std::string::npos);
	REQUIRE(retired != std::string::npos);
	REQUIRE(spawn != std::string::npos);
	REQUIRE(think != std::string::npos);
	REQUIRE(retired < spawn);
	REQUIRE(retired < think);
	const size_t live = tick.find("s_BotRetiredLogged = false;", retired);
	REQUIRE(live != std::string::npos);
	REQUIRE(live < spawn);
	const std::string gate = tick.substr(retired, live - retired);
	REQUIRE(gate.find("\"BOT: match chr table retired; simulants are inert until the next stage\"") != std::string::npos);
	REQUIRE(gate.find("aibot->speedmultforwards = 0;") != std::string::npos);
	REQUIRE(gate.find("return chrTick(prop);") != std::string::npos);

	// B-368 still clears the table in that exit
	const std::string bridge = code_only(read_text_file("port/fast3d/pdgui_bridge.c"));
	const std::string exit = function_body(bridge, "void pdguiEndscreenExitToRoom(void)\n{");
	REQUIRE(exit.find("mpClearRuntimeChrState();") != std::string::npos);
}

TEST_CASE("results for every player: the primary results button is the default focus",
          "[net][rematch][finding4][b1192]") {
	// B-1192 part 2: the debounce disables the bar when ImGui picks the first
	// focus, so the default has to be named
	const std::string es = code_only(read_text_file("port/fast3d/pdgui_menu_endscreen.cpp"));
	const std::string mp = function_body(es, "static void renderMpEndscreen(struct menudialog *dialog, const char *titleOverride, s32 challengeResult)\n{");
	for (const char *primary : { "pdguiActionBarButton(\"Return to Room\", 1, halfW)", "pdguiActionBarButton(\"Play Again\", 1, halfW)" }) {
		INFO(primary);
		const size_t button = mp.find(primary);
		const size_t focus = mp.find("ImGui::SetItemDefaultFocus();", button);
		const size_t next = mp.find("pdguiActionBarButton(", button + 1);
		REQUIRE(button != std::string::npos);
		REQUIRE(focus != std::string::npos);
		REQUIRE(focus < next);
	}
	const std::string solo = function_body(es, "static void renderSoloEndscreen(struct menudialog *dialog, bool completed)\n{");
	const size_t appearing = solo.find("if (ImGui::IsWindowAppearing()) {");
	REQUIRE(appearing != std::string::npos);
	REQUIRE(solo.find("ImGui::SetWindowFocus();", appearing) < solo.find("achievementsRefresh();", appearing));
	const size_t next = solo.find("pdguiActionBarButton(\"Next Mission\", 1, slotW)");
	REQUIRE(next != std::string::npos);
	REQUIRE(solo.find("ImGui::SetItemDefaultFocus();", next) < solo.find("pdguiActionBarButton(\"Retry Mission\", hasNext ? 0 : 1", next));
}
