/*
 * tests/test_updater_safety.cpp -- c067 slice: pin the updater's protected-path
 * safety invariants so an auto-update can never delete the user's data.
 *
 * The updater's stale-file cleanup walks the install root and removes anything
 * absent from the new build. isProtectedRelPath() is the ONLY thing standing
 * between that cleanup and a user's saves / mods / ROM / config. Both the
 * in-client updater (updater.c) and the standalone Updater.exe (updater_gui.c)
 * carry their own copy of the protected list, so this static test pins the
 * critical invariants in BOTH -- dropping "saves"/"mods"/ROM/pd.ini protection,
 * or removing the check-before-delete, then fails CI instead of silently wiping a
 * user's data on their next update.
 *
 * This is a static pin (no heavy curl/SDL updater deps to link). Behavioural
 * coverage of the matcher itself would want the c065 compile-boundary extraction
 * of isProtectedRelPath into a globals-free TU -- tracked as future work, kept
 * off the safety-critical live path in a saturated session.
 */
#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {
std::string readFile(const char *p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} /* anon */

TEST_CASE("c067: updater protected-path safety invariants pinned in both updaters",
          "[updater][safety][static][c067]") {
    const std::string u = readFile("port/src/updater.c");
    const std::string g = readFile("port/src/updater_standalone/updater_gui.c");
    REQUIRE_FALSE(u.empty());
    REQUIRE_FALSE(g.empty());

    /* The default protected-folder list must keep the user-data dirs, in BOTH. */
    REQUIRE(u.find("\"mods,data,extracted,saves,logs\"") != std::string::npos);
    REQUIRE(g.find("\"mods,data,extracted,saves,logs\"") != std::string::npos);

    /* pd.ini is always protected, in BOTH. */
    REQUIRE(u.find("\"pd.ini\"") != std::string::npos);
    REQUIRE(g.find("\"pd.ini\"") != std::string::npos);

    /* Root-level ROM files are protected, in BOTH (client via a dedicated helper,
     * standalone via the .z64/.v64/.n64 extension check). */
    REQUIRE(u.find("isProtectedRootRomRelPath") != std::string::npos);
    REQUIRE(g.find(".z64") != std::string::npos);
    REQUIRE(g.find(".v64") != std::string::npos);
    REQUIRE(g.find(".n64") != std::string::npos);

    /* Cleanup MUST consult the protection check before removing, in BOTH. */
    REQUIRE(u.find("isProtectedRelPath(relPath)") != std::string::npos);
    REQUIRE(g.find("isProtectedRelPath(relPath)") != std::string::npos);
}
