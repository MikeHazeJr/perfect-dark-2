#include "catch.hpp"
#include "net/room_wire.h"
#include <cstring>
extern "C" {
struct netbuf { u8 *data; u32 size, rp, wp, error; };
void netbufStartWrite(struct netbuf *);
void netbufStartReadData(struct netbuf *, const void *, u32);
}

TEST_CASE("Room create wire preserves initial remote settings and rejects every truncated payload atomically",
          "[net][room][wire][T-NETWORKING-011]")
{
    net_room_create_request_t input{};
    std::strcpy(input.name, "Custom room");
    std::strcpy(input.password, "private");
    input.access = ROOM_ACCESS_PASSWORD;
    input.max_players = 13;
    input.settings.num_bots = 5;
    input.settings.timelimit = 27;
    input.settings.scorelimit = 89;
    input.settings.teamscorelimit = 1234;
    input.settings.options = 0x98765432;
    input.settings.scenario = 3;
    input.settings.weapon_set = 0xff;
    std::strcpy(input.settings.stage_id, "custom:arena");
    std::strcpy(input.playlist, "custom:track_a;custom:track_b");
    u8 bytes[2048]{};
    netbuf write{bytes, sizeof(bytes), 0, 0, 0};
    REQUIRE(netRoomCreatePayloadWrite(&write, &input));
    for (u32 size = 0; size < write.wp; ++size) {
        netbuf read{};
        netbufStartReadData(&read, bytes, size);
        net_room_create_request_t output;
        std::memset(&output, 0x5a, sizeof(output));
        const auto before = output;
        REQUIRE_FALSE(netRoomCreatePayloadRead(&read, &output));
        REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
    }
    netbuf read{};
    netbufStartReadData(&read, bytes, write.wp);
    net_room_create_request_t output{};
    REQUIRE(netRoomCreatePayloadRead(&read, &output));
    REQUIRE(std::memcmp(&input, &output, sizeof(input)) == 0);
    REQUIRE(read.rp == write.wp);
}

TEST_CASE("Room create encoder rejects insufficient capacity without partial payload bytes",
          "[net][room][wire][T-NETWORKING-011]")
{
    net_room_create_request_t input{};
    u8 bytes[24];
    std::memset(bytes, 0x5a, sizeof(bytes));
    netbuf write{bytes, sizeof(bytes), 0, 0, 0};
    REQUIRE_FALSE(netRoomCreatePayloadWrite(&write, &input));
    REQUIRE(write.wp == 0);
    for (const auto byte : bytes) REQUIRE(byte == 0x5a);
}

TEST_CASE("Room create decoder rejects identity bytes hidden behind an embedded terminator",
          "[net][room][wire][T-NETWORKING-011]")
{
    net_room_create_request_t input{};
    std::strcpy(input.name, "VisibleHidden");
    u8 bytes[128]{};
    netbuf write{bytes, sizeof(bytes), 0, 0, 0};
    REQUIRE(netRoomCreatePayloadWrite(&write, &input));
    bytes[2 + 7] = 0; // Keep the declared wire string length, hide its suffix.
    netbuf read{};
    netbufStartReadData(&read, bytes, write.wp);
    net_room_create_request_t output{};
    std::strcpy(output.name, "Unchanged");
    REQUIRE_FALSE(netRoomCreatePayloadRead(&read, &output));
    REQUIRE(std::strcmp(output.name, "Unchanged") == 0);
}
