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
    const size_t grid_stage_guard = read.find(
        "gridMatchRuntimeActive() || (g_NetStageEpoch && gridMatchRuntimePending())", context_gate);
    const size_t invalid_context = read.find("NET_LOBBY_START_INVALID_CONTEXT", context_gate);
    const size_t authority_gate = read.find(
        "if (!netLobbyStartIsAuthority(srccl, &room))");
    const size_t first_payload_read = read.find("netbufReadU8(src)");

    REQUIRE(context_gate != std::string::npos);
    REQUIRE(grid_stage_guard != std::string::npos);
    REQUIRE(invalid_context != std::string::npos);
    REQUIRE(authority_gate != std::string::npos);
    REQUIRE(first_payload_read != std::string::npos);
    REQUIRE(grid_stage_guard < invalid_context);
    REQUIRE(invalid_context < authority_gate);
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

TEST_CASE("Grid Continue retires the world and exact leases before Room resync", "[net][lifecycle][static]")
{
    const std::string bridge = read_text_file("port/fast3d/pdgui_bridge.c");
    const std::string main = read_text_file("port/src/pdmain.c");
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string continue_room = function_block(bridge, "void pdguiEndscreenExitToRoom");
    const std::string queue = function_block(main, "void mainQueueGridRoomReturn");
    const std::string loop = function_block(main, "void mainLoop(void)");
    const std::string tick = function_block(main, "void mainTick(void)");
    const std::string resync = function_block(netmsg, "void netSendLobbyResync");
    const std::string start = function_block(netmsg, "u32 netmsgClcLobbyStartRead");
    const std::string offer = function_block(netmsg, "u32 netmsgSvcGridMatchOfferRead");
    const std::string terminal = function_block(net, "static u32 netClientReadGridResult");

    REQUIRE(continue_room.find("mainQueueGridRoomReturn();") != std::string::npos);
    const size_t identity = queue.find("g_NetLocalClient->room_id != grid->offer.room_id");
    const size_t queue_system_stage = queue.find("mainChangeToStage(STAGE_TITLE);");
    REQUIRE(identity < queue_system_stage);
    REQUIRE(queue_system_stage != std::string::npos);

    const size_t room_return = loop.find("s_GridRoomBackdrop = mainGridRoomReturnPending()");
    REQUIRE(room_return != std::string::npos);
    const size_t stop = loop.rfind("lvStop();", room_return);
    const size_t release_manifest = loop.find("manifestReleaseCurrent();", stop);
    const size_t retire_exact_sources = loop.find("mainRetireGridStage();", release_manifest);
    REQUIRE(stop < release_manifest);
    REQUIRE(release_manifest < retire_exact_sources);
    REQUIRE(retire_exact_sources != std::string::npos);

    const size_t await_retirement = tick.find("gridMatchRuntimeIsIdle()");
    const size_t send = tick.find("netSendLobbyResync();", await_retirement);
    REQUIRE(await_retirement < send);
    REQUIRE(send != std::string::npos);
    REQUIRE(resync.find("if (mainGridRoomReturnPending()) return;") != std::string::npos);

    const size_t reviewing = start.find("if (plan.players[i].client->reviewing_results)");
    const size_t commit = start.find("if (!netLobbyStartCommit(&plan))");
    REQUIRE(reviewing < commit);
    REQUIRE(commit != std::string::npos);
    REQUIRE(offer.find("!gridMatchRuntimeIsIdle()") < offer.find("netbufReadStr(src)"));
    REQUIRE(offer.find("netbufReadStr(src)") != std::string::npos);

    const size_t commit_result = terminal.find("netmsgSvcStageEndRead(&stage_end, cl)");
    const size_t clear_epoch = terminal.find("netResetStageReplication(\"grid-result-commit\", true)");
    const size_t retain_result = terminal.find("netStoreGridResult(&result, &scorecard)");
    REQUIRE(commit_result < clear_epoch);
    REQUIRE(clear_epoch < retain_result);
    REQUIRE(retain_result != std::string::npos);

    const auto room_menu = read_text_file("port/fast3d/pdgui_menu_room.cpp");
    const auto played_variant = function_block(room_menu, "static void roomRenderPlayedVariant");
    const auto cleanup = function_block(room_menu, "static void roomClearPlayedVariantReview");
    const auto panel_status = played_variant.find("if (!panel.doc && panel.status[0]) ImGui::TextWrapped");
    const auto modal = played_variant.find("ImGui::BeginPopupModal(");
    const auto save = played_variant.find("forgeLevelSavePlayedVariant(");
    const auto warning = played_variant.find("snprintf(panel.status, sizeof(panel.status), \"%s\", err);", save);
    const auto success = played_variant.find("\"Saved %s to your local levels.\"", warning);
    const auto close_saved = played_variant.find("close = true;", success);
    REQUIRE(panel_status < modal);
    REQUIRE(modal < save);
    REQUIRE(save < warning);
    REQUIRE(warning < success);
    REQUIRE(success < close_saved);
    REQUIRE(close_saved != std::string::npos);
    CHECK(cleanup.find("panel.status") == std::string::npos);

    const auto lobby = read_text_file("port/fast3d/pdgui_lobby.cpp");
    const auto lobby_render = function_definition_block(lobby, "void pdguiLobbyRender(");
    const auto interactive = lobby_render.find(
        "const bool gameClient = mode == NETMODE_CLIENT || (mode == NETMODE_SERVER && !g_NetDedicated);");
    const auto leaving = lobby_render.find(
        "if (!gameClient || !s_InRoom || !netLocalClientInLobby()) pdguiRoomClearPlayedVariant();");
    const auto rendering = lobby_render.find("if (gameClient) {");
    REQUIRE(interactive < leaving);
    REQUIRE(leaving < rendering);
    REQUIRE(rendering != std::string::npos);
    CHECK(lobby_render.find("pdguiRoomClearPlayedVariant();",
        lobby_render.find("if (resultsOpen || s_ResultsPending)")) != std::string::npos);
}

TEST_CASE("Grid host checks the exact pending source binding before leaving the Room", "[net][lifecycle][static]")
{
    const auto net = read_text_file("port/src/net/netmsg.c");
    const auto runtime = read_text_file("port/src/grid_match_runtime.c");
    const auto filesystem = read_text_file("port/src/fs.c");
    const auto commit = function_definition_block(net, "static bool netLobbyStartCommit(");
    const auto countdown = function_definition_block(net, "void readyGateTickCountdown(");
    const auto abort = function_definition_block(net, "static void readyGateAbort(");
    const auto prepare = function_definition_block(runtime, "s32 gridMatchRuntimePrepare(");
    const auto validate = function_definition_block(runtime, "s32 gridMatchRuntimeValidatePendingStage(");
    const auto bindings = function_definition_block(runtime, "static s32 runtimeValidateStageBindings(");
    const auto publish = function_definition_block(runtime, "s32 gridMatchRuntimePublishStage(");

    const auto scope = commit.find("grid->offer.room_id != plan->room_id");
    const auto snapshot = commit.find("grid_offer = grid->offer;");
    const auto begin = commit.find("lobbyStartTransactionBegin(plan->room_id)");
    const auto retain = commit.find("s_ReadyGate.grid_offer = grid_offer;");
    REQUIRE(scope < snapshot);
    REQUIRE(snapshot < begin);
    REQUIRE(begin < retain);
    REQUIRE(retain != std::string::npos);

    const auto recheck = countdown.find("gridMatchRuntimeValidatePendingStage(s_ReadyGate.stagenum, &s_ReadyGate.grid_offer");
    const auto rollback = countdown.find("readyGateAbort(\"Grid sources changed before launch\");");
    const auto announce = countdown.find("readyGateBroadcastCountdown(MANIFEST_PHASE_LOADING)");
    const auto loading = countdown.find("roomTransition(room, ROOM_STATE_LOADING)");
    REQUIRE(recheck < rollback);
    REQUIRE(rollback < announce);
    REQUIRE(announce < loading);
    REQUIRE(loading != std::string::npos);
    CHECK(abort.find("lobbyStartTransactionRollback(canceller_name)") != std::string::npos);

    CHECK(validate.find("gridMatchCacheReady(match->cache, offer)") != std::string::npos);
    CHECK(validate.find("!match->prepared_archives") != std::string::npos);
    CHECK(validate.find("runtimeValidateStageBindings(match, err, err_cap)") != std::string::npos);
    CHECK(bindings.find("assetCatalogGetGeneration()") != std::string::npos);
    CHECK(bindings.find("strcmp(descriptor, asset->descriptor)") != std::string::npos);
    CHECK(validate.find("fsPublishPreparedStageArchives(") == std::string::npos);
    const auto prepare_archives = prepare.find("fsPrepareStageArchives(&match->prepared_archives");
    const auto own_cache = prepare.find("match->cache = *cache;");
    const auto mark_pending = prepare.find("s_PendingMatch = match;");
    REQUIRE(prepare_archives < own_cache);
    REQUIRE(own_cache < mark_pending);
    REQUIRE(mark_pending != std::string::npos);
    const auto validate_at_load = publish.find("gridMatchRuntimeValidatePendingStage(stage, offer");
    const auto register_sources = publish.find("fsPublishPreparedStageArchives(&match->prepared_archives)");
    REQUIRE(validate_at_load < register_sources);
    REQUIRE(register_sources != std::string::npos);

    const auto prepare_table = function_definition_block(filesystem, "s32 fsPrepareStageArchives(");
    CHECK(prepare_table.find("fsStageArchiveLocatorValid(") != std::string::npos);
    CHECK(prepare_table.find("qsort(") != std::string::npos);
    CHECK(prepare_table.find("strcmp(bindings[i - 1].root, bindings[i].root)") != std::string::npos);
    CHECK(prepare_table.find("s_StageArchives") == std::string::npos);
    const auto publish_table = function_definition_block(filesystem, "s32 fsPublishPreparedStageArchives(");
    const auto lock = publish_table.find("fsLockStageSource()");
    const auto busy = publish_table.find("s_StageArchives || s_ScenarioSourceRootLen");
    const auto install = publish_table.find("s_StageArchives = *prepared");
    const auto consume = publish_table.find("*prepared = NULL");
    REQUIRE(lock < busy);
    REQUIRE(busy < install);
    REQUIRE(install < consume);
    REQUIRE(consume != std::string::npos);
    CHECK(publish_table.find("calloc(") == std::string::npos);
    CHECK(publish_table.find("qsort(") == std::string::npos);
}

TEST_CASE("Grid guest downloads use a digest-named scratch path",
          "[net][lifecycle][grid-edit][static]")
{
    const auto io = read_text_file("port/src/grid_match_package_io.c");
    const auto messages = read_text_file("port/src/net/netmsg.c");
    const auto path = function_definition_block(io, "s32 gridMatchPackagePrivatePath(");
    const auto typed = path.find("gridMatchPackageTypeShareable(assetArchiveTypeForPath(suffix), suffix)");
    const auto private_root = path.find("fsFullPath(\"$S/grid-session\"");
    const auto absolute = path.find("assetPathIsAbsolute(resolved)");
    const auto digest = path.find("sha256ToHex(digest, hex)");
    const auto name = path.find("\"%s/%s%s\", resolved, hex, suffix");
    const auto bounded = path.find("length > FS_MAXPATH");
    const auto publish_path = path.find("memcpy(path, locator,");
    REQUIRE(typed < private_root);
    REQUIRE(private_root < absolute);
    REQUIRE(absolute < digest);
    REQUIRE(digest < name);
    REQUIRE(name < bounded);
    REQUIRE(bounded < publish_path);
    REQUIRE(publish_path != std::string::npos);
    CHECK(path.find("fsCreateDir(") == std::string::npos);
    CHECK(path.find("saveAtomicBegin(") == std::string::npos);

    const auto staged = function_definition_block(messages, "static s32 netGridWriteStagedSource(");
    const auto cached = staged.find("gridMatchCacheRead(join->cache, &join->identity, index, &size)");
    const auto write_verified = staged.find("bytes && netGridWriteSourceFile(&join->sources.packages[index], bytes, size, path)");
    REQUIRE(cached < write_verified);
    REQUIRE(write_verified != std::string::npos);

    const auto writer = function_definition_block(messages, "static s32 netGridWriteSourceFile(");
    const auto resolve = writer.find("gridMatchPackagePrivatePath(package->digest, package->suffix, folder, path)");
    const auto folder = writer.find("fsCreateDir(folder)");
    const auto write = writer.find("saveAtomicBegin(&output, path)");
    const auto body = writer.find("fwrite(bytes, 1, size, saveAtomicStream(&output)) != size");
    const auto abort = writer.find("saveAtomicAbort(&output)");
    const auto commit = writer.find("saveAtomicCommitNew(&output)");
    REQUIRE(resolve < folder);
    REQUIRE(folder < write);
    REQUIRE(write < body);
    REQUIRE(body < abort);
    REQUIRE(abort < commit);
    REQUIRE(commit != std::string::npos);
    CHECK(writer.find("path[0] = '\\0';", commit) != std::string::npos);
}

TEST_CASE("Grid arena lease preflight precedes registration and cleanup preserves saved revisions",
          "[net][lifecycle][grid-edit][static]")
{
    const auto scanner = read_text_file("port/src/assetcatalog_scanner.c");
    const auto preflight = function_definition_block(scanner, "s32 assetCatalogPreflightGridArenaLease(");
    const auto admit = function_definition_block(scanner, "s32 assetCatalogAdmitGridArenaLease(");
    const auto verify = admit.find("assetCatalogPreflightGridArenaLease(");
    const auto register_rows = admit.find("externalScanTransactionBegin(");
    REQUIRE(verify < register_rows);
    REQUIRE(register_rows != std::string::npos);
    CHECK(preflight.find("externalScanTransactionBegin(") == std::string::npos);
    CHECK(preflight.find("stageTableReserve") == std::string::npos);
    CHECK(preflight.find("gridLeaseFilePolicyMatches(") != std::string::npos);
    CHECK(preflight.find("assetCatalogPreviewGridArenaLeaseStage(scenario_id)") != std::string::npos);

    const auto stage = function_definition_block(scanner, "s32 assetCatalogPreviewGridArenaLeaseStage(");
    CHECK(stage.find("assetCatalogPeekStagenumPrivateSlot(scenario_id)") != std::string::npos);
    CHECK(stage.find("row->id == stagenum") != std::string::npos);
    CHECK(stage.find("row->id == STAGE_MP_SKEDAR") != std::string::npos);
    CHECK(stage.find("assetCatalogResolveStagenumPrivateSlot(") == std::string::npos);
    CHECK(stage.find("stageTableAppend(") == std::string::npos);
    const auto inspect = function_definition_block(scanner, "static s32 gridLeaseInspect(");
    const auto qualify = inspect.find("qualifyTypedArchiveSourcePaths(ini, scenario_ref)");
    const auto provider = inspect.find("fileProviderCanInternPaths(paths,", qualify);
    const auto output = inspect.find("*out = candidate;");
    REQUIRE(qualify < provider);
    REQUIRE(provider < output);
    REQUIRE(output != std::string::npos);
    CHECK(inspect.find("const char *paths[] = {scenario_ref, primary};") != std::string::npos);

    const auto runtime = read_text_file("port/src/grid_match_runtime.c");
    const auto transfer = function_definition_block(runtime, "s32 gridMatchRuntimeRetireDetachedArenaLease(");
    const auto policy = transfer.find("s_RetiringArena.file_policy = lease->file_policy");
    const auto release = transfer.find("memset(lease, 0, sizeof(*lease))");
    REQUIRE(policy < release);
    REQUIRE(release != std::string::npos);
    const auto retry = function_definition_block(runtime, "s32 gridMatchRuntimeRetryArenaLeaseRetirement(");
    const auto retire_catalog = retry.find("assetCatalogRetireGridArenaLease(");
    const auto scratch = function_block(retry, "s_RetiringArena.file_policy == GRID_ARENA_LEASE_TEMPORARY_FILE");
    REQUIRE(retire_catalog < retry.find(scratch));
    const auto remove_file = retry.find("remove(s_RetiringArena.path)");
    REQUIRE(remove_file != std::string::npos);
    CHECK(scratch.find("remove(s_RetiringArena.path)") != std::string::npos);
    CHECK(retry.find("remove(", remove_file + 1) == std::string::npos);

    const auto inspect_package = function_definition_block(scanner, "static s32 gridSourceSetInspectPackage(");
    const auto exact_bytes = inspect_package.find("memcmp(sha, package->digest, sizeof(sha)) == 0");
    const auto inventory = function_definition_block(scanner, "static s32 gridSourceSetInspectInventory(");
    const auto members = inspect_package.find("gridSourceSetInspectInventory(scan, package_index, bytes, size,");
    CHECK(inventory.find("gridSourceSetChildType(entry->asset.type, package->owner.type, entry->member_chain)")
        != std::string::npos);
    const auto child_type = function_definition_block(scanner, "static s32 gridSourceSetChildType(");
    CHECK(child_type.find("gridMatchPackageMemberOwner(outer_type, chain, &owner, &member)") != std::string::npos);
    CHECK(child_type.find("owner == ASSET_EFFECT && effectNestedMemberType(member, &child) && child == ASSET_TEXTURE")
        != std::string::npos);
    const auto record_rows = function_definition_block(scanner, "static s32 gridSourceSetRecordRows(");
    CHECK(record_rows.find("gridSourceSetChildType(entry->asset.type, owner->owner.type, entry->member_chain)")
        != std::string::npos);
    CHECK(inventory.find("arena && scenarios != 1") != std::string::npos);
    CHECK(inventory.find("gridWeaponInspectMembers(scan, bytes, size,") != std::string::npos);
    CHECK(inventory.find("entry->member_chain[0] && !seen_members[i]") != std::string::npos);
    const auto member = function_definition_block(scanner, "static s32 gridWeaponInspectMember(");
    const auto extract = member.find("modArchiveExtractMemAlloc(");
    REQUIRE(extract != std::string::npos);
    CHECK(member.find("size > admission->remaining") < extract);
    CHECK(member.find("scan->depth >= MOD_ASSET_VALIDATE_MAX_DEPTH") < extract);
    CHECK(member.find("if (*seen)") < extract);
    CHECK(member.find(": gridWeaponChildType(entry->asset.type, name)") < extract);
    const auto nested_format = member.find("assetArchiveValidateBytes(nested, length, name", extract);
    const auto nested_identity = member.find("gridSourceSetReadDescriptor(nested, length, name", nested_format);
    const auto nested_texture = member.find("gridSourceSetValidateTexture(nested, length, entry->asset.id", nested_identity);
    REQUIRE(extract < nested_format);
    REQUIRE(nested_format < nested_identity);
    REQUIRE(nested_identity < nested_texture);
    REQUIRE(nested_texture != std::string::npos);
    const auto detached = function_definition_block(scanner, "s32 assetCatalogGridPackageInventoryEligible(");
    CHECK(detached.find(".remaining = NET_DISTRIB_MAX_SESSION, .strict_inventory = 1") != std::string::npos);
    CHECK(detached.find("gridSourceSetInspectInventory(&scan, package_index, bytes, size,") != std::string::npos);
    CHECK(detached.find("gridMatchPackageLoad(") == std::string::npos);
    CHECK(detached.find("assetCatalogRegister(") == std::string::npos);
    const auto release_format = inspect_package.find("assetArchiveValidateBytes(bytes, size, path, ASSET_ARCHIVE_VALIDATE_RELEASE,");
    const auto original = inspect_package.find("gridMatchPackageOrigin(path, bytes, size) == GRID_ARCHIVE_ORIGINAL");
    const auto identity = inspect_package.find("gridSourceSetReadDescriptor(bytes, size, path, package->owner.type, package->owner.id,");
    const auto native_arena = inspect_package.find("assetCatalogPreflightGridArenaLease(path, arena_id, scenario_id,");
    REQUIRE(exact_bytes < members);
    REQUIRE(members < release_format);
    REQUIRE(release_format < original);
    REQUIRE(original < identity);
    REQUIRE(identity < native_arena);
    REQUIRE(native_arena != std::string::npos);
    CHECK(inspect_package.find("GRID_ARENA_LEASE_TEMPORARY_FILE, bytes, size, &source", native_arena) != std::string::npos);

    const auto set_admit = function_definition_block(scanner, "s32 assetCatalogAdmitGridSourceSet(");
    const auto seal = set_admit.find("gridMatchSourcesSeal(&sealed,");
    const auto expected_path = set_admit.find("gridMatchPackagePrivatePath(package->digest, package->suffix, folder, expected)");
    const auto exact_path = set_admit.find("strcmp(expected, paths[i]) == 0");
    const auto preflight_set = set_admit.find("gridSourceSetInspectPackage(set, indices[i],");
    CHECK(set_admit.find(".remaining = NET_DISTRIB_MAX_SESSION, .strict_inventory = 1") < preflight_set);
    const auto transaction = set_admit.find("externalScanTransactionBegin(&transaction)");
    const auto register_set = set_admit.find("registerTypedPdDescriptorFile(set->packages[i].path,");
    const auto compare_rows = set_admit.find("gridSourceSetValidateRows(&admission, &transaction, err, err_cap)");
    const auto recheck = set_admit.find("modArchiveSha256(set->packages[i].path, digest)");
    const auto rollback = set_admit.find("externalScanTransactionRollback(&transaction)");
    const auto publish_set = set_admit.find("*out = set;");
    REQUIRE(seal < expected_path);
    REQUIRE(expected_path < exact_path);
    REQUIRE(exact_path < preflight_set);
    REQUIRE(preflight_set < transaction);
    REQUIRE(transaction < register_set);
    REQUIRE(register_set < compare_rows);
    REQUIRE(compare_rows < recheck);
    REQUIRE(recheck < rollback);
    REQUIRE(rollback < publish_set);
    REQUIRE(publish_set != std::string::npos);
    const auto rows = function_definition_block(scanner, "static s32 gridSourceSetValidateRows(");
    CHECK(rows.find("catalogEntrySnapshotMatches(prior, row)") != std::string::npos);
    CHECK(rows.find("catalogDepValidateAppended(&transaction->deps, gridSourceSetAcceptEdge,") != std::string::npos);

    const auto messages = read_text_file("port/src/net/netmsg.c");
    const auto guest = function_definition_block(messages, "static s32 netGridPrepareStagedTemporarySources(");
    const auto cache_ready = guest.find("gridMatchCacheReady(join->cache, &join->identity)");
    const auto scratch_write = guest.find("netGridWriteStagedSource(join, index, file->path,");
    const auto guest_admit = guest.find("assetCatalogAdmitGridSourceSet(&join->sources, session.count, join->missing.indexes,");
    const auto prepare = guest.find("gridMatchRuntimePrepare(&join->sources, &join->identity, &join->cache,");
    const auto ownership = guest.find("gridMatchRuntimeOwnSourceSet(&session,");
    const auto cancel_pending = guest.find("gridMatchRuntimeCancelPending(");
    const auto retire_scratch = guest.find("gridMatchRuntimeRetireDetachedSourceSet(&session,");
    REQUIRE(cache_ready < scratch_write);
    REQUIRE(scratch_write < guest_admit);
    REQUIRE(guest_admit < prepare);
    REQUIRE(prepare < ownership);
    REQUIRE(ownership < cancel_pending);
    REQUIRE(cancel_pending < retire_scratch);
    REQUIRE(retire_scratch != std::string::npos);

    const auto session_retry = function_definition_block(runtime, "s32 gridMatchRuntimeRetrySourceSetRetirement(");
    const auto retire_set = session_retry.find("assetCatalogRetireGridSourceSet(&s_RetiringSources.lease,");
    const auto unlink_scratch = session_retry.find("remove(file->path)");
    REQUIRE(retire_set < unlink_scratch);
    REQUIRE(unlink_scratch != std::string::npos);
}

TEST_CASE("Grid Fiesta seals host eligibility and uses it through activation and sparse spawns",
          "[net][lifecycle][grid-match][static]")
{
    const auto messages = read_text_file("port/src/net/netmsg.c");
    const auto prepare = function_definition_block(messages, "static s32 netLobbyStartPrepareGrid(");
    const auto collect = prepare.find("manifestCollectSpawnWeaponPool(fiesta,");
    const auto expand = prepare.find("manifestAppendCatalogClosure(&plan->manifest, fiesta[i].id", collect);
    const auto private_sources = prepare.find("forgeLevelBuildPrivateGameplaySources(", expand);
    const auto private_pool = prepare.find("gridMatchSourcesSetFiestaPool(&gameplay,", private_sources);
    const auto all_sources = prepare.find("forgeLevelBuildPinnedMatchSources(", private_pool);
    const auto match_pool = prepare.find("gridMatchSourcesSetFiestaPool(&sources,", all_sources);
    const auto offer = prepare.find("gridMatchOfferWrite(&wire, &offer, &sources,", match_pool);
    REQUIRE(collect < expand);
    REQUIRE(expand < private_sources);
    REQUIRE(private_sources < private_pool);
    REQUIRE(private_pool < all_sources);
    REQUIRE(all_sources < match_pool);
    REQUIRE(match_pool < offer);
    REQUIRE(offer != std::string::npos);

    const auto manifest = read_text_file("port/src/net/netmanifest.c");
    const auto enumerate = function_definition_block(manifest, "s32 manifestCollectSpawnWeaponPool(");
    CHECK(enumerate.find("assetCatalogIterateUnlockedByType(ASSET_WEAPON, s_collectSpawnWeaponId") != std::string::npos);
    CHECK(enumerate.find("pool.overflow || !pool.count") != std::string::npos);
    const auto match = function_definition_block(messages, "static s32 netGridManifestMatches(");
    CHECK(match.find("if (!source->fiesta_pool) continue;") != std::string::npos);
    CHECK(match.find("row->type == MANIFEST_TYPE_WEAPON && strcmp(row->id, source->asset.id)") != std::string::npos);

    const auto host_stage = function_definition_block(messages, "static bool netStageStartWritePrepare(");
    const auto guest_stage = function_definition_block(messages, "u32 netmsgSvcStageStartRead(");
    CHECK(host_stage.find("gridMatchRuntimeFiestaMatches(&grid->offer,") != std::string::npos);
    const auto guest_pool = guest_stage.find("gridMatchRuntimeFiestaMatches(&grid->offer, wireMode == SPAWNWEAPON_MODE_FIESTA)");
    const auto client_commit = guest_stage.find("g_MatchConfig.spawnWeaponMode = plan.spawn_weapon_mode;");
    REQUIRE(guest_pool < client_commit);
    REQUIRE(client_commit != std::string::npos);

    const auto main = read_text_file("port/src/pdmain.c");
    const auto activate = function_block(main, "if (s_GridStageQueued == g_StageNum)");
    const auto load = activate.find("manifestMPTransition()");
    const auto validate = activate.find("gridMatchRuntimeValidateActiveFiesta(");
    const auto retire = activate.find("mainRetireGridStage()", validate);
    REQUIRE(load < validate);
    REQUIRE(validate < retire);
    REQUIRE(retire != std::string::npos);
    const auto picker = function_definition_block(read_text_file("port/src/net/matchsetup.c"),
        "s32 spawnWeaponPickFromMatchManifest(");
    const auto exact = picker.find("gridMatchRuntimeFiestaSlots(&pool)");
    const auto legacy = picker.find("spawnWeaponSelectManifest()");
    REQUIRE(exact < legacy);
    REQUIRE(legacy != std::string::npos);
    CHECK(read_text_file("src/game/player.c").find("spawnWeaponPickFromMatchManifest()") != std::string::npos);
    CHECK(read_text_file("src/game/bot.c").find("spawnWeaponPickFromMatchManifest()") != std::string::npos);

    const auto setup = function_definition_block(read_text_file("src/game/setup.c"), "void setupCreateProps(");
    const auto force = setup.find("matchOptionsForceBit(&g_MatchConfig.options,");
    const auto keep_fiesta = setup.find("gridMatchRuntimeFiestaMatches(&grid->offer, 1)", force);
    const auto fallback = setup.find("else if (!userPickedSpawnWeapon)", keep_fiesta);
    REQUIRE(force < keep_fiesta);
    REQUIRE(keep_fiesta < fallback);
    REQUIRE(fallback != std::string::npos);
}

TEST_CASE("Grid scopes typed combat messages to the exact admitted stage", "[net][lifecycle][static]")
{
    const auto messages = read_text_file("port/src/net/netmsg.c");
    const auto write_damage = function_definition_block(messages, "u32 netmsgSvcChrDamageWrite(");
    const auto encoded = write_damage.find("netmsgGridEncodeWeaponRef(gset->weaponnum)");
    const auto opcode = write_damage.find("grid_epoch ? SVC_GRID_CHR_DAMAGE : SVC_CHR_DAMAGE");
    const auto epoch = write_damage.find("netbufWriteU32(dst, grid_epoch)");
    const auto metadata = write_damage.find("netbufWriteGset(dst, gset, grid_epoch != 0, ref)");
    REQUIRE(encoded < opcode);
    REQUIRE(opcode < epoch);
    REQUIRE(epoch < metadata);
    REQUIRE(metadata != std::string::npos);

    const auto read_damage = function_definition_block(messages, "u32 netmsgSvcChrDamageRead(");
    const auto tail = read_damage.find("netbufReadCoord(src, &explosionpos)");
    const auto route = read_damage.find("epoch != active_epoch || !netmsgGridMovePeerReady(srccl)");
    const auto typed = read_damage.find("netmsgGridDecodeWeaponRef(weapon_ref, &weapon)");
    const auto apply = read_damage.find("chrDamage(chrprop->chr");
    REQUIRE(tail < route);
    REQUIRE(route < typed);
    REQUIRE(typed < apply);
    REQUIRE(apply != std::string::npos);

    const auto resolve = function_definition_block(messages, "s32 netmsgGridDecodeWeaponRef(");
    REQUIRE(resolve.find("ref < WEAPON_CUSTOM_START") != std::string::npos);
    REQUIRE(resolve.find("row->type != MANIFEST_TYPE_WEAPON") != std::string::npos);
    REQUIRE(resolve.find("strcmp(entry->catalog_id, row->id) != 0") != std::string::npos);
    REQUIRE(resolve.find("gridMatchRuntimeResolveSpawnWeapon(&grid->offer, entry->catalog_id, &local)")
        != std::string::npos);

    const auto offer = function_definition_block(messages, "static s32 netLobbyStartPrepareGrid(");
    const auto reconnect = function_definition_block(messages, "static s32 netmsgGridReconnectOfferWrite(");
    REQUIRE(offer.find("netbufWriteU8(&wire, GRID_GAMEPLAY_WIRE_REVISION)") != std::string::npos);
    REQUIRE(reconnect.find("netbufWriteU8(dst, GRID_GAMEPLAY_WIRE_REVISION)") != std::string::npos);
    for (const char *signature : {"u32 netmsgSvcGridMatchOfferRead(", "u32 netmsgSvcGridReconnectOfferRead("}) {
        const auto parse = function_definition_block(messages, signature);
        const auto revision = parse.find("netbufReadU8(src) != GRID_GAMEPLAY_WIRE_REVISION");
        const auto manifest = parse.find("manifestDeserializeStrict(");
        REQUIRE(revision < manifest);
        REQUIRE(manifest != std::string::npos);
    }
}

TEST_CASE("Grid seals the typed mesh the model provider actually reads", "[net][lifecycle][static]")
{
    const auto runtime = read_text_file("port/src/grid_match_runtime.c");
    const auto descriptor = function_definition_block(runtime,
        "s32 gridMatchCatalogArchiveDescriptor(");
    const auto provider = descriptor.find(
        "entry->type == ASSET_MODEL && runtimeProviderDescriptor(entry, provider_path, out, capacity)");
    const auto scanner = descriptor.find("strstr(entry->descriptor_path,");
    REQUIRE(provider < scanner);
    REQUIRE(scanner != std::string::npos);

    const auto level = read_text_file("port/src/forge_level_io.c");
    const auto inspect = function_definition_block(level,
        "static s32 gridInspectSourceWithTextures(");
    const auto exact_owner = inspect.find("gridMatchCatalogArchiveDescriptor(entry,");
    const auto identity = inspect.find("strcmp(iniGet(ini, \"catalog_id\", \"\"), asset->id) == 0");
    const auto materials = inspect.find("gridVisitModelTextures(entry, archive,");
    REQUIRE(exact_owner < identity);
    REQUIRE(identity < materials);
    REQUIRE(materials != std::string::npos);

    const auto visit = function_definition_block(level, "static s32 gridVisitModelTextures(");
    const auto source_match = visit.find(
        "strcmp(absolute, archive->path) != 0 || strcmp(chain, archive->member_chain) != 0");
    const auto read_mtl = visit.find("gridMatchPackageReadMember(mesh, size, \"model.mtl\",");
    REQUIRE(source_match < read_mtl);
    REQUIRE(read_mtl != std::string::npos);
}

TEST_CASE("Grid Room score review uses the committed scorecard after stage retirement", "[net][lifecycle][static]")
{
    const auto net = read_text_file("port/src/net/net.c");
    const auto freeze = function_definition_block(net, "static s32 netFreezeGridStageEnd(");
    const auto publish = function_definition_block(net, "static bool netServerFlushCutsceneAuthorityPacket(");
    const auto receive = function_definition_block(net, "static u32 netClientReadGridResult(");
    const auto accessor = function_definition_block(net, "const grid_match_scorecard_t *netGridLastMatchScores(");
    const auto clear = function_definition_block(net, "void netGridClearMatchResult(");
    const auto finalize = function_definition_block(net, "static void netCompleteGridScorecard(");
    const auto display = function_definition_block(read_text_file("port/fast3d/pdgui_menu_room.cpp"),
        "static void roomRenderGridResult(");
    const auto host_scorecard = freeze.find("gridMatchResultReadScores(&result, wire.data + 1, wire.wp - 1, &end->grid_scorecard)");
    const auto host_publish = publish.find("netStoreGridResult(&end->grid_result, &end->grid_scorecard)");
    const auto client_scorecard = receive.find("gridMatchResultReadScores(&result, scores + 1, score_bytes - 1, &scorecard)");
    const auto client_publish = receive.find("netStoreGridResult(&result, &scorecard)");
    REQUIRE(host_scorecard != std::string::npos);
    REQUIRE(host_publish != std::string::npos);
    REQUIRE(client_scorecard != std::string::npos);
    REQUIRE(client_publish != std::string::npos);
    CHECK(freeze.find("sha256Hash(wire.data + 1, wire.wp - 1, result.score_sha256)") < host_scorecard);
    CHECK(publish.find("netSendToMatch(") < host_publish);
    CHECK(client_scorecard < receive.find("netmsgSvcPlayerScoresRead("));
    CHECK(receive.find("netmsgSvcPlayerScoresRead(") < receive.find("netCompleteGridScorecard(&scorecard)"));
    CHECK(receive.find("netCompleteGridScorecard(&scorecard)") < receive.find("netmsgSvcStageEndRead("));
    CHECK(receive.find("netmsgSvcStageEndRead(") < client_publish);
    CHECK(finalize.find("scenarioCalculatePlayerScore(config, slot, &row->score") != std::string::npos);
    CHECK(accessor.find("return netGridLastMatchResult() ? &s_LastGridScores : NULL;") != std::string::npos);
    CHECK(clear.find("memset(&s_LastGridScores, 0,") != std::string::npos);
    CHECK(display.find("netGridLastMatchScores()") != std::string::npos);
    CHECK(display.find("g_MpAllChrConfigPtrs") == std::string::npos);
}

TEST_CASE("Grid guest retains the played arena from its installed or leased stage cache", "[net][lifecycle][static]")
{
    const std::string runtime = read_text_file("port/src/grid_match_runtime.c");
    const std::string prepare = function_block(runtime, "s32 gridMatchRuntimePrepare(");
    const std::string retain = function_block(runtime, "s32 gridMatchRuntimeRememberReturnedArena(");
    const std::string terminal = function_block(read_text_file("port/src/net/net.c"),
        "static u32 netClientReadGridResult");
    const std::string lease = function_block(retain, "if (lease->arena_id[0])");

    REQUIRE(prepare.find("match->arena_package_index = arena_source->package_index;") != std::string::npos);
    REQUIRE(prepare.find("sizeof(match->arena_stage_root), scenario_root") != std::string::npos);
    REQUIRE(prepare.find("runtimeBindRoot(match, scenario_root") != std::string::npos);
    REQUIRE(retain.find("g_NetMode != NETMODE_CLIENT") != std::string::npos);
    REQUIRE(retain.find("const char *bound_archive = match->arena_stage_root;") != std::string::npos);
    REQUIRE(lease.find("bound_archive = lease->archive_path;") != std::string::npos);
    REQUIRE(retain.find("gridMatchPackageLoad(") == std::string::npos);

    const size_t result_identity = retain.find("gridMatchResultMatchesStage(result,");
    const size_t cache = retain.find("gridMatchCachePackage(match->cache,");
    const size_t binding = retain.find("runtimeHasBoundArchive(match, bound_archive, bytes, size)");
    const size_t rehash = retain.find("sha256Hash(copy, size, digest)");
    const size_t publish = retain.find("s_ReturnedArena.bytes = copy;");
    REQUIRE(result_identity < cache);
    REQUIRE(cache < binding);
    REQUIRE(binding < rehash);
    REQUIRE(rehash < publish);
    REQUIRE(publish != std::string::npos);
    REQUIRE(retain.find("size > NET_DISTRIB_MAX_COMP") < binding);

    const size_t remember = terminal.find("gridMatchRuntimeRememberReturnedArena(&result)");
    const size_t stage_end = terminal.find("netmsgSvcStageEndRead(&stage_end, cl)");
    REQUIRE(remember < stage_end);
    REQUIRE(stage_end != std::string::npos);
}

TEST_CASE("Grid startup rollback releases the exact manifest before the stage leases", "[net][lifecycle][static]")
{
    const std::string main = read_text_file("port/src/pdmain.c");
    const std::string loop = function_block(main, "void mainLoop(void)");
    const std::string activate = function_block(loop, "if (s_GridStageQueued == g_StageNum)");
    const std::string player_failure = function_block(loop, "if (player_result != PLAYMGR_ALLOC_OK)");
    const std::string world_failure = function_block(loop, "if (!lvReset(g_StageNum))");
    const std::string release = "if (gridMatchRuntimeActive() || saved_variant) manifestReleaseCurrent();";

    for (const auto &failure : {player_failure, world_failure}) {
        const size_t release_manifest = failure.find(release);
        const size_t retire_exact_sources = failure.find("mainRetireGridStage();");
        REQUIRE(retire_exact_sources != std::string::npos);
        REQUIRE(release_manifest < retire_exact_sources);
    }
    REQUIRE(world_failure.find("lvStop();") < world_failure.find(release));
    const auto validate_spawn = activate.find("gridMatchRuntimeValidateActiveSpawnWeapon(");
    const auto release_manifest = activate.find("if (manifest_active) manifestReleaseCurrent();");
    const auto retire_exact_sources = activate.find("mainRetireGridStage();");
    REQUIRE(validate_spawn < release_manifest);
    REQUIRE(release_manifest < retire_exact_sources);
    REQUIRE(retire_exact_sources != std::string::npos);
}

TEST_CASE("Grid process shutdown stops the world before retiring the exact packages", "[net][lifecycle][static]")
{
    const std::string app = read_text_file("port/src/main.c");
    const std::string stage = read_text_file("port/src/pdmain.c");
    const std::string cleanup = function_block(app, "static void cleanup(void)");
    const std::string stop = function_block(stage, "void mainStopGridWorldForShutdown(void)");
    const std::string retire = function_block(stage, "void mainRetireGridForShutdown(void)");

    const size_t stop_world = cleanup.find("mainStopGridWorldForShutdown();");
    const size_t stop_scene = cleanup.find("sceneShutdown();");
    const size_t stop_ui = cleanup.find("pdguiShutdown();");
    const size_t recover = cleanup.find("netCrashRecoveryMarkClean();");
    const size_t disconnect = cleanup.find("netDisconnect();");
    const size_t release = cleanup.find("mainRetireGridForShutdown();");
    const size_t stop_mods = cleanup.find("modmgrShutdown();");
    REQUIRE(stop_world < stop_scene);
    REQUIRE(stop_scene < stop_ui);
    REQUIRE(stop_ui < recover);
    REQUIRE(recover < disconnect);
    REQUIRE(disconnect < release);
    REQUIRE(release < stop_mods);
    REQUIRE(stop_mods != std::string::npos);

    REQUIRE(stop.find("if (!s_GridWorldReady) return;") < stop.find("lvStop();"));
    REQUIRE(stop.find("lvStop();") != std::string::npos);
    REQUIRE(retire.find("manifestReleaseCurrent();") < retire.find("mainRetireGridStage();"));
    REQUIRE(retire.find("mainRetireGridStage();") != std::string::npos);
}

TEST_CASE("Grid live resume admits a Room member and rebinds only after an exact creator grant",
          "[net][lifecycle][static]")
{
    const std::string messages = read_text_file("port/src/net/netmsg.c");
    const std::string editor = read_text_file("port/src/net/grid_edit_room_net.c");
    const std::string join = function_block(messages, "u32 netmsgClcRoomJoinRead");
    const std::string admit = function_block(editor, "s32 netGridEditPrivateResumeCanJoin");
    const std::string grant = function_block(editor, "static void editReceiveSnapshot(struct");

    const size_t lobby = join.find("srccl->state != CLSTATE_LOBBY");
    const size_t permitted = join.find("netGridEditPrivateResumeCanJoin(room_id, srccl)");
    const size_t private_join = join.find("roomJoinForClientDuringPrivatePlaytest");
    const size_t success = join.find("result != ROOM_RESULT_OK", private_join);
    const size_t notify = join.find("netGridEditPrivateResumeAdmitted(room_id, srccl)", success);
    REQUIRE(lobby < permitted);
    REQUIRE(permitted < private_join);
    REQUIRE(private_join < success);
    REQUIRE(success < notify);
    REQUIRE(notify != std::string::npos);
    REQUIRE(admit.find("editAuthenticatedLobbyPeer(client)") != std::string::npos);
    REQUIRE(admit.find("editRoomInviteMatches(room_id, client)") != std::string::npos);

    const size_t acknowledgement = grant.find("gridEditSnapshotOfferRead(payload, GRID_EDIT_SNAPSHOT_ACK");
    const size_t native_grant = grant.find("gridEditRoomHostGrantResume", acknowledgement);
    const size_t active_world = grant.find("editActivePlaytestWorld(ack.identity.room_id)", native_grant);
    const size_t rebind = grant.find("s_EditPlaytest.draft = ack.identity", active_world);
    const size_t send_tail = grant.find("editSendDelta(peer, &tail.deltas[i])", rebind);
    const size_t abort_tail = grant.find("editEndCreator(peer,", send_tail);
    const size_t send_grant = grant.find("editSendGrant(peer)", abort_tail);
    REQUIRE(acknowledgement < native_grant);
    REQUIRE(native_grant < active_world);
    REQUIRE(active_world < rebind);
    REQUIRE(rebind < send_tail);
    REQUIRE(send_tail < abort_tail);
    REQUIRE(abort_tail < send_grant);
    REQUIRE(send_grant != std::string::npos);

    const std::string send = function_definition_block(editor, "static s32 editSend(");
    const std::string delta = function_definition_block(editor, "static s32 editSendDelta(");
    const std::string control = function_definition_block(editor, "static s32 editSendControl(");
    const std::string apply = function_definition_block(editor, "static s32 editApplyHost(");
    CHECK(send.find("netSend(peer, wire, 1, NETCHAN_CONTROL)") != std::string::npos);
    CHECK(delta.find("editSend(peer, &wire)") != std::string::npos);
    CHECK(control.find("editSend(peer, &wire)") != std::string::npos);
    CHECK(apply.find("!defer_delta && !editSendDelta(peer, &delta)") != std::string::npos);

    const std::string receive = function_definition_block(editor, "s32 netGridEditReceive(");
    const auto require_snapshot = receive.find("resume_delta && s_EditResume.phase != EDIT_RESUME_WAITING_GRANT");
    const auto apply_delta = receive.find("gridEditRoomGuestApplyDelta(", require_snapshot);
    REQUIRE(require_snapshot < apply_delta);
    REQUIRE(apply_delta != std::string::npos);
}

TEST_CASE("A staged Grid extension registers only at the next explicit playtest start",
          "[net][lifecycle][static]")
{
    const std::string author = read_text_file("port/src/forge_level_io.c");
    const std::string editor = read_text_file("port/src/net/grid_edit_room_net.c");
    const std::string fork = function_block(author, "static s32 levelForkRevision");
    const std::string save = function_block(editor, "static s32 editSaveVariant");
    const std::string stage = function_block(editor, "static s32 editStageVariant");
    const std::string start = function_block(editor, "static s32 editStartPrivatePlaytest");
    const std::string bind = function_block(editor, "static s32 editEnableStagedVariant");
    const std::string preflight = function_block(editor, "static s32 editPreflightVariantFile");
    const size_t build_sources = fork.find("levelBuildRoomExtension(&source, room, extra_length");
    const size_t save_sources = fork.find("if (room) saved = levelSaveCheckedRoomFork(&request", build_sources);
    REQUIRE(build_sources < save_sources);
    REQUIRE(save_sources != std::string::npos);
    const size_t fork_extension = save.find("forgeLevelForkExtendedRevision");
    const size_t stage_extension = save.find("editStageVariant(&result, &state.invite, revision, room, extra_length, directory)");
    const size_t normal_pin = save.find("s_EditSavedVariant = pinned;");
    REQUIRE(fork_extension < stage_extension);
    REQUIRE(stage_extension < normal_pin);
    REQUIRE(normal_pin != std::string::npos);
    REQUIRE(save.find("s_EditStagedVariant =") == std::string::npos);
    REQUIRE(save.find("modAuthorRuntimeEnable") == std::string::npos);
    const size_t probe = stage.find("editPreflightVariantFile(candidate.path, candidate.arena_id, NULL, candidate.digest)");
    const size_t parent = stage.find("forgeRevisionReadParent(directory, &candidate, &parent");
    const size_t pin = stage.find("forgeRevisionPublishLocal(directory, candidate.arena_id, candidate.path, candidate.digest");
    const size_t publish = stage.find("s_EditStagedVariant = staged;");
    REQUIRE(probe < parent);
    REQUIRE(parent < pin);
    REQUIRE(pin < publish);
    REQUIRE(publish != std::string::npos);
    CHECK(stage.find("strcmp(parent.digest, source->arena_sha256)") < pin);
    CHECK(stage.find(".draft_revision = revision") < publish);
    CHECK(stage.find("modAuthorRuntimeEnable") == std::string::npos);

    const size_t world_idle = start.find("s_EditPlaytest.phase != EDIT_PLAYTEST_NONE");
    const size_t enable = start.find("editEnableStagedVariant(variant_id)");
    const size_t select = start.find("variant = &s_EditStagedVariant.pin;", enable);
    const size_t launch = start.find("netLobbyRequestStartWithSims", enable);
    REQUIRE(world_idle < enable);
    REQUIRE(enable < select);
    REQUIRE(select < launch);
    REQUIRE(launch != std::string::npos);

    const size_t finished_return = bind.find("!gridMatchRuntimeIsIdle() || mainGridRoomReturnPending()");
    const size_t gate = bind.find("if (!editPreflightVariantFile(s_EditStagedVariant.archive_path, arena_id, sha, NULL)) return 0;");
    const size_t register_source = bind.find("modAuthorRuntimeEnable(s_EditStagedVariant.mod_id");
    const size_t exact_catalog = bind.find("gridMatchCatalogArchiveDescriptor", register_source);
    const size_t recheck = bind.find("editStagedVariantMatchesPin(root, sha)", exact_catalog);
    REQUIRE(finished_return < gate);
    REQUIRE(gate < register_source);
    REQUIRE(register_source < exact_catalog);
    REQUIRE(exact_catalog < recheck);
    REQUIRE(recheck != std::string::npos);
    const size_t staged_bytes = preflight.find("gridMatchPackageLoad(path, &size)");
    const size_t exact_source = preflight.find("gridMatchRuntimePreflightRoomExtension(path, bytes, size, arena_id, expected_sha");
    REQUIRE(staged_bytes < exact_source);
    REQUIRE(exact_source < preflight.find("free(bytes);"));
    REQUIRE(preflight.find("free(bytes);") != std::string::npos);
}

TEST_CASE("An installed BYOR shared draft saves an exact local Variant without rebuilding the catalog",
          "[net][lifecycle][grid-edit][static]")
{
    const auto editor = read_text_file("port/src/net/grid_edit_room_net.c");
    const auto origin = function_definition_block(editor, "static grid_archive_origin_e editPinOrigin(");
    const auto same_sha = origin.find("strcmp(sha, pin->digest) == 0");
    const auto provenance = origin.find("gridArchiveOrigin(pin->path, bytes, size)");
    REQUIRE(same_sha < provenance);
    REQUIRE(provenance != std::string::npos);

    const auto shared = function_definition_block(editor, "static s32 editSharedVariantContext(");
    CHECK(shared.find("s_EditSourceOrigin != origin") != std::string::npos);
    CHECK(shared.find("editHostRoomStillOwned()") != std::string::npos);
    CHECK(shared.find("!gridMatchRuntimeIsIdle()") != std::string::npos);
    CHECK(shared.find("g_NetStageEpoch || mainStageChangePending()") != std::string::npos);
    CHECK(shared.find("!room->lifetime") != std::string::npos);
    CHECK(shared.find("g_NetLocalClient->state != CLSTATE_LOBBY") != std::string::npos);
    const auto byor = function_definition_block(editor, "static s32 editLocalByorVariantContext(");
    CHECK(byor.find("editSharedVariantContext(GRID_ARCHIVE_BYOR, draft, key)") != std::string::npos);
    CHECK(byor.find("!arena->temporary && arena->grid_offer_only") != std::string::npos);
    const auto original = function_definition_block(editor, "s32 netGridEditCanReviewVariantSources(");
    CHECK(original.find("editSharedVariantContext(GRID_ARCHIVE_ORIGINAL, &draft, &key)") != std::string::npos);

    const auto save = function_definition_block(editor, "s32 netGridEditSaveLocalByorVariant(");
    const auto context = save.find("editLocalByorVariantContext(&draft, &key)");
    const auto clone = save.find("gridEditRoomHostCloneDocument(s_EditRoom, &revision)");
    const auto revision = save.find("revision != key.revision");
    const auto write = save.find("forgeLevelForkLocalByorRevision(draft.invite.arena_id, key.parent_sha256");
    const auto publish = save.find("if (published)");
    const auto activate = save.find("editPinAndEnableSavedVariant(saved, revision)");
    REQUIRE(context < clone);
    REQUIRE(clone < revision);
    REQUIRE(revision < write);
    REQUIRE(write < publish);
    REQUIRE(publish < activate);
    REQUIRE(activate < save.find("return published;"));
    const auto pin = function_definition_block(editor, "static void editPinAndEnableSavedVariant(");
    const auto pin_file = pin.find("forgeRevisionPublishLocal(");
    const auto add_library = pin.find("modmgrEnableAuthoredGridVariant(");
    REQUIRE(pin_file < add_library);
    REQUIRE(add_library < pin.find("s_EditSavedVariant = pin;"));
    CHECK(pin.find("modAuthorRuntimeEnable(") == std::string::npos);

    const auto io = read_text_file("port/src/forge_level_io.c");
    const auto native = function_definition_block(io, "s32 forgeLevelForkLocalByorRevision(");
    CHECK(native.find("levelForkEditedRevision(arena_id, parent_sha, document, name, 0, 0, 1,") != std::string::npos);
    const auto exact = function_definition_block(io, "static s32 levelForkEditedRevision(");
    REQUIRE(exact.find("forgeRevisionRead(directory, arena_id, parent_sha,")
        < exact.find("levelForkRevisionWithChecks(&revision,"));
    const auto fork = function_definition_block(io, "static s32 levelForkRevisionWithChecks(");
    CHECK(fork.find("strcmp(provenance, GRID_ARENA_PROVENANCE_BYOR) != 0") != std::string::npos);
    CHECK(fork.find("{ GRID_ARENA_PROVENANCE_KEY, provenance }") != std::string::npos);
    CHECK(fork.find(".require_new_folder = 1") != std::string::npos);
    CHECK(fork.find("else if (local_byor) saved = modAuthorSave(") != std::string::npos);

    const auto room = read_text_file("port/fast3d/pdgui_menu_room.cpp");
    const auto panel = function_definition_block(room, "static void roomRenderSharedVariantSave(");
    const auto local_button = panel.find("if (save && localByor)");
    const auto local_save = panel.find("netGridEditSaveLocalByorVariant(", local_button);
    const auto warning = panel.find("snprintf(panel.localSaveWarning,", local_save);
    const auto picker_branch = panel.find("else if (save && available)", warning);
    const auto picker = panel.find("netGridEditReviewVariantSources(", picker_branch);
    REQUIRE(local_button < local_save);
    REQUIRE(local_save < warning);
    REQUIRE(warning < picker_branch);
    REQUIRE(picker_branch < picker);
    REQUIRE(picker != std::string::npos);
    CHECK(panel.find("ImGui::TextWrapped(\"%s\", panel.localSaveWarning)") != std::string::npos);
}

TEST_CASE("Staged Variant reviews stay on the admitted creator's exact private round",
          "[net][lifecycle][grid-edit][static]")
{
    const auto editor = read_text_file("port/src/net/grid_edit_room_net.c");
    const auto ready = function_definition_block(editor, "static s32 editReviewDraft(");
    const auto scope = function_definition_block(editor, "static s32 editReviewHostScope(");
    const auto active = function_definition_block(editor, "static s32 editReviewMatchesCurrent(");
    const auto offer = function_definition_block(editor, "static s32 editOfferStagedReview(");
    const auto reply = function_definition_block(editor, "s32 netGridEditReplyToStagedReview(");
    const auto dispatch = function_definition_block(editor, "static void editReceiveControl(");
    const auto receive = function_definition_block(editor, "static void editReceiveVariantReview(");
    const auto retire = function_definition_block(editor, "void netGridEditPrivatePlaytestStageStopped(");
    const auto detach = function_definition_block(editor, "static s32 editKeepDraftAfterPeerDeparture(");
    CHECK(scope.find("s_EditReviewSupported && editReviewDraft(draft)") != std::string::npos);
    CHECK(ready.find("pending_command") == std::string::npos);
    CHECK(scope.find("draft->revision == s_EditStagedVariant.draft_revision") != std::string::npos);
    CHECK(scope.find("editActivePlaytestWorld(draft->invite.identity.room_id)") != std::string::npos);
    CHECK(scope.find("&players, &creators) && creators") != std::string::npos);
    CHECK(active.find("s_EditReview.connection != s_EditConnection") != std::string::npos);
    CHECK(active.find("draft.peer_actor == s_EditReview.actor") != std::string::npos);
    CHECK(active.find("offer->review.stage_epoch == g_NetStageEpoch") != std::string::npos);
    CHECK(offer.find("editSendControl(editBoundPeer(), SVC_GRID_EDIT_CONTROL, &offer)") != std::string::npos);
    CHECK(offer.find("netSendToMatch") == std::string::npos);
    const auto local = function_definition_block(editor, "static grid_edit_variant_review_reply_e editReviewLocalResult(");
    CHECK(local.find("editFindReviewedLocalArena(&offer->review, root)") != std::string::npos);
    CHECK(local.find("? GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED : GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED") != std::string::npos);
    CHECK(reply.find("editReviewLocalResult(&offer, reply.review_deps_sha256") != std::string::npos);
    const auto peer = dispatch.find("client != editBoundPeer()");
    const auto forward = dispatch.find("editReceiveVariantReview(is_client, control)");
    REQUIRE(forward != std::string::npos);
    CHECK(peer < forward);
    CHECK(receive.find("!editReviewExpired()") != std::string::npos);
    CHECK(receive.find("gridEditVariantReviewMatches(&s_EditReview.offer.review, review)") != std::string::npos);
    CHECK(receive.find("review->offer_serial <= s_EditLastReviewSeen") != std::string::npos);
    CHECK(receive.find("strcmp(parent, draft.invite.arena_sha256)") != std::string::npos);
    CHECK(retire.find("editCancelReview(") != std::string::npos);
    CHECK(detach.find("s_EditReviewSupported = 0;") != std::string::npos);

    // Extended review is opt-in and preserves the source-only fallback.
    CHECK(detach.find("s_EditReviewCapabilities = 0;") != std::string::npos);
    const auto reset = function_definition_block(editor, "static void netGridEditReset(");
    CHECK(reset.find("s_EditReviewCapabilities = 0;") != std::string::npos);
    CHECK(active.find("s_EditReview.catalog_generation != assetCatalogGetGeneration()") != std::string::npos);
    CHECK(offer.find("s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_DEPS_V1") != std::string::npos);
    CHECK(offer.find(".kind = GRID_EDIT_ROOM_CONTROL_REVIEW_OFFER") != std::string::npos);
    const auto host_digest = offer.find("forgeLevelPrepareStagedArenaSources(s_EditStagedVariant.pin.path");
    const auto upgrade = offer.find("? GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER");
    const auto offer_send = offer.find("editSendControl(editBoundPeer(), SVC_GRID_EDIT_CONTROL, &offer)");
    REQUIRE(offer_send != std::string::npos);
    CHECK(host_digest < upgrade);
    CHECK(upgrade < offer_send);
    CHECK(offer.find("connection != s_EditConnection || peer_actor != draft.peer_actor", upgrade) < offer_send);

    const auto guest_digest = local.find("forgeLevelPrepareStagedArenaSources(root,");
    const auto guest_match = local.find("memcmp(computed, offer->review_deps_sha256");
    const auto guest_copy = local.find("memcpy(proof, computed");
    REQUIRE(guest_copy != std::string::npos);
    CHECK(guest_digest < guest_match);
    CHECK(guest_match < guest_copy);
    CHECK(reply.find("!editReviewMatchesCurrent()", reply.find("editReviewLocalResult("))
        < reply.find("editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL"));
    CHECK(receive.find("control->kind == expected_reply && !s_EditReview.reply") != std::string::npos);
    const auto host_match = receive.find("memcmp(control->review_deps_sha256, s_EditReview.offer.review_deps_sha256");
    const auto commit = receive.find("s_EditReview.reply = control->review_reply;");
    REQUIRE(commit != std::string::npos);
    CHECK(host_match < commit);
    CHECK(receive.find("modAuthorRuntimeEnable") == std::string::npos);
    CHECK(receive.find("netLobbyRequestStart") == std::string::npos);

    // Only the negotiated prepared review can keep a cache on either peer.
    const auto prepared = function_definition_block(editor, "static s32 editReviewSourcesReady(");
    const auto prepared_key = function_definition_block(editor, "static grid_match_offer_t editReviewSourcesKey(");
    CHECK(prepared_key.find(".room_id = offer->invite.identity.room_id") != std::string::npos);
    CHECK(prepared_key.find(".generation = offer->review.offer_serial") != std::string::npos);
    CHECK(prepared_key.find("sha256ToHex(offer->review_deps_sha256, key.manifest_digest)") != std::string::npos);
    CHECK(prepared.find("gridMatchCacheReady(sources, &key)") != std::string::npos);
    const auto host_retains = offer.find("s_EditReview.sources = sources;");
    const auto guest_retains = reply.find("s_EditReview.sources = sources;");
    REQUIRE(host_retains != std::string::npos);
    REQUIRE(guest_retains != std::string::npos);
    CHECK(offer_send < host_retains);
    CHECK(reply.find("editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL") < guest_retains);
    CHECK(receive.find("prepared && !editReviewSourcesReady(&s_EditReview.offer, s_EditReview.sources)") < commit);
    CHECK(receive.find("editReleaseReviewSources(control->review_reply") < commit);
    CHECK(receive.find("editClearReview(\"peer-cancel\")") < commit);
    const auto view = function_definition_block(editor, "s32 netGridEditGetStagedReview(");
    CHECK(view.find("editPreparedReview(s_EditReview.offer.kind) && editReviewExpired()") != std::string::npos);
    CHECK(view.find("out->sources_prepared = s_EditReview.reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED") != std::string::npos);
    const auto cancel = function_definition_block(editor, "static void editCancelReview(");
    CHECK(cancel.find("g_NetMode == NETMODE_CLIENT && editPreparedReview(kind)") != std::string::npos);
    CHECK(cancel.find("editClearReview(reason)") != std::string::npos);
    const auto clear = function_definition_block(editor, "static void editClearReview(");
    const auto release = function_definition_block(editor, "static void editReleaseReviewSources(");
    CHECK(clear.find("editReleaseReviewSources(reason)") < clear.find("memset(&s_EditReview"));
    CHECK(release.find("gridMatchCacheReset(&s_EditReview.sources)") != std::string::npos);
    CHECK(reset.find("editClearReview(") != std::string::npos);
    CHECK(detach.find("editClearReview(") != std::string::npos);

    const auto forge = read_text_file("port/src/forge_level_io.c");
    const auto stage = function_definition_block(forge, "static s32 gridPrepareStagedArenaSources(");
    const auto staged_bytes = stage.find("gridStageMatchSources(&plan, &offer, &cache");
    const auto handoff = stage.find("*out_cache = cache;");
    REQUIRE(handoff != std::string::npos);
    CHECK(staged_bytes < handoff);
    CHECK(stage.find("assetCatalogGetGeneration() != generation", staged_bytes) < handoff);
    CHECK(stage.find("gridMatchRuntimePrepare") == std::string::npos);
    CHECK(stage.find("fsPublishPreparedStageArchives") == std::string::npos);

    const auto received = function_definition_block(forge, "s32 forgeLevelPrepareReceivedStagedArenaSources(");
    const auto envelope = received.find("gridMatchCacheCheckArchiveBudget(bytes, size, err, err_cap)");
    const auto received_prepare = received.find("gridPrepareStagedArenaSources(label, bytes, size");
    REQUIRE(received_prepare != std::string::npos);
    CHECK(received.find("strcmp(sha, expected_sha)") < envelope);
    CHECK(envelope < received_prepare);
    CHECK(received.find("assetCatalogResolveAny(arena_id)") < envelope);
    CHECK(received.find("gridMatchPackageLoad") == std::string::npos);
    const auto native_probe = stage.find("gridMatchRuntimePreflightRoomExtension(");
    REQUIRE(native_probe != std::string::npos);
    CHECK(stage.find("levelDescriptorValueMatches(ini, FORGE_REVISION_PARENT_MAP_KEY, parent_map)") < native_probe);
    CHECK(stage.find("levelDescriptorValueMatches(ini, FORGE_REVISION_PARENT_DIGEST_KEY, parent_sha)") < native_probe);
    CHECK(stage.find("if (parent_id) package->path[0]") < staged_bytes);
}

TEST_CASE("Staged arena transfer requires both review consents and an exact prepared receipt",
          "[net][lifecycle][grid-edit][static]")
{
    const auto editor = read_text_file("port/src/net/grid_edit_room_net.c");
    const auto select = function_definition_block(editor, "static s32 editSelectReviewTransferArena(");
    CHECK(select.find("ASSET_ARENA") != std::string::npos);
    CHECK(select.find("GRID_ARCHIVE_ORIGINAL") != std::string::npos);
    CHECK(select.find("offer->review.arena_sha256") != std::string::npos);
    CHECK(select.find("entry->bundled") != std::string::npos);
    const auto offer = function_definition_block(editor, "static s32 editOfferStagedReview(");
    CHECK(offer.find("if (allow_transfer)") < offer.find("offer.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_OFFER"));
    const auto accept = function_definition_block(editor, "s32 netGridEditAcceptStagedReviewTransfer(");
    const auto request = accept.find("editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &request)");
    REQUIRE(request != std::string::npos);
    CHECK(accept.find("gridMatchTransferBegin(&transfer") < request);
    CHECK(request < accept.find("s_EditReview.transfer = transfer;"));
    const auto admit = function_definition_block(editor, "static void editReceiveReviewTransferRequest(");
    const auto consent = admit.find("s_EditReview.transfer_active = 1;");
    REQUIRE(consent != std::string::npos);
    CHECK(admit.find("request->review_arena_size != offer->review_arena_size") < consent);
    CHECK(admit.find("gridEditVariantReviewMatches(&offer->review, &request->review)") < consent);
    CHECK(admit.find("memcmp(request->review_deps_sha256, offer->review_deps_sha256") < consent);
    const auto pump = function_definition_block(editor, "static void editPumpReviewTransfer(");
    const auto sent = pump.find("netSend(peer, &wire, 1, NETCHAN_TRANSFER)");
    REQUIRE(sent != std::string::npos);
    CHECK(pump.find("!s_EditReview.transfer_active") < sent);
    CHECK(pump.find("!editReviewMatchesCurrent()") < sent);
    CHECK(pump.find("GRID_EDIT_REVIEW_TRANSFER_MAX_QUEUED_COMMANDS") < sent);
    CHECK(pump.find("netSendToMatch") == std::string::npos);
    const auto chunk = function_definition_block(editor, "static void editReceiveReviewArenaChunk(");
    const auto completed = chunk.find("gridMatchTransferComplete(");
    const auto prepare = chunk.find("forgeLevelPrepareReceivedStagedArenaSources(bytes, size,");
    const auto ready = chunk.find("strcmp(measured.manifest_digest, key.manifest_digest)");
    const auto proof = chunk.find("editSendControl(client, CLC_GRID_EDIT_CONTROL, &reply)");
    REQUIRE(proof != std::string::npos);
    CHECK(chunk.find("client != editBoundPeer()") < completed);
    CHECK(chunk.find("draft_generation != offer.invite.identity.generation") < completed);
    CHECK(completed < prepare);
    CHECK(prepare < ready);
    CHECK(ready < proof);
    CHECK(proof < chunk.find("s_EditReview.sources = sources;"));
    CHECK(chunk.find("gridMatchRuntimePrepare") == std::string::npos);
    CHECK(chunk.find("modAuthorRuntimeEnable") == std::string::npos);
    const auto cleanup = function_definition_block(editor, "static void editReleaseReviewTransfer(");
    CHECK(cleanup.find("gridMatchTransferReset(&s_EditReview.transfer)") != std::string::npos);
}

TEST_CASE("Private structural review combines arena sources with the sealed running match selections",
          "[net][lifecycle][grid-edit][static]")
{
    const auto messages = read_text_file("port/src/net/netmsg.c");
    const auto start = function_definition_block(messages, "static s32 netLobbyStartPrepareGrid(");
    const auto gameplay = start.find("forgeLevelBuildPrivateGameplaySources(");
    const auto stage = start.find("manifestAppendCatalogClosure(&plan->manifest, plan->stage_id,");
    const auto complete = start.find("forgeLevelBuildPinnedMatchSources(");
    const auto retain = start.find("gridMatchRuntimeOwnPrivateGameplaySources(&offer, &gameplay");
    REQUIRE(retain != std::string::npos);
    CHECK(start.find("if (plan->private_playtest)") < gameplay);
    CHECK(gameplay < stage);
    CHECK(stage < complete);
    CHECK(complete < retain);
    CHECK(retain < start.find("plan->grid_prepared = 1;"));
    const auto fingerprint = function_definition_block(messages, "s32 netmsgGridPrivateGameplayFingerprint(");
    CHECK(fingerprint.find("netmsgServerMatchClientMask(room_id) != (1u << g_NetLocalClient->id)") != std::string::npos);
    CHECK(fingerprint.find("netStageStartWritePrepare(&plan, false, false)") != std::string::npos);
    CHECK(fingerprint.find("plan.roster_count != 1") != std::string::npos);
    CHECK(fingerprint.find("plan.net_tick = 0") != std::string::npos);
    CHECK(fingerprint.find("audioPickNextPlaylistTrack") == std::string::npos);

    const auto runtime = read_text_file("port/src/grid_match_runtime.c");
    const auto own = function_definition_block(runtime, "s32 gridMatchRuntimeOwnPrivateGameplaySources(");
    const auto moved = own.find("match->private_gameplay_sources = *sources;");
    REQUIRE(moved != std::string::npos);
    CHECK(own.find("gridMatchSourcesExactSubset(") < moved);
    const auto compose = function_definition_block(runtime, "s32 gridMatchRuntimeComposePrivateCandidate(");
    CHECK(compose.find("gridMatchCacheCopySources(arena_cache, &candidate.arena_offer") != std::string::npos);
    const auto combine = compose.find("gridMatchSourcesCombine(");
    const auto cache = compose.find("gridMatchCacheCompose(");
    const auto outgoing = compose.find("gridMatchCacheRetainReady(");
    REQUIRE(combine != std::string::npos);
    REQUIRE(cache != std::string::npos);
    REQUIRE(outgoing != std::string::npos);
    CHECK(combine < cache);
    CHECK(combine < outgoing);
    const auto check_outgoing = compose.find("runtimeValidateStageBindings(match, err, err_cap)");
    const auto outgoing_table = compose.find("fsPrepareStageArchives(&candidate.outgoing_archives");
    const auto incoming_table = compose.find("runtimePreparePrivateIncoming(");
    const auto transfer = compose.find("*out = candidate;");
    REQUIRE(check_outgoing < outgoing_table);
    REQUIRE(outgoing < outgoing_table);
    REQUIRE(outgoing_table < transfer);
    REQUIRE(cache < incoming_table);
    REQUIRE(incoming_table < transfer);
    REQUIRE(transfer != std::string::npos);
    const auto incoming = function_definition_block(runtime, "static s32 runtimePreparePrivateIncoming(");
    const auto read_pinned = incoming.find("gridMatchPackageLoad(revision_path,");
    const auto preflight = incoming.find("assetCatalogPreflightGridArenaLease(");
    const auto mount = incoming.find("fsPrepareStageArchives(&candidate->incoming_archives");
    REQUIRE(read_pinned < preflight);
    REQUIRE(preflight < mount);
    REQUIRE(mount != std::string::npos);
    CHECK(incoming.find("assetCatalogBindGridScenarioDescriptor(") != std::string::npos);
    CHECK(incoming.find("GRID_ARENA_LEASE_PINNED_FILE") != std::string::npos);
    CHECK(incoming.find("runtimeBindAsset(") != std::string::npos);
    CHECK(incoming.find("assetCatalogAdmitGridArenaLease(") == std::string::npos);
    CHECK(incoming.find("fsPublishPreparedStageArchives(") == std::string::npos);
    CHECK(compose.find("fsPublishPreparedStageArchives") == std::string::npos);
    CHECK(compose.find("s_PendingMatch =") == std::string::npos);
    const auto current_runtime = function_definition_block(runtime, "s32 gridMatchRuntimePrivateCandidateCurrent(");
    CHECK(current_runtime.find("runtimeSameOffer(&candidate->arena_offer, arena_offer)") != std::string::npos);
    CHECK(current_runtime.find("runtimePrivateCandidateActive(match, &candidate->outgoing.offer)") != std::string::npos);
    CHECK(current_runtime.find("assetCatalogPreviewGridArenaLeaseStage(candidate->scenario_id)") != std::string::npos);
    CHECK(current_runtime.find("assetCatalogResolveAny(candidate->arena_id)") != std::string::npos);
    CHECK(current_runtime.find("assetCatalogResolveAny(candidate->scenario_id)") != std::string::npos);
    CHECK(current_runtime.find("candidate->outgoing_cache == match->cache") != std::string::npos);
    CHECK(current_runtime.find("gridMatchCacheReady(candidate->cache, &candidate->offer)") != std::string::npos);
    const auto release = function_definition_block(runtime, "void gridMatchRuntimeReleasePrivateCandidate(");
    const auto release_incoming = release.find("fsReleasePreparedStageArchives(&candidate->incoming_archives)");
    const auto release_outgoing = release.find("fsReleasePreparedStageArchives(&candidate->outgoing_archives)");
    const auto release_candidate_cache = release.find("gridMatchCacheReset(&candidate->cache)");
    const auto release_outgoing_cache = release.find("gridMatchCacheReset(&candidate->outgoing_cache)");
    REQUIRE(release_incoming < release_candidate_cache);
    REQUIRE(release_outgoing < release_outgoing_cache);
    REQUIRE(release_candidate_cache != std::string::npos);
    REQUIRE(release_outgoing_cache != std::string::npos);
    const auto retire = function_definition_block(runtime, "static void runtimeFree(");
    CHECK(retire.find("gridMatchSourcesFree(&match->private_gameplay_sources)") != std::string::npos);

    const auto room = read_text_file("port/src/net/grid_edit_room_net.c");
    const auto verified = function_definition_block(room, "static void editPreparePrivateCandidateSources(");
    const auto union_plan = verified.find("gridMatchRuntimeComposePrivateCandidate(&active->offer");
    REQUIRE(union_plan != std::string::npos);
    CHECK(verified.find("editReviewMatchesCurrent()") < union_plan);
    CHECK(verified.find("editPlaytestArenaMatches(active)") < union_plan);
    CHECK(verified.find("editCandidateGameplayCurrent(arena.room_id)") < union_plan);
    REQUIRE(verified.find("s_EditStagedVariant.pin.path, review->review.arena_id, review->review.arena_sha256",
        union_plan) != std::string::npos);
    const auto same_gameplay = function_definition_block(room, "static s32 editCandidateGameplayCurrent(");
    CHECK(same_gameplay.find("memcmp(gameplay, s_EditPlaytest.gameplay_fingerprint") != std::string::npos);
    const auto current = function_definition_block(room, "static s32 editCandidateSourcesCurrent(");
    CHECK(current.find("offer->review.stage_epoch != g_NetStageEpoch") != std::string::npos);
    CHECK(current.find("editActivePlaytestWorld(arena.room_id)") != std::string::npos);
    CHECK(current.find("gridMatchRuntimePrivateCandidateCurrent(&s_EditReview.candidate, &arena)") != std::string::npos);
    const auto refresh = function_definition_block(room, "static void editCheckPreparedCandidate(");
    const auto invalidate = refresh.find("gridMatchRuntimeReleasePrivateCandidate(&s_EditReview.candidate)");
    REQUIRE(invalidate != std::string::npos);
    CHECK(refresh.find("editCandidateSourcesCurrent()") < invalidate);
    CHECK(refresh.find("editCandidateGameplayCurrent(") < invalidate);
    const auto receive = function_definition_block(room, "static void editReceiveVariantReview(");
    CHECK(receive.find("memcmp(control->review_deps_sha256, s_EditReview.offer.review_deps_sha256")
        < receive.find("editPreparePrivateCandidateSources();"));
    const auto clear = function_definition_block(room, "static void editReleaseReviewSources(");
    CHECK(clear.find("gridMatchRuntimeReleasePrivateCandidate(&s_EditReview.candidate)") != std::string::npos);
    const auto view = function_definition_block(room, "s32 netGridEditGetStagedReview(");
    CHECK(view.find("&& editCandidateSourcesCurrent()") != std::string::npos);
    CHECK(view.find("show_issue ? s_EditReview.candidate_issue") != std::string::npos);
    CHECK(refresh.find("assetCatalogPreviewGridArenaLeaseStage(s_EditReview.candidate.scenario_id)") < invalidate);
    CHECK(refresh.find("gridMatchCacheReset(&s_EditReview.sources)") == std::string::npos);
}

TEST_CASE("Private Grid playlist advances follow the admitted match audience",
          "[net][lifecycle][grid-edit][static]")
{
    const auto messages = read_text_file("port/src/net/netmsg.c");
    const auto advance = function_definition_block(messages, "void netMusicBroadcastAdvance(");
    const auto private_match = advance.find("netGridEditPrivatePlaytestRoom(g_NetMatchRoomId)");
    const auto match_send = advance.find("netSendToMatch(g_NetMatchRoomId, &g_NetMsgRel, true, NETCHAN_CONTROL)");
    const auto room_send = advance.find("netSendToRoom(room_id, &g_NetMsgRel, true, NETCHAN_CONTROL)");
    REQUIRE(room_send != std::string::npos);
    CHECK(private_match < match_send);
    CHECK(match_send < advance.find("return;", match_send));
    CHECK(advance.find("return;", match_send) < room_send);
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
	// The counts must match; B-1206 counts again after removing what the
	// snapshot does not name, before the incomplete check
	const size_t count_check = resync_read.find("if (local_count != npccount) {");
	REQUIRE(count_check != std::string::npos);
	REQUIRE(count_check < incomplete_reject);
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
		"netSendToMatch(g_NetMatchRoomId, &wire");
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
    const std::string net = read_text_file("port/src/net/net.c");
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

    const size_t exactGridSlot = read.find(
        "plan.clients[i].playernum != reconnect_player");
    const size_t retainGridSlot = read.find(
        "client->grid_wire_playernum = grid ? prepared->playernum : NET_NULL_CLIENT;",
        globalPublication);
    REQUIRE(exactGridSlot < globalPublication);
    REQUIRE(retainGridSlot < asyncLoad);
    REQUIRE(net.find("s_NetReconnectAttempt.grid_playernum = g_NetLocalClient->grid_wire_playernum;") !=
        std::string::npos);

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

TEST_CASE("net room create: complete wire and authority validation precede membership commit",
          "[net][room][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcRoomCreateRead");
    const size_t decode = read.find("netRoomCreatePayloadRead(src, &request)");
    const size_t validate = read.find("netmsgRoomSettingsValid(&initial_settings)", decode);
    const size_t create = read.find("roomCreateForClient(previous", validate);
    const size_t reject = read.find("if (result != ROOM_RESULT_OK)", create);
    const size_t assign = read.find("srccl->room_id = created->id", reject);
    REQUIRE(decode != std::string::npos);
    REQUIRE(validate != std::string::npos);
    REQUIRE(create != std::string::npos);
    REQUIRE(reject != std::string::npos);
    REQUIRE(assign != std::string::npos);
    REQUIRE(read.find("roomLeave(") == std::string::npos);
    REQUIRE(read.find("netmsgSendRoomResult(srccl, CLC_ROOM_CREATE, result)") != std::string::npos);
    // Production-linked room transaction tests exercise invalid access,
    // password, capacity and reservation rejection without old-room mutation.
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
    const size_t send = read.find("netmsgDeliverRoomState(ncl, &g_NetMsgRel)", encode_guard);

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

    const size_t clamp = read.find("if (!netmsgRoomPlaylistValid(pl)) return 1");
    const size_t loop = read.find("for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++)", clamp);
    const size_t skip_source = read.find("if (ncl == srccl) continue", loop);
    const size_t start = read.find("netbufStartWrite(&g_NetMsgRel)", skip_source);
    const size_t write = read.find("netmsgSvcRoomPlaylistWrite(&g_NetMsgRel", start);
    const size_t encode_guard = read.find("if (g_NetMsgRel.error)", write);
    const size_t send = read.find("netmsgDeliverRoomState(ncl, &g_NetMsgRel)", encode_guard);

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

    const size_t assign = send.find("netmsgSvcRoomAssignWrite(&wire, room_id)");
    const size_t lookup = send.find("roomGetById(room_id)", assign);
    const size_t settings = send.find("room->settings_revision", lookup);
    const size_t playlist = send.find("room->playlist_revision", settings);
    REQUIRE(assign != std::string::npos);
    REQUIRE(lookup != std::string::npos);
    REQUIRE(settings != std::string::npos);
    REQUIRE(playlist != std::string::npos);
    REQUIRE(send.find("g_MatchConfig") == std::string::npos);
    REQUIRE(send.find("netmsgBuildCurrentPlaylistString") == std::string::npos);
    REQUIRE(send.find("!dstcl->peer") == std::string::npos);
    const std::string deliver = function_block(netmsg, "static void netmsgDeliverRoomState");
    REQUIRE(deliver.find("dstcl == g_NetLocalClient && !dstcl->peer") != std::string::npos);
    REQUIRE(deliver.find("netmsgSvcRoomAssignRead(&read, dstcl)") != std::string::npos);
    REQUIRE(deliver.find("netmsgSvcRoomSettingsRead(&read, dstcl)") != std::string::npos);
    REQUIRE(deliver.find("netmsgSvcRoomPlaylistRead(&read, dstcl)") != std::string::npos);
    REQUIRE(deliver.find("netSend(dstcl, wire, true, NETCHAN_CONTROL)") != std::string::npos);

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
