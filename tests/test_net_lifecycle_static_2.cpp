/*
 * Split out of tests/test_net_lifecycle_static.cpp for build time (T-TOOLING-018):
 * part 2 of 2 of its TEST_CASEs, in their original order, with
 * copies of the internal helpers they use. Link stubs stay in the original.
 */
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
    const std::string decline = function_definition_block(distrib,
        "static void netDistribClientDeclineActiveManifest");
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

    REQUIRE(manifest.find("netDistribClientBeginManifestTransferSet(") !=
        std::string::npos);
    REQUIRE(distrib.find("transfer set awaiting next item") !=
        std::string::npos);
    REQUIRE(distrib.find("received_count <") != std::string::npos);
    REQUIRE(distrib.find("missing_count") != std::string::npos);
    REQUIRE(distrib.find("distribSendEnd(cl, catalog_id, 0)") !=
        std::string::npos);
    REQUIRE(distrib.find("DISTRIB_CHUNKS_PER_TICK 4") != std::string::npos);
    REQUIRE(distrib.find("enet_list_size(&peer->outgoingCommands)") !=
        std::string::npos);
    REQUIRE(distrib.find("distribSendEnd(cl, s_SendStream.id, 0)") !=
        std::string::npos);
    REQUIRE(distrib.find("distribSendEnd(cl, s_SendStream.id, 1)") !=
        std::string::npos);
    REQUIRE(netmsg.find("netDistribServerCancelClient(srccl)") !=
        std::string::npos);
    REQUIRE(distrib.find("distribRequestSameConnection(&request, cl, cl->peer,") !=
        std::string::npos);
    REQUIRE(distrib.find("distribQueuePush(&s_Queue, &request)") !=
        std::string::npos);
    const auto reset = function_definition_block(
        read_text_file("port/src/net/net.c"), "static inline void netClientReset(");
    REQUIRE(reset.find("netDistribServerCancelClient(cl)") <
        reset.find("memset(cl, 0, sizeof(*cl))"));

    REQUIRE(distrib.find("distribManifestComponentSha(slot->id)") !=
        std::string::npos);
    REQUIRE(distrib.find("modmgrRegisterSessionFolder(destdir, slot->id, package_sha)") !=
        std::string::npos);
    REQUIRE(modmgr_h.find("session_only") != std::string::npos);
    REQUIRE(modmgr_h.find("modmgrRegisterSessionFolder") != std::string::npos);
    REQUIRE(modmgr.find("memcmp(mod->sha256, expected_sha256") !=
        std::string::npos);
    REQUIRE(modmgr.find("mod->session_only = 1") != std::string::npos);
    // Both persistence formats delegate to the checked shared helper. Session
    // downloads must remain excluded even though the loop moved out of modmgr.c.
    const std::string enabled_state = read_text_file("port/src/modmgr_enabled_state.cpp");
    const auto legacy_save = function_definition_block(modmgr, "static bool modmgrBuildEnabledList(void)");
    const auto enabled_csv = function_definition_block(enabled_state, "int modmgrBuildEnabledCsv(");
    const auto enabled_json = function_definition_block(enabled_state, "int modmgrSaveEnabledFile(");
    REQUIRE(legacy_save.find("if (!modmgrBuildEnabledCsv(g_ModRegistry, g_ModRegistryCount,") !=
        std::string::npos);
    REQUIRE(modmgr.find("modmgrSaveEnabledFile(path, g_ModRegistry, g_ModRegistryCount") !=
        std::string::npos);
    REQUIRE(enabled_csv.find("if (!registry[i].enabled || registry[i].session_only) continue;") !=
        std::string::npos);
    REQUIRE(enabled_json.find("if (!registry[i].enabled || registry[i].session_only) continue;") !=
        std::string::npos);
	REQUIRE(net_h.find("#define NET_PROTOCOL_VER 65") != std::string::npos);
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
        "s32 netDistribClientBeginManifestTransferSet")) != std::string::npos);
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
    const size_t stage_send = start.find("netSendToMatch(", stage_write);
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
    const std::string transition = function_block(manifest, "s32 manifestMPTransition(void)");

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

TEST_CASE("Grid consent owns menu input during exact match preparation", "[net][lifecycle][grid][input][static]")
{
    const auto bridge = read_text_file("port/fast3d/pdgui_bridge.c");
    const auto lobby = read_text_file("port/fast3d/pdgui_lobby.cpp");
    const auto netmsg = read_text_file("port/src/net/netmsg.c");
    const auto offer = function_definition_block(netmsg, "u32 netmsgSvcGridMatchOfferRead(");
    const size_t preparing = offer.find("srccl->state = CLSTATE_PREPARING;");
    const size_t consent = offer.find("netDistribClientBeginGridConsent(");
    REQUIRE(consent != std::string::npos);
    CHECK(preparing < consent);

    const auto guard = function_definition_block(bridge, "s32 netLocalClientPreparingGridTransfer(");
    CHECK(guard.find("NETMODE_CLIENT") != std::string::npos);
    CHECK(guard.find("netPeerHasGridCapability(g_NetLocalClient)") != std::string::npos);
    CHECK(guard.find("netLocalClientIsPreparing()") != std::string::npos);
    const auto preparing_guard = function_definition_block(bridge, "s32 netLocalClientIsPreparing(");
    CHECK(preparing_guard.find("state == CLSTATE_PREPARING") != std::string::npos);
    CHECK(guard.find("netDistribClientIsGridArena()") != std::string::npos);
    const auto render = function_definition_block(lobby, "void pdguiLobbyRender(");
    const auto prompt = function_block(render, "if (gridPreparing)");
    CHECK(render.find("s_InRoom ? MENU_TYPE_ROOM : MENU_TYPE_SOCIAL_LOBBY") < render.find("if (gridPreparing)"));
    CHECK(render.find("if (gridPreparing)") < render.find("if (netLocalClientInLobby())"));
    REQUIRE(prompt.find("pdguiDistribOverlayRender(winW, winH, 1)") != std::string::npos);
    CHECK(prompt.find("menupoolAcquire(consentMenu, nullptr, &g_CtxImGuiMenu)")
        < prompt.find("pdguiDistribOverlayRender(winW, winH, 1)"));
    CHECK(prompt.find("pdguiDistribOverlayRender(winW, winH, 1)") < prompt.find("return;"));
    CHECK(prompt.find("pdguiRoomScreenRender(") == std::string::npos);
    CHECK(prompt.find("pdguiLobbyScreenRender(") == std::string::npos);

    const auto overlay = function_definition_block(read_text_file("port/fast3d/pdgui_lobby_distrib.cpp"),
        "void pdguiDistribOverlayRender(");
    const auto open = overlay.find("ImGui::OpenPopup(consentPopup)");
    const auto modal = overlay.find("ImGui::BeginPopupModal(consentPopup", open);
    const auto session = overlay.find("ImGui::Button(gridContent ? \"Receive for Match\" : \"This Session\"", modal);
    const auto focus = overlay.find("pdguiMenuRecordView(0, -1, ImGui::GetItemID()", session);
    REQUIRE(open < modal);
    REQUIRE(modal < session);
    REQUIRE(session < focus);
    CHECK(focus != std::string::npos);
    const auto download = function_block(overlay.substr(modal), "if (!gridContent)");
    CHECK(download.find("ImGui::Button(\"Download\"") != std::string::npos);
    CHECK(overlay.find("const bool temporary = gridContent || !download || !accept;") != std::string::npos);
}

TEST_CASE("Grid transfer displays accepted bytes, verified package order and a scoped refusal",
          "[net][distribution][grid][lifecycle][static]")
{
    const auto netmsg = read_text_file("port/src/net/netmsg.c");
    const auto distrib = read_text_file("port/src/net/netdistrib.c");
    const auto overlay = read_text_file("port/fast3d/pdgui_lobby_distrib.cpp");
    const auto request = function_definition_block(netmsg, "static s32 netGridRequestNextSource(");
    const size_t status = request.find("netDistribClientGridArenaPackage(join->next_package, package->owner.id, join->received_bytes)");
    REQUIRE(status != std::string::npos);
    CHECK(request.find("netSend(client, &wire, 1, NETCHAN_CONTROL) == size") < status);

    const auto chunk = function_definition_block(netmsg, "u32 netmsgSvcGridArenaChunkRead(");
    const size_t progress = chunk.find("netDistribClientGridArenaProgress(join->received_bytes + chunk.offset + chunk.size)");
    REQUIRE(progress != std::string::npos);
    CHECK(chunk.find("gridMatchTransferAppend(") < progress);
    const size_t next = chunk.find("netGridRequestNextSource(join, srccl)");
    REQUIRE(next != std::string::npos);
    CHECK(chunk.find("gridMatchCacheAccept(") < chunk.find("join->received_bytes += size;"));
    CHECK(chunk.find("join->received_bytes += size;") < next);

    const auto refuse = function_definition_block(netmsg, "static void netGridRefuseGuestTransfer(");
    const size_t fail = refuse.find("netDistribClientFailGridArena(reason)");
    REQUIRE(fail != std::string::npos);
    CHECK(refuse.find("netGridSendStatus(client, &s_GridGuestOffer.identity, 0)") < fail);
    CHECK(fail < refuse.find("netmsgGridCancelArenaOffer()"));
    const auto end = function_definition_block(distrib, "void netDistribClientEndGridArena(");
    CHECK(end.find("if (!s_GridArenaTransfer) return;") < end.find("s_ClientStatus.state ="));
    CHECK(function_definition_block(distrib, "void netDistribClientBeginManifestOffer(")
        .find("netDistribClientClearGridArenaError()") != std::string::npos);
    CHECK(function_definition_block(netmsg, "u32 netmsgSvcRoomAssignRead(")
        .find("netDistribClientClearGridArenaError()") != std::string::npos);

    const auto render = function_definition_block(overlay, "void pdguiDistribOverlayRender(");
    CHECK(render.find("(float)st.current_bytes_received / (float)st.current_bytes_total") != std::string::npos);
    CHECK(render.find("netDistribClientGridArenaError()") != std::string::npos);
    CHECK(render.find("ImGui::TextWrapped(\"%s\", gridError)") != std::string::npos);
}

TEST_CASE("Shared Draft input survives unrelated commits and marks a changed resource",
          "[net][lifecycle][grid-edit][input][static]")
{
    const auto room = read_text_file("port/fast3d/pdgui_menu_room.cpp");
    const auto local = function_block(room, "struct SharedDraftInput");
    const auto sync = function_block(local, "void sync(");
    CHECK(sync.find("selectionChanged || committed || (!conflict && !dirty())") != std::string::npos);
    CHECK(sync.find("draft.next_sequence != submittedSequence") != std::string::npos);
    CHECK(sync.find("if (!sameDraftValue(baseline, current)) conflict = true;") != std::string::npos);
    CHECK(sync.find("draft.revision") == std::string::npos);
    const auto ambient = function_definition_block(room, "static void roomRenderSharedAmbient(");
    const auto starts = function_definition_block(room, "static bool roomRenderSharedSpawns(");
    const auto decorations = function_definition_block(room, "static bool roomRenderSharedDecorations(");
    CHECK(ambient.find("ImGui::Button(\"Reload Ambient Light\")) input.reload(current)") != std::string::npos);
    CHECK(starts.find("strcmp(input.baseline.spawn_id, current.spawn_id) != 0") != std::string::npos);
    CHECK(starts.find("ImGui::Button(\"Reload Start\")) input.reload(current)") != std::string::npos);
    CHECK(decorations.find("strcmp(input.baseline.record_id, current.record_id) != 0") != std::string::npos);
    CHECK(decorations.find("ImGui::Button(\"Reload Decoration\")) input.reload(current)") != std::string::npos);
    for (const auto *editor : {&ambient, &starts, &decorations}) {
        CHECK(editor->find("ImGui::BeginDisabled(input.conflict || !dirty)") != std::string::npos);
        CHECK(editor->find("input.submittedSequence = draft.next_sequence") != std::string::npos);
    }
}

TEST_CASE("match preparation keeps menu input for content consent and countdown cancellation",
          "[net][lifecycle][input][listen-host][lobby-ui][static]")
{
    const std::string lobby = function_block(
        read_text_file("port/fast3d/pdgui_lobby.cpp"), "void pdguiLobbyRender");
    const size_t preparing = lobby.find("else if (netLocalClientIsPreparing())");
    const size_t menu_input = lobby.find(
        "menupoolAcquire(MENU_TYPE_ROOM, NULL, &g_CtxImGuiMenu)", preparing);
    const size_t content = lobby.find("pdguiDistribOverlayRender(winW, winH, 1)", preparing);
    const size_t release = lobby.find("menupoolRelease(MENU_TYPE_ROOM)", preparing);
    const size_t close_content = lobby.find("pdguiDistribOverlayRender(winW, winH, 0)", release);
    REQUIRE(preparing < menu_input);
    REQUIRE(menu_input < content);
    REQUIRE(content < release);
    REQUIRE(release < close_content);
    REQUIRE(close_content != std::string::npos);

    const std::string distrib = function_block(
        read_text_file("port/fast3d/pdgui_lobby_distrib.cpp"), "void pdguiDistribOverlayRender");
    const size_t inactive = distrib.find("!consentPending && ImGui::IsPopupOpen(consentPopup)");
    const size_t cleanup = distrib.find("ImGui::CloseCurrentPopup()", inactive);
    const size_t open_content = distrib.find("ImGui::OpenPopup(consentPopup)");
    const size_t content_open = distrib.find("ImGui::BeginPopupModal(consentPopup", open_content);
    const size_t back = distrib.find("pdguiMenuCancelPressed()", content_open);
    const size_t skip = distrib.find("ImGui::Button(gridContent ? \"Decline\" : \"Skip\"", back);
    const size_t resolve = distrib.find("if (netDistribClientResolveConsent(accept, temporary))", skip);
    const size_t close_resolved = distrib.find("ImGui::CloseCurrentPopup()", resolve);
    REQUIRE(inactive < cleanup);
    REQUIRE(cleanup < open_content);
    REQUIRE(open_content < content_open);
    REQUIRE(content_open < back);
    REQUIRE(back < skip);
    REQUIRE(skip < resolve);
    REQUIRE(resolve < close_resolved);
    REQUIRE(close_resolved != std::string::npos);
    REQUIRE(distrib.find("const bool accept = !cancel && !skip", skip) < resolve);

    const std::string bridge = function_block(
        read_text_file("port/fast3d/pdgui_bridge.c"), "s32 pdguiCountdownIsActive");
    REQUIRE(bridge.find("netLocalClientIsPreparing() && g_MatchCountdownState.active") != std::string::npos);

    const std::string countdown = function_block(
        read_text_file("port/fast3d/pdgui_countdown.cpp"), "void pdguiCountdownRender");
    const size_t open_countdown = countdown.find("ImGui::OpenPopup(countdownPopup)");
    const size_t countdown_open = countdown.find("ImGui::BeginPopupModal(countdownPopup", open_countdown);
    const size_t glyph = countdown.find("pdguiGlyphGetActionLabel(ACTION_MENU_CANCEL", countdown_open);
    const size_t mapped = countdown.find("pdguiMenuCancelPressed()", glyph);
    const size_t button = countdown.find("ImGui::Button(label,", mapped);
    const size_t cancel = countdown.find("netLobbyRequestCancel()", button);
    REQUIRE(open_countdown < countdown_open);
    REQUIRE(countdown_open < glyph);
    REQUIRE(glyph < mapped);
    REQUIRE(mapped < button);
    REQUIRE(button < cancel);
    REQUIRE(cancel != std::string::npos);

    const std::string transport = read_text_file("port/src/net/netmsg.c");
    const std::string publish = function_block(transport, "static void readyGateBroadcastCountdown");
    const std::string receive = function_block(transport, "u32 netmsgSvcMatchCountdownRead");
    const size_t local_room = publish.find("g_NetLocalClient->room_id == s_ReadyGate.room_id");
    const size_t local_publish = publish.find("matchCountdownSet(", local_room);
    const size_t peer_publish = publish.find("netSendToRoom(", local_publish);
    REQUIRE(local_room < local_publish);
    REQUIRE(local_publish < peer_publish);
    REQUIRE(peer_publish != std::string::npos);
    REQUIRE(receive.find("matchCountdownSet(") != std::string::npos);
}

TEST_CASE("Social owns Back and tab navigation above the Room and server lobby",
          "[net][lifecycle][input][lobby-ui][static]")
{
    const std::string room = function_block(
        read_text_file("port/fast3d/pdgui_menu_room.cpp"), "void pdguiRoomScreenRender");
    const std::string lobby = function_block(
        read_text_file("port/fast3d/pdgui_menu_lobby.cpp"), "void pdguiLobbyScreenRender");
    for (const std::string *screen : {&room, &lobby}) {
        const size_t social = screen->find("socialOwnsInput = pdguiFriendsAnySurfaceIsOpen()");
        const size_t no_input = screen->find("wflags |= ImGuiWindowFlags_NoInputs", social);
        const size_t window = screen->find("ImGui::Begin(", no_input);
        REQUIRE(social < no_input);
        REQUIRE(no_input < window);
        REQUIRE(window != std::string::npos);
    }
    REQUIRE(room.find("!socialOwnsInput && pdguiMenuTabPrevPressed()") != std::string::npos);
    REQUIRE(room.find("!socialOwnsInput && pdguiMenuTabNextPressed()") != std::string::npos);
    REQUIRE(room.find("roomChildBlocks = roomPopupOwnedFrame || socialOwnsInput") != std::string::npos);
    REQUIRE(room.find("!socialOwnsInput && (restoreRoomFocus || ImGui::IsWindowAppearing())") != std::string::npos);
    REQUIRE(lobby.find("if (!socialOwnsInput && !roomModalOwnedInput") != std::string::npos);
    REQUIRE(lobby.find("!socialOwnsInput && (restoreLobbyFocus || ImGui::IsWindowAppearing())") != std::string::npos);
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
	const std::string stage_queue = function_block(
		netmsg, "static u32 netmsgReconnectQueueStage");
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
		read_text_file("port/src/net/net_player_move_ingress.c"), "u32 netmsgClcMoveRead");
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
	const size_t match_boundary = auth.find(
		"} else if (ingame && !netGridEditPrivateResumeRoomOpen(g_NetMatchRoomId))",
		admitted_record);
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
		"netmsgReconnectQueueStage(srccl)", reconnect_gate);
	REQUIRE(reconnect_gate != std::string::npos);
	REQUIRE(hash_gate != std::string::npos);
	REQUIRE(transfer != std::string::npos);
	REQUIRE(ready_call != std::string::npos);
	REQUIRE(reconnect_gate < hash_gate);
	REQUIRE(hash_gate < ready_call);
	REQUIRE(hash_gate < transfer);

	const size_t restore_call = stage_queue.find(
		"netServerRestorePreserved");
	const size_t full_stage_capacity = stage_queue.find(
		"malloc(NET_BUFSIZE)");
	const size_t send_failure = stage_queue.find(
		"netRestoreResultRequiresRetryableClose(restore_result)",
		restore_call);
	const size_t retryable_close = stage_queue.find(
		"DISCONNECT_TIMEOUT", send_failure);
	REQUIRE(restore_call != std::string::npos);
	REQUIRE(full_stage_capacity != std::string::npos);
	REQUIRE(full_stage_capacity < restore_call);
	REQUIRE(send_failure != std::string::npos);
	REQUIRE(retryable_close != std::string::npos);
	REQUIRE(restore_call < send_failure);
	REQUIRE(send_failure < retryable_close);
	REQUIRE(stage_queue.find("netmsgReconnectPreparedSend") ==
		std::string::npos);
	REQUIRE(stage_queue.find("status=stage_queued") != std::string::npos);
	REQUIRE(stage_queue.find("reservation_retained=1") !=
		std::string::npos);
	REQUIRE(stage_queue.find("restore=committed") == std::string::npos);
	REQUIRE(stage_queue.find("netmsgServerPublishAuthenticatedTopology") ==
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
		"if (!netPlayerMoveCommit(srccl, &newmove, outmoveack))");
	const size_t move_commit = move.find("return src->error;", stale_move_gate);
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
	const std::string snapshot = function_block(
		read_text_file("port/src/net/net_player_move_ingress.c"),
		"u32 netmsgSvcPlayerMoveRead(");
	const size_t snapshot_commit = snapshot.find(
		"if (!netPlayerMoveCommit(movecl, &newmove, outmoveack))");
	const size_t snapshot_reject = snapshot.find("return src->error;", snapshot_commit);
	const size_t teleport = snapshot.find("chrSetPos(");
	REQUIRE(snapshot_commit != std::string::npos);
	REQUIRE(snapshot_reject != std::string::npos);
	REQUIRE(teleport != std::string::npos);
	REQUIRE(snapshot_commit < snapshot_reject);
	REQUIRE(snapshot_reject < teleport);

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
	const std::string stage_queue = function_definition_block(
		netmsg, "static u32 netmsgReconnectQueueStage");
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
	REQUIRE(stage_queue.find(
		"netRestoreResultRequiresRetryableClose(restore_result)") !=
		std::string::npos);
	REQUIRE(stage_ready.find(
		"netRestoreResultRequiresRetryableClose(restore_result)") !=
		std::string::npos);
}

TEST_CASE("B-1096 reconnect player snapshots restore state without replaying death",
		  "[net][lifecycle][reconnect][player][static][b1064][b1096][b1097]")
{
	const std::string reconnect_header = read_text_file(
		"port/include/net/net_reconnect.h");
	const std::string reconnect_source = read_text_file(
		"port/src/net/net_reconnect.c");
	const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string player_header = read_text_file(
		"src/include/game/player.h");
	const std::string player = read_text_file("src/game/player.c");
	const std::string planner = function_definition_block(reconnect_source,
		"net_reconnect_player_state_action_e netReconnectPlanPlayerState");
	const std::string stats_read = function_definition_block(netmsg,
		"static u32 netmsgReadPlayerStats");
	const std::string apply_dead = function_definition_block(player,
		"static bool playerApplyDeadState");
	const std::string live_death = function_definition_block(player,
		"void playerDieByShooter");
	const std::string snapshot_death = function_definition_block(player,
		"bool playerRestoreDeadStateFromSnapshot");

	REQUIRE(reconnect_header.find(
		"typedef enum net_reconnect_player_state_action_e") !=
		std::string::npos);
	REQUIRE(planner.find("NET_RECONNECT_PLAYER_STATE_SNAPSHOT_DEATH") !=
		std::string::npos);
	REQUIRE(planner.find("NET_RECONNECT_PLAYER_STATE_LIVE_DEATH") !=
		std::string::npos);
	REQUIRE(stats_read.find(
		"s_ReconnectPropReceive.complete") != std::string::npos);
	REQUIRE(stats_read.find(
		"playerRestoreDeadStateFromSnapshot()") != std::string::npos);
	REQUIRE(stats_read.find(
		"NET.RECONNECT.PLAYER_STATE client=%u dead=1 apply=snapshot live_side_effects=0 drops=0 score=0 owner_cleanup=0") !=
		std::string::npos);
	REQUIRE(player_header.find(
		"bool playerRestoreDeadStateFromSnapshot(void)") !=
		std::string::npos);

	/* The shared state application keeps dead presentation identical. Only
	 * the live entry point owns historical event effects. */
	REQUIRE(apply_dead.find("if (replay_live_event)") !=
		std::string::npos);
	REQUIRE(apply_dead.find("mpstatsRecordDeath") != std::string::npos);
	REQUIRE(apply_dead.find("currentPlayerDropAllItems") !=
		std::string::npos);
	REQUIRE(apply_dead.find("g_PlayersOwnerCleanupPending") !=
		std::string::npos);
	REQUIRE(apply_dead.find("bgunHandlePlayerDead") != std::string::npos);
	REQUIRE(live_death.find(
		"playerApplyDeadState(shooter, force, true)") != std::string::npos);
	REQUIRE(snapshot_death.find(
		"playerApplyDeadState(g_Vars.currentplayernum, true, false)") !=
		std::string::npos);
	REQUIRE(snapshot_death.find("mpstatsRecordDeath") == std::string::npos);
	REQUIRE(snapshot_death.find("currentPlayerDropAllItems") ==
		std::string::npos);
}

TEST_CASE("B-1096 reconnect prop adoption owns exactly one intrusive-link domain",
		  "[net][lifecycle][reconnect][prop][static][b1064][b1096][b1097]")
{
	const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string spawn_write = function_definition_block(netmsg,
		"u32 netmsgSvcPropSpawnWrite");
	const std::string spawn_read = function_definition_block(netmsg,
		"u32 netmsgSvcPropSpawnRead");
	const std::string begin_read = function_definition_block(netmsg,
		"u32 netmsgSvcReconnectPropBeginRead");
	const std::string end_read = function_definition_block(netmsg,
		"u32 netmsgSvcReconnectPropEndRead");
	const std::string membership = function_definition_block(netmsg,
		"static s32 netmsgPropSchedulerMembershipCount");
	const std::string prune = function_definition_block(netmsg,
		"static u16 netmsgReconnectPruneLocalWeaponChildren");

	REQUIRE(spawn_write.find("netReconnectPlanPropPlacement") !=
		std::string::npos);
	REQUIRE(spawn_write.find("netmsgPropSchedulerMembershipCount") !=
		std::string::npos);
	REQUIRE(membership.find("visited < g_Vars.maxprops") !=
		std::string::npos);
	REQUIRE(membership.find("return prop ? -1 : count") !=
		std::string::npos);
	REQUIRE(prune.find("child->syncid == 0") != std::string::npos);
	REQUIRE(prune.find("child->type == PROPTYPE_WEAPON") !=
		std::string::npos);
	REQUIRE(prune.find("objFreePermanently(child->obj, true)") !=
		std::string::npos);
	REQUIRE(begin_read.find("netmsgReconnectPruneLocalWeaponChildren") !=
		std::string::npos);

	const size_t complete_payload = spawn_read.find(
		"if (msgflags & (1 << 1))");
	const size_t publish_boundary = spawn_read.find(
		"Publish exactly one topology", complete_payload);
	const size_t delist = spawn_read.find("propDelist(prop)", publish_boundary);
	const size_t reparent = spawn_read.find("propReparent(prop, parent)", delist);
	const size_t map_publish = spawn_read.find(
		"netSyncIdMapSet(syncid, prop)", reparent);
	REQUIRE(complete_payload != std::string::npos);
	REQUIRE(publish_boundary != std::string::npos);
	REQUIRE(delist != std::string::npos);
	REQUIRE(reparent != std::string::npos);
	REQUIRE(map_publish != std::string::npos);
	REQUIRE(complete_payload < publish_boundary);
	REQUIRE(publish_boundary < delist);
	REQUIRE(delist < reparent);
	REQUIRE(reparent < map_publish);
	REQUIRE(spawn_read.find(
		"NET_RECONNECT_PROP_PLACEMENT_INVALID") != std::string::npos);
	REQUIRE(end_read.find("scheduler_membership != 0") !=
		std::string::npos);
	REQUIRE(end_read.find("scheduler_membership != 1") !=
		std::string::npos);
	REQUIRE(end_read.find("topology=exclusive") != std::string::npos);
	REQUIRE(end_read.find("local_weapon_pruned=%u") !=
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
        manifest, "s32 manifestMPTransition(void)");
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
    const size_t exhaustion_check = arm.find("s_NetStageEpochSequence == UINT32_MAX");
    const size_t stage_reservation = arm.find("g_NetStageEpoch = ++s_NetStageEpochSequence;");
    REQUIRE(exhaustion_check < stage_reservation);
    REQUIRE(stage_reservation != std::string::npos);
    const std::string clear = function_block(net, "static void netResetStageReplication");
    const std::string rollback = function_block(net, "static void netServerRollbackStageReplicationBarrier");
    REQUIRE(clear.find("g_NetStageEpoch = 0;") != std::string::npos);
    REQUIRE(rollback.find("g_NetStageEpoch = prior_epoch;") != std::string::npos);
    REQUIRE(clear.find("s_NetStageEpochSequence") == std::string::npos);
    REQUIRE(rollback.find("s_NetStageEpochSequence") == std::string::npos);
    const size_t fresh_host = net.find("s_NetStageEpochSequence = 0;");
    REQUIRE(fresh_host != std::string::npos);
    REQUIRE(net.find("netResetStageReplication(\"server-start\", true);", fresh_host) != std::string::npos);
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
        "netSendToMatch(g_NetMatchRoomId, &g_NetMsg, false");
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

TEST_CASE("Listen server publishes its successful bound port before discovery and UI use",
          "[net][lifecycle][port][static][T-NETWORKING-011]")
{
    const auto source = read_text_file("port/src/net/net.c");
    const auto start = function_block(source, "s32 netStartServer");
    const auto create = start.find("g_NetHost = enet_host_create");
    const auto failure = start.find("if (!g_NetHost)", create);
    const auto bound = start.find("port = g_NetHost->address.port;", failure);
    const auto publish = start.find("g_NetServerPort = port;", bound);
    const auto discovery = start.find("netUpnpSetup(port)", publish);
    REQUIRE(create != std::string::npos); REQUIRE(failure != std::string::npos);
    REQUIRE(bound != std::string::npos); REQUIRE(publish != std::string::npos);
    REQUIRE(discovery != std::string::npos);
    REQUIRE(create < failure); REQUIRE(failure < bound); REQUIRE(bound < publish);
    REQUIRE(publish < discovery);
}

TEST_CASE("Match packets exclude connected room guests outside the committed roster",
          "[net][distribution][lifecycle][audience][static]")
{
    const auto net = read_text_file("port/src/net/net.c");
    const auto msg = read_text_file("port/src/net/netmsg.c");
    const auto send = function_definition_block(net, "u32 netSendToMatch(");
    const auto roster = function_definition_block(msg, "static u32 netmsgMatchClientMask(");
    REQUIRE(roster.find("s_CutsceneAuthority.full_client_mask") != std::string::npos);
    REQUIRE(roster.find("s_CutsceneAuthority.room_id != room_id") != std::string::npos);
    const auto server_roster = function_definition_block(msg, "u32 netmsgServerMatchClientMask(");
    REQUIRE(server_roster.find("g_NetMode == NETMODE_SERVER ? netmsgMatchClientMask(room_id) : 0") != std::string::npos);
    const auto stats = function_definition_block(msg, "static u32 netmsgReadPlayerStats(");
    REQUIRE(stats.find("!(netmsgMatchClientMask(srccl->room_id) & (1u << clid))") != std::string::npos);
    REQUIRE(send.find("netmsgServerMatchClientMask(room_id)") != std::string::npos);
    REQUIRE(send.find("!(admitted & (1u << (u32)i))") < send.find("enet_peer_send"));
    REQUIRE(send.find("cl->state != CLSTATE_GAME") < send.find("enet_peer_send"));
    REQUIRE(send.find("cl->room_id != room_id") < send.find("enet_peer_send"));
    const auto flush = function_definition_block(net, "static inline void netFlushSendBuffers(");
    REQUIRE(flush.find("netSendToMatch(g_NetMatchRoomId, &g_NetMsgRel") != std::string::npos);
    REQUIRE(flush.find("netSendToMatch(g_NetMatchRoomId, &g_NetMsg,") != std::string::npos);
    REQUIRE(send.find("g_NetSimPacketLoss") != std::string::npos);
    const auto distrib = read_text_file("port/src/net/netdistrib.c");
    const auto decline = function_definition_block(distrib, "s32 netDistribClientResolveConsent(");
    const auto failure = decline.find("decline could not be sent; consent retained for retry");
    const auto committed = decline.find("s_ManifestDeclined = 1", failure);
    REQUIRE(failure != std::string::npos);
    REQUIRE(committed != std::string::npos);
    REQUIRE(failure < committed);
    REQUIRE(decline.find("return 0;", failure) < committed);
}

TEST_CASE("Manifest consent owns the final local preparation state",
          "[net][distribution][lifecycle][manifest][static]")
{
    const auto msg = read_text_file("port/src/net/netmsg.c");
    const auto read = function_definition_block(msg, "u32 netmsgSvcMatchManifestRead(");
    const auto offer = read.find("netDistribClientBeginManifestOffer()");
    const auto prepare = read.find("g_NetLocalClient->state = CLSTATE_PREPARING");
    const auto check = read.find("manifestCheck(&g_ClientManifest)");
    REQUIRE(offer != std::string::npos);
    REQUIRE(prepare != std::string::npos);
    REQUIRE(check != std::string::npos);
    REQUIRE(offer < prepare);
    REQUIRE(prepare < check);
    REQUIRE(read.find("CLSTATE_PREPARING", check) == std::string::npos);
    const auto distrib = read_text_file("port/src/net/netdistrib.c");
    const auto reset = function_definition_block(distrib, "void netDistribClientBeginManifestOffer(");
    REQUIRE(reset.find("s_ManifestDeclined = 0") != std::string::npos);
}

TEST_CASE("Grid Room offers only exact required packages after proposal approval and guest consent",
	"[net][lifecycle][grid]")
{
	const auto room = read_text_file("port/fast3d/pdgui_menu_room.cpp");
	const auto popup = function_definition_block(room, "static void roomRenderGridModSharing(");
	const auto review = popup.find("netmsgGridReviewModProposal()");
	const auto approve_button = popup.find("ImGui::Button(\"Approve Mod Request\") && netmsgGridResolveModProposal(1)");
	const auto decline_button = popup.find("ImGui::Button(\"Decline\") && netmsgGridResolveModProposal(0)");
	REQUIRE(review < approve_button);
	REQUIRE(approve_button < decline_button);
	REQUIRE(decline_button != std::string::npos);
	const auto propose_button = popup.find("ImGui::Button(\"Propose and Allow Sharing\")");
	const auto donor_submit = popup.find("propose && netmsgGridProposeMod(chosen)");
	REQUIRE(propose_button < donor_submit);
	REQUIRE(donor_submit != std::string::npos);

	const auto io = read_text_file("port/src/forge_level_io.c");
	const auto share = function_definition_block(io, "s32 forgeLevelMatchPackageShareAllowed(");
	const auto type = share.find("gridMatchPackageTypeShareable(package->owner.type, package->suffix)");
	const auto sha = share.find("sha256Hash(exact_bytes, size, digest)");
	const auto exact = share.find("memcmp(digest, package->digest, sizeof(digest)) == 0");
	const auto origin = share.find("gridMatchPackageOrigin(package->suffix, exact_bytes, size) == GRID_ARCHIVE_ORIGINAL");
	REQUIRE(type < sha);
	REQUIRE(sha < exact);
	REQUIRE(exact < origin);
	REQUIRE(origin != std::string::npos);
	CHECK(share.find("size != package->size") < sha);
	CHECK(share.find("package->bundled") < sha);

	const auto messages = read_text_file("port/src/net/netmsg.c");
	const auto resolve = function_definition_block(messages, "s32 netmsgGridResolveModProposal(");
	const auto peer = resolve.find("netGridProposalHostClient(&peer)");
	const auto refuse = function_block(resolve, "if (!accept)");
	const auto approved_missing = resolve.find("!assetCatalogResolveAny(package->owner.id)");
	const auto approve = resolve.find("gridModProposalHostApprove(s_GridProposalHost, &peer, nonce, &missing,");
	const auto request = resolve.find("netGridProposalSend(cl, SVC_GRID_MOD_PROPOSAL, &request)");
	REQUIRE(peer < resolve.find(refuse));
	REQUIRE(resolve.find(refuse) < approved_missing);
	REQUIRE(approved_missing < approve);
	REQUIRE(approve < request);
	REQUIRE(request != std::string::npos);
	CHECK(refuse.find("netGridProposalResult(cl, offer, 0)") != std::string::npos);
	CHECK(refuse.find("return 1;") != std::string::npos);
	CHECK(refuse.find("netGridProposalSend(") == std::string::npos);
	const auto proposal_io = read_text_file("port/src/net/grid_mod_proposal.c");
	const auto accept = function_definition_block(proposal_io, "s32 gridModProposalHostApprove(");
	const auto donor_grant = accept.find("packagesAllowed(missing, &proposal->packages)");
	const auto approved_transfer = accept.find("gridMatchTransferBegin(&host->transfer, &approved.offer,");
	REQUIRE(donor_grant < approved_transfer);
	REQUIRE(approved_transfer != std::string::npos);

	const auto legacy_catalog = function_definition_block(messages, "static void catalogInfoWriteChunkCb(");
	CHECK(legacy_catalog.find("gridMatchPackageLocation(e->descriptor_path, archive, sizeof(archive),") != std::string::npos);
	CHECK(legacy_catalog.find("member, sizeof(member), NULL) && member[0]") != std::string::npos);
	const auto legacy_transfer = read_text_file("port/src/net/netdistrib.c");
	const auto original_guard = function_definition_block(legacy_transfer, "static s32 distribGridArchiveWithheld(");
	CHECK(original_guard.find("gridMatchPackageTypeShareable(type, suffix)") != std::string::npos);
	CHECK(original_guard.find("gridMatchPackageOrigin(suffix, bytes, size) : gridArchiveOrigin(name, bytes, size)") != std::string::npos);
	const auto overlay_source = read_text_file("port/fast3d/pdgui_lobby_distrib.cpp");
	const auto overlay = function_definition_block(overlay_source, "void pdguiDistribOverlayRender(");
	CHECK(overlay.find("focusNewGridPrompt = gridPrompt && !showedGridPrompt") != std::string::npos);
	CHECK(overlay.find("focusNewGridPrompt || ImGui::IsWindowAppearing()") != std::string::npos);

	const auto relay = function_definition_block(messages, "static s32 netGridProposalMayRelay(");
	CHECK(relay.find("forgeLevelVariantDependencyOwnsPackage(arena_id, arena_digest, package)") != std::string::npos);
	CHECK(relay.find("room->id == room_id && netmsgGridRoomOwnsProposedPackage(package)") != std::string::npos);
	const auto owns_package = function_definition_block(messages, "s32 netmsgGridRoomOwnsProposedPackage(");
	CHECK(owns_package.find("lease->room_lifetime != room->lifetime") != std::string::npos);
	CHECK(owns_package.find("file->owner.type == package->owner.type && strcmp(file->owner.id, package->owner.id) == 0") != std::string::npos);
	CHECK(owns_package.find("strcmp(file->path, path) == 0") != std::string::npos);
	CHECK(owns_package.find("strcmp(path, package->path) != 0") != std::string::npos);
	CHECK(owns_package.find("assetCatalogGridSourceSetPath(lease->session.lease, file->package_index)") != std::string::npos);
	const auto prepare = function_definition_block(messages, "static s32 netLobbyStartPrepareGrid(");
	const auto closure = prepare.find("forgeLevelBuildPinnedMatchSources(plan->stage_id, roots,");
	const auto staged = prepare.find("forgeLevelStageLocalMatchSources(&sources, &offer, &cache,");
	const auto selected = prepare.find("i < sources.package_count", staged);
	const auto bytes = prepare.find("gridMatchCacheRead(cache, &offer, i, &size)", selected);
	const auto room_grant = prepare.find("netGridProposalMayRelay(plan->room_id, plan->stage_id, arena_digest, package)", bytes);
	const auto listing = prepare.find("sharing.indexes[sharing.count++] = (u16)i", room_grant);
	const auto wire = prepare.find("gridMatchSharedPackagesWrite(&wire, &sharing)", listing);
	REQUIRE(closure < staged);
	REQUIRE(staged < selected);
	REQUIRE(selected < bytes);
	REQUIRE(bytes < room_grant);
	REQUIRE(room_grant < listing);
	REQUIRE(listing < wire);
	REQUIRE(wire != std::string::npos);
	const auto host_share = prepare.find("&& forgeLevelMatchPackageShareAllowed(package, bytes, size)", room_grant);
	const auto host_inventory = prepare.find("assetCatalogGridPackageInventoryEligible(&sources, i, bytes, size,", host_share);
	REQUIRE(host_share < host_inventory);
	REQUIRE(host_inventory < listing);

	const auto donor = function_definition_block(messages, "s32 netmsgGridProposeMod(");
	const auto donor_bytes = donor.find("gridMatchCacheRead(cache, &proposal.offer, i, &size)");
	const auto donor_share = donor.find("forgeLevelMatchPackageShareAllowed(package, bytes, size)", donor_bytes);
	const auto donor_inventory = donor.find("assetCatalogGridPackageInventoryEligible(&proposal.sources, i, bytes, size,", donor_share);
	const auto donor_listing = donor.find("proposal.packages.indexes[proposal.packages.count++] = (u16)i", donor_inventory);
	const auto donor_send = donor.find("netGridProposalSend(g_NetLocalClient, CLC_GRID_MOD_PROPOSAL, &proposal)", donor_listing);
	REQUIRE(donor_bytes < donor_share);
	REQUIRE(donor_share < donor_inventory);
	REQUIRE(donor_inventory < donor_listing);
	REQUIRE(donor_listing < donor_send);
	REQUIRE(donor_send != std::string::npos);

	const auto guest = function_definition_block(messages, "u32 netmsgSvcGridMatchOfferRead(");
	const auto missing = guest.find("netGridFindMissingPackages(&sources, &shared, arena_id, &missing)");
	const auto verified = guest.find("forgeLevelStageMatchSourcesExcept(&sources, &offer, missing.indexes, missing.count,");
	const auto prompt = guest.find("netDistribClientBeginGridConsent(ids, missing.count, size, arena_only)");
	REQUIRE(missing < verified);
	REQUIRE(verified < prompt);
	REQUIRE(prompt != std::string::npos);
	const auto consent = function_definition_block(messages, "s32 netmsgGridResolveArenaConsent(");
	const auto decline = function_block(consent, "if (!accept)");
	const auto start_download = consent.find("netGridRequestNextSource(join, client)");
	CHECK(decline.find("netGridSendStatus(client, &join->identity, 0)") != std::string::npos);
	CHECK(decline.find("netGridRequestNextSource(") == std::string::npos);
	REQUIRE(consent.find(decline) < start_download);
	REQUIRE(start_download != std::string::npos);
	const auto package_request = function_definition_block(messages, "u32 netmsgClcGridArenaRequestRead(");
	const auto match_offer = package_request.find("!accept || !netGridOfferMatches(&offer, &pending->offer)");
	const auto advertised = package_request.find("s_GridHostShare.indexes[ordinal] != package");
	const auto queue = package_request.find("netDistribServerQueueGridArena(srccl, &offer, package, source->owner.id)");
	REQUIRE(match_offer < advertised);
	REQUIRE(advertised < queue);
	REQUIRE(queue != std::string::npos);
}

TEST_CASE("A detached Grid creator can freshly authenticate to the Lounge during the private match",
	"[net][lifecycle][grid-edit][static]")
{
	const auto net = read_text_file("port/src/net/net.c");
	const auto msg = read_text_file("port/src/net/netmsg.c");
	const auto connect = function_definition_block(net, "static void netServerEvConnect(");
	const auto auth = function_definition_block(msg, "u32 netmsgClcAuthRead(");
	const auto ban = connect.find("serverBansIsBanned(ip)");
	const auto resume = connect.find("grid_resume_lobby = ingame && !reconnect");
	const auto live_gate = connect.find("if (ingame && !grid_resume_lobby)");
	const auto fresh_slot = connect.find("!grid_resume_lobby || !netServerFindPreservedByClientId((u8)i)");
	REQUIRE(ban < resume);
	REQUIRE(resume < live_gate);
	REQUIRE(live_gate < fresh_slot);
	CHECK(connect.find("netGridEditPrivateResumeRoomOpen(g_NetMatchRoomId)", resume) < live_gate);
	CHECK(connect.find("DISCONNECT_LATE", live_gate) < fresh_slot);
	CHECK(connect.find("cl->flags = pp ? CLFLAG_ABSENT : 0;", fresh_slot) != std::string::npos);

	const auto auth_gate = auth.find("else if (ingame && !netGridEditPrivateResumeRoomOpen(g_NetMatchRoomId))");
	const auto lobby = auth.find("srccl->state = CLSTATE_LOBBY;");
	REQUIRE(auth.find("netmsgReconnectCredentialMatches(") < auth_gate);
	REQUIRE(auth_gate < lobby);
	CHECK(auth.find("netServerKick(srccl, DISCONNECT_LATE);", auth_gate) < lobby);
	CHECK(auth.find("netmsgSvcAuthWrite(&srccl->out, srccl);", lobby) != std::string::npos);
	CHECK(auth.find("netGridEditInvite(") == std::string::npos);
	CHECK(auth.find("roomJoin(") == std::string::npos);
}

TEST_CASE("Campaign autostart retains its mission across room default publication",
          "[net][lifecycle][coop][static]")
{
    const auto main = read_text_file("port/src/main.c");
    const auto stage = main.find("const char *stageId = mode == NETGAMEMODE_MP");
    REQUIRE(stage != std::string::npos);
    REQUIRE(main.find("? g_MatchConfig.stage_id : g_BootLaunchMpArena", stage) != std::string::npos);
    REQUIRE(main.find("netLobbyRequestStartWithSims(\n\t\tmode,\n\t\tstageId,", stage) != std::string::npos);
}

} // namespace
