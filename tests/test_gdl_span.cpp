#include "catch.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

extern "C" {
#include "gdl_span.h"
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
