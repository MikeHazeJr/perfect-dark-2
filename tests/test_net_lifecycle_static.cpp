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

        FAIL("function definition block not closed");
    }
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

TEST_CASE("net interoperability: friend remote handoffs use hole-punch-aware joins",
          "[net][interoperability][static][c3828]")
{
    const std::string group = read_text_file("port/src/net/group_session.c");
    const std::string spectator = read_text_file("port/src/spectator.c");

    REQUIRE(group.find("#include \"net/netholepunch.h\"") != std::string::npos);
    REQUIRE(group.find("netStartClientWithHolePunch(addr)") != std::string::npos);
    REQUIRE(group.find("netStartClient(addr)") == std::string::npos);

    REQUIRE(spectator.find("#include \"net/netholepunch.h\"") != std::string::npos);
    REQUIRE(spectator.find("netStartClientWithHolePunch(addr)") != std::string::npos);
    REQUIRE(spectator.find("netStartClient(addr)") == std::string::npos);
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
    const std::string leave = function_block(room, "void roomLeave");

    const size_t found_gate = leave.find("if (found < 0) return");
    const size_t decrement = leave.find("room->client_count--", found_gate);
    const size_t client_abort = leave.find("netReadyGateOnClientLeft(clientId)", decrement);
    const size_t empty_gate = leave.find("room->client_count == 0 && room->id != 0", client_abort);
    const size_t room_abort = leave.find("netReadyGateAbortForRoom(room->id, \"Room closed\")", empty_gate);
    const size_t destroy = leave.find("roomDestroy(room)", room_abort);

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

    const size_t active_clear = abort.find("s_ReadyGate.active           = 0");
    const size_t countdown_clear = abort.find("s_ReadyGate.countdown_active = 0", active_clear);
    const size_t expected_clear = abort.find("s_ReadyGate.expected_mask    = 0", countdown_clear);
    const size_t preparing_loop = abort.find("g_NetClients[i].state == CLSTATE_PREPARING", expected_clear);
    const size_t lobby_state = abort.find("g_NetClients[i].state = CLSTATE_LOBBY", preparing_loop);
    const size_t room_lobby = abort.find("roomTransition(room, ROOM_STATE_LOBBY)", lobby_state);
    const size_t cancel_write = abort.find("netmsgSvcMatchCancelledWrite(&g_NetMsgRel", room_lobby);
    const size_t cancel_send = abort.find("netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL)", cancel_write);

    REQUIRE(active_clear != std::string::npos);
    REQUIRE(countdown_clear != std::string::npos);
    REQUIRE(expected_clear != std::string::npos);
    REQUIRE(preparing_loop != std::string::npos);
    REQUIRE(lobby_state != std::string::npos);
    REQUIRE(room_lobby != std::string::npos);
    REQUIRE(cancel_write != std::string::npos);
    REQUIRE(cancel_send != std::string::npos);

    REQUIRE(active_clear < countdown_clear);
    REQUIRE(countdown_clear < expected_clear);
    REQUIRE(expected_clear < preparing_loop);
    REQUIRE(preparing_loop < lobby_state);
    REQUIRE(lobby_state < room_lobby);
    REQUIRE(room_lobby < cancel_write);
    REQUIRE(cancel_write < cancel_send);
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
    const size_t received_check = end.find("s_ClientStatus.received_count < s_ClientStatus.missing_count", error_state);
    const size_t decline_call = end.find("netDistribClientDeclineActiveManifest", received_check);
    const size_t manifest_recheck = end.find("manifestCheck(&g_ClientManifest)", decline_call);

    REQUIRE(completed_init != std::string::npos);
    REQUIRE(failure != std::string::npos);
    REQUIRE(failure_done != std::string::npos);
    REQUIRE(received != std::string::npos);
    REQUIRE(completed != std::string::npos);
    REQUIRE(done_label != std::string::npos);
    REQUIRE(completed_guard != std::string::npos);
    REQUIRE(error_state != std::string::npos);
    REQUIRE(received_check != std::string::npos);
    REQUIRE(decline_call != std::string::npos);
    REQUIRE(manifest_recheck != std::string::npos);

    REQUIRE(completed_init < failure);
    REQUIRE(failure < failure_done);
    REQUIRE(failure_done < received);
    REQUIRE(received < completed);
    REQUIRE(completed < done_label);
    REQUIRE(done_label < completed_guard);
    REQUIRE(completed_guard < error_state);
    REQUIRE(error_state < received_check);
    REQUIRE(received_check < decline_call);
    REQUIRE(decline_call < manifest_recheck);
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
    REQUIRE(system.find("sysArgCheck(\"--debug-weapon-diag\")") != std::string::npos);
    REQUIRE(system.find("sysArgCheck(\"--debug-force-first-person\")") != std::string::npos);
    REQUIRE(system.find("sysArgCheck(\"--debug-generated-mesh-render-audit\")") != std::string::npos);
    REQUIRE(log_printf.find("strncmp(logmsg, \"LOG.WPN.DIAG:\", 13)") != std::string::npos);
    REQUIRE(log_printf.find("!sysWeaponDiagLoggingEnabled()") != std::string::npos);
}

TEST_CASE("net lifecycle: reconnect and drop-in gates preserve slots before reset",
          "[net][lifecycle][reconnect][static][c3813]")
{
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string connect = function_block(net, "static void netServerEvConnect");
    const std::string disconnect = function_block(net, "static void netServerEvDisconnect");

    const size_t ingame_gate = connect.find("const bool ingame =");
    const size_t no_preserved = connect.find("ingame && g_NetNumPreserved == 0", ingame_gate);
    const size_t late_disconnect = connect.find("DISCONNECT_LATE", no_preserved);
    const size_t reset = connect.find("netClientReset(cl)", late_disconnect);
    const size_t auth_state = connect.find("cl->state = CLSTATE_AUTH", reset);
    const size_t absent_flag = connect.find("cl->flags = ingame ? CLFLAG_ABSENT : 0", auth_state);

    REQUIRE(ingame_gate != std::string::npos);
    REQUIRE(no_preserved != std::string::npos);
    REQUIRE(late_disconnect != std::string::npos);
    REQUIRE(reset != std::string::npos);
    REQUIRE(auth_state != std::string::npos);
    REQUIRE(absent_flag != std::string::npos);

    REQUIRE(ingame_gate < no_preserved);
    REQUIRE(no_preserved < late_disconnect);
    REQUIRE(late_disconnect < reset);
    REQUIRE(reset < auth_state);
    REQUIRE(auth_state < absent_flag);

    const size_t preserve_gate = disconnect.find("cl->state >= CLSTATE_GAME && cl->settings.name[0]");
    const size_t preserve = disconnect.find("netServerPreservePlayer(cl)", preserve_gate);
    const size_t kill = disconnect.find("playerDie(true)", preserve);
    const size_t room_gate = disconnect.find("if (cl->room_id != 0xFF)", kill);
    const size_t room_leave = disconnect.find("roomLeave(room, cl->id)", room_gate);
    const size_t client_reset = disconnect.find("netClientReset(cl)", room_leave);
    const size_t room_broadcast = disconnect.find("netBroadcastRoomList()", client_reset);

    REQUIRE(preserve_gate != std::string::npos);
    REQUIRE(preserve != std::string::npos);
    REQUIRE(kill != std::string::npos);
    REQUIRE(room_gate != std::string::npos);
    REQUIRE(room_leave != std::string::npos);
    REQUIRE(client_reset != std::string::npos);
    REQUIRE(room_broadcast != std::string::npos);

    REQUIRE(preserve_gate < preserve);
    REQUIRE(preserve < kill);
    REQUIRE(kill < room_gate);
    REQUIRE(room_gate < room_leave);
    REQUIRE(room_leave < client_reset);
    REQUIRE(client_reset < room_broadcast);
}

TEST_CASE("net lifecycle: reconnect restores score identity and schedules full state resync",
          "[net][lifecycle][reconnect][static][c3813]")
{
    const std::string net = read_text_file("port/src/net/net.c");
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");
    const std::string restore = function_block(net, "void netServerRestorePreserved");
    const std::string auth = function_block(netmsg, "u32 netmsgClcAuthRead");

    const size_t score_cfg = restore.find("struct mpchrconfig *mpchr");
    const size_t kills = restore.find("memcpy(mpchr->killcounts", score_cfg);
    const size_t deaths = restore.find("mpchr->numdeaths = pp->numdeaths", kills);
    const size_t points = restore.find("mpchr->numpoints = pp->numpoints", deaths);
    const size_t client_link = restore.find("cl->config->client = cl", points);
    const size_t game_state = restore.find("cl->state = CLSTATE_GAME", client_link);
    const size_t one_use = restore.find("pp->active = false", game_state);

    REQUIRE(score_cfg != std::string::npos);
    REQUIRE(kills != std::string::npos);
    REQUIRE(deaths != std::string::npos);
    REQUIRE(points != std::string::npos);
    REQUIRE(client_link != std::string::npos);
    REQUIRE(game_state != std::string::npos);
    REQUIRE(one_use != std::string::npos);

    REQUIRE(score_cfg < kills);
    REQUIRE(kills < deaths);
    REQUIRE(deaths < points);
    REQUIRE(points < client_link);
    REQUIRE(client_link < game_state);
    REQUIRE(game_state < one_use);

    const size_t ingame = auth.find("const bool ingame");
    const size_t zero_cookie = auth.find("bool cookieZero = true", ingame);
    const size_t cookie_lookup = auth.find("netServerFindPreservedByCookie(name, suppliedCookie)", zero_cookie);
    const size_t late_join_reject = auth.find("mid-game join without cookie", cookie_lookup);
    const size_t restore_call = auth.find("netServerRestorePreserved(srccl, pp)", late_join_reject);
    const size_t clear_absent = auth.find("srccl->flags &= ~CLFLAG_ABSENT", restore_call);
    const size_t stage_start = auth.find("netmsgSvcStageStartWrite(&srccl->out)", clear_absent);
    const size_t chr_resync = auth.find("NET_RESYNC_FLAG_CHRS", stage_start);
    const size_t prop_resync = auth.find("NET_RESYNC_FLAG_PROPS", chr_resync);
    const size_t score_resync = auth.find("NET_RESYNC_FLAG_SCORES", prop_resync);

    REQUIRE(ingame != std::string::npos);
    REQUIRE(zero_cookie != std::string::npos);
    REQUIRE(cookie_lookup != std::string::npos);
    REQUIRE(late_join_reject != std::string::npos);
    REQUIRE(restore_call != std::string::npos);
    REQUIRE(clear_absent != std::string::npos);
    REQUIRE(stage_start != std::string::npos);
    REQUIRE(chr_resync != std::string::npos);
    REQUIRE(prop_resync != std::string::npos);
    REQUIRE(score_resync != std::string::npos);

    REQUIRE(ingame < zero_cookie);
    REQUIRE(zero_cookie < cookie_lookup);
    REQUIRE(cookie_lookup < late_join_reject);
    REQUIRE(late_join_reject < restore_call);
    REQUIRE(restore_call < clear_absent);
    REQUIRE(clear_absent < stage_start);
    REQUIRE(stage_start < chr_resync);
    REQUIRE(chr_resync < prop_resync);
    REQUIRE(prop_resync < score_resync);
}
