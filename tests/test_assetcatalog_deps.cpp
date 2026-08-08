/*
 * tests/test_assetcatalog_deps.cpp -- catalog dependency graph behavior.
 *
 * Public typed archives rely on the dependency graph to keep composite source
 * closure visible during manifest build/load. The graph must scale with the
 * catalog instead of dropping valid deps at the old initial allocation size.
 */

#include "catch.hpp"

#include <cstdio>
#include <string>
#include <vector>

extern "C" {
#include "assetcatalog_deps.h"
}

namespace {
void collectDep(const char *dep_id, void *userdata)
{
	auto *deps = static_cast<std::vector<std::string> *>(userdata);
	deps->push_back(dep_id ? dep_id : "");
}
}

TEST_CASE("catalog dependency graph grows past its initial allocation",
          "[modding][pdxxx][deps][c3844][source][static]")
{
	catalogDepClear();

	for (int i = 0; i < 320; i++) {
		char owner[64];
		char dep[64];
		std::snprintf(owner, sizeof(owner), "mod:weapon_bundle_%03d", i);
		std::snprintf(dep, sizeof(dep), "mod:projectile_dep_%03d", i);
		catalogDepRegister(owner, dep, 0);
	}

	REQUIRE(catalogDepCount() == 320);

	std::vector<std::string> first;
	catalogDepForEach("mod:weapon_bundle_000", collectDep, &first);
	REQUIRE(first.size() == 1);
	REQUIRE(first[0] == "mod:projectile_dep_000");

	std::vector<std::string> last;
	catalogDepForEach("mod:weapon_bundle_319", collectDep, &last);
	REQUIRE(last.size() == 1);
	REQUIRE(last[0] == "mod:projectile_dep_319");

	catalogDepRegister("mod:weapon_bundle_319", "mod:projectile_dep_319", 0);
	REQUIRE(catalogDepCount() == 320);

	catalogDepClear();
}

TEST_CASE("catalog dependency graph reservation does not mutate truth",
		"[modding][pdxxx][pdtheme][deps][T-ASSETS-027]")
{
	catalogDepClear();
	REQUIRE(catalogDepReserve(4) == 1);
	REQUIRE(catalogDepCount() == 0);
	for (int i = 0; i < 4; i++) {
		char dep[64];
		std::snprintf(dep, sizeof(dep), "example:theme_dep_%d", i);
		catalogDepRegister("example:theme", dep, 0);
	}
	REQUIRE(catalogDepCount() == 4);
	catalogDepClear();
}

TEST_CASE("catalog dependency graph skips bundled pairs during manifest expansion",
          "[modding][pdxxx][deps][c3844][source][static]")
{
	catalogDepClear();

	catalogDepRegister("base:weapon_native", "base:projectile_native", 1);
	catalogDepRegister("mod:weapon_custom", "mod:projectile_custom", 0);

	std::vector<std::string> bundled;
	catalogDepForEach("base:weapon_native", collectDep, &bundled);
	REQUIRE(bundled.empty());

	std::vector<std::string> custom;
	catalogDepForEach("mod:weapon_custom", collectDep, &custom);
	REQUIRE(custom.size() == 1);
	REQUIRE(custom[0] == "mod:projectile_custom");

	catalogDepClearMods();
	REQUIRE(catalogDepCount() == 1);

	catalogDepClear();
	REQUIRE(catalogDepCount() == 0);
}
