/*
 * tests/test_savebuffer.cpp -- Save format bit-pack primitive tests.
 *
 * Code under test: pure subset of src/game/savebuffer.c via
 * tests/savebuffer_pure.c (a verbatim copy that drops the GBI/VI/Mtx
 * tail of the file). See tests/savebuffer_pure.c header for drift-
 * audit instructions.
 *
 * The bit-pack primitives are the bottom layer of every save file
 * write. mpsetupfileSaveWad / mpsetupfileLoadWad use them to encode
 * MP setup blocks. The pak system uses them for save slots. A drift
 * here breaks every save file silently.
 *
 * Tests:
 *   - bitpos starts at 0; bitpos advances by numbits on each write.
 *   - savebufferOr is a SET-bits operation; it ORs bits into the
 *     buffer (does not clear). A clean buffer is needed for a clean
 *     write.
 *   - savebufferReadBits is the inverse; for a cleared buffer,
 *     write(N, v) followed by reset(bitpos) and read(N) returns v.
 *   - Multi-field roundtrip: alternating widths preserve order.
 *   - Boundary widths: 1, 7, 8, 13, 32, 63 bits.
 *   - 64-bit value roundtrip (the wpnRndPacked path in mpsetupfile).
 *   - Cross-byte-boundary writes (e.g. write 13 bits starting at bit 5
 *     spans 3 bytes).
 */

#include "catch.hpp"
#include "savebuffer_pure.h"

#include <cstring>
#include <cstdint>
#include <vector>

namespace {
using Buf = pdtest_savebuffer;

/* Helper: construct + clear, return Buf. */
Buf make_buf() {
    Buf b;
    pdtest_savebufferClear(&b);
    return b;
}

/* Helper: rewind bitpos for read after writing. */
void rewind_for_read(Buf *b) {
    b->bitpos = 0;
}
} /* anon */

TEST_CASE("savebuffer: clear zeroes bytes and resets bitpos", "[savebuffer]") {
    Buf b;
    /* Set everything to a known non-zero pattern. */
    std::memset(&b, 0xAA, sizeof(b));
    pdtest_savebufferClear(&b);
    REQUIRE(b.bitpos == 0u);
    for (size_t i = 0; i < sizeof(b.bytes); i++) {
        REQUIRE(b.bytes[i] == 0u);
    }
}

TEST_CASE("savebuffer: 1-bit write then read", "[savebuffer]") {
    Buf b = make_buf();
    pdtest_savebufferOr(&b, 1, 1);
    REQUIRE(b.bitpos == 1u);
    REQUIRE(b.bytes[0] == 0x80);
    rewind_for_read(&b);
    REQUIRE(pdtest_savebufferReadBits(&b, 1) == 1u);
    REQUIRE(b.bitpos == 1u);
}

TEST_CASE("savebuffer: 0-bit write leaves bytes zeroed", "[savebuffer]") {
    Buf b = make_buf();
    pdtest_savebufferOr(&b, 0, 1);
    REQUIRE(b.bitpos == 1u);
    REQUIRE(b.bytes[0] == 0x00);
    rewind_for_read(&b);
    REQUIRE(pdtest_savebufferReadBits(&b, 1) == 0u);
}

TEST_CASE("savebuffer: 8-bit byte-aligned roundtrip", "[savebuffer]") {
    Buf b = make_buf();
    pdtest_savebufferOr(&b, 0xA5, 8);
    REQUIRE(b.bitpos == 8u);
    REQUIRE(b.bytes[0] == 0xA5);
    rewind_for_read(&b);
    REQUIRE(pdtest_savebufferReadBits(&b, 8) == 0xA5u);
}

TEST_CASE("savebuffer: cross-byte boundary write", "[savebuffer]") {
    /* Write 13 bits of 0x1F4F starting at bitpos 5 — spans bytes [0,1,2]. */
    Buf b = make_buf();
    pdtest_savebufferOr(&b, 0, 5);   /* skip first 5 bits */
    pdtest_savebufferOr(&b, 0x1F4Fu, 13);
    REQUIRE(b.bitpos == 18u);
    rewind_for_read(&b);
    REQUIRE(pdtest_savebufferReadBits(&b, 5) == 0u);
    REQUIRE(pdtest_savebufferReadBits(&b, 13) == 0x1F4Fu);
    REQUIRE(b.bitpos == 18u);
}

TEST_CASE("savebuffer: multi-field sequence preserves order and widths",
          "[savebuffer]") {
    /* Mirrors a chunk of mpsetupfileSaveWad's actual layout:
     *   4 bits  active bot count
     *   7 bits  stagenum
     *   3 bits  scenario
     *   32 bits options
     */
    Buf b = make_buf();
    pdtest_savebufferOr(&b, 5,            4);    /* active bots */
    pdtest_savebufferOr(&b, 42,           7);    /* stagenum */
    pdtest_savebufferOr(&b, 3,            3);    /* scenario */
    pdtest_savebufferOr(&b, 0x12345678u, 32);    /* options */
    REQUIRE(b.bitpos == (4u + 7u + 3u + 32u));

    rewind_for_read(&b);
    REQUIRE(pdtest_savebufferReadBits(&b, 4)  == 5u);
    REQUIRE(pdtest_savebufferReadBits(&b, 7)  == 42u);
    REQUIRE(pdtest_savebufferReadBits(&b, 3)  == 3u);
    REQUIRE(pdtest_savebufferReadBits(&b, 32) == 0x12345678u);
}

TEST_CASE("savebuffer: 64-bit roundtrip (wpnRndPacked path)", "[savebuffer]") {
    /* mpsetupfileSaveWad/LoadWad write the random-filter mask as 64
     * bits via savebufferOr(64). A regression here would silently corrupt
     * which weapons are eligible for random selection across save loads. */
    Buf b = make_buf();
    const std::uint64_t value = 0xDEADBEEFCAFEBABEull;
    pdtest_savebufferOr(&b, value, 64);
    REQUIRE(b.bitpos == 64u);
    rewind_for_read(&b);
    REQUIRE(pdtest_savebufferReadBits(&b, 64) == value);
}

TEST_CASE("savebuffer: boundary widths roundtrip", "[savebuffer]") {
    struct {
        int width;
        std::uint64_t value;
    } cases[] = {
        {  1, 1 },
        {  7, 127 },                                /* widely-used scenario/headnum/bodynum */
        {  8, 255 },
        { 13, 0x1FFF },                             /* deviceserial in pak guid */
        { 32, 0xFFFFFFFFu },                        /* options (full u32) */
        { 63, 0x7FFFFFFFFFFFFFFFull },              /* one bit shy of u64 */
    };
    for (auto &tc : cases) {
        Buf b = make_buf();
        pdtest_savebufferOr(&b, tc.value, tc.width);
        rewind_for_read(&b);
        std::uint64_t got = pdtest_savebufferReadBits(&b, tc.width);
        INFO("width=" << tc.width << " value=0x" << std::hex << tc.value);
        REQUIRE(got == tc.value);
    }
}

TEST_CASE("savebuffer: independent fields don't interfere",
          "[savebuffer]") {
    /* If two writes accidentally shared state (e.g., reading bytes[i]
     * after a partial write), the second value would appear corrupted.
     * Sweep the alternation pattern across many widths. */
    Buf b = make_buf();
    for (int i = 0; i < 16; i++) {
        pdtest_savebufferOr(&b, (std::uint64_t)i & 0xFu, 4);
    }
    REQUIRE(b.bitpos == 64u);
    rewind_for_read(&b);
    for (int i = 0; i < 16; i++) {
        std::uint64_t got = pdtest_savebufferReadBits(&b, 4);
        REQUIRE(got == (std::uint64_t)(i & 0xF));
    }
}

TEST_CASE("savebuffer: reading without writing returns zeroed value",
          "[savebuffer]") {
    /* The buffer starts cleared. Reading any width returns 0. */
    Buf b = make_buf();
    REQUIRE(pdtest_savebufferReadBits(&b, 8)  == 0u);
    REQUIRE(pdtest_savebufferReadBits(&b, 32) == 0u);
    REQUIRE(b.bitpos == 40u);
}
