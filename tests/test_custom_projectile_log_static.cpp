// A custom (graph) projectile leaves one line when it is spawned or fails to
// spawn, and one when it lands or attaches, so a mod author can tell "spawned
// but too small to see" from "never spawned" (MODS ask, week note 2026-11-15).
// Base weapons never log, and both lines are capped per process.
#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

size_t countOf(const std::string &text, const std::string &needle)
{
	size_t count = 0;
	for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
		++count;
	}
	return count;
}

} // namespace

TEST_CASE("custom projectiles log their spawn and their landing", "[weapon][graph][mods][static]")
{
	const std::string bgun = readSource("src/game/bondgun.c");
	const size_t helper = bgun.find("static void bgunLogCustomProjectileSpawn(");
	REQUIRE(helper != std::string::npos);
	const std::string body = bgun.substr(helper, 1800);
	REQUIRE(body.find("if (weaponnum < WEAPON_CUSTOM_START || projectilegraph == NULL") != std::string::npos);
	REQUIRE(body.find("s_BgunCustomProjectileSpawnLogs >= BGUN_CUSTOM_PROJECTILE_LOG_CAP") != std::string::npos);
	REQUIRE(body.find("WEAPON.GRAPH: custom projectile spawned (%s) weapon=%d asset='%s'") != std::string::npos);
	REQUIRE(body.find("WEAPON.GRAPH: custom projectile not spawned (%s) weapon=%d asset='%s'") != std::string::npos);
	// Fired (created and failed) and thrown.
	REQUIRE(countOf(bgun, "bgunLogCustomProjectileSpawn(\"fired\"") == 2);
	REQUIRE(countOf(bgun, "bgunLogCustomProjectileSpawn(\"thrown\"") == 1);
	const size_t thrown = bgun.find("bgunLogCustomProjectileSpawn(\"thrown\"");
	REQUIRE(bgun.find("projectileApplyGraphRuntime(obj, projectilegraph);") < thrown);

	const std::string propobj = readSource("src/game/propobj.c");
	const size_t land = propobj.find("void objLand(struct prop *prop, struct coord *arg1, struct coord *arg2, bool *embedded)");
	REQUIRE(land != std::string::npos);
	const size_t landEnd = propobj.find("\n}\n", land);
	REQUIRE(landEnd != std::string::npos);
	const std::string landBody = propobj.substr(land, landEnd - land);
	REQUIRE(landBody.find("*embedded = true;\n\t\t\t\tattached = true;") != std::string::npos);
	REQUIRE(landBody.find("objLogCustomProjectileLanding(obj, arg1, attached);") != std::string::npos);
	REQUIRE(propobj.find("landgraph = weaponGetCustomProjectileGraph(weapon);") != std::string::npos);
	REQUIRE(propobj.find("WEAPON.GRAPH: custom projectile landed weapon=%d asset='%s' on=%s: %s") != std::string::npos);
	REQUIRE(propobj.find("s_ObjCustomProjectileLandLogs >= OBJ_CUSTOM_PROJECTILE_LAND_LOG_CAP") != std::string::npos);
}
