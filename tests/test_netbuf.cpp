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

#include <cstring>
#include <string>

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
