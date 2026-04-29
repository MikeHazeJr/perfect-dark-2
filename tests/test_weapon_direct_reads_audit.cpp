/*
 * tests/test_weapon_direct_reads_audit.cpp -- S484 F3+: pin that
 * specific files no longer perform direct g_Weapons[] indexing.
 *
 * Static-grep test. Reads each target source file and asserts the
 * file contains no `g_Weapons[` substring. The migration funnels
 * every weapon-data read through weaponFindById /
 * catalogManagerGetWeaponByIndex so the migration to .pdbase-served
 * data in F11+ is transparent.
 *
 * Files in scope land here as their corresponding F-step commits add
 * them. Tag list grows with the migration.
 *
 * @SYNC: any new direct g_Weapons[] read in any of the listed files
 *        is a regression and will fail this test.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readFile(const char *path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool fileContainsGWeapons(const char *path) {
    std::string src = readFile(path);
    return src.find("g_Weapons[") != std::string::npos;
}

}  /* anonymous namespace */

TEST_CASE("F3: src/game/game_0b0fd0.c has no direct g_Weapons[] reads",
          "[catalog-mgr-weapon][s484][f3]") {
    REQUIRE_FALSE(fileContainsGWeapons("src/game/game_0b0fd0.c"));
}

TEST_CASE("F4: src/game/bondgun.c has no direct g_Weapons[] reads",
          "[catalog-mgr-weapon][s484][f4]") {
    REQUIRE_FALSE(fileContainsGWeapons("src/game/bondgun.c"));
}

TEST_CASE("F5: src/game/bondgunreset.c has no direct g_Weapons[] reads",
          "[catalog-mgr-weapon][s484][f5]") {
    REQUIRE_FALSE(fileContainsGWeapons("src/game/bondgunreset.c"));
}

TEST_CASE("F5: src/game/playerreset.c has no direct g_Weapons[] reads",
          "[catalog-mgr-weapon][s484][f5]") {
    REQUIRE_FALSE(fileContainsGWeapons("src/game/playerreset.c"));
}

TEST_CASE("F6: src/game/modelmgrreset.c has no direct g_Weapons[] reads",
          "[catalog-mgr-weapon][s484][f6]") {
    REQUIRE_FALSE(fileContainsGWeapons("src/game/modelmgrreset.c"));
}
