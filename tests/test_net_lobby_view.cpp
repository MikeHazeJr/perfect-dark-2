#include "catch.hpp"
#include "net/lobby_view.h"
#include <cstring>
extern "C" {
#include "net/netlobby.h"
#include "room.h"
void lobbyFixtureReset(int server, int local_id);
void lobbyFixtureClient(int index, const char *name, int state);
void lobbyFixtureLocalRoom(unsigned char room_id);
int lobbyFixtureLobbyState(void);
int lobbyFixturePreparingState(void);
int lobbyFixtureGameState(void);
}

TEST_CASE("Lobby C projection preserves every field in the shared C++ player view",
          "[net][lobby][view][T-NETWORKING-011]")
{
    lobbyplayer player{};
    player.active=1; player.isLeader=1; player.isReady=1; player.team=7; player.clientId=31;
    std::strcpy(player.name, "Joanna Dark");
    lobbyplayer_view view{};
    REQUIRE(lobbyProjectPlayerView(&player, 1, 5, &view));
    REQUIRE(view.active == 1); REQUIRE(view.isLeader == 1); REQUIRE(view.isReady == 1);
    REQUIRE(view.team == 7); REQUIRE(view.clientId == 31);
    REQUIRE(view.isLocal == 1); REQUIRE(view.state == 5);
    REQUIRE(std::strcmp(view.name, "Joanna Dark") == 0);
    std::memset(player.name, 'x', sizeof(player.name));
    REQUIRE(lobbyProjectPlayerView(&player, 0, 4, &view));
    REQUIRE(std::strlen(view.name) == sizeof(view.name)-1);
    player.active=0;
    REQUIRE_FALSE(lobbyProjectPlayerView(&player, 1, 4, &view));
    const lobbyplayer_view empty{};
    REQUIRE(std::memcmp(&view, &empty, sizeof(view)) == 0);
}

TEST_CASE("Listen host is a visible lobby participant and row compaction preserves leader identity",
          "[net][lobby][roster][T-NETWORKING-011]")
{
    lobbyFixtureReset(1, 0);
    lobbyFixtureClient(0, "Host", lobbyFixtureLobbyState());
    lobbyFixtureClient(2, "Earlier peer", lobbyFixtureLobbyState());
    lobbyFixtureClient(4, "Room creator", lobbyFixtureLobbyState());
    lobbyUpdate();
    REQUIRE(g_Lobby.numPlayers == 3);
    REQUIRE(g_Lobby.players[0].clientId == 0);
    REQUIRE(std::strcmp(g_Lobby.players[0].name, "Host") == 0);
    REQUIRE(lobbyIsLocalLeader());
    lobbySetLeader(2);
    lobbyFixtureClient(2, "", 0);
    lobbyUpdate();
    REQUIRE(g_Lobby.numPlayers == 2);
    REQUIRE(g_Lobby.players[g_Lobby.leaderSlot].clientId == 4);
    REQUIRE_FALSE(lobbyIsLocalLeader());
    REQUIRE(g_Lobby.players[2].active == 0);
}

TEST_CASE("Room UI authority follows accepted room ownership on host and remote clients",
          "[net][lobby][authority][T-NETWORKING-011]")
{
    lobbyFixtureReset(1, -1);
    REQUIRE_FALSE(lobbyIsLocalLeader());
    lobbyFixtureReset(1, 0);
    lobbyFixtureClient(0, "Host", lobbyFixtureLobbyState());
    lobbyFixtureClient(3, "Room owner", lobbyFixtureLobbyState());
    lobbyUpdate();
    auto *room = roomCreateConfigured("Remote room", 4, ROOM_ACCESS_OPEN, "", 3);
    REQUIRE(room);
    REQUIRE(roomJoinForClient(nullptr, room, 0, "") == ROOM_RESULT_OK);
    lobbyFixtureLocalRoom(room->id);
    REQUIRE_FALSE(lobbyIsLocalLeader());
    room->creator_client_id=0;
    REQUIRE(lobbyIsLocalLeader());

    lobbyFixtureReset(0, 3);
    lobbyFixtureClient(0, "Host", lobbyFixtureLobbyState());
    lobbyFixtureClient(3, "Remote owner", lobbyFixtureLobbyState());
    lobbyUpdate();
    lobbyFixtureLocalRoom(1);
    REQUIRE_FALSE(lobbyIsLocalLeader()); // No authoritative cache yet.
    g_RoomCacheCount=1; g_RoomCache[0].id=1; g_RoomCache[0].creator_client_id=3;
    REQUIRE(lobbyIsLocalLeader());
    g_RoomCache[0].creator_client_id=0;
    REQUIRE_FALSE(lobbyIsLocalLeader());
}

TEST_CASE("Lobby preparation never masquerades as live gameplay readiness",
          "[net][lobby][ready][T-NETWORKING-011]")
{
    lobbyFixtureReset(1, 0);
    lobbyFixtureClient(0, "Host", lobbyFixturePreparingState());
    lobbyUpdate();
    REQUIRE(g_Lobby.numPlayers == 1);
    REQUIRE(g_Lobby.players[0].isReady == 0);
    REQUIRE(g_Lobby.inGame == 0);
    lobbyFixtureClient(0, "Host", lobbyFixtureGameState());
    lobbyUpdate();
    REQUIRE(g_Lobby.players[0].isReady == 1);
    REQUIRE(g_Lobby.inGame == 1);
}
