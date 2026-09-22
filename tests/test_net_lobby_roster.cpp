#include "catch.hpp"
#include "net/lobby_view.h"
#include <cstring>
#include <cstdio>
extern "C" {
#include "net/lobby_roster_wire.h"
// Mirror the existing test_netbuf C boundary; types.h redefines C++ bool.
struct netbuf { u8 *data; u32 size, rp, wp, error; };
void netbufStartReadData(struct netbuf *, const void *, u32);
#include "room.h"
void lobbyFixtureReset(int server, int local_id);
void lobbyFixtureClient(int index, const char *name, int state);
void lobbyFixtureLocalRoom(unsigned char room_id);
void lobbyFixtureClientRoom(int index, unsigned char room_id);
int lobbyFixtureClientState(int index);
void lobbyFixturePendingReconnect(int index);
int lobbyFixtureLobbyState(void);
int lobbyFixturePreparingState(void);
int lobbyFixtureGameState(void);
}

static lobby_roster_snapshot_t roster()
{
    lobby_roster_snapshot_t s{};
    s.count = 3; s.leaderClientId = 0;
    const unsigned char ids[] = {0, 3, 9};
    for (int i = 0; i < s.count; ++i) {
        auto &p = s.players[i];
        p.active = 1; p.clientId = ids[i]; p.state = lobbyFixtureLobbyState();
        p.roomId = i ? 1 : 0; p.team = i;
        std::snprintf(p.name, sizeof(p.name), "Player %u", p.clientId);
        std::snprintf(p.body_id, sizeof(p.body_id), "base:body_%u", p.clientId);
        std::snprintf(p.head_id, sizeof(p.head_id), "base:head_%u", p.clientId);
    }
    return s;
}

TEST_CASE("Roster wire preserves sparse identities and public catalog IDs", "[net][lobby][roster-wire]")
{
    auto s = roster(); s.players[1].state = lobbyFixturePreparingState();
    s.players[2].state = lobbyFixtureGameState();
    unsigned char data[LOBBY_ROSTER_WIRE_MAX]{};
    netbuf out{}; out.data = data; out.size = sizeof(data);
    REQUIRE(lobbyRosterPayloadWrite(&out, &s));
    netbuf in{}; netbufStartReadData(&in, data, out.wp);
    lobby_roster_snapshot_t actual{};
    REQUIRE(lobbyRosterPayloadRead(&in, &actual));
    REQUIRE(in.rp == out.wp); REQUIRE(actual.count == 3);
    for (int i = 0; i < s.count; ++i) {
        REQUIRE(actual.players[i].clientId == s.players[i].clientId);
        REQUIRE(actual.players[i].roomId == s.players[i].roomId);
        REQUIRE(actual.players[i].team == s.players[i].team);
        REQUIRE(std::strcmp(actual.players[i].body_id, s.players[i].body_id) == 0);
        REQUIRE(std::strcmp(actual.players[i].head_id, s.players[i].head_id) == 0);
    }
    REQUIRE(actual.players[0].isLeader == 1);
    REQUIRE(actual.players[1].isReady == 0); REQUIRE(actual.players[2].isReady == 1);
}

TEST_CASE("Roster rejects every truncated payload without partial destination mutation", "[net][lobby][roster-wire]")
{
    const auto s = roster(); unsigned char data[LOBBY_ROSTER_WIRE_MAX]{};
    netbuf out{}; out.data = data; out.size = sizeof(data);
    REQUIRE(lobbyRosterPayloadWrite(&out, &s));
    lobby_roster_snapshot_t sentinel; std::memset(&sentinel, 0xa5, sizeof(sentinel));
    for (unsigned size = 0; size < out.wp; ++size) {
        INFO("truncated size " << size);
        netbuf in{}; netbufStartReadData(&in, data, size);
        auto result = sentinel;
        REQUIRE_FALSE(lobbyRosterPayloadRead(&in, &result));
        REQUIRE(std::memcmp(&result, &sentinel, sizeof(result)) == 0);
    }
}

TEST_CASE("Roster rejects duplicate identities invalid bounds and hidden string suffixes", "[net][lobby][roster-wire]")
{
    auto s = roster();
    SECTION("duplicate") { s.players[2].clientId = s.players[0].clientId; }
    SECTION("count") { s.count = LOBBY_MAX_PLAYERS + 1; }
    SECTION("client ID") { s.players[1].clientId = 32; }
    SECTION("unauthenticated") { s.players[1].state = 2; }
    SECTION("state") { s.players[1].state = 255; }
    SECTION("room") { s.players[1].roomId = HUB_MAX_ROOMS; }
    SECTION("team") { s.players[1].team = 254; }
    SECTION("missing leader") { s.leaderClientId = 8; }
    SECTION("name") { std::memset(s.players[1].name, 'x', sizeof(s.players[1].name)); }
    SECTION("empty name") { s.players[1].name[0] = 0; }
    SECTION("body") { std::memset(s.players[1].body_id, 'x', sizeof(s.players[1].body_id)); }
    SECTION("hidden suffix") {
        unsigned char data[LOBBY_ROSTER_WIRE_MAX]{};
        netbuf out{}; out.data = data; out.size = sizeof(data);
        REQUIRE(lobbyRosterPayloadWrite(&out, &s));
        data[8] = 0; // count/leader + four fields + two-byte string length.
        netbuf in{}; netbufStartReadData(&in, data, out.wp);
        auto result = s;
        REQUIRE_FALSE(lobbyRosterPayloadRead(&in, &result));
        REQUIRE(std::memcmp(&result, &s, sizeof(s)) == 0);
        return;
    }
    REQUIRE_FALSE(lobbyRosterValid(&s));
    unsigned char data[LOBBY_ROSTER_WIRE_MAX]{};
    netbuf out{}; out.data = data; out.size = sizeof(data);
    REQUIRE_FALSE(lobbyRosterPayloadWrite(&out, &s)); REQUIRE(out.wp == 0);
}

TEST_CASE("Roster supports all 32 players and refuses insufficient output capacity atomically", "[net][lobby][roster-wire]")
{
    auto s = roster(); s.count = LOBBY_MAX_PLAYERS; s.leaderClientId = 31;
    for (int i = 0; i < s.count; ++i) {
        s.players[i] = s.players[0]; s.players[i].clientId = i;
        std::memset(s.players[i].name, 'n', sizeof(s.players[i].name)-1);
        std::memset(s.players[i].body_id, 'b', sizeof(s.players[i].body_id)-1);
        std::memset(s.players[i].head_id, 'h', sizeof(s.players[i].head_id)-1);
    }
    unsigned char data[LOBBY_ROSTER_WIRE_MAX]{};
    netbuf out{}; out.data = data; out.size = sizeof(data);
    REQUIRE(lobbyRosterPayloadWrite(&out, &s));
    netbuf in{}; netbufStartReadData(&in, data, out.wp);
    lobby_roster_snapshot_t actual{};
    REQUIRE(lobbyRosterPayloadRead(&in, &actual)); REQUIRE(actual.count == 32);
    REQUIRE(actual.players[31].isLeader == 1);
    out.size = out.wp - 1; out.wp = 0;
    REQUIRE_FALSE(lobbyRosterPayloadWrite(&out, &s)); REQUIRE(out.wp == 0);
}

TEST_CASE("Remote roster updates presentation without fabricating gameplay clients", "[net][lobby][roster-apply]")
{
    lobbyFixtureReset(0, 3); lobbyFixtureClient(3, "Local", lobbyFixtureLobbyState());
    auto s = roster(); REQUIRE(lobbyAcceptRoster(&s));
    lobbyUpdate(); REQUIRE(lobbyPlayerCountForView(0) == 3);
    REQUIRE(lobbyFixtureClientState(0) == 0); REQUIRE(lobbyFixtureClientState(9) == 0);
    lobbyplayer_view view{};
    REQUIRE(lobbyProjectViewIndex(0, 0, &view)); REQUIRE(view.clientId == 0);
    REQUIRE(view.isLocal == 0); REQUIRE(view.isLeader == 1);
    REQUIRE(lobbyProjectViewIndex(1, 0, &view)); REQUIRE(view.isLocal == 1);
    const auto before = g_Lobby;
    s.players[1].clientId = 4; // Local client absent, never apply this snapshot.
    REQUIRE_FALSE(lobbyAcceptRoster(&s));
    REQUIRE(std::memcmp(&before, &g_Lobby, sizeof(before)) == 0);
    lobbyInit(); lobbyUpdate(); REQUIRE(lobbyPlayerCountForView(0) == 1);
}

TEST_CASE("Room presentation maps identity portraits and authority through the same filtered rows", "[net][lobby][roster-apply]")
{
    lobbyFixtureReset(0, 3); lobbyFixtureClient(3, "Local", lobbyFixtureLobbyState());
    lobbyFixtureLocalRoom(1);
    g_RoomCacheCount = 1; g_RoomCache[0].id = 1; g_RoomCache[0].creator_client_id = 9;
    auto s = roster(); REQUIRE(lobbyAcceptRoster(&s));
    REQUIRE(lobbyPlayerCountForView(0) == 3); REQUIRE(lobbyRoomGetPlayerCount() == 2);
    lobbyplayer_view view{};
    REQUIRE(lobbyRoomGetPlayerInfo(0, &view)); REQUIRE(view.clientId == 3);
    REQUIRE(view.isLocal == 1); REQUIRE(view.isLeader == 0);
    REQUIRE(std::strcmp(lobbyRoomGetPlayerBodyId(0), "base:body_3") == 0);
    REQUIRE(lobbyRoomGetPlayerInfo(1, &view)); REQUIRE(view.clientId == 9);
    REQUIRE(view.isLeader == 1);
    REQUIRE(std::strcmp(lobbyRoomGetPlayerHeadId(1), "base:head_9") == 0);
    REQUIRE_FALSE(lobbyRoomGetPlayerInfo(2, &view)); REQUIRE(view.active == 0);
    s.players[0] = s.players[1]; s.players[1] = s.players[2]; s.count = 2; s.leaderClientId = 9;
    REQUIRE(lobbyAcceptRoster(&s));
    REQUIRE(lobbyRoomGetPlayerInfo(1, &view)); REQUIRE(view.clientId == 9);
    lobbyFixtureLocalRoom(0xff); REQUIRE(lobbyRoomGetPlayerCount() == 0);
}

TEST_CASE("Authoritative roster excludes unauthenticated slots and tracks room moves", "[net][lobby][roster-apply]")
{
    lobbyFixtureReset(1, 0);
    lobbyFixtureClient(0, "Host", lobbyFixtureLobbyState());
    lobbyFixtureClient(3, "Pending authentication", 2);
    lobbyFixtureClient(9, "Peer", lobbyFixtureLobbyState());
    lobbyFixtureClient(13, "Provisional reconnect", lobbyFixtureLobbyState());
    lobbyFixturePendingReconnect(13);
    lobbyFixtureLocalRoom(1); lobbyFixtureClientRoom(9, 2);
    lobby_roster_snapshot_t s{}; lobbyCaptureRoster(&s);
    REQUIRE(s.count == 2); REQUIRE(s.players[1].clientId == 9);
    REQUIRE(lobbyRoomGetPlayerCount() == 1);
    lobbyFixtureClientRoom(9, 1); REQUIRE(lobbyRoomGetPlayerCount() == 2);
    REQUIRE_FALSE(lobbyAcceptRoster(&s)); // Server authority is not overwritten.
}

TEST_CASE("Roster accepts initial authentication before public appearance settings arrive", "[net][lobby][roster-auth-empty]")
{
    lobbyFixtureReset(0, 3); lobbyFixtureClient(3, "Local", lobbyFixtureLobbyState());
    auto s = roster(); s.count = 2;
    s.players[1].body_id[0] = 0; s.players[1].head_id[0] = 0;
    unsigned char data[LOBBY_ROSTER_WIRE_MAX]{};
    for (int phase = 0; phase < 2; ++phase) {
        INFO("authentication/settings phase " << phase);
        if (phase) {
            std::strcpy(s.players[1].body_id, "base:dark_combat");
            std::strcpy(s.players[1].head_id, "base:head_dark_combat");
        }
        netbuf out{}; out.data = data; out.size = sizeof(data);
        REQUIRE(lobbyRosterPayloadWrite(&out, &s));
        netbuf in{}; netbufStartReadData(&in, data, out.wp);
        lobby_roster_snapshot_t actual{};
        REQUIRE(lobbyRosterPayloadRead(&in, &actual));
        REQUIRE(lobbyAcceptRoster(&actual));
        REQUIRE(lobbyPlayerCountForView(0) == 2);
        const auto *local = lobbyPlayerForView(1, 0);
        REQUIRE(local); REQUIRE(local->clientId == 3);
        REQUIRE(std::strcmp(local->body_id, s.players[1].body_id) == 0);
        REQUIRE(std::strcmp(local->head_id, s.players[1].head_id) == 0);
    }
}

TEST_CASE("Roster empty metadata uses canonical string sizes and preserves short output buffers", "[net][lobby][roster-auth-empty]")
{
    // One player: two header bytes, four scalar bytes, name A, two empty IDs.
    const unsigned char canonical[] = {1,1,1,3,255,255,2,0,'A',0,1,0,0,1,0,0};
    const unsigned char emptyLengthForm[] = {1,1,1,3,255,255,2,0,'A',0,0,0,0,0};
    lobby_roster_snapshot_t s{}; s.count = 1; s.leaderClientId = 1;
    auto &p = s.players[0]; p.active = 1; p.clientId = 1;
    p.state = lobbyFixtureLobbyState(); p.roomId = 255; p.team = 255;
    std::strcpy(p.name, "A");
    unsigned char data[sizeof(canonical)]{};
    netbuf out{}; out.data = data; out.size = sizeof(data);
    REQUIRE(lobbyRosterPayloadWrite(&out, &s)); REQUIRE(out.wp == sizeof(canonical));
    REQUIRE(std::memcmp(data, canonical, sizeof(canonical)) == 0);
    for (int form = 0; form < 2; ++form) {
        netbuf in{};
        netbufStartReadData(&in, form ? emptyLengthForm : canonical,
            form ? sizeof(emptyLengthForm) : sizeof(canonical));
        lobby_roster_snapshot_t actual{};
        REQUIRE(lobbyRosterPayloadRead(&in, &actual)); REQUIRE(in.rp == in.size);
        REQUIRE(actual.players[0].body_id[0] == 0);
        REQUIRE(actual.players[0].head_id[0] == 0);
    }
    std::memset(data, 0xa5, sizeof(data));
    unsigned char before[sizeof(data)]; std::memcpy(before, data, sizeof(data));
    out.wp = 0; out.size = sizeof(data) - 1;
    REQUIRE_FALSE(lobbyRosterPayloadWrite(&out, &s)); REQUIRE(out.wp == 0);
    REQUIRE(std::memcmp(data, before, sizeof(data)) == 0);
}
