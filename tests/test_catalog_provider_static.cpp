/*
 * tests/test_catalog_provider_static.cpp -- Catalog/provider migration guards.
 *
 * These are intentionally narrow string checks for the call sites migrated in
 * S488. Broader repository-wide bans should wait until every domain has an
 * approved typed API or seed/generator exception.
 */

#include "catch.hpp"
#include <fstream>
#include <sstream>
#include <string>

static std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

TEST_CASE("catalog lifecycle call sites use typed wrappers", "[catalog][provider][static]")
{
	const std::string lv = readTextFile("src/game/lv.c");
	const std::string screenmfst = readTextFile("port/src/screenmfst.c");

	REQUIRE(lv.find("catalogLoadAsset(") == std::string::npos);
	REQUIRE(lv.find("catalogUnloadAsset(") == std::string::npos);
	REQUIRE(screenmfst.find("catalogLoadAsset(") == std::string::npos);
	REQUIRE(screenmfst.find("catalogUnloadAsset(") == std::string::npos);
}

TEST_CASE("modeldef handle loader is not RomProvider-only", "[catalog][provider][static]")
{
	const std::string modeldef = readTextFile("src/game/modeldef.c");

	REQUIRE(modeldef.find("handle.provider != romProvider()") == std::string::npos);
	REQUIRE(modeldef.find("only RomProvider handles can safely use this loader") == std::string::npos);
	REQUIRE(modeldef.find("modeldefPromoteDisplayListsWithSizes") != std::string::npos);
}
