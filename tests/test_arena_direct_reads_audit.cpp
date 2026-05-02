/*
 * tests/test_arena_direct_reads_audit.cpp -- Catalog Gate 3 Arenas F13
 * grep-guard.
 *
 * Static-grep test. Pins that no NEW direct `g_MpArenas[<idx>].<field>`
 * reads exist outside the allowed sites. Every consumer either goes
 * through the catalog API (assetCatalogIterateByType + e->ext.arena.X)
 * or through the manager (catalogManagerGetArenaByIndex / By Stagenum
 * / By Id).
 *
 * Allowed sites (these files contain the table definition or
 * registration loop and are intentionally kept):
 *   - src/game/mplayer/setup.c                  (g_MpArenas[] literal definition)
 *   - port/src/server_stubs.c                   (g_MpArenas[] server-stub mirror)
 *   - port/src/assetcatalog_base.c              (registration loop reads
 *                                                g_MpArenas[idx].{stagenum,
 *                                                requirefeature, name})
 *
 * Disallowed sites (these files must contain ZERO `g_MpArenas[`
 * substrings; any new direct read is a regression):
 *   - port/src/modmgr.c
 *   - src/game/challenge.c
 *   - port/fast3d/pdgui_menu_room.cpp
 *   - port/fast3d/pdgui_menu_mainmenu.cpp
 *   - port/fast3d/pdgui_menu_mpsetup.cpp
 *   - port/fast3d/pdgui_bridge.c
 *
 * @SYNC: a new direct g_MpArenas[] read in any of the listed files
 *        is a regression and will fail this test. The 2026-04-26
 *        selector-pool migration eliminated the live-UI consumers;
 *        the 2026-05-02 Gate 3 migration retired the parity bridge.
 */

#include "catch.hpp"

#include <fstream>
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

bool fileContains(const char *path, const char *needle) {
	std::string src = readFile(path);
	return src.find(needle) != std::string::npos;
}

}  /* anonymous namespace */

TEST_CASE("F13: port/src/modmgr.c has no direct g_MpArenas[] reads",
          "[catalog-mgr-arena][gate3][f13]") {
	REQUIRE_FALSE(fileContains("port/src/modmgr.c", "g_MpArenas["));
}

TEST_CASE("F13: src/game/challenge.c has no direct g_MpArenas[] reads",
          "[catalog-mgr-arena][gate3][f13]") {
	REQUIRE_FALSE(fileContains("src/game/challenge.c", "g_MpArenas["));
}

TEST_CASE("F13: port/fast3d/pdgui_menu_room.cpp has no direct g_MpArenas[] reads",
          "[catalog-mgr-arena][gate3][f13]") {
	REQUIRE_FALSE(fileContains("port/fast3d/pdgui_menu_room.cpp", "g_MpArenas["));
}

TEST_CASE("F13: port/fast3d/pdgui_menu_mainmenu.cpp has no direct g_MpArenas[] reads",
          "[catalog-mgr-arena][gate3][f13]") {
	REQUIRE_FALSE(fileContains("port/fast3d/pdgui_menu_mainmenu.cpp", "g_MpArenas["));
}

TEST_CASE("F13: port/fast3d/pdgui_menu_mpsetup.cpp has no direct g_MpArenas[] reads",
          "[catalog-mgr-arena][gate3][f13]") {
	REQUIRE_FALSE(fileContains("port/fast3d/pdgui_menu_mpsetup.cpp", "g_MpArenas["));
}

TEST_CASE("F13: port/fast3d/pdgui_bridge.c has no direct g_MpArenas[] reads",
          "[catalog-mgr-arena][gate3][f13]") {
	REQUIRE_FALSE(fileContains("port/fast3d/pdgui_bridge.c", "g_MpArenas["));
}

/* The allowed sites are pinned positively so a future cleanup that
 * accidentally drops the registration loop or the server stub does
 * not silently land. */

TEST_CASE("F13: src/game/mplayer/setup.c retains g_MpArenas[] table definition",
          "[catalog-mgr-arena][gate3][f13]") {
	std::string src = readFile("src/game/mplayer/setup.c");
	REQUIRE(src.find("struct mparena g_MpArenas[]") != std::string::npos);
}

TEST_CASE("F13: port/src/server_stubs.c retains g_MpArenas[] server stub",
          "[catalog-mgr-arena][gate3][f13]") {
	std::string src = readFile("port/src/server_stubs.c");
	REQUIRE(src.find("struct mparena g_MpArenas[]") != std::string::npos);
}

TEST_CASE("F13: port/src/assetcatalog_base.c retains arena registration loop",
          "[catalog-mgr-arena][gate3][f13]") {
	std::string src = readFile("port/src/assetcatalog_base.c");
	/* Registration reads each arena's three g_MpArenas fields; pin all
	 * three to detect accidental partial rewrites. */
	REQUIRE(src.find("g_MpArenas[idx].stagenum") != std::string::npos);
	REQUIRE(src.find("g_MpArenas[idx].requirefeature") != std::string::npos);
	REQUIRE(src.find("g_MpArenas[idx].name") != std::string::npos);
}

TEST_CASE("F13: port/src/assetcatalog_base.c retains s_ArenaNames + s_ArenaGroupMap",
          "[catalog-mgr-arena][gate3][f13]") {
	std::string src = readFile("port/src/assetcatalog_base.c");
	REQUIRE(src.find("s_ArenaNames[") != std::string::npos);
	REQUIRE(src.find("s_ArenaGroupMap[") != std::string::npos);
}
