/*
 * tests/test_weapon_findbyid_migrated.cpp -- S484 F2: pin
 * weaponFindById's migration to the catalog manager.
 *
 * Invariant: src/game/game_0b0fd0.c::weaponFindById delegates to
 * catalogManagerGetWeaponByIndex. The function MUST NOT perform a
 * direct g_Weapons[] indirection. Future regressions that re-introduce
 * the direct array read here would bypass the manager and undo the
 * migration plumbing.
 *
 * Scope: this test inspects ONLY the body of weaponFindById. Other
 * direct g_Weapons[] reads in the same file (covered by F3) are out
 * of scope and will be pinned by a separate static test in F3.
 *
 * @SYNC: changes to game_0b0fd0.c::weaponFindById must be reflected
 *        in this test.
 */

#include "catch.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {

std::string readFile(const char *path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/* Extract weaponFindById's body. Pattern: from "weaponFindById(" up to
 * the matching closing brace at the end of the function. We rely on
 * the function being short and using brace-balanced C. */
std::string extractWeaponFindByIdBody(const std::string &src) {
    /* Find function signature start. */
    auto sig = src.find("struct weapon *weaponFindById(");
    REQUIRE(sig != std::string::npos);

    /* Walk forward to opening brace. */
    auto open = src.find('{', sig);
    REQUIRE(open != std::string::npos);

    /* Brace-balance walk. */
    int depth = 1;
    size_t i = open + 1;
    while (i < src.size() && depth > 0) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') --depth;
        ++i;
    }
    REQUIRE(depth == 0);
    return src.substr(open, i - open);
}

}  /* anonymous namespace */

TEST_CASE("F2: weaponFindById body delegates to catalog manager",
          "[catalog-mgr-weapon][s484][f2]") {
    std::string src = readFile("src/game/game_0b0fd0.c");
    std::string body = extractWeaponFindByIdBody(src);

    /* Must call the manager. */
    REQUIRE(body.find("catalogManagerGetWeaponByIndex") != std::string::npos);
}

TEST_CASE("F2: weaponFindById body has no direct g_Weapons[] read",
          "[catalog-mgr-weapon][s484][f2]") {
    std::string src = readFile("src/game/game_0b0fd0.c");
    std::string body = extractWeaponFindByIdBody(src);

    /* Must not read g_Weapons[ directly. The migration replaces the
     * single-line return g_Weapons[itemid] with a manager call. */
    REQUIRE(body.find("g_Weapons[") == std::string::npos);
}
