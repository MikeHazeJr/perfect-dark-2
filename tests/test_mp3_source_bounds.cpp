#include <limits>
#include <vector>

#include "catch.hpp"

extern "C" {
#include "mp3_source_bounds.h"
}

TEST_CASE("typed public MP3 allocations own one fixed-DMA guard window",
		"[audio][mp3][b1036]")
{
	u32 allocation_size = 77;

	REQUIRE(mp3SourceAllocationSize(46496, &allocation_size) == 1);
	REQUIRE(allocation_size == 46496 + MP3_SOURCE_DMA_WINDOW);

	std::vector<u8> source(allocation_size, 0);
	for (u32 i = 0; i < 46496; ++i) {
		source[i] = static_cast<u8>((i % 251) + 1);
	}
	for (u32 i = 46496; i < allocation_size; ++i) {
		REQUIRE(source[i] == 0);
	}

	allocation_size = 77;
	REQUIRE(mp3SourceAllocationSize(0, &allocation_size) == 0);
	REQUIRE(allocation_size == 77);
	REQUIRE(mp3SourceAllocationSize(1, nullptr) == 0);
	REQUIRE(mp3SourceAllocationSize(
			static_cast<u32>(std::numeric_limits<s32>::max()) + 1u,
			&allocation_size) == 0);
	REQUIRE(allocation_size == 77);
}

TEST_CASE("typed public MP3 reads stop at EOF without signed underflow",
		"[audio][mp3][b1036]")
{
	constexpr s32 size = 46496;

	REQUIRE(mp3SourceClampRead(size, 0, MP3_SOURCE_DMA_WINDOW) == 1024);
	REQUIRE(mp3SourceClampRead(size, 46080, MP3_SOURCE_DMA_WINDOW) == 416);
	REQUIRE(mp3SourceClampRead(size, size - 1, MP3_SOURCE_DMA_WINDOW) == 1);
	REQUIRE(mp3SourceClampRead(size, size, MP3_SOURCE_DMA_WINDOW) == 0);
	REQUIRE(mp3SourceClampRead(size, size + 1, MP3_SOURCE_DMA_WINDOW) == 0);
	REQUIRE(mp3SourceClampRead(size, -1, MP3_SOURCE_DMA_WINDOW) == 0);
	REQUIRE(mp3SourceClampRead(size, 0, -1) == 0);
	REQUIRE(mp3SourceClampRead(-1, 0, MP3_SOURCE_DMA_WINDOW) == 0);
	REQUIRE(mp3SourceClampRead(size, size - 8,
			std::numeric_limits<s32>::max()) == 8);
}
