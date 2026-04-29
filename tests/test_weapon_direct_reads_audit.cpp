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

TEST_CASE("F8: src/game/game_0b0fd0.c default fallbacks route through manager",
          "[catalog-mgr-weapon][s484][f8]") {
    std::string src = readFile("src/game/game_0b0fd0.c");
    /* Manager-routed defaults must be present. */
    REQUIRE(src.find("catalogManagerWeaponDefaultAimSettings") != std::string::npos);
    REQUIRE(src.find("catalogManagerWeaponDefaultNoiseSettings") != std::string::npos);
    /* Direct extern fallback addresses-of must be gone. */
    REQUIRE(src.find("&invaimsettings_default") == std::string::npos);
    REQUIRE(src.find("&invnoisesettings_silent") == std::string::npos);
}

TEST_CASE("F8 (I.1): currentPlayerSetWeaponPos removed",
          "[catalog-mgr-weapon][s484][f8]") {
    /* Mike's I.1 decision (2026-04-27): the dead-code position-offset
     * mutator is removed. The function definition is gone from
     * game_0b0fd0.c and from the public header. */
    std::string src = readFile("src/game/game_0b0fd0.c");
    REQUIRE(src.find("void currentPlayerSetWeaponPos(") == std::string::npos);
    std::string hdr = readFile("src/include/game/game_0b0fd0.h");
    REQUIRE(hdr.find("currentPlayerSetWeaponPos") == std::string::npos);
}

TEST_CASE("F9 (I.2): ext.weapon shadow fields dropped",
          "[catalog-mgr-weapon][s484][f9]") {
    /* Mike's I.2 decision (2026-04-27): damage / fire_rate / ammo_type
     * fields dropped from asset_entry.ext.weapon. The catalog manager
     * (catalogManagerGetWeaponByIndex(weapon_num)->...) is the single
     * source of truth for those gameplay numbers. */
    std::string hdr = readFile("port/include/assetcatalog.h");
    /* The struct definition no longer carries the legacy fields. The
     * substring is pinned with the type prefix to avoid matching unrelated
     * `damage` field names elsewhere. */
    REQUIRE(hdr.find("f32  damage;") == std::string::npos);
    REQUIRE(hdr.find("f32  fire_rate;") == std::string::npos);
    REQUIRE(hdr.find("s32  ammo_type;") == std::string::npos);
    /* New pdbase_* fields present (F11+ data move scaffold). */
    REQUIRE(hdr.find("pdbase_path[128]") != std::string::npos);
    REQUIRE(hdr.find("pdbase_offset") != std::string::npos);
    REQUIRE(hdr.find("pdbase_size") != std::string::npos);
}

TEST_CASE("F9: assetCatalogRegisterWeapon signature dropped 3 args",
          "[catalog-mgr-weapon][s484][f9]") {
    std::string hdr = readFile("port/include/assetcatalog.h");
    /* Old signature contained `f32 damage, f32 fire_rate, s32 ammo_type`
     * comma-listed. New signature is just `s32 dual_wieldable` after
     * `model_file`. */
    REQUIRE(hdr.find("f32 damage, f32 fire_rate") == std::string::npos);
    REQUIRE(hdr.find("s32 ammo_type, s32 dual_wieldable") == std::string::npos);
}

TEST_CASE("F9: setters for dropped fields removed from scanner + netdistrib",
          "[catalog-mgr-weapon][s484][f9]") {
    std::string sc = readFile("port/src/assetcatalog_scanner.c");
    REQUIRE(sc.find("ext.weapon.damage") == std::string::npos);
    REQUIRE(sc.find("ext.weapon.fire_rate") == std::string::npos);
    REQUIRE(sc.find("ext.weapon.ammo_type") == std::string::npos);

    std::string nd = readFile("port/src/net/netdistrib.c");
    REQUIRE(nd.find("ext.weapon.damage") == std::string::npos);
    REQUIRE(nd.find("ext.weapon.fire_rate") == std::string::npos);
    REQUIRE(nd.find("ext.weapon.ammo_type") == std::string::npos);
}
