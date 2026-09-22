#include "catch.hpp"
#include <cstring>

extern "C" {
#include "room.h"
u32 g_NetTick = 1;
static unsigned room_test_leaves;
static unsigned room_test_aborts;
void netReadyGateOnClientLeft(u8) { ++room_test_leaves; }
void netReadyGateAbortForRoom(u8, const char *) { ++room_test_aborts; }
}

namespace {
struct Rooms {
    Rooms() {
        roomsInit();
        for (u8 i = 0; i < HUB_MAX_ROOMS; ++i) {
            if (auto *room = roomGetById(i)) roomDestroy(room);
        }
        room_test_leaves = room_test_aborts = 0;
    }
};
}

TEST_CASE("Room admission preserves the previous room and ready gate on rejection",
          "[net][room][transactions][T-NETWORKING-011]")
{
    Rooms reset;
    auto *previous = roomCreateConfigured("Old", 4, ROOM_ACCESS_OPEN, "", 1);
    auto *full = roomCreateConfigured("Full", 1, ROOM_ACCESS_OPEN, "", 2);
    REQUIRE(previous);
    REQUIRE(full);
    const auto before = *previous;
    const auto target_before = *full;
    REQUIRE(roomJoinForClient(previous, full, 1, "") == ROOM_RESULT_FULL);
    REQUIRE(std::memcmp(previous, &before, sizeof(before)) == 0);
    REQUIRE(std::memcmp(full, &target_before, sizeof(target_before)) == 0);
    REQUIRE(room_test_leaves == 0);
    REQUIRE(room_test_aborts == 0);
    REQUIRE(roomJoinForClient(previous, nullptr, 1, "") == ROOM_RESULT_NOT_FOUND);
    REQUIRE(std::memcmp(previous, &before, sizeof(before)) == 0);
}

TEST_CASE("Permanent Lounge initializes and resets with publishable capacity and no stale owner",
          "[net][room][defaults][T-NETWORKING-011]")
{
    roomsInit();
    auto *lounge = roomGetById(0);
    REQUIRE(lounge);
    REQUIRE(lounge->max_players == HUB_MAX_CLIENTS);
    REQUIRE(lounge->creator_client_id == 0xff);
    REQUIRE(roomOccupiedCount(lounge) == 0);
    REQUIRE(roomCanJoin(lounge, 1));
    lounge->max_players = 2;
    lounge->creator_client_id = 1;
    roomDestroy(lounge);
    REQUIRE(lounge->max_players == HUB_MAX_CLIENTS);
    REQUIRE(lounge->creator_client_id == 0xff);
    REQUIRE(lounge->state == ROOM_STATE_LOBBY);
    REQUIRE(roomCanJoin(lounge, 1));
}

TEST_CASE("Room admission is idempotent and commits one membership after validation",
          "[net][room][transactions][T-NETWORKING-011]")
{
    Rooms reset;
    auto *previous = roomCreateConfigured("Old", 4, ROOM_ACCESS_OPEN, "", 1);
    auto *next = roomCreateConfigured("Next", 4, ROOM_ACCESS_PASSWORD, "secret", 2);
    REQUIRE(previous);
    REQUIRE(next);
    REQUIRE(roomJoinForClient(previous, next, 1, "wrong") == ROOM_RESULT_PASSWORD);
    REQUIRE(previous->client_count == 1);
    REQUIRE(roomJoinForClient(previous, next, 1, "secret") == ROOM_RESULT_OK);
    REQUIRE(next->client_count == 2);
    REQUIRE(roomGetById(previous->id) == nullptr);
    REQUIRE(room_test_leaves == 1);
    REQUIRE(roomJoinForClient(next, next, 1, "") == ROOM_RESULT_OK);
    REQUIRE(next->client_count == 2);
    REQUIRE(room_test_leaves == 1);
}

TEST_CASE("Room create exhaustion and invalid access preserve the previous assignment",
          "[net][room][transactions][T-NETWORKING-011]")
{
    Rooms reset;
    auto *previous = roomCreateConfigured("Old", 4, ROOM_ACCESS_OPEN, "", 1);
    REQUIRE(previous);
    REQUIRE(roomCreateConfigured("Second", 4, ROOM_ACCESS_OPEN, "", 2));
    REQUIRE(roomCreateConfigured("Third", 4, ROOM_ACCESS_OPEN, "", 3));
    const auto before = *previous;
    auto *result = previous;
    REQUIRE(roomCreateForClient(previous, 1, "New", 4, ROOM_ACCESS_OPEN, "", &result)
            == ROOM_RESULT_NO_ROOM_SLOTS);
    REQUIRE(result == previous);
    REQUIRE(std::memcmp(previous, &before, sizeof(before)) == 0);
    REQUIRE(roomCreateForClient(previous, 1, "New", 4, (room_access_t)99, "", &result)
            == ROOM_RESULT_INVALID);
    REQUIRE(std::memcmp(previous, &before, sizeof(before)) == 0);
    REQUIRE(room_test_leaves == 0);
}

TEST_CASE("Room invitation is leader scoped and consumed by successful admission",
          "[net][room][transactions][T-NETWORKING-011]")
{
    Rooms reset;
    auto *room = roomCreateConfigured("Private", 4, ROOM_ACCESS_INVITE, "", 1);
    REQUIRE(room);
    REQUIRE(roomJoinForClient(nullptr, room, 2, "") == ROOM_RESULT_INVITE_REQUIRED);
    REQUIRE(roomInviteClient(room, 3, 2) == ROOM_RESULT_NOT_LEADER);
    REQUIRE(roomInviteClient(room, 1, 2) == ROOM_RESULT_OK);
    REQUIRE(roomJoinForClient(nullptr, room, 2, "") == ROOM_RESULT_OK);
    REQUIRE((room->invited_client_mask & (1u << 2)) == 0);
    roomLeave(room, 1);
    REQUIRE(room->creator_client_id == 2);
}

TEST_CASE("Room snapshots are independent and failed writes preserve accepted data",
          "[net][room][snapshots][T-NETWORKING-011]")
{
    Rooms reset;
    auto *first = roomCreateConfigured("First", 4, ROOM_ACCESS_OPEN, "", 1);
    auto *second = roomCreateConfigured("Second", 4, ROOM_ACCESS_OPEN, "", 2);
    REQUIRE(first);
    REQUIRE(second);
    room_settings_t settings{};
    settings.num_bots = 7;
    settings.timelimit = 13;
    std::strcpy(settings.stage_id, "base:mp_skedar");
    REQUIRE(roomStoreSettings(first, &settings) == 1);
    REQUIRE(roomStorePlaylist(first, "mod:first;mod:second") == 1);
    REQUIRE(first->settings_revision == 1);
    REQUIRE(first->playlist_revision == 1);
    REQUIRE(second->settings_revision == 0);
    REQUIRE(second->playlist_revision == 0);
    REQUIRE(first->settings.num_bots == 7);
    REQUIRE(std::strcmp(first->playlist, "mod:first;mod:second") == 0);
    const auto before = *first;
    std::memset(settings.stage_id, 'x', sizeof(settings.stage_id));
    REQUIRE(roomStoreSettings(first, &settings) == 0);
    char oversized[ROOM_PLAYLIST_TEXT_MAX + 1];
    std::memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = 0;
    REQUIRE(roomStorePlaylist(first, oversized) == 0);
    REQUIRE(std::memcmp(first, &before, sizeof(before)) == 0);
    REQUIRE(roomStorePlaylist(first, "") == 1);
    REQUIRE(first->playlist_revision == 2);
    REQUIRE(first->playlist[0] == 0);
}

TEST_CASE("Room reset clears private access and accepted snapshots including the permanent lounge",
          "[net][room][snapshots][T-NETWORKING-011]")
{
    Rooms reset;
    auto *lounge = roomGetById(0);
    REQUIRE(lounge);
    room_settings_t settings{};
    settings.timelimit = 42;
    REQUIRE(roomStoreSettings(lounge, &settings));
    REQUIRE(roomStorePlaylist(lounge, "mod:track"));
    lounge->creator_client_id = 1;
    REQUIRE(roomInviteClient(lounge, 1, 3) == ROOM_RESULT_OK);
    lounge->access = ROOM_ACCESS_INVITE;
    roomDestroy(lounge);
    REQUIRE(lounge->state == ROOM_STATE_LOBBY);
    REQUIRE(lounge->settings_revision == 0);
    REQUIRE(lounge->playlist_revision == 0);
    REQUIRE(lounge->invited_client_mask == 0);
    REQUIRE(lounge->creator_client_id == 0xff);
    REQUIRE(lounge->access == ROOM_ACCESS_OPEN);
}

TEST_CASE("Room leader reservation retains authority until expiry and then elects a present member",
          "[net][room][reconnect][T-NETWORKING-011]")
{
    Rooms reset;
    auto *room = roomCreateConfigured("Reconnect", 2, ROOM_ACCESS_OPEN, "", 1);
    REQUIRE(room);
    REQUIRE(roomJoinForClient(nullptr, room, 2, "") == ROOM_RESULT_OK);
    REQUIRE(roomLeaveForReconnect(room, 1));
    REQUIRE(room->creator_client_id == 1);
    REQUIRE(roomJoinForClient(nullptr, room, 3, "") == ROOM_RESULT_FULL);
    roomReleaseReconnectReservation(room, 1);
    REQUIRE(room->creator_client_id == 2);
    REQUIRE(roomJoinForClient(nullptr, room, 3, "") == ROOM_RESULT_OK);
}
