/*
 * tests/test_net_lifecycle_static.cpp -- Static guards for network lifecycle
 * transitions whose live paths are too coupled for pd-tests to execute.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

size_t function_body_end(const std::string &text, size_t brace)
{
	enum class lexical_state {
		normal,
		line_comment,
		block_comment,
		string_literal,
		character_literal,
	};

	lexical_state state = lexical_state::normal;
	size_t depth = 0;

	for (size_t pos = brace; pos < text.size(); pos++) {
		const char ch = text[pos];
		const char next = pos + 1 < text.size() ? text[pos + 1] : '\0';

		switch (state) {
		case lexical_state::normal:
			if (ch == '/' && next == '/') {
				state = lexical_state::line_comment;
				pos++;
			} else if (ch == '/' && next == '*') {
				state = lexical_state::block_comment;
				pos++;
			} else if (ch == '"') {
				state = lexical_state::string_literal;
			} else if (ch == '\'') {
				state = lexical_state::character_literal;
			} else if (ch == '{') {
				depth++;
			} else if (ch == '}') {
				REQUIRE(depth > 0);
				depth--;
				if (depth == 0) {
					return pos;
				}
			}
			break;

		case lexical_state::line_comment:
			if (ch == '\n') {
				state = lexical_state::normal;
			}
			break;

		case lexical_state::block_comment:
			if (ch == '*' && next == '/') {
				state = lexical_state::normal;
				pos++;
			}
			break;

		case lexical_state::string_literal:
		case lexical_state::character_literal:
			if (ch == '\\' && pos + 1 < text.size()) {
				pos++;
			} else if ((state == lexical_state::string_literal && ch == '"')
					|| (state == lexical_state::character_literal && ch == '\'')) {
				state = lexical_state::normal;
			}
			break;
		}
	}

	FAIL("function definition block not closed");
	return std::string::npos;
}

std::string read_text_file(const char *path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string function_block(const std::string &text, const char *signature)
{
    const size_t begin = text.find(signature);
    REQUIRE(begin != std::string::npos);

    const size_t brace = text.find('{', begin);
    REQUIRE(brace != std::string::npos);

	const size_t end = function_body_end(text, brace);
	REQUIRE(end != std::string::npos);
	return text.substr(begin, end - begin + 1);
}

std::string function_definition_block(const std::string &text, const char *signature)
{
    size_t search = 0;

    while (true) {
        const size_t begin = text.find(signature, search);
        REQUIRE(begin != std::string::npos);

        const size_t brace = text.find('{', begin);
        REQUIRE(brace != std::string::npos);

        const size_t semi = text.find(';', begin);
        if (semi != std::string::npos && semi < brace) {
            search = semi + 1;
            continue;
        }

		const size_t end = function_body_end(text, brace);
		REQUIRE(end != std::string::npos);
		return text.substr(begin, end - begin + 1);
    }
}

size_t count_occurrences(const std::string &text, const char *needle)
{
	size_t count = 0;
	size_t pos = 0;
	const size_t length = std::char_traits<char>::length(needle);

	while ((pos = text.find(needle, pos)) != std::string::npos) {
		count++;
		pos += length;
	}
	return count;
}

} /* anon */

TEST_CASE("B-1092 server disconnect policy survives ENet sender-local reason loss",
          "[net][lifecycle][reconnect][static][b1092]")
{
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string pdgui = read_text_file("port/fast3d/pdgui_bridge.c");
    const std::string server = read_text_file("port/src/server_bridge.c");
    const std::string kick = function_definition_block(
        net, "void netServerKick(struct netclient *cl");
    const std::string disconnect = function_definition_block(
        net, "static void netServerEvDisconnect(struct netclient *cl");
    const std::string admin = function_definition_block(
        netmsg, "u32 netmsgClcAdminRead");
    const std::string pdgui_kick = function_definition_block(
        pdgui, "void netServerKickClient");
    const std::string server_kick = function_definition_block(
        server, "void netServerKickClient");
    const std::string server_ban = function_definition_block(
        server, "void netServerBanClient");

    const size_t duplicate_gate = kick.find(
        "if (cl->server_disconnect_intent_pending)");
    const size_t latch_reason = kick.find(
        "cl->server_disconnect_intent_reason = reason", duplicate_gate);
    const size_t latch_pending = kick.find(
        "cl->server_disconnect_intent_pending = true", latch_reason);
    const size_t enet_call = kick.find(
        "enet_peer_disconnect(cl->peer, reason)", latch_pending);
    REQUIRE(duplicate_gate != std::string::npos);
    REQUIRE(latch_reason != std::string::npos);
    REQUIRE(latch_pending != std::string::npos);
    REQUIRE(enet_call != std::string::npos);
    REQUIRE(duplicate_gate < latch_reason);
    REQUIRE(latch_reason < latch_pending);
    REQUIRE(latch_pending < enet_call);

    const size_t resolve = disconnect.find(
        "netReconnectPlanServerDisconnect(transport_reason");
    const size_t consume = disconnect.find(
        "cl->server_disconnect_intent_pending = false", resolve);
    const size_t retryable = disconnect.find(
        "netReconnectReasonIsRetryable(reason", consume);
    REQUIRE(resolve != std::string::npos);
    REQUIRE(consume != std::string::npos);
    REQUIRE(retryable != std::string::npos);
    REQUIRE(resolve < consume);
    REQUIRE(consume < retryable);

    REQUIRE(admin.find("netServerKick(srccl, DISCONNECT_ADMIN_AUTH)") !=
        std::string::npos);
    REQUIRE(pdgui_kick.find("netServerKick(cl, DISCONNECT_KICKED)") !=
        std::string::npos);
    REQUIRE(server_kick.find("netServerKick(cl, DISCONNECT_KICKED)") !=
        std::string::npos);
    REQUIRE(server_ban.find("netServerKick(cl, DISCONNECT_BANNED)") !=
        std::string::npos);
}

TEST_CASE("net lifecycle: CLC_LOBBY_START authorizes before reading payload",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcLobbyStartRead");
    const std::string authority = function_block(
        netmsg, "static bool netLobbyStartIsAuthority");

    const size_t context_gate = read.find(
        "if (!src || g_NetMode != NETMODE_SERVER || s_ReadyGate.active");
    const size_t authority_gate = read.find(
        "if (!netLobbyStartIsAuthority(srccl, &room))");
    const size_t first_payload_read = read.find("netbufReadU8(src)");

    REQUIRE(context_gate != std::string::npos);
    REQUIRE(authority_gate != std::string::npos);
    REQUIRE(first_payload_read != std::string::npos);
    REQUIRE(context_gate < authority_gate);
    REQUIRE(authority_gate < first_payload_read);

    REQUIRE(authority.find("room->state != ROOM_STATE_LOBBY") != std::string::npos);
    REQUIRE(authority.find("room->creator_client_id != srccl->id") != std::string::npos);
    REQUIRE(authority.find("netLobbyStartRoomContains(room, (u8)srccl->id)") !=
        std::string::npos);
    REQUIRE(authority.find("g_Lobby.leaderSlot < LOBBY_MAX_PLAYERS") !=
        std::string::npos);
    REQUIRE(authority.find("for (u8 i = 0; i < NET_MAX_CLIENTS; i++)") !=
        std::string::npos);
    REQUIRE(authority.find("lobbyUpdate();") == std::string::npos);
    REQUIRE(read.find("lobbyUpdate();") == std::string::npos);
}

TEST_CASE("net lifecycle: listen host can start a match (Combat Sim / co-op start fix)",
          "[net][lifecycle][static]")
{
    /* The in-client listen host runs NETMODE_SERVER (g_NetDedicated==0) and has
     * no wire to itself, so netLobbyRequestStartWithSims must replay
     * CLC_LOBBY_START through the server handler locally instead of rejecting
     * every non-CLIENT mode (the dead "Start Match" button bug). */
    const std::string bridge = read_text_file("port/fast3d/pdgui_bridge.c");
    const std::string fn = function_block(bridge, "s32 netLobbyRequestStartWithSims");

    /* The guard accepts the listen host, not only NETMODE_CLIENT. */
    const size_t listen_host = fn.find("g_NetMode == NETMODE_SERVER && !g_NetDedicated");
    REQUIRE(listen_host != std::string::npos);

    /* The write still happens, and the host replays it through the server
     * handler (skipping the message-type byte first), mirroring
     * netSendRoomSettingsUpdate. */
    const size_t write = fn.find("netmsgClcLobbyStartWrite(");
    const size_t skip_type = fn.find("netbufReadU8(&rb)", write);
    const size_t local_replay = fn.find("netmsgClcLobbyStartRead(&rb, g_NetLocalClient)", skip_type);
    const size_t client_send = fn.find("netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL)");

    REQUIRE(write != std::string::npos);
    REQUIRE(skip_type != std::string::npos);
    REQUIRE(local_replay != std::string::npos);
    REQUIRE(client_send != std::string::npos);   /* remote client path preserved */
    REQUIRE(write < local_replay);

    /* The old client-only rejection must be gone. */
    REQUIRE(fn.find("if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient)") == std::string::npos);
}

TEST_CASE("B-1076 every match start closes one complete menu owner before requesting its stage",
		"[net][lifecycle][menu][transition][static][b1076]")
{
	const std::string net = read_text_file("port/src/net/net.c");
	const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string matchsetup = read_text_file("port/src/net/matchsetup.c");
	const std::string menutick = read_text_file("src/game/menutick.c");
	const std::string mplayer = read_text_file("src/game/mplayer/mplayer.c");
	const std::string cleanup = function_block(
		net, "static void netServerPrepareInClientStageTransition");
	const std::string combat = function_block(net, "s32 netServerStageStart");
	const std::string coop = function_block(net, "s32 netServerCoopStageStart");
	const std::string client = function_block(netmsg,
		"u32 netmsgSvcStageStartRead");
	const std::string local = function_block(matchsetup, "s32 matchStart(void)");
	const std::string challenge = function_block(matchsetup,
		"s32 matchStartFromChallenge");
	const std::string mp_start = function_block(mplayer,
		"void mpStartMatch(void)");
	const size_t mp_cleanup = mp_start.find("menuStop();");
	const size_t mp_release = mp_start.find(
		"sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL");
	const size_t mp_request = mp_start.find("mainChangeToStage(stagenum);");
	const size_t coop_cleanup = coop.find(
		"netServerPrepareInClientStageTransition(\"server stage start coop\")");
	const size_t coop_request = coop.find("mainChangeToStage(stagenum);");
	const size_t legacy_begin = menutick.find("g_MenuData.prevmenuroot == -5");
	const size_t legacy_end = menutick.find("g_MenuData.prevmenuroot == -6",
		legacy_begin);
	REQUIRE(legacy_begin != std::string::npos);
	REQUIRE(legacy_end != std::string::npos);
	const std::string legacy_start = menutick.substr(legacy_begin,
		legacy_end - legacy_begin);

	REQUIRE(cleanup.find("menuStop();") != std::string::npos);
	REQUIRE(cleanup.find(
		"sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL")
		!= std::string::npos);
	REQUIRE(count_occurrences(mp_start, "menuStop();") == 1);
	REQUIRE(count_occurrences(mp_start,
		"sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL") == 1);
	REQUIRE(mp_cleanup < mp_release);
	REQUIRE(mp_release < mp_request);

	REQUIRE(count_occurrences(combat, "mpStartMatch();") == 1);
	REQUIRE(combat.find("server stage start combat") == std::string::npos);
	REQUIRE(count_occurrences(client, "mpStartMatch();") == 1);
	REQUIRE(client.find("SVC_STAGE_START combat") == std::string::npos);
	REQUIRE(count_occurrences(local, "mpStartMatch();") == 1);
	REQUIRE(local.find("menuStop();") == std::string::npos);
	REQUIRE(count_occurrences(challenge, "mpStartMatch();") == 1);
	REQUIRE(challenge.find("menuStop();") == std::string::npos);
	REQUIRE(count_occurrences(legacy_start, "mpStartMatch();") == 1);
	REQUIRE(legacy_start.find("menuStop();") == std::string::npos);

	REQUIRE(count_occurrences(coop,
		"netServerPrepareInClientStageTransition(\"server stage start coop\")") == 1);
	REQUIRE(coop_cleanup < coop_request);
}

TEST_CASE("net lifecycle: local settings refresh serves clients and listen hosts without a host loopback send",
          "[net][lifecycle][settings][static][b1071]")
{
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string match = read_text_file("port/src/net/matchsetup.c");
    const std::string changed = function_block(net, "s32 netClientSettingsChanged");
    const std::string init = function_block(match, "void matchConfigInit");

    const size_t client_role = changed.find("g_NetMode == NETMODE_CLIENT");
    const size_t host_role = changed.find(
        "g_NetMode == NETMODE_SERVER && !g_NetDedicated");
    const size_t refresh = changed.find(
        "netClientReadConfig(g_NetLocalClient, 0)");
    const size_t prepare = changed.find("netClientPrepareCachedSettings(", refresh);
    const size_t commit = changed.find("netClientCommitPreparedSettings(", prepare);
    const size_t host_return = changed.find("if (is_listen_host)", commit);
    const size_t write = changed.find("netmsgClcSettingsWrite(&g_NetMsgRel)",
                                      host_return);
    const size_t send = changed.find("netSend(NULL, &g_NetMsgRel", write);
    const size_t reset = init.find("matchResetHandicaps()");
    const size_t publish = init.find("g_MatchConfig.numSlots = 1", reset);
    const size_t notify = init.find("netClientSettingsChanged()", publish);

    REQUIRE(client_role != std::string::npos);
    REQUIRE(host_role != std::string::npos);
    REQUIRE(refresh != std::string::npos);
    REQUIRE(prepare != std::string::npos);
    REQUIRE(commit != std::string::npos);
    REQUIRE(host_return != std::string::npos);
    REQUIRE(write != std::string::npos);
    REQUIRE(send != std::string::npos);
    REQUIRE(reset != std::string::npos);
    REQUIRE(publish != std::string::npos);
    REQUIRE(notify != std::string::npos);
    REQUIRE(refresh < prepare);
    REQUIRE(prepare < commit);
    REQUIRE(commit < host_return);
    REQUIRE(host_return < write);
    REQUIRE(write < send);
    REQUIRE(reset < publish);
    REQUIRE(publish < notify);
}

TEST_CASE("net settings: v55 publishes exact lobby identity and handicap before roster start",
          "[net][lifecycle][settings][wire][static][b1071]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string codec = read_text_file(
        "port/src/net/net_client_settings_wire.c");
    const std::string bridge = read_text_file("port/fast3d/pdgui_bridge.c");
    const std::string write = function_block(netmsg, "u32 netmsgClcSettingsWrite");
    const std::string read = function_block(netmsg, "u32 netmsgClcSettingsRead");
    const std::string start = function_block(bridge, "s32 netLobbyRequestStartWithSims");
    const std::string roster = function_block(netmsg,
        "static bool netLobbyStartPrepareRoster");

    REQUIRE(write.find("input.body_id = g_NetLocalClient->settings.body_id") !=
        std::string::npos);
    REQUIRE(write.find("input.head_id = g_NetLocalClient->settings.head_id") !=
        std::string::npos);
    REQUIRE(write.find("input.handicap = g_NetLocalClient->settings.handicap") !=
        std::string::npos);
    REQUIRE(write.find("netClientSettingsWireWrite(dst, CLC_SETTINGS") !=
        std::string::npos);
    REQUIRE(write.find("sessionCatalogGetId") == std::string::npos);

    REQUIRE(read.find("netClientSettingsWireRead(src, &plan") !=
        std::string::npos);
    REQUIRE(read.find("status != NET_CLIENT_SETTINGS_WIRE_OK") !=
        std::string::npos);
    REQUIRE(read.find("srccl->settings.handicap = effective_handicap") !=
        std::string::npos);
    REQUIRE(read.find("body_session") == std::string::npos);
    REQUIRE(read.find("head_session") == std::string::npos);

    REQUIRE(codec.find("playerIdentityPrepare(input->body_id, input->head_id") !=
        std::string::npos);
    REQUIRE(codec.find("input->handicap == 0") != std::string::npos);
    REQUIRE(codec.find("netbufWriteStr(dst, plan.identity.body_id)") !=
        std::string::npos);
    REQUIRE(codec.find("netbufWriteStr(dst, plan.identity.head_id)") !=
        std::string::npos);
    REQUIRE(codec.find("netbufWriteU8(dst, plan.handicap)") !=
        std::string::npos);

    const size_t refresh = start.find("netClientSettingsChanged()");
    const size_t lobbyWrite = start.find("netmsgClcLobbyStartWrite(", refresh);
    REQUIRE(refresh != std::string::npos);
    REQUIRE(lobbyWrite != std::string::npos);
    REQUIRE(refresh < lobbyWrite);

    REQUIRE(roster.find("reason=client_settings") != std::string::npos);
    REQUIRE(roster.find("netClientSettingsPrepare(&settings_input") !=
        std::string::npos);
    REQUIRE(roster.find("player->identity = settings_plan.identity") !=
        std::string::npos);
    REQUIRE(roster.find("player->config.handicap = settings_plan.handicap") !=
        std::string::npos);
}

TEST_CASE("net lifecycle: malformed SVC_MATCH_MANIFEST clears staged client manifest",
          "[net][lifecycle][manifest][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcMatchManifestRead");

    const size_t initial_clear = read.find("manifestClear(&g_ClientManifest);");
    const size_t hash_stage = read.find("g_ClientManifest.manifest_hash = manifest_hash;", initial_clear);
    const size_t deserialize = read.find("manifestDeserialize(src, &g_ClientManifest)", hash_stage);
    const size_t parse_error = read.find("NET: SVC_MATCH_MANIFEST parse error", deserialize);
    const size_t failure_clear = read.find("manifestClear(&g_ClientManifest);", parse_error);
    const size_t failure_return = read.find("return 1;", failure_clear);
    const size_t preparing_state = read.find("g_NetLocalClient->state = CLSTATE_PREPARING", failure_return);

    REQUIRE(initial_clear != std::string::npos);
    REQUIRE(hash_stage != std::string::npos);
    REQUIRE(deserialize != std::string::npos);
    REQUIRE(parse_error != std::string::npos);
    REQUIRE(failure_clear != std::string::npos);
    REQUIRE(failure_return != std::string::npos);
    REQUIRE(preparing_state != std::string::npos);

    REQUIRE(initial_clear < hash_stage);
    REQUIRE(hash_stage < deserialize);
    REQUIRE(deserialize < parse_error);
    REQUIRE(parse_error < failure_clear);
    REQUIRE(failure_clear < failure_return);
    REQUIRE(failure_return < preparing_state);
}

TEST_CASE("net auth: local player count is validated before auth state commits",
          "[net][auth][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcAuthRead");

    const size_t players_read = read.find("const u8 players = netbufReadU8(src)");
    const size_t error_gate = read.find("if (src->error)", players_read);
    const size_t players_gate = read.find("players == 0 || players > MAX_PLAYERS", error_gate);
    const size_t players_return = read.find("return 1;", players_gate);
    const size_t file_check = read.find("romCrc != utilCrc32(g_RomName)", players_return);
    const size_t settings_commit = read.find("srccl->settings", players_return);

    REQUIRE(players_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(players_gate != std::string::npos);
    REQUIRE(players_return != std::string::npos);
    REQUIRE(file_check != std::string::npos);
    REQUIRE(settings_commit != std::string::npos);

    REQUIRE(players_read < error_gate);
    REQUIRE(error_gate < players_gate);
    REQUIRE(players_gate < players_return);
    REQUIRE(players_return < file_check);
    REQUIRE(players_return < settings_commit);
}

TEST_CASE("net auth: local mod paths never replace manifest content agreement",
          "[net][auth][manifest][static][b1033]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcAuthRead");

    const size_t path_read = read.find("const char *modDir = netbufReadStr(src)");
    const size_t malformed_gate = read.find("if (src->error)", path_read);
    const size_t rom_gate = read.find("romCrc != utilCrc32(g_RomName)", malformed_gate);
    const size_t rom_kick = read.find("netServerKick(srccl, DISCONNECT_FILES)", rom_gate);
    const size_t compatibility = read.find("(void)modDir", rom_kick);
    const size_t auth_commit = read.find("srccl->state = CLSTATE_LOBBY", compatibility);
    const size_t catalog_offer = read.find("netDistribServerSendCatalogInfo(srccl)", auth_commit);

    REQUIRE(path_read != std::string::npos);
    REQUIRE(malformed_gate != std::string::npos);
    REQUIRE(rom_gate != std::string::npos);
    REQUIRE(rom_kick != std::string::npos);
    REQUIRE(compatibility != std::string::npos);
    REQUIRE(auth_commit != std::string::npos);
    REQUIRE(catalog_offer != std::string::npos);

    REQUIRE(path_read < malformed_gate);
    REQUIRE(malformed_gate < rom_gate);
    REQUIRE(rom_gate < rom_kick);
    REQUIRE(rom_kick < compatibility);
    REQUIRE(compatibility < auth_commit);
    REQUIRE(auth_commit < catalog_offer);

    REQUIRE(read.find("manifest/READY protocol") != std::string::npos);
    REQUIRE(read.find("authoritative match manifest and ready gate") != std::string::npos);
    REQUIRE(read.find("fsGetModDir()") == std::string::npos);
    REQUIRE(read.find("strcasecmp(modDir") == std::string::npos);
    REQUIRE(read.find("has the wrong mod") == std::string::npos);
}

TEST_CASE("net manifest status: status and hash are validated before ready gate commits",
          "[net][lifecycle][manifest][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcManifestStatusRead");

    const size_t status_read = read.find("const u8  status");
    const size_t missing_read = read.find("const u16 num_missing", status_read);
    const size_t error_gate = read.find("if (src->error)", missing_read);
    const size_t status_gate = read.find("status > MANIFEST_STATUS_DECLINE", error_gate);
    const size_t status_return = read.find("return 1;", status_gate);
    const size_t hash_gate = read.find("manifest_hash != g_ServerManifest.manifest_hash", status_return);
    const size_t hash_return = read.find("return 1;", hash_gate);
    const size_t missing_alloc = read.find("calloc(num_missing", hash_return);
    const size_t missing_loop = read.find("for (s32 mi = 0;", missing_alloc);
    const size_t ready_gate = read.find("if (s_ReadyGate.active && srccl)", missing_loop);
    const size_t ready_commit = read.find("s_ReadyGate.ready_mask |=", ready_gate);

    REQUIRE(status_read != std::string::npos);
    REQUIRE(missing_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(status_gate != std::string::npos);
    REQUIRE(status_return != std::string::npos);
    REQUIRE(hash_gate != std::string::npos);
    REQUIRE(hash_return != std::string::npos);
    REQUIRE(missing_alloc != std::string::npos);
    REQUIRE(missing_loop != std::string::npos);
    REQUIRE(ready_gate != std::string::npos);
    REQUIRE(ready_commit != std::string::npos);

    REQUIRE(status_read < missing_read);
    REQUIRE(missing_read < error_gate);
    REQUIRE(error_gate < status_gate);
    REQUIRE(status_gate < status_return);
    REQUIRE(status_return < hash_gate);
    REQUIRE(hash_gate < hash_return);
    REQUIRE(hash_return < missing_alloc);
    REQUIRE(missing_alloc < missing_loop);
    REQUIRE(missing_loop < ready_gate);
    REQUIRE(ready_gate < ready_commit);
}

TEST_CASE("net bot authority: bot move count is validated before state writes",
          "[net][bot-authority][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcBotMoveRead");

    const size_t count_read = read.find("const u8 count = netbufReadU8(src)");
    const size_t error_gate = read.find("if (src->error)", count_read);
    const size_t count_gate = read.find("count > MAX_BOTS || count > g_BotCount", error_gate);
    const size_t count_return = read.find("return 1;", count_gate);
    const size_t authorized = read.find("const bool authorized", count_return);
    const size_t apply_write = read.find("prop->syncid = syncid;", authorized);

    REQUIRE(count_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(count_gate != std::string::npos);
    REQUIRE(count_return != std::string::npos);
    REQUIRE(authorized != std::string::npos);
    REQUIRE(apply_write != std::string::npos);

    REQUIRE(count_read < error_gate);
    REQUIRE(error_gate < count_gate);
    REQUIRE(count_gate < count_return);
    REQUIRE(count_return < authorized);
    REQUIRE(authorized < apply_write);
}

TEST_CASE("net interoperability: friend joins consume only typed signed match routes",
		  "[net][interoperability][static][c3828][d-003][b1058]")
{
	const std::string group = read_text_file("port/src/net/group_session.c");
	const std::string presence = read_text_file("port/src/presence.c");
	const std::string stun_h = read_text_file("port/include/net/netstun.h");
	const std::string fixture = read_text_file(
		"tools/smoke-verify/generate_friend_play_identity.py");
	const std::string authority_initiator = read_text_file(
		"tools/smoke-verify/tests/friend_play_authority_initiator_smoke.json");
	const std::string authority_invitee = read_text_file(
		"tools/smoke-verify/tests/friend_play_authority_invitee_smoke.json");
	const std::string authority_prefs = read_text_file(
		"tools/smoke-verify/fixtures/prefs_needler_enabled.ini");
    const std::string spectator = read_text_file("port/src/spectator.c");
	const std::string pair_open = function_block(group, "static void onPairOpen");
	const std::string drive = function_block(group, "static void driveMatchTransport");
	const std::string latch = function_block(group,
		"static s32 latchPreconnectAuthority");
	const std::string debug_init = function_block(group,
		"static void debugFriendPlayInit");

    REQUIRE(group.find("#include \"net/netholepunch.h\"") != std::string::npos);
	REQUIRE(drive.find("presencePeerMatchRoute(authority_handle, &route)") != std::string::npos);
	REQUIRE(drive.find("if (!latchPreconnectAuthority()) return;") != std::string::npos);
	REQUIRE(drive.find("s_Session.latched_authority_handle") != std::string::npos);
	REQUIRE(latch.find("s_Session.latched_authority_handle = s_Session.authority_handle") !=
		std::string::npos);
	REQUIRE(latch.find("pre-connect authority latched") != std::string::npos);
	REQUIRE(drive.find("netMatchRouteFormat(&route, addr, sizeof(addr))") != std::string::npos);
    REQUIRE(drive.find("netStartClientWithHolePunch(addr)") != std::string::npos);
    REQUIRE(group.find("netStartClient(addr)") == std::string::npos);
	REQUIRE(pair_open.find("netStartClientWithHolePunch") == std::string::npos);
	REQUIRE(pair_open.find("netStartClient(") == std::string::npos);
	REQUIRE(pair_open.find("snprintf(addr") == std::string::npos);
	REQUIRE(group.find("stunGetDiscoveryPort() == (u16)g_NetServerPort") !=
		std::string::npos);
	REQUIRE(group.find("stunGetNatType() == STUN_NAT_CONE") !=
		std::string::npos);
	REQUIRE(stun_h.find("u16 stunGetDiscoveryPort(void);") !=
		std::string::npos);

	REQUIRE(presence.find("PRESENCE_VERSION          5") != std::string::npos);
	REQUIRE(presence.find("PRESENCE_BODY_LEN         132") != std::string::npos);
	REQUIRE(presence.find("PRESENCE_FRAME_LEN        196") != std::string::npos);
	REQUIRE(presence.find("PRESENCE_MATCH_ROUTE_IPV4_OFFSET 88") != std::string::npos);
	REQUIRE(presence.find("PRESENCE_MATCH_ROUTE_ISSUED_OFFSET 96") != std::string::npos);
	REQUIRE(presence.find("pd-presence-v5") != std::string::npos);
	REQUIRE(presence.find("rejected invalid signed match route") != std::string::npos);
	REQUIRE(presence.find("rejected stale signed match route clear") !=
		std::string::npos);
	REQUIRE(presence.find("match_route_latest_nonce") != std::string::npos);
	REQUIRE(presence.find("netMatchRouteTimestampIsFresh") !=
		std::string::npos);
	REQUIRE(presence.find("kind == PRESENCE_KIND_PING || kind == PRESENCE_KIND_PONG") !=
		std::string::npos);
	REQUIRE(presence.find("const u32 upload_kbps = groupSessionLocalElectionKbps()") !=
		std::string::npos);

	REQUIRE(debug_init.find("smokeHarnessIsActive()") != std::string::npos);
	REQUIRE(debug_init.find("controls ignored outside smoke harness") != std::string::npos);
	REQUIRE(fixture.find("pd-identity.dat") != std::string::npos);
	REQUIRE(fixture.find("pd-social-connect-v1\\n") != std::string::npos);
	REQUIRE(fixture.find("0x7F000001") != std::string::npos);
	REQUIRE(fixture.find("fixtures\" / \"agent_smoke.json") !=
		std::string::npos);
	REQUIRE(fixture.find("agent_template[\"name\"] = role") !=
		std::string::npos);
	REQUIRE(fixture.find("\"version\": 2") == std::string::npos);
	REQUIRE(fixture.find("\"besttimes\":") == std::string::npos);
	/* B-1083: Agent activation owns enabled-mod preference state and therefore
	 * supersedes the machine-level mods-enabled.json fixture.  Only the elected
	 * authority receives a validated legacy sidecar, which the production v2->v3
	 * Agent migration consumes and retires before friend play starts. */
	REQUIRE(authority_prefs.find("[Mods]") != std::string::npos);
	REQUIRE(authority_prefs.find("Enabled=needler") != std::string::npos);
	REQUIRE(authority_initiator.find("prefs_needler_enabled.ini") !=
		std::string::npos);
	REQUIRE(authority_initiator.find("\"dst\": \"prefs_initiator.ini\"") !=
		std::string::npos);
	REQUIRE(authority_initiator.find("prefs_invitee.ini") == std::string::npos);
	REQUIRE(authority_invitee.find("prefs_needler_enabled.ini") !=
		std::string::npos);
	REQUIRE(authority_invitee.find("\"dst\": \"prefs_invitee.ini\"") !=
		std::string::npos);
	REQUIRE(authority_invitee.find("prefs_initiator.ini") == std::string::npos);
	/* B-1084/B-1085/B-1088: mission-map peers can observe different presentation
	 * timing after the same authoritative stage start. Each client crosses the
	 * current stage-ready epoch, then one stateful barrier requires a continuous
	 * gameplay-ready window. While gameplay is false, the exact cutscene aperture
	 * may issue one bounded Skip assist; release is owned by the barrier. A
	 * one-frame gameplay sample and duplicated wall-clock waves are not evidence. */
	for (const std::string *authority : {&authority_initiator, &authority_invitee}) {
		REQUIRE(authority->find("\"timeout_seconds\": 360") !=
			std::string::npos);
		REQUIRE(authority->find(
			"\"at_ms\": 0, \"type\": \"wait_until\", \"condition\": \"network_stage_live\", \"timeout_ms\": 220000") !=
			std::string::npos);
		REQUIRE(authority->find(
			"\"at_ms\": 0, \"type\": \"wait_until\", \"condition\": \"gameplay_ready\", \"timeout_ms\": 60000, \"stable_ms\": 3000, \"assist_action\": \"ACTION_SKIP_CUTSCENE\", \"assist_condition\": \"cutscene_skip_ready\", \"assist_hold_ms\": 900") !=
			std::string::npos);
		REQUIRE(authority->find(
			"SMOKE\\\\.WAIT\\\\.ASSIST: action=72 condition=cutscene_skip_ready outcome=(?:used at_ms=0 waited_ms=\\\\d+ press_count=1 release_count=1|not_needed at_ms=0 waited_ms=\\\\d+ press_count=0 release_count=0)") !=
			std::string::npos);
		REQUIRE(authority->find(
			"SMOKE\\\\.WAIT: satisfied condition=gameplay_ready at_ms=0 timeout_ms=60000 .*stable_ms=3000 stable_elapsed_ms=(?:3\\\\d{3}|[4-9]\\\\d{3}|[1-5]\\\\d{4}) assist_action=72 assist_condition=cutscene_skip_ready assist_hold_ms=900") !=
			std::string::npos);
		REQUIRE(authority->find("cutscene_skip_or_gameplay_ready") ==
			std::string::npos);
		REQUIRE(authority->find(
			"\"type\": \"action\", \"name\": \"ACTION_SKIP_CUTSCENE\"") ==
			std::string::npos);
		REQUIRE(authority->find("\"at_ms\": 750") == std::string::npos);
		REQUIRE(authority->find("\"at_ms\": 2250") == std::string::npos);
		REQUIRE(authority->find("\"at_ms\": 2500") == std::string::npos);
		REQUIRE(authority->find("\"at_ms\": 59000") == std::string::npos);
		REQUIRE(authority->find("\"at_ms\": 105000") == std::string::npos);
		REQUIRE(authority->find("\"at_ms\": 145000") == std::string::npos);
		REQUIRE(authority->find(
			"SMOKE\\\\.WAIT\\\\.ASSIST: action=72 condition=cutscene_skip_ready event=press") !=
			std::string::npos);
		REQUIRE(authority->find(
			"SMOKE\\\\.WAIT\\\\.ASSIST: action=72 condition=cutscene_skip_ready event=release") !=
			std::string::npos);
		REQUIRE(authority->find(
			"MATCH: immutable network launch activeMask=0x[0-9a-f]+ players=2 bots=0") !=
			std::string::npos);
		REQUIRE(authority->find("NET: mpStartMatch server activeMask=") ==
			std::string::npos);
	}

    REQUIRE(spectator.find("#include \"net/netholepunch.h\"") != std::string::npos);
    REQUIRE(spectator.find("netStartClientWithHolePunch(addr)") != std::string::npos);
    REQUIRE(spectator.find("netStartClient(addr)") == std::string::npos);
}

TEST_CASE("net lifecycle: rejected Counter-Op start cannot publish role state",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcLobbyStartRead");
    const std::string roster = function_block(
        netmsg, "static bool netLobbyStartPrepareRoster");
    const std::string commit = function_block(
        netmsg, "static bool netLobbyStartCommit");

    const size_t roster_prepare = read.find(
        "netLobbyStartPrepareRoster(&plan, srccl, room)");
    const size_t manifest_prepare = read.find(
        "netLobbyStartPrepareManifest(src, &plan)", roster_prepare);
    const size_t commit_call = read.find("netLobbyStartCommit(&plan)", manifest_prepare);
    REQUIRE(roster_prepare != std::string::npos);
    REQUIRE(manifest_prepare != std::string::npos);
    REQUIRE(commit_call != std::string::npos);
    REQUIRE(roster_prepare < manifest_prepare);
    REQUIRE(manifest_prepare < commit_call);

    REQUIRE(roster.find("plan->num_players != 2") != std::string::npos);
    REQUIRE(roster.find("plan->anti_client_id == NET_NULL_CLIENT") !=
        std::string::npos);
    REQUIRE(roster.find("plan->anti_client_id == srccl->id") !=
        std::string::npos);
    REQUIRE(roster.find("if (!anti_present) return false;") != std::string::npos);
    REQUIRE(commit.find("if (!lobbyStartTransactionBegin(plan->room_id))") !=
        std::string::npos);
    REQUIRE(commit.find("g_NetCounterOpClientId =") != std::string::npos);
    REQUIRE(read.find("g_NetCounterOpClientId =") == std::string::npos);
}

TEST_CASE("NPC replication shares one complete-owner and transactional resync boundary",
		"[net][npc][replication][resync][static][B-1104][SP-69]")
{
	const std::string header = read_text_file("port/include/net/netmsg.h");
	const std::string net = read_text_file("port/src/net/net.c");
	const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string ready = function_block(
		netmsg, "bool netNpcIsReplicationReady");
	const std::string count = function_block(netmsg, "u32 netNpcCount");
	const std::string checksum = function_block(
		netmsg, "static bool netNpcSnapshotDigest");
	const std::string snapshot_target = function_definition_block(
		netmsg, "static struct prop *netNpcSnapshotTarget");
	const std::string sync_write = function_block(
		netmsg, "u32 netmsgSvcNpcSyncWrite");
	const std::string sync_read = function_block(
		netmsg, "u32 netmsgSvcNpcSyncRead");
	const std::string move_write = function_block(
		netmsg, "u32 netmsgSvcNpcMoveWrite");
	const std::string move_read = function_block(
		netmsg, "u32 netmsgSvcNpcMoveRead");
	const std::string state_write = function_block(
		netmsg, "u32 netmsgSvcNpcStateWrite");
	const std::string state_read = function_block(
		netmsg, "u32 netmsgSvcNpcStateRead");
	const std::string resync_write = function_block(
		netmsg, "u32 netmsgSvcNpcResyncWrite");
	const std::string record_read = function_block(
		netmsg, "static void netmsgNpcResyncRecordRead");
	const std::string record_ready = function_block(
		netmsg, "static bool netmsgNpcResyncRecordIsReady");
	const std::string record_apply = function_block(
		netmsg, "static void netmsgNpcResyncRecordApply");
	const std::string resync_read = function_block(
		netmsg, "u32 netmsgSvcNpcResyncRead");
	const std::string append = function_block(
		net, "static bool netAppendResyncTransaction");
	const std::string npc_append = function_definition_block(
		net, "static bool netAppendNpcResyncTransaction");
	const std::string publish = function_block(
		net, "static bool netServerPublishPendingResyncs");
	const std::string endframe = function_block(net, "void netEndFrame");

	REQUIRE(header.find(
		"bool netNpcIsReplicationReady(const struct chrdata *chr);") !=
		std::string::npos);
	REQUIRE(ready.find("chr->prop") != std::string::npos);
	REQUIRE(ready.find("chr->prop->type != PROPTYPE_CHR") != std::string::npos);
	REQUIRE(ready.find("chr->prop->chr != chr") != std::string::npos);
	REQUIRE(ready.find("chr->aibot") != std::string::npos);
	REQUIRE(ready.find("chr->model") != std::string::npos);
	REQUIRE(ready.find("chr->model->definition") != std::string::npos);
	REQUIRE(ready.find("chr->model->definition->rootnode") != std::string::npos);
	const size_t chrinfo = ready.find("MODELNODETYPE_CHRINFO");
	const size_t rodata = ready.find("!root->rodata", chrinfo);
	const size_t rwdatas = ready.find("!chr->model->rwdatas", rodata);
	REQUIRE(chrinfo != std::string::npos);
	REQUIRE(rodata != std::string::npos);
	REQUIRE(rwdatas != std::string::npos);
	REQUIRE(chrinfo < rodata);
	REQUIRE(rodata < rwdatas);

	for (const std::string *writer : {&count, &checksum, &move_write,
			&state_write, &resync_write}) {
		REQUIRE(writer->find("netNpcIsReplicationReady") != std::string::npos);
	}
	REQUIRE(checksum.find("qsort(ordered") != std::string::npos);
	REQUIRE(checksum.find("chr->prop->syncid") != std::string::npos);
	REQUIRE(checksum.find("netNpcDigestMixU32") != std::string::npos);
	REQUIRE(checksum.find("netNpcFloatBits(chrGetInverseTheta") !=
		std::string::npos);
	REQUIRE(checksum.find("netNpcSnapshotFlags(chr, target)") !=
		std::string::npos);
	REQUIRE(checksum.find("netNpcSnapshotTarget(chr)") != std::string::npos);
	REQUIRE(checksum.find("if (chr->prop->rooms[room] < 0)") !=
		std::string::npos);
	REQUIRE(snapshot_target.find("chr->target < 0") != std::string::npos);
	REQUIRE(snapshot_target.find("target->syncid != 0 ? target : NULL") !=
		std::string::npos);
	REQUIRE(checksum.find("count > 0xffffu") != std::string::npos);
	REQUIRE(sync_write.find(
		"netNpcSnapshotDigest(&count, &checksum)") != std::string::npos);
	REQUIRE(move_write.find("netNpcIsReplicationReady") <
		move_write.find("chrGetInverseTheta"));
	REQUIRE(resync_write.find("netNpcIsReplicationReady") <
		resync_write.find("chrGetInverseTheta"));
	const size_t schedule_first = endframe.find(
		"if (netNpcIsReplicationReady(chr))");
	const size_t schedule_second = endframe.find(
		"if (netNpcIsReplicationReady(chr))", schedule_first + 1);
	REQUIRE(schedule_first != std::string::npos);
	REQUIRE(schedule_second != std::string::npos);
	REQUIRE(endframe.find(
		"chr->prop && chr->prop->type == PROPTYPE_CHR && !chr->aibot") ==
		std::string::npos);

	const size_t resync_count_bound = resync_write.find("count > 0xffffu");
	const size_t resync_first_write = resync_write.find("netbufWriteU8");
	REQUIRE(resync_count_bound != std::string::npos);
	REQUIRE(resync_first_write != std::string::npos);
	REQUIRE(resync_count_bound < resync_first_write);
	for (const std::string *bounded : {&sync_write, &resync_write}) {
		REQUIRE(bounded->find("dst->wp = wp_before") != std::string::npos);
		REQUIRE(bounded->find("dst->error = error_before") !=
			std::string::npos);
	}
	REQUIRE(resync_write.find("emitted++") != std::string::npos);
	REQUIRE(resync_write.find("emitted != count") != std::string::npos);
	REQUIRE(resync_write.find("targetprop = netNpcSnapshotTarget(chr)") !=
		std::string::npos);
	REQUIRE(resync_write.find("netNpcSnapshotFlags(chr, targetprop)") !=
		std::string::npos);

	const size_t move_payload_end = move_read.find(
		"const f32 newsurface_z = netbufReadF32(src);");
	const size_t move_ready = move_read.find(
		"netNpcIsReplicationReady(prop->chr)");
	REQUIRE(move_payload_end != std::string::npos);
	REQUIRE(move_ready != std::string::npos);
	REQUIRE(move_payload_end < move_ready);
	REQUIRE(move_read.find("prop->chr->prop != prop", move_ready) !=
		std::string::npos);
	REQUIRE(move_ready < move_read.find("chrSetLookAngle"));

	const size_t state_payload_end = state_read.find(
		"const u8 fadealpha = netbufReadU8(src);");
	const size_t state_ready = state_read.find(
		"netNpcIsReplicationReady(prop->chr)");
	REQUIRE(state_payload_end != std::string::npos);
	REQUIRE(state_ready != std::string::npos);
	REQUIRE(state_payload_end < state_ready);
	REQUIRE(state_read.find("prop->chr->prop != prop", state_ready) !=
		std::string::npos);

	REQUIRE(record_read.find("const u32 target_syncid = netbufReadU32(src);") !=
		std::string::npos);
	REQUIRE(record_read.find(
		"record->target_valid = target_syncid == 0 || record->targetprop != NULL") !=
		std::string::npos);
	REQUIRE(record_ready.find("netNpcIsReplicationReady(record->prop->chr)") !=
		std::string::npos);
	REQUIRE(record_ready.find("record->prop->chr->prop == record->prop") !=
		std::string::npos);
	REQUIRE(record_ready.find("record->target_valid") != std::string::npos);
	REQUIRE(record_ready.find("record->flags & (1 << 2)") !=
		std::string::npos);
	REQUIRE(record_apply.find("chrSetLookAngle") != std::string::npos);

	const size_t state_gate = resync_read.find(
		"srccl->state < CLSTATE_GAME");
	const size_t first_record_read = resync_read.find(
		"netmsgNpcResyncRecordRead(src, &record)", state_gate);
	const size_t incomplete_reject = resync_read.find("if (!complete)",
		first_record_read);
	const size_t rewind = resync_read.find("src->rp = records_rp",
		incomplete_reject);
	const size_t second_record_read = resync_read.find(
		"netmsgNpcResyncRecordRead(src, &record)", first_record_read + 1);
	const size_t apply_record = resync_read.find(
		"netmsgNpcResyncRecordApply(&record)", second_record_read);
	const size_t applied_digest = resync_read.find(
		"netNpcSnapshotDigest(&applied_count, &applied_checksum)",
		apply_record);
	const size_t pending_snapshot = resync_read.find(
		"s_NetNpcSnapshotValidation.pending = true", applied_digest);
	const size_t reset_desync = resync_read.find(
		"g_NetNpcDesyncCount = 0", pending_snapshot);
	REQUIRE(state_gate != std::string::npos);
	REQUIRE(first_record_read != std::string::npos);
	REQUIRE(incomplete_reject != std::string::npos);
	REQUIRE(rewind != std::string::npos);
	REQUIRE(second_record_read != std::string::npos);
	REQUIRE(apply_record != std::string::npos);
	REQUIRE(applied_digest != std::string::npos);
	REQUIRE(pending_snapshot != std::string::npos);
	REQUIRE(reset_desync != std::string::npos);
	REQUIRE(state_gate < first_record_read);
	REQUIRE(first_record_read < incomplete_reject);
	REQUIRE(incomplete_reject < rewind);
	REQUIRE(rewind < second_record_read);
	REQUIRE(second_record_read < apply_record);
	REQUIRE(apply_record < applied_digest);
	REQUIRE(applied_digest < pending_snapshot);
	REQUIRE(pending_snapshot < reset_desync);
	REQUIRE(resync_read.find("local_count == npccount") != std::string::npos);
	REQUIRE(resync_read.find("!srccl->stage_ready") != std::string::npos);
	REQUIRE(resync_read.find("g_NetStageEpoch == 0") != std::string::npos);
	REQUIRE(resync_read.find(
		"g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_NPCS") !=
		std::string::npos);
	REQUIRE(sync_read.find("const net_npc_snapshot_validation_t applied") !=
		std::string::npos);
	REQUIRE(sync_read.find("applied.stage_epoch == g_NetStageEpoch") !=
		std::string::npos);
	REQUIRE(sync_read.find("applied.tick == tick") != std::string::npos);
	REQUIRE(sync_read.find("applied.count == npccount") != std::string::npos);
	REQUIRE(sync_read.find("applied.checksum == server_checksum") !=
		std::string::npos);

	REQUIRE(append.find("dst->wp = wp_before") != std::string::npos);
	REQUIRE(append.find("dst->error = error_before") != std::string::npos);
	REQUIRE(npc_append.find("g_ObjectiveLastIndex <= 255") !=
		std::string::npos);
	const size_t npc_resync = npc_append.find("netmsgSvcNpcResyncWrite");
	const size_t npc_sync = npc_append.find("netmsgSvcNpcSyncWrite",
		npc_resync);
	REQUIRE(npc_resync != std::string::npos);
	REQUIRE(npc_sync != std::string::npos);
	REQUIRE(npc_resync < npc_sync);
	REQUIRE(npc_append.find("netmsgSvcStageFlagWrite") != std::string::npos);
	REQUIRE(npc_append.find("netmsgSvcObjStatusWrite") != std::string::npos);
	REQUIRE(npc_append.find("dst->wp = wp_before") != std::string::npos);
	REQUIRE(net.find("static u8 s_NetResyncTxnBuf[65536]") !=
		std::string::npos);
	REQUIRE(publish.find("requested = g_NetPendingResyncFlags") !=
		std::string::npos);
	REQUIRE(publish.find(".data = s_NetResyncTxnBuf") !=
		std::string::npos);
	REQUIRE(publish.find("&g_NetMsgRel") == std::string::npos);
	for (const char *writer : {"netmsgSvcChrResyncWrite",
			"netmsgSvcPropResyncWrite", "netmsgSvcPlayerScoresWrite",
			"netAppendNpcResyncTransaction"}) {
		REQUIRE(publish.find(writer) != std::string::npos);
	}
	const size_t baseline_queue = publish.find(
		"netSendToRoom(g_NetMatchRoomId, &wire");
	const size_t baseline_clear = publish.find(
		"g_NetPendingResyncFlags &= (u8)~requested", baseline_queue);
	REQUIRE(baseline_queue != std::string::npos);
	REQUIRE(baseline_clear != std::string::npos);
	REQUIRE(baseline_queue < baseline_clear);
	REQUIRE(publish.find("request retained for retry") != std::string::npos);
	REQUIRE(endframe.find("g_NetPendingResyncFlags &=") == std::string::npos);
	REQUIRE(endframe.find("g_NetPendingResyncFlags = 0") ==
		std::string::npos);
	REQUIRE(endframe.find("netmsgSvcNpcSyncWrite(&g_NetMsgRel)") ==
		std::string::npos);
}

TEST_CASE("net lifecycle: SVC_STAGE_START validates mode before committing state",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcStageStartRead");

    const size_t mode_read = read.find("const u8 mode = netbufReadU8(src)");
    const size_t error_gate = read.find("if (src->error)", mode_read);
    const size_t invalid_mode = read.find("mode != NETGAMEMODE_MP", error_gate);
    const size_t invalid_return = read.find("netStageStartReject", invalid_mode);
    const size_t plan_mode = read.find("plan.mode = mode;", invalid_return);
    const size_t publication = read.find("/* One publication point", plan_mode);
    const size_t mode_commit = read.find("g_NetGameMode = plan.mode;", publication);
    const size_t mp_commit = read.find("g_MpSetup = plan.setup;", mode_commit);
    const size_t mission_commit = read.find("g_MissionConfig = plan.mission;", mp_commit);

    REQUIRE(mode_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(invalid_mode != std::string::npos);
    REQUIRE(invalid_return != std::string::npos);
    REQUIRE(mode_commit != std::string::npos);
    REQUIRE(plan_mode != std::string::npos);
    REQUIRE(publication != std::string::npos);
    REQUIRE(mp_commit != std::string::npos);
    REQUIRE(mission_commit != std::string::npos);

    REQUIRE(mode_read < error_gate);
    REQUIRE(error_gate < invalid_mode);
    REQUIRE(invalid_mode < invalid_return);
    REQUIRE(invalid_return < plan_mode);
    REQUIRE(plan_mode < publication);
    REQUIRE(publication < mode_commit);
    REQUIRE(mode_commit < mp_commit);
    REQUIRE(mp_commit < mission_commit);
}

TEST_CASE("net lifecycle: SVC_STAGE_START rejects missing source client before state access",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcStageStartRead");

    const size_t null_gate = read.find("if (!srccl)");
    const size_t null_return = read.find("return netStageStartReject", null_gate);
    const size_t state_gate = read.find("srccl->state != CLSTATE_LOBBY", null_return);
    const size_t first_payload_read = read.find("plan.net_tick = netbufReadU32(src)");

    REQUIRE(null_gate != std::string::npos);
    REQUIRE(null_return != std::string::npos);
    REQUIRE(state_gate != std::string::npos);
    REQUIRE(first_payload_read != std::string::npos);

    REQUIRE(null_gate < null_return);
    REQUIRE(null_return < state_gate);
    REQUIRE(state_gate < first_payload_read);
}

TEST_CASE("net lifecycle: reconnect stage authority is validated before deferred presentation reset",
          "[net][lifecycle][reconnect][static][b1099]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string lv = read_text_file("src/game/lv.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcStageStartRead");
    const std::string reset = function_block(lv, "bool lvReset(s32 stagenum)");
    const std::string reconnectCommit = function_block(netmsg,
        "u32 netmsgSvcReconnectCommitRead");

    REQUIRE_FALSE(read.empty());
    REQUIRE_FALSE(reset.empty());
    REQUIRE_FALSE(reconnectCommit.empty());

    const size_t finalWireGate = read.find("truncated bot roster");
    const size_t authorityCommit = read.find(
        "netmsgCutsceneAuthorityBeginMatch", finalWireGate);
    const size_t globalPublication = read.find(
        "/* One publication point", authorityCommit);
    const size_t asyncLoad = read.find("mpStartMatch()", globalPublication);
    REQUIRE(finalWireGate != std::string::npos);
    REQUIRE(authorityCommit != std::string::npos);
    REQUIRE(globalPublication != std::string::npos);
    REQUIRE(asyncLoad != std::string::npos);
    REQUIRE(finalWireGate < authorityCommit);
    REQUIRE(authorityCommit < globalPublication);
    REQUIRE(globalPublication < asyncLoad);

    const size_t presentationBoundary = reset.find(
        "playerApplyAuthoritativeStageStartPresentation()");
    const size_t playerAllocation = reset.find("playerReset()",
        presentationBoundary);
    REQUIRE(presentationBoundary != std::string::npos);
    REQUIRE(playerAllocation != std::string::npos);
    REQUIRE(presentationBoundary < playerAllocation);

    REQUIRE(reconnectCommit.find("playerSetTickMode") == std::string::npos);
    REQUIRE(reconnectCommit.find("g_Vars.tickmode") == std::string::npos);
}

TEST_CASE("net lifecycle: SVC_STAGE_START stages tick and RNG until identity is valid",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcStageStartRead");

    const size_t tick_read = read.find("plan.net_tick = netbufReadU32(src)");
    const size_t rng0_read = read.find("plan.rng_seed_0 = netbufReadU64(src)", tick_read);
    const size_t rng1_read = read.find("plan.rng_seed_1 = netbufReadU64(src)", rng0_read);
    const size_t seed_read = read.find("plan.match_seed = netbufReadU32(src)", rng1_read);
    const size_t epoch_read = read.find("plan.stage_epoch = netbufReadU32(src)", seed_read);
    const size_t stage_read = read.find("const u16 stage_session = catalogReadAssetRef(src)", epoch_read);
    const size_t zero_stage = read.find("if (stage_session == 0)", stage_read);
    const size_t zero_return = read.find("return netStageStartReject", zero_stage);
    const size_t unknown_stage = read.find("NET: SVC_STAGE unknown stage session", zero_return);
    const size_t unknown_return = read.find("return netStageStartReject", unknown_stage);
    const size_t mode_read = read.find("const u8 mode = netbufReadU8(src)", unknown_return);
    const size_t invalid_mode = read.find("mode != NETGAMEMODE_MP", mode_read);
    const size_t invalid_return = read.find("return netStageStartReject", invalid_mode);
    const size_t tick_commit = read.find("g_NetTick = plan.net_tick;", invalid_return);
    const size_t rng0_commit = read.find("g_NetRngSeeds[0] = plan.rng_seed_0;", tick_commit);
    const size_t rng1_commit = read.find("g_NetRngSeeds[1] = plan.rng_seed_1;", rng0_commit);
    const size_t latch_commit = read.find("g_NetRngLatch = true;", rng1_commit);
    const size_t seed_commit = read.find("g_NetMatchSeed = plan.match_seed;", latch_commit);
    const size_t epoch_commit = read.find("g_NetStageEpoch = plan.stage_epoch;", seed_commit);
    const size_t mode_commit = read.find("g_NetGameMode = plan.mode;", epoch_commit);

    REQUIRE(tick_read != std::string::npos);
    REQUIRE(rng0_read != std::string::npos);
    REQUIRE(rng1_read != std::string::npos);
    REQUIRE(seed_read != std::string::npos);
    REQUIRE(epoch_read != std::string::npos);
    REQUIRE(stage_read != std::string::npos);
    REQUIRE(zero_stage != std::string::npos);
    REQUIRE(zero_return != std::string::npos);
    REQUIRE(unknown_stage != std::string::npos);
    REQUIRE(unknown_return != std::string::npos);
    REQUIRE(mode_read != std::string::npos);
    REQUIRE(invalid_mode != std::string::npos);
    REQUIRE(invalid_return != std::string::npos);
    REQUIRE(tick_commit != std::string::npos);
    REQUIRE(rng0_commit != std::string::npos);
    REQUIRE(rng1_commit != std::string::npos);
    REQUIRE(latch_commit != std::string::npos);
    REQUIRE(seed_commit != std::string::npos);
    REQUIRE(epoch_commit != std::string::npos);
    REQUIRE(mode_commit != std::string::npos);

    REQUIRE(tick_read < rng0_read);
    REQUIRE(rng0_read < rng1_read);
    REQUIRE(rng1_read < seed_read);
    REQUIRE(seed_read < epoch_read);
    REQUIRE(epoch_read < stage_read);
    REQUIRE(stage_read < zero_stage);
    REQUIRE(zero_stage < zero_return);
    REQUIRE(zero_return < unknown_stage);
    REQUIRE(unknown_stage < unknown_return);
    REQUIRE(unknown_return < mode_read);
    REQUIRE(mode_read < invalid_mode);
    REQUIRE(invalid_mode < invalid_return);
    REQUIRE(invalid_return < tick_commit);
    REQUIRE(tick_commit < rng0_commit);
    REQUIRE(rng0_commit < rng1_commit);
    REQUIRE(rng1_commit < latch_commit);
    REQUIRE(latch_commit < seed_commit);
    REQUIRE(seed_commit < epoch_commit);
    REQUIRE(epoch_commit < mode_commit);

    REQUIRE(read.find("g_NetTick = netbufReadU32(src)") == std::string::npos);
}

TEST_CASE("net lifecycle: SVC_LOBBY_STATE validates mode and status before commit",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcLobbyStateRead");

    const size_t mode_read = read.find("const u8 gamemode = netbufReadU8(src)");
    const size_t status_read = read.find("const u8 status", mode_read);
    const size_t error_gate = read.find("if (src->error)", status_read);
    const size_t mode_check = read.find("gamemode != NETGAMEMODE_MP", error_gate);
    const size_t mode_return = read.find("return 1;", mode_check);
    const size_t status_check = read.find("if (status > 2)", mode_return);
    const size_t status_return = read.find("return 1;", status_check);
    const size_t mode_commit = read.find("g_NetGameMode = gamemode;", status_return);
    const size_t scenario_commit = read.find("g_Lobby.settings.scenario = gamemode;", mode_commit);

    REQUIRE(mode_read != std::string::npos);
    REQUIRE(status_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(mode_check != std::string::npos);
    REQUIRE(mode_return != std::string::npos);
    REQUIRE(status_check != std::string::npos);
    REQUIRE(status_return != std::string::npos);
    REQUIRE(mode_commit != std::string::npos);
    REQUIRE(scenario_commit != std::string::npos);

    REQUIRE(mode_read < status_read);
    REQUIRE(status_read < error_gate);
    REQUIRE(error_gate < mode_check);
    REQUIRE(mode_check < mode_return);
    REQUIRE(mode_return < status_check);
    REQUIRE(status_check < status_return);
    REQUIRE(status_return < mode_commit);
    REQUIRE(mode_commit < scenario_commit);
}

TEST_CASE("net lifecycle: CLC_LOBBY_START rejects over-cap bots before record reads",
          "[net][lifecycle][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcLobbyStartRead");
    const std::string bots = function_block(
        netmsg, "static bool netLobbyStartReadBots");
    const std::string manifest = function_block(
        netmsg, "static bool netLobbyStartPrepareManifest");

    const size_t cap_check = bots.find("advertised_bots > MAX_BOTS");
    const size_t participant_cap = bots.find(
        "plan->num_players + advertised_bots > MATCH_PARTICIPANT_CAP",
        cap_check);
    const size_t bot_loop = bots.find(
        "for (u8 i = 0; i < advertised_bots; i++)", participant_cap);
    const size_t first_record_read = bots.find(
        "const char *name = netbufReadStr(src)", bot_loop);
    REQUIRE(cap_check != std::string::npos);
    REQUIRE(participant_cap != std::string::npos);
    REQUIRE(bot_loop != std::string::npos);
    REQUIRE(first_record_read != std::string::npos);
    REQUIRE(cap_check < participant_cap);
    REQUIRE(participant_cap < bot_loop);
    REQUIRE(bot_loop < first_record_read);

    const size_t roster_prepare = read.find("netLobbyStartPrepareRoster(&plan");
    const size_t bot_prepare = read.find("netLobbyStartReadBots(src, &plan", roster_prepare);
    const size_t manifest_prepare = read.find(
        "netLobbyStartPrepareManifest(src, &plan)", bot_prepare);
    const size_t state_prepare = read.find("netLobbyStartBuildState(&plan)", manifest_prepare);
    const size_t commit = read.find("netLobbyStartCommit(&plan)", state_prepare);
    REQUIRE(roster_prepare < bot_prepare);
    REQUIRE(bot_prepare < manifest_prepare);
    REQUIRE(manifest_prepare < state_prepare);
    REQUIRE(state_prepare < commit);
    REQUIRE(manifest.find("manifestDeserializeStrict(src, &plan->manifest") !=
        std::string::npos);
	REQUIRE(read.find("clampedSims") == std::string::npos);
}

TEST_CASE("B-1073 lobby start uses one canonical zero-bot wire form",
          "[net][lifecycle][bots][wire][static][b1073]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string main_source = read_text_file("port/src/main.c");
    const std::string room = read_text_file("port/fast3d/pdgui_menu_room.cpp");
    const std::string write = function_block(
        netmsg, "static bool netLobbyStartWritePrepare");
    const std::string read = function_block(
        netmsg, "static bool netLobbyStartReadBots");
    const std::string autostart = function_block(
        main_source, "s32 bootHostAutostartTick");
    const std::string lead_type = function_block(room, "static u8 getLeadSimType");

    const size_t count_check = write.find("if (bot_count != (s32)num_sims)");
    const size_t zero_check = write.find("if (bot_count == 0)", count_check);
    const size_t zero_commit = write.find("plan->sim_type = 0;", zero_check);
    const size_t lead_check = write.find(
        "else if (sim_type != plan->bots[0].difficulty)", zero_commit);
    REQUIRE(count_check != std::string::npos);
    REQUIRE(zero_check != std::string::npos);
    REQUIRE(zero_commit != std::string::npos);
    REQUIRE(lead_check != std::string::npos);
    REQUIRE(count_check < zero_check);
    REQUIRE(zero_check < zero_commit);
    REQUIRE(zero_commit < lead_check);

    REQUIRE(read.find("advertised_bots == 0 && plan->sim_type == 0") !=
        std::string::npos);
    REQUIRE(lead_type.find("return 0;") != std::string::npos);
    REQUIRE(lead_type.find("return 2;") == std::string::npos);
    REQUIRE(autostart.find("g_MatchConfig.numSlots > MATCH_MAX_SLOTS") !=
        std::string::npos);
    REQUIRE(autostart.find("slot->type != SLOT_BOT") != std::string::npos);
    const size_t sim_init = autostart.find("u8 simType = 0;");
    const size_t sim_from_bot = autostart.find(
        "simType = slot->botDifficulty;", sim_init);
    const size_t mission_zero = autostart.find(
        "if (mode != NETGAMEMODE_MP)", sim_from_bot);
    const size_t mission_sim_zero = autostart.find(
        "simType = 0;", mission_zero);
    const size_t request = autostart.find(
        "netLobbyRequestStartWithSims(", mission_sim_zero);
    const size_t count_argument = autostart.find("numSims,", request);
    const size_t type_argument = autostart.find("simType,", count_argument);
    REQUIRE(sim_init != std::string::npos);
    REQUIRE(sim_from_bot != std::string::npos);
    REQUIRE(mission_zero != std::string::npos);
    REQUIRE(mission_sim_zero != std::string::npos);
    REQUIRE(request != std::string::npos);
    REQUIRE(count_argument != std::string::npos);
    REQUIRE(type_argument != std::string::npos);
    REQUIRE(sim_init < sim_from_bot);
    REQUIRE(sim_from_bot < mission_zero);
    REQUIRE(mission_zero < mission_sim_zero);
    REQUIRE(mission_sim_zero < request);
    REQUIRE(request < count_argument);
    REQUIRE(count_argument < type_argument);
}

TEST_CASE("net settings: client team changes are sanitized after prepared rollback and before writes",
          "[net][settings][security][static][B-1103]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string codec = read_text_file(
        "port/src/net/net_client_settings_wire.c");
    const std::string sanitize = function_block(netmsg, "static u8 netmsgSanitizeClientTeam");
    const std::string read = function_block(netmsg, "u32 netmsgClcSettingsRead");

    REQUIRE(sanitize.find("wireTeam >= MAX_TEAMS") != std::string::npos);
    REQUIRE(sanitize.find("srccl->state >= CLSTATE_GAME") != std::string::npos);
    REQUIRE(sanitize.find("MPOPTION_TEAMSENABLED") != std::string::npos);
    REQUIRE(sanitize.find("g_MpSetup.options") != std::string::npos);

    const size_t candidate_read = read.find(
        "netClientSettingsWireRead(src, &plan");
    const size_t error_gate = read.find(
        "status != NET_CLIENT_SETTINGS_WIRE_OK", candidate_read);
    const size_t prepared_gate = read.find(
        "if (readyGatePreparingClientIndex(srccl) >= 0)", error_gate);
    const size_t prepared_abort = read.find(
        "readyGateAbort(", prepared_gate);
    const size_t sanitized = read.find(
        "sanitizedTeam = netmsgSanitizeClientTeam(srccl, plan.team)",
        prepared_abort);
    const size_t config_write = read.find("srccl->config->base.team = sanitizedTeam");
    const size_t settings_write = read.find("srccl->settings.team = sanitizedTeam");

    REQUIRE(candidate_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(prepared_gate != std::string::npos);
    REQUIRE(prepared_abort != std::string::npos);
    REQUIRE(sanitized != std::string::npos);
    REQUIRE(config_write != std::string::npos);
    REQUIRE(settings_write != std::string::npos);

    REQUIRE(candidate_read < error_gate);
    REQUIRE(error_gate < prepared_gate);
    REQUIRE(prepared_gate < prepared_abort);
    REQUIRE(prepared_abort < sanitized);
    REQUIRE(sanitized < config_write);
    REQUIRE(config_write < settings_write);

    REQUIRE(read.find("srccl->config->base.team = plan.team") == std::string::npos);
    REQUIRE(read.find("srccl->settings.team = plan.team") == std::string::npos);
    REQUIRE(codec.find("input->team >= MAX_TEAMS") != std::string::npos);
}

TEST_CASE("net room mutations: source client is validated before room state access",
          "[net][room][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");

    auto require_source_guard = [&](const char *signature, const char *first_srccl_use) {
        const std::string read = function_block(netmsg, signature);
        const size_t server_gate = read.find("if (g_NetMode != NETMODE_SERVER)");
        const size_t source_guard = read.find("if (!srccl) return 1", server_gate);
        const size_t first_use = read.find(first_srccl_use, source_guard);

        REQUIRE(server_gate != std::string::npos);
        REQUIRE(source_guard != std::string::npos);
        REQUIRE(first_use != std::string::npos);

        REQUIRE(server_gate < source_guard);
        REQUIRE(source_guard < first_use);
    };

    require_source_guard("u32 netmsgClcRoomCreateRead", "netmsgRoomRateAllow(srccl)");
    require_source_guard("u32 netmsgClcRoomJoinRead", "netmsgRoomRateAllow(srccl)");
    require_source_guard("u32 netmsgClcRoomLeaveRead", "srccl->room_id == 0xFF");
    require_source_guard("u32 netmsgClcRoomSettingsUpdateRead", "srccl->room_id == 0xFF");
    require_source_guard("u32 netmsgClcRoomPlaylistUpdateRead", "srccl->room_id == 0xFF");
}

TEST_CASE("net room create: access mode is rejected before room creation",
          "[net][room][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcRoomCreateRead");

    REQUIRE(read.find("downgrading to OPEN") == std::string::npos);

    const size_t access_gate = read.find("accessRaw > ROOM_ACCESS_INVITE");
    const size_t access_return = read.find("return 1;", access_gate);
    const size_t password_gate = read.find("if (!passwordBuf[0])", access_return);
    const size_t password_return = read.find("return 1;", password_gate);
    const size_t create = read.find("roomCreateConfigured", password_return);

    REQUIRE(access_gate != std::string::npos);
    REQUIRE(access_return != std::string::npos);
    REQUIRE(password_gate != std::string::npos);
    REQUIRE(password_return != std::string::npos);
    REQUIRE(create != std::string::npos);

    REQUIRE(access_gate < access_return);
    REQUIRE(access_return < password_gate);
    REQUIRE(password_gate < password_return);
    REQUIRE(password_return < create);
}

TEST_CASE("net room settings: rebroadcast buffer is rebuilt per recipient",
          "[net][room][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcRoomSettingsUpdateRead");

    REQUIRE(read.find("u8 bcastData[256]") == std::string::npos);
    REQUIRE(read.find("struct netbuf bcast") == std::string::npos);
    REQUIRE(read.find("netSend(ncl, &bcast") == std::string::npos);

    const size_t loop = read.find("for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++)");
    const size_t skip_source = read.find("if (ncl == srccl) continue", loop);
    const size_t start = read.find("netbufStartWrite(&g_NetMsgRel)", skip_source);
    const size_t write = read.find("netmsgSvcRoomSettingsWrite(&g_NetMsgRel", start);
    const size_t encode_guard = read.find("if (g_NetMsgRel.error)", write);
    const size_t send = read.find("netSend(ncl, &g_NetMsgRel, true, NETCHAN_CONTROL)", encode_guard);

    REQUIRE(loop != std::string::npos);
    REQUIRE(skip_source != std::string::npos);
    REQUIRE(start != std::string::npos);
    REQUIRE(write != std::string::npos);
    REQUIRE(encode_guard != std::string::npos);
    REQUIRE(send != std::string::npos);

    REQUIRE(loop < skip_source);
    REQUIRE(skip_source < start);
    REQUIRE(start < write);
    REQUIRE(write < encode_guard);
    REQUIRE(encode_guard < send);
}

TEST_CASE("net room playlist: rebroadcast buffer is rebuilt per recipient",
          "[net][room][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcRoomPlaylistUpdateRead");

    REQUIRE(read.find("struct netbuf bcast") == std::string::npos);
    REQUIRE(read.find("netSend(ncl, &bcast") == std::string::npos);

    const size_t clamp = read.find("char clamped[AUDIO_MAX_PLAYLIST * 65 - 8]");
    const size_t loop = read.find("for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++)", clamp);
    const size_t skip_source = read.find("if (ncl == srccl) continue", loop);
    const size_t start = read.find("netbufStartWrite(&g_NetMsgRel)", skip_source);
    const size_t write = read.find("netmsgSvcRoomPlaylistWrite(&g_NetMsgRel", start);
    const size_t encode_guard = read.find("if (g_NetMsgRel.error)", write);
    const size_t send = read.find("netSend(ncl, &g_NetMsgRel, true, NETCHAN_CONTROL)", encode_guard);

    REQUIRE(clamp != std::string::npos);
    REQUIRE(loop != std::string::npos);
    REQUIRE(skip_source != std::string::npos);
    REQUIRE(start != std::string::npos);
    REQUIRE(write != std::string::npos);
    REQUIRE(encode_guard != std::string::npos);
    REQUIRE(send != std::string::npos);

    REQUIRE(clamp < loop);
    REQUIRE(loop < skip_source);
    REQUIRE(skip_source < start);
    REQUIRE(start < write);
    REQUIRE(write < encode_guard);
    REQUIRE(encode_guard < send);
}

TEST_CASE("net lifecycle: v49 lobby resync replays assignment settings and playlist",
          "[net][lifecycle][room][static][c3813]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcLobbyResyncRead");
    const std::string send = function_block(netmsg, "static void netmsgSendLobbyResyncRoomState");

    const size_t source_guard = read.find("if (src->error || !srccl)");
    const size_t state_gate = read.find("srccl->state < CLSTATE_LOBBY", source_guard);
    const size_t room_id = read.find("const u8 room_id = srccl->room_id", state_gate);
    const size_t replay = read.find("netmsgSendLobbyResyncRoomState(srccl, room_id)", room_id);
    const size_t dirty = read.find("netRoomListMarkDirty();", replay);

    REQUIRE(source_guard != std::string::npos);
    REQUIRE(state_gate != std::string::npos);
    REQUIRE(room_id != std::string::npos);
    REQUIRE(replay != std::string::npos);
    REQUIRE(dirty != std::string::npos);

    REQUIRE(source_guard < state_gate);
    REQUIRE(state_gate < room_id);
    REQUIRE(room_id < replay);
    REQUIRE(replay < dirty);

    const size_t assign = send.find("netmsgSvcRoomAssignWrite(&assignBuf, room_id)");
    const size_t peer_guard = send.find("!dstcl || !dstcl->peer");
    const size_t default_chan = send.find("NETCHAN_DEFAULT", assign);
    const size_t lounge_gate = send.find("if (room_id == 0xFF)", default_chan);
    const size_t bot_count = send.find("netmsgCountCurrentRoomBots()", lounge_gate);
    const size_t settings = send.find("netmsgSvcRoomSettingsWrite(&g_NetMsgRel", bot_count);
    const size_t settings_chan = send.find("NETCHAN_CONTROL", settings);
    const size_t playlist_string = send.find("netmsgBuildCurrentPlaylistString(pl, sizeof(pl))", settings_chan);
    const size_t playlist = send.find("netmsgSvcRoomPlaylistWrite(&g_NetMsgRel, pl)", playlist_string);
    const size_t playlist_chan = send.find("NETCHAN_CONTROL", playlist);

    REQUIRE(peer_guard != std::string::npos);
    REQUIRE(assign != std::string::npos);
    REQUIRE(default_chan != std::string::npos);
    REQUIRE(lounge_gate != std::string::npos);
    REQUIRE(bot_count != std::string::npos);
    REQUIRE(settings != std::string::npos);
    REQUIRE(settings_chan != std::string::npos);
    REQUIRE(playlist_string != std::string::npos);
    REQUIRE(playlist != std::string::npos);
    REQUIRE(playlist_chan != std::string::npos);

    REQUIRE(peer_guard < assign);
    REQUIRE(assign < default_chan);
    REQUIRE(default_chan < lounge_gate);
    REQUIRE(lounge_gate < bot_count);
    REQUIRE(bot_count < settings);
    REQUIRE(settings < settings_chan);
    REQUIRE(settings_chan < playlist_string);
    REQUIRE(playlist_string < playlist);
    REQUIRE(playlist < playlist_chan);
}

TEST_CASE("net lifecycle: listen host return-to-room does not broadcast a client resync opcode",
          "[net][lifecycle][room][static][c3813]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string send = function_block(netmsg, "void netSendLobbyResync");

    const size_t listen_gate = send.find("g_NetMode == NETMODE_SERVER && !g_NetDedicated");
    const size_t dirty = send.find("netRoomListMarkDirty();", listen_gate);
    const size_t local_log = send.find("satisfied locally for listen host", dirty);
    const size_t local_return = send.find("return;", local_log);
    const size_t write_clc = send.find("netmsgClcLobbyResyncWrite(&g_NetMsgRel)", local_return);
    const size_t network_send = send.find("netSend(NULL, &g_NetMsgRel", local_return);

    REQUIRE(listen_gate != std::string::npos);
    REQUIRE(dirty != std::string::npos);
    REQUIRE(local_log != std::string::npos);
    REQUIRE(local_return != std::string::npos);
    REQUIRE(write_clc != std::string::npos);
    REQUIRE(network_send != std::string::npos);

    REQUIRE(listen_gate < dirty);
    REQUIRE(dirty < local_log);
    REQUIRE(local_log < local_return);
    REQUIRE(local_return < write_clc);
    REQUIRE(local_return < network_send);
}

TEST_CASE("net room settings: listen host local loop consumes the client opcode first",
          "[net][room][static][c3813]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");

    const std::string settings = function_block(netmsg, "void netSendRoomSettingsUpdate");
    const size_t settings_listen = settings.find("g_NetMode == NETMODE_SERVER && !g_NetDedicated");
    const size_t settings_read = settings.find("netbufStartReadData(&rb", settings_listen);
    const size_t settings_opcode = settings.find("netbufReadU8(&rb)", settings_read);
    const size_t settings_handler = settings.find("netmsgClcRoomSettingsUpdateRead(&rb, g_NetLocalClient)", settings_opcode);

    REQUIRE(settings_listen != std::string::npos);
    REQUIRE(settings_read != std::string::npos);
    REQUIRE(settings_opcode != std::string::npos);
    REQUIRE(settings_handler != std::string::npos);

    REQUIRE(settings_listen < settings_read);
    REQUIRE(settings_read < settings_opcode);
    REQUIRE(settings_opcode < settings_handler);

    const std::string playlist = function_block(netmsg, "void netSendRoomPlaylistUpdate");
    const size_t playlist_listen = playlist.find("g_NetMode == NETMODE_SERVER && !g_NetDedicated");
    const size_t playlist_read = playlist.find("netbufStartReadData(&rb", playlist_listen);
    const size_t playlist_opcode = playlist.find("netbufReadU8(&rb)", playlist_read);
    const size_t playlist_handler = playlist.find("netmsgClcRoomPlaylistUpdateRead(&rb, g_NetLocalClient)", playlist_opcode);

    REQUIRE(playlist_listen != std::string::npos);
    REQUIRE(playlist_read != std::string::npos);
    REQUIRE(playlist_opcode != std::string::npos);
    REQUIRE(playlist_handler != std::string::npos);

    REQUIRE(playlist_listen < playlist_read);
    REQUIRE(playlist_read < playlist_opcode);
    REQUIRE(playlist_opcode < playlist_handler);
}

TEST_CASE("net lifecycle: room leave aborts ready gate before destroying an empty room",
          "[net][lifecycle][room][static][c3813]")
{
    const std::string room = read_text_file("port/src/room.c");
	const std::string leave = function_block(
		room, "void roomLeave(hub_room_t *room, u8 clientId)");
	const std::string leave_internal = function_block(
		room, "static void roomLeaveInternal");

	const size_t found_gate = leave_internal.find("if (found < 0) return");
	const size_t decrement = leave_internal.find("room->client_count--", found_gate);
	const size_t client_abort = leave_internal.find(
		"netReadyGateOnClientLeft(clientId)", decrement);
	const size_t empty_gate = leave_internal.find(
		"roomOccupiedCount(room) == 0 && room->id != 0", client_abort);
	const size_t room_abort = leave_internal.find(
		"netReadyGateAbortForRoom(room->id, \"Room closed\")", empty_gate);
	const size_t destroy = leave_internal.find("roomDestroy(room)", room_abort);

	REQUIRE(leave.find("roomLeaveInternal(room, clientId, false)") !=
		std::string::npos);
    REQUIRE(found_gate != std::string::npos);
    REQUIRE(decrement != std::string::npos);
    REQUIRE(client_abort != std::string::npos);
    REQUIRE(empty_gate != std::string::npos);
    REQUIRE(room_abort != std::string::npos);
    REQUIRE(destroy != std::string::npos);

    REQUIRE(found_gate < decrement);
    REQUIRE(decrement < client_abort);
    REQUIRE(client_abort < empty_gate);
    REQUIRE(empty_gate < room_abort);
    REQUIRE(room_abort < destroy);
}

TEST_CASE("net lifecycle: endscreen continue preserves connected room and requests resync",
          "[net][lifecycle][room][static][c3813]")
{
    const std::string endscreen = read_text_file("port/fast3d/pdgui_menu_endscreen.cpp");
    const std::string read = function_block(endscreen, "static s32 endscreenGraphMpContinue");

    const size_t network_gate = read.find("g_NetMode != ES_NETMODE_NONE");
    const size_t exit_room = read.find("pdguiEndscreenExitToRoom();", network_gate);
    const size_t in_room = read.find("pdguiSetInRoom(1);", exit_room);
    const size_t resync = read.find("netSendLobbyResync();", in_room);
    const size_t local_exit = read.find("pdguiEndscreenExitToMainMenu();", resync);
    const size_t solo_return = read.find("pdguiSoloRoomReturn();", local_exit);

    REQUIRE(network_gate != std::string::npos);
    REQUIRE(exit_room != std::string::npos);
    REQUIRE(in_room != std::string::npos);
    REQUIRE(resync != std::string::npos);
    REQUIRE(local_exit != std::string::npos);
    REQUIRE(solo_return != std::string::npos);

    REQUIRE(network_gate < exit_room);
    REQUIRE(exit_room < in_room);
    REQUIRE(in_room < resync);
    REQUIRE(resync < local_exit);
    REQUIRE(local_exit < solo_return);
}

TEST_CASE("net lifecycle: ready gate cancel only aborts an active preparing countdown",
          "[net][lifecycle][ready-gate][static][c3813]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string cancel = function_block(netmsg, "s32 netReadyGateCancelByLocalClient");
    const std::string abort = function_definition_block(netmsg, "static void readyGateAbort");

    const size_t mode_gate = cancel.find("g_NetMode != NETMODE_SERVER");
    const size_t null_gate = cancel.find("if (!srccl)", mode_gate);
    const size_t countdown_gate = cancel.find("!s_ReadyGate.active || !s_ReadyGate.countdown_active", null_gate);
    const size_t state_gate = cancel.find("srccl->state != CLSTATE_PREPARING", countdown_gate);
    const size_t abort_call = cancel.find("readyGateAbort(srccl->settings.name[0]", state_gate);

    REQUIRE(mode_gate != std::string::npos);
    REQUIRE(null_gate != std::string::npos);
    REQUIRE(countdown_gate != std::string::npos);
    REQUIRE(state_gate != std::string::npos);
    REQUIRE(abort_call != std::string::npos);

    REQUIRE(mode_gate < null_gate);
    REQUIRE(null_gate < countdown_gate);
    REQUIRE(countdown_gate < state_gate);
    REQUIRE(state_gate < abort_call);

    const size_t room_snapshot = abort.find("const u8 room_id = s_ReadyGate.room_id");
    const size_t expected_snapshot = abort.find(
        "const u32 expected_mask = s_ReadyGate.expected_mask", room_snapshot);
    const size_t active_gate = abort.find("if (!s_ReadyGate.active)", expected_snapshot);
    const size_t transaction_gate = abort.find("if (s_LobbyStartTxn.active)", active_gate);
    const size_t transaction_rollback = abort.find(
        "lobbyStartTransactionRollback(canceller_name)", transaction_gate);
    const size_t preparing_loop = abort.find(
        "g_NetClients[i].state == CLSTATE_PREPARING", transaction_rollback);
    const size_t lobby_state = abort.find("g_NetClients[i].state = CLSTATE_LOBBY", preparing_loop);
    const size_t room_lobby = abort.find("roomTransition(room, ROOM_STATE_LOBBY)", lobby_state);
    const size_t cancel_write = abort.find("netmsgSvcMatchCancelledWrite(&g_NetMsgRel", room_lobby);
    const size_t cancel_send = abort.find(
        "netSendToRoom(room_id, &g_NetMsgRel, true, NETCHAN_CONTROL)", cancel_write);
    const size_t gate_clear = abort.find(
        "memset(&s_ReadyGate, 0, sizeof(s_ReadyGate))", cancel_send);

    REQUIRE(room_snapshot != std::string::npos);
    REQUIRE(expected_snapshot != std::string::npos);
    REQUIRE(active_gate != std::string::npos);
    REQUIRE(transaction_gate != std::string::npos);
    REQUIRE(transaction_rollback != std::string::npos);
    REQUIRE(preparing_loop != std::string::npos);
    REQUIRE(lobby_state != std::string::npos);
    REQUIRE(room_lobby != std::string::npos);
    REQUIRE(cancel_write != std::string::npos);
    REQUIRE(cancel_send != std::string::npos);
    REQUIRE(gate_clear != std::string::npos);

    REQUIRE(room_snapshot < expected_snapshot);
    REQUIRE(expected_snapshot < active_gate);
    REQUIRE(active_gate < transaction_gate);
    REQUIRE(transaction_gate < transaction_rollback);
    REQUIRE(transaction_rollback < preparing_loop);
    REQUIRE(preparing_loop < lobby_state);
    REQUIRE(lobby_state < room_lobby);
    REQUIRE(room_lobby < cancel_write);
    REQUIRE(cancel_write < cancel_send);
    REQUIRE(cancel_send < gate_clear);
    REQUIRE(abort.find("netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL)") ==
        std::string::npos);
}

TEST_CASE("net manifest distribution: failed active transfer declines instead of rechecking forever",
          "[net][manifest][static][c3813]")
{
    const std::string distrib = read_text_file("port/src/net/netdistrib.c");
    const std::string decline = function_block(distrib, "static void netDistribClientDeclineActiveManifest");
    const std::string end = function_block(distrib, "void netDistribClientHandleEnd");

    const size_t client_gate = decline.find("g_NetMode != NETMODE_CLIENT || !g_NetLocalClient");
    const size_t preparing_gate = decline.find("g_NetLocalClient->state != CLSTATE_PREPARING", client_gate);
    const size_t manifest_gate = decline.find("g_ClientManifest.num_entries == 0", preparing_gate);
    const size_t decline_write = decline.find("MANIFEST_STATUS_DECLINE", manifest_gate);
    const size_t decline_send = decline.find("netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL)", decline_write);
    const size_t local_lobby = decline.find("g_NetLocalClient->state = CLSTATE_LOBBY", decline_send);

    REQUIRE(client_gate != std::string::npos);
    REQUIRE(preparing_gate != std::string::npos);
    REQUIRE(manifest_gate != std::string::npos);
    REQUIRE(decline_write != std::string::npos);
    REQUIRE(decline_send != std::string::npos);
    REQUIRE(local_lobby != std::string::npos);

    REQUIRE(client_gate < preparing_gate);
    REQUIRE(preparing_gate < manifest_gate);
    REQUIRE(manifest_gate < decline_write);
    REQUIRE(decline_write < decline_send);
    REQUIRE(decline_send < local_lobby);

    const size_t completed_init = end.find("s32 slot_completed = 0");
    const size_t failure = end.find("if (!success)", completed_init);
    const size_t failure_done = end.find("goto done;", failure);
    const size_t received = end.find("s_ClientStatus.received_count++", failure_done);
    const size_t completed = end.find("slot_completed = 1", received);
    const size_t done_label = end.find("done:", completed);
    const size_t completed_guard = end.find("if (!slot_completed)", done_label);
    const size_t error_state = end.find("DISTRIB_CSTATE_ERROR", completed_guard);
    const size_t decline_call = end.find("netDistribClientDeclineActiveManifest", error_state);
    const size_t received_check = end.find("s_ClientStatus.received_count <", decline_call);
    const size_t waiting = end.find("transfer set awaiting next item", received_check);
    const size_t manifest_recheck = end.find("manifestCheck(&g_ClientManifest)", waiting);

    REQUIRE(completed_init != std::string::npos);
    REQUIRE(failure != std::string::npos);
    REQUIRE(failure_done != std::string::npos);
    REQUIRE(received != std::string::npos);
    REQUIRE(completed != std::string::npos);
    REQUIRE(done_label != std::string::npos);
    REQUIRE(completed_guard != std::string::npos);
    REQUIRE(error_state != std::string::npos);
    REQUIRE(received_check != std::string::npos);
    REQUIRE(waiting != std::string::npos);
    REQUIRE(decline_call != std::string::npos);
    REQUIRE(manifest_recheck != std::string::npos);

    REQUIRE(completed_init < failure);
    REQUIRE(failure < failure_done);
    REQUIRE(failure_done < received);
    REQUIRE(received < completed);
    REQUIRE(completed < done_label);
    REQUIRE(done_label < completed_guard);
    REQUIRE(completed_guard < error_state);
    REQUIRE(error_state < decline_call);
    REQUIRE(decline_call < received_check);
    REQUIRE(received_check < waiting);
    REQUIRE(waiting < manifest_recheck);
}

TEST_CASE("net lifecycle: c3813 listen-host smoke fixtures cover live peer setup",
          "[net][lifecycle][smoke][static][c3813]")
{
    const std::string peer = read_text_file("tools/smoke-verify/tests/listen_host_peer_smoke.json");
    const std::string init = read_text_file("tools/smoke-verify/tests/listen_host_init_smoke.json");
    const std::string runner = read_text_file("tools/smoke-verify/run.ps1");
    const std::string system = read_text_file("port/src/system.c");

    REQUIRE(peer.find("\"scenario_name\": \"listen_host_peer_smoke\"") != std::string::npos);
    REQUIRE(peer.find("\"processes\"") != std::string::npos);
    REQUIRE(peer.find("\"--listen-bind\"") != std::string::npos);
    REQUIRE(peer.find("\"--connect-host\"") != std::string::npos);
    REQUIRE(peer.find("\"wait_for\": \"NET: created server on port 27200\"") != std::string::npos);
    REQUIRE(peer.find("\"wait_timeout_seconds\": 75") != std::string::npos);
    REQUIRE(peer.find("\"timeout_seconds\": 120") != std::string::npos);
    REQUIRE(peer.find("\"at_ms\": 90000") != std::string::npos);
    REQUIRE(peer.find("NET: incoming connection") != std::string::npos);
    REQUIRE(peer.find("NET: connected to server, sending CLC_AUTH") != std::string::npos);
    REQUIRE(peer.find("NET: client slot \\\\d+ assigned to peer") != std::string::npos);
    REQUIRE(peer.find("\"BOOT: --no-net set; netInit\\\\(\\\\) skipped\"") != std::string::npos);
    REQUIRE(peer.find("\"--no-net\"") == std::string::npos);

    REQUIRE(init.find("\"scenario_name\": \"listen_host_init_smoke\"") != std::string::npos);
    REQUIRE(init.find("P2P\\\\.NAT: layer initialised") != std::string::npos);
    REQUIRE(init.find("P2P\\\\.LAN: listener bound on UDP 27101") != std::string::npos);
    REQUIRE(init.find("PRESENCE: socket bound on UDP 27105") != std::string::npos);
    REQUIRE(init.find("\"--no-net\"") == std::string::npos);

    const size_t multi = runner.find("function Invoke-SmokeTestMultiProcess");
    const size_t process_array = runner.find("$def.processes", multi);
    const size_t aggregate = runner.find("$aggLogPath", process_array);
    const size_t append_logs = runner.find("$aggBody.Append($content)", aggregate);
    const size_t assertions = runner.find("Invoke-SmokeAssertions -LogPath $aggLogPath", append_logs);
    REQUIRE(multi != std::string::npos);
    REQUIRE(process_array != std::string::npos);
    REQUIRE(aggregate != std::string::npos);
    REQUIRE(append_logs != std::string::npos);
    REQUIRE(assertions != std::string::npos);
    REQUIRE(multi < process_array);
    REQUIRE(process_array < aggregate);
    REQUIRE(aggregate < append_logs);
    REQUIRE(append_logs < assertions);

    const std::string default_log = function_block(system, "static void sysLogSetDefaultPath");
    const std::string log_printf = function_block(system, "void sysLogPrintf");
    REQUIRE(default_log.find("sysArgCheck(\"--host\")") != std::string::npos);
    REQUIRE(default_log.find("sysLogSetPath(LOG_CLIENT_DIR, \"pd-host.log\")") != std::string::npos);

    const size_t lazy_path = log_printf.find("if (logPath[0] == '\\0')");
    const size_t default_call = log_printf.find("sysLogSetDefaultPath()", lazy_path);
    REQUIRE(lazy_path != std::string::npos);
    REQUIRE(default_call != std::string::npos);
    REQUIRE(lazy_path < default_call);

    REQUIRE(system.find("static s32 sysWeaponDiagLoggingEnabled(void)") != std::string::npos);
    const std::string weapon_diag = function_block(
        system, "static s32 sysWeaponDiagLoggingEnabled(void)");
    REQUIRE(weapon_diag.find("sysArgCheck(\"--debug-weapon-diag\")") !=
        std::string::npos);
    REQUIRE(weapon_diag.find("sysArgCheck(\"--debug-generated-mesh-render-audit\")") !=
        std::string::npos);
    REQUIRE(weapon_diag.find("sysArgCheck(\"--debug-force-first-person\")") ==
        std::string::npos);
    REQUIRE(log_printf.find("strncmp(logmsg, \"LOG.WPN.DIAG:\", 13)") != std::string::npos);
    REQUIRE(log_printf.find("!sysWeaponDiagLoggingEnabled()") != std::string::npos);
    REQUIRE(system.find("#define WEAPON_DIAG_LOG_BUDGET 2048") !=
        std::string::npos);
    REQUIRE(system.find("static SDL_atomic_t s_WeaponDiagLogCount;") !=
        std::string::npos);
    REQUIRE(log_printf.find("SDL_AtomicAdd(&s_WeaponDiagLogCount, 1)") !=
        std::string::npos);
    REQUIRE(log_printf.find("prior_count > WEAPON_DIAG_LOG_BUDGET") !=
        std::string::npos);
    REQUIRE(log_printf.find("output budget exhausted after %d lines") !=
        std::string::npos);
}

TEST_CASE("manifest component identity resolves through the mod registry",
          "[net][manifest][static][b1028]")
{
    const std::string manifest = read_text_file("port/src/net/netmanifest.c");
    const size_t component = manifest.find(
        "if (e->type == MANIFEST_TYPE_COMPONENT)",
        manifest.find("void manifestCheck"));
    const size_t mod_lookup = manifest.find("modmgrFindMod(e->id)", component);
    const size_t catalog_lookup = manifest.find("assetCatalogResolve(e->id)",
        component);
    REQUIRE(component != std::string::npos);
    REQUIRE(mod_lookup != std::string::npos);
    REQUIRE(catalog_lookup != std::string::npos);
    REQUIRE(mod_lookup < catalog_lookup);
    REQUIRE(manifest.find("localMod->enabled && localMod->valid") !=
        std::string::npos);
    REQUIRE(manifest.find("OK (mod registry)") != std::string::npos);
}

TEST_CASE("manifest distribution preserves package identity and waits for the complete set",
          "[net][manifest][distribution][static][b1034]")
{
    const std::string distrib = read_text_file("port/src/net/netdistrib.c");
    const std::string distrib_h = read_text_file("port/include/net/netdistrib.h");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string manifest = read_text_file("port/src/net/netmanifest.c");
    const std::string modmgr = read_text_file("port/src/modmgr.c");
    const std::string modmgr_h = read_text_file("port/include/modmgr.h");
    const std::string net_h = read_text_file("port/include/net/net.h");

    REQUIRE(netmsg.find("netDistribServerHandleManifestDiff(srccl") !=
        std::string::npos);
    REQUIRE(distrib_h.find("netDistribServerHandleManifestDiff") !=
        std::string::npos);
    REQUIRE(distrib.find("entry->type == MANIFEST_TYPE_COMPONENT") !=
        std::string::npos);
    REQUIRE(distrib.find("DISTRIB_QUEUE_PACKAGE") != std::string::npos);
    REQUIRE(distrib.find("buildModPackageArchive(mod, &raw_len)") !=
        std::string::npos);
    REQUIRE(distrib.find("modArchiveGetEntryCount(archive)") !=
        std::string::npos);
    REQUIRE(distrib.find("modArchiveExtractAlloc(archive, i, &size)") !=
        std::string::npos);
    REQUIRE(distrib.find("DISTRIB_PACKAGE_CATEGORY \"pdmod\"") !=
        std::string::npos);

    REQUIRE(manifest.find("netDistribClientBeginManifestTransferSet((u16)num_missing)") !=
        std::string::npos);
    REQUIRE(distrib.find("transfer set awaiting next item") !=
        std::string::npos);
    REQUIRE(distrib.find("received_count <") != std::string::npos);
    REQUIRE(distrib.find("missing_count") != std::string::npos);
    REQUIRE(distrib.find("distribSendEnd(cl, catalog_id, 0)") !=
        std::string::npos);
    REQUIRE(distrib.find("sent_ok ? 1 : 0") != std::string::npos);

    REQUIRE(distrib.find("distribManifestComponentSha(slot->id)") !=
        std::string::npos);
    REQUIRE(distrib.find("modmgrRegisterSessionFolder(destdir, slot->id, package_sha)") !=
        std::string::npos);
    REQUIRE(modmgr_h.find("session_only") != std::string::npos);
    REQUIRE(modmgr_h.find("modmgrRegisterSessionFolder") != std::string::npos);
    REQUIRE(modmgr.find("memcmp(mod->sha256, expected_sha256") !=
        std::string::npos);
    REQUIRE(modmgr.find("mod->session_only = 1") != std::string::npos);
    REQUIRE(modmgr.find("enabled && !g_ModRegistry[i].session_only") !=
        std::string::npos);
	REQUIRE(net_h.find("#define NET_PROTOCOL_VER 58") != std::string::npos);
}

TEST_CASE("client sessions initialize distribution before receiving server packets",
          "[net][distribution][lifecycle][static][b1037]")
{
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string client = function_block(net, "s32 netStartClient(");
    const std::string server = function_block(net, "s32 netStartServer(");

    const size_t client_mode = client.find("g_NetMode = NETMODE_CLIENT");
    const size_t client_lobby = client.find("lobbyInit()", client_mode);
    const size_t client_distrib = client.find("netDistribInit()", client_lobby);
    REQUIRE(client_mode != std::string::npos);
    REQUIRE(client_lobby != std::string::npos);
    REQUIRE(client_distrib != std::string::npos);
    REQUIRE(client_mode < client_lobby);
    REQUIRE(client_lobby < client_distrib);

    const size_t server_lobby = server.find("lobbyInit()");
    const size_t server_distrib = server.find("netDistribInit()", server_lobby);
    REQUIRE(server_lobby != std::string::npos);
    REQUIRE(server_distrib != std::string::npos);
    REQUIRE(server_lobby < server_distrib);
}

TEST_CASE("received catalog identities use collision-free storage names and local persistence policy",
          "[net][distribution][path][static][b1038]")
{
    const std::string distrib = read_text_file("port/src/net/netdistrib.c");
    const std::string distrib_h = read_text_file("port/include/net/netdistrib.h");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");

    REQUIRE(distrib.find("static s32 distribStorageSegment(") != std::string::npos);
    REQUIRE(distrib.find("out[i * 2] = hex[value >> 4]") != std::string::npos);
    REQUIRE(distrib.find("out[i * 2 + 1] = hex[value & 0x0f]") != std::string::npos);
    REQUIRE(distrib.find("temp_root, \"/\", category_segment") != std::string::npos);
    REQUIRE(distrib.find("\"/\", id_segment") != std::string::npos);
    REQUIRE(distrib.find("static s32 s_PendingTemporary = 1") != std::string::npos);
    REQUIRE(distrib.find("s_PendingTemporary = 1;", distrib.find(
        "void netDistribClientBeginManifestTransferSet")) != std::string::npos);
    REQUIRE(distrib_h.find("netDistribClientGetTransferTemporary") != std::string::npos);
    REQUIRE(netmsg.find("netDistribClientGetTransferTemporary()") != std::string::npos);
    REQUIRE(netmsg.find("expected_sha256, 0") == std::string::npos);
}

TEST_CASE("listen host starts one authoritative local and remote Combat Simulator session",
          "[net][match-start][listen-host][static][b1039]")
{
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string lv = read_text_file("src/game/lv.c");
    const std::string start = function_block(net, "s32 netServerStageStart");
    const std::string countdown = function_block(netmsg, "void readyGateTickCountdown");
    const std::string mpstart = function_block(
        read_text_file("src/game/mplayer/mplayer.c"), "void mpStartMatch");

    const size_t game_state = start.find("g_NetClients[ci].state = CLSTATE_GAME");
    const size_t stage_validate = start.find("netmsgSvcStageStartValidate()", game_state);
    const size_t stage_write = start.find("netmsgServerStageStartWrite", stage_validate);
    const size_t stage_send = start.find("netSendToRoom(", stage_write);
    const size_t local_start = start.find("mpStartMatch();", stage_send);

    REQUIRE(game_state != std::string::npos);
    REQUIRE(stage_validate != std::string::npos);
    REQUIRE(stage_write != std::string::npos);
    REQUIRE(local_start != std::string::npos);
    REQUIRE(stage_send != std::string::npos);
    REQUIRE(game_state < stage_validate);
    REQUIRE(stage_validate < stage_write);
    REQUIRE(stage_write < stage_send);
    REQUIRE(stage_send < local_start);
    REQUIRE(start.find("if (g_NetDedicated) {\n\t\tmpStartMatch();") ==
        std::string::npos);
    REQUIRE(countdown.find("launch_result = netServerStageStart();") !=
        std::string::npos);
    REQUIRE(countdown.find("if (launch_result != 0)") != std::string::npos);
    REQUIRE(countdown.find("readyGateAbort(\"Authoritative stage launch rejected\")") !=
        std::string::npos);
    REQUIRE(countdown.find("mainChangeToStage(s_ReadyGate.stagenum)") ==
        std::string::npos);
    REQUIRE(lv.find("netServerStageStart();") == std::string::npos);
    REQUIRE(lv.find("server stage start is emitted by the authoritative lobby/ready") !=
        std::string::npos);

    const size_t network_flag = mpstart.find("const bool network_prepared");
    const size_t offline_gate = mpstart.find("if (!network_prepared)", network_flag);
    const size_t random_apply = mpstart.find("matchConfigSelectWeaponSet", offline_gate);
    const size_t quick_team = mpstart.find("mpConfigureQuickTeamSimulants()", random_apply);
    const size_t stage_resolve = mpstart.find("mpResolveMatchStage()", quick_team);
    const size_t immutable_branch = mpstart.find("} else {", stage_resolve);
    REQUIRE(network_flag != std::string::npos);
    REQUIRE(offline_gate != std::string::npos);
    REQUIRE(random_apply != std::string::npos);
    REQUIRE(quick_team != std::string::npos);
    REQUIRE(stage_resolve != std::string::npos);
    REQUIRE(immutable_branch != std::string::npos);
    REQUIRE(offline_gate < random_apply);
    REQUIRE(random_apply < quick_team);
    REQUIRE(quick_team < stage_resolve);
    REQUIRE(stage_resolve < immutable_branch);
}

TEST_CASE("listen host applies its authoritative manifest before gameplay asset use",
          "[net][manifest][listen-host][static][b1040]")
{
    const std::string manifest = read_text_file("port/src/net/netmanifest.c");
    const std::string manifest_h = read_text_file("port/include/net/netmanifest.h");
    const std::string pdmain = read_text_file("port/src/pdmain.c");
    const std::string select = function_block(
        manifest, "static const match_manifest_t *s_manifestMPTransitionNeeded");
    const std::string count = function_block(
        manifest, "s32 manifestMPTransitionEntryCount");
    const std::string transition = function_block(manifest, "void manifestMPTransition");

    REQUIRE(select.find("g_NetMode == NETMODE_SERVER") != std::string::npos);
    REQUIRE(select.find("return &g_ServerManifest;") != std::string::npos);
    REQUIRE(select.find("return &g_ClientManifest;") != std::string::npos);
    REQUIRE(select.find("g_ServerManifest = g_ClientManifest") == std::string::npos);
    REQUIRE(select.find("g_ClientManifest = g_ServerManifest") == std::string::npos);
    REQUIRE(count.find("s_manifestMPTransitionNeeded(NULL)->num_entries") !=
        std::string::npos);
    REQUIRE(transition.find("const match_manifest_t *needed =") != std::string::npos);
    REQUIRE(transition.find("manifestDiff(&g_CurrentLoadedManifest, needed") !=
        std::string::npos);
    REQUIRE(transition.find("manifestApplyDiff(needed") != std::string::npos);
    REQUIRE(transition.find("&g_ClientManifest, &s_SpLastDiff") == std::string::npos);
    REQUIRE(manifest_h.find("s32 manifestMPTransitionEntryCount(void);") !=
        std::string::npos);
    REQUIRE(pdmain.find("const s32 mpManifestEntries = manifestMPTransitionEntryCount();") !=
        std::string::npos);
    REQUIRE(pdmain.find("if (g_ClientManifest.num_entries > 0)") ==
        std::string::npos);
    const size_t menu_transition = pdmain.find("GAMELOOP.MANIFEST: menu transition");
    const size_t clear_client = pdmain.find(
        "manifestClear(&g_ClientManifest);", menu_transition);
    const size_t clear_server = pdmain.find(
        "manifestClear(&g_ServerManifest);", clear_client);
    REQUIRE(menu_transition != std::string::npos);
    REQUIRE(clear_client != std::string::npos);
    REQUIRE(clear_server != std::string::npos);
    REQUIRE(menu_transition < clear_client);
    REQUIRE(clear_client < clear_server);
}

TEST_CASE("match start preserves the authoritative arena catalog identity",
          "[net][match-start][arena-identity][static][b1041]")
{
    const std::string setup = read_text_file("port/src/net/matchsetup.c");
    const std::string mplayer = read_text_file("src/game/mplayer/mplayer.c");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string manifest = read_text_file("port/src/net/netmanifest.c");
    const std::string prepare = function_block(setup, "static match_start_status_e matchStartPrepare(");
    const std::string commit = function_block(setup, "static void matchStartCommit");
    const std::string start = function_block(setup, "s32 matchStart(void)");
    const std::string mpstart = function_block(mplayer, "void mpStartMatch");
    const std::string resolve_stage = function_block(mplayer, "s32 mpResolveMatchStage");
    const std::string receive_start = function_block(netmsg, "u32 netmsgClcLobbyStartRead");
    const std::string build_state = function_block(
        netmsg, "static bool netLobbyStartBuildState");
    const std::string set_stage = function_block(manifest, "s32 manifestSetStageEntry");

    const size_t resolve = prepare.find("entry = assetCatalogResolve(g_MatchConfig.stage_id)");
    const size_t derive = prepare.find("plan->setup.stagenum =", resolve);
    const size_t preserve = start.find(
        "matchStartCommit(&plan)");

    REQUIRE(resolve != std::string::npos);
    REQUIRE(derive != std::string::npos);
    REQUIRE(preserve != std::string::npos);
    REQUIRE(resolve < derive);
    REQUIRE(prepare.find("strncpy(plan->setup.stage_id, g_MatchConfig.stage_id") !=
        std::string::npos);
    REQUIRE(commit.find("g_MpSetup = plan->setup;") != std::string::npos);

    REQUIRE(mpstart.find("if (!network_prepared)") != std::string::npos);
    REQUIRE(mpstart.find("if (!mpResolveMatchStage())") != std::string::npos);
    REQUIRE(resolve_stage.find("assetCatalogResolve(g_MpSetup.stage_id)") !=
        std::string::npos);
    REQUIRE(resolve_stage.find("selected_stagenum != stagenum") != std::string::npos);
    REQUIRE(resolve_stage.find("const char *sid = catalogStageIdByStagenum(stagenum);") !=
        std::string::npos);
    REQUIRE(resolve_stage.find("strncpy(g_MpSetup.stage_id, sid") != std::string::npos);

    const size_t exact_stage = receive_start.find(
        "catalogResolveStageForType(plan.stage_id,");
    const size_t manifest_prepare = receive_start.find(
        "netLobbyStartPrepareManifest(src, &plan)", exact_stage);
    const size_t build = receive_start.find(
        "netLobbyStartBuildState(&plan)", manifest_prepare);
    const size_t publish = receive_start.find("netLobbyStartCommit(&plan)", build);
    REQUIRE(exact_stage != std::string::npos);
    REQUIRE(manifest_prepare != std::string::npos);
    REQUIRE(build != std::string::npos);
    REQUIRE(publish != std::string::npos);
    REQUIRE(exact_stage < manifest_prepare);
    REQUIRE(manifest_prepare < build);
    REQUIRE(build < publish);
    REQUIRE(build_state.find("plan->setup.stagenum = plan->stagenum") !=
        std::string::npos);
    REQUIRE(build_state.find("plan->match.stagenum = plan->stagenum") !=
        std::string::npos);
    REQUIRE(receive_start.find(
        "plan.mode == NETGAMEMODE_MP ? ASSET_ARENA : ASSET_MAP") !=
        std::string::npos);

    const std::string write_prepare = function_block(
        netmsg, "static bool netLobbyStartWritePrepare");
    REQUIRE(write_prepare.find("catalogResolveStageForType(plan->stage_id,") !=
        std::string::npos);
    REQUIRE(write_prepare.find(
        "gamemode == NETGAMEMODE_MP ? ASSET_ARENA : ASSET_MAP") !=
        std::string::npos);

    REQUIRE(set_stage.find("stage->type != ASSET_ARENA && stage->type != ASSET_MAP") !=
        std::string::npos);
    REQUIRE(set_stage.find("m->entries[i].type != MANIFEST_TYPE_STAGE") !=
        std::string::npos);
    REQUIRE(set_stage.find("m->num_entries--") != std::string::npos);
}

TEST_CASE("network gameplay transition releases lobby menu input ownership",
          "[net][lifecycle][input][listen-host][static][b1042]")
{
    const std::string lobby = read_text_file("port/fast3d/pdgui_lobby.cpp");
    const std::string render = function_block(lobby, "void pdguiLobbyRender");

    const size_t lobby_gate = render.find("if (netLocalClientInLobby())");
    const size_t release_lobby = render.find(
        "menupoolRelease(MENU_TYPE_SOCIAL_LOBBY);", lobby_gate);
    const size_t release_room = render.find(
        "menupoolRelease(MENU_TYPE_ROOM);", release_lobby);
    const size_t sidebar = render.find("renderInGameSidebar", release_room);

    REQUIRE(lobby_gate != std::string::npos);
    REQUIRE(release_lobby != std::string::npos);
    REQUIRE(release_room != std::string::npos);
    REQUIRE(sidebar != std::string::npos);
    REQUIRE(lobby_gate < release_lobby);
    REQUIRE(release_lobby < release_room);
    REQUIRE(release_room < sidebar);
}

TEST_CASE("smoke PDCA ingress suppresses only the absent-peer acknowledgement",
          "[net][distribution][static][b1028]")
{
    const std::string distrib = read_text_file("port/src/net/netdistrib.c");
    REQUIRE(distrib.find("s_SmokeReceiveActive = 1") != std::string::npos);
    REQUIRE(distrib.find("s_SmokeReceiveActive = 0") != std::string::npos);
    REQUIRE(distrib.find("peer manifest acknowledgement suppressed") !=
        std::string::npos);
    REQUIRE(distrib.find("pdcaExtractArchiveBegin") != std::string::npos);
    REQUIRE(distrib.find("assetCatalogScanExternalLayoutFolderDeferred") !=
        std::string::npos);
}

TEST_CASE("received typed-source rejection rolls back zero and negative scanner results",
          "[net][distribution][static][b1027]")
{
    const std::string distrib = read_text_file("port/src/net/netdistrib.c");
    const std::string scanner = read_text_file("port/src/assetcatalog_scanner.c");
    REQUIRE(distrib.find("if (registered <= 0)") != std::string::npos);
    REQUIRE(distrib.find("DISTRIB.CATALOG.ADMISSION: id=%s scanner_result=%d") !=
        std::string::npos);
    REQUIRE(distrib.find("pdcaExtractTransactionRollback(&install_transaction)") !=
        std::string::npos);
    REQUIRE(distrib.find("DISTRIB.CATALOG.ROLLBACK: id=%s result=%d active=%d") !=
        std::string::npos);
    REQUIRE(distrib.find("DISTRIB.CATALOG.RELOAD: id=%s result=%d") !=
        std::string::npos);
    REQUIRE(distrib.find("catalogReloadInvalidatedTypedAssets()") !=
        std::string::npos);
    REQUIRE(scanner.find("return rejected ? -(count + 1) : count;") !=
        std::string::npos);
	REQUIRE(scanner.find("externalScanAccumulate(scanTypedPdDescriptorsRecurse") !=
		std::string::npos);
	REQUIRE(scanner.find("externalScanTransactionRollback(&transaction)") !=
		std::string::npos);
	REQUIRE(scanner.find("if (!defer_reloads) (void)catalogReloadInvalidatedTypedAssets()") !=
        std::string::npos);
}

TEST_CASE("net lifecycle: reconnect and drop-in gates preserve slots before reset",
          "[net][lifecycle][reconnect][static][c3813]")
{
    const std::string net = read_text_file("port/src/net/net.c");
	const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string room = read_text_file("port/src/room.c");
	const std::string room_header = read_text_file("port/include/room.h");
    const std::string connect = function_block(net, "static void netServerEvConnect");
    const std::string disconnect = function_block(net, "static void netServerEvDisconnect");
	const std::string room_can_join = function_definition_block(
		room, "s32 roomCanJoin");
	const std::string room_leave_fn = function_definition_block(
		room, "static void roomLeaveInternal");
	const std::string room_rejoin = function_definition_block(
		room, "s32 roomRejoin");
	const std::string discard = function_definition_block(
		net, "static void netServerDiscardPreservedPlayer");
	const std::string match_progress = function_definition_block(
		net, "s32 netServerMatchInProgress");
	const std::string client_disconnect = function_definition_block(
		net, "static s32 netDisconnectWithIntent");
	const std::string client_reset_fn = function_definition_block(
		net, "static inline void netClientReset");

	const size_t decode = connect.find("netReconnectConnectDataDecode");
	const size_t match_gate = connect.find("netServerMatchInProgress", decode);
	const size_t stable_lookup = connect.find(
		"netServerFindPreservedByClientId(reconnect_client_id)", match_gate);
	const size_t exact_slot = connect.find(
		"cl = &g_NetClients[reconnect_client_id]", stable_lookup);
	const size_t reset = connect.find("netClientReset(cl)", exact_slot);
	const size_t pending = connect.find("cl->reconnect_preserved_index", reset);
	REQUIRE(decode != std::string::npos);
	REQUIRE(match_gate != std::string::npos);
	REQUIRE(stable_lookup != std::string::npos);
	REQUIRE(exact_slot != std::string::npos);
	REQUIRE(reset != std::string::npos);
	REQUIRE(pending != std::string::npos);
	REQUIRE(decode < match_gate);
	REQUIRE(match_gate < stable_lookup);
	REQUIRE(stable_lookup < exact_slot);
	REQUIRE(exact_slot < reset);
	REQUIRE(reset < pending);

	const size_t retry_gate = disconnect.find("netReconnectReasonIsRetryable");
	const size_t preserve = disconnect.find("netServerPreservePlayer(cl)", retry_gate);
	const size_t kill = disconnect.find("playerDie(true)", preserve);
	const size_t room_leave = disconnect.find("roomLeaveForReconnect", kill);
	const size_t client_reset = disconnect.find("netClientReset(cl)", room_leave);
	REQUIRE(retry_gate != std::string::npos);
	REQUIRE(preserve != std::string::npos);
	REQUIRE(kill != std::string::npos);
	REQUIRE(room_leave != std::string::npos);
	REQUIRE(client_reset != std::string::npos);
	REQUIRE(retry_gate < preserve);
	REQUIRE(preserve < kill);
	REQUIRE(kill < room_leave);
	REQUIRE(room_leave < client_reset);
	REQUIRE(disconnect.find("if (cl->state == CLSTATE_GAME") !=
		std::string::npos);
	REQUIRE(disconnect.find(
		"cl->state >= CLSTATE_GAME && cl->settings.name") ==
		std::string::npos);

	const size_t terminal_gate = disconnect.find(
		"if (!retryable && authenticated");
	const size_t terminal_detach = disconnect.find(
		"cl->reconnect_preserved_index = NET_NULL_CLIENT", terminal_gate);
	const size_t terminal_release = disconnect.find(
		"netServerDiscardPreservedPlayer", terminal_detach);
	REQUIRE(terminal_gate != std::string::npos);
	REQUIRE(terminal_detach != std::string::npos);
	REQUIRE(terminal_release != std::string::npos);
	REQUIRE(terminal_gate < terminal_detach);
	REQUIRE(terminal_detach < terminal_release);

	REQUIRE(room_header.find("u32          reconnect_reservation_mask") !=
		std::string::npos);
	REQUIRE(room_can_join.find("roomOccupiedCount(room) >= cap") !=
		std::string::npos);
	const size_t reserve = room_leave_fn.find(
		"room->reconnect_reservation_mask |= 1u << clientId");
	const size_t active_remove = room_leave_fn.find(
		"room->client_count--", reserve);
	REQUIRE(reserve != std::string::npos);
	REQUIRE(active_remove != std::string::npos);
	REQUIRE(reserve < active_remove);
	const size_t consume_reservation = room_rejoin.find(
		"room->reconnect_reservation_mask &= ~(1u << clientId)");
	const size_t restore_membership = room_rejoin.find(
		"room->clients[room->client_count++] = clientId", consume_reservation);
	REQUIRE(consume_reservation != std::string::npos);
	REQUIRE(restore_membership != std::string::npos);
	REQUIRE(consume_reservation < restore_membership);
	REQUIRE(discard.find("roomReleaseReconnectReservation(room, client_id)") !=
		std::string::npos);
	REQUIRE(match_progress.find(
		"g_NetClients[i].state == CLSTATE_GAME") != std::string::npos);
	REQUIRE(match_progress.find(
		"g_NetClients[i].state >= CLSTATE_GAME") == std::string::npos);
	REQUIRE(client_disconnect.find(
		"g_NetLocalClient->state == CLSTATE_GAME") != std::string::npos);
	REQUIRE(client_reset_fn.find("cl->config && cl->config->client == cl") !=
		std::string::npos);
	REQUIRE(client_reset_fn.find("cl->config->client = NULL") !=
		std::string::npos);

	REQUIRE(net.find("netDisconnectWithIntent(netReconnectReasonIsRetryable(reason") !=
		std::string::npos);
	REQUIRE(netmsg.find("netmsgClcAuthEndpointEqual") != std::string::npos);
	REQUIRE(netmsg.find(
		"memcmp(&a->ipv6, &b->ipv6, sizeof(a->ipv6)) == 0") !=
		std::string::npos);
	REQUIRE(netmsg.find("netmsgClcAuthPrepareConnect") != std::string::npos);
	REQUIRE(netmsg.find("netbufWriteData(dst, s_AuthSession.cookie") !=
		std::string::npos);
}

TEST_CASE("net lifecycle: reconnect commits after post-load ack and ordered exact state",
          "[net][lifecycle][reconnect][static][c3813][b1064]")
{
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string netmsg_header = read_text_file("port/include/net/netmsg.h");
    const std::string prepare_candidate = function_block(
        net, "static enum net_restore_result netServerPrepareRestoreCandidate");
    const std::string stage_prepare = function_block(
        net, "enum net_restore_result netServerRestorePreserved");
	const std::string complete = function_block(
		net, "enum net_restore_result netServerCompleteReconnect");
	const std::string targeted_state = function_block(
		net, "s32 netServerSendReconnectState");
	const std::string client_stage_loaded = function_block(
		net, "void netLocalStageLoaded");
	const std::string auth = function_block(netmsg, "u32 netmsgClcAuthRead");
	const std::string auth_write = function_definition_block(
		netmsg, "u32 netmsgClcAuthWrite");
	const std::string settings = function_block(
		netmsg, "u32 netmsgClcSettingsRead");
	const std::string manifest = function_block(
		netmsg, "u32 netmsgClcManifestStatusRead");
	const std::string manifest_ready = function_block(
		netmsg, "static u32 netmsgReconnectManifestReady");
	const std::string stage_ready = function_block(
		netmsg, "u32 netmsgClcStageReadyRead");
	const std::string reconnect_state = function_block(
		netmsg, "u32 netmsgSvcReconnectStateWrite");
	const std::string move_snapshot = function_block(
		netmsg, "static u32 netmsgSvcPlayerMoveSnapshotWrite");
	const std::string reconnect_commit = function_block(
		netmsg, "u32 netmsgSvcReconnectCommitRead");
	const std::string reconnect_props_write = function_block(
		netmsg, "static u32 netmsgSvcReconnectPropsWrite");
	const std::string reconnect_client_mask = function_block(
		netmsg, "static u32 netmsgReconnectValidClientMask");
	const std::string reconnect_world_predicate = function_block(
		netmsg, "static bool netmsgReconnectIsWorldPropCandidate(");
	const std::string reconnect_props_begin = function_block(
		netmsg, "u32 netmsgSvcReconnectPropBeginRead");
	const std::string reconnect_prop_write = function_block(
		netmsg, "static u32 netmsgSvcReconnectPropStateWrite");
	const std::string reconnect_prop_state = function_block(
		netmsg, "u32 netmsgSvcReconnectPropStateRead");
	const std::string reconnect_props_end = function_block(
		netmsg, "u32 netmsgSvcReconnectPropEndRead");
	const std::string reconnect_projectile_write = function_block(
		netmsg, "static u32 netmsgReconnectProjectileWrite");
	const std::string prop_spawn_read = function_block(
		netmsg, "u32 netmsgSvcPropSpawnRead");
	const std::string reconnect_inventory = function_block(
		netmsg, "u32 netmsgSvcReconnectInventoryRead");
	const std::string sync_ids = function_block(
		net, "void netSyncIdsAllocate");
	const std::string prop_dirty = function_block(
		netmsg, "void netPropMarkDirty");
	const std::string cutscene_snapshot = function_definition_block(
		netmsg, "static u32 netmsgReconnectCutsceneAuthorityWrite");
	const std::string stage_roster = function_block(
		netmsg, "static bool netStageStartWritePrepare");
	const std::string client_receive = function_definition_block(
		net, "static void netClientEvReceive");
	const std::string move = function_definition_block(
		netmsg, "u32 netmsgClcMoveRead");
	const std::string cutscene_retire = function_definition_block(
		netmsg, "void netmsgCutsceneAuthorityRetireClient");
	const std::string stage_end = function_definition_block(
		net, "static void netServerCommitPendingStageEnd");
	const std::string stage_end_commit = function_definition_block(
		netmsg, "void netmsgSvcStageEndCommit");

	REQUIRE(auth.find("netmsgReconnectCredentialMatches") != std::string::npos);
	REQUIRE(netmsg_header.find("#define SVC_RECONNECT_COMMIT 0x55") !=
		std::string::npos);
	REQUIRE(netmsg_header.find("#define SVC_RECONNECT_PROP_BEGIN 0x56") !=
		std::string::npos);
	REQUIRE(netmsg_header.find("#define SVC_RECONNECT_PROP_STATE 0x57") !=
		std::string::npos);
	REQUIRE(netmsg_header.find("#define SVC_RECONNECT_PROP_END   0x58") !=
		std::string::npos);
	REQUIRE(netmsg_header.find("#define SVC_RECONNECT_INVENTORY  0x59") !=
		std::string::npos);
	REQUIRE(netmsg_header.find(
		"void netmsgServerPublishAuthenticatedTopology(void);") !=
		std::string::npos);
	REQUIRE(auth.find("settings=pending") != std::string::npos);
	REQUIRE(auth.find("netServerRestorePreserved") == std::string::npos);
	const size_t reconnect_name_gate = auth_write.find("bool reconnecting");
	const size_t frozen_name = auth_write.find(
		"const char *name = g_NetLocalClient->settings.name", reconnect_name_gate);
	const size_t profile_only_fresh = auth_write.find(
		"if (!reconnecting && profile && profile->name[0])", frozen_name);
	REQUIRE(reconnect_name_gate != std::string::npos);
	REQUIRE(frozen_name != std::string::npos);
	REQUIRE(profile_only_fresh != std::string::npos);
	REQUIRE(reconnect_name_gate < frozen_name);
	REQUIRE(frozen_name < profile_only_fresh);
	const size_t admitted_record = auth.find(
		"if (srccl->reconnect_preserved_index < NET_MAX_CLIENTS)");
	const size_t match_boundary = auth.find("} else if (ingame)", admitted_record);
	REQUIRE(admitted_record != std::string::npos);
	REQUIRE(match_boundary != std::string::npos);
	REQUIRE(admitted_record < match_boundary);

	REQUIRE(prepare_candidate.find("cl->state != CLSTATE_PREPARING") !=
		std::string::npos);
	REQUIRE(prepare_candidate.find("!(cl->flags & CLFLAG_ABSENT)") !=
		std::string::npos);
	REQUIRE(prepare_candidate.find("!cl->reconnect_settings_pending") !=
		std::string::npos);
	REQUIRE(prepare_candidate.find("netServerReconnectSettingsMatch") !=
		std::string::npos);
	REQUIRE(prepare_candidate.find("playerIdentityPrepare") !=
		std::string::npos);
	REQUIRE(prepare_candidate.find("out->player->prop->syncid != pp->prop_syncid") !=
		std::string::npos);
	REQUIRE(prepare_candidate.find("roomCanRejoin") != std::string::npos);

	const size_t temporary_publish = stage_prepare.find(
		"cl->state = CLSTATE_GAME");
	const size_t stage_write = stage_prepare.find(
		"netmsgSvcStageReplayWrite", temporary_publish);
	const size_t stage_send = stage_prepare.find(
		"netSend(cl, stage_wire, true, NETCHAN_DEFAULT)", stage_write);
	const size_t restore_temporary = stage_prepare.find(
		"restore_temporary_publication:", stage_send);
	const size_t restore_state = stage_prepare.find(
		"cl->state = client_state_before", restore_temporary);
	const size_t arm_post_load = stage_prepare.find(
		"cl->reconnect_resync_pending = true", restore_state);
	REQUIRE(temporary_publish != std::string::npos);
	REQUIRE(stage_write != std::string::npos);
	REQUIRE(stage_send != std::string::npos);
	REQUIRE(restore_temporary != std::string::npos);
	REQUIRE(restore_state != std::string::npos);
	REQUIRE(arm_post_load != std::string::npos);
	REQUIRE(temporary_publish < stage_write);
	REQUIRE(stage_write < stage_send);
	REQUIRE(stage_send < restore_temporary);
	REQUIRE(restore_temporary < restore_state);
	REQUIRE(restore_state < arm_post_load);
	REQUIRE(stage_prepare.find("roomRejoin") == std::string::npos);
	REQUIRE(stage_prepare.find("pp->active = false") == std::string::npos);
	REQUIRE(stage_prepare.find("commit=pending_post_load") !=
		std::string::npos);

	const size_t room_join = complete.find("roomRejoin");
	const size_t config_commit = complete.find(
		"g_PlayerConfigsArray[pp->playernum] = candidate.config", room_join);
	const size_t game_state = complete.find(
		"cl->state = CLSTATE_GAME", config_commit);
	const size_t targeted_send = complete.find(
		"netServerSendReconnectState(cl)", game_state);
	const size_t rollback_room = complete.find(
		"*candidate.room = room_before", targeted_send);
	const size_t consume_record = complete.find(
		"memset(pp, 0, sizeof(*pp))", targeted_send);
	const size_t commit_log = complete.find(
		"PLAYER.INIT.COMMIT reconnect", consume_record);
	REQUIRE(room_join != std::string::npos);
	REQUIRE(config_commit != std::string::npos);
	REQUIRE(game_state != std::string::npos);
	REQUIRE(targeted_send != std::string::npos);
	REQUIRE(rollback_room != std::string::npos);
	REQUIRE(consume_record != std::string::npos);
	REQUIRE(commit_log != std::string::npos);
	REQUIRE(room_join < config_commit);
	REQUIRE(config_commit < game_state);
	REQUIRE(game_state < targeted_send);
	REQUIRE(targeted_send < rollback_room);
	REQUIRE(targeted_send < consume_record);
	REQUIRE(consume_record < commit_log);
	REQUIRE(complete.find("NET_RESTORE_STATE_SEND_FAILED") !=
		std::string::npos);

	const size_t state_write = targeted_state.find(
		"netmsgSvcReconnectStateWrite");
	const size_t state_send = targeted_state.find(
		"netSend(dstcl, &wire, true, NETCHAN_DEFAULT)", state_write);
	const size_t state_consume = targeted_state.find(
		"reconnect_resync_pending = false", state_send);
	const size_t witness_arm = targeted_state.find(
		"reconnect_gameplay_witness_pending = true", state_consume);
	REQUIRE(targeted_state.find("malloc(NET_BUFSIZE)") != std::string::npos);
	REQUIRE(state_write != std::string::npos);
	REQUIRE(state_send != std::string::npos);
	REQUIRE(state_consume != std::string::npos);
	REQUIRE(witness_arm != std::string::npos);
	REQUIRE(state_write < state_send);
	REQUIRE(state_send < state_consume);
	REQUIRE(state_consume < witness_arm);
	REQUIRE(targeted_state.find("g_NetPendingResyncFlags") ==
		std::string::npos);

	REQUIRE(reconnect_state.find(
		"netmsgReconnectCutsceneAuthorityWrite(dst)") != std::string::npos);
	REQUIRE(reconnect_state.find("netServerFindPreservedByClientId") !=
		std::string::npos);
	REQUIRE(reconnect_state.find("struct netclient absent") !=
		std::string::npos);
	const size_t exact_world = reconnect_state.find(
		"netmsgSvcReconnectPropsWrite");
	const size_t exact_inventory = reconnect_state.find(
		"netmsgSvcReconnectInventoryWrite", exact_world);
	const size_t exact_players = reconnect_state.find(
		"netmsgSvcPlayerStatsWrite", exact_inventory);
	const size_t exact_chrs = reconnect_state.find(
		"netmsgSvcChrResyncWrite", exact_players);
	REQUIRE(exact_world != std::string::npos);
	REQUIRE(exact_inventory != std::string::npos);
	REQUIRE(exact_players != std::string::npos);
	REQUIRE(exact_chrs != std::string::npos);
	REQUIRE(exact_world < exact_inventory);
	REQUIRE(exact_inventory < exact_players);
	REQUIRE(exact_players < exact_chrs);
	REQUIRE(reconnect_state.find("netmsgSvcPropResyncWrite") ==
		std::string::npos);
	REQUIRE(reconnect_state.find("netmsgSvcPlayerMoveSnapshotWrite") !=
		std::string::npos);
	REQUIRE(move_snapshot.find("UCMD_FL_FORCEPOS") != std::string::npos);
	REQUIRE(move_snapshot.find("UCMD_FL_FORCEANGLE") != std::string::npos);
	REQUIRE(move_snapshot.find("UCMD_FL_FORCEGROUND") != std::string::npos);
	REQUIRE(move_snapshot.find("move.weaponnum =") != std::string::npos);
	REQUIRE(move_snapshot.find("player->ucmd") == std::string::npos);

	REQUIRE(reconnect_props_write.find("SVC_RECONNECT_PROP_BEGIN") !=
		std::string::npos);
	REQUIRE(netmsg.find("_Static_assert(NET_MAX_CLIENTS > 0 && NET_MAX_CLIENTS <= 32") !=
		std::string::npos);
	REQUIRE(reconnect_client_mask.find("#if NET_MAX_CLIENTS >= 32") !=
		std::string::npos);
	REQUIRE(reconnect_client_mask.find("return ~(u32)0;") !=
		std::string::npos);
	REQUIRE(netmsg.find("~((1u << NET_MAX_CLIENTS) - 1u)") ==
		std::string::npos);
	REQUIRE(reconnect_props_write.find("g_NetFirstDynamicSyncId") !=
		std::string::npos);
	REQUIRE(reconnect_props_write.find("netmsgSvcPropSpawnWrite") !=
		std::string::npos);
	REQUIRE(prop_spawn_read.find("if (!s_ReconnectPropReceive.active") !=
		std::string::npos);
	REQUIRE(prop_spawn_read.find("must not replay an already-consumed throw effect") !=
		std::string::npos);
	REQUIRE(reconnect_props_write.find("netmsgSvcReconnectPropStateWrite") !=
		std::string::npos);
	REQUIRE(reconnect_props_write.find("SVC_RECONNECT_PROP_END") !=
		std::string::npos);
	REQUIRE(reconnect_world_predicate.find(
		"prop->syncid < g_NetFirstDynamicSyncId") != std::string::npos);
	REQUIRE(reconnect_world_predicate.find(
		"netmsgReconnectCanSpawnDynamicProp(prop)") != std::string::npos);
	REQUIRE(reconnect_props_begin.find("first_dynamic_syncid != g_NetFirstDynamicSyncId") !=
		std::string::npos);
	REQUIRE(reconnect_props_begin.find("netmsgReconnectRemoveWorldProp") !=
		std::string::npos);
	REQUIRE(reconnect_props_begin.find(
		"NET_RECONNECT_LIFECYCLE_PRUNED_PARENT") != std::string::npos);
	REQUIRE(reconnect_props_begin.find("propDetach(prop)") !=
		std::string::npos);
	REQUIRE(reconnect_props_begin.find("netSyncIdMapRebuild") !=
		std::string::npos);
	REQUIRE(reconnect_prop_write.find("attachment_mtx_index") !=
		std::string::npos);
	REQUIRE(reconnect_prop_write.find("modelFindNodeMtxIndex") !=
		std::string::npos);
	REQUIRE(reconnect_prop_write.find("prop->obj->shadecol") !=
		std::string::npos);
	REQUIRE(reconnect_prop_write.find("prop->obj->model->scale") !=
		std::string::npos);
	REQUIRE(reconnect_prop_write.find("weapon->dualweapon->base.prop") !=
		std::string::npos);
	REQUIRE(reconnect_projectile_write.find(
		"NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY") != std::string::npos);
	REQUIRE(reconnect_prop_state.find("netmsgReconnectApplyProjectile") !=
		std::string::npos);
	REQUIRE(reconnect_prop_state.find("parent_ids_by_slot") !=
		std::string::npos);
	REQUIRE(reconnect_props_end.find("refs=resolved") != std::string::npos);
	REQUIRE(reconnect_props_end.find("projectile->ownerprop") !=
		std::string::npos);
	REQUIRE(reconnect_props_end.find("modelFindNodeByMtxIndex") !=
		std::string::npos);
	REQUIRE(reconnect_props_end.find("weapons_held[held_slot]") !=
		std::string::npos);
	REQUIRE(reconnect_props_end.find("weapon_dual_id") !=
		std::string::npos);
	REQUIRE(reconnect_props_end.find("propDetach(prop)") !=
		std::string::npos);
	REQUIRE(reconnect_inventory.find("invClear()") != std::string::npos);
	REQUIRE(reconnect_inventory.find("invInsertItem(item)") !=
		std::string::npos);
	REQUIRE(reconnect_inventory.find("inventory_received_mask") !=
		std::string::npos);

	REQUIRE(sync_ids.find("g_Vars.freeprops") != std::string::npos);
	REQUIRE(sync_ids.find("for (s32 i = 0; i < g_Vars.maxprops; ++i)") !=
		std::string::npos);
	REQUIRE(sync_ids.find("g_Vars.props[i].syncid = (u32)i + 1") !=
		std::string::npos);
	REQUIRE(sync_ids.find("max_initial_syncid + 1") != std::string::npos);
	REQUIRE(sync_ids.find("g_Vars.activeprops") == std::string::npos);
	REQUIRE(prop_dirty.find("netSyncIdLookup(syncid)") !=
		std::string::npos);
	REQUIRE(prop_dirty.find("NET_PROP_DIRTY_MAXSYNCID") ==
		std::string::npos);
	const size_t scores = reconnect_state.find("netmsgSvcPlayerScoresWrite");
	const size_t marker = reconnect_state.find(
		"netmsgSvcReconnectCommitWrite", scores);
	REQUIRE(scores != std::string::npos);
	REQUIRE(marker != std::string::npos);
	REQUIRE(scores < marker);

	REQUIRE(cutscene_snapshot.find(
		"NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE") != std::string::npos);
	REQUIRE(cutscene_snapshot.find(
		"s_CutsceneAuthority.full_client_mask") != std::string::npos);
	REQUIRE(cutscene_snapshot.find(
		"s_CutsceneAuthority.tracker.generation") != std::string::npos);
	REQUIRE(cutscene_snapshot.find(
		"s_CutsceneAuthority.accepted_client_mask") != std::string::npos);
	REQUIRE(cutscene_snapshot.find("netmsgWriteCutsceneState") !=
		std::string::npos);
	REQUIRE(cutscene_snapshot.find("netmsgSvcCutsceneSkipWrite") !=
		std::string::npos);

	REQUIRE(stage_roster.find("netServerFindPreservedByClientId") !=
		std::string::npos);
	REQUIRE(stage_roster.find("preserved->playernum, CLFLAG_ABSENT") !=
		std::string::npos);
	REQUIRE(stage_roster.find(
		"participant mask and authoritative roster disagree") !=
		std::string::npos);
	REQUIRE(netmsg.find(
		"netmsgSvcStageStartWriteInternal(dst, false, false)") !=
		std::string::npos);

	const size_t exact_candidate = settings.find(
		"netServerReconnectSettingsMatch");
	const size_t context = settings.find("netmsgReconnectContextPrepare",
		exact_candidate);
	const size_t pending_plan = settings.find(
		"srccl->reconnect_settings_plan = plan", context);
	const size_t context_send = settings.find("netmsgReconnectContextSend",
		pending_plan);
	REQUIRE(settings.find("netServerRestorePreserved") == std::string::npos);
	REQUIRE(exact_candidate != std::string::npos);
	REQUIRE(context != std::string::npos);
	REQUIRE(pending_plan != std::string::npos);
	REQUIRE(context_send != std::string::npos);
	REQUIRE(exact_candidate < context);
	REQUIRE(context < pending_plan);
	REQUIRE(pending_plan < context_send);

	const size_t reconnect_gate = manifest.find(
		"if (srccl && srccl->reconnect_settings_pending)");
	const size_t hash_gate = manifest.find(
		"manifest_hash != srccl->reconnect_manifest_hash", reconnect_gate);
	const size_t transfer = manifest.find(
		"netDistribServerHandleManifestDiff", hash_gate);
	const size_t ready_call = manifest.find(
		"netmsgReconnectManifestReady(srccl)", reconnect_gate);
	REQUIRE(reconnect_gate != std::string::npos);
	REQUIRE(hash_gate != std::string::npos);
	REQUIRE(transfer != std::string::npos);
	REQUIRE(ready_call != std::string::npos);
	REQUIRE(reconnect_gate < hash_gate);
	REQUIRE(hash_gate < ready_call);
	REQUIRE(hash_gate < transfer);

	const size_t restore_call = manifest_ready.find(
		"netServerRestorePreserved");
	const size_t full_stage_capacity = manifest_ready.find(
		"malloc(NET_BUFSIZE)");
	const size_t send_failure = manifest_ready.find(
		"netRestoreResultRequiresRetryableClose(restore_result)",
		restore_call);
	const size_t retryable_close = manifest_ready.find(
		"DISCONNECT_TIMEOUT", send_failure);
	REQUIRE(restore_call != std::string::npos);
	REQUIRE(full_stage_capacity != std::string::npos);
	REQUIRE(full_stage_capacity < restore_call);
	REQUIRE(send_failure != std::string::npos);
	REQUIRE(retryable_close != std::string::npos);
	REQUIRE(restore_call < send_failure);
	REQUIRE(send_failure < retryable_close);
	REQUIRE(manifest_ready.find("netmsgReconnectPreparedSend") ==
		std::string::npos);
	REQUIRE(manifest_ready.find("status=stage_queued") != std::string::npos);
	REQUIRE(manifest_ready.find("reservation_retained=1") !=
		std::string::npos);
	REQUIRE(manifest_ready.find("restore=committed") == std::string::npos);
	REQUIRE(manifest_ready.find("netmsgServerPublishAuthenticatedTopology") ==
		std::string::npos);

	const size_t pending_ready = stage_ready.find(
		"if (srccl->reconnect_resync_pending)");
	const size_t complete_ready = stage_ready.find(
		"netServerCompleteReconnect(srccl)", pending_ready);
	const size_t game_ready = stage_ready.find(
		"srccl->state != CLSTATE_GAME", complete_ready);
	REQUIRE(pending_ready != std::string::npos);
	REQUIRE(complete_ready != std::string::npos);
	REQUIRE(game_ready != std::string::npos);
	REQUIRE(pending_ready < complete_ready);
	REQUIRE(complete_ready < game_ready);
	REQUIRE(stage_ready.find(
		"netRestoreResultRequiresRetryableClose(restore_result)") !=
		std::string::npos);

	REQUIRE(client_stage_loaded.find("netmsgClcStageReadyWrite") !=
		std::string::npos);
	REQUIRE(client_stage_loaded.find(
		"netSend(g_NetLocalClient, &wire, true, NETCHAN_DEFAULT)") !=
		std::string::npos);
	REQUIRE(client_stage_loaded.find("netClientReconnectCommitAccepted") ==
		std::string::npos);
	REQUIRE(reconnect_commit.find("client_id != srccl->id") !=
		std::string::npos);
	REQUIRE(reconnect_commit.find("!srccl->stage_ready") !=
		std::string::npos);
	REQUIRE(reconnect_commit.find("!s_ReconnectPropReceive.complete") !=
		std::string::npos);
	REQUIRE(reconnect_commit.find("inventory_received_mask") !=
		std::string::npos);
	REQUIRE(reconnect_commit.find("netClientReconnectCommitAccepted()") !=
		std::string::npos);
	REQUIRE(client_receive.find("case SVC_RECONNECT_PROP_BEGIN") !=
		std::string::npos);
	REQUIRE(client_receive.find("case SVC_RECONNECT_PROP_STATE") !=
		std::string::npos);
	REQUIRE(client_receive.find("case SVC_RECONNECT_PROP_END") !=
		std::string::npos);
	REQUIRE(client_receive.find("case SVC_RECONNECT_INVENTORY") !=
		std::string::npos);
	REQUIRE(client_receive.find("case SVC_RECONNECT_COMMIT") !=
		std::string::npos);

	const size_t malformed_log = client_receive.find(
		"malformed or unknown message");
	const size_t stage_fail_gate = client_receive.find(
		"msgid == SVC_STAGE_START", malformed_log);
	const size_t terminal_close = client_receive.find(
		"enet_peer_disconnect(cl->peer, DISCONNECT_FILES)", stage_fail_gate);
	REQUIRE(malformed_log != std::string::npos);
	REQUIRE(stage_fail_gate != std::string::npos);
	REQUIRE(terminal_close != std::string::npos);
	REQUIRE(malformed_log < stage_fail_gate);
	REQUIRE(stage_fail_gate < terminal_close);
	REQUIRE(client_receive.find("msgid == SVC_MATCH_MANIFEST") !=
		std::string::npos);
	REQUIRE(client_receive.find("msgid == SVC_SESSION_CATALOG") !=
		std::string::npos);
	REQUIRE(client_receive.find("msgid == SVC_ROOM_ASSIGN") !=
		std::string::npos);
	REQUIRE(client_receive.find("s_NetReconnectAttempt.valid") !=
		std::string::npos);

	const size_t stale_move_gate = move.find(
		"newmove.tick - srccl->inmove[0].tick");
	const size_t move_commit = move.find("srccl->inmove[0] = newmove",
		stale_move_gate);
	const size_t fire_witness = move.find(
		"srccl->reconnect_gameplay_witness_pending", move_commit);
	const size_t authoritative_fire = move.find(
		"NET.RECONNECT.GAMEPLAY", fire_witness);
	REQUIRE(stale_move_gate != std::string::npos);
	REQUIRE(move_commit != std::string::npos);
	REQUIRE(fire_witness != std::string::npos);
	REQUIRE(authoritative_fire != std::string::npos);
	REQUIRE(stale_move_gate < move_commit);
	REQUIRE(move_commit < fire_witness);
	REQUIRE(fire_witness < authoritative_fire);
	REQUIRE(move.find("newmove.ucmd & UCMD_FIRE", fire_witness) !=
		std::string::npos);

	/* A transport retirement clears only transient skip state. The frozen
	 * match roster remains authoritative and becomes usable again after the
	 * exact client/player slot is restored. */
	REQUIRE(cutscene_retire.find("accepted_client_mask &=") !=
		std::string::npos);
	REQUIRE(cutscene_retire.find("participant_count =") ==
		std::string::npos);
	REQUIRE(cutscene_retire.find("match_active = false") ==
		std::string::npos);

	const size_t end_commit = stage_end.find(
		"netmsgSvcStageEndCommit(room_id, mode)");
	const size_t reservations_clear = stage_end.find(
		"netServerClearPreservedPlayers(\"stage_end_commit\")", end_commit);
	const size_t room_scope_clear = stage_end.find(
		"g_NetMatchRoomId = 0xFF", reservations_clear);
	REQUIRE(end_commit != std::string::npos);
	REQUIRE(reservations_clear != std::string::npos);
	REQUIRE(room_scope_clear != std::string::npos);
	REQUIRE(end_commit < reservations_clear);
	REQUIRE(reservations_clear < room_scope_clear);
	REQUIRE(stage_end_commit.find(
		"reconnect_resync_pending = false") !=
		std::string::npos);
	REQUIRE(stage_end_commit.find(
		"reconnect_gameplay_witness_pending = false") !=
		std::string::npos);
}

TEST_CASE("B-1096 reconnect snapshot preserves typed failure ownership",
		  "[net][lifecycle][reconnect][static][b1064][b1096]")
{
	const std::string net = read_text_file("port/src/net/net.c");
	const std::string net_header = read_text_file("port/include/net/net.h");
	const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string netmsg_header = read_text_file("port/include/net/netmsg.h");
	const std::string propobj = read_text_file("src/game/propobj.c");
	const std::string reconnect = read_text_file(
		"port/src/net/net_reconnect.c");
	const std::string projectile_init = function_definition_block(
		propobj, "void func0f0685e4");
	const std::string projectile_write = function_definition_block(
		netmsg, "static u32 netmsgReconnectProjectileWrite");
	const std::string world_prop_filter = function_definition_block(
		netmsg, "static bool netmsgReconnectIsWorldProp(");
	const std::string prop_state_write = function_definition_block(
		netmsg, "static u32 netmsgSvcReconnectPropStateWrite");
	const std::string state_write = function_definition_block(
		netmsg, "u32 netmsgSvcReconnectStateWrite");
	const std::string state_send = function_definition_block(
		net, "s32 netServerSendReconnectState");
	const std::string retry_policy = function_definition_block(
		net, "s32 netRestoreResultRequiresRetryableClose");
	const std::string manifest_ready = function_definition_block(
		netmsg, "static u32 netmsgReconnectManifestReady");
	const std::string stage_ready = function_definition_block(
		netmsg, "u32 netmsgClcStageReadyRead");

	/* The shared allocator/reset helper owns both sides of the relation. This
	 * covers death drops, ordinary falls, and fallaway doors rather than
	 * weakening the reconnect validator for one fixture. */
	const size_t direct_projectile = projectile_init.find(
		"projectile = obj->projectile");
	const size_t owner_binding = projectile_init.find(
		"projectile->obj = obj", direct_projectile);
	REQUIRE(projectile_init.find(
		"projectile = obj->embedment->projectile") != std::string::npos);
	REQUIRE(direct_projectile != std::string::npos);
	REQUIRE(owner_binding != std::string::npos);
	REQUIRE(direct_projectile < owner_binding);

	/* Exact-state validation remains fail closed and names the broken prop. */
	REQUIRE(projectile_write.find("projectile->obj != prop->obj") !=
		std::string::npos);
	REQUIRE(projectile_write.find(
		"NET_RECONNECT_SNAPSHOT_WORLD_PROJECTILE_OWNER") !=
		std::string::npos);
	REQUIRE(netmsg_header.find(
		"typedef enum net_reconnect_snapshot_status_e") !=
		std::string::npos);
	REQUIRE(netmsg_header.find("net_reconnect_snapshot_result_t *out_result") !=
		std::string::npos);
	REQUIRE(state_write.find("NET_RECONNECT_SNAPSHOT_INVENTORY") !=
		std::string::npos);
	REQUIRE(state_write.find("NET_RECONNECT_SNAPSHOT_PLAYER_STATS") !=
		std::string::npos);
	REQUIRE(state_write.find("NET_RECONNECT_SNAPSHOT_PLAYER_MOVEMENT") !=
		std::string::npos);
	REQUIRE(state_write.find("NET_RECONNECT_SNAPSHOT_CHARACTER") !=
		std::string::npos);
	REQUIRE(state_write.find("NET_RECONNECT_SNAPSHOT_OBJECTIVE") !=
		std::string::npos);

	/* Terminal delete is exact-set absence. Regenerating setup objects retain
	 * the pending transition that lets the authority bring them back later. */
	REQUIRE(world_prop_filter.find(
		"netReconnectWorldPropShouldSerialize") != std::string::npos);
	REQUIRE(world_prop_filter.find("OBJHFLAG_DELETING") != std::string::npos);
	REQUIRE(world_prop_filter.find("OBJH2FLAG_CANREGEN") != std::string::npos);
	REQUIRE(reconnect.find("return !pending_delete || can_regenerate") !=
		std::string::npos);
	REQUIRE(state_write.find("terminal_absent_prop_count") !=
		std::string::npos);
	REQUIRE(state_write.find("first_terminal_absent_syncid") !=
		std::string::npos);

	/* Every local prop-state rejection has a stable machine-readable reason;
	 * dual relations are validated symmetrically before publication. */
	REQUIRE(netmsg_header.find(
		"typedef enum net_reconnect_prop_state_status_e") !=
		std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_MODEL_SCALE_NONPOSITIVE") !=
		std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_MODEL_SCALE_TOO_LARGE") !=
		std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_MODEL_SCALE_NAN") != std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_ATTACHMENT_PAIR") != std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_ATTACHMENT_PARENT") != std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_ATTACHMENT_MATRIX_RANGE") !=
		std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_ATTACHMENT_MATRIX_MISSING") !=
		std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_EMBEDDED_ATTACHMENT") !=
		std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_WEAPON_IDENTITY") != std::string::npos);
	REQUIRE(prop_state_write.find(
		"weapon->weaponnum < WEAPON_UNARMED") != std::string::npos);
	REQUIRE(prop_state_write.find(
		"NET_RECONNECT_PROP_STATE_WEAPON_DUAL_REFERENCE") !=
		std::string::npos);
	REQUIRE(prop_state_write.find(
		"weapon->dualweapon->dualweapon != weapon") != std::string::npos);
	const std::string player_drop = function_definition_block(
		propobj, "void weaponCreateForPlayerDrop");
	const size_t drop_commit = player_drop.find("if (!objDrop(prop, true))");
	const size_t failed_drop_retire = player_drop.find(
		"objFreePermanently(prop->obj, true)", drop_commit);
	const size_t spawn_publish = player_drop.find(
		"netmsgSvcPropSpawnWrite", failed_drop_retire);
	REQUIRE(drop_commit != std::string::npos);
	REQUIRE(failed_drop_retire != std::string::npos);
	REQUIRE(spawn_publish != std::string::npos);
	REQUIRE(drop_commit < failed_drop_retire);
	REQUIRE(failed_drop_retire < spawn_publish);

	/* The compound writer is atomic even if a future caller supplies a shared
	 * packet, and the outer boundary emits the first typed owner before the
	 * reconnect transaction rolls back. */
	const size_t rollback_label = state_write.find("rollback:");
	const size_t rollback_wp = state_write.find("dst->wp = bytes_before",
		rollback_label);
	const size_t rollback_error = state_write.find("dst->error = error_before",
		rollback_wp);
	REQUIRE(rollback_label != std::string::npos);
	REQUIRE(rollback_wp != std::string::npos);
	REQUIRE(rollback_error != std::string::npos);
	REQUIRE(rollback_label < rollback_wp);
	REQUIRE(rollback_wp < rollback_error);
	REQUIRE(state_send.find("net_reconnect_snapshot_result_t snapshot_result") !=
		std::string::npos);
	REQUIRE(state_send.find("NET.RECONNECT.RESYNC.FAIL") != std::string::npos);
	REQUIRE(state_send.find("netmsgReconnectSnapshotStatusString") !=
		std::string::npos);
	REQUIRE(state_send.find("prop=%u") != std::string::npos);
	REQUIRE(state_send.find("prop_reason=%s") != std::string::npos);
	REQUIRE(state_send.find("hidden=0x%08x") != std::string::npos);

	/* Authority-local generation and enqueue failures take the retryable close;
	 * validated peer-content failures remain terminal. */
	REQUIRE(net_header.find(
		"s32 netRestoreResultRequiresRetryableClose") != std::string::npos);
	REQUIRE(retry_policy.find("NET_RESTORE_STAGE_WRITE_FAILED") !=
		std::string::npos);
	REQUIRE(retry_policy.find("NET_RESTORE_STAGE_SEND_FAILED") !=
		std::string::npos);
	REQUIRE(retry_policy.find("NET_RESTORE_STATE_WRITE_FAILED") !=
		std::string::npos);
	REQUIRE(retry_policy.find("NET_RESTORE_STATE_SEND_FAILED") !=
		std::string::npos);
	REQUIRE(retry_policy.find("NET_RESTORE_SETTINGS_MISMATCH") ==
		std::string::npos);
	REQUIRE(manifest_ready.find(
		"netRestoreResultRequiresRetryableClose(restore_result)") !=
		std::string::npos);
	REQUIRE(stage_ready.find(
		"netRestoreResultRequiresRetryableClose(restore_result)") !=
		std::string::npos);
}

TEST_CASE("B-1097 character model clones preserve packed part lookup state",
		  "[model][clone][net][reconnect][static][b1097]")
{
	const std::string model = read_text_file("src/lib/model.c");
	const std::string propobj = read_text_file("src/game/propobj.c");
	const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string clone = function_definition_block(
		model, "struct modeldef *modeldefCloneForChr");
	const std::string equip = function_definition_block(
		propobj, "bool chrEquipWeapon");
	const std::string reconnect_end = function_definition_block(
		netmsg, "u32 netmsgSvcReconnectPropEndRead");

	/* modeldef.parts is one packed allocation: node pointers followed by the
	 * sorted s16 keys that modelGetPart bisects. Cloning either half alone is
	 * an out-of-bounds lookup, not a partial model clone. */
	REQUIRE(model.find(
		"partnums = (s16 *)&modeldef->parts[modeldef->numparts]") !=
		std::string::npos);
	REQUIRE(model.find("modeldef->parts == NULL") != std::string::npos);
	REQUIRE(model.find("upper = modeldef->numparts - 1") !=
		std::string::npos);
	REQUIRE(model.find("upper = modeldef->numparts;") ==
		std::string::npos);
	REQUIRE(clone.find("size_t part_ptr_bytes") != std::string::npos);
	REQUIRE(clone.find("size_t part_num_bytes") != std::string::npos);
	REQUIRE(clone.find("part_ptr_bytes + part_num_bytes") !=
		std::string::npos);
	REQUIRE(clone.find(
		"(const s16 *)&src->parts[src->numparts]") != std::string::npos);
	REQUIRE(clone.find("new_partnums[i] = src_partnums[i]") !=
		std::string::npos);
	REQUIRE(clone.find(
		"total_bytes = modeldef_bytes + node_bytes + rodata_bytes + part_bytes") !=
		std::string::npos);
	REQUIRE(clone.find("clone_storage = mempAlloc(total_bytes, MEMPOOL_STAGE)") !=
		std::string::npos);
	REQUIRE(clone.find("if (clone_storage == NULL)") != std::string::npos);
	REQUIRE(clone.find(
		"src->numparts * sizeof(struct modelnode *)), MEMPOOL_STAGE") ==
		std::string::npos);

	/* Resolve and require the render node before publishing either pointer or
	 * semantic ownership. Reconnect restores held slots only from that pair. */
	const size_t resolve = equip.find("attachment_node = modelGetPart");
	const size_t require_node = equip.find("if (!attachment_node)");
	const size_t publish_model = equip.find(
		"weapon->base.model->attachedtomodel = chr->model");
	const size_t publish_node = equip.find(
		"weapon->base.model->attachedtonode = attachment_node");
	REQUIRE(resolve != std::string::npos);
	REQUIRE(require_node != std::string::npos);
	REQUIRE(publish_model != std::string::npos);
	REQUIRE(publish_node != std::string::npos);
	REQUIRE(resolve < require_node);
	REQUIRE(require_node < publish_model);
	REQUIRE(publish_model < publish_node);
	REQUIRE(reconnect_end.find(
		"Parent-only props can be valid transition state") != std::string::npos);
	REQUIRE(reconnect_end.find("if (attachment_node && parent") !=
		std::string::npos);
}

TEST_CASE("B-1098 character clones own relation topology and fail closed",
		  "[model][clone][net][reconnect][static][b1098]")
{
	const std::string model = read_text_file("src/lib/model.c");
	const std::string body = read_text_file("src/game/body.c");
	const std::string relation_size = function_definition_block(
		model, "static size_t modeldefCloneRelationRodataSize");
	const std::string indexed_size = function_definition_block(
		model, "static size_t modeldefCloneIndexedRodataSize");
	const std::string collect = function_definition_block(
		model, "static s32 modeldefCloneCollectNodes");
	const std::string clone = function_definition_block(
		model, "struct modeldef *modeldefCloneForChr");
	const std::string prepare = function_definition_block(
		body, "struct model *body0f02ce8c");

	/* Only topology-bearing relation records become private. Immutable payload
	 * rodata remains shared, preserving generated-model provenance, while every
	 * rodata kind that modelCalculateRwDataIndexes dereferences is preflighted. */
	REQUIRE(relation_size.find("MODELNODETYPE_DISTANCE") != std::string::npos);
	REQUIRE(relation_size.find("MODELNODETYPE_TOGGLE") != std::string::npos);
	REQUIRE(relation_size.find("MODELNODETYPE_REORDER") != std::string::npos);
	REQUIRE(relation_size.find("MODELNODETYPE_DL") == std::string::npos);
	REQUIRE(indexed_size.find("MODELNODETYPE_CHRINFO") != std::string::npos);
	REQUIRE(indexed_size.find("MODELNODETYPE_HEADSPOT") != std::string::npos);
	REQUIRE(indexed_size.find("MODELNODETYPE_DL") != std::string::npos);
	REQUIRE(collect.find("modelRodataIsReadable(node, sizeof(*node))") !=
		std::string::npos);
	REQUIRE(collect.find("modeldefCloneIndexedRodataSize(node)") !=
		std::string::npos);
	REQUIRE(collect.find("node->rodata->distance.target") !=
		std::string::npos);
	REQUIRE(collect.find("node->rodata->toggle.target") !=
		std::string::npos);
	REQUIRE(collect.find("node->rodata->reorder.unk18") !=
		std::string::npos);
	REQUIRE(collect.find("case MODELNODETYPE_HEADSPOT:") !=
		std::string::npos);
	REQUIRE(collect.find("Attached heads are instance state") !=
		std::string::npos);

	/* Nodes, private relation rodata, and the packed part vector publish from
	 * one stage allocation. Embedded targets must point back into newnodes. */
	REQUIRE(clone.find("size_t rodata_bytes = 0") != std::string::npos);
	REQUIRE(clone.find("sizeof(union modelrodata)") != std::string::npos);
	REQUIRE(clone.find(
		"total_bytes = modeldef_bytes + node_bytes + rodata_bytes + part_bytes") !=
		std::string::npos);
	REQUIRE(clone.find("clone_storage = mempAlloc(total_bytes, MEMPOOL_STAGE)") !=
		std::string::npos);
	REQUIRE(clone.find("memset(clone_storage, 0, total_bytes)") !=
		std::string::npos);
	REQUIRE(clone.find("newnodes[i].rodata = &newrodatas[relation_index++]") !=
		std::string::npos);
	REQUIRE(clone.find("newnodes[i].rodata->distance.target = modeldefCloneMap") !=
		std::string::npos);
	REQUIRE(clone.find("newnodes[i].rodata->toggle.target = modeldefCloneMap") !=
		std::string::npos);
	REQUIRE(clone.find("newnodes[i].rodata->reorder.unk18 = modeldefCloneMap") !=
		std::string::npos);
	REQUIRE(clone.find("case MODELNODETYPE_HEADSPOT:") != std::string::npos);
	REQUIRE(clone.find("newnodes[i].child = NULL") != std::string::npos);
	REQUIRE(clone.find("dst->parts = NULL") != std::string::npos);
	REQUIRE(clone.find("dst->parts = newparts") != std::string::npos);
	REQUIRE(clone.find(
		"dst->rwdatalen = modelCalculateRwDataIndexes(dst->rootnode)") !=
		std::string::npos);
	REQUIRE(clone.find("return src") == std::string::npos);

	/* Both catalog-selected and active random heads converge on the same single
	 * clone point. Neither body nor head clone failure may reuse shared state. */
	const size_t random_select = prepare.find(
		"headmodeldef = func0f18e57c(-1 - headnum, &headnum)");
	const size_t catalog_select = prepare.find(
		"catalogGetHeadModeldefChecked(headnum, &headmodeldef)");
	const size_t common_head_clone = prepare.find(
		"modeldefCloneForChr(headmodeldef)");
	REQUIRE(random_select != std::string::npos);
	REQUIRE(catalog_select != std::string::npos);
	REQUIRE(common_head_clone != std::string::npos);
	REQUIRE(random_select < common_head_clone);
	REQUIRE(catalog_select < common_head_clone);
	REQUIRE(prepare.find("modeldefCloneForChr(headmodeldef)",
		common_head_clone + 1) == std::string::npos);
	REQUIRE(prepare.find("cloned_modeldef = modeldefCloneForChr(bodymodeldef)") !=
		std::string::npos);
	REQUIRE(prepare.find("BODY.CLONE.FAIL: body clone rejected") !=
		std::string::npos);
	REQUIRE(prepare.find("BODY.CLONE.FAIL: head clone rejected") !=
		std::string::npos);
	REQUIRE(prepare.find("BODY.RWDATA.FAIL: resize rejected") !=
		std::string::npos);
	REQUIRE(prepare.find("headmodeldef = cloned_modeldef") !=
		std::string::npos);
	REQUIRE(prepare.find("bodymodeldef->rwdatalen += headmodeldef->rwdatalen") !=
		std::string::npos);
}

TEST_CASE("temporary distribution recovery is a durable all-family lifecycle",
          "[net][distribution][recovery][static][t-networking-009][b1044]")
{
    const std::string distrib = read_text_file("port/src/net/netdistrib.c");
    const std::string scanner = read_text_file("port/src/assetcatalog_scanner.c");
    const std::string modmgr = read_text_file("port/src/modmgr.c");
    const std::string main = read_text_file("port/src/main.c");
    const std::string backend = read_text_file("port/fast3d/pdgui_backend.cpp");
    const std::string modal = read_text_file("port/fast3d/pdgui_crash_recovery.cpp");

    REQUIRE(main.find("netCrashRecoveryStartup();") != std::string::npos);
    REQUIRE(main.find("netCrashRecoveryMarkClean();") != std::string::npos);
    REQUIRE(backend.find("pdguiCrashRecoveryIsActive()") != std::string::npos);
    REQUIRE(backend.find("pdguiCrashRecoveryRender") != std::string::npos);
    REQUIRE(modal.find("Keep and Load") != std::string::npos);
    REQUIRE(modal.find("Keep Disabled") != std::string::npos);
    REQUIRE(modal.find("Discard") != std::string::npos);
    REQUIRE(modal.find("menupoolAcquire(MENU_TYPE_CRASH_RECOVERY, NULL, &g_CtxImGuiMenu)") != std::string::npos);
    REQUIRE(modal.find("menupoolRelease(MENU_TYPE_CRASH_RECOVERY)") != std::string::npos);
    REQUIRE(modal.find("DISTRIB.RECOVERY.UI.LAYOUT") != std::string::npos);
    REQUIRE(modal.find("DISTRIB.RECOVERY.UI.INPUT") != std::string::npos);
    REQUIRE(modal.find("viewport->GetWorkCenter()") != std::string::npos);
    REQUIRE(modal.find("ImGui::SetWindowPos") != std::string::npos);
    REQUIRE(modal.find("!ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None)") !=
        std::string::npos);
    REQUIRE(distrib.find("fsGetModDir()") == std::string::npos);
    REQUIRE(read_text_file("port/src/menupool.c").find("case MENU_TYPE_CRASH_RECOVERY:") !=
        std::string::npos);

    REQUIRE(distrib.find("distribWriteRecoveryReceipt") != std::string::npos);
    REQUIRE(distrib.find("_commit(_fileno(fp))") != std::string::npos);
    REQUIRE(distrib.find("MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH") !=
        std::string::npos);
    REQUIRE(distrib.find("distribVerifyRecoveryTree") != std::string::npos);
    REQUIRE(distrib.find("FILE_ATTRIBUTE_REPARSE_POINT") != std::string::npos);
    REQUIRE(distrib.find("pdcaNormalizeMemberPath") != std::string::npos);
    REQUIRE(distrib.find("pdguiThemeRegisterModDir") == std::string::npos);
    REQUIRE(distrib.find("assetCatalogScanExternalLayoutFolderDeferred") !=
        std::string::npos);
    REQUIRE(distrib.find("static const char *distribGetModsRoot(void)") !=
        std::string::npos);
    REQUIRE(distrib.find("const char *modsdir = modmgrGetModsDir();") !=
        std::string::npos);
    REQUIRE(distrib.find("distribMarkRecoveryLaunchingAt(temp_root, slot->id)") !=
        std::string::npos);
    REQUIRE(distrib.find("distribMarkRecoveryLaunchingAt(tempdir, suspect)") !=
        std::string::npos);

    const size_t retire = distrib.find("modmgrRetireSessionContent()");
    const size_t quarantine = distrib.find("distribQuarantineAndRemoveTemp(tempdir)", retire);
    REQUIRE(retire != std::string::npos);
    REQUIRE(quarantine != std::string::npos);
    REQUIRE(retire < quarantine);
    REQUIRE(distrib.find("CRASH_RECOVERY_DISABLED") != std::string::npos);
    REQUIRE(distrib.find("distribWriteCrashStateAt(tempdir, 0,") !=
        std::string::npos);
    REQUIRE(distrib.find("s_RecoveryPreserveDisabled = 1") != std::string::npos);
    REQUIRE(distrib.find(".disabled") == std::string::npos);

    REQUIRE(modmgr.find("s32 modmgrRetireSessionContent(void)") !=
        std::string::npos);
    REQUIRE(modmgr.find("modmgrUnloadAllMods();") != std::string::npos);
    REQUIRE(modmgr.find("modmgrRebuildCatalogFromCurrentSelection();") !=
        std::string::npos);
    REQUIRE(modmgr.find("catalogBuildRuntimeCaches();") != std::string::npos);
    REQUIRE(modmgr.find("videoResetTextureCache();") != std::string::npos);

    REQUIRE(scanner.find("static const external_descriptor_spec_t root_specs[]") !=
        std::string::npos);
    REQUIRE(scanner.find("registerComponentIniFile(mod_dir, descriptor") !=
        std::string::npos);
    REQUIRE(scanner.find("type == ASSET_MODEL && pathEndsWithNoCase(path, \".pdmesh\")") !=
        std::string::npos);
    REQUIRE(scanner.find("type == ASSET_PROJECTILE") != std::string::npos);
    REQUIRE(scanner.find("pathEndsWithNoCase(path, \".pdprojectile\")") !=
        std::string::npos);
    REQUIRE(scanner.find("type == ASSET_ENTITY && pathEndsWithNoCase(path, \".pdentity\")") !=
        std::string::npos);
    REQUIRE(scanner.find("pass == 4 && expected_type != ASSET_ENTITY") !=
        std::string::npos);
    REQUIRE(scanner.find("pass == 5 && expected_type != ASSET_PROJECTILE") !=
        std::string::npos);
    REQUIRE(scanner.find("Loose theme.ini remains intentionally unsupported") !=
        std::string::npos);
}

TEST_CASE("received theme owners release before session package retirement",
		"[net][distribution][pdtheme][b1053][T-ASSETS-030]")
{
	const std::string main = read_text_file("port/src/main.c");
	const auto cleanup = main.find("static void cleanup(void)");
	const auto theme_shutdown = main.find("pdguiShutdown();", cleanup);
	const auto retire_session = main.find("netCrashRecoveryMarkClean();", cleanup);
	const auto disconnect = main.find("netDisconnect();", cleanup);

	REQUIRE(cleanup != std::string::npos);
	REQUIRE(theme_shutdown != std::string::npos);
	REQUIRE(retire_session != std::string::npos);
	REQUIRE(disconnect != std::string::npos);
	REQUIRE(theme_shutdown < retire_session);
	REQUIRE(retire_session < disconnect);
}

TEST_CASE("B-1075 manifest lifecycle preserves the exact catalog type",
          "[net][manifest][catalog][static][b1075]")
{
    const std::string catalog_h = read_text_file("port/include/assetcatalog.h");
    const std::string catalog = read_text_file("port/src/assetcatalog.c");
    const std::string manifest = read_text_file("port/src/net/netmanifest.c");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string screen_h = read_text_file("port/include/screenmfst.h");
    const std::string screen = read_text_file("port/src/screenmfst.c");
    const std::string smoke = read_text_file(
        "tools/smoke-verify/tests/listen_host_match_smoke.json");
    const std::string classify = function_block(
        manifest, "static asset_type_e s_manifestEntryCatalogAssetType");
    const std::string validate = function_block(
        manifest, "s32 manifestValidate");
    const std::string apply = function_block(
        manifest, "s32 manifestApplyDiff");
    const std::string mp_transition = function_block(
        manifest, "void manifestMPTransition");
    const std::string ensure = function_block(
        manifest, "s32 manifestEnsureLoaded");
    const std::string admission = function_block(
        netmsg, "static bool netLobbyManifestEntryMatchesCatalog");

    REQUIRE(catalog_h.find("assetCatalogResolveAny(const char *id)") !=
        std::string::npos);
    REQUIRE(catalog.find("static const asset_entry_t *s_resolveAnyLocked") !=
        std::string::npos);
    REQUIRE(catalog.find("entry && entry->enabled ? entry : NULL") !=
        std::string::npos);

    REQUIRE(classify.find("assetCatalogResolveAny(e->id)") !=
        std::string::npos);
    REQUIRE(classify.find("netManifestTypeAcceptsCatalogAsset(") !=
        std::string::npos);
    REQUIRE(classify.find("return asset->type;") != std::string::npos);
    REQUIRE(manifest.find("s_manifestCatalogAssetType") == std::string::npos);
    REQUIRE(manifest.find("de->slot_index = ne->slot_index") !=
        std::string::npos);
    REQUIRE(manifest.find("de->slot_index = ce->slot_index") !=
        std::string::npos);

    REQUIRE(validate.find("assetCatalogResolveAny(entry->id)") !=
        std::string::npos);
    REQUIRE(validate.find("e->type != expected") != std::string::npos);
    REQUIRE(validate.find("entry->type, entry->slot_index, expected") !=
        std::string::npos);
    REQUIRE(apply.find("s_manifestDiffEntryHasExactType(&diff->to_load[i], 1)") !=
        std::string::npos);
    REQUIRE(apply.find("s_manifestDiffEntryHasExactType(&diff->to_unload[i], 0)") !=
        std::string::npos);
    REQUIRE(apply.find("for (rollback = i; rollback-- > 0; )") !=
        std::string::npos);
    REQUIRE(apply.find("catalogReleaseTypedAsset(") != std::string::npos);
    REQUIRE(apply.find("MANIFEST.LIFECYCLE.ROLLBACK") != std::string::npos);
    const size_t all_loads = apply.find("for (i = 0; i < diff->num_to_load; i++)");
    const size_t first_unload = apply.find(
        "for (i = 0; i < diff->num_to_unload; i++)", all_loads);
    const size_t publish = apply.find(
        "s_manifestCopyInto(&g_CurrentLoadedManifest, needed)", first_unload);
    REQUIRE(all_loads != std::string::npos);
    REQUIRE(first_unload != std::string::npos);
    REQUIRE(publish != std::string::npos);
    REQUIRE(all_loads < first_unload);
    REQUIRE(first_unload < publish);
    REQUIRE(mp_transition.find("manifestValidate(&s_SpLastDiff) != 0") !=
        std::string::npos);
    REQUIRE(mp_transition.find("|| !manifestApplyDiff(needed, &s_SpLastDiff)") !=
        std::string::npos);
    REQUIRE(mp_transition.find("current manifest preserved") !=
        std::string::npos);
    REQUIRE(ensure.find("catalogLoadTypedAsset(e->type, e->id)") !=
        std::string::npos);
    REQUIRE(admission.find("netManifestTypeAcceptsCatalogAsset(") !=
        std::string::npos);

    REQUIRE(screen_h.find("const asset_type_e *types") != std::string::npos);
    REQUIRE(screen.find("catalogLoadTypedAsset(e->types[j], e->ids[j])") !=
        std::string::npos);
    REQUIRE(screen.find("screenManifestCatalogAssetType") == std::string::npos);

    REQUIRE(smoke.find("MANIFEST-SP: load 'base:arena_mp_felicity'") !=
        std::string::npos);
    REQUIRE(smoke.find("CATALOG\\\\.LIFECYCLE\\\\.LOAD: .*type mismatch") !=
        std::string::npos);
    REQUIRE(smoke.find("MANIFEST-SP: load failed 'base:arena_mp_felicity'") !=
        std::string::npos);
    REQUIRE(smoke.find("MANIFEST\\\\.LIFECYCLE\\\\.(REJECT|ROLLBACK)") !=
        std::string::npos);
}

TEST_CASE("B-1104 ordinary gameplay waits for the real post-load stage barrier",
          "[net][stage-ready][transaction][static][b1104]")
{
    const std::string header = read_text_file("port/include/net/net.h");
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string blocked = function_definition_block(
        net, "bool netServerStageReplicationBlocked(void)");
    const std::string arm = function_definition_block(
        net, "static bool netServerArmStageReplicationBarrier");
    const std::string ready = function_definition_block(
        net, "static bool netServerStageReplicationReady");
    const std::string local_loaded = function_definition_block(
        net, "void netLocalStageLoaded(void)");
    const std::string combat_start = function_definition_block(
        net, "s32 netServerStageStart(void)");
    const std::string coop_start = function_definition_block(
        net, "s32 netServerCoopStageStart");
    const std::string end_frame = function_definition_block(
        net, "void netEndFrame(void)");
    const std::string publish = function_definition_block(
        net, "static bool netServerPublishPendingResyncs(void)");
    const std::string ready_read = function_definition_block(
        netmsg, "u32 netmsgClcStageReadyRead");
    const std::string ready_write = function_definition_block(
        netmsg, "u32 netmsgClcStageReadyWrite");
    const std::string gpu_send = function_definition_block(
        netmsg, "void netSendGpuSwarmState(void)");

    REQUIRE(header.find("bool netServerStageReplicationBlocked(void);") !=
        std::string::npos);
    REQUIRE(header.find("extern u32  g_NetStageEpoch;") !=
        std::string::npos);
    REQUIRE(blocked.find("g_NetMode == NETMODE_SERVER") != std::string::npos);
    REQUIRE(blocked.find(
        "s_NetStageReplicationPhase != NET_STAGE_REPLICATION_ACTIVE") !=
        std::string::npos);
    REQUIRE(arm.find(
        "s_NetStageReplicationPhase != NET_STAGE_REPLICATION_INACTIVE") !=
        std::string::npos);
    REQUIRE(arm.find("netServerStageReplicationPeer(cl, g_NetMatchRoomId)") !=
        std::string::npos);
    REQUIRE(arm.find("cl->stage_ready = false") != std::string::npos);
    REQUIRE(arm.find("wait_mask |= 1u << (u32)i") != std::string::npos);
    REQUIRE(arm.find("g_NetStageEpoch++") != std::string::npos);
    REQUIRE(arm.find(
        "s_NetStageReplicationPhase = NET_STAGE_REPLICATION_WAITING") !=
        std::string::npos);
    REQUIRE(arm.find("s_NetStageReplicationWaitMask = wait_mask") !=
        std::string::npos);
    REQUIRE(arm.find("s_NetStageAuthorityReady = false") !=
        std::string::npos);
    const size_t combat_arm = combat_start.find(
        "netServerArmStageReplicationBarrier(\"combat-stage-start\")");
    const size_t combat_write = combat_start.find(
        "netmsgServerStageStartWrite", combat_arm);
    const size_t coop_arm = coop_start.find(
        "netServerArmStageReplicationBarrier(\"coop-stage-start\")");
    const size_t coop_write = coop_start.find(
        "netmsgServerStageStartWrite", coop_arm);
    REQUIRE(combat_arm != std::string::npos);
    REQUIRE(combat_write != std::string::npos);
    REQUIRE(coop_arm != std::string::npos);
    REQUIRE(coop_write != std::string::npos);
    REQUIRE(combat_arm < combat_write);
    REQUIRE(coop_arm < coop_write);

    REQUIRE(ready.find("if (!cl->stage_ready)") != std::string::npos);
    REQUIRE(ready.find("pending_mask |= bit") != std::string::npos);
    REQUIRE(ready.find("if (pending_mask || !s_NetStageAuthorityReady)") !=
        std::string::npos);
    REQUIRE(ready.find(
        "s_NetStageReplicationPhase = NET_STAGE_REPLICATION_RELEASE") !=
        std::string::npos);
    REQUIRE(local_loaded.find("s_NetStageAuthorityReady = true") !=
        std::string::npos);
    REQUIRE(local_loaded.find("ready authority=server epoch=%u") !=
        std::string::npos);
    REQUIRE(local_loaded.find("g_NetLocalClient->stage_ready") !=
        std::string::npos);
    REQUIRE(ready_read.find("ready_epoch = netbufReadU32(src)") !=
        std::string::npos);
    REQUIRE(ready_read.find("ready_epoch != g_NetStageEpoch") !=
        std::string::npos);
    REQUIRE(ready_write.find("g_NetStageEpoch == 0") != std::string::npos);
    REQUIRE(ready_write.find("netbufWriteU32(dst, g_NetStageEpoch)") !=
        std::string::npos);
    REQUIRE(ready_read.find("srccl->stage_ready = true") != std::string::npos);
    REQUIRE(ready_read.find("post_load=1") != std::string::npos);
    const size_t gpu_barrier = gpu_send.find(
        "netServerStageReplicationBlocked()");
    const size_t gpu_readback = gpu_send.find("swarmGpuReadbackTextureRows");
    const size_t gpu_broadcast = gpu_send.find(
        "netSend(NULL, &g_NetMsg, false");
    REQUIRE(gpu_barrier != std::string::npos);
    REQUIRE(gpu_readback != std::string::npos);
    REQUIRE(gpu_broadcast != std::string::npos);
    REQUIRE(gpu_barrier < gpu_readback);
    REQUIRE(gpu_readback < gpu_broadcast);

    const size_t readiness = end_frame.find(
        "stage_replication_ready = netServerStageReplicationReady()");
    const size_t initial_discard = end_frame.find(
        "s_NetStageReplicationPhase", readiness);
    const size_t server_broadcast = end_frame.find(
        "if (g_NetMode == NETMODE_SERVER && g_NetNumClients > 0",
        initial_discard);
    const size_t active_incrementals = end_frame.find(
        "s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE",
        server_broadcast);
    const size_t late_authority = end_frame.find("end-frame-late",
        active_incrementals);
    const size_t pending_resync = end_frame.find(
        "netServerPublishPendingResyncs()", late_authority);
    const size_t release_retry = end_frame.find(
        "authority_publication_failed = true", pending_resync);
    const size_t final_discard = end_frame.find(
        "Consult the live phase here", pending_resync);
    const size_t final_flush = end_frame.find("netFlushSendBuffers()",
        final_discard);
    const size_t active_commit = end_frame.find(
        "s_NetStageReplicationPhase = NET_STAGE_REPLICATION_ACTIVE",
        final_flush);
    REQUIRE(readiness != std::string::npos);
    REQUIRE(initial_discard != std::string::npos);
    REQUIRE(server_broadcast != std::string::npos);
    REQUIRE(active_incrementals != std::string::npos);
    REQUIRE(late_authority != std::string::npos);
    REQUIRE(pending_resync != std::string::npos);
    REQUIRE(release_retry != std::string::npos);
    REQUIRE(final_discard != std::string::npos);
    REQUIRE(final_flush != std::string::npos);
    REQUIRE(active_commit != std::string::npos);
    REQUIRE(readiness < initial_discard);
    REQUIRE(initial_discard < server_broadcast);
    REQUIRE(server_broadcast < active_incrementals);
    REQUIRE(active_incrementals < late_authority);
    REQUIRE(late_authority < pending_resync);
    REQUIRE(pending_resync < release_retry);
    REQUIRE(release_retry < final_discard);
    REQUIRE(final_discard < final_flush);
    REQUIRE(final_flush < active_commit);
    REQUIRE(publish.find(
        "s_NetStageReplicationPhase == NET_STAGE_REPLICATION_RELEASE") !=
        std::string::npos);
    REQUIRE(publish.find("reason=missing-baseline") != std::string::npos);
    const size_t reelect_gate = end_frame.find(
        "g_NetDedicated && !g_NetBotAuthorityDelegated");
    const size_t reelect_active = end_frame.find(
        "s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE",
        reelect_gate);
    const size_t reelect_send = end_frame.find(
        "netSend(candidate, &wire", reelect_active);
    REQUIRE(reelect_gate != std::string::npos);
    REQUIRE(reelect_active != std::string::npos);
    REQUIRE(reelect_send != std::string::npos);
    REQUIRE(reelect_gate < reelect_active);
    REQUIRE(reelect_active < reelect_send);
}
