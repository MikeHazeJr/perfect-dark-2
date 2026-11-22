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

	// B-1263: a launch binds the object to the archive whose record it
	// applies (a fired rocket, bolt or grenade round carries its own weapon
	// number, not the gun's), and the events and the fuse rule identify a
	// projectile by that binding before its own weapon and function.
	const size_t applyRecord = propobj.find("void projectileApplyGraphRuntime(struct defaultobj *obj,");
	REQUIRE(applyRecord != std::string::npos);
	REQUIRE(propobj.find("\tprojectileEventBind(obj, runtime->asset_id);", applyRecord) <
		propobj.find("\n}\n", applyRecord));
	REQUIRE(countOf(propobj, "projectileEventBind(") == 1);
	// Slice 6: the binding is kept in the prop's own record for as long as
	// the prop lives (slice 1 kept the last 64 launches in a ring).
	REQUIRE(events.find("\tif (record->generation != prop->lifecycle_generation) {") != std::string::npos);
	REQUIRE(events.find("\treturn record && record->bound ? record : NULL;") != std::string::npos);
	REQUIRE(events.find("\tbind = projectileRecordOf(obj->prop, 1);") != std::string::npos);
	REQUIRE(events.find("PROJECTILE_BIND_MAX") == std::string::npos);
	REQUIRE(events.find("strcmp(held->projectile_ref, asset_id) == 0") != std::string::npos);
	// The records, the fuse rule and the contact rules (slice 3).
	REQUIRE(countOf(events, "!projectileIdentify(obj, &who)") == 3);
	// The fuse rule and the other rules each ask for the program of their
	// event: an entity archive's program answers the fuse only.
	REQUIRE(countOf(events, "program = projectileProgramFor(&who, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, &asset_id);") == 1);
	REQUIRE(countOf(events, "program = projectileProgramFor(&who, kind, &asset_id);") == 1);
	REQUIRE(events.find("\tif (!program && who->gunnum >= 0\n"
		"\t\t\t&& event_kind == MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) {") != std::string::npos);
	REQUIRE(events.find("projectileProgramForWeapon") == std::string::npos);
	// An object that is no weapon is recorded only when a launch bound it
	// (the thrown Laptop gun is an autogun object).
	REQUIRE(events.find("\treturn bind != NULL;\n}") != std::string::npos);
	// B-1264: an unbound weapon object is described by its own function's
	// archive only when that function launches the object itself. A rocket
	// launcher, a Devastator or a crossbow lying on the floor is the gun, not
	// the rocket, the round or the bolt: it has no archive and no program,
	// its projectile archive's or its entity archive's.
	REQUIRE(events.find("static s32 projectileFunctionLaunches(s32 gunnum, s32 func, s32 weaponnum, s32 gunfunc)\n{") != std::string::npos);
	REQUIRE(events.find("\tprojectileRulesLaunchedObject(gunnum, func, f->type == INVENTORYFUNCTYPE_THROW,\n"
		"\t\t(f->flags & FUNCFLAG_HOMINGROCKET) != 0, &objweapon, &objfunc);\n"
		"\treturn objweapon == weaponnum && objfunc == gunfunc;") != std::string::npos);
	REQUIRE(events.find("\t\t\tif (projectileUnboundIsItsProjectile(weapon)) {") != std::string::npos);
	REQUIRE(events.find("\treturn projectileFunctionLaunches(weapon->weaponnum, weapon->gunfunc,\n"
		"\t\tweapon->weaponnum, weapon->gunfunc);") != std::string::npos);
	// Slice 6 review: a mod weapon's objects all carry the weapon's number
	// (the gun on a pad and what it fires), so an unbound one is its own
	// projectile only when its function throws the item itself.
	REQUIRE(events.find("\tif (weapon->weaponnum >= WEAPON_CUSTOM_START) {\n"
		"\t\tconst struct weaponfunc *f = weaponGetFunctionById(weapon->weaponnum, weapon->gunfunc);\n\n"
		"\t\treturn f && (f->type & 0xff) == INVENTORYFUNCTYPE_THROW;\n\t}") != std::string::npos);
	REQUIRE(events.find("\t\t\t\tout->not_launched = 1;") != std::string::npos);
	// A bound object is held to the same test: a launch that made something
	// other than what its archive is wired for (a player firing the guards'
	// launcher makes no rocket) has no program either.
	REQUIRE(events.find("\t\t\t\tif (!projectileFunctionLaunches(bind->gunnum, bind->func,\n"
		"\t\t\t\t\t\tmade->weaponnum, made->gunfunc)) {\n"
		"\t\t\t\t\tout->not_launched = 1;") != std::string::npos);
	REQUIRE(events.find("\tif (who->not_launched) {\n\t\treturn NULL;\n\t}") != std::string::npos);

	// Slices 2, 4 and 6: the five arms that run a fuse ask the rule what goes
	// off (the countdown that the grenade, the grenade round and the N-bomb
	// share, the rocket's, the timed mine's, the remote mine's and the
	// proximity items'), and the rule fires the program's timer event, takes
	// its modules and counts parity.
	REQUIRE(countOf(propobj, "(fusemods = projectileFuseFor(obj, &fusetype)) != 0") == 5);
	REQUIRE(propobj.find("projectileFuseRule") == std::string::npos);
	REQUIRE(events.find("event.kind = MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED;") != std::string::npos);
	REQUIRE(events.find("\t\tif (bit == PROJECTILE_MODULE_EXPLODE) {") != std::string::npos);
	REQUIRE(events.find("return original; /* a v1 archive: the original fuse decides */") != std::string::npos);
	REQUIRE(events.find(" fuse.compared=%d fuse.differ=%d") != std::string::npos);

	// Step 4: every base projectile and entity archive is v2. Since slice 4
	// the fuse is part of the wiring the extractor writes from the rule
	// table (only for an object that has one), behind the weapon cache kind.
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	REQUIRE(countOf(extractor, "\"  \\\"schema\\\": \\\"pd.projectile_graph.v2\\\",\\n\"") == 1);
	REQUIRE(countOf(extractor, "\"  \\\"schema\\\": \\\"pd.entity_graph.v2\\\",\\n\"") == 1);
	REQUIRE(extractor.find("{ MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, \"fuse\" },") != std::string::npos);
	REQUIRE(extractor.find("og.projectile.fuse_expired") == std::string::npos);
	REQUIRE(countOf(extractor, "if (s_projectileRuleWiring(p, ") == 2);
	REQUIRE(extractor.find("_projref_fuseprogram_contactrules_devicerules_sentryrules_armrules_heldprograms_heldtriggers_heldshots_heldreloads_heldcharges_flightrules_heldlocks_heldrecoil\"") != std::string::npos);
}

// B-1265: a base throw's launch record carries placeholders for what a
// throw does not have ("timer60": -1, "reflect_angle": 0). Applied as
// authored values they wiped the thrown item's fuse (its activation time,
// set at the launch site just before) and its first-bounce boost.
TEST_CASE("a launch record's placeholders do not overwrite a throw's fuse and bounce",
	"[weapon][graph][projectile][static]")
{
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string bgun = readSource("src/game/bondgun.c");
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");

	const size_t apply = propobj.find("void projectileApplyGraphRuntime(struct defaultobj *obj,");
	REQUIRE(apply != std::string::npos);
	const std::string body = propobj.substr(apply, propobj.find("\n}\n", apply) - apply);
	CHECK(body.find("\t} else if (runtime->has_timer60 && runtime->timer60 >= 0) {\n") != std::string::npos);
	CHECK(body.find("\t} else if (runtime->has_timer60) {\n") == std::string::npos);
	CHECK(body.find("\tif (runtime->has_reflect_angle && runtime->reflect_angle > 0.0f) {\n") != std::string::npos);
	CHECK(body.find("\tif (runtime->has_reflect_angle) {\n") == std::string::npos);

	// The launch site sets the fuse and the boost, then applies the record.
	const size_t fuse = bgun.find("\t\t\t\tweaponobj->timer240 = activatetime60;");
	const size_t boost = bgun.find("\t\t\tobj->projectile->unk08c = 0.1f;\n"
		"\t\t\tobj->projectile->pickuptimer240 = TICKS(240);\n"
		"\t\t\tprojectileApplyGraphRuntime(obj, projectilegraph);");
	REQUIRE(fuse != std::string::npos);
	REQUIRE(boost != std::string::npos);
	CHECK(fuse < boost);

	// The extractor still writes the placeholders: a throw has no timer60
	// (it starts at -1) and no reflect angle (0).
	CHECK(extractor.find("\ts32 timer60 = -1;\n") != std::string::npos);
	CHECK(extractor.find("\tf32 reflectangle = 0.0f;\n") != std::string::npos);
	CHECK(extractor.find("\t\t\"        \\\"timer60\\\": %d,\\n\"\n") != std::string::npos);
	CHECK(extractor.find("\t\t\"        \\\"reflect_angle\\\": %.7g,\\n\"\n") != std::string::npos);
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
