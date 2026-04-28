/*
 * tests/test_net_lifecycle_static.cpp -- Static guards for network lifecycle
 * transitions whose live paths are too coupled for pd-tests to execute.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

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

    size_t depth = 0;
    for (size_t pos = brace; pos < text.size(); pos++) {
        if (text[pos] == '{') {
            depth++;
        } else if (text[pos] == '}') {
            depth--;
            if (depth == 0) {
                return text.substr(begin, pos - begin + 1);
            }
        }
    }

    FAIL("function block not closed");
    return {};
}

} /* anon */

TEST_CASE("net lifecycle: CLC_LOBBY_START authorizes before reading payload",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcLobbyStartRead");

    const size_t server_gate = read.find("if (g_NetMode != NETMODE_SERVER)");
    const size_t null_client_gate = read.find("if (!srccl)");
    const size_t lobby_refresh = read.find("lobbyUpdate();");
    const size_t leader_gate = read.find("if (!isLeader)");
    const size_t leader_reject = read.find("return src->error;", leader_gate);
    const size_t first_payload_read = read.find("netbufReadU8(src)");

    REQUIRE(server_gate != std::string::npos);
    REQUIRE(null_client_gate != std::string::npos);
    REQUIRE(lobby_refresh != std::string::npos);
    REQUIRE(leader_gate != std::string::npos);
    REQUIRE(leader_reject != std::string::npos);
    REQUIRE(first_payload_read != std::string::npos);

    REQUIRE(server_gate < first_payload_read);
    REQUIRE(null_client_gate < first_payload_read);
    REQUIRE(lobby_refresh < first_payload_read);
    REQUIRE(leader_gate < first_payload_read);
    REQUIRE(leader_reject < first_payload_read);
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

TEST_CASE("net manifest status: status and hash are validated before ready gate commits",
          "[net][lifecycle][manifest][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcManifestStatusRead");

    const size_t status_read = read.find("const u8  status");
    const size_t missing_read = read.find("const u8  num_missing", status_read);
    const size_t error_gate = read.find("if (src->error)", missing_read);
    const size_t status_gate = read.find("status > MANIFEST_STATUS_DECLINE", error_gate);
    const size_t status_return = read.find("return 1;", status_gate);
    const size_t hash_gate = read.find("manifest_hash != g_ServerManifest.manifest_hash", status_return);
    const size_t hash_return = read.find("return 1;", hash_gate);
    const size_t missing_loop = read.find("for (s32 mi = 0;", hash_return);
    const size_t ready_gate = read.find("if (s_ReadyGate.active && srccl)", missing_loop);
    const size_t ready_commit = read.find("s_ReadyGate.ready_mask |=", ready_gate);

    REQUIRE(status_read != std::string::npos);
    REQUIRE(missing_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(status_gate != std::string::npos);
    REQUIRE(status_return != std::string::npos);
    REQUIRE(hash_gate != std::string::npos);
    REQUIRE(hash_return != std::string::npos);
    REQUIRE(missing_loop != std::string::npos);
    REQUIRE(ready_gate != std::string::npos);
    REQUIRE(ready_commit != std::string::npos);

    REQUIRE(status_read < missing_read);
    REQUIRE(missing_read < error_gate);
    REQUIRE(error_gate < status_gate);
    REQUIRE(status_gate < status_return);
    REQUIRE(status_return < hash_gate);
    REQUIRE(hash_gate < hash_return);
    REQUIRE(hash_return < missing_loop);
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

TEST_CASE("net lifecycle: rejected Counter-Op start does not commit mode state",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcLobbyStartRead");

    const size_t local_initial = read.find("u8 counterOpClientId = NET_NULL_CLIENT");
    const size_t anti_branch = read.find("if (gamemode == NETGAMEMODE_ANTI)", local_initial);
    const size_t invalid_check = read.find("antiClientId == NET_NULL_CLIENT", anti_branch);
    const size_t invalid_return = read.find("return src->error;", invalid_check);
    const size_t state_check = read.find("antiCl->state < CLSTATE_LOBBY", invalid_return);
    const size_t state_return = read.find("return src->error;", state_check);
    const size_t room_check = read.find("antiCl->room_id != srccl->room_id", state_return);
    const size_t room_return = read.find("return src->error;", room_check);
    const size_t local_commit = read.find("counterOpClientId = antiClientId;", room_return);
    const size_t mode_commit = read.find("g_NetGameMode = gamemode;", local_commit);
    const size_t anti_commit = read.find("g_NetCounterOpClientId = counterOpClientId;", mode_commit);

    REQUIRE(local_initial != std::string::npos);
    REQUIRE(anti_branch != std::string::npos);
    REQUIRE(invalid_check != std::string::npos);
    REQUIRE(invalid_return != std::string::npos);
    REQUIRE(state_check != std::string::npos);
    REQUIRE(state_return != std::string::npos);
    REQUIRE(room_check != std::string::npos);
    REQUIRE(room_return != std::string::npos);
    REQUIRE(local_commit != std::string::npos);
    REQUIRE(mode_commit != std::string::npos);
    REQUIRE(anti_commit != std::string::npos);

    REQUIRE(local_initial < anti_branch);
    REQUIRE(anti_branch < invalid_check);
    REQUIRE(invalid_check < invalid_return);
    REQUIRE(invalid_return < state_check);
    REQUIRE(state_check < state_return);
    REQUIRE(state_return < room_check);
    REQUIRE(room_check < room_return);
    REQUIRE(room_return < local_commit);
    REQUIRE(local_commit < mode_commit);
    REQUIRE(mode_commit < anti_commit);

    REQUIRE(read.find("g_NetCounterOpClientId = antiClientId") == std::string::npos);
}

TEST_CASE("net lifecycle: SVC_STAGE_START validates mode before committing state",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcStageStartRead");

    const size_t mode_read = read.find("const u8 mode = netbufReadU8(src)");
    const size_t error_gate = read.find("if (src->error)", mode_read);
    const size_t invalid_mode = read.find("mode != NETGAMEMODE_MP", error_gate);
    const size_t invalid_return = read.find("return 1;", invalid_mode);
    const size_t mode_commit = read.find("g_NetGameMode = mode;", invalid_return);
    const size_t mission_write = read.find("g_MissionConfig.stagenum = stagenum", mode_commit);
    const size_t mp_write = read.find("g_MpSetup.stagenum = stagenum", mode_commit);

    REQUIRE(mode_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(invalid_mode != std::string::npos);
    REQUIRE(invalid_return != std::string::npos);
    REQUIRE(mode_commit != std::string::npos);
    REQUIRE(mission_write != std::string::npos);
    REQUIRE(mp_write != std::string::npos);

    REQUIRE(mode_read < error_gate);
    REQUIRE(error_gate < invalid_mode);
    REQUIRE(invalid_mode < invalid_return);
    REQUIRE(invalid_return < mode_commit);
    REQUIRE(mode_commit < mission_write);
    REQUIRE(mode_commit < mp_write);
}

TEST_CASE("net lifecycle: SVC_STAGE_START rejects missing source client before state access",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcStageStartRead");

    const size_t null_gate = read.find("if (!srccl)");
    const size_t null_return = read.find("return 1;", null_gate);
    const size_t state_gate = read.find("srccl->state != CLSTATE_LOBBY", null_return);
    const size_t first_payload_read = read.find("netbufReadU32(src)");

    REQUIRE(null_gate != std::string::npos);
    REQUIRE(null_return != std::string::npos);
    REQUIRE(state_gate != std::string::npos);
    REQUIRE(first_payload_read != std::string::npos);

    REQUIRE(null_gate < null_return);
    REQUIRE(null_return < state_gate);
    REQUIRE(state_gate < first_payload_read);
}

TEST_CASE("net lifecycle: SVC_STAGE_START stages tick and RNG until identity is valid",
          "[net][lifecycle][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgSvcStageStartRead");

    const size_t tick_read = read.find("const u32 netTick = netbufReadU32(src)");
    const size_t rng0_read = read.find("const u64 rngSeed0 = netbufReadU64(src)", tick_read);
    const size_t rng1_read = read.find("const u64 rngSeed1 = netbufReadU64(src)", rng0_read);
    const size_t seed_read = read.find("const u32 matchSeed = netbufReadU32(src)", rng1_read);
    const size_t stage_read = read.find("const u16 stage_session = catalogReadAssetRef(src)", seed_read);
    const size_t zero_stage = read.find("if (stage_session == 0)", stage_read);
    const size_t zero_return = read.find("return 1;", zero_stage);
    const size_t unknown_stage = read.find("NET: SVC_STAGE unknown stage session", zero_return);
    const size_t unknown_return = read.find("return 1;", unknown_stage);
    const size_t mode_read = read.find("const u8 mode = netbufReadU8(src)", unknown_return);
    const size_t invalid_mode = read.find("mode != NETGAMEMODE_MP", mode_read);
    const size_t invalid_return = read.find("return 1;", invalid_mode);
    const size_t tick_commit = read.find("g_NetTick = netTick;", invalid_return);
    const size_t rng0_commit = read.find("g_NetRngSeeds[0] = rngSeed0;", tick_commit);
    const size_t rng1_commit = read.find("g_NetRngSeeds[1] = rngSeed1;", rng0_commit);
    const size_t latch_commit = read.find("g_NetRngLatch = true;", rng1_commit);
    const size_t seed_commit = read.find("g_NetMatchSeed = matchSeed;", latch_commit);
    const size_t mode_commit = read.find("g_NetGameMode = mode;", seed_commit);

    REQUIRE(tick_read != std::string::npos);
    REQUIRE(rng0_read != std::string::npos);
    REQUIRE(rng1_read != std::string::npos);
    REQUIRE(seed_read != std::string::npos);
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
    REQUIRE(mode_commit != std::string::npos);

    REQUIRE(tick_read < rng0_read);
    REQUIRE(rng0_read < rng1_read);
    REQUIRE(rng1_read < seed_read);
    REQUIRE(seed_read < stage_read);
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
    REQUIRE(seed_commit < mode_commit);

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

TEST_CASE("net lifecycle: CLC_LOBBY_START drains over-cap bot records before manifest",
          "[net][lifecycle][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string read = function_block(netmsg, "u32 netmsgClcLobbyStartRead");

    const size_t clamp = read.find("u8 clampedSims");
    const size_t kept_loop = read.find("for (s32 bi = 0; bi < clampedSims; bi++)", clamp);
    const size_t drain_guard = read.find("if (clampedSims < numSims)", kept_loop);
    const size_t drain_loop = read.find("for (s32 bi = clampedSims; bi < (s32)numSims; bi++)", drain_guard);
    const size_t drain_name = read.find("netbufReadStr(src); /* bot name */", drain_loop);
    const size_t drain_body = read.find("netbufReadStr(src); /* body catalog ID */", drain_name);
    const size_t drain_head = read.find("netbufReadStr(src); /* head catalog ID */", drain_body);
    const size_t drain_diff = read.find("netbufReadU8(src);  /* difficulty */", drain_head);
    const size_t drain_type = read.find("netbufReadU8(src);  /* type */", drain_diff);
    const size_t drain_error = read.find("malformed over-cap bot configs", drain_type);
    const size_t manifest = read.find("manifestDeserialize(src, &g_ServerManifest)");

    REQUIRE(clamp != std::string::npos);
    REQUIRE(kept_loop != std::string::npos);
    REQUIRE(drain_guard != std::string::npos);
    REQUIRE(drain_loop != std::string::npos);
    REQUIRE(drain_name != std::string::npos);
    REQUIRE(drain_body != std::string::npos);
    REQUIRE(drain_head != std::string::npos);
    REQUIRE(drain_diff != std::string::npos);
    REQUIRE(drain_type != std::string::npos);
    REQUIRE(drain_error != std::string::npos);
    REQUIRE(manifest != std::string::npos);

    REQUIRE(clamp < kept_loop);
    REQUIRE(kept_loop < drain_guard);
    REQUIRE(drain_guard < drain_loop);
    REQUIRE(drain_loop < drain_name);
    REQUIRE(drain_name < drain_body);
    REQUIRE(drain_body < drain_head);
    REQUIRE(drain_head < drain_diff);
    REQUIRE(drain_diff < drain_type);
    REQUIRE(drain_type < drain_error);
    REQUIRE(drain_error < manifest);
}

TEST_CASE("net settings: client team changes are sanitized before match state writes",
          "[net][settings][security][static]")
{
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string sanitize = function_block(netmsg, "static u8 netmsgSanitizeClientTeam");
    const std::string read = function_block(netmsg, "u32 netmsgClcSettingsRead");

    REQUIRE(sanitize.find("wireTeam >= MAX_TEAMS") != std::string::npos);
    REQUIRE(sanitize.find("srccl->state >= CLSTATE_GAME") != std::string::npos);
    REQUIRE(sanitize.find("MPOPTION_TEAMSENABLED") != std::string::npos);
    REQUIRE(sanitize.find("g_MpSetup.options") != std::string::npos);

    const size_t team_read = read.find("const u8 team = netbufReadU8(src)");
    const size_t error_gate = read.find("if (src->error)");
    const size_t sanitized = read.find("const u8 sanitizedTeam = netmsgSanitizeClientTeam(srccl, team)");
    const size_t config_write = read.find("srccl->config->base.team = sanitizedTeam");
    const size_t settings_write = read.find("srccl->settings.team = sanitizedTeam");

    REQUIRE(team_read != std::string::npos);
    REQUIRE(error_gate != std::string::npos);
    REQUIRE(sanitized != std::string::npos);
    REQUIRE(config_write != std::string::npos);
    REQUIRE(settings_write != std::string::npos);

    REQUIRE(team_read < error_gate);
    REQUIRE(error_gate < sanitized);
    REQUIRE(sanitized < config_write);
    REQUIRE(config_write < settings_write);

    REQUIRE(read.find("srccl->config->base.team = team") == std::string::npos);
    REQUIRE(read.find("srccl->settings.team = team") == std::string::npos);
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
