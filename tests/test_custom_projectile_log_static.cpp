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

// B-1261: a projectile record's `flags` are its weapon function's FUNCFLAG_*
// bits; applying the record never copies them into PROJECTILEFLAG_*.
TEST_CASE("a projectile record's function flags never become projectile flags",
	"[weapon][graph][projectile][static][b1261]")
{
	const std::string propobj = readSource("src/game/propobj.c");
	const size_t apply = propobj.find("void projectileApplyGraphRuntime(struct defaultobj *obj,");
	REQUIRE(apply != std::string::npos);
	const size_t end = propobj.find("\n}\n", apply);
	REQUIRE(end != std::string::npos);
	const std::string body = propobj.substr(apply, end - apply);
	REQUIRE(body.find("runtime->flags") == std::string::npos);
	REQUIRE(body.find("projectile->flags |= PROJECTILEFLAG_HOMING;") != std::string::npos);

	// The extractor still writes the function's flags there, and the held
	// readers still take a record's flags as function flags.
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("u32 flags = f ? f->flags : 0;") != std::string::npos);
	const std::string bgun = readSource("src/game/bondgun.c");
	REQUIRE(bgun.find("u32 funcflags = graph ? graph->flags : func->flags;") != std::string::npos);
}

// projectile-events.md slice 1: every native seam records its event,
// record-only, after the native decision.
TEST_CASE("projectile events are recorded at the native seams",
	"[weapon][graph][projectile][static][projectile-events]")
{
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string bgun = readSource("src/game/bondgun.c");
	const std::string chraction = readSource("src/game/chraction.c");
	const std::string pdmain = readSource("port/src/pdmain.c");
	const std::string harness = readSource("port/src/smoke_harness.c");
	const std::string events = readSource("port/src/projectile_events.c");

	REQUIRE(countOf(propobj, "projectileEventRecord(PROJECTILE_EVENT_ENDED, obj);") == 1);
	REQUIRE(countOf(propobj, "projectileEventRecord(PROJECTILE_EVENT_LANDED, obj);") == 1);
	REQUIRE(countOf(propobj, "projectileEventRecord(PROJECTILE_EVENT_DETONATED, obj);") == 1);
	REQUIRE(countOf(propobj, "projectileEventRecord(PROJECTILE_EVENT_BOUNCED, obj);") == 1);
	REQUIRE(countOf(propobj, "projectileEventRecord(PROJECTILE_EVENT_HIT_TARGET, obj);") == 1);
	REQUIRE(countOf(bgun, "projectileEventRecord(PROJECTILE_EVENT_SPAWNED,") == 3);
	REQUIRE(countOf(chraction, "projectileEventRecord(PROJECTILE_EVENT_SPAWNED,") == 1);
	// The landing records before objLand frees the projectile state.
	const size_t land = propobj.find("void objLand(struct prop *prop,");
	REQUIRE(land != std::string::npos);
	REQUIRE(propobj.find("projectileEventRecord(PROJECTILE_EVENT_LANDED, obj);", land) <
		propobj.find("objFreeProjectile(obj);", land));
	// The host's events only, and the totals at stage end and smoke exit.
	REQUIRE(events.find("g_NetMode == NETMODE_CLIENT") != std::string::npos);
	REQUIRE(pdmain.find("projectileEventsReport(\"stage_end\");") != std::string::npos);
	REQUIRE(harness.find("projectileEventsReport(\"smoke_exit\");") != std::string::npos);
	REQUIRE(harness.find("case SMOKE_EVENT_GIVE_WEAPON:") != std::string::npos);

	// Slice 2: the four fuses ask the rule, and the rule fires the program's
	// timer event, runs the fuse module and counts parity.
	REQUIRE(countOf(propobj, "projectileFuseRule(obj)") == 4);
	REQUIRE(events.find("event.kind = MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED;") != std::string::npos);
	REQUIRE(events.find("strcmp(kind, \"og.projectile.fuse_expired\") == 0") != std::string::npos);
	REQUIRE(events.find("return 1; /* a v1 archive: the native rule decides */") != std::string::npos);
	REQUIRE(events.find(" fuse.compared=%d fuse.differ=%d") != std::string::npos);
}

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
