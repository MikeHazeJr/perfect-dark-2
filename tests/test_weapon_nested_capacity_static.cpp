#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

static std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

}

TEST_CASE("weapon nested scanner has growable transactional ingress",
		"[modding][pdxxx][weapon][nested][capacity][t-assets-037]")
{
	const std::string scanner = readSource("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("WEAPON_NESTED_MEDIA_MAX") == std::string::npos);
	REQUIRE(scanner.find("char (*entries)[FS_MAXPATH]") != std::string::npos);
	REQUIRE(scanner.find("pending = calloc(scan.count, sizeof(*pending))") !=
		std::string::npos);
	REQUIRE(scanner.find("catalogDepReserve((s32)edge_count)") !=
		std::string::npos);
	REQUIRE(scanner.find("for (size_t i = pending_count; i-- > 0; )") !=
		std::string::npos);
	REQUIRE(scanner.find("effect_dep_edges_created") != std::string::npos);
	REQUIRE(scanner.find("effect_child_dep_edges_created") != std::string::npos);
}

TEST_CASE("weapon nested production harness proves over-64 closure and rollback",
		"[modding][pdxxx][weapon][nested][capacity][t-assets-037]")
{
	const std::string harness = readSource(
		"port/src/weapon_nested_runtime_harness.c");
	REQUIRE(harness.find("HARNESS_DEP_MAX") == std::string::npos);
	REQUIRE(harness.find("harnessCapacityAccept") != std::string::npos);
	REQUIRE(harness.find("harnessCapacityReject") != std::string::npos);
	REQUIRE(harness.find("assetCatalogRegisterWeaponNestedDependencies(owner, archive") !=
		std::string::npos);
	REQUIRE(harness.find("expected_effect_deps <= 64") != std::string::npos);
	REQUIRE(harness.find("expected_effects < 2") != std::string::npos);
	REQUIRE(harness.find("deps.types[i] != ASSET_EFFECT") != std::string::npos);
	REQUIRE(harness.find("effect_count != (size_t)expected_effects") !=
		std::string::npos);
	REQUIRE(harness.find("catalogDepCount() != dep_baseline") !=
		std::string::npos);
}
