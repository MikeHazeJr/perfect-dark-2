#include "catch.hpp"

#include <array>
#include <cstring>

extern "C" {
#include "combat_sim_verify.h"
}

TEST_CASE("combat sim kill matrix parses a bounded non-tied award fixture",
		"[combat-sim][awards][parser][b1074]")
{
	std::array<combat_sim_kill_entry_t, 8> entries{};
	size_t count = 0;

	REQUIRE(combatSimKillMatrixParse("0:0=4,0:8=1,8:0=2",
		entries.data(), entries.size(), &count) == COMBAT_SIM_KILL_MATRIX_OK);
	REQUIRE(count == 3);
	CHECK(entries[0].attacker == 0);
	CHECK(entries[0].victim == 0);
	CHECK(entries[0].count == 4);
	CHECK(entries[1].attacker == 0);
	CHECK(entries[1].victim == 8);
	CHECK(entries[1].count == 1);
	CHECK(entries[2].attacker == 8);
	CHECK(entries[2].victim == 0);
	CHECK(entries[2].count == 2);

	REQUIRE(combatSimKillMatrixParse("39:39=1",
		entries.data(), entries.size(), &count) == COMBAT_SIM_KILL_MATRIX_OK);
	REQUIRE(count == 1);
	CHECK(entries[0].attacker == 39);
	CHECK(entries[0].victim == 39);
}

TEST_CASE("combat sim kill matrix rejects malformed or ambiguous input",
		"[combat-sim][awards][parser][b1074]")
{
	std::array<combat_sim_kill_entry_t, 8> entries{};
	size_t count = 77;

	CHECK(combatSimKillMatrixParse("", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_EMPTY);
	CHECK(combatSimKillMatrixParse("0-0=1", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT);
	CHECK(combatSimKillMatrixParse("0:0=1,", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT);
	CHECK(combatSimKillMatrixParse("0:0=1x", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_INVALID_FORMAT);
	CHECK(combatSimKillMatrixParse("40:0=1", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_OUT_OF_RANGE);
	CHECK(combatSimKillMatrixParse("0:40=1", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_OUT_OF_RANGE);
	CHECK(combatSimKillMatrixParse("0:0=0", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_OUT_OF_RANGE);
	CHECK(combatSimKillMatrixParse("0:0=32768", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_OUT_OF_RANGE);
	CHECK(combatSimKillMatrixParse("0:0=1,0:0=2", entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_DUPLICATE_PAIR);
	CHECK(count == 77);
}

TEST_CASE("combat sim kill matrix publishes only after complete validation",
		"[combat-sim][awards][parser][transaction]")
{
	std::array<combat_sim_kill_entry_t, 2> entries{};
	entries[0] = {9, 8, 7};
	entries[1] = {6, 5, 4};
	auto before = entries;
	size_t count = 55;

	CHECK(combatSimKillMatrixParse("0:0=32767,1:0=1",
		entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_DEATH_TOTAL_OVERFLOW);
	CHECK(std::memcmp(entries.data(), before.data(), sizeof(entries)) == 0);
	CHECK(count == 55);

	CHECK(combatSimKillMatrixParse("0:0=1,1:1=1,2:2=1",
		entries.data(), entries.size(), &count)
		== COMBAT_SIM_KILL_MATRIX_TOO_MANY_ENTRIES);
	CHECK(std::memcmp(entries.data(), before.data(), sizeof(entries)) == 0);
	CHECK(count == 55);
}
