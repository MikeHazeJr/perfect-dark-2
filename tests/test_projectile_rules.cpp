/*
 * test_projectile_rules.cpp -- the modules of a projectile program and the
 * original game's contact rules (port/src/projectile_rules.c;
 * context/designs/modding/projectile-events.md, slice 3).
 *
 * The table is the one source the game's seams, the extractor's wiring and
 * the archive validator share, so these cases pin it three ways: its own
 * answers, a program compiled and fired through the graph runner with the
 * same module and gate kinds, and the source text of the places that must
 * agree with it (propobj.c's seams, bondgun.c's launched objects, the
 * validator's lists).
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "constants.h"
#include "mission_graph_runner.h"
#include "projectile_rules.h"

#include <utility>

namespace {

std::string readSource(const char *relative)
{
	std::ifstream in(std::string(PD_SOURCE_DIR) + "/" + relative, std::ios::binary);
	std::ostringstream out;
	out << in.rdbuf();
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

s32 moduleKnown(const char *kind)
{
	return kind && (projectileRulesModuleBit(kind) != 0 ||
		std::string(kind) == PROJECTILE_GATE_FUSE_ARMED_KIND) ? 1 : 0;
}

struct Choice {
	u32 modules = 0;
	bool armed = false;
};

s32 runModule(const char *kind, const char *, const mission_graph_params_t *,
	const mission_graph_event_t *, void *user)
{
	Choice *choice = static_cast<Choice *>(user);
	if (std::string(kind) == PROJECTILE_GATE_FUSE_ARMED_KIND) {
		return choice->armed ? 1 : 0;
	}
	const u32 bit = projectileRulesModuleBit(kind);
	choice->modules |= bit;
	return bit ? 1 : 0;
}

} // namespace

TEST_CASE("projectile rules: every module has one kind and one bit", "[projectile][rules]")
{
	u32 seen = 0;
	for (s32 m = 0; m < PROJECTILE_MODULE_COUNT; m++) {
		const u32 bit = 1u << m;
		const char *kind = projectileRulesModuleKind(bit);
		REQUIRE(kind != nullptr);
		CHECK(std::string(kind).rfind("og.projectile.", 0) == 0);
		CHECK(projectileRulesModuleBit(kind) == bit);
		seen |= bit;
	}
	CHECK(seen == PROJECTILE_MODULE_ALL);
	CHECK(projectileRulesModuleKind(0) == nullptr);
	CHECK(projectileRulesModuleKind(PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_PIERCE) == nullptr);
	CHECK(projectileRulesModuleKind(1u << PROJECTILE_MODULE_COUNT) == nullptr);
	CHECK(projectileRulesModuleBit("og.projectile.stick") == PROJECTILE_MODULE_STICK);
	CHECK(projectileRulesModuleBit("og.projectile.explode") == 0);
	CHECK(projectileRulesModuleBit(PROJECTILE_GATE_FUSE_ARMED_KIND) == 0);
	CHECK(projectileRulesModuleBit(nullptr) == 0);
}

TEST_CASE("projectile rules: the events and the modules each applies", "[projectile][rules]")
{
	s32 count = 0;
	s32 previous = 0;
	for (s32 i = 0; projectileRulesEventAt(i) >= 0; i++) {
		const s32 kind = projectileRulesEventAt(i);
		CHECK(kind > previous); // in kind order, none twice
		previous = kind;
		REQUIRE(projectileRulesEventName(kind) != nullptr);
		CHECK(std::string(projectileRulesEventName(kind)).rfind("event.projectile.", 0) == 0);
		CHECK(missionGraphEventIsProjectile(kind) == 1);
		CHECK((projectileRulesEventModules(kind) & ~PROJECTILE_MODULE_ALL) == 0);
		count++;
	}
	CHECK(count == 10);
	CHECK(projectileRulesEventAt(-1) == -1);
	CHECK(projectileRulesEventName(MISSION_GRAPH_EVENT_PICKUP_TOUCHED) == nullptr);
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PICKUP_TOUCHED) == 0);

	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) ==
		PROJECTILE_MODULE_FUSE_EXPIRED);
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_CONTACT) ==
		(PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_WALL_HUG | PROJECTILE_MODULE_PIERCE |
			PROJECTILE_MODULE_IMPACT));
	for (s32 kind : { MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED, MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED,
			MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST }) {
		CHECK(projectileRulesEventModules(kind) == PROJECTILE_MODULE_SPEND_FUSE);
	}
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_LANDED) ==
		(PROJECTILE_MODULE_SHIELD_DEVICE | PROJECTILE_MODULE_LAND_BOLT | PROJECTILE_MODULE_LAND_KNIFE |
			PROJECTILE_MODULE_POISON_TARGET));
	// The records apply nothing.
	for (s32 kind : { MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED, MISSION_GRAPH_EVENT_PROJECTILE_HIT_TARGET,
			MISSION_GRAPH_EVENT_PROJECTILE_ENDED, MISSION_GRAPH_EVENT_PROJECTILE_DETONATED }) {
		CHECK(projectileRulesEventModules(kind) == 0);
	}
	// What the original chooses at an event is always something that event applies.
	for (s32 i = 0; projectileRulesEventAt(i) >= 0; i++) {
		const s32 kind = projectileRulesEventAt(i);
		for (s32 weaponnum = 0; weaponnum < 0x60; weaponnum++) {
			for (s32 func = FUNC_PRIMARY; func <= FUNC_2; func++) {
				for (s32 sticks = 0; sticks <= 1; sticks++) {
					const u32 native = projectileRulesNative(kind, weaponnum, func, sticks) &
						PROJECTILE_MODULE_ALL;
					CHECK((native & ~projectileRulesEventModules(kind)) == 0);
				}
			}
		}
	}
}

TEST_CASE("projectile rules: the original game's contact rules by weapon", "[projectile][rules]")
{
	const auto at = [](s32 kind, s32 weaponnum, s32 func, s32 sticks = 0) {
		return projectileRulesNative(kind, weaponnum, func, sticks);
	};
	const s32 contact = MISSION_GRAPH_EVENT_PROJECTILE_CONTACT;
	const s32 landed = MISSION_GRAPH_EVENT_PROJECTILE_LANDED;
	const s32 rest = MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST;

	// Mines and planted devices stick; four of them cannot be shot away.
	for (s32 weaponnum : { WEAPON_REMOTEMINE, WEAPON_TIMEDMINE, WEAPON_PROXIMITYMINE }) {
		CHECK(at(contact, weaponnum, FUNC_PRIMARY) == PROJECTILE_MODULE_STICK);
		CHECK(at(landed, weaponnum, FUNC_PRIMARY) == 0);
	}
	for (s32 weaponnum : { WEAPON_ECMMINE, WEAPON_COMMSRIDER, WEAPON_TRACERBUG, WEAPON_TARGETAMPLIFIER }) {
		CHECK(at(contact, weaponnum, FUNC_PRIMARY) == PROJECTILE_MODULE_STICK);
		CHECK(at(landed, weaponnum, FUNC_PRIMARY) == PROJECTILE_MODULE_SHIELD_DEVICE);
	}
	// A bolt and a thrown knife stick and pierce; each lands its own way,
	// and the knife poisons.
	CHECK(at(contact, WEAPON_BOLT, FUNC_PRIMARY) == (PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_PIERCE));
	CHECK(at(contact, WEAPON_BOLT, FUNC_SECONDARY) == (PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_PIERCE));
	CHECK(at(contact, WEAPON_COMBATKNIFE, FUNC_SECONDARY) ==
		(PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_PIERCE));
	CHECK(at(landed, WEAPON_BOLT, FUNC_PRIMARY) == PROJECTILE_MODULE_LAND_BOLT);
	CHECK(at(landed, WEAPON_COMBATKNIFE, FUNC_SECONDARY) ==
		(PROJECTILE_MODULE_LAND_KNIFE | PROJECTILE_MODULE_POISON_TARGET));
	// A rocket is consumed by a target and spent by anything else it strikes.
	for (s32 weaponnum : { WEAPON_ROCKET, WEAPON_HOMINGROCKET }) {
		CHECK(at(contact, weaponnum, FUNC_PRIMARY) == PROJECTILE_MODULE_IMPACT);
		CHECK(at(MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED, weaponnum, FUNC_PRIMARY) ==
			PROJECTILE_MODULE_SPEND_FUSE);
		CHECK(at(MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED, weaponnum, FUNC_PRIMARY) ==
			PROJECTILE_MODULE_SPEND_FUSE);
		CHECK(at(rest, weaponnum, FUNC_PRIMARY) == 0);
	}
	// The Devastator's wall hugger sticks through its function flag and
	// latches its wall timer; its plain round does not stick.
	CHECK(at(contact, WEAPON_GRENADEROUND, FUNC_SECONDARY, 1) == PROJECTILE_MODULE_WALL_HUG);
	CHECK(at(contact, WEAPON_GRENADEROUND, FUNC_SECONDARY, 0) == 0);
	CHECK(at(contact, WEAPON_GRENADEROUND, FUNC_PRIMARY, 0) == 0);
	// Any other function flagged to stick to walls sticks.
	CHECK(at(contact, WEAPON_GRENADE, FUNC_PRIMARY, 1) == PROJECTILE_MODULE_STICK);
	// A grenade round goes off where it comes to rest, whatever fired it; the
	// N-bomb's first function only once its fuse is armed; a hand grenade
	// and the N-bomb's second function wait for their fuse.
	for (s32 func : { FUNC_PRIMARY, FUNC_SECONDARY, FUNC_2 }) {
		CHECK(at(rest, WEAPON_GRENADEROUND, func) == PROJECTILE_MODULE_SPEND_FUSE);
	}
	CHECK(at(rest, WEAPON_NBOMB, FUNC_PRIMARY) ==
		(PROJECTILE_MODULE_SPEND_FUSE | PROJECTILE_RULE_GATE_FUSE_ARMED));
	CHECK(at(rest, WEAPON_NBOMB, FUNC_SECONDARY) == 0);
	CHECK(at(rest, WEAPON_GRENADE, FUNC_PRIMARY) == 0);
	// A hand grenade, a Dragon and a dropped gun bounce: no contact rule.
	for (s32 weaponnum : { WEAPON_GRENADE, WEAPON_NBOMB, WEAPON_DRAGON, WEAPON_FALCON2 }) {
		CHECK(at(contact, weaponnum, FUNC_PRIMARY) == 0);
		CHECK(at(MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED, weaponnum, FUNC_PRIMARY) == 0);
		CHECK(at(landed, weaponnum, FUNC_PRIMARY) == 0);
	}
	// Every weapon's fuse is its own.
	CHECK(at(MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, WEAPON_GRENADE, FUNC_PRIMARY) ==
		PROJECTILE_MODULE_FUSE_EXPIRED);
	// The records choose nothing.
	CHECK(at(MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED, WEAPON_ROCKET, FUNC_PRIMARY) == 0);
	CHECK(at(MISSION_GRAPH_EVENT_PROJECTILE_HIT_TARGET, WEAPON_BOLT, FUNC_PRIMARY) == 0);
}

TEST_CASE("projectile rules: the object a weapon function launches", "[projectile][rules]")
{
	const auto launched = [](s32 gunnum, s32 func, bool thrown, bool homing) {
		s32 weaponnum = -2;
		s32 gunfunc = -2;
		projectileRulesLaunchedObject(gunnum, func, thrown, homing, &weaponnum, &gunfunc);
		return std::pair<s32, s32>(weaponnum, gunfunc);
	};
	using Pair = std::pair<s32, s32>;

	CHECK(launched(WEAPON_ROCKETLAUNCHER, FUNC_PRIMARY, false, false) == Pair(WEAPON_ROCKET, FUNC_PRIMARY));
	CHECK(launched(WEAPON_ROCKETLAUNCHER, FUNC_SECONDARY, false, true) ==
		Pair(WEAPON_HOMINGROCKET, FUNC_PRIMARY));
	CHECK(launched(WEAPON_SLAYER, FUNC_SECONDARY, false, false) == Pair(WEAPON_ROCKET, FUNC_PRIMARY));
	// The guards' launcher: a character's shot makes a rocket of it too.
	CHECK(launched(WEAPON_ROCKETLAUNCHER_34, FUNC_PRIMARY, false, false) == Pair(WEAPON_ROCKET, FUNC_PRIMARY));
	CHECK(launched(WEAPON_CROSSBOW, FUNC_SECONDARY, false, false) == Pair(WEAPON_BOLT, FUNC_SECONDARY));
	CHECK(launched(WEAPON_DEVASTATOR, FUNC_SECONDARY, false, false) ==
		Pair(WEAPON_GRENADEROUND, FUNC_SECONDARY));
	CHECK(launched(WEAPON_SUPERDRAGON, FUNC_SECONDARY, false, false) == Pair(WEAPON_GRENADEROUND, FUNC_2));
	// Anything else fired keeps the gun's weapon and function; a thrown item
	// is itself; the thrown Laptop gun is no weapon object.
	CHECK(launched(WEAPON_BOLT, FUNC_PRIMARY, false, false) == Pair(WEAPON_BOLT, FUNC_PRIMARY));
	CHECK(launched(WEAPON_GRENADE, FUNC_SECONDARY, true, false) == Pair(WEAPON_GRENADE, FUNC_SECONDARY));
	CHECK(launched(WEAPON_COMBATKNIFE, FUNC_SECONDARY, true, false) ==
		Pair(WEAPON_COMBATKNIFE, FUNC_SECONDARY));
	CHECK(launched(WEAPON_LAPTOPGUN, FUNC_SECONDARY, true, false).first == -1);
	projectileRulesLaunchedObject(WEAPON_CROSSBOW, FUNC_PRIMARY, 0, 0, nullptr, nullptr); // no crash

	// bondgun.c is where a fired projectile gets its own weapon number.
	const std::string bgun = readSource("src/game/bondgun.c");
	REQUIRE(bgun.find("} else if (hand->gset.weaponnum == WEAPON_ROCKETLAUNCHER || hand->gset.weaponnum == WEAPON_SLAYER) {")
		!= std::string::npos);
	REQUIRE(bgun.find("weapon = weaponCreateProjectileFromWeaponNum(projectilemodelnum, WEAPON_BOLT, g_Vars.currentplayer->prop->chr);")
		!= std::string::npos);
	REQUIRE(countOf(bgun, "weapon = weaponCreateProjectileFromWeaponNum(projectilemodelnum, WEAPON_GRENADEROUND, g_Vars.currentplayer->prop->chr);")
		== 2);
	REQUIRE(bgun.find("\t\t\t\t\tweapon->gunfunc = FUNC_2;") != std::string::npos);
	// A character's shot makes the same objects, and a rocket of the
	// guards' launcher as well.
	const std::string chraction = readSource("src/game/chraction.c");
	REQUIRE(chraction.find("\t\t\t\t\t\tif (gset.weaponnum == WEAPON_ROCKETLAUNCHER\n"
		"\t\t\t\t\t\t\t\t|| gset.weaponnum == WEAPON_ROCKETLAUNCHER_34\n"
		"\t\t\t\t\t\t\t\t|| gset.weaponnum == WEAPON_SLAYER) {") != std::string::npos);
	REQUIRE(chraction.find("projectileobj = weaponCreateProjectileFromWeaponNum(projectilemodelnum, WEAPON_BOLT, chr);")
		!= std::string::npos);
	REQUIRE(countOf(chraction, "projectileobj = weaponCreateProjectileFromWeaponNum(projectilemodelnum, WEAPON_GRENADEROUND, chr);")
		== 2);
}

TEST_CASE("projectile rules: a program chooses modules through the graph runner", "[projectile][rules][graph]")
{
	// A bolt's wiring, a rocket's, and the N-bomb's gated rest rule, as the
	// extractor writes them.
	const std::string graph = R"({
  "schema": "pd.projectile_graph.v2",
  "nodes": [
    { "id": "motion", "kind": "projectile.motion", "params": {} },
    { "id": "contact", "kind": "event.projectile.contact" },
    { "id": "contact_stick", "kind": "og.projectile.stick" },
    { "id": "contact_pierce", "kind": "og.projectile.pierce" },
    { "id": "bounced", "kind": "event.projectile.bounced" },
    { "id": "bounced_spend_fuse", "kind": "og.projectile.spend_fuse" },
    { "id": "came_to_rest", "kind": "event.projectile.came_to_rest" },
    { "id": "came_to_rest_fuse_armed", "kind": "gate.projectile.fuse_armed" },
    { "id": "came_to_rest_spend_fuse", "kind": "og.projectile.spend_fuse" },
    { "id": "launch_blocked", "kind": "event.projectile.launch_blocked" }
  ],
  "edges": [
    { "from": "contact", "output": "out", "to": "contact_stick", "input": "exec" },
    { "from": "contact", "output": "out", "to": "contact_pierce", "input": "exec" },
    { "from": "bounced", "output": "out", "to": "bounced_spend_fuse", "input": "exec" },
    { "from": "came_to_rest", "output": "out", "to": "came_to_rest_fuse_armed", "input": "exec" },
    { "from": "came_to_rest_fuse_armed", "output": "pass", "to": "came_to_rest_spend_fuse", "input": "exec" }
  ]
})";
	char err[256] = "";
	mission_graph_program_t *program = missionGraphProgramCompileProjectile(graph.data(), graph.size(),
		moduleKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_CONTACT) == 1);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST) == 1);
	// An event node with no execution edge handles nothing.
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED) == 0);
	CHECK(missionGraphProgramHandlesEvent(program, MISSION_GRAPH_EVENT_PROJECTILE_LANDED) == 0);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST,
		"og.projectile.spend_fuse") == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_CONTACT,
		"og.projectile.spend_fuse") == 0);

	int instance = 0;
	const auto fire = [&](s32 kind, bool armed) {
		mission_graph_event_t event = {};
		event.kind = kind;
		event.instance = &instance;
		Choice choice;
		choice.armed = armed;
		CHECK(missionGraphProgramFire(program, &event, runModule, &choice) >= 0);
		return choice.modules;
	};
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, false) ==
		(PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_PIERCE));
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED, false) == PROJECTILE_MODULE_SPEND_FUSE);
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST, false) == 0);
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST, true) == PROJECTILE_MODULE_SPEND_FUSE);
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_LANDED, false) == 0);
	missionGraphProgramFree(program);

	// The contact events are projectile events: a mission or level graph
	// that names one is refused, and a projectile program takes no other.
	const std::string mission = R"({ "nodes": [ { "id": "c", "kind": "event.projectile.contact" } ], "edges": [] })";
	CHECK(missionGraphProgramCompile(mission.data(), mission.size(), moduleKnown, err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("belongs to a projectile or entity graph") != std::string::npos);
	CHECK(missionGraphProgramCompileLevel(mission.data(), mission.size(), moduleKnown, err, sizeof(err)) == nullptr);
	const std::string other = R"({ "nodes": [ { "id": "d", "kind": "event.door.use" } ], "edges": [] })";
	CHECK(missionGraphProgramCompileProjectile(other.data(), other.size(), moduleKnown, err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("is not a projectile event") != std::string::npos);
}

TEST_CASE("projectile rules: the seams, the extractor and the validator read the table",
	"[projectile][rules][static]")
{
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string events = readSource("port/src/projectile_events.c");
	const std::string runtime = readSource("port/src/weapon_graph_runtime.c");
	const std::string extractor = readSource("port/src/romextract_pdweapon.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// One helper asks the rule at every seam: the original rules go in as
	// the answer for a projectile with no program and as the parity reference.
	REQUIRE(propobj.find("static u32 projectileRuleFor(s32 kind, struct defaultobj *obj)\n{") != std::string::npos);
	REQUIRE(propobj.find("\tnative = projectileRulesNative(kind, weapon->weaponnum, weapon->gunfunc,\n"
		"\t\tgsetHasFunctionFlags(&weapon->gset, FUNCFLAG_STICKTOWALL));") != std::string::npos);
	REQUIRE(propobj.find("\treturn projectileRuleModules(kind, obj, native);\n}") != std::string::npos);
	REQUIRE(countOf(propobj, "projectileRuleFor(MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, obj)") == 1);
	REQUIRE(countOf(propobj, "projectileRuleFor(MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED, obj)") == 1);
	REQUIRE(countOf(propobj, "projectileRuleFor(MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED, obj)") == 1);
	REQUIRE(countOf(propobj, "projectileRuleFor(MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST, obj)") == 1);
	REQUIRE(countOf(propobj, "projectileRuleFor(MISSION_GRAPH_EVENT_PROJECTILE_LANDED, obj)") == 1);

	// The contact block asks no weapon number any more: from the contact
	// rule to the end of the stick commit, only the modules decide.
	const size_t contact = propobj.find("contactmods = projectileRuleFor(MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, obj);");
	const size_t commitEnd = propobj.find("bgunModProjectileLanded((struct weaponobj *) obj);", contact);
	REQUIRE(contact != std::string::npos);
	REQUIRE(commitEnd != std::string::npos);
	const std::string block = propobj.substr(contact, commitEnd - contact);
	CHECK(block.find("weaponnum == WEAPON_") == std::string::npos);
	CHECK(block.find("if (contactmods & (PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_WALL_HUG)) {") != std::string::npos);
	CHECK(block.find("if (contactmods & PROJECTILE_MODULE_WALL_HUG) {") != std::string::npos);
	CHECK(countOf(block, "contactmods & PROJECTILE_MODULE_PIERCE") == 4);
	CHECK(block.find("} else if ((contactmods & PROJECTILE_MODULE_IMPACT) || impactgraph != NULL) {") != std::string::npos);

	// The landing likewise, up to its sounds.
	const size_t land = propobj.find("void objLand(struct prop *prop, struct coord *arg1, struct coord *arg2, bool *embedded)");
	REQUIRE(land != std::string::npos);
	const std::string landing = propobj.substr(land, propobj.find("\n}\n", land) - land);
	CHECK(landing.find("weaponnum == WEAPON_") == std::string::npos);
	CHECK(landing.find("if (landedmods & PROJECTILE_MODULE_SHIELD_DEVICE) {") != std::string::npos);
	CHECK(landing.find("if (landedmods & PROJECTILE_MODULE_LAND_BOLT) {") != std::string::npos);
	CHECK(landing.find("} else if (landedmods & PROJECTILE_MODULE_LAND_KNIFE) {") != std::string::npos);
	CHECK(landing.find("if ((landedmods & PROJECTILE_MODULE_POISON_TARGET)") != std::string::npos);

	// The fuse is spent by the bounce and came_to_rest rules; the rocket and
	// grenade round branches that remain are their trails.
	CHECK(propobj.find("\t\t\t\t\t\tif (bouncedmods & PROJECTILE_MODULE_SPEND_FUSE) {\n"
		"\t\t\t\t\t\t\tweapon->timer240 = 0;\n\t\t\t\t\t\t}") != std::string::npos);
	CHECK(propobj.find("\t\t\t\t\t\tif (atrest && (projectileRuleFor(MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST, obj)\n"
		"\t\t\t\t\t\t\t\t& PROJECTILE_MODULE_SPEND_FUSE)) {\n\t\t\t\t\t\t\tweapon->timer240 = 0;") != std::string::npos);
	CHECK(propobj.find("weapon->weaponnum == WEAPON_NBOMB && weapon->gunfunc == FUNC_PRIMARY)) {\n"
		"\t\t\t\t\t\t\tif (sp350") == std::string::npos);

	// The rule: the program chooses, a module its event does not apply is
	// dropped, the host counts parity.
	REQUIRE(events.find("u32 projectileRuleModules(s32 kind, struct defaultobj *obj, u32 native)\n{") != std::string::npos);
	REQUIRE(events.find("\t\treturn native; /* a v1 archive: the original rules decide */") != std::string::npos);
	REQUIRE(events.find("\t\trule.modules &= projectileRulesEventModules(kind);") != std::string::npos);
	REQUIRE(events.find("\tif (g_NetMode != NETMODE_CLIENT && (handled || native)) {") != std::string::npos);
	REQUIRE(events.find("\"PROJECTILE.RULE differs: %s weapon=%s func=%s program=%s modules=%s native=%s\"") != std::string::npos);
	REQUIRE(events.find(" fuse.compared=%d fuse.differ=%d rules.compared=%d rules.differ=%d") != std::string::npos);
	REQUIRE(events.find("if (strcmp(kind, PROJECTILE_GATE_FUSE_ARMED_KIND) == 0) {") != std::string::npos);

	// Registration takes the table's modules and gate and refuses a module
	// wired where it would do nothing.
	REQUIRE(runtime.find("\treturn projectileRulesModuleBit(kind) != 0 ||\n"
		"\t\tstrcmp(kind, PROJECTILE_GATE_FUSE_ARMED_KIND) == 0;") != std::string::npos);
	REQUIRE(runtime.find("snprintf(why, why_cap, \"%s has no effect on %s\", module_kind,") != std::string::npos);
	REQUIRE(runtime.find("\tif (!projectileProgramModulesApply(program, why, sizeof(why))) {") != std::string::npos);

	// The extractor writes the wiring from the table, for the object the
	// function launches, behind the weapon cache kind.
	REQUIRE(extractor.find("static s32 s_projectileRuleWiring(const pdweapon_nested_payload_t *p,") != std::string::npos);
	REQUIRE(extractor.find("u32 modules = projectileRulesNative(events[i].kind, objweapon, objfunc, sticks);") != std::string::npos);
	REQUIRE(extractor.find("\tprojectileRulesLaunchedObject(p->weaponnum, p->mode_index,") != std::string::npos);
	REQUIRE(extractor.find("\t\tpayload_plan.payloads[i].weaponnum = weapon_id;") != std::string::npos);
	REQUIRE(extractor.find("_projref_fuseprogram_contactrules\"") != std::string::npos);

	// The validator knows every event, module and gate of the table, and
	// which modules each event applies.
	for (s32 i = 0; projectileRulesEventAt(i) >= 0; i++) {
		const std::string name = projectileRulesEventName(projectileRulesEventAt(i));
		INFO(name);
		CHECK(conformance.find("\"" + name + "\"") != std::string::npos);
	}
	for (s32 m = 0; m < PROJECTILE_MODULE_COUNT; m++) {
		const std::string kind = projectileRulesModuleKind(1u << m);
		INFO(kind);
		CHECK(conformance.find("\"" + kind + "\"") != std::string::npos);
	}
	CHECK(conformance.find("PROJECTILE_GRAPH_GATES = (\"gate.objective.status\", \"gate.mission.phase\",\n"
		"                          \"gate.projectile.fuse_armed\")") != std::string::npos);
	CHECK(conformance.find("PROJECTILE_GRAPH_EVENT_MODULES = {") != std::string::npos);
}
