/*
 * tests/test_updater_rom_cleanup_static.cpp
 *
 * Static pin for B-338: updater stale-file cleanup must never delete the
 * user's BYOR ROM from the install root. Release zips intentionally exclude
 * ROMs, so root-level ROM files must be protected outside the configurable
 * folder list in both updater implementations.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSourceFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} /* anonymous namespace */

TEST_CASE("updater cleanup protects root ROM files in both apply paths",
          "[updater][rom][static][b338]")
{
	const std::string ingame = readSourceFile("port/src/updater.c");
	const std::string standalone = readSourceFile("port/src/updater_standalone/updater_gui.c");
	REQUIRE(!ingame.empty());
	REQUIRE(!standalone.empty());

	const std::string sources[] = { ingame, standalone };
	for (const std::string &src : sources) {
		REQUIRE(src.find("isProtectedRootRomRelPath") != std::string::npos);
		REQUIRE(src.find("strchr(norm, '/') != NULL") != std::string::npos);
		REQUIRE(src.find("strrchr(norm, '.')") != std::string::npos);
		REQUIRE(src.find("strcmp(dot, \".z64\") == 0") != std::string::npos);
		REQUIRE(src.find("strcmp(dot, \".v64\") == 0") != std::string::npos);
		REQUIRE(src.find("strcmp(dot, \".n64\") == 0") != std::string::npos);
		REQUIRE(src.find("if (isProtectedRootRomRelPath(norm))") != std::string::npos);
	}
}

TEST_CASE("standalone updater warns that root ROM files are preserved",
          "[updater][rom][static][b338]")
{
	const std::string standalone = readSourceFile("port/src/updater_standalone/updater_gui.c");
	REQUIRE(!standalone.empty());

	REQUIRE(standalone.find("root ROM files") != std::string::npos);
	REQUIRE(standalone.find("*.z64/*.v64/*.n64") != std::string::npos);
}
