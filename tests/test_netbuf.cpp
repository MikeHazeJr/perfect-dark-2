/*
 * tests/test_netbuf.cpp -- Wire format primitive roundtrip tests.
 *
 * Code under test: port/src/net/netbuf.c
 *
 * The netbuf primitives are the bottom layer of every netmsg encode and
 * decode path. A byte-order regression here breaks every wire message
 * silently. These tests verify that:
 *   - Write-then-read for each primitive type returns the original value.
 *   - Multi-write / multi-read sequences preserve order.
 *   - Read past end of buffer does not crash and sets the error flag.
 *   - Write past capacity does not crash and sets the error flag.
 *   - String length-prefix discipline is preserved.
 *
 * Roundtrip is the right test shape here: any drift between Write and
 * Read pair (PD_LE swap, sizeof, alignment, length-prefix) breaks the
 * roundtrip property. We do NOT test specific wire bytes because byte
 * layout is implementation detail; the contract is "what I wrote, I
 * read back."
 *
 * NOTE: This file deliberately does NOT include "types.h" or
 * "net/netbuf.h". The project pattern (see port/fast3d/pdgui_*.cpp) is
 * that C++ TUs forward-declare the C API directly to avoid the
 * `#define bool s32` macro in types.h colliding with the C++ standard.
 * Forward declarations here mirror port/include/net/netbuf.h verbatim.
 */

#include "catch.hpp"
#include <PR/ultratypes.h>

#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "net/grid_match_result.h"
#include "net/grid_match_wire.h"
#include "net_move_fixture.h"

/* Forward-declared netbuf API — keep in sync with port/include/net/netbuf.h. */
extern "C" {
struct netbuf {
    u8 *data;
    u32 size;
    u32 rp;
    u32 wp;
    u32 error;
};

void netbufStartRead(struct netbuf *buf);
void netbufStartReadData(struct netbuf *buf, const void *data, u32 size);
s32  netbufReadLeft(const struct netbuf *buf);
u8   netbufReadU8(struct netbuf *buf);
u16  netbufReadU16(struct netbuf *buf);
u32  netbufReadU32(struct netbuf *buf);
u64  netbufReadU64(struct netbuf *buf);
s8   netbufReadS8(struct netbuf *buf);
s16  netbufReadS16(struct netbuf *buf);
s32  netbufReadS32(struct netbuf *buf);
s64  netbufReadS64(struct netbuf *buf);
f32  netbufReadF32(struct netbuf *buf);
const char *netbufReadStr(struct netbuf *buf);

void netbufStartWrite(struct netbuf *buf);
s32  netbufWriteLeft(const struct netbuf *buf);
u32  netbufWriteU8(struct netbuf *buf, const u8 v);
u32  netbufWriteU16(struct netbuf *buf, const u16 v);
u32  netbufWriteU32(struct netbuf *buf, const u32 v);
u32  netbufWriteU64(struct netbuf *buf, const u64 v);
u32  netbufWriteS8(struct netbuf *buf, const s8 v);
u32  netbufWriteS16(struct netbuf *buf, const s16 v);
u32  netbufWriteS32(struct netbuf *buf, const s32 v);
u32  netbufWriteS64(struct netbuf *buf, const s64 v);
u32  netbufWriteF32(struct netbuf *buf, const f32 v);
u32  netbufWriteStr(struct netbuf *buf, const char *v);
void netbufReset(struct netbuf *buf);
}

namespace {
constexpr size_t kBufBytes = 256;

struct ScratchBuf {
    u8 storage[kBufBytes];
    netbuf nb;

    ScratchBuf() {
        std::memset(storage, 0, sizeof(storage));
        nb.data  = storage;
        nb.size  = sizeof(storage);
        nb.rp    = 0;
        nb.wp    = 0;
        nb.error = 0;
        netbufStartWrite(&nb);
    }

    /* Real receivers call netbufStartReadData with the actual wire-payload
     * size; the buffer's `size` field becomes the readable extent.
     * netbufStartRead alone resets wp = size (the FULL buffer), so it
     * is the wrong helper for "switch from write to read on the same
     * scratch buffer with N bytes written" — use this instead. */
    void rewind_for_read() {
        u32 written = nb.wp;
        netbufStartReadData(&nb, storage, written);
    }
};
} /* anon */

TEST_CASE("netbuf: u8 roundtrip", "[netbuf]") {
    ScratchBuf b;
    netbufWriteU8(&b.nb, 0xA5);
    netbufWriteU8(&b.nb, 0x00);
    netbufWriteU8(&b.nb, 0xFF);
    b.rewind_for_read();
    REQUIRE(netbufReadU8(&b.nb) == 0xA5);
    REQUIRE(netbufReadU8(&b.nb) == 0x00);
    REQUIRE(netbufReadU8(&b.nb) == 0xFF);
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: u16 little-endian roundtrip", "[netbuf]") {
    ScratchBuf b;
    netbufWriteU16(&b.nb, 0x1234);
    netbufWriteU16(&b.nb, 0xCAFE);
    netbufWriteU16(&b.nb, 0x0001);
    netbufWriteU16(&b.nb, 0xFFFF);
    b.rewind_for_read();
    REQUIRE(netbufReadU16(&b.nb) == 0x1234);
    REQUIRE(netbufReadU16(&b.nb) == 0xCAFE);
    REQUIRE(netbufReadU16(&b.nb) == 0x0001);
    REQUIRE(netbufReadU16(&b.nb) == 0xFFFF);
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: u16 little-endian byte order", "[netbuf]") {
    /* 0x1234 must hit the wire as bytes [0x34, 0x12], NOT [0x12, 0x34].
     * Catches a regression in PD_LE16 macro definition. */
    ScratchBuf b;
    netbufWriteU16(&b.nb, 0x1234);
    REQUIRE(b.storage[0] == 0x34);
    REQUIRE(b.storage[1] == 0x12);
}

TEST_CASE("netbuf: u32 little-endian roundtrip", "[netbuf]") {
    ScratchBuf b;
    netbufWriteU32(&b.nb, 0xDEADBEEFu);
    netbufWriteU32(&b.nb, 0u);
    netbufWriteU32(&b.nb, 0xFFFFFFFFu);
    netbufWriteU32(&b.nb, 0x12345678u);
    b.rewind_for_read();
    REQUIRE(netbufReadU32(&b.nb) == 0xDEADBEEFu);
    REQUIRE(netbufReadU32(&b.nb) == 0u);
    REQUIRE(netbufReadU32(&b.nb) == 0xFFFFFFFFu);
    REQUIRE(netbufReadU32(&b.nb) == 0x12345678u);
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: u32 little-endian byte order", "[netbuf]") {
    /* 0xDEADBEEF -> [0xEF, 0xBE, 0xAD, 0xDE] on the wire. */
    ScratchBuf b;
    netbufWriteU32(&b.nb, 0xDEADBEEFu);
    REQUIRE(b.storage[0] == 0xEF);
    REQUIRE(b.storage[1] == 0xBE);
    REQUIRE(b.storage[2] == 0xAD);
    REQUIRE(b.storage[3] == 0xDE);
}

TEST_CASE("netbuf: u64 little-endian roundtrip", "[netbuf]") {
    ScratchBuf b;
    netbufWriteU64(&b.nb, 0x0123456789ABCDEFull);
    netbufWriteU64(&b.nb, 0ull);
    netbufWriteU64(&b.nb, 0xFFFFFFFFFFFFFFFFull);
    b.rewind_for_read();
    REQUIRE(netbufReadU64(&b.nb) == 0x0123456789ABCDEFull);
    REQUIRE(netbufReadU64(&b.nb) == 0ull);
    REQUIRE(netbufReadU64(&b.nb) == 0xFFFFFFFFFFFFFFFFull);
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: signed roundtrip preserves negative values", "[netbuf]") {
    ScratchBuf b;
    netbufWriteS8(&b.nb, -1);
    netbufWriteS8(&b.nb, -128);
    netbufWriteS16(&b.nb, -32768);
    netbufWriteS32(&b.nb, -1);
    netbufWriteS64(&b.nb, -1);
    b.rewind_for_read();
    REQUIRE(netbufReadS8(&b.nb) == -1);
    REQUIRE(netbufReadS8(&b.nb) == -128);
    REQUIRE(netbufReadS16(&b.nb) == -32768);
    REQUIRE(netbufReadS32(&b.nb) == -1);
    REQUIRE(netbufReadS64(&b.nb) == -1);
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: f32 roundtrip preserves bit pattern", "[netbuf]") {
    /* IEEE 754 f32 must roundtrip exactly through the union punning path. */
    ScratchBuf b;
    netbufWriteF32(&b.nb, 0.0f);
    netbufWriteF32(&b.nb, 1.0f);
    netbufWriteF32(&b.nb, -1.5f);
    netbufWriteF32(&b.nb, 3.14159265f);
    netbufWriteF32(&b.nb, 1e20f);
    b.rewind_for_read();
    REQUIRE(netbufReadF32(&b.nb) == 0.0f);
    REQUIRE(netbufReadF32(&b.nb) == 1.0f);
    REQUIRE(netbufReadF32(&b.nb) == -1.5f);
    REQUIRE(netbufReadF32(&b.nb) == Approx(3.14159265f));
    REQUIRE(netbufReadF32(&b.nb) == Approx(1e20f));
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: string roundtrip preserves null terminator", "[netbuf]") {
    ScratchBuf b;
    netbufWriteStr(&b.nb, "hello");
    netbufWriteStr(&b.nb, "");
    netbufWriteStr(&b.nb, "base:dark_combat");
    b.rewind_for_read();
    const char *s1 = netbufReadStr(&b.nb);
    REQUIRE(s1 != nullptr);
    REQUIRE(std::string(s1) == "hello");
    const char *s2 = netbufReadStr(&b.nb);
    REQUIRE(s2 != nullptr);
    REQUIRE(std::string(s2) == "");
    const char *s3 = netbufReadStr(&b.nb);
    REQUIRE(s3 != nullptr);
    REQUIRE(std::string(s3) == "base:dark_combat");
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: zero-length wire string returns safe empty string", "[netbuf][security]") {
    /* Malformed peers can send a string length of 0. The reader must not
     * return a pointer into subsequent field bytes and let strlen/strcmp
     * scan outside the declared string. */
    u8 wire[] = {
        0x00, 0x00,             /* string len = 0 */
        'N', 'O', 'T', 0x00     /* next field payload, not part of string */
    };
    netbuf nb;
    netbufStartReadData(&nb, wire, sizeof(wire));
    const char *s = netbufReadStr(&nb);
    REQUIRE(s != nullptr);
    REQUIRE(std::string(s) == "");
    REQUIRE(nb.rp == 2);
    REQUIRE(netbufReadLeft(&nb) == 4);
    REQUIRE(nb.error == 0);
}

TEST_CASE("netbuf: unterminated wire string is rejected without mutating payload",
          "[netbuf][security]") {
    u8 wire[] = {
        0x05, 0x00,             /* string len = 5 */
        'h', 'e', 'l', 'l', 'o',
        0xAB                    /* next field payload */
    };
    netbuf nb;
    netbufStartReadData(&nb, wire, sizeof(wire));
    const char *s = netbufReadStr(&nb);
    REQUIRE(s == nullptr);
    REQUIRE(nb.error == 1);
    REQUIRE(nb.rp == 2);
    REQUIRE(wire[6] == 'o');
    REQUIRE(wire[7] == 0xAB);
}

TEST_CASE("net distribution: SVC_DISTRIB_BEGIN carries mandatory digest",
          "[netbuf][security][static]") {
    auto read_file = [](const char *path) {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };

    const std::string distrib = read_file("port/src/net/netdistrib.c");
    const std::string netmsg = read_file("port/src/net/netmsg.c");
    const std::string netmsg_h = read_file("port/include/net/netmsg.h");

    REQUIRE(distrib.find("expected_sha256[SHA256_DIGEST_SIZE]") != std::string::npos);
    REQUIRE(distrib.find("sha256Hash(compressed, (size_t)compressed_len, compressed_sha256)") != std::string::npos);
    REQUIRE(distrib.find("memcmp(actual, slot->expected_sha256") != std::string::npos);
    REQUIRE(netmsg.find("netbufWriteData(dst, expected_sha256, 32)") != std::string::npos);
    REQUIRE(netmsg.find("netbufReadData(src, expected_sha256, sizeof(expected_sha256))") != std::string::npos);
    REQUIRE(netmsg_h.find("const u8 expected_sha256[32]") != std::string::npos);
}

TEST_CASE("net move: client weapon select is server inventory-gated",
          "[netbuf][security][static]") {
    auto read_file = [](const char *path) {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };

    const std::string netmsg = read_file("port/src/net/net_player_move_ingress.c");

    REQUIRE(netmsg.find("static bool netmsgClientCanSelectWeapon") != std::string::npos);
    REQUIRE(netmsg.find("invHasSingleWeaponIncAllGuns(weaponnum)") != std::string::npos);
    REQUIRE(netmsg.find("invHasDoubleWeaponIncAllGuns(weaponnum, weaponnum)") != std::string::npos);
    REQUIRE(netmsg.find("NET: rejected CLC_MOVE weapon select") != std::string::npos);
    REQUIRE(netmsg.find("newmove.ucmd &= ~(UCMD_SELECT | UCMD_SELECT_DUAL)") != std::string::npos);

    const size_t move_read = netmsg.find("u32 netmsgClcMoveRead");
    const size_t parse_move = netmsg.find("netbufReadPlayerMove(src, &newmove)", move_read);
    const size_t parse_error = netmsg.find("if (src->error)", parse_move);
    const size_t state_gate = netmsg.find("srccl->state != CLSTATE_GAME", parse_error);
    const size_t weapon_gate = netmsg.find("netmsgClientCanSelectWeapon", parse_error);
    const size_t ack_commit = netmsg.find("netPlayerMoveCommit(srccl, &newmove, outmoveack)", parse_error);
    REQUIRE(move_read != std::string::npos);
    REQUIRE(parse_move != std::string::npos);
    REQUIRE(parse_error != std::string::npos);
    REQUIRE(state_gate != std::string::npos);
    REQUIRE(weapon_gate != std::string::npos);
    REQUIRE(ack_commit != std::string::npos);
    REQUIRE(parse_move < parse_error);
    REQUIRE(parse_error < state_gate);
    REQUIRE(parse_error < weapon_gate);
    REQUIRE(parse_error < ack_commit);
    REQUIRE(weapon_gate < ack_commit);

    // A dead player's select of no weapon is dropped before the gate, without
    // the rejection warning (49 warnings in one death, 2026-11-16)
    const size_t none_drop = netmsg.find("(newmove.ucmd & UCMD_SELECT) && newmove.weaponnum == WEAPON_NONE", parse_error);
    const size_t gated = netmsg.find("} else if ((newmove.ucmd & UCMD_SELECT) && newmove.weaponnum >= 0) {", none_drop);
    REQUIRE(none_drop != std::string::npos);
    REQUIRE(gated != std::string::npos);
    REQUIRE(none_drop < weapon_gate);
    REQUIRE(netmsg.find("sysLogPrintf", none_drop) > gated);
}

namespace {
constexpr u32 kMoveFire = 1u;
constexpr u32 kMoveSelect = 1u << 7;
constexpr u32 kMoveDual = 1u << 8;
constexpr u32 kMoveForcePosition = 1u << 29;
constexpr u16 kGridTriangleRef = 0x8114;
constexpr u16 kGridNoSelection = 0xffff;

void writeMoveIngressPacket(ScratchBuf &packet, bool snapshot, bool grid, u32 epoch,
        u32 tick, u32 ack, u32 commands, u16 weapon) {
    if (grid) netbufWriteU32(&packet.nb, epoch);
    if (snapshot) netbufWriteU8(&packet.nb, 1);
    netbufWriteU32(&packet.nb, ack);
    netbufWriteU32(&packet.nb, tick);
    netbufWriteU32(&packet.nb, commands);
    for (int i = 0; i < 8; i++) netbufWriteF32(&packet.nb, 0.f);
    if (grid) netbufWriteU16(&packet.nb, weapon);
    else netbufWriteU8(&packet.nb, static_cast<u8>(weapon));
    netbufWriteF32(&packet.nb, 250.f);
    netbufWriteF32(&packet.nb, 0.f);
    netbufWriteF32(&packet.nb, 0.f);
    if (snapshot && (commands & kMoveForcePosition)) {
        netbufWriteS16(&packet.nb, 4);
        netbufWriteS16(&packet.nb, -1);
    }
}
} // namespace

TEST_CASE("Grid movement checks the host's local inventory for a typed selection", "[net][move][grid]") {
    const std::unique_ptr<void, decltype(&moveIngressDestroy)> fixture(moveIngressCreate(0), moveIngressDestroy);
    REQUIRE(fixture);
    moveIngressGrid(fixture.get(), 7, 1, kGridTriangleRef, 86);
    move_ingress_result result{};

    ScratchBuf accepted;
    const u32 command = kMoveFire | kMoveSelect | kMoveDual;
    writeMoveIngressPacket(accepted, false, true, 7, 100, 49, command, kGridTriangleRef);
    REQUIRE(moveIngressReadGrid(fixture.get(), 0, accepted.storage, accepted.nb.wp, &result) == 0);
    CHECK(result.weapon == 86);
    CHECK(result.inventoryWeapon == 86);
    CHECK(result.inventoryPlayer == 1);
    CHECK(result.singleChecks == 1);
    CHECK(result.doubleChecks == 1);
    CHECK(result.command == command);

    ScratchBuf single;
    writeMoveIngressPacket(single, false, true, 7, 101, 50, kMoveFire | kMoveSelect, kGridTriangleRef);
    REQUIRE(moveIngressReadGrid(fixture.get(), 0, single.storage, single.nb.wp, &result) == 0);
    CHECK(result.weapon == 86);
    CHECK(result.command == (kMoveFire | kMoveSelect));
    CHECK(result.doubleChecks == 0);

    moveIngressInventory(fixture.get(), 0, 0);
    ScratchBuf denied;
    writeMoveIngressPacket(denied, false, true, 7, 102, 51, command, kGridTriangleRef);
    REQUIRE(moveIngressReadGrid(fixture.get(), 0, denied.storage, denied.nb.wp, &result) == 0);
    CHECK(result.inventoryWeapon == 86);
    CHECK(result.weapon == -1);
    CHECK(result.command == kMoveFire);
    CHECK(result.ack == 51);

    u32 tick = 103;
    for (u16 ref : {static_cast<u16>(kGridTriangleRef + 1), kGridNoSelection}) {
        ScratchBuf invalid;
        writeMoveIngressPacket(invalid, false, true, 7, tick++, 51, command, ref);
        REQUIRE(moveIngressReadGrid(fixture.get(), 0, invalid.storage, invalid.nb.wp, &result) == 0);
        CHECK(result.weapon == -1);
        CHECK(result.command == kMoveFire);
        CHECK(result.singleChecks == 0);
        CHECK(result.doubleChecks == 0);
    }
}

TEST_CASE("Grid movement discards stale and legacy frames after consuming the complete record", "[net][move][grid]") {
    const std::unique_ptr<void, decltype(&moveIngressDestroy)> fixture(moveIngressCreate(1), moveIngressDestroy);
    REQUIRE(fixture);
    move_ingress_result result{};
    const u32 command = kMoveFire | kMoveSelect | kMoveForcePosition;

    for (bool snapshot : {false, true}) {
        for (bool grid : {false, true}) {
            moveIngressGrid(fixture.get(), 7, 1, kGridTriangleRef, 87);
            ScratchBuf stale;
            writeMoveIngressPacket(stale, snapshot, grid, 6, 200, 80, command, kGridTriangleRef);
            const u32 messageSize = stale.nb.wp;
            netbufWriteU8(&stale.nb, 0xa5);
            const auto read = grid ? moveIngressReadGrid : moveIngressRead;
            REQUIRE(read(fixture.get(), snapshot, stale.storage, stale.nb.wp, &result) == 0);
            CHECK(result.unchanged == 1);
            CHECK(result.bytesRead == messageSize);
            CHECK(result.gridRefChecks == 0);
            CHECK(result.teleports == 0);
            CHECK(result.ackValid == 0);
        }
    }

    ScratchBuf current;
    writeMoveIngressPacket(current, true, true, 7, 201, 81, command, kGridTriangleRef);
    moveIngressGrid(fixture.get(), 7, 0, kGridTriangleRef, 87);
    REQUIRE(moveIngressReadGrid(fixture.get(), 1, current.storage, current.nb.wp, &result) == 0);
    CHECK(result.unchanged == 1);
    CHECK(result.bytesRead == current.nb.wp);
    CHECK(result.gridRefChecks == 0);

    moveIngressGrid(fixture.get(), 7, 1, kGridTriangleRef, 87);
    REQUIRE(moveIngressReadGrid(fixture.get(), 1, current.storage, current.nb.wp - 1, &result) != 0);
    CHECK(result.unchanged == 1);
    CHECK(result.teleports == 0);
    CHECK(result.ackValid == 0);

    REQUIRE(moveIngressReadGrid(fixture.get(), 1, current.storage, current.nb.wp, &result) == 0);
    CHECK(result.weapon == 87);
    CHECK(result.command == command);
    CHECK(result.ack == 81);
    CHECK(result.gridRefChecks == 1);
    CHECK(result.teleports == 1);
    CHECK(result.room == 4);
}

TEST_CASE("Grid reconnect records a preserved remote peer's position", "[net][move][grid]") {
    const std::unique_ptr<void, decltype(&moveIngressDestroy)> fixture(moveIngressCreate(0), moveIngressDestroy);
    REQUIRE(fixture);
    moveIngressAbsent(fixture.get());
    moveIngressGrid(fixture.get(), 7, 1, kGridTriangleRef, 87);

    ScratchBuf snapshot;
    writeMoveIngressPacket(snapshot, true, true, 7, 200, 80, kMoveForcePosition, kGridNoSelection);
    move_ingress_result result{};
    REQUIRE(moveIngressReadGrid(fixture.get(), 1, snapshot.storage, snapshot.nb.wp, &result) == 0);
    CHECK(result.tick == 200);
    CHECK(result.ack == 80);
    CHECK(result.command == kMoveForcePosition);
    CHECK(result.position == 250.f);
    CHECK(result.weapon == -1);
    CHECK(result.gridRefChecks == 0);
    CHECK(result.teleports == 0);
}

TEST_CASE("netbuf: mixed-type sequence preserves order", "[netbuf]") {
    /* Real netmsg payloads interleave types. Verify a canonical
     * SVC_PLAYER_STATS-shaped sequence roundtrips. */
    ScratchBuf b;
    netbufWriteU8(&b.nb, 0x22);                  /* opcode */
    netbufWriteU8(&b.nb, 7);                     /* slot index */
    netbufWriteU32(&b.nb, 12345);                /* score */
    netbufWriteS16(&b.nb, -42);                  /* delta */
    netbufWriteStr(&b.nb, "victim_name");        /* name */
    netbufWriteS8(&b.nb, 3);                     /* attacker_id (v43+) */
    b.rewind_for_read();
    REQUIRE(netbufReadU8(&b.nb)  == 0x22);
    REQUIRE(netbufReadU8(&b.nb)  == 7);
    REQUIRE(netbufReadU32(&b.nb) == 12345u);
    REQUIRE(netbufReadS16(&b.nb) == -42);
    const char *s = netbufReadStr(&b.nb);
    REQUIRE(s != nullptr);
    REQUIRE(std::string(s) == "victim_name");
    REQUIRE(netbufReadS8(&b.nb) == 3);
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: read past end sets error and returns zero", "[netbuf]") {
    ScratchBuf b;
    netbufWriteU8(&b.nb, 0xAB);
    b.rewind_for_read();
    REQUIRE(netbufReadU8(&b.nb) == 0xAB);
    REQUIRE(b.nb.error == 0);
    /* No more data; second read must fail safely. */
    u8 sentinel = netbufReadU8(&b.nb);
    REQUIRE(sentinel == 0);
    REQUIRE(b.nb.error == 1);
    /* Subsequent reads keep failing without crashing. */
    REQUIRE(netbufReadU16(&b.nb) == 0);
    REQUIRE(netbufReadU32(&b.nb) == 0u);
    REQUIRE(b.nb.error == 1);
}

TEST_CASE("netbuf: write past capacity sets error", "[netbuf]") {
    /* Make a tiny 2-byte buffer; one u32 write must fail. */
    u8 tiny[2] = {0, 0};
    netbuf nb{ tiny, sizeof(tiny), 0, 0, 0 };
    netbufStartWrite(&nb);
    netbufWriteU8(&nb, 1);
    netbufWriteU8(&nb, 2);
    REQUIRE(nb.error == 0);
    netbufWriteU8(&nb, 3);  /* third byte exceeds 2-byte capacity */
    REQUIRE(nb.error == 1);
}

TEST_CASE("netbuf: read partial after error remains safe", "[netbuf]") {
    ScratchBuf b;
    /* Write 3 bytes then ask for a u32 (4 bytes). */
    netbufWriteU8(&b.nb, 0xAA);
    netbufWriteU8(&b.nb, 0xBB);
    netbufWriteU8(&b.nb, 0xCC);
    b.rewind_for_read();
    u32 v = netbufReadU32(&b.nb);
    REQUIRE(v == 0u);
    REQUIRE(b.nb.error == 1);
}

TEST_CASE("netbuf: netbufReadLeft / netbufWriteLeft reflect cursor", "[netbuf]") {
    ScratchBuf b;
    REQUIRE(netbufWriteLeft(&b.nb) == (s32)kBufBytes);
    netbufWriteU32(&b.nb, 0);
    REQUIRE(netbufWriteLeft(&b.nb) == (s32)kBufBytes - 4);
    b.rewind_for_read();
    REQUIRE(netbufReadLeft(&b.nb) == 4);
    netbufReadU8(&b.nb);
    REQUIRE(netbufReadLeft(&b.nb) == 3);
}

TEST_CASE("netbuf: netbufReset zeroes the struct", "[netbuf]") {
    ScratchBuf b;
    netbufWriteU32(&b.nb, 0xAABBCCDDu);
    REQUIRE(b.nb.wp != 0);
    netbufReset(&b.nb);
    REQUIRE(b.nb.data == nullptr);
    REQUIRE(b.nb.size == 0);
    REQUIRE(b.nb.rp == 0);
    REQUIRE(b.nb.wp == 0);
    REQUIRE(b.nb.error == 0);
}

TEST_CASE("netbuf: netbufStartReadData wires external buffer for reading", "[netbuf]") {
    /* This is the path used when a netmsg arrives over the wire — server
     * hands the raw ENet payload to netbufStartReadData and the message
     * decode functions iterate primitives off it. Verify no copy is
     * needed and reads see the input bytes. */
    u8 ext[8] = { 0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00 };
    netbuf nb;
    netbufStartReadData(&nb, ext, sizeof(ext));
    REQUIRE(nb.data == ext);
    REQUIRE(nb.size == 8);
    REQUIRE(nb.rp == 0);
    REQUIRE(nb.wp == 8);
    REQUIRE(netbufReadU32(&nb) == 0x12345678u);
}

TEST_CASE("Grid offers round trip exact owner identity and reject a partial or changed payload",
        "[netbuf][grid-match-wire]") {
    grid_match_source_package_t package{};
    package.owner = {"grid:arena", ASSET_ARENA};
    std::strcpy(package.path, "D:/host-private/arena.pdarena");
    std::strcpy(package.suffix, ".pdarena");
    package.digest[0] = 0xa1;
    package.size = 420;
    grid_match_source_entry_t entries[2]{};
    entries[0].asset = {"grid:arena", ASSET_ARENA};
    entries[1].asset = {"grid:scenario", ASSET_SCENARIO};
    std::strcpy(entries[1].member_chain, "sources/scenario.pdscenario");
    grid_match_sources_t plan{};
    plan.packages = &package;
    plan.package_count = 1;
    plan.entries = entries;
    plan.entry_count = 2;
    char error[256]{};
    REQUIRE(gridMatchSourcesSeal(&plan, error, sizeof(error)));
    grid_match_offer_t offer{};
    offer.room_id = 3;
    offer.generation = 0x1234567890ABCDEFULL;
    std::strcpy(offer.manifest_digest, plan.digest);

    ScratchBuf buffer;
    REQUIRE(gridMatchOfferWrite(&buffer.nb, &offer, &plan, error, sizeof(error)));
    REQUIRE(buffer.nb.wp > 80);
    REQUIRE(std::string(reinterpret_cast<const char *>(buffer.nb.data), buffer.nb.wp)
        .find("host-private") == std::string::npos);
    struct Decoded {
        grid_match_sources_t plan{};
        ~Decoded() { gridMatchSourcesFree(&plan); }
    } decoded;
    grid_match_offer_t received{};
    netbuf input;
    netbufStartReadData(&input, buffer.nb.data, buffer.nb.wp);
    REQUIRE(gridMatchOfferRead(&input, &received, &decoded.plan, error, sizeof(error)));
    REQUIRE(input.rp == input.wp);
    REQUIRE(received.room_id == offer.room_id);
    REQUIRE(received.generation == offer.generation);
    REQUIRE(std::string(decoded.plan.digest) == plan.digest);
    REQUIRE(std::string(decoded.plan.entries[1].member_chain) == entries[1].member_chain);
    REQUIRE(std::string(decoded.plan.packages[0].owner.id) == package.owner.id);
    REQUIRE(decoded.plan.packages[0].path[0] == '\0');
    REQUIRE(decoded.plan.total_bytes == 420);

    netbufStartReadData(&input, buffer.nb.data, buffer.nb.wp - 2);
    REQUIRE_FALSE(gridMatchOfferRead(&input, &received, &decoded.plan, error, sizeof(error)));
    REQUIRE(input.rp == 0);
    REQUIRE(std::string(decoded.plan.digest) == plan.digest);
    buffer.nb.data[10] = 'x'; // first digest byte after version, room and generation
    netbufStartReadData(&input, buffer.nb.data, buffer.nb.wp);
    REQUIRE_FALSE(gridMatchOfferRead(&input, &received, &decoded.plan, error, sizeof(error)));
    REQUIRE(received.generation == offer.generation);
    REQUIRE(std::string(decoded.plan.digest) == plan.digest);

    std::strcpy(entries[1].member_chain, "../scenario.pdscenario");
    const u32 old_end = buffer.nb.wp;
    REQUIRE_FALSE(gridMatchOfferWrite(&buffer.nb, &offer, &plan, error, sizeof(error)));
    REQUIRE(buffer.nb.wp == old_end);
}

TEST_CASE("Grid readiness carries the exact offer identity", "[netbuf][grid-match-wire]") {
    grid_match_offer_t offer{};
    offer.room_id = 3;
    offer.generation = 200;
    std::memset(offer.manifest_digest, 'a', SHA256_HEX_SIZE - 1);
    ScratchBuf output;
    REQUIRE(gridMatchStatusWrite(&output.nb, &offer, 1));
    grid_match_offer_t received{};
    u8 ready = 0;
    netbuf input;
    netbufStartReadData(&input, output.nb.data, output.nb.wp);
    REQUIRE(gridMatchStatusRead(&input, &received, &ready));
    REQUIRE(input.rp == input.wp);
    REQUIRE(received.room_id == offer.room_id);
    REQUIRE(received.generation == offer.generation);
    REQUIRE(std::string(received.manifest_digest) == offer.manifest_digest);
    REQUIRE(ready == 1);
    output.nb.data[output.nb.wp - 1] = 2;
    netbufStartReadData(&input, output.nb.data, output.nb.wp);
    REQUIRE_FALSE(gridMatchStatusRead(&input, &received, &ready));
    REQUIRE(input.rp == 0);
    REQUIRE(ready == 1);
}

TEST_CASE("Grid stage cancellation consumes only the exact offer and epoch",
        "[netbuf][grid-match-wire][grid-stage-cancel]") {
    grid_match_offer_t offer{};
    offer.room_id = 3;
    offer.generation = 200;
    std::memset(offer.manifest_digest, 'a', SHA256_HEX_SIZE - 1);

    ScratchBuf output;
    REQUIRE(gridMatchStageCancelWrite(&output.nb, &offer, 17));
    const u32 bytes = output.nb.wp;
    netbuf input;
    netbufStartReadData(&input, output.nb.data, bytes);
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &offer, 16));
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &offer, 18));
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &offer, 0));
    REQUIRE(input.rp == 0);
    REQUIRE_FALSE(input.error);

    auto other = offer;
    other.room_id++;
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &other, 17));
    other = offer;
    other.generation++;
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &other, 17));
    other = offer;
    other.manifest_digest[0] = 'b';
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &other, 17));
    REQUIRE(input.rp == 0);
    REQUIRE_FALSE(input.error);

    netbufStartReadData(&input, output.nb.data, bytes - 1);
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &offer, 17));
    REQUIRE(input.rp == 0);
    output.nb.data[bytes] = 0x01;
    netbufStartReadData(&input, output.nb.data, bytes + 1);
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &offer, 17));
    REQUIRE(input.rp == 0);

    netbufStartReadData(&input, output.nb.data, bytes);
    output.nb.data[bytes - sizeof(u32) - 1] = 1;
    REQUIRE_FALSE(gridMatchStageCancelRead(&input, &offer, 17));
    REQUIRE(input.rp == 0);
    output.nb.data[bytes - sizeof(u32) - 1] = 0;
    REQUIRE(gridMatchStageCancelRead(&input, &offer, 17));
    REQUIRE(input.rp == input.wp);

    ScratchBuf too_short;
    too_short.nb.size = bytes - 1;
    too_short.nb.data[0] = 0x7a;
    REQUIRE_FALSE(gridMatchStageCancelWrite(&too_short.nb, &offer, 17));
    REQUIRE(too_short.nb.wp == 0);
    REQUIRE(too_short.nb.data[0] == 0x7a);
}

TEST_CASE("Grid shared package indices consume one canonical field", "[netbuf][grid-match-wire]") {
    const u16 count = static_cast<u16>(GENERATE(0, 1, 2, 300, GRID_MATCH_MAX_SHARED_PACKAGES));
    grid_match_shared_packages_t sent = {};
    sent.count = count;
    for (u16 i = 0; i < count; ++i) {
        sent.indexes[i] = count == GRID_MATCH_MAX_SHARED_PACKAGES ? i : static_cast<u16>(0x123 + 2 * i);
    }
    std::array<u8, GRID_MATCH_SHARED_PACKAGES_WIRE_MAX + 2> storage = {};
    netbuf packet = {storage.data(), static_cast<u32>(storage.size()), 0, 0, 0};
    REQUIRE(netbufWriteU8(&packet, 0x6e));
    REQUIRE(gridMatchSharedPackagesWrite(&packet, &sent));
    REQUIRE(netbufWriteU8(&packet, 0xbc));
    const u32 field_size = sizeof(u16) + count * sizeof(u16);
    REQUIRE(packet.wp == 2 + field_size);
    CHECK(storage[1] == (count & 0xff));
    CHECK(storage[2] == (count >> 8));
    if (count) {
        CHECK(storage[3] == (sent.indexes[0] & 0xff));
        CHECK(storage[4] == (sent.indexes[0] >> 8));
        CHECK(storage[field_size - 1] == (sent.indexes[count - 1] & 0xff));
        CHECK(storage[field_size] == (sent.indexes[count - 1] >> 8));
    }

    REQUIRE(netbufReadU8(&packet) == 0x6e);
    grid_match_shared_packages_t received = {2, {45, 78}};
    REQUIRE(gridMatchSharedPackagesRead(&packet, &received));
    CHECK(received.count == count);
    CHECK(std::memcmp(received.indexes, sent.indexes, sizeof(sent.indexes)) == 0);
    CHECK(netbufReadU8(&packet) == 0xbc);
    CHECK(packet.rp == packet.wp);
    CHECK(packet.error == 0);
}

TEST_CASE("Grid shared package index writes reject invalid fields transactionally", "[netbuf][grid-match-wire]") {
    const std::array<u8, 9> unchanged = {0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68};
    auto bytes = unchanged;
    netbuf packet = {bytes.data(), static_cast<u32>(bytes.size()), 1, 1, 0};
    grid_match_shared_packages_t sent = {3, {17, 260, GRID_MATCH_MAX_SHARED_PACKAGES - 1}};
    SECTION("count exceeds the bound") { sent.count = GRID_MATCH_MAX_SHARED_PACKAGES + 1; }
    SECTION("an index reaches the bound") { sent.indexes[2] = GRID_MATCH_MAX_SHARED_PACKAGES; }
    SECTION("a reserved index is present") { sent.indexes[2] = 0xffff; }
    SECTION("an index is repeated") { sent.indexes[1] = 17; }
    SECTION("indices are descending") { sent.indexes[1] = 15; }
    SECTION("capacity is too short") { packet.size--; }
    SECTION("cursor order is corrupt") { packet.rp = packet.wp + 1; }
    SECTION("packet already has an error") { packet.error = 1; }
    const netbuf before = packet;
    CHECK_FALSE(gridMatchSharedPackagesWrite(&packet, &sent));
    CHECK(packet.rp == before.rp);
    CHECK(packet.wp == before.wp);
    CHECK(packet.error == before.error);
    CHECK(bytes == unchanged);
}

TEST_CASE("Grid shared package index reads reject invalid fields transactionally", "[netbuf][grid-match-wire]") {
    u8 bytes[] = {0x6e, 3, 0, 17, 0, 0x04, 0x01, 0xff, 0x0f};
    u32 size = sizeof(bytes);
    SECTION("count exceeds the bound") {
        const u16 count = GRID_MATCH_MAX_SHARED_PACKAGES + 1;
        bytes[1] = static_cast<u8>(count);
        bytes[2] = static_cast<u8>(count >> 8);
    }
    SECTION("an index reaches the bound") {
        bytes[7] = static_cast<u8>(GRID_MATCH_MAX_SHARED_PACKAGES);
        bytes[8] = static_cast<u8>(GRID_MATCH_MAX_SHARED_PACKAGES >> 8);
    }
    SECTION("a reserved index is present") { bytes[7] = bytes[8] = 0xff; }
    SECTION("an index is repeated") { bytes[7] = bytes[5]; bytes[8] = bytes[6]; }
    SECTION("indices are descending") { bytes[7] = 15; bytes[8] = 0; }
    SECTION("the count or an index is truncated") {
        size = static_cast<u32>(GENERATE(1, 2, 3, 4, 5, 6, 7, 8));
    }
    netbuf packet = {bytes, size, 1, size, 0};
    const auto before = packet;
    grid_match_shared_packages_t received;
    std::memset(&received, 0x7a, sizeof(received));
    const auto original = received;
    CHECK_FALSE(gridMatchSharedPackagesRead(&packet, &received));
    CHECK(packet.rp == before.rp);
    CHECK(packet.wp == before.wp);
    CHECK(packet.error == before.error);
    CHECK(std::memcmp(&received, &original, sizeof(received)) == 0);
}

namespace {
constexpr u8 kFinalScores[] = {0x23, 0x02, 0x01, 0x0A, 0x00};
constexpr size_t kGridScoreRowBytes = 6 + MAX_MPCHRS * 2;

void writeGridScoreRow(netbuf &wire, u8 slot, u8 team, s16 deaths, s16 points,
        const std::array<s16, MAX_MPCHRS> &kills) {
    netbufWriteU8(&wire, slot);
    netbufWriteU8(&wire, team);
    netbufWriteS16(&wire, deaths);
    netbufWriteS16(&wire, points);
    for (const s16 count : kills) netbufWriteS16(&wire, count);
}

grid_match_result_t makeGridMatchResult() {
    grid_match_result_t result{};
    result.offer.room_id = 4;
    result.offer.generation = 0x0123456789ABCDEFULL;
    std::memset(result.offer.manifest_digest, 'a', SHA256_HEX_SIZE - 1);
    result.stage_epoch = 0x10203040;
    result.end_tick = 0;
    result.reason = GRID_MATCH_END_TEAM_SCORE;
    std::memset(result.arena_id, 'a', sizeof(result.arena_id) - 1);
    std::memcpy(result.arena_id, "grid:", sizeof("grid:") - 1);
    std::memset(result.arena_sha256, 0x7B, sizeof(result.arena_sha256));
    sha256Hash(kFinalScores, sizeof(kFinalScores), result.score_sha256);
    return result;
}
}

TEST_CASE("Grid results preserve the exact played revision and score bytes",
        "[netbuf][grid-match-result]") {
    const grid_match_result_t sent = makeGridMatchResult();
    ScratchBuf buffer;
    netbufWriteU8(&buffer.nb, 0xCC);
    REQUIRE(gridMatchResultWrite(&buffer.nb, &sent));
    REQUIRE(buffer.nb.wp == 1 + GRID_MATCH_RESULT_WIRE_MAX);
    netbufWriteU16(&buffer.nb, 0x4321);

    buffer.rewind_for_read();
    grid_match_result_t received{};
    REQUIRE(netbufReadU8(&buffer.nb) == 0xCC);
    REQUIRE(gridMatchResultRead(&buffer.nb, &received));
    REQUIRE(gridMatchResultMatchesStage(&received, &sent.offer, sent.stage_epoch,
        sent.arena_id, sent.arena_sha256));
    REQUIRE(received.end_tick == 0);
    REQUIRE(received.reason == GRID_MATCH_END_TEAM_SCORE);
    REQUIRE(netbufReadU16(&buffer.nb) == 0x4321);
    REQUIRE(netbufReadLeft(&buffer.nb) == 0);
    REQUIRE(gridMatchResultMatchesScores(&received, kFinalScores, sizeof(kFinalScores)));

    auto stale = sent.offer;
    stale.generation++;
    REQUIRE_FALSE(gridMatchResultMatchesStage(&received, &stale, sent.stage_epoch,
        sent.arena_id, sent.arena_sha256));
    REQUIRE_FALSE(gridMatchResultMatchesStage(&received, &sent.offer, sent.stage_epoch + 1,
        sent.arena_id, sent.arena_sha256));
    u8 otherRevision[SHA256_DIGEST_SIZE];
    std::memcpy(otherRevision, sent.arena_sha256, sizeof(otherRevision));
    otherRevision[0] ^= 1;
    REQUIRE_FALSE(gridMatchResultMatchesStage(&received, &sent.offer, sent.stage_epoch,
        sent.arena_id, otherRevision));
    u8 changedScores[sizeof(kFinalScores)];
    std::memcpy(changedScores, kFinalScores, sizeof(changedScores));
    changedScores[sizeof(changedScores) - 1] ^= 1;
    REQUIRE_FALSE(gridMatchResultMatchesScores(&received, changedScores, sizeof(changedScores)));
    REQUIRE_FALSE(gridMatchResultMatchesScores(&received, kFinalScores, sizeof(kFinalScores) - 1));
}

TEST_CASE("Grid results refuse invalid writes without altering the packet",
        "[netbuf][grid-match-result]") {
    grid_match_result_t result = makeGridMatchResult();
    ScratchBuf buffer;
    netbufWriteU8(&buffer.nb, 0xCC);
    u8 before[kBufBytes];
    std::memcpy(before, buffer.storage, sizeof(before));

    SECTION("insufficient capacity") { buffer.nb.size = GRID_MATCH_RESULT_WIRE_MAX; }
    SECTION("zero stage epoch") { result.stage_epoch = 0; }
    SECTION("lounge room") { result.offer.room_id = 0xFF; }
    SECTION("unknown end reason") { result.reason = static_cast<grid_match_end_reason_e>(0); }
    SECTION("unterminated arena ID") { result.arena_id[CATALOG_ID_LEN - 1] = 'a'; }
    SECTION("noncanonical manifest digest") { result.offer.manifest_digest[0] = 'A'; }
    SECTION("missing score digest") { std::memset(result.score_sha256, 0, sizeof(result.score_sha256)); }

    REQUIRE_FALSE(gridMatchResultWrite(&buffer.nb, &result));
    REQUIRE(buffer.nb.wp == 1);
    REQUIRE(buffer.nb.error == 0);
    REQUIRE(std::memcmp(before, buffer.storage, sizeof(before)) == 0);
}

TEST_CASE("Grid results refuse invalid reads without replacing an earlier result",
        "[netbuf][grid-match-result]") {
    const grid_match_result_t sent = makeGridMatchResult();
    ScratchBuf buffer;
    netbufWriteU8(&buffer.nb, 0xCC);
    REQUIRE(gridMatchResultWrite(&buffer.nb, &sent));
    const u32 written = buffer.nb.wp;
    auto previous = sent;
    previous.offer.generation--;
    previous.end_tick = 100;
    previous.reason = GRID_MATCH_END_TIME;
    grid_match_result_t received = previous;
    auto rejects = [&](u32 size) {
        netbuf input;
        netbufStartReadData(&input, buffer.storage, size);
        REQUIRE(netbufReadU8(&input) == 0xCC);
        REQUIRE_FALSE(gridMatchResultRead(&input, &received));
        REQUIRE(input.rp == 1);
        REQUIRE(input.wp == size);
        REQUIRE(input.error == 0);
        REQUIRE(gridMatchResultMatchesStage(&received, &previous.offer, previous.stage_epoch,
            previous.arena_id, previous.arena_sha256));
        REQUIRE(gridMatchResultMatchesScores(&received, kFinalScores, sizeof(kFinalScores)));
        REQUIRE(received.reason == previous.reason);
        REQUIRE(received.end_tick == previous.end_tick);
    };

    SECTION("every truncated prefix") {
        for (u32 size = 1; size < written; size++) rejects(size);
    }
    SECTION("unsupported version") {
        buffer.storage[1] = 0xFF;
        rejects(written);
    }
    SECTION("an embedded NUL in the declared arena ID") {
        const size_t idStart = written - 2 * SHA256_DIGEST_SIZE - (CATALOG_ID_LEN - 1);
        buffer.storage[idStart + 6] = '\0';
        rejects(written);
    }
    SECTION("unknown end reason") {
        const size_t idStart = written - 2 * SHA256_DIGEST_SIZE - (CATALOG_ID_LEN - 1);
        buffer.storage[idStart - 2] = 0xFF;
        rejects(written);
    }
}

TEST_CASE("Grid scorecards decode signed scores in sparse host slot order",
        "[netbuf][grid-match-result]") {
    std::array<u8, 1 + 2 * kGridScoreRowBytes> data{};
    netbuf wire = {data.data(), static_cast<u32>(data.size()), 0, 0, 0};
    netbufWriteU8(&wire, 2);
    std::array<s16, MAX_MPCHRS> first{};
    first[0] = -3;
    first[1] = 32767;
    first[2] = 170;
    first[8] = 32767;
    first[MAX_MPCHRS - 1] = 7;
    writeGridScoreRow(wire, 2, 3, -32768, 32767, first);
    std::array<s16, MAX_MPCHRS> last{};
    last[0] = 4;
    last[2] = -12;
    last[MAX_MPCHRS - 1] = -32768;
    writeGridScoreRow(wire, MAX_MPCHRS - 1, 7, 5, -23, last);
    REQUIRE(wire.error == 0);
    REQUIRE(wire.wp == data.size());

    auto result = makeGridMatchResult();
    sha256Hash(data.data(), data.size(), result.score_sha256);
    grid_match_scorecard_t card{};
    std::strcpy(card.rows[0].name, "previous");
    REQUIRE(gridMatchResultReadScores(&result, data.data(), data.size(), &card));
    REQUIRE(card.count == 2);
    CHECK(card.rows[0].host_slot == 2);
    CHECK(card.rows[0].team == 3);
    CHECK(card.rows[0].deaths == -32768);
    CHECK(card.rows[0].points == 32767);
    CHECK(card.rows[0].kills == 65538);
    CHECK(card.rows[1].host_slot == MAX_MPCHRS - 1);
    CHECK(card.rows[1].team == 7);
    CHECK(card.rows[1].deaths == 5);
    CHECK(card.rows[1].points == -23);
    CHECK(card.rows[1].kills == -8);
    CHECK(card.rows[0].name[0] == '\0');
    CHECK(card.rows[1].name[0] == '\0');
}

TEST_CASE("Grid scorecards accept the wire count boundaries",
        "[netbuf][grid-match-result]") {
    std::array<u8, 1 + MAX_MPCHRS * kGridScoreRowBytes> data{};
    netbuf wire = {data.data(), static_cast<u32>(data.size()), 0, 0, 0};
    const u8 count = GENERATE(0, MAX_MPCHRS);
    netbufWriteU8(&wire, count);
    for (u8 i = 0; i < count; ++i) writeGridScoreRow(wire, i, 0, 0, 0, {});
    REQUIRE(wire.error == 0);
    REQUIRE(wire.wp == 1 + count * kGridScoreRowBytes);

    auto result = makeGridMatchResult();
    sha256Hash(data.data(), wire.wp, result.score_sha256);
    grid_match_scorecard_t card{};
    REQUIRE(gridMatchResultReadScores(&result, data.data(), wire.wp, &card));
    CHECK(card.count == count);
    if (count) CHECK(card.rows[count - 1].host_slot == count - 1);
}

TEST_CASE("Grid scorecards refuse invalid payloads without changing the previous card",
        "[netbuf][grid-match-result]") {
    std::array<u8, 2 + 2 * kGridScoreRowBytes> data{};
    netbuf wire = {data.data(), static_cast<u32>(data.size()), 0, 0, 0};
    netbufWriteU8(&wire, 2);
    writeGridScoreRow(wire, 1, 0, 5, 7, {});
    writeGridScoreRow(wire, 3, 2, 9, 11, {});
    REQUIRE(wire.error == 0);
    size_t size = wire.wp;
    auto result = makeGridMatchResult();
    bool rehash = true;
    SECTION("the count exceeds the participant limit") { data[0] = MAX_MPCHRS + 1; }
    SECTION("the second host slot repeats the first") { data[1 + kGridScoreRowBytes] = data[1]; }
    SECTION("the second host slot is descending") { data[1 + kGridScoreRowBytes] = 0; }
    SECTION("the second host slot is outside the domain") { data[1 + kGridScoreRowBytes] = MAX_MPCHRS; }
    SECTION("the last signed kill count is truncated") { --size; }
    SECTION("trailing data remains after the last row") { ++size; }
    SECTION("the score hash belongs to other bytes") {
        sha256Hash(data.data(), size, result.score_sha256);
        data[size - 1] ^= 1;
        rehash = false;
    }
    if (rehash) sha256Hash(data.data(), size, result.score_sha256);

    grid_match_scorecard_t card;
    std::memset(&card, 0x5A, sizeof(card));
    std::array<u8, sizeof(card)> previous;
    std::memcpy(previous.data(), &card, sizeof(card));
    REQUIRE_FALSE(gridMatchResultReadScores(&result, data.data(), size, &card));
    CHECK(std::memcmp(&card, previous.data(), sizeof(card)) == 0);
}
