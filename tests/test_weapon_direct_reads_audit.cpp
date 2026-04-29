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

TEST_CASE("F7: src/game/bot.c has no direct g_AibotWeaponPreferences reads",
          "[catalog-mgr-weapon][s484][f7]") {
    /* All reads route through catalogManagerGetWeaponBotPref. The
     * table definition `g_AibotWeaponPreferences[] = {...}` lives in
     * botinv.c and is the only allowed mention site (until F11+). */
    std::string src = readFile("src/game/bot.c");
    REQUIRE(src.find("g_AibotWeaponPreferences") == std::string::npos);
}

TEST_CASE("F7: src/game/botinv.c has no g_AibotWeaponPreferences reads",
          "[catalog-mgr-weapon][s484][f7]") {
    /* Per F7: only the table definition is allowed in botinv.c.
     * That's the line `struct aibotweaponpreference
     * g_AibotWeaponPreferences[]`; any access pattern
     * `g_AibotWeaponPreferences[<non-empty>]` is migrated. */
    std::string src = readFile("src/game/botinv.c");
    /* No subscripted access to the table. */
    REQUIRE(src.find("g_AibotWeaponPreferences[w") == std::string::npos);
    REQUIRE(src.find("g_AibotWeaponPreferences[c") == std::string::npos);
    REQUIRE(src.find("g_AibotWeaponPreferences[i") == std::string::npos);
    REQUIRE(src.find("g_AibotWeaponPreferences[0") == std::string::npos);
    /* The table definition `g_AibotWeaponPreferences[]` (empty sub)
     * is the sole permitted mention. */
    REQUIRE(src.find("g_AibotWeaponPreferences[]") != std::string::npos);
}
