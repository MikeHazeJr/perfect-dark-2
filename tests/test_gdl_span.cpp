#include "catch.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

extern "C" {
#include "gdl_span.h"
}

/* gSP1Triangle needs _SHIFTL from PR/mbi.h, which also pulls in os_libc.h
 * and clashes with the C++ headers here; this is mbi.h's definition. */
#ifndef _SHIFTL
#define _SHIFTL(v, s, w) \
	((unsigned int) (((unsigned int)(v) & ((0x01 << (w)) - 1)) << (s)))
#endif

TEST_CASE("G_TRI1 and G_TRI4 vertex indices decode through the Gfx union on this build",
	"[render][gbi][B-1212]")
{
	/* What the character and room hit tests, the world mesh, the x-ray room
	 * renderer and texLoadFromGdl read. B-1212: Gtri had no 64-bit padding,
	 * so every G_TRI1 index read the zero upper half of w0. Generated models
	 * draw with G_TRI1 only, so no triangle of theirs could be hit. */
	Gfx one = {};
	gSP1Triangle(&one, 3, 7, 12, 0);
	REQUIRE(one.tri.tri.v[GFX_TRI_VTX(0)] / 10 == 3);
	REQUIRE(one.tri.tri.v[GFX_TRI_VTX(1)] / 10 == 7);
	REQUIRE(one.tri.tri.v[GFX_TRI_VTX(2)] / 10 == 12);

	const auto *base = reinterpret_cast<const unsigned char *>(&one);
	REQUIRE(reinterpret_cast<const unsigned char *>(&one.tri.tri) - base ==
		reinterpret_cast<const unsigned char *>(&one.words.w1) - base);

	/* G_TRI4 words as gSPTri4 (src/include/gbiex.h) packs them: z in w0,
	 * x and y in w1. Gtri4 was already padded; it must agree. */
	Gfx four = {};
	four.words.w0 = ((u32)G_TRI4 & 0xffu) << 24 | 12u << 12 | 9u << 8 | 6u << 4 | 3u;
	four.words.w1 = 11u << 28 | 10u << 24 | 8u << 20 | 7u << 16 | 5u << 12 | 4u << 8 | 2u << 4 | 1u;
	REQUIRE(four.tri4.x1 == 1);
	REQUIRE(four.tri4.y1 == 2);
	REQUIRE(four.tri4.z1 == 3);
	REQUIRE(four.tri4.x2 == 4);
	REQUIRE(four.tri4.y2 == 5);
	REQUIRE(four.tri4.z2 == 6);
	REQUIRE(four.tri4.x3 == 7);
	REQUIRE(four.tri4.y3 == 8);
	REQUIRE(four.tri4.z3 == 9);
	REQUIRE(four.tri4.x4 == 10);
	REQUIRE(four.tri4.y4 == 11);
	REQUIRE(four.tri4.z4 == 12);
}

TEST_CASE("GDL span reports exact aligned bytes remaining", "[render][gdl-span][B-1080]")
{
	Gfx commands[4] = {};
	u32 remaining = 0xdeadbeefu;

	REQUIRE(gdlSpanBytesRemaining(commands, sizeof(commands), commands,
		&remaining) == 1);
	REQUIRE(remaining == sizeof(commands));

	REQUIRE(gdlSpanBytesRemaining(commands, sizeof(commands), &commands[2],
		&remaining) == 1);
	REQUIRE(remaining == 2 * sizeof(Gfx));

	REQUIRE(gdlSpanBytesRemaining(commands, sizeof(commands), &commands[4],
		&remaining) == 0);
	REQUIRE(remaining == 0);

	const auto *misaligned = reinterpret_cast<const std::uint8_t *>(commands) + 1;
	remaining = 123;
	REQUIRE(gdlSpanBytesRemaining(commands, sizeof(commands), misaligned,
		&remaining) == 0);
	REQUIRE(remaining == 0);

	REQUIRE(gdlSpanBytesRemaining(commands, sizeof(commands) - 1, commands,
		&remaining) == 0);
	REQUIRE(gdlSpanBytesRemaining(nullptr, sizeof(commands), commands,
		&remaining) == 0);
	REQUIRE(gdlSpanBytesRemaining(commands, sizeof(commands), commands,
		nullptr) == 0);
}

TEST_CASE("GDL subspans must fit their owning allocation", "[render][gdl-span][B-1080]")
{
	const std::size_t commandBytes = 2 * sizeof(Gfx);

	REQUIRE(gdlSpanFitsAllocation(64, 16, commandBytes) == 1);
	REQUIRE(gdlSpanFitsAllocation(48, 16, commandBytes) == 1);
	REQUIRE(gdlSpanFitsAllocation(47, 16, commandBytes) == 0);
	REQUIRE(gdlSpanFitsAllocation(64, 65, commandBytes) == 0);
	REQUIRE(gdlSpanFitsAllocation(64, 16, commandBytes - 1) == 0);
	REQUIRE(gdlSpanFitsAllocation(64, 16, 0) == 0);
	REQUIRE(gdlSpanFitsAllocation(
		std::numeric_limits<std::size_t>::max(),
		std::numeric_limits<std::size_t>::max() - sizeof(Gfx) + 1,
		2 * sizeof(Gfx)) == 0);
}

TEST_CASE("GDL span requires the allocation to end with ENDDL", "[render][gdl-span][B-1080]")
{
	Gfx commands[3] = {};

	REQUIRE(gdlSpanEndsWithEnddl(commands, sizeof(commands)) == 0);
	commands[1].bytes[GFX_W0_BYTE(0)] = static_cast<u8>(G_ENDDL);
	REQUIRE(gdlSpanEndsWithEnddl(commands, sizeof(commands)) == 0);
	commands[2].bytes[GFX_W0_BYTE(0)] = static_cast<u8>(G_ENDDL);
	REQUIRE(gdlSpanEndsWithEnddl(commands, sizeof(commands)) == 1);

	REQUIRE(gdlSpanEndsWithEnddl(commands, sizeof(commands) - 1) == 0);
	REQUIRE(gdlSpanEndsWithEnddl(nullptr, sizeof(commands)) == 0);
	REQUIRE(gdlSpanEndsWithEnddl(commands, 0) == 0);
}
